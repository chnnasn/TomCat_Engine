#include "tcpch.h"
#include "ScriptEngine.h"

#include "TomCat/Core/Input.h"
#include "TomCat/Core/KeyCodes.h"
#include "TomCat/Core/Log.h"
#include "TomCat/Scene/Components.h"
#include "TomCat/Scene/Entity.h"
#include "TomCat/Scene/Scene.h"
#include "TomCat/Scene/Serialization/PrefabArchiveCodec.h"

#include <cmath>
#include <iomanip>
#include <limits>
#include <locale>
#include <sstream>
#include <type_traits>

namespace TomCat::Scripting {

	namespace {

		void AppendJsonString(std::string& output, std::string_view value)
		{
			static constexpr char Hex[] = "0123456789abcdef";
			output.push_back('"');
			for (const unsigned char character : value)
			{
				switch (character)
				{
					case '"': output += "\\\""; break;
					case '\\': output += "\\\\"; break;
					case '\b': output += "\\b"; break;
					case '\f': output += "\\f"; break;
					case '\n': output += "\\n"; break;
					case '\r': output += "\\r"; break;
					case '\t': output += "\\t"; break;
					default:
						if (character < 0x20)
						{
							output += "\\u00";
							output.push_back(Hex[character >> 4]);
							output.push_back(Hex[character & 0x0f]);
						}
						else
							output.push_back(static_cast<char>(character));
						break;
				}
			}
			output.push_back('"');
		}

		template<typename T>
		void AppendNumber(std::string& output, T value)
		{
			std::ostringstream stream;
			stream.imbue(std::locale::classic());
			if constexpr (std::is_floating_point_v<T>)
				stream << std::setprecision(std::numeric_limits<T>::max_digits10);
			stream << value;
			output += stream.str();
		}

		void AppendVector(std::string& output, const glm::vec2& value)
		{
			output.push_back('[');
			AppendNumber(output, value.x); output.push_back(',');
			AppendNumber(output, value.y); output.push_back(']');
		}

		void AppendVector(std::string& output, const glm::vec3& value)
		{
			output.push_back('[');
			AppendNumber(output, value.x); output.push_back(',');
			AppendNumber(output, value.y); output.push_back(',');
			AppendNumber(output, value.z); output.push_back(']');
		}

		void AppendVector(std::string& output, const glm::vec4& value)
		{
			output.push_back('[');
			AppendNumber(output, value.x); output.push_back(',');
			AppendNumber(output, value.y); output.push_back(',');
			AppendNumber(output, value.z); output.push_back(',');
			AppendNumber(output, value.w); output.push_back(']');
		}

		void AppendFieldValue(std::string& output, const ScriptFieldValue& value)
		{
			std::visit([&](const auto& current)
			{
				using T = std::decay_t<decltype(current)>;
				if constexpr (std::is_same_v<T, bool>)
					output += current ? "true" : "false";
				else if constexpr (std::is_same_v<T, std::string>)
					AppendJsonString(output, current);
				else if constexpr (std::is_same_v<T, glm::vec2>
					|| std::is_same_v<T, glm::vec3> || std::is_same_v<T, glm::vec4>)
					AppendVector(output, current);
				else
					AppendNumber(output, current);
			}, value);
		}

		bool IsSuccess(ScriptStatus status)
		{
			return status == ScriptStatus::Success;
		}

		bool IsSupportedComponentType(NativeComponentType type)
		{
			switch (type)
			{
				case NativeComponentType::Transform:
				case NativeComponentType::Rigidbody2D:
				case NativeComponentType::BoxCollider2D:
				case NativeComponentType::CircleCollider2D:
				case NativeComponentType::DistanceJoint2D:
				case NativeComponentType::SpriteRenderer:
					return true;
			}
			return false;
		}

	}

	ScriptEngine& ScriptEngine::Get()
	{
		static ScriptEngine engine;
		return engine;
	}

	ScriptEngine::ScriptEngine()
		: m_MainThread(std::this_thread::get_id())
	{
	}

