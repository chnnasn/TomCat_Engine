#pragma once

#include "Asset.h"

#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace TomCat {

	class AssetManager;
	class AssetRegistry;

	inline constexpr uint32_t SpriteAtlasSettingsSchemaVersion = 2;

	// Multiple-Sprite settings are a flat, canonical map so they naturally feed
	// ArtifactKey. Entries use Sprite.<persistent-id>.{Name,Rect,Pivot,
	// PixelsPerUnit,Border}; vector values are comma-separated decimal values.
	[[nodiscard]] bool ParseSpriteAtlasSettings(const AssetImportSettings& settings,
		std::vector<AssetSubAsset>& sprites, std::string& error);

	struct ResolvedSpriteAsset
	{
		AssetHandle TextureHandle = AssetHandle(0);
		SpriteSubAssetData Data;
		bool IsSubAsset = false;
	};

	struct SpriteRenderGeometry
	{
		float Width = 1.0f;
		float Height = 1.0f;
		float OffsetX = 0.0f;
		float OffsetY = 0.0f;
		float UMin = 0.0f;
		float VMin = 0.0f;
		float UMax = 1.0f;
		float VMax = 1.0f;
		float BorderLeft = 0.0f;
		float BorderBottom = 0.0f;
		float BorderRight = 0.0f;
		float BorderTop = 0.0f;
	};

	// Atlas Rect uses a top-left source-image origin. Pivot uses the conventional
	// normalized bottom-left sprite origin. The returned UVs account for the
	// vertically flipped OpenGL upload performed by Texture2D.
	[[nodiscard]] bool BuildSpriteRenderGeometry(const SpriteSubAssetData& data,
		uint32_t textureWidth, uint32_t textureHeight,
		SpriteRenderGeometry& geometry);

	// The cooked sub-sprite envelope carries its slice plus the imported atlas
	// bytes. A Player therefore needs neither source .tcmeta nor loose files.
	[[nodiscard]] std::vector<uint8_t> BuildCookedSpriteSubAsset(
		AssetHandle sourceTexture, const SpriteSubAssetData& data,
		std::span<const uint8_t> atlasBytes);
	[[nodiscard]] bool ParseCookedSpriteSubAsset(std::span<const uint8_t> bytes,
		ResolvedSpriteAsset& sprite, std::span<const uint8_t>& atlasBytes);

	// Whole-image Texture2D handles remain the legacy-compatible Sprite path;
	// Sprite Atlas regions are selectable Texture2D sub-assets with stable handles.
	[[nodiscard]] bool IsSpriteAsset(const AssetMetadata& metadata);

	// Finds a project primitive by its committed import metadata, so moving or
	// renaming the image never breaks the creation menu.
	[[nodiscard]] AssetHandle FindPrimitiveSpriteAsset(const AssetRegistry& registry,
		std::string_view primitiveName);

	// Creates a normal TGA source asset under Assets when the requested primitive
	// is not present, imports it, and records Usage/Primitive in its .tcmeta.
	[[nodiscard]] AssetHandle EnsurePrimitiveSpriteAsset(AssetManager& manager,
		std::string_view primitiveName);
	[[nodiscard]] AssetHandle EnsurePrimitiveSpriteAsset(AssetRegistry& registry,
		std::string_view primitiveName);

}
