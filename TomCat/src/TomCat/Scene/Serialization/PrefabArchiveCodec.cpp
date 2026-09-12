#include "tcpch.h"
#include "PrefabArchiveCodec.h"

#include "ComponentCodecs.h"
#include "SceneArchiveCodec.h"
#include "TomCat/Asset/AssetManager.h"
#include "TomCat/Scene/Components.h"
#include "TomCat/Scene/Scene.h"
#include "TomCat/Scene/SceneSerializer.h"
#include "TomCat/Utils/FileSystemUtils.h"
#include "TomCat/Utils/PathUtils.h"

#include <fstream>
#include <limits>
#include <stdexcept>
#include <unordered_set>

#include <yaml-cpp/yaml.h>

namespace TomCat {

	namespace {

		constexpr const char* PrefabDiagnosticName = "PrefabArchive";

		void RequireExactMapFields(const YAML::Node& node, const std::string& context,
			std::initializer_list<const char*> required)
		{
			if (!node || !node.IsMap())
				throw std::runtime_error(context + " must be a map");
			std::unordered_set<std::string> allowed;
			for (const char* field : required)
				allowed.emplace(field);
			std::unordered_set<std::string> seen;
			for (const auto& pair : node)
			{
				if (!pair.first.IsScalar())
					throw std::runtime_error(context + " contains a non-scalar key");
				const std::string key = pair.first.as<std::string>();
				if (!allowed.contains(key))
					throw std::runtime_error(context + " contains unknown field '" + key + "'");
				if (!seen.emplace(key).second)
					throw std::runtime_error(context + " contains duplicate field '" + key + "'");
			}
			for (const char* field : required)
			{
				if (!seen.contains(field))
					throw std::runtime_error(context + " is missing field '" + field + "'");
			}
		}

		uint64_t ReadRequiredUInt64(const YAML::Node& node, const char* field,
			const std::string& context)
		{
			const YAML::Node value = node[field];
			if (!value || !value.IsScalar())
				throw std::runtime_error(context + "." + field + " must be an unsigned integer");
			try
			{
				return value.as<uint64_t>();
			}
			catch (const YAML::Exception&)
			{
				throw std::runtime_error(context + "." + field + " must be an unsigned integer");
			}
		}

		void CollectSubtree(Scene& scene, Entity entity, std::vector<UUID>& order,
			std::unordered_set<UUID>& visited)
		{
			if (!entity || !visited.emplace(entity.GetUUID()).second)
				throw std::runtime_error("Prefab source hierarchy contains an invalid entity or cycle");
			order.push_back(entity.GetUUID());
			for (UUID childID : scene.GetChildrenUUIDs(entity))
			{
				Entity child = scene.FindEntityByUUID(childID);
				if (!child || scene.GetParent(child) != entity)
					throw std::runtime_error("Prefab source hierarchy is inconsistent");
				CollectSubtree(scene, child, order, visited);
			}
		}

		void CollectAttachmentIDs(Scene& scene, std::unordered_set<uint64_t>& result)
		{
			for (UUID entityID : scene.GetRootEntityUUIDs())
			{
				std::vector<UUID> stack{ entityID };
				while (!stack.empty())
				{
					const UUID currentID = stack.back();
					stack.pop_back();
					Entity current = scene.FindEntityByUUID(currentID);
					if (!current)
						continue;
					if (current.HasComponent<CSharpScripts>())
					{
						for (const CSharpScriptEntry& script :
							current.GetComponent<CSharpScripts>().Scripts)
						{
							const uint64_t id = static_cast<uint64_t>(script.AttachmentID);
							if (id != 0)
								result.emplace(id);
						}
					}
					for (UUID child : scene.GetChildrenUUIDs(current))
						stack.push_back(child);
				}
			}
		}