	void ScriptEngine::SetRuntime(std::shared_ptr<IScriptRuntime> runtime)
	{
		std::lock_guard<std::mutex> lock(m_Mutex);
		if (!m_Scenes.empty())
		{
			TC_Core_Error("Cannot replace the script runtime while a scene is running");
			return;
		}
		m_Runtime = std::move(runtime);
		m_DeferredCommands.clear();
	}

	std::shared_ptr<IScriptRuntime> ScriptEngine::GetRuntime() const
	{
		std::lock_guard<std::mutex> lock(m_Mutex);
		return m_Runtime;
	}

	void ScriptEngine::ReportFailure(const char* operation, ScriptStatus status) const
	{
		if (status != ScriptStatus::Success)
			TC_Core_Error("C# script operation {0} failed with status {1}",
				operation ? operation : "<unknown>", static_cast<int32_t>(status));
	}

	std::string ScriptEngine::SerializeFields(Scene& scene) const
	{
		return SerializeFields(scene, std::span<const UUID>(scene.m_EntityOrder));
	}

	std::string ScriptEngine::SerializeFields(Scene& scene,
		std::span<const UUID> entityIDs) const
	{
		std::string output = "{\"attachments\":[";
		bool firstAttachment = true;
		for (UUID entityId : entityIDs)
		{
			Entity entity = scene.FindEntityByUUID(entityId);
			if (!entity || !entity.HasComponent<CSharpScripts>())
				continue;
			for (const CSharpScriptEntry& script : entity.GetComponent<CSharpScripts>().Scripts)
			{
				if (!firstAttachment) output.push_back(',');
				firstAttachment = false;
				output += "{\"attachmentId\":";
				AppendNumber(output, static_cast<uint64_t>(script.AttachmentID));
				output += ",\"fields\":[";
				bool firstField = true;
				for (const ScriptField& field : script.Fields)
				{
					if (!firstField) output.push_back(',');
					firstField = false;
					output += "{\"fieldId\":";
					AppendJsonString(output, field.FieldID);
					output += ",\"name\":";
					AppendJsonString(output, field.Name);
					output += ",\"type\":";
					AppendJsonString(output, ScriptFieldTypeToString(field.Type));
					output += ",\"value\":";
					AppendFieldValue(output, field.Value);
					output.push_back('}');
				}
				output += "]}";
			}
		}
		output += "]}";
		return output;
	}

	bool ScriptEngine::InstantiateRuntimeAttachments(Scene& scene,
		uint64_t sceneSessionId, std::span<const UUID> entityIDs)
	{
		uint64_t runtimeGeneration = 0;
		{
			std::lock_guard<std::mutex> lock(m_Mutex);
			const auto binding = m_Scenes.find(sceneSessionId);
			if (binding == m_Scenes.end() || binding->second.ScenePointer != &scene)
				return false;
			runtimeGeneration = binding->second.RuntimeGeneration;
		}

		auto runtime = GetRuntime();
		if (!runtime || !runtime->IsReady() || runtimeGeneration == 0)
			return false;
		std::vector<NativeScriptAttachmentV1> attachments;
		for (UUID entityId : entityIDs)
		{
			Entity entity = scene.FindEntityByUUID(entityId);
			if (!entity || !entity.HasComponent<CSharpScripts>())
				continue;
			for (const CSharpScriptEntry& script : entity.GetComponent<CSharpScripts>().Scripts)
			{
				NativeScriptAttachmentV1 attachment;
				attachment.Entity = EntityHandleV1{ sceneSessionId,
					static_cast<uint64_t>(entityId), runtimeGeneration };
				attachment.AttachmentId = static_cast<uint64_t>(script.AttachmentID);
				attachment.ScriptAsset = static_cast<uint64_t>(script.ScriptAsset);
				attachment.Enabled = script.Enabled ? 1 : 0;
				attachments.push_back(attachment);
			}
		}
		if (attachments.empty())
			return true;

		const std::string fields = SerializeFields(scene, entityIDs);
		const ScriptStatus status = runtime->InstantiateAttachments(attachments, fields);
		if (!IsSuccess(status))
			ReportFailure("Instantiate dynamic script attachments", status);
		return IsSuccess(status);
	}

