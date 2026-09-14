#include "tcpch.h"
#include "AudioSceneRuntime.h"

#include "TomCat/Core/Log.h"
#include "TomCat/Scene/Components.h"
#include "TomCat/Scene/Entity.h"
#include "TomCat/Scene/Scene.h"

#include <algorithm>
#include <cmath>
#include <vector>

namespace TomCat {

	namespace {

		struct DeferredVoiceDestroy
		{
			Scene* Owner = nullptr;
			AudioVoiceHandle Voice = 0;
		};

		std::vector<DeferredVoiceDestroy> DeferredVoiceDestroys;

		void FlushDeferredVoiceDestroys(Scene& scene)
		{
			AudioEngine& engine = AudioEngine::Get();
			for (auto iterator = DeferredVoiceDestroys.begin();
				iterator != DeferredVoiceDestroys.end();)
			{
				if (iterator->Owner != &scene)
				{
					++iterator;
					continue;
				}
				if (iterator->Voice != 0 && engine.HasVoice(iterator->Voice)
					&& !engine.DestroyVoice(iterator->Voice))
					TC_Core_Warn("Could not destroy deferred audio voice {0}",
						iterator->Voice);
				iterator = DeferredVoiceDestroys.erase(iterator);
			}
		}

		bool ValidSource(const AudioSource& source)
		{
			return std::isfinite(source.Volume) && source.Volume >= 0.0f
				&& source.Volume <= 4.0f && std::isfinite(source.Pitch)
				&& source.Pitch >= 0.25f && source.Pitch <= 4.0f
				&& source.MixerGroup <= static_cast<uint8_t>(AudioMixerGroup::SFX)
				&& std::isfinite(source.SpatialBlend)
				&& source.SpatialBlend >= 0.0f && source.SpatialBlend <= 1.0f
				&& std::isfinite(source.MinDistance) && source.MinDistance >= 0.0f
				&& std::isfinite(source.MaxDistance)
				&& source.MaxDistance > source.MinDistance;
		}

		AudioMixerGroup ToMixerGroup(uint8_t value)
		{
			return static_cast<AudioMixerGroup>(value);
		}

		std::vector<Entity> CollectEntities(Scene& scene)
		{
			std::vector<Entity> result;
			std::vector<UUID> pending = scene.GetRootEntityUUIDs();
			while (!pending.empty())
			{
				const UUID id = pending.back();
				pending.pop_back();
				Entity entity = scene.FindEntityByUUID(id);
				if (!entity)
					continue;
				result.push_back(entity);
				for (UUID child : scene.GetChildrenUUIDs(entity))
					pending.push_back(child);
			}
			return result;
		}

		struct ListenerPose
		{
			bool Available = false;
			bool Primary = false;
			glm::vec3 Position{ 0.0f };
			glm::vec2 Right{ 1.0f, 0.0f };
		};

		ListenerPose FindListener(Scene& scene,
			const std::vector<Entity>& entities)
		{
			ListenerPose result;
			for (Entity entity : entities)
			{
				if (!entity.HasComponent<AudioListener>()
					|| !scene.IsActiveInHierarchy(entity))
					continue;
				const auto& listener = entity.GetComponent<AudioListener>();
				if (!listener.Enabled || (result.Available && (result.Primary
					|| !listener.Primary)))
					continue;
				const auto& transform = entity.GetComponent<Transform>();
				const float rotation = transform._Rotation.z;
				result.Available = true;
				result.Primary = listener.Primary;
				result.Position = transform._Translation;
				result.Right = { std::cos(rotation), std::sin(rotation) };
			}
			return result;
		}

	}

	void AudioSceneRuntime::Start(Scene& scene)
	{
		FlushDeferredVoiceDestroys(scene);
		// Prepare runtime fields before managed OnCreate. Auto-play is reconciled by
		// Update only after managed startup has finalized ActiveSelf and source data.
		for (Entity entity : CollectEntities(scene))
		{
			if (!entity.HasComponent<AudioSource>())
				continue;
			auto& source = entity.GetComponent<AudioSource>();
			source.RuntimeVoice = 0;
			source.RuntimeClipHandle = AssetHandle(0);
			source.RuntimeAutoPlayEvaluated = false;
			source.RuntimeStreaming = false;
		}
	}