		bool ValidateInternalEntityReferences(const PrefabArchive& archive,
			std::string& error)
		{
			if (!archive.TemplateScene)
			{
				error = "Prefab has no validated template Scene";
				return false;
			}
			for (const EntityArchive& record : archive.Entities)
			{
				Entity entity = archive.TemplateScene->FindEntityByUUID(UUID(record.LocalID));
				if (!entity)
				{
					error = "Prefab record references missing LocalID "
						+ std::to_string(record.LocalID);
					return false;
				}
				if (!entity.HasComponent<CSharpScripts>())
					continue;
				for (const CSharpScriptEntry& script :
					entity.GetComponent<CSharpScripts>().Scripts)
				{
					for (const ScriptField& field : script.Fields)
					{
						if (field.Type != ScriptFieldType::Entity)
							continue;
						if (!std::holds_alternative<uint64_t>(field.Value))
						{
							error = "C# Entity field '" + field.Name
								+ "' has an incompatible value";
							return false;
						}
						const uint64_t target = std::get<uint64_t>(field.Value);
						if (target != 0 && !archive.TemplateScene->FindEntityByUUID(UUID(target)))
						{
							error = "C# Entity field '" + field.Name
								+ "' references LocalID outside the Prefab";
							return false;
						}
					}
				}
			}
			return true;
		}

		bool ValidateArchiveGraph(const PrefabArchive& archive, std::string& error)
		{
			if (archive.SchemaVersion != PrefabArchiveCodec::CurrentSchemaVersion)
			{
				error = "Unsupported Prefab SchemaVersion";
				return false;
			}
			if (archive.RootLocalID == 0 || archive.Entities.empty()
				|| !archive.TemplateScene)
			{
				error = "Prefab must contain a nonzero root and at least one entity";
				return false;
			}

			std::unordered_set<EntityLocalID> ids;
			size_t roots = 0;
			for (const EntityArchive& record : archive.Entities)
			{
				if (record.LocalID == 0 || !ids.emplace(record.LocalID).second)
				{
					error = "Prefab LocalIDs must be nonzero and unique";
					return false;
				}
				if (record.ParentLocalID == 0)
				{
					++roots;
					if (record.LocalID != archive.RootLocalID)
					{
						error = "Prefab root does not match RootLocalID";
						return false;
					}
				}
			}
			if (roots != 1 || !ids.contains(archive.RootLocalID)
				|| ids.size() != archive.Entities.size())
			{
				error = "Prefab must contain exactly one reachable root";
				return false;
			}
			for (const EntityArchive& record : archive.Entities)
			{
				if (record.ParentLocalID != 0 && !ids.contains(record.ParentLocalID))
				{
					error = "Prefab parent references a missing LocalID";
					return false;
				}
				Entity entity = archive.TemplateScene->FindEntityByUUID(UUID(record.LocalID));
				if (!entity)
				{
					error = "Prefab template is missing an EntityArchive LocalID";
					return false;
				}
				Entity parent = archive.TemplateScene->GetParent(entity);
				const uint64_t actualParent = parent
					? static_cast<uint64_t>(parent.GetUUID()) : 0;
				if (actualParent != record.ParentLocalID)
				{
					error = "Prefab EntityArchive parent does not match its validated template";
					return false;
				}
			}
			std::unordered_set<uint64_t> templateIDs;
			std::vector<UUID> pending = archive.TemplateScene->GetRootEntityUUIDs();
			while (!pending.empty())
			{
				const UUID entityID = pending.back();
				pending.pop_back();
				if (!templateIDs.emplace(static_cast<uint64_t>(entityID)).second)
				{
					error = "Prefab template hierarchy contains a duplicate or cycle";
					return false;
				}
				Entity entity = archive.TemplateScene->FindEntityByUUID(entityID);
				if (!entity)
				{
					error = "Prefab template hierarchy contains a missing entity";
					return false;
				}
				for (UUID child : archive.TemplateScene->GetChildrenUUIDs(entity))
					pending.push_back(child);
			}
			if (templateIDs.size() != ids.size())
			{
				error = "Prefab template contains entities outside EntityArchive records";
				return false;
			}
			return ValidateInternalEntityReferences(archive, error);
		}

