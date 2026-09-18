#include "tcpch.h"
#include "TomCat/Scene/Advanced2D.h"

#include <algorithm>
#include <cmath>
#include <unordered_map>

namespace TomCat {

	namespace {
		bool CoordinateLess(const TilemapCell& left, const TilemapCell& right)
		{
			return left.Coordinate.y != right.Coordinate.y
				? left.Coordinate.y < right.Coordinate.y
				: left.Coordinate.x < right.Coordinate.x;
		}

		uint64_t CoordinateKey(glm::ivec2 coordinate)
		{
			return (static_cast<uint64_t>(static_cast<uint32_t>(coordinate.x)) << 32)
				| static_cast<uint32_t>(coordinate.y);
		}

		uint32_t NextRandom(uint32_t& state)
		{
			if (state == 0)
				state = 0x6d2b79f5u;
			state ^= state << 13;
			state ^= state >> 17;
			state ^= state << 5;
			return state;
		}

		float RandomUnit(uint32_t& state)
		{
			return static_cast<float>(NextRandom(state) >> 8)
				* (1.0f / 16777216.0f);
		}
	}

	namespace Tilemap2DRuntime {
		void Normalize(Tilemap2D& tilemap)
		{
			std::unordered_map<uint64_t, size_t> lastIndex;
			lastIndex.reserve(tilemap.Cells.size());
			for (size_t i = 0; i < tilemap.Cells.size(); ++i)
				lastIndex[CoordinateKey(tilemap.Cells[i].Coordinate)] = i;

			std::vector<TilemapCell> normalized;
			normalized.reserve(lastIndex.size());
			for (size_t i = 0; i < tilemap.Cells.size(); ++i)
			{
				const TilemapCell& cell = tilemap.Cells[i];
				if (lastIndex[CoordinateKey(cell.Coordinate)] == i)
				{
					TilemapCell copy = cell;
					copy.RotationQuarterTurns = ((copy.RotationQuarterTurns % 4) + 4) % 4;
					normalized.push_back(std::move(copy));
				}
			}
			std::sort(normalized.begin(), normalized.end(), CoordinateLess);
			tilemap.Cells = std::move(normalized);
		}

		const TilemapCell* FindCell(const Tilemap2D& tilemap, glm::ivec2 coordinate)
		{
			const auto found = std::lower_bound(tilemap.Cells.begin(), tilemap.Cells.end(),
				TilemapCell{ coordinate }, CoordinateLess);
			return found != tilemap.Cells.end() && found->Coordinate == coordinate
				? &*found : nullptr;
		}

		bool SetCell(Tilemap2D& tilemap, TilemapCell cell)
		{
			cell.RotationQuarterTurns = ((cell.RotationQuarterTurns % 4) + 4) % 4;
			auto found = std::lower_bound(tilemap.Cells.begin(), tilemap.Cells.end(),
				cell, CoordinateLess);
			if (found != tilemap.Cells.end() && found->Coordinate == cell.Coordinate)
			{
				if (found->SpriteHandle == cell.SpriteHandle && found->Tint == cell.Tint
					&& found->FlipX == cell.FlipX && found->FlipY == cell.FlipY
					&& found->RotationQuarterTurns == cell.RotationQuarterTurns)
					return false;
				*found = std::move(cell);
				return true;
			}
			tilemap.Cells.insert(found, std::move(cell));
			return true;
		}

		bool EraseCell(Tilemap2D& tilemap, glm::ivec2 coordinate)
		{
			auto found = std::lower_bound(tilemap.Cells.begin(), tilemap.Cells.end(),
				TilemapCell{ coordinate }, CoordinateLess);
			if (found == tilemap.Cells.end() || found->Coordinate != coordinate)
				return false;
			tilemap.Cells.erase(found);
			return true;
		}

		glm::mat4 GetCellTransform(const Tilemap2D& tilemap,
			const TilemapCell& cell)
		{
			const glm::vec2 stride = tilemap.CellSize + tilemap.CellGap;
			glm::mat4 result = glm::translate(glm::mat4(1.0f), glm::vec3(
				static_cast<float>(cell.Coordinate.x) * stride.x,
				static_cast<float>(cell.Coordinate.y) * stride.y, 0.0f));
			result = glm::rotate(result,
				glm::radians(90.0f * static_cast<float>(cell.RotationQuarterTurns)),
				glm::vec3(0.0f, 0.0f, 1.0f));
			const glm::vec2 signedSize(
				cell.FlipX ? -tilemap.CellSize.x : tilemap.CellSize.x,
				cell.FlipY ? -tilemap.CellSize.y : tilemap.CellSize.y);
			return glm::scale(result, glm::vec3(signedSize, 1.0f));
		}
	}

	namespace ParticleSystem2DRuntime {
		void Reset(ParticleSystem2D& system)
		{
			system.RuntimePlaying = false;
			system.RuntimeInitialized = false;
			system.RuntimeTime = 0.0f;
			system.RuntimeEmissionAccumulator = 0.0f;
			system.RuntimeRandomState = system.Seed == 0 ? 1u : system.Seed;
			system.RuntimeParticles.clear();
		}