	void AudioSceneRuntime::Stop(Scene& scene)
	{
		FlushDeferredVoiceDestroys(scene);
		for (Entity entity : CollectEntities(scene))
		{
			if (entity.HasComponent<AudioSource>())
			{
				DestroySource(entity);
				entity.GetComponent<AudioSource>().RuntimeAutoPlayEvaluated = false;
			}
		}
	}

	void AudioSceneRuntime::Update(Scene& scene, double deltaSeconds)
	{
		FlushDeferredVoiceDestroys(scene);
		AudioEngine::Get().Update(deltaSeconds);
		const std::vector<Entity> entities = CollectEntities(scene);
		const ListenerPose listener = FindListener(scene, entities);
		for (Entity entity : entities)
		{
			if (!entity.HasComponent<AudioSource>())
				continue;
			auto& source = entity.GetComponent<AudioSource>();
			if (!scene.IsActiveInHierarchy(entity))
			{
				DestroySource(entity);
				source.RuntimeAutoPlayEvaluated = false;
				continue;
			}
			if (!source.RuntimeAutoPlayEvaluated)
			{
				source.RuntimeAutoPlayEvaluated = true;
				if (source.Enabled && source.PlayOnStart
					&& static_cast<uint64_t>(source.Clip) != 0)
				{
					std::string error;
					if (!Play(entity, &error))
						TC_Core_Warn("Could not auto-play a runtime AudioSource on entity '{0}': {1}",
							entity.GetName(), error);
				}
			}
			if (!source.Enabled || source.RuntimeClipHandle != source.Clip
				|| (source.RuntimeVoice != 0
					&& source.RuntimeStreaming != source.Streaming)
				|| !ValidSource(source))
			{
				DestroySource(entity);
				continue;
			}
			if (source.RuntimeVoice != 0)
			{
				if (!ApplySettings(entity))
					TC_Core_Warn("Could not reconcile AudioSource settings on entity '{0}'",
						entity.GetName());
				AudioSpatialSettings spatial;
				if (listener.Available)
				{
					const glm::vec3 position = entity.GetComponent<Transform>()._Translation;
					spatial = CalculateAudioSpatial2D(position.x, position.y,
						listener.Position.x, listener.Position.y, listener.Right.x,
						listener.Right.y, source.SpatialBlend,
						source.MinDistance, source.MaxDistance);
				}
				if (!AudioEngine::Get().SetSpatial(source.RuntimeVoice, spatial))
					TC_Core_Warn("Could not reconcile AudioSource spatial settings on entity '{0}'",
						entity.GetName());
			}
		}
	}

	bool AudioSceneRuntime::Play(Entity entity, std::string* error)
	{
		if (error)
			error->clear();
		if (!entity || !entity.HasComponent<AudioSource>()
			|| !entity.IsActiveInHierarchy())
		{
			if (error) *error =
				"Entity does not have an active AudioSource";
			return false;
		}
		auto& source = entity.GetComponent<AudioSource>();
		source.RuntimeAutoPlayEvaluated = true;
		if (!source.Enabled || static_cast<uint64_t>(source.Clip) == 0
			|| !ValidSource(source))
		{
			if (error) *error = "AudioSource is disabled, missing a clip, or has invalid settings";
			return false;
		}

		AudioEngine& engine = AudioEngine::Get();
		if (source.RuntimeVoice != 0 && source.RuntimeClipHandle != source.Clip)
			DestroySource(entity);
		if (source.RuntimeVoice == 0)
		{
			AudioVoiceSettings settings;
			settings.Loop = source.Loop;
			settings.Volume = source.Volume;
			settings.Pitch = source.Pitch;
			std::string createError;
			source.RuntimeVoice = source.Streaming
				? engine.CreateStreamingVoice(source.Clip,
					ToMixerGroup(source.MixerGroup), settings, createError)
				: engine.CreateVoice(source.Clip,
					ToMixerGroup(source.MixerGroup), settings, createError);
			if (source.RuntimeVoice == 0)
			{
				source.RuntimeClipHandle = AssetHandle(0);
				if (error) *error = std::move(createError);
				return false;
			}
			source.RuntimeClipHandle = source.Clip;
			source.RuntimeStreaming = source.Streaming;
		}
		if (!ApplySettings(entity) || !engine.Play(source.RuntimeVoice))
		{
			if (error) *error = "Audio backend rejected playback";
			return false;
		}
		return true;
	}

