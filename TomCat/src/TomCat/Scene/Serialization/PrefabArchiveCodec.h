#pragma once

#include "EntityArchive.h"
#include "TomCat/Scene/Entity.h"

#include <filesystem>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

#include <glm/glm.hpp>

namespace TomCat {

	struct PrefabArchive
	{
		uint32_t SchemaVersion = 1;
		EntityLocalID RootLocalID = 0;
		std::vector<EntityArchive> Entities;
		// A fully validated template Scene whose UUID namespace is the Prefab's
		// LocalID namespace. It is never installed as a runtime Scene.
		Ref<Scene> TemplateScene;
	};

	struct PrefabInstantiateOptions
	{
		std::optional<glm::vec3> RootWorldPosition;
		std::optional<UUID> Parent;
		bool ResolveAssets = true;
	};

	struct PrefabInstantiationResult
	{
		Entity Root;
		std::vector<Entity> Entities;
		std::unordered_map<EntityLocalID, UUID> LocalToSceneUUID;
	};

	class PrefabArchiveCodec final
	{
	public:
		static constexpr uint32_t CurrentSchemaVersion = 1;

		static bool CaptureSubtree(const Ref<Scene>& source, Entity root,
			PrefabArchive& archive, std::string& error);
		static bool Encode(const PrefabArchive& archive, std::string& document,
			std::string& error);
		static bool Decode(const std::vector<uint8_t>& bytes,
			const std::filesystem::path& diagnosticPath,
			PrefabArchive& archive, std::string& error);

		static bool SaveSubtree(const Ref<Scene>& source, Entity root,
			const std::filesystem::path& filepath, AssetHandle* savedHandle = nullptr);
		static bool Load(const std::filesystem::path& filepath,
			PrefabArchive& archive, std::string& error);
		static bool Load(AssetHandle handle, PrefabArchive& archive,
			std::string& error);
		static bool ValidateCurrentFormat(const std::vector<uint8_t>& bytes,
			const std::filesystem::path& diagnosticPath);

		// Builds and validates a complete instance in a private staging Scene before
		// touching destination. Successful commit always generates fresh Scene UUIDs
		// and fresh C# AttachmentIDs.
		static bool Instantiate(const PrefabArchive& archive, Scene& destination,
			const PrefabInstantiateOptions& options,
			PrefabInstantiationResult& result, std::string& error);
	};

}
