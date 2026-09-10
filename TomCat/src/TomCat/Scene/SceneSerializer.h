#pragma once
#include "Scene.h"

#include <filesystem>

namespace TomCat {

	class SceneSerializer
	{
	public:
		SceneSerializer(const Ref<Scene>& scene);

		bool Serialize(const std::filesystem::path& filepath);

		bool Deserialize(const std::filesystem::path& filepath);

	private:
		Ref<Scene> m_Scene;
	};

}