		YAML::Node ConvertSceneEntityToPrefab(const YAML::Node& sceneEntity)
		{
			YAML::Node prefabEntity(YAML::NodeType::Map);
			for (const auto& pair : sceneEntity)
			{
				const std::string key = pair.first.as<std::string>();
				prefabEntity[key == "Entity" ? "LocalID" : key] = pair.second;
			}
			return prefabEntity;
		}

		YAML::Node ConvertPrefabEntityToScene(const YAML::Node& prefabEntity,
			const std::string& context, EntityArchive& record)
		{
			if (!prefabEntity || !prefabEntity.IsMap())
				throw std::runtime_error(context + " must be a map");
			std::unordered_set<std::string> seen;
			bool hasLocalID = false;
			bool hasParent = false;
			YAML::Node sceneEntity(YAML::NodeType::Map);
			for (const auto& pair : prefabEntity)
			{
				if (!pair.first.IsScalar())
					throw std::runtime_error(context + " contains a non-scalar key");
				const std::string key = pair.first.as<std::string>();
				if (!seen.emplace(key).second)
					throw std::runtime_error(context + " contains duplicate field '" + key + "'");
				if (key == "Entity")
					throw std::runtime_error(context + " must use LocalID, never a Scene Entity UUID");
				if (key == "LocalID")
				{
					record.LocalID = ReadRequiredUInt64(prefabEntity, "LocalID", context);
					hasLocalID = true;
					sceneEntity["Entity"] = record.LocalID;
				}
				else
				{
					sceneEntity[key] = pair.second;
					if (key == "Parent")
					{
						record.ParentLocalID = ReadRequiredUInt64(prefabEntity,
							"Parent", context);
						hasParent = true;
					}
				}
			}
			if (!hasLocalID || !hasParent)
				throw std::runtime_error(context + " requires LocalID and Parent");
			return sceneEntity;
		}

		bool ReadAllBytes(const std::filesystem::path& filepath,
			std::vector<uint8_t>& bytes, std::string& error)
		{
			std::ifstream input(filepath, std::ios::binary | std::ios::ate);
			if (!input)
			{
				error = "Could not open Prefab file";
				return false;
			}
			const std::streamoff end = input.tellg();
			if (end < 0 || static_cast<uint64_t>(end) >
				static_cast<uint64_t>((std::numeric_limits<size_t>::max)()))
			{
				error = "Prefab file is too large";
				return false;
			}
			bytes.resize(static_cast<size_t>(end));
			input.seekg(0, std::ios::beg);
			if (!bytes.empty() && !input.read(reinterpret_cast<char*>(bytes.data()),
				static_cast<std::streamsize>(bytes.size())))
			{
				error = "Could not read Prefab file";
				return false;
			}
			return true;
		}

	}

