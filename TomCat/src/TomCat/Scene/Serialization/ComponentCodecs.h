#pragma once

#include "TomCat/Scene/ComponentRegistry.h"

#include <cstdint>
#include <string>
#include <unordered_map>
#include <unordered_set>

namespace TomCat {

	class Entity;

	// The single authoring-component copy/remap policy shared by Scene duplication
	// and Prefab instantiation. YAML component encoding remains centralized in
	// SceneSerializer/SceneArchiveCodec.
	class ComponentCodecs final
	{
	public:
		using MissingEntityReferencePolicy = TomCat::MissingEntityReferencePolicy;

		static bool CopyAuthoringComponents(Entity source, Entity destination,
			bool resolveAssets, std::string& error);

		// Remaps every Registry-declared entity reference and always gives copied C#
		// attachments fresh identities. usedAttachmentIDs must contain identities
		// already live in the destination Scene.
		static bool RemapInstanceReferences(Entity entity,
			const std::unordered_map<UUID, UUID>& entityMap,
			MissingEntityReferencePolicy missingPolicy,
			std::unordered_set<uint64_t>& usedAttachmentIDs,
			bool regenerateAttachmentIDs,
			std::string& error);
	};

}
