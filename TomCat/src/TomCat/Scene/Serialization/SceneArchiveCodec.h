#pragma once

#include "TomCat/Core/Base.h"

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace TomCat {

	class Scene;

	// In-memory gateway to the canonical SceneSerializer component format.
	// PrefabArchiveCodec intentionally routes all component parsing through this
	// type instead of maintaining a second component codec.
	class SceneArchiveCodec final
	{
	public:
		static bool Encode(const Ref<Scene>& scene, std::string& document,
			std::string& error);
		static bool Decode(const std::vector<uint8_t>& bytes,
			const Ref<Scene>& destination,
			const std::filesystem::path& diagnosticPath,
			bool resolveAssets = false);
	};

}