	bool AudioSceneRuntime::Pause(Entity entity)
	{
		if (!entity || !entity.HasComponent<AudioSource>())
			return false;
		const auto& source = entity.GetComponent<AudioSource>();
		return source.RuntimeVoice != 0
			&& AudioEngine::Get().Pause(source.RuntimeVoice);
	}

	bool AudioSceneRuntime::Stop(Entity entity)
	{
		if (!entity || !entity.HasComponent<AudioSource>())
			return false;
		const auto& source = entity.GetComponent<AudioSource>();
		return source.RuntimeVoice != 0
			&& AudioEngine::Get().Stop(source.RuntimeVoice);
	}

	AudioPlaybackState AudioSceneRuntime::GetState(Entity entity)
	{
		if (!entity || !entity.HasComponent<AudioSource>())
			return AudioPlaybackState::Stopped;
		const auto& source = entity.GetComponent<AudioSource>();
		return source.RuntimeVoice == 0 ? AudioPlaybackState::Stopped
			: AudioEngine::Get().GetState(source.RuntimeVoice);
	}

	bool AudioSceneRuntime::ApplySettings(Entity entity)
	{
		if (!entity || !entity.HasComponent<AudioSource>())
			return false;
		const auto& source = entity.GetComponent<AudioSource>();
		if (source.RuntimeVoice == 0 || !ValidSource(source))
			return false;
		AudioEngine& engine = AudioEngine::Get();
		return engine.SetLoop(source.RuntimeVoice, source.Loop)
			&& engine.SetVolume(source.RuntimeVoice, source.Volume)
			&& engine.SetPitch(source.RuntimeVoice, source.Pitch)
			&& engine.SetMixerGroup(source.RuntimeVoice,
				ToMixerGroup(source.MixerGroup));
	}

	void AudioSceneRuntime::DeferDestroySource(Entity entity)
	{
		if (!entity || !entity.HasComponent<AudioSource>())
			return;
		auto& source = entity.GetComponent<AudioSource>();
		if (source.RuntimeVoice != 0)
		{
			const DeferredVoiceDestroy pending{ entity.GetScene(), source.RuntimeVoice };
			const bool alreadyQueued = std::any_of(DeferredVoiceDestroys.begin(),
				DeferredVoiceDestroys.end(), [&](const DeferredVoiceDestroy& item)
				{
					return item.Owner == pending.Owner && item.Voice == pending.Voice;
				});
			if (!alreadyQueued)
				DeferredVoiceDestroys.push_back(pending);
		}
		source.RuntimeVoice = 0;
		source.RuntimeClipHandle = AssetHandle(0);
		source.RuntimeStreaming = false;
	}

	void AudioSceneRuntime::DestroySource(Entity entity)
	{
		if (!entity || !entity.HasComponent<AudioSource>())
			return;
		auto& source = entity.GetComponent<AudioSource>();
		if (source.RuntimeVoice != 0)
			AudioEngine::Get().DestroyVoice(source.RuntimeVoice);
		source.RuntimeVoice = 0;
		source.RuntimeClipHandle = AssetHandle(0);
		source.RuntimeStreaming = false;
	}

}
