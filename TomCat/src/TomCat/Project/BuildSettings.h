#pragma once

#include "TomCat/Asset/Asset.h"

#include <filesystem>
#include <vector>

namespace TomCat {

	struct BuildSceneSettings
	{
		AssetHandle Handle = AssetHandle(0);
		bool Enabled = true;
		// Assets-relative authoring hint only. Runtime and Cook resolve by Handle.
		std::filesystem::path PathHint;

		bool operator==(const BuildSceneSettings&) const = default;
	};

	struct BuildSettings
	{
		AssetHandle EntrySceneHandle = AssetHandle(0);
		// Author order is stable. tcpak stores enabled handles in this order.
		std::vector<BuildSceneSettings> Scenes;

		bool operator==(const BuildSettings&) const = default;
	};

}