	void ScriptEngine::RollbackRuntimeEntityBatch(Scene& scene,
		std::span<const UUID> entityIDs, bool destroyManagedAttachments)
	{
		if (destroyManagedAttachments)
		{
			std::vector<uint64_t> attachmentIDs;
			for (UUID entityID : entityIDs)
			{
				Entity entity = scene.FindEntityByUUID(entityID);
				if (!entity || !entity.HasComponent<CSharpScripts>())
					continue;
				for (const CSharpScriptEntry& script :
					entity.GetComponent<CSharpScripts>().Scripts)
				{
					const uint64_t attachmentID =
						static_cast<uint64_t>(script.AttachmentID);
					if (attachmentID != 0)
						attachmentIDs.push_back(attachmentID);
				}
			}
			auto runtime = GetRuntime();
			if (runtime && !attachmentIDs.empty())
				ReportFailure("Rollback dynamic script attachments",
					runtime->DestroyAttachments(attachmentIDs));
		}

		// Remove the serialized records before destroying Entities. When startup
		// failed there is no live managed Scene left for NotifyEntityDestroyed;
		// after a dynamic failure the explicit batch destroy above is authoritative.
		for (UUID entityID : entityIDs)
		{
			Entity entity = scene.FindEntityByUUID(entityID);
			if (entity && entity.HasComponent<CSharpScripts>())
				entity.RemoveComponent<CSharpScripts>();
		}
		for (auto iterator = entityIDs.rbegin(); iterator != entityIDs.rend(); ++iterator)
		{
			Entity entity = scene.FindEntityByUUID(*iterator);
			if (entity)
				scene.DestroyEntity(entity);
		}
	}

	void ScriptEngine::InstallRuntimeEntityBatchCallback(Scene& scene,
		uint64_t sceneSessionId)
	{
		scene.SetRuntimeEntityBatchCreatedCallback(
			[this, &scene, sceneSessionId](std::span<const UUID> entityIDs)
			{
				if (InstantiateRuntimeAttachments(scene, sceneSessionId, entityIDs))
				{
					// Managed OnCreate/OnEnable may have queued commands. Commit those
					// only after the managed batch callback has returned.
					FlushDeferredCommands(sceneSessionId);
					return;
				}
				RollbackRuntimeEntityBatch(scene, entityIDs, true);
			});
	}

	uint64_t ScriptEngine::StartScene(Scene& scene, uint64_t runtimeGeneration)
	{
		return StartSceneCore(scene, runtimeGeneration,
			std::span<const UUID>(scene.m_EntityOrder), true);
	}

	uint64_t ScriptEngine::StartSceneForRuntimeBatch(Scene& scene,
		uint64_t runtimeGeneration, std::span<const UUID> entityIDs)
	{
		// Anything outside this just-created batch is initial state. This keeps
		// the first runtime batch on the incremental ABI even when a runtime is
		// established lazily for a Scene that entered Play without scripts.
		std::vector<UUID> initialEntityIDs;
		initialEntityIDs.reserve(scene.m_EntityOrder.size());
		for (UUID entityID : scene.m_EntityOrder)
		{
			if (std::find(entityIDs.begin(), entityIDs.end(), entityID)
				== entityIDs.end())
				initialEntityIDs.push_back(entityID);
		}

		const uint64_t sceneSessionId = StartSceneCore(scene, runtimeGeneration,
			initialEntityIDs, false);
		if (sceneSessionId == 0)
		{
			RollbackRuntimeEntityBatch(scene, entityIDs, false);
			return 0;
		}

		if (!InstantiateRuntimeAttachments(scene, sceneSessionId, entityIDs))
			RollbackRuntimeEntityBatch(scene, entityIDs, true);
		else
			FlushDeferredCommands(sceneSessionId);
		return sceneSessionId;
	}

