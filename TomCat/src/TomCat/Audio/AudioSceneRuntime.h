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
		static void DestroySource(Entity entity);
	};

}
