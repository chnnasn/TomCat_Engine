#pragma once

#include "Asset.h"

#include <span>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace TomCat {

	class AssetManager;
	class AssetRegistry;

	inline constexpr uint32_t SpriteAtlasSettingsSchemaVersion = 2;

	struct SpriteAtlasRect
	{
		uint32_t X = 0;
		uint32_t Y = 0;
		uint32_t Width = 0;
		uint32_t Height = 0;

		bool operator==(const SpriteAtlasRect&) const = default;
	};

	struct SpriteAtlasSliceOptions
	{
		uint8_t AlphaThreshold = 1;
		uint32_t MinimumOpaquePixels = 1;
		uint32_t Padding = 0;
	};

	struct SpriteAtlasGridOptions
	{
		uint32_t CellWidth = 32;
		uint32_t CellHeight = 32;
		uint32_t OffsetX = 0;
		uint32_t OffsetY = 0;
		uint32_t SpacingX = 0;
		uint32_t SpacingY = 0;
		bool IncludePartialCells = false;
	};

	struct SpriteAtlasPackOptions
	{
		uint32_t MaximumWidth = 4096;
		uint32_t MaximumHeight = 4096;
		uint32_t Padding = 1;
		bool PowerOfTwo = true;
	};

	struct SpriteAtlasPackedLayout
	{
		uint32_t Width = 0;
		uint32_t Height = 0;
		// Placements retain the same order as the input Rects. Only Width and
		// Height are read from input Rects; X/Y are ignored.
		std::vector<SpriteAtlasRect> Placements;
	};

	// Finds four-connected opaque islands in top-left-origin RGBA pixels. The
	// result is deterministic and sorted by Y, then X. Padding expands each
	// bounding rectangle while clipping it to the image.
	[[nodiscard]] bool SliceSpriteAtlasByAlpha(std::span<const uint8_t> rgbaPixels,
		uint32_t width, uint32_t height, const SpriteAtlasSliceOptions& options,
		std::vector<SpriteAtlasRect>& regions, std::string& error);

	// Produces a regular grid in top-left source coordinates. Partial edge cells
	// are either omitted or clipped according to IncludePartialCells.
	[[nodiscard]] bool SliceSpriteAtlasGrid(uint32_t width, uint32_t height,
		const SpriteAtlasGridOptions& options, std::vector<SpriteAtlasRect>& regions,
		std::string& error);

	// Matches generated regions to existing slices before creating deterministic
	// IDs for new regions. Exact Rect matches win, followed by overlap. Matched
	// slices keep their persistent ID, name, pivot, PPU and valid border values.
	[[nodiscard]] std::vector<AssetSubAsset> ReconcileSpriteAtlasSlices(
		std::span<const SpriteAtlasRect> regions,
		std::span<const AssetSubAsset> existingSlices,
		float defaultPixelsPerUnit = 100.0f);

	// Deterministically packs rectangle sizes with a skyline search. Padding is
	// reserved around every item and placements map directly to input order.
	[[nodiscard]] bool PackSpriteAtlasRects(std::span<const SpriteAtlasRect> rects,
		const SpriteAtlasPackOptions& options, SpriteAtlasPackedLayout& layout,
		std::string& error);

	// Re-packs regions from one RGBA image and extrudes edge pixels through the
	// requested padding. This is the reusable core used by editor export tools.
	[[nodiscard]] bool BuildPackedSpriteAtlasRGBA(
		std::span<const uint8_t> sourceRGBA, uint32_t sourceWidth,
		uint32_t sourceHeight, std::span<const SpriteAtlasRect> sourceRegions,
		const SpriteAtlasPackOptions& options, SpriteAtlasPackedLayout& layout,
		std::vector<uint8_t>& packedRGBA, std::string& error);

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

	struct BuiltInSpriteAsset
	{
		AssetHandle Handle = AssetHandle(0);
		std::string_view Name;
		std::string_view PackageRelativePath;
	};

	// Built-in Sprites are immutable engine package resources. Their handles are
	// stable across projects and their paths are relative to the Packages root.
	[[nodiscard]] std::span<const BuiltInSpriteAsset> GetBuiltInSpriteAssets();
	[[nodiscard]] const BuiltInSpriteAsset* FindBuiltInSpriteAsset(AssetHandle handle);
	[[nodiscard]] const BuiltInSpriteAsset* FindBuiltInSpriteAsset(std::string_view name);
	[[nodiscard]] std::filesystem::path GetBuiltInSpriteAssetPath(AssetHandle handle);

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

	// Legacy authoring/test seam for creating an editable project-local primitive.
	// Editor creation menus use the immutable package assets above.
	[[nodiscard]] AssetHandle EnsurePrimitiveSpriteAsset(AssetManager& manager,
		std::string_view primitiveName);
	[[nodiscard]] AssetHandle EnsurePrimitiveSpriteAsset(AssetRegistry& registry,
		std::string_view primitiveName);

}