	uint64_t ScriptEngine::StartSceneCore(Scene& scene, uint64_t runtimeGeneration,
		std::span<const UUID> initialEntityIDs, bool flushPendingCreates)
	{
		auto runtime = GetRuntime();
		if (!runtime || !runtime->IsReady() || runtimeGeneration == 0)
			return 0;

		uint64_t session = 0;
		{
			std::lock_guard<std::mutex> lock(m_Mutex);
			do { session = m_NextSceneSessionId++; }
			while (session == 0 || m_Scenes.find(session) != m_Scenes.end());
			m_Scenes.emplace(session, SceneBinding{ &scene, runtimeGeneration });
		}
		InstallRuntimeEntityBatchCallback(scene, session);

		std::vector<NativeScriptAttachmentV1> attachments;
		for (UUID entityId : initialEntityIDs)
		{
			Entity entity = scene.FindEntityByUUID(entityId);
			if (!entity || !entity.HasComponent<CSharpScripts>())
				continue;
			for (const CSharpScriptEntry& script : entity.GetComponent<CSharpScripts>().Scripts)
			{
				NativeScriptAttachmentV1 attachment;
				attachment.Entity = EntityHandleV1{ session,
					static_cast<uint64_t>(entityId), runtimeGeneration };
				attachment.AttachmentId = static_cast<uint64_t>(script.AttachmentID);
				attachment.ScriptAsset = static_cast<uint64_t>(script.ScriptAsset);
				attachment.Enabled = script.Enabled ? 1 : 0;
				attachments.push_back(attachment);
			}
		}

		ScriptStatus status = runtime->CreateSceneRuntime(session, runtimeGeneration);
		if (IsSuccess(status)) status = runtime->InstantiateAll(attachments);
		const std::string fields = SerializeFields(scene, initialEntityIDs);
		if (IsSuccess(status)) status = runtime->ApplySerializedFields(fields);
		if (IsSuccess(status)) status = runtime->InvokeCreateAll();
		if (!IsSuccess(status))
		{
			scene.SetRuntimeEntityBatchCreatedCallback({});
			ReportFailure("StartScene", status);
			ReportFailure("DestroyAll after failed StartScene", runtime->DestroyAll());
			bool unloaded = false;
			for (uint32_t attempt = 0; attempt < 8 && !unloaded; ++attempt)
				unloaded = runtime->PollUnload();
			if (!unloaded)
			{
				TC_Core_Error("The failed Play AssemblyLoadContext did not unload; restart the Editor before loading another script generation");
				runtime->OnUnloadFailed("The failed Play AssemblyLoadContext did not unload");
			}
			std::lock_guard<std::mutex> lock(m_Mutex);
			m_Scenes.erase(session);
			return 0;
		}
		FlushDeferredCommands(session);
		if (flushPendingCreates)
			scene.FlushPendingRuntimeEntityCreates();
		return session;
	}

	void ScriptEngine::StopScene(uint64_t sceneSessionId)
	{
		if (sceneSessionId == 0)
			return;
		Scene* scene = nullptr;
		{
			std::lock_guard<std::mutex> lock(m_Mutex);
			const auto binding = m_Scenes.find(sceneSessionId);
			if (binding != m_Scenes.end())
				scene = binding->second.ScenePointer;
		}
		if (scene)
			scene->SetRuntimeEntityBatchCreatedCallback({});
		auto runtime = GetRuntime();
		if (runtime)
			ReportFailure("DestroyAll", runtime->DestroyAll());
		if (runtime)
		{
			bool unloaded = false;
			for (uint32_t attempt = 0; attempt < 8 && !unloaded; ++attempt)
				unloaded = runtime->PollUnload();
			if (!unloaded)
			{
				TC_Core_Error("The Play AssemblyLoadContext did not unload; restart the Editor before loading another script generation");
				runtime->OnUnloadFailed("The Play AssemblyLoadContext did not unload");
			}
		}
		std::lock_guard<std::mutex> lock(m_Mutex);
		m_Scenes.erase(sceneSessionId);
		m_DeferredCommands.erase(std::remove_if(m_DeferredCommands.begin(),
			m_DeferredCommands.end(), [sceneSessionId](const DeferredCommand& command)
			{
				return command.Entity.SceneSessionId == 0
					|| command.Entity.SceneSessionId == sceneSessionId;
			}), m_DeferredCommands.end());
	}

