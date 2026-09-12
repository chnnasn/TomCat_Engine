#pragma once

#include "TomCat/Asset/Asset.h"

#include <cstdint>
#include <functional>
#include <string>

namespace YAML {
	class Node;
}

namespace TomCat {

	enum class SerializedAssetReferenceKind : uint8_t
	{
		Sprite,
		CSharpScript,
		ScriptField
	};

	// A schema-aware reference emitted from serialized entity/component data.
	// AssetType::None means that any runtime-cookable asset type is accepted.
	struct SerializedAssetReference
	{
		AssetHandle Handle = AssetHandle(0);
		AssetType ExpectedType = AssetType::None;
		SerializedAssetReferenceKind Kind = SerializedAssetReferenceKind::ScriptField;
		std::string PropertyPath;
		bool Required = false;
	};

	// Shared traversal seam for Scene and snapshot-Prefab archives.
	// It deliberately follows the serialized schema instead of guessing that an
	// arbitrary YAML property whose name ends in "Handle" is an asset reference.
	class AssetReferenceVisitor final
	{
	public:
		using Visitor = std::function<bool(const SerializedAssetReference&)>;

		static bool VisitScene(const YAML::Node& sceneDocument,
			const Visitor& visitor, std::string& errorMessage);
		static bool VisitPrefab(const YAML::Node& prefabDocument,
			const Visitor& visitor, std::string& errorMessage);
		static bool VisitEntities(const YAML::Node& entities,
			const std::string& propertyPath, const Visitor& visitor,
			std::string& errorMessage);
	};

}