	bool PrefabArchiveCodec::CaptureSubtree(const Ref<Scene>& source, Entity root,
		PrefabArchive& archive, std::string& error)
	{
		error.clear();
		if (!source || !root || source->FindEntityByUUID(root.GetUUID()) != root)
		{
			error = "Prefab capture requires a valid root from the source Scene";
			return false;
		}

		try
		{
			std::vector<UUID> sourceOrder;
			std::unordered_set<UUID> visited;
			CollectSubtree(*source, root, sourceOrder, visited);
			std::unordered_map<UUID, UUID> sourceToLocal;
			for (size_t index = 0; index < sourceOrder.size(); ++index)
				sourceToLocal.emplace(sourceOrder[index], UUID(index + 1));

			Ref<Scene> templateScene = CreateRef<Scene>();
			templateScene->SetSceneName(PrefabDiagnosticName);
			std::vector<EntityArchive> records;
			records.reserve(sourceOrder.size());
			for (UUID sourceID : sourceOrder)
			{
				Entity sourceEntity = source->FindEntityByUUID(sourceID);
				const UUID localID = sourceToLocal.at(sourceID);
				Entity destination = templateScene->CreateEntityWithUUID(localID,
					sourceEntity.GetName());
				std::string copyError;
				if (!ComponentCodecs::CopyAuthoringComponents(sourceEntity, destination,
					false, copyError))
					throw std::runtime_error(copyError);

				EntityArchive record;
				record.LocalID = static_cast<uint64_t>(localID);
				Entity sourceParent = source->GetParent(sourceEntity);
				if (sourceID != root.GetUUID())
				{
					if (!sourceParent || !sourceToLocal.contains(sourceParent.GetUUID()))
						throw std::runtime_error("Prefab subtree contains an external parent");
					record.ParentLocalID = static_cast<uint64_t>(
						sourceToLocal.at(sourceParent.GetUUID()));
				}
				records.push_back(record);
			}

			for (const EntityArchive& record : records)
			{
				if (record.ParentLocalID == 0)
					continue;
				if (!templateScene->SetParent(
					templateScene->FindEntityByUUID(UUID(record.LocalID)),
					templateScene->FindEntityByUUID(UUID(record.ParentLocalID))))
					throw std::runtime_error("Could not reproduce Prefab hierarchy");
			}

			Entity templateRoot = templateScene->FindEntityByUUID(UUID(1));
			auto& rootTransform = templateRoot.GetComponent<Transform>();
			rootTransform._LocalTranslation = rootTransform._Translation;
			rootTransform._LocalRotation = rootTransform._Rotation;
			rootTransform._LocalScale = rootTransform._Scale;

			std::unordered_set<uint64_t> attachmentIDs;
			for (const EntityArchive& record : records)
			{
				std::string remapError;
				if (!ComponentCodecs::RemapInstanceReferences(
					templateScene->FindEntityByUUID(UUID(record.LocalID)), sourceToLocal,
					ComponentCodecs::MissingEntityReferencePolicy::Reject,
					attachmentIDs, false, remapError))
					throw std::runtime_error(remapError);
			}
			if (!templateScene->SyncTransformHierarchy())
				throw std::runtime_error("Prefab transform hierarchy is invalid");

			PrefabArchive captured;
			captured.RootLocalID = 1;
			captured.Entities = std::move(records);
			captured.TemplateScene = std::move(templateScene);
			if (!ValidateArchiveGraph(captured, error))
				return false;
			archive = std::move(captured);
			return true;
		}
		catch (const std::exception& exception)
		{
			error = exception.what();
			return false;
		}
	}

	bool PrefabArchiveCodec::Encode(const PrefabArchive& archive,
		std::string& document, std::string& error)
	{
		document.clear();
		if (!ValidateArchiveGraph(archive, error))
			return false;
		try
		{
			std::string sceneDocument;
			if (!SceneArchiveCodec::Encode(archive.TemplateScene, sceneDocument, error))
				return false;
			const YAML::Node sceneRoot = YAML::Load(sceneDocument);
			const YAML::Node sceneEntities = sceneRoot["Entities"];
			if (!sceneEntities || !sceneEntities.IsSequence()
				|| sceneEntities.size() != archive.Entities.size())
				throw std::runtime_error("Canonical Scene entity set does not match Prefab records");

			YAML::Emitter output;
			output << YAML::BeginMap;
			output << YAML::Key << "SchemaVersion" << YAML::Value << CurrentSchemaVersion;
			output << YAML::Key << "RootLocalID" << YAML::Value << archive.RootLocalID;
			output << YAML::Key << "Entities" << YAML::Value << YAML::BeginSeq;
			for (const YAML::Node& entity : sceneEntities)
				output << ConvertSceneEntityToPrefab(entity);
			output << YAML::EndSeq << YAML::EndMap;
			if (!output.good())
				throw std::runtime_error(output.GetLastError());
			document.assign(output.c_str());
			return true;
		}
		catch (const std::exception& exception)
		{
			error = exception.what();
			return false;
		}
	}