	void ScriptEngine::UpdateAll(uint64_t sceneSessionId, float deltaTime)
	{
		auto runtime = GetRuntime();
		if (!runtime || !std::isfinite(deltaTime) || deltaTime < 0.0f)
			return;
		ReportFailure("UpdateAll", runtime->UpdateAll(deltaTime));
		FlushDeferredCommands(sceneSessionId);
	}

	void ScriptEngine::FixedUpdateAll(uint64_t sceneSessionId, float fixedDeltaTime)
	{
		auto runtime = GetRuntime();
		if (!runtime || !std::isfinite(fixedDeltaTime) || fixedDeltaTime <= 0.0f)
			return;
		ReportFailure("FixedUpdateAll", runtime->FixedUpdateAll(fixedDeltaTime));
		FlushDeferredCommands(sceneSessionId);
	}

	void ScriptEngine::DispatchPhysicsEvents(uint64_t sceneSessionId,
		std::span<const NativePhysicsEventV1> events)
	{
		auto runtime = GetRuntime();
		if (!runtime || events.empty())
			return;
		ReportFailure("DispatchPhysicsEvents", runtime->DispatchPhysicsEvents(events));
		FlushDeferredCommands(sceneSessionId);
	}

	Scene* ScriptEngine::ResolveScene(const EntityHandleV1& handle) const
	{
		if (handle.SceneSessionId == 0 || handle.EntityId == 0
			|| handle.RuntimeGeneration == 0)
			return nullptr;
		std::lock_guard<std::mutex> lock(m_Mutex);
		const auto iterator = m_Scenes.find(handle.SceneSessionId);
		return iterator != m_Scenes.end()
			&& iterator->second.RuntimeGeneration == handle.RuntimeGeneration
			? iterator->second.ScenePointer : nullptr;
	}

	Entity ScriptEngine::ResolveEntity(const EntityHandleV1& handle) const
	{
		Scene* scene = ResolveScene(handle);
		return scene ? scene->FindEntityByUUID(UUID(handle.EntityId)) : Entity{};
	}

	uint64_t ScriptEngine::GetRuntimeGeneration(uint64_t sceneSessionId) const
	{
		std::lock_guard<std::mutex> lock(m_Mutex);
		const auto iterator = m_Scenes.find(sceneSessionId);
		return iterator == m_Scenes.end() ? 0 : iterator->second.RuntimeGeneration;
	}

	bool ScriptEngine::IsMainThread() const
	{
		return std::this_thread::get_id() == m_MainThread;
	}

	bool ScriptEngine::GetBehaviourEnabled(uint64_t attachmentId, bool& enabled) const
	{
		if (attachmentId == 0)
			return false;
		std::lock_guard<std::mutex> lock(m_Mutex);
		for (const auto& [ignoredSession, binding] : m_Scenes)
		{
			(void)ignoredSession;
			if (!binding.ScenePointer)
				continue;
			for (UUID entityId : binding.ScenePointer->m_EntityOrder)
			{
				Entity entity = binding.ScenePointer->FindEntityByUUID(entityId);
				if (!entity || !entity.HasComponent<CSharpScripts>())
					continue;
				for (const auto& script : entity.GetComponent<CSharpScripts>().Scripts)
				{
					if (static_cast<uint64_t>(script.AttachmentID) == attachmentId)
					{
						enabled = script.Enabled;
						return true;
					}
				}
			}
		}
		return false;
	}

	bool ScriptEngine::QueueCommand(DeferredCommand command)
	{
		std::lock_guard<std::mutex> lock(m_Mutex);
		if (command.Entity.SceneSessionId != 0
			&& m_Scenes.find(command.Entity.SceneSessionId) == m_Scenes.end())
			return false;
		m_DeferredCommands.push_back(command);
		return true;
	}

	bool ScriptEngine::QueueBehaviourEnabled(uint64_t attachmentId, bool enabled)
	{
		if (attachmentId == 0)
			return false;
		DeferredCommand command;
		command.Kind = DeferredCommandKind::SetBehaviourEnabled;
		command.AttachmentId = attachmentId;
		command.Enabled = enabled;
		return QueueCommand(command);
	}

