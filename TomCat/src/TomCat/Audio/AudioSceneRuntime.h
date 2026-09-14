#pragma once

#include "AudioEngine.h"

namespace TomCat {

	class Entity;
	class Scene;

	class AudioSceneRuntime final
	{
	public:
		static void Start(Scene& scene);
		static void Stop(Scene& scene);
		static void Update(Scene& scene, double deltaSeconds);

		static bool Play(Entity entity, std::string* error = nullptr);
		static bool Pause(Entity entity);
		static bool Stop(Entity entity);
		static AudioPlaybackState GetState(Entity entity);
		static bool ApplySettings(Entity entity);
		// Component transactions remove AudioSource before the runtime can observe it.
		// Retain only the voice handle here; the backend destroy happens from Update
		// or Stop after the validated command batch has finished publishing ECS data.
		static void DeferDestroySource(Entity entity);
		static void DestroySource(Entity entity);
	};

}
