#pragma once

#include "TomCat/Core/UUID.h"

#include <cstdint>
#include <filesystem>
#include <map>
#include <string>
#include <vector>

namespace TomCat {

	// Asset handles are stable project identities. Paths are Editor/import details and
	// must never be used as serialized references by scenes or runtime code.
	using AssetHandle = UUID;

	// Engine package assets use a reserved, stable namespace so scenes can reference
	// read-only resources without copying them into every project's Assets folder.
	inline constexpr uint64_t BuiltInCircleSpriteHandleValue = 0x54434D5350520001ULL;
	inline constexpr uint64_t BuiltInSquareSpriteHandleValue = 0x54434D5350520002ULL;

	[[nodiscard]] inline constexpr bool IsBuiltInAssetHandleValue(uint64_t value)
	{
		return value == BuiltInCircleSpriteHandleValue
			|| value == BuiltInSquareSpriteHandleValue;
	}

	[[nodiscard]] inline bool IsBuiltInAssetHandle(AssetHandle handle)
	{
		return IsBuiltInAssetHandleValue(static_cast<uint64_t>(handle));
	}

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

	// Importer-owned Sprite slicing data. Rect is expressed in source-image
	// pixels, Pivot is normalized inside Rect, and Border uses
	// left/bottom/right/top pixels. Keeping this beside the sub-asset identity
	// lets .tcmeta v2 preserve the complete deterministic import result.
	struct SpriteSubAssetData
	{
		uint32_t X = 0;
		uint32_t Y = 0;
		uint32_t Width = 0;
		uint32_t Height = 0;
		float PivotX = 0.5f;
		float PivotY = 0.5f;
		float PixelsPerUnit = 100.0f;
		float BorderLeft = 0.0f;
		float BorderBottom = 0.0f;
		float BorderRight = 0.0f;
		float BorderTop = 0.0f;

		bool operator==(const SpriteSubAssetData&) const = default;
	};

	// A sub-asset keeps the handle assigned by the sidecar while its importer-owned
	// PersistentID remains present. Display names may change without breaking
	// serialized references.
	struct AssetSubAsset
	{
		AssetHandle Handle = AssetHandle(0);
		std::string PersistentID;
		std::string Name;
		AssetType Type = AssetType::None;
		SpriteSubAssetData Sprite;
	};

	struct AssetMetadata
	{
		AssetHandle Handle = AssetHandle(0);
		AssetType Type = AssetType::None;
		std::filesystem::path FilePath; // Relative to the project's Assets directory.
		AssetImportSettings ImportSettings;
		std::vector<AssetSubAsset> SubAssets;
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