	bool ScriptEngine::QueueRemoveBehaviour(uint64_t attachmentId)
	{
		if (attachmentId == 0)
			return false;
		DeferredCommand command;
		command.Kind = DeferredCommandKind::RemoveBehaviour;
		command.AttachmentId = attachmentId;
		return QueueCommand(command);
	}

	bool ScriptEngine::QueueDestroyEntity(const EntityHandleV1& entity)
	{
		DeferredCommand command;
		command.Kind = DeferredCommandKind::DestroyEntity;
		command.Entity = entity;
		return ResolveEntity(entity) && QueueCommand(command);
	}

	bool ScriptEngine::QueueAddComponent(const EntityHandleV1& entity,
		NativeComponentType componentType)
	{
		if (!IsSupportedComponentType(componentType))
			return false;
		DeferredCommand command;
		command.Kind = DeferredCommandKind::AddComponent;
		command.Entity = entity;
		command.ComponentType = componentType;
		return ResolveEntity(entity) && QueueCommand(command);
	}

	bool ScriptEngine::QueueRemoveComponent(const EntityHandleV1& entity,
		NativeComponentType componentType)
	{
		if (!IsSupportedComponentType(componentType)
			|| componentType == NativeComponentType::Transform)
			return false;
		DeferredCommand command;
		command.Kind = DeferredCommandKind::RemoveComponent;
		command.Entity = entity;
		command.ComponentType = componentType;
		return ResolveEntity(entity) && QueueCommand(command);
	}

	bool ScriptEngine::QueueInstantiatePrefab(const EntityHandleV1& context,
		uint64_t prefabHandle, NativeVector3 worldPosition,
		const EntityHandleV1& parent)
	{
		if (prefabHandle == 0 || !std::isfinite(worldPosition.X)
			|| !std::isfinite(worldPosition.Y) || !std::isfinite(worldPosition.Z)
			|| !ResolveEntity(context))
			return false;
		const bool hasParent = parent.SceneSessionId != 0 || parent.EntityId != 0
			|| parent.RuntimeGeneration != 0;
		if (hasParent && (parent.SceneSessionId != context.SceneSessionId
			|| parent.RuntimeGeneration != context.RuntimeGeneration
			|| parent.EntityId == 0 || !ResolveEntity(parent)))
			return false;

		DeferredCommand command;
		command.Kind = DeferredCommandKind::InstantiatePrefab;
		command.Entity = context;
		command.AssetHandle = prefabHandle;
		command.WorldPosition = worldPosition;
		command.Parent = parent;
		return QueueCommand(command);
	}

