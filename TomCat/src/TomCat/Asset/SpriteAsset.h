#pragma once

#include "Asset.h"

#include <string_view>

namespace TomCat {

	class AssetManager;
	class AssetRegistry;

	// TomCat currently treats one imported image as one selectable Sprite. This
	// keeps SpriteRenderer handle-only until atlas regions/pivots justify a real
	// Sprite sub-asset type.
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