		void Play(ParticleSystem2D& system, bool restart)
		{
			if (restart)
			{
				system.RuntimeTime = 0.0f;
				system.RuntimeEmissionAccumulator = 0.0f;
				system.RuntimeRandomState = system.Seed == 0 ? 1u : system.Seed;
				system.RuntimeParticles.clear();
			}
			system.RuntimeInitialized = true;
			system.RuntimePlaying = true;
		}

		void Stop(ParticleSystem2D& system, bool clear)
		{
			system.RuntimePlaying = false;
			if (clear)
				system.RuntimeParticles.clear();
		}

		void Update(ParticleSystem2D& system, float deltaSeconds)
		{
			if (!system.Enabled || !std::isfinite(deltaSeconds) || deltaSeconds <= 0.0f)
				return;
			if (!system.RuntimeInitialized)
			{
				system.RuntimeInitialized = true;
				system.RuntimeRandomState = system.Seed == 0 ? 1u : system.Seed;
				system.RuntimePlaying = system.PlayOnStart;
			}

			for (Particle2D& particle : system.RuntimeParticles)
			{
				particle.Age += deltaSeconds;
				particle.Velocity.y += -9.81f * system.GravityScale * deltaSeconds;
				particle.Position += particle.Velocity * deltaSeconds;
			}
			std::erase_if(system.RuntimeParticles, [](const Particle2D& particle)
				{ return particle.Age >= particle.Lifetime; });

			if (!system.RuntimePlaying)
				return;
			float emissionDelta = deltaSeconds;
			if (!system.Loop)
			{
				emissionDelta = std::min(deltaSeconds,
					std::max(system.Duration - system.RuntimeTime, 0.0f));
				system.RuntimeTime += deltaSeconds;
				if (emissionDelta <= 0.0f)
				{
					system.RuntimePlaying = false;
					return;
				}
			}
			else
			{
				system.RuntimeTime += deltaSeconds;
			}
			if (system.Loop && system.Duration > 0.0f)
				system.RuntimeTime = std::fmod(system.RuntimeTime, system.Duration);

			const int32_t capacity = std::max(system.MaxParticles, 0);
			const float emissionRate = std::max(system.EmissionRate, 0.0f);
			system.RuntimeEmissionAccumulator += emissionRate * emissionDelta;
			int32_t emitCount = static_cast<int32_t>(
				std::floor(system.RuntimeEmissionAccumulator));
			system.RuntimeEmissionAccumulator -= static_cast<float>(emitCount);
			emitCount = std::min(emitCount, capacity
				- static_cast<int32_t>(system.RuntimeParticles.size()));

			glm::vec2 direction = system.Direction;
			const float length = glm::length(direction);
			direction = length > 0.00001f ? direction / length : glm::vec2(0.0f, 1.0f);
			const float baseAngle = std::atan2(direction.y, direction.x);
			const float spread = glm::radians(std::max(system.SpreadDegrees, 0.0f));
			for (int32_t i = 0; i < emitCount; ++i)
			{
				const float angle = baseAngle
					+ (RandomUnit(system.RuntimeRandomState) - 0.5f) * spread;
				Particle2D particle;
				particle.Velocity = glm::vec2(std::cos(angle), std::sin(angle))
					* std::max(system.StartSpeed, 0.0f);
				particle.Lifetime = std::max(system.StartLifetime, 0.0001f);
				particle.StartSize = std::max(system.StartSize, 0.0f);
				particle.EndSize = std::max(system.EndSize, 0.0f);
				system.RuntimeParticles.push_back(particle);
			}
			if (!system.Loop && system.RuntimeTime >= system.Duration)
				system.RuntimePlaying = false;
		}

		glm::vec4 EvaluateColor(const ParticleSystem2D& system,
			const Particle2D& particle)
		{
			const float normalizedAge = glm::clamp(
				particle.Age / std::max(particle.Lifetime, 0.0001f), 0.0f, 1.0f);
			return glm::mix(system.StartColor, system.EndColor, normalizedAge);
		}

		float EvaluateSize(const Particle2D& particle)
		{
			const float normalizedAge = glm::clamp(
				particle.Age / std::max(particle.Lifetime, 0.0001f), 0.0f, 1.0f);
			return glm::mix(particle.StartSize, particle.EndSize, normalizedAge);
		}
	}

	namespace Light2DRuntime {
		float EvaluateAttenuation(float distance, float radius, float falloff)
		{
			if (!std::isfinite(distance) || !std::isfinite(radius)
				|| !std::isfinite(falloff) || radius <= 0.0f)
				return 0.0f;
			const float linear = glm::clamp(1.0f - std::max(distance, 0.0f) / radius,
				0.0f, 1.0f);
			return std::pow(linear, std::max(falloff, 0.0001f));
		}
	}

}