	void ScriptEngine::FlushDeferredCommands(uint64_t sceneSessionId)
	{
		std::vector<DeferredCommand> commands;
		{
			std::lock_guard<std::mutex> lock(m_Mutex);
			for (auto iterator = m_DeferredCommands.begin();
				iterator != m_DeferredCommands.end();)
			{
				if (iterator->Entity.SceneSessionId == 0
					|| iterator->Entity.SceneSessionId == sceneSessionId)
				{
					commands.push_back(*iterator);
					iterator = m_DeferredCommands.erase(iterator);
				}
				else ++iterator;
			}
		}

		auto runtime = GetRuntime();
		for (const DeferredCommand& command : commands)
		{
			if (command.Kind == DeferredCommandKind::SetBehaviourEnabled)
			{
				bool applied = false;
				Scene* owner = nullptr;
				{
					std::lock_guard<std::mutex> lock(m_Mutex);
					for (auto& [session, binding] : m_Scenes)
					{
						if (session != sceneSessionId || !binding.ScenePointer) continue;
						for (UUID entityId : binding.ScenePointer->m_EntityOrder)
						{
							Entity entity = binding.ScenePointer->FindEntityByUUID(entityId);
							if (!entity || !entity.HasComponent<CSharpScripts>()) continue;
							for (auto& script : entity.GetComponent<CSharpScripts>().Scripts)
							{
								if (static_cast<uint64_t>(script.AttachmentID) == command.AttachmentId)
								{
									script.Enabled = command.Enabled;
									owner = binding.ScenePointer;
									applied = true;
									break;
								}
							}
							if (applied) break;
						}
						if (applied) break;
					}
				}
				(void)owner;
				if (applied && runtime)
					ReportFailure("SetEnabled", runtime->SetEnabled(
						command.AttachmentId, command.Enabled));
				continue;
			}
			if (command.Kind == DeferredCommandKind::RemoveBehaviour)
			{
				Scene* ownerScene = nullptr;
				{
					std::lock_guard<std::mutex> lock(m_Mutex);
					const auto binding = m_Scenes.find(sceneSessionId);
					if (binding != m_Scenes.end())
						ownerScene = binding->second.ScenePointer;
				}
				if (!ownerScene)
					continue;

				Entity owner;
				for (UUID entityId : ownerScene->m_EntityOrder)
				{
					Entity candidate = ownerScene->FindEntityByUUID(entityId);
					if (!candidate || !candidate.HasComponent<CSharpScripts>())
						continue;
					const auto& scripts = candidate.GetComponent<CSharpScripts>().Scripts;
					if (std::any_of(scripts.begin(), scripts.end(), [&](const CSharpScriptEntry& script)
					{
						return static_cast<uint64_t>(script.AttachmentID) == command.AttachmentId;
					}))
					{
						owner = candidate;
						break;
					}
				}
				if (!owner)
					continue;

				const std::array<uint64_t, 1> ids{ command.AttachmentId };
				const ScriptStatus destroyStatus = runtime
					? runtime->DestroyAttachments(ids) : ScriptStatus::Success;
				if (!IsSuccess(destroyStatus))
				{
					ReportFailure("DestroyAttachments", destroyStatus);
					continue;
				}
				if (!owner || !owner.HasComponent<CSharpScripts>())
					continue;
				auto& scripts = owner.GetComponent<CSharpScripts>().Scripts;
				scripts.erase(std::remove_if(scripts.begin(), scripts.end(),
					[&](const CSharpScriptEntry& script)
					{
						return static_cast<uint64_t>(script.AttachmentID)
							== command.AttachmentId;
					}), scripts.end());
				continue;
			}
			if (command.Kind == DeferredCommandKind::InstantiatePrefab)
			{
				Scene* scene = ResolveScene(command.Entity);
				if (!scene || !ResolveEntity(command.Entity))
					continue;
				PrefabArchive archive;
				std::string error;
				if (!PrefabArchiveCodec::Load(TomCat::AssetHandle(command.AssetHandle),
					archive, error))
				{
					TC_Core_Error("Could not load Prefab asset {0}: {1}",
						command.AssetHandle, error);
					continue;
				}

				PrefabInstantiateOptions options;
				options.RootWorldPosition = glm::vec3(command.WorldPosition.X,
					command.WorldPosition.Y, command.WorldPosition.Z);
				const bool hasParent = command.Parent.SceneSessionId != 0
					|| command.Parent.EntityId != 0
					|| command.Parent.RuntimeGeneration != 0;
				if (hasParent)
				{
					Entity parent = ResolveEntity(command.Parent);
					if (!parent)
						continue;
					options.Parent = UUID(command.Parent.EntityId);
				}
				PrefabInstantiationResult instance;
				if (!PrefabArchiveCodec::Instantiate(archive, *scene, options,
					instance, error))
				{
					TC_Core_Error("Could not instantiate Prefab asset {0}: {1}",
						command.AssetHandle, error);
				}
				continue;
			}

			Entity entity = ResolveEntity(command.Entity);
			if (!entity)
				continue;
			if (command.Kind == DeferredCommandKind::DestroyEntity)
			{
				Scene* scene = ResolveScene(command.Entity);
				if (scene) scene->DestroyEntity(entity);
				continue;
			}

			const bool adding = command.Kind == DeferredCommandKind::AddComponent;
			auto add = [&]<typename T>() { if (!entity.HasComponent<T>()) entity.AddComponent<T>(); };
			auto remove = [&]<typename T>() { if (entity.HasComponent<T>()) entity.RemoveComponent<T>(); };
			auto mutate = [&]<typename T>()
			{
				if (adding) add.template operator()<T>();
				else remove.template operator()<T>();
			};
			switch (command.ComponentType)
			{
				case NativeComponentType::Transform: if (adding) add.template operator()<Transform>(); break;
				case NativeComponentType::Rigidbody2D: mutate.template operator()<Rigidbody2D>(); break;
				case NativeComponentType::BoxCollider2D: mutate.template operator()<BoxCollider2D>(); break;
				case NativeComponentType::CircleCollider2D: mutate.template operator()<CircleCollider2D>(); break;
				case NativeComponentType::DistanceJoint2D: mutate.template operator()<DistanceJoint2D>(); break;
				case NativeComponentType::SpriteRenderer: mutate.template operator()<SpriteRenderer>(); break;
			}
		}
	}

