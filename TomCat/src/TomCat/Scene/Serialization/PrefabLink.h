#pragma once

#include "TomCat/Scene/ComponentRegistry.h"
#include "PrefabArchiveCodec.h"

	namespace TomCat {

	// Editor authoring data. The baseline is in instance UUID space, so updates
	// can preserve external references to existing entities.
	struct PrefabLink
	{
		EKIT_COMPONENT(PrefabLink);
		AssetHandle Source{ 0 };
		std::string State;
	};

	ComponentDescriptor MakePrefabLinkDescriptor();

    struct PrefabPropertyOverride
    {
        UUID EntityID{0}, ComponentID{0}, PropertyID{0};
        std::string Path;
        PropertyValue Before, After;
    };

    class PrefabLinkedInstance final
	{
	public:
		static bool Attach(const Ref<Scene>& scene, AssetHandle source,
			const PrefabArchive& archive, const PrefabInstantiationResult& instance,
			std::string& error);
		// Edit-mode operation. Successful updates replace the registry atomically;
		// callers must re-resolve Entity wrappers by UUID afterwards.
		static bool Update(const Ref<Scene>& scene, UUID root,
			const PrefabArchive& latest, bool revert, std::string& error, bool resolveAssets = true);
		static bool Capture(const Ref<Scene>& scene, UUID root,
			PrefabArchive& archive, std::string& error);
		static bool RefreshAll(const Ref<Scene>& scene, bool& changed, std::string& error,
			bool resolveAssets = true);
		static bool Apply(const Ref<Scene>& scene, UUID root, std::string& error);
		static bool Remap(Entity entity, const std::unordered_map<UUID, UUID>& entities,
			const std::unordered_map<UUID, UUID>* attachments, bool regenerate,
			std::string& error);
		static bool ResolveComposition(PrefabArchive& archive, std::string& error);
        static bool GetPropertyOverrides(const Ref<Scene>& scene, UUID root,
            std::vector<PrefabPropertyOverride>& values, std::string& error);
        static bool RevertProperty(const Ref<Scene>& scene, UUID root, UUID entity,
            UUID component, UUID property, std::string& error);
        static bool ApplyProperty(const Ref<Scene>& scene, UUID root, UUID entity,
            UUID component, UUID property, std::string& error);
        static bool GetOverridePaths(const Ref<Scene>& scene, UUID root,
			std::vector<std::string>& paths, std::string& error);
	};
}
