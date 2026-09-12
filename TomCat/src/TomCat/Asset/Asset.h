#pragma once

#include "TomCat/Core/UUID.h"

#include <cstdint>
#include <filesystem>
#include <map>
#include <string>

namespace TomCat {

	// Asset handles are stable project identities. Paths are Editor/import details and
	// must never be used as serialized references by scenes or runtime code.
	using AssetHandle = UUID;

	enum class AssetType : uint16_t
	{
		None = 0,
		Scene = 1,
		Texture2D = 2,
		Shader = 3,
		Audio = 4,
		Font = 5,
		Mesh = 6,
		Material = 7,
		// Keep the former Script numeric slot for metadata/package migration while
		// giving C# source a precise, single-language identity.
		CSharpScript = 8,
		Other = 9,
		Prefab = 10
	};

	const char* AssetTypeToString(AssetType type);
	AssetType AssetTypeFromString(const std::string& value);
	AssetType AssetTypeFromPath(const std::filesystem::path& path);

	// Import settings intentionally remain string keyed. Importers can evolve their
	// own settings without forcing the registry schema to change.
	using AssetImportSettings = std::map<std::string, std::string>;

	struct AssetMetadata
	{
		AssetHandle Handle = AssetHandle(0);
		AssetType Type = AssetType::None;
		std::filesystem::path FilePath; // Relative to the project's Assets directory.
		AssetImportSettings ImportSettings;
		bool IsMissing = false; // Runtime/cache state; never written to .tcmeta.

		explicit operator bool() const
		{
			return static_cast<uint64_t>(Handle) != 0 && Type != AssetType::None && !FilePath.empty();
		}
	};

	struct AssetReference
	{
		AssetHandle ReferencedAsset = AssetHandle(0);
		AssetHandle ReferencingAsset = AssetHandle(0);
		std::filesystem::path FilePath;
		std::string PropertyPath;
	};

	// Editor drag-and-drop transports identity only. Consumers resolve the current
	// type and location through AssetRegistry instead of reconstructing a path.
	inline constexpr const char* AssetDragDropPayloadID = "TOMCAT_ASSET_HANDLE";

}