	void ScriptEngine::NotifyEntityDestroyed(Scene& scene, uint64_t entityId)
	{
		if (entityId == 0)
			return;
		uint64_t sceneSessionId = 0;
		{
			std::lock_guard<std::mutex> lock(m_Mutex);
			for (const auto& [session, binding] : m_Scenes)
			{
				if (binding.ScenePointer == &scene)
				{
					sceneSessionId = session;
					break;
				}
			}
		}
		if (sceneSessionId == 0)
			return;

		Entity entity = scene.FindEntityByUUID(UUID(entityId));
		if (!entity || !entity.HasComponent<CSharpScripts>())
			return;
		std::vector<uint64_t> ids;
		for (const CSharpScriptEntry& script : entity.GetComponent<CSharpScripts>().Scripts)
		{
			const uint64_t id = static_cast<uint64_t>(script.AttachmentID);
			if (id != 0)
				ids.push_back(id);
		}
		if (ids.empty())
			return;
		auto runtime = GetRuntime();
		if (runtime)
			ReportFailure("DestroyAttachments", runtime->DestroyAttachments(ids));
	}

	void ScriptEngine::CaptureInputState()
	{
		if (!IsMainThread())
			return;
		m_PreviousKeys = m_CurrentKeys;
		for (uint32_t key = 0; key < m_CurrentKeys.size(); ++key)
		{
			const bool valid = (key >= 32 && key <= 96) || (key >= 161 && key <= 162)
				|| (key >= 256 && key <= 269) || (key >= 280 && key <= 284)
				|| (key >= 290 && key <= 314) || (key >= 320 && key <= 336)
				|| (key >= 340 && key <= 348);
			m_CurrentKeys[key] = valid && Input::IsKeyPressed(static_cast<KeyCode>(key));
		}
		m_PreviousMousePosition = m_MousePosition;
		const auto [x, y] = Input::GetMousePosition();
		m_MousePosition = { x, y };
		m_MouseDelta = { x - m_PreviousMousePosition.X, y - m_PreviousMousePosition.Y };
	}

	bool ScriptEngine::IsKeyHeld(uint32_t key) const
	{
		return key < m_CurrentKeys.size() && m_CurrentKeys[key];
	}

	bool ScriptEngine::WasKeyPressed(uint32_t key) const
	{
		return key < m_CurrentKeys.size() && m_CurrentKeys[key] && !m_PreviousKeys[key];
	}

	bool ScriptEngine::WasKeyReleased(uint32_t key) const
	{
		return key < m_CurrentKeys.size() && !m_CurrentKeys[key] && m_PreviousKeys[key];
	}

	NativeVector2 ScriptEngine::GetMousePosition() const { return m_MousePosition; }
	NativeVector2 ScriptEngine::GetMouseDelta() const { return m_MouseDelta; }

	uint32_t ScriptEngine::GetModifiers() const
	{
		uint32_t result = 0;
		if (IsKeyHeld(Key::LeftShift) || IsKeyHeld(Key::RightShift)) result |= 1u;
		if (IsKeyHeld(Key::LeftControl) || IsKeyHeld(Key::RightControl)) result |= 2u;
		if (IsKeyHeld(Key::LeftAlt) || IsKeyHeld(Key::RightAlt)) result |= 4u;
		if (IsKeyHeld(Key::LeftSuper) || IsKeyHeld(Key::RightSuper)) result |= 8u;
		return result;
	}

}