	bool PrefabArchiveCodec::Decode(const std::vector<uint8_t>& bytes,
		const std::filesystem::path& diagnosticPath, PrefabArchive& archive,
		std::string& error)
	{
		error.clear();
		try
		{
			const YAML::Node root = YAML::Load(std::string(bytes.begin(), bytes.end()));
			RequireExactMapFields(root, "Prefab document",
				{ "SchemaVersion", "RootLocalID", "Entities" });
			const uint64_t schemaVersion = ReadRequiredUInt64(root, "SchemaVersion",
				"Prefab document");
			if (schemaVersion != CurrentSchemaVersion)
				throw std::runtime_error("Prefab SchemaVersion must be 1");
			const uint64_t rootLocalID = ReadRequiredUInt64(root, "RootLocalID",
				"Prefab document");
			const YAML::Node entities = root["Entities"];
			if (!entities || !entities.IsSequence() || entities.size() == 0)
				throw std::runtime_error("Prefab Entities must be a nonempty sequence");

			std::vector<EntityArchive> records;
			records.reserve(entities.size());
			YAML::Emitter synthetic;
			synthetic << YAML::BeginMap;
			synthetic << YAML::Key << "SchemaVersion" << YAML::Value
				<< SceneSerializer::CurrentSchemaVersion;
			synthetic << YAML::Key << "SceneName" << YAML::Value << PrefabDiagnosticName;
			synthetic << YAML::Key << "Entities" << YAML::Value << YAML::BeginSeq;
			for (size_t index = 0; index < entities.size(); ++index)
			{
				EntityArchive record;
				synthetic << ConvertPrefabEntityToScene(entities[index],
					"Entities[" + std::to_string(index) + "]", record);
				records.push_back(record);
			}
			synthetic << YAML::EndSeq << YAML::EndMap;
			if (!synthetic.good())
				throw std::runtime_error(synthetic.GetLastError());

			const std::string sceneDocument(synthetic.c_str());
			const std::vector<uint8_t> sceneBytes(sceneDocument.begin(), sceneDocument.end());
			Ref<Scene> templateScene = CreateRef<Scene>();
			if (!SceneArchiveCodec::Decode(sceneBytes, templateScene, diagnosticPath, false))
				throw std::runtime_error("Prefab component or hierarchy validation failed");

			PrefabArchive decoded;
			decoded.SchemaVersion = static_cast<uint32_t>(schemaVersion);
			decoded.RootLocalID = rootLocalID;
			decoded.Entities = std::move(records);
			decoded.TemplateScene = std::move(templateScene);
			if (!ValidateArchiveGraph(decoded, error))
				return false;
			archive = std::move(decoded);
			return true;
		}
		catch (const YAML::Exception& exception)
		{
			error = exception.what();
		}
		catch (const std::exception& exception)
		{
			error = exception.what();
		}
		TC_Core_Error("Failed to decode Prefab '{0}': {1}",
			PathToUTF8(diagnosticPath), error);
		return false;
	}

