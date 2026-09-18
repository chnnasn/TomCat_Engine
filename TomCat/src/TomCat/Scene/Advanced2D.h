#pragma once

#include "TomCat/Scene/Components.h"

#include <cstddef>

namespace TomCat {

	namespace Tilemap2DRuntime {
		// Restores the canonical row-major representation and removes duplicate
		// coordinates. The last authored value wins, which matches painting.
		void Normalize(Tilemap2D& tilemap);
		const TilemapCell* FindCell(const Tilemap2D& tilemap, glm::ivec2 coordinate);
		bool SetCell(Tilemap2D& tilemap, TilemapCell cell);
		bool EraseCell(Tilemap2D& tilemap, glm::ivec2 coordinate);
		glm::mat4 GetCellTransform(const Tilemap2D& tilemap,
			const TilemapCell& cell);
	}

	namespace ParticleSystem2DRuntime {
		void Reset(ParticleSystem2D& system);
		void Play(ParticleSystem2D& system, bool restart = true);
		void Stop(ParticleSystem2D& system, bool clear = false);
		void Update(ParticleSystem2D& system, float deltaSeconds);
		glm::vec4 EvaluateColor(const ParticleSystem2D& system,
			const Particle2D& particle);
		float EvaluateSize(const Particle2D& particle);
	}

	namespace Light2DRuntime {
		float EvaluateAttenuation(float distance, float radius, float falloff);
	}

}