	bool PrefabArchiveCodec::SaveSubtree(const Ref<Scene>& source, Entity root,
		const std::filesystem::path& filepath, AssetHandle* savedHandle)
	{
		if (savedHandle)
			*savedHandle = AssetHandle(0);
		if (AssetTypeFromPath(filepath) != AssetType::Prefab)
		{
			TC_Core_Error("Prefab files must use the .tcprefab extension: {0}",
				PathToUTF8(filepath));
			return false;
		}
		AssetManager& assets = AssetManager::Get();
		if (assets.GetRegistry().IsInitialized()
			&& !assets.GetRegistry().IsManagedPath(filepath, true))
		{
			TC_Core_Error("Prefabs in an active project must be saved inside Assets: {0}",
				PathToUTF8(filepath));
			return false;
		}

		PrefabArchive archive;
		std::string error;
		std::string document;
		if (!CaptureSubtree(source, root, archive, error)
			|| !Encode(archive, document, error))
		{
			TC_Core_Error("Could not encode Prefab '{0}': {1}",
				PathToUTF8(filepath), error);
			return false;
		}
		std::string writeError;
		if (!FileSystem::WriteFileAtomically(filepath, document, writeError))
		{
			TC_Core_Error("Could not atomically replace Prefab '{0}': {1}",
				PathToUTF8(filepath), writeError);
			return false;
		}
		if (!assets.GetRegistry().IsInitialized())
			return true;
		const AssetHandle handle = assets.ImportAsset(filepath);
		const AssetMetadata* metadata = assets.GetRegistry().GetMetadata(handle);
		if (static_cast<uint64_t>(handle) == 0 || !metadata || metadata->IsMissing
			|| metadata->Type != AssetType::Prefab)
		{
			TC_Core_Error("Prefab was written but could not be registered: {0}",
				PathToUTF8(filepath));
			return false;
		}
		if (savedHandle)
			*savedHandle = handle;
		return true;
	}

	bool PrefabArchiveCodec::Load(const std::filesystem::path& filepath,
		PrefabArchive& archive, std::string& error)
	{
		if (AssetTypeFromPath(filepath) != AssetType::Prefab)
		{
			error = "Prefab files must use the .tcprefab extension";
			return false;
		}
		std::vector<uint8_t> bytes;
		if (!ReadAllBytes(filepath, bytes, error))
			return false;
		return Decode(bytes, filepath, archive, error);
	}

	bool PrefabArchiveCodec::Load(AssetHandle handle, PrefabArchive& archive,
		std::string& error)
	{
		AssetManager& assets = AssetManager::Get();
		AssetType type = AssetType::None;
		std::vector<uint8_t> bytes;
		if (static_cast<uint64_t>(handle) == 0
			|| !assets.ReadAssetBytes(handle, bytes, &type) || type != AssetType::Prefab)
		{
			error = "Cooked asset is unavailable or is not a Prefab";
			return false;
		}
		return Decode(bytes, UTF8ToPath("CookedPrefab-" + std::to_string(
			static_cast<uint64_t>(handle))), archive, error);
	}

	bool PrefabArchiveCodec::ValidateCurrentFormat(const std::vector<uint8_t>& bytes,
		const std::filesystem::path& diagnosticPath)
	{
		PrefabArchive archive;
		std::string error;
		return Decode(bytes, diagnosticPath, archive, error);
	}

	bool PrefabArchiveCodec::Instantiate(const PrefabArchive& archive,
		Scene& destination, const PrefabInstantiateOptions& options,
		PrefabInstantiationResult& result, std::string& error)
	{
		result = {};
		error.clear();
		if (!ValidateArchiveGraph(archive, error))
			return false;
		Entity parent;
		if (options.Parent.has_value())
		{
			if (static_cast<uint64_t>(*options.Parent) == 0
				|| !(parent = destination.FindEntityByUUID(*options.Parent)))
			{
				error = "Prefab instance parent does not exist in the destination Scene";
				return false;
			}
		}

		std::unordered_map<UUID, UUID> localToGenerated;
		std::unordered_set<UUID> generated;
		for (const EntityArchive& record : archive.Entities)
		{
			UUID sceneID;
			while (static_cast<uint64_t>(sceneID) == 0
				|| destination.FindEntityByUUID(sceneID) || !generated.emplace(sceneID).second)
				sceneID = UUID();
			localToGenerated.emplace(UUID(record.LocalID), sceneID);
		}

		Ref<Scene> staged = CreateRef<Scene>();
		staged->SetSceneName(PrefabDiagnosticName);
		std::unordered_set<uint64_t> usedAttachmentIDs;
		CollectAttachmentIDs(destination, usedAttachmentIDs);
		for (const EntityArchive& record : archive.Entities)
		{
			Entity source = archive.TemplateScene->FindEntityByUUID(UUID(record.LocalID));
			const UUID generatedID = localToGenerated.at(UUID(record.LocalID));
			Entity target = staged->CreateEntityWithUUID(generatedID, source.GetName());
			if (!target || !ComponentCodecs::CopyAuthoringComponents(source, target,
				false, error))
				return false;
			if (!ComponentCodecs::RemapInstanceReferences(target, localToGenerated,
				ComponentCodecs::MissingEntityReferencePolicy::Reject,
				usedAttachmentIDs, true, error))
				return false;
		}
		for (const EntityArchive& record : archive.Entities)
		{
			if (record.ParentLocalID == 0)
				continue;
			if (!staged->SetParent(
				staged->FindEntityByUUID(localToGenerated.at(UUID(record.LocalID))),
				staged->FindEntityByUUID(localToGenerated.at(UUID(record.ParentLocalID)))))
			{
				error = "Could not stage Prefab hierarchy";
				return false;
			}
		}
		Entity stagedRoot = staged->FindEntityByUUID(
			localToGenerated.at(UUID(archive.RootLocalID)));
		if (options.RootWorldPosition.has_value())
		{
			glm::mat4 world = stagedRoot.GetComponent<Transform>().GetTransform();
			world[3][0] = options.RootWorldPosition->x;
			world[3][1] = options.RootWorldPosition->y;
			world[3][2] = options.RootWorldPosition->z;
			if (!staged->SetWorldTransform(stagedRoot, world))
			{
				error = "Prefab root position is not a finite decomposable transform";
				return false;
			}
		}
		if (!staged->SyncTransformHierarchy())
		{
			error = "Staged Prefab hierarchy is invalid";
			return false;
		}

		std::vector<UUID> createdIDs;
		createdIDs.reserve(archive.Entities.size());
		auto rollback = [&]()
		{
			for (auto it = createdIDs.rbegin(); it != createdIDs.rend(); ++it)
			{
				Entity created = destination.FindEntityByUUID(*it);
				if (created)
					destination.DestroyEntity(created);
			}
		};

		for (const EntityArchive& record : archive.Entities)
		{
			const UUID generatedID = localToGenerated.at(UUID(record.LocalID));
			Entity source = staged->FindEntityByUUID(generatedID);
			Entity target = destination.CreateEntityWithUUID(generatedID, source.GetName());
			if (!target)
			{
				rollback();
				return false;
			}
			createdIDs.push_back(generatedID);
			if (!ComponentCodecs::CopyAuthoringComponents(source, target,
				options.ResolveAssets, error))
			{
				rollback();
				return false;
			}
		}
		for (const EntityArchive& record : archive.Entities)
		{
			if (record.ParentLocalID == 0)
				continue;
			if (!destination.SetParent(
				destination.FindEntityByUUID(localToGenerated.at(UUID(record.LocalID))),
				destination.FindEntityByUUID(localToGenerated.at(UUID(record.ParentLocalID)))))
			{
				error = "Could not commit Prefab hierarchy";
				rollback();
				return false;
			}
		}

		Entity root = destination.FindEntityByUUID(
			localToGenerated.at(UUID(archive.RootLocalID)));
		if (parent && !destination.SetParent(root, parent))
		{
			error = "Could not attach Prefab root to the requested parent";
			rollback();
			return false;
		}
		if (!destination.SyncTransformHierarchy())
		{
			error = "Committed Prefab transform hierarchy is invalid";
			rollback();
			return false;
		}

		result.Root = root;
		result.Entities.reserve(archive.Entities.size());
		for (const EntityArchive& record : archive.Entities)
		{
			const UUID sceneID = localToGenerated.at(UUID(record.LocalID));
			result.Entities.push_back(destination.FindEntityByUUID(sceneID));
			result.LocalToSceneUUID.emplace(record.LocalID, sceneID);
		}
		destination.QueueRuntimeEntityBatchCreated(createdIDs);
		return true;
	}

}
