#include "tcpch.h"
#include "ScriptEngine.h"

#include "ScriptGlue.h"

#include "TomCat/Core/Input.h"
#include "TomCat/Core/KeyCodes.h"
#include "TomCat/Core/Log.h"
#include "TomCat/Scene/Components.h"
#include "TomCat/Scene/ComponentRegistry.h"
#include "TomCat/Scene/Entity.h"
#include "TomCat/Scene/Scene.h"
#include "TomCat/Scene/SceneCommandBuffer.h"
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
				case NativeComponentType::Camera:
				case NativeComponentType::SpriteAnimator:
					return true;
			}
			return false;
		}

		bool HasNativeComponent(Entity entity, NativeComponentType type)
		{
			if (!entity)
				return false;
			switch (type)
			{
				case NativeComponentType::Transform: return entity.HasComponent<Transform>();
				case NativeComponentType::Rigidbody2D: return entity.HasComponent<Rigidbody2D>();
				case NativeComponentType::BoxCollider2D: return entity.HasComponent<BoxCollider2D>();
				case NativeComponentType::CircleCollider2D: return entity.HasComponent<CircleCollider2D>();
				case NativeComponentType::DistanceJoint2D: return entity.HasComponent<DistanceJoint2D>();
				case NativeComponentType::SpriteRenderer: return entity.HasComponent<SpriteRenderer>();
				case NativeComponentType::Camera: return entity.HasComponent<C_Camera>();
				case NativeComponentType::SpriteAnimator: return entity.HasComponent<SpriteAnimator>();
			}
			return false;
		}

		bool SameEntity(const EntityHandleV1& left, const EntityHandleV1& right)
		{
			return left.SceneSessionId == right.SceneSessionId
				&& left.EntityId == right.EntityId
				&& left.RuntimeGeneration == right.RuntimeGeneration;
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
		m_PendingFixedInput.clear();
		m_ActiveFixedInput = {};
		m_ActiveFixedStepSceneSessionId = 0;
		m_InputDispatchPhase = InputDispatchPhase::DisplayFrame;
		m_FixedStepExposesTransitions = false;
		m_PreviousFixedStepInputDispatchPhase = InputDispatchPhase::DisplayFrame;
		m_PreviousFixedStepExposesTransitions = false;
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
			m_PendingFixedInput.try_emplace(session);
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
			m_PendingFixedInput.erase(session);
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
		m_PendingFixedInput.erase(sceneSessionId);
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
		const InputDispatchPhase previousPhase = m_InputDispatchPhase;
		const bool previousFixedTransitions = m_FixedStepExposesTransitions;
		m_InputDispatchPhase = InputDispatchPhase::DisplayFrame;
		m_FixedStepExposesTransitions = false;
		ScriptStatus status;
		try
		{
			status = runtime->UpdateAll(deltaTime);
		}
		catch (...)
		{
			m_InputDispatchPhase = previousPhase;
			m_FixedStepExposesTransitions = previousFixedTransitions;
			throw;
		}
		m_InputDispatchPhase = previousPhase;
		m_FixedStepExposesTransitions = previousFixedTransitions;
		ReportFailure("UpdateAll", status);
		FlushDeferredCommands(sceneSessionId);
	}

	bool ScriptEngine::BeginFixedStep(uint64_t sceneSessionId)
	{
		if (sceneSessionId == 0)
			return false;
		if (m_ActiveFixedStepSceneSessionId != 0)
		{
			TC_Core_Error("Cannot begin fixed step for scene {0}; scene {1} already owns the active fixed-step input batch",
				sceneSessionId, m_ActiveFixedStepSceneSessionId);
			return false;
		}

		m_PreviousFixedStepInputDispatchPhase = m_InputDispatchPhase;
		m_PreviousFixedStepExposesTransitions = m_FixedStepExposesTransitions;
		// Update and FixedUpdate are independent readers. Each scene accumulates
		// frozen display-frame batches until its next physics tick. The first fixed
		// substep consumes that ordered batch; catch-up substeps see no one-shot
		// transitions. Keep the consumed batch active through physics event dispatch,
		// so every managed callback belonging to the substep sees the same input.
		{
			std::lock_guard<std::mutex> lock(m_Mutex);
			auto [pending, inserted] = m_PendingFixedInput.try_emplace(sceneSessionId);
			if (inserted)
				AccumulateCurrentInput(pending->second);
			m_ActiveFixedInput = std::move(pending->second);
			pending->second = {};
		}
		m_ActiveFixedStepSceneSessionId = sceneSessionId;
		m_InputDispatchPhase = InputDispatchPhase::FixedUpdate;
		m_FixedStepExposesTransitions = m_ActiveFixedInput.LastFrameNumber != 0;
		return true;
	}

	void ScriptEngine::EndFixedStep(uint64_t sceneSessionId)
	{
		if (m_ActiveFixedStepSceneSessionId == 0)
			return;
		if (m_ActiveFixedStepSceneSessionId != sceneSessionId)
		{
			TC_Core_Error("Cannot end fixed step for scene {0}; scene {1} owns the active fixed-step input batch",
				sceneSessionId, m_ActiveFixedStepSceneSessionId);
			return;
		}

		m_ActiveFixedInput = {};
		m_ActiveFixedStepSceneSessionId = 0;
		m_InputDispatchPhase = m_PreviousFixedStepInputDispatchPhase;
		m_FixedStepExposesTransitions = m_PreviousFixedStepExposesTransitions;
		m_PreviousFixedStepInputDispatchPhase = InputDispatchPhase::DisplayFrame;
		m_PreviousFixedStepExposesTransitions = false;
	}

	void ScriptEngine::FixedUpdateAll(uint64_t sceneSessionId, float fixedDeltaTime)
	{
		auto runtime = GetRuntime();
		if (!runtime || !std::isfinite(fixedDeltaTime) || fixedDeltaTime <= 0.0f)
			return;
		const bool ownsFixedStep = m_ActiveFixedStepSceneSessionId == 0;
		if (ownsFixedStep && !BeginFixedStep(sceneSessionId))
			return;
		if (m_ActiveFixedStepSceneSessionId != sceneSessionId)
		{
			TC_Core_Error("Cannot dispatch FixedUpdate for scene {0} while scene {1} owns the fixed-step input batch",
				sceneSessionId, m_ActiveFixedStepSceneSessionId);
			return;
		}

		ScriptStatus status;
		try
		{
			status = runtime->FixedUpdateAll(fixedDeltaTime);
			ReportFailure("FixedUpdateAll", status);
			FlushDeferredCommands(sceneSessionId);
		}
		catch (...)
		{
			if (ownsFixedStep)
				EndFixedStep(sceneSessionId);
			throw;
		}
		if (ownsFixedStep)
			EndFixedStep(sceneSessionId);
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

	bool ScriptEngine::IsPendingCreate(const EntityHandleV1& entity) const
	{
		if (entity.SceneSessionId == 0 || entity.EntityId == 0
			|| entity.RuntimeGeneration == 0)
			return false;
		std::lock_guard<std::mutex> lock(m_Mutex);
		return std::any_of(m_DeferredCommands.begin(), m_DeferredCommands.end(),
			[&](const DeferredCommand& command)
			{
				return command.Kind == DeferredCommandKind::CreateEntity
					&& command.Entity.SceneSessionId == entity.SceneSessionId
					&& command.Entity.EntityId == entity.EntityId
					&& command.Entity.RuntimeGeneration == entity.RuntimeGeneration;
			});
	}

	bool ScriptEngine::GetProjectedComponentPresence(const EntityHandleV1& entity,
		NativeComponentType componentType, bool& present) const
	{
		present = false;
		if (!IsSupportedComponentType(componentType))
			return false;

		Entity resolved = ResolveEntity(entity);
		const bool pendingCreate = !resolved && IsPendingCreate(entity);
		if (!resolved && !pendingCreate)
			return false;
		present = resolved ? HasNativeComponent(resolved, componentType)
			: componentType == NativeComponentType::Transform;

		std::lock_guard<std::mutex> lock(m_Mutex);
		for (const DeferredCommand& command : m_DeferredCommands)
		{
			if (!SameEntity(command.Entity, entity)
				|| command.ComponentType != componentType)
				continue;
			if (command.Kind == DeferredCommandKind::AddComponent)
				present = true;
			else if (command.Kind == DeferredCommandKind::RemoveComponent)
				present = false;
		}
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
		return (ResolveEntity(entity) || IsPendingCreate(entity))
			&& QueueCommand(command);
	}

	bool ScriptEngine::QueueCreateEntity(const EntityHandleV1& context,
		std::string name, NativeVector3 worldPosition,
		const EntityHandleV1& parent, EntityHandleV1& reservedEntity)
	{
		reservedEntity = {};
		if (!IsMainThread() || !ResolveEntity(context) || name.size() > 1024
			|| !std::isfinite(worldPosition.X) || !std::isfinite(worldPosition.Y)
			|| !std::isfinite(worldPosition.Z))
			return false;
		const bool hasParent = parent.SceneSessionId != 0 || parent.EntityId != 0
			|| parent.RuntimeGeneration != 0;
		if (hasParent && (parent.SceneSessionId != context.SceneSessionId
			|| parent.RuntimeGeneration != context.RuntimeGeneration
			|| parent.EntityId == 0
			|| (!ResolveEntity(parent) && !IsPendingCreate(parent))))
			return false;

		for (uint32_t attempt = 0; attempt < 64; ++attempt)
		{
			const EntityHandleV1 candidate{ context.SceneSessionId,
				static_cast<uint64_t>(UUID()), context.RuntimeGeneration };
			if (candidate.EntityId == 0 || ResolveEntity(candidate)
				|| IsPendingCreate(candidate))
				continue;
			DeferredCommand command;
			command.Kind = DeferredCommandKind::CreateEntity;
			command.Entity = candidate;
			command.Parent = parent;
			command.WorldPosition = worldPosition;
			command.Name = std::move(name);
			if (!QueueCommand(std::move(command)))
				return false;
			reservedEntity = candidate;
			return true;
		}
		return false;
	}

	bool ScriptEngine::QueueSetParent(const EntityHandleV1& entity,
		const EntityHandleV1& parent)
	{
		if (!IsMainThread() || (!ResolveEntity(entity) && !IsPendingCreate(entity)))
			return false;
		const bool hasParent = parent.SceneSessionId != 0 || parent.EntityId != 0
			|| parent.RuntimeGeneration != 0;
		if (hasParent && (parent.SceneSessionId != entity.SceneSessionId
			|| parent.RuntimeGeneration != entity.RuntimeGeneration
			|| parent.EntityId == 0
			|| (!ResolveEntity(parent) && !IsPendingCreate(parent))))
			return false;
		DeferredCommand command;
		command.Kind = DeferredCommandKind::SetParent;
		command.Entity = entity;
		command.Parent = parent;
		return QueueCommand(std::move(command));
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
		return (ResolveEntity(entity) || IsPendingCreate(entity))
			&& QueueCommand(command);
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
		return (ResolveEntity(entity) || IsPendingCreate(entity))
			&& QueueCommand(command);
	}

	bool ScriptEngine::QueueSetComponentProperty(const EntityHandleV1& entity,
		NativeComponentType componentType, uint32_t propertyId,
		NativePropertyValueV1 value)
	{
		if (!IsMainThread() || !IsSupportedComponentType(componentType)
			|| propertyId == 0
			|| (!ResolveEntity(entity) && !IsPendingCreate(entity)))
			return false;
		bool present = false;
		if (!GetProjectedComponentPresence(entity, componentType, present) || !present)
			return false;
		DeferredCommand command;
		command.Kind = DeferredCommandKind::SetComponentProperty;
		command.Entity = entity;
		command.ComponentType = componentType;
		command.PropertyId = propertyId;
		command.PropertyValue = value;
		return QueueCommand(std::move(command));
	}

	bool ScriptEngine::QueueSetActiveSelf(const EntityHandleV1& entity, bool active)
	{
		if (!IsMainThread() || (!ResolveEntity(entity) && !IsPendingCreate(entity)))
			return false;
		DeferredCommand command;
		command.Kind = DeferredCommandKind::SetActiveSelf;
		command.Entity = entity;
		command.Enabled = active;
		return QueueCommand(std::move(command));
	}

	bool ScriptEngine::GetProjectedRegisteredComponentPresence(
		const EntityHandleV1& entity, uint64_t componentTypeId, bool& present) const
	{
		present = false;
		const ComponentDescriptor* descriptor = componentTypeId == 0 ? nullptr
			: ComponentRegistry::Get().Find(UUID(componentTypeId));
		if (!descriptor || !descriptor->ScriptAccessible)
			return false;
		Entity resolved = ResolveEntity(entity);
		if (!resolved && !IsPendingCreate(entity))
			return false;
		present = resolved && descriptor->Has(resolved);
		std::lock_guard<std::mutex> lock(m_Mutex);
		for (const DeferredCommand& command : m_DeferredCommands)
		{
			if (!SameEntity(command.Entity, entity)
				|| command.RegisteredTypeId != componentTypeId)
				continue;
			if (command.Kind == DeferredCommandKind::AddRegisteredComponent)
				present = true;
			else if (command.Kind == DeferredCommandKind::RemoveRegisteredComponent)
				present = false;
		}
		return true;
	}

	bool ScriptEngine::QueueAddRegisteredComponent(const EntityHandleV1& entity,
		uint64_t componentTypeId)
	{
		if (!IsMainThread())
			return false;
		bool present = false;
		if (!GetProjectedRegisteredComponentPresence(entity, componentTypeId, present)
			|| present)
			return false;
		DeferredCommand command;
		command.Kind = DeferredCommandKind::AddRegisteredComponent;
		command.Entity = entity;
		command.RegisteredTypeId = componentTypeId;
		return QueueCommand(std::move(command));
	}

	bool ScriptEngine::QueueRemoveRegisteredComponent(const EntityHandleV1& entity,
		uint64_t componentTypeId)
	{
		if (!IsMainThread())
			return false;
		const ComponentDescriptor* descriptor = componentTypeId == 0 ? nullptr
			: ComponentRegistry::Get().Find(UUID(componentTypeId));
		if (!descriptor || !descriptor->ScriptAccessible || !descriptor->Removable)
			return false;
		bool present = false;
		if (!GetProjectedRegisteredComponentPresence(entity, componentTypeId, present)
			|| !present)
			return false;
		DeferredCommand command;
		command.Kind = DeferredCommandKind::RemoveRegisteredComponent;
		command.Entity = entity;
		command.RegisteredTypeId = componentTypeId;
		return QueueCommand(std::move(command));
	}

	bool ScriptEngine::QueueSetRegisteredComponentProperty(
		const EntityHandleV1& entity, uint64_t componentTypeId,
		uint64_t propertyId, NativePropertyValueV1 value)
	{
		if (!IsMainThread() || propertyId == 0)
			return false;
		bool present = false;
		if (!GetProjectedRegisteredComponentPresence(entity, componentTypeId, present)
			|| !present)
			return false;
		DeferredCommand command;
		command.Kind = DeferredCommandKind::SetRegisteredComponentProperty;
		command.Entity = entity;
		command.RegisteredTypeId = componentTypeId;
		command.RegisteredPropertyId = propertyId;
		command.PropertyValue = value;
		return QueueCommand(std::move(command));
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
			|| parent.EntityId == 0
			|| (!ResolveEntity(parent) && !IsPendingCreate(parent))))
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
			if (command.Kind == DeferredCommandKind::CreateEntity)
			{
				Scene* scene = ResolveScene(command.Entity);
				if (!scene || scene->FindEntityByUUID(UUID(command.Entity.EntityId)))
					continue;
				const bool hasParent = command.Parent.SceneSessionId != 0
					|| command.Parent.EntityId != 0
					|| command.Parent.RuntimeGeneration != 0;
				std::optional<UUID> parent;
				if (hasParent)
				{
					Entity parentEntity = ResolveEntity(command.Parent);
					if (!parentEntity)
						continue;
					parent = parentEntity.GetUUID();
				}
				SceneCommandBuffer buffer(*scene);
				std::string error;
				if (!buffer.CreateEntityWithReservedId(UUID(command.Entity.EntityId),
					command.Name, parent) || !buffer.Flush(error))
				{
					TC_Core_Error("Could not create reserved C# entity {0}: {1}",
						command.Entity.EntityId, error);
					continue;
				}
				Entity created = scene->FindEntityByUUID(UUID(command.Entity.EntityId));
				if (!created || !scene->SetWorldTransform(created,
					Math::ComposeTransform(glm::vec3(command.WorldPosition.X,
						command.WorldPosition.Y, command.WorldPosition.Z),
						glm::vec3(0.0f), glm::vec3(1.0f))))
					TC_Core_Error("Could not apply the initial transform to reserved C# entity {0}",
						command.Entity.EntityId);
				continue;
			}
			if (command.Kind == DeferredCommandKind::SetComponentProperty)
			{
				const int32_t status = ApplyGameplayComponentPropertyNow(command.Entity,
					command.ComponentType, command.PropertyId, command.PropertyValue);
				if (status != static_cast<int32_t>(ScriptStatus::Success))
					TC_Core_Error("Could not apply queued C# component property {0} on entity {1}: status {2}",
						command.PropertyId, command.Entity.EntityId, status);
				continue;
			}
			if (command.Kind == DeferredCommandKind::SetActiveSelf)
			{
				Entity entity = ResolveEntity(command.Entity);
				if (!entity || !entity.HasComponent<Tag>())
				{
					TC_Core_Error("Could not apply queued C# ActiveSelf on entity {0}",
						command.Entity.EntityId);
					continue;
				}
				entity.GetComponent<Tag>().Visible = command.Enabled;
				continue;
			}
			if (command.Kind == DeferredCommandKind::SetRegisteredComponentProperty)
			{
				const int32_t status = ApplyRegisteredComponentPropertyNow(command.Entity,
					command.RegisteredTypeId, command.RegisteredPropertyId,
					command.PropertyValue);
				if (status != static_cast<int32_t>(ScriptStatus::Success))
					TC_Core_Error("Could not apply queued C# registered property {0} on entity {1}: status {2}",
						command.RegisteredPropertyId, command.Entity.EntityId, status);
				continue;
			}
			if (command.Kind == DeferredCommandKind::AddRegisteredComponent
				|| command.Kind == DeferredCommandKind::RemoveRegisteredComponent)
			{
				Entity entity = ResolveEntity(command.Entity);
				const ComponentDescriptor* descriptor = ComponentRegistry::Get().Find(
					UUID(command.RegisteredTypeId));
				std::string error;
				const bool adding = command.Kind
					== DeferredCommandKind::AddRegisteredComponent;
				if (!entity || !descriptor || !descriptor->ScriptAccessible
					|| !(adding ? descriptor->Add(entity, error)
						: descriptor->Remove(entity, error)))
					TC_Core_Error("Could not {0} queued C# registered component {1} on entity {2}: {3}",
						adding ? "add" : "remove", command.RegisteredTypeId,
						command.Entity.EntityId, error);
				continue;
			}
			if (command.Kind == DeferredCommandKind::SetParent)
			{
				Scene* scene = ResolveScene(command.Entity);
				Entity child = ResolveEntity(command.Entity);
				if (!scene || !child)
					continue;
				const bool hasParent = command.Parent.SceneSessionId != 0
					|| command.Parent.EntityId != 0
					|| command.Parent.RuntimeGeneration != 0;
				std::optional<UUID> parent;
				if (hasParent)
				{
					Entity parentEntity = ResolveEntity(command.Parent);
					if (!parentEntity)
						continue;
					parent = parentEntity.GetUUID();
				}
				SceneCommandBuffer buffer(*scene);
				std::string error;
				if (!buffer.ReparentEntity(child.GetUUID(), parent)
					|| !buffer.Flush(error))
					TC_Core_Error("Could not reparent C# entity {0}: {1}",
						command.Entity.EntityId, error);
				continue;
			}
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
				case NativeComponentType::Camera: mutate.template operator()<C_Camera>(); break;
				case NativeComponentType::SpriteAnimator: mutate.template operator()<SpriteAnimator>(); break;
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
		const InputEventQueue::FrameSnapshot& frame = Input::GetFrameSnapshot();
		if (frame.FrameNumber == m_LastCapturedInputFrame)
			return;
		m_LastCapturedInputFrame = frame.FrameNumber;
		m_InputEventsDroppedThisFrame = frame.DroppedEventCount;

		m_PreviousWindowFocused = m_WindowFocused;
		m_WindowFocused = Input::IsWindowFocused();
		m_CurrentKeys = frame.KeysHeld;
		m_KeysPressedThisFrame = frame.KeysPressed;
		m_KeysReleasedThisFrame = frame.KeysReleased;
		m_CurrentMouseButtons = frame.MouseButtonsHeld;
		m_MouseButtonsPressedThisFrame = frame.MouseButtonsPressed;
		m_MouseButtonsReleasedThisFrame = frame.MouseButtonsReleased;
		m_PreviousMousePosition = m_MousePosition;
		const auto [x, y] = Input::GetMousePosition();
		m_MousePosition = { x, y };
		m_MouseDelta = !m_WindowFocused || !m_PreviousWindowFocused
			? NativeVector2{}
			: NativeVector2{ x - m_PreviousMousePosition.X, y - m_PreviousMousePosition.Y };
		const auto [scrollX, scrollY] = Input::ConsumeScrollDelta();
		m_ScrollDelta = m_WindowFocused ? NativeVector2{ scrollX, scrollY }
			: NativeVector2{};

		m_PreviousGamepads = m_CurrentGamepads;
		for (uint32_t index = 0; index < m_CurrentGamepads.size(); ++index)
		{
			const Input::GamepadSnapshot input = Input::GetGamepadSnapshot(index);
			auto& output = m_CurrentGamepads[index];
			output.Connected = input.Connected;
			output.Name = input.Name;
			if (m_WindowFocused)
			{
				output.Buttons = input.Buttons;
				output.Axes = input.Axes;
			}
			else
			{
				output.Buttons.fill(false);
				output.Axes.fill(0.0f);
			}
			m_GamepadsConnectedThisFrame[index] = frame.GamepadsConnected[index]
				|| (output.Connected && !m_PreviousGamepads[index].Connected);
			m_GamepadsDisconnectedThisFrame[index] = frame.GamepadsDisconnected[index]
				|| (!output.Connected && m_PreviousGamepads[index].Connected);
			for (uint32_t button = 0; button < output.Buttons.size(); ++button)
			{
				m_GamepadButtonsPressedThisFrame[index][button] =
					frame.GamepadButtonsPressed[index][button]
					|| (output.Buttons[button]
						&& !m_PreviousGamepads[index].Buttons[button]);
				m_GamepadButtonsReleasedThisFrame[index][button] =
					frame.GamepadButtonsReleased[index][button]
					|| (!output.Buttons[button]
						&& m_PreviousGamepads[index].Buttons[button]);
			}
		}

		m_InputEventsThisFrame.clear();
		m_InputEventsThisFrame.reserve(frame.Events.size());
		for (const InputEventQueue::Event& event : frame.Events)
		{
			NativeInputDeviceV1 device = NativeInputDeviceV1::Keyboard;
			switch (event.Source)
			{
				case InputEventQueue::Device::Keyboard:
					device = NativeInputDeviceV1::Keyboard;
					break;
				case InputEventQueue::Device::Mouse:
					device = NativeInputDeviceV1::MouseButton;
					break;
				case InputEventQueue::Device::GamepadConnection:
					device = NativeInputDeviceV1::GamepadConnection;
					break;
				case InputEventQueue::Device::GamepadButton:
					device = NativeInputDeviceV1::GamepadButton;
					break;
			}
			NativeInputActionV1 action = NativeInputActionV1::Pressed;
			switch (event.Transition)
			{
				case InputEventQueue::Action::Pressed:
					action = NativeInputActionV1::Pressed;
					break;
				case InputEventQueue::Action::Released:
					action = NativeInputActionV1::Released;
					break;
				case InputEventQueue::Action::Repeated:
					action = NativeInputActionV1::Repeated;
					break;
			}
			m_InputEventsThisFrame.push_back({ event.Sequence, event.Timestamp,
				frame.FrameNumber, device, action, event.Code, event.DeviceIndex });
		}

		std::lock_guard<std::mutex> lock(m_Mutex);
		for (auto& [sceneSessionId, pending] : m_PendingFixedInput)
		{
			(void)sceneSessionId;
			AccumulateCurrentInput(pending);
		}
	}

	void ScriptEngine::AccumulateCurrentInput(FixedInputBatch& batch) const
	{
		if (m_LastCapturedInputFrame == 0)
			return;
		if (batch.FirstFrameNumber == 0)
			batch.FirstFrameNumber = m_LastCapturedInputFrame;
		batch.LastFrameNumber = m_LastCapturedInputFrame;
		batch.DroppedEventCount += m_InputEventsDroppedThisFrame;

		constexpr size_t maximumPendingEvents = 16384;
		const size_t incomingCount = m_InputEventsThisFrame.size();
		if (incomingCount >= maximumPendingEvents)
		{
			batch.DroppedEventCount += batch.Events.size()
				+ incomingCount - maximumPendingEvents;
			batch.Events.assign(m_InputEventsThisFrame.end() - maximumPendingEvents,
				m_InputEventsThisFrame.end());
		}
		else
		{
			const size_t total = batch.Events.size() + incomingCount;
			if (total > maximumPendingEvents)
			{
				const size_t removeCount = total - maximumPendingEvents;
				batch.Events.erase(batch.Events.begin(),
					batch.Events.begin() + removeCount);
				batch.DroppedEventCount += removeCount;
			}
			batch.Events.insert(batch.Events.end(), m_InputEventsThisFrame.begin(),
				m_InputEventsThisFrame.end());
		}

		auto accumulateFlags = [](auto& destination, const auto& source)
		{
			for (size_t index = 0; index < destination.size(); ++index)
				destination[index] = destination[index] || source[index];
		};
		accumulateFlags(batch.KeysPressed, m_KeysPressedThisFrame);
		accumulateFlags(batch.KeysReleased, m_KeysReleasedThisFrame);
		accumulateFlags(batch.MouseButtonsPressed,
			m_MouseButtonsPressedThisFrame);
		accumulateFlags(batch.MouseButtonsReleased,
			m_MouseButtonsReleasedThisFrame);
		accumulateFlags(batch.GamepadsConnected, m_GamepadsConnectedThisFrame);
		accumulateFlags(batch.GamepadsDisconnected,
			m_GamepadsDisconnectedThisFrame);
		for (size_t gamepad = 0; gamepad < batch.GamepadButtonsPressed.size(); ++gamepad)
		{
			accumulateFlags(batch.GamepadButtonsPressed[gamepad],
				m_GamepadButtonsPressedThisFrame[gamepad]);
			accumulateFlags(batch.GamepadButtonsReleased[gamepad],
				m_GamepadButtonsReleasedThisFrame[gamepad]);
		}
		batch.MouseDelta.X += m_MouseDelta.X;
		batch.MouseDelta.Y += m_MouseDelta.Y;
		batch.ScrollDelta.X += m_ScrollDelta.X;
		batch.ScrollDelta.Y += m_ScrollDelta.Y;
	}

	bool ScriptEngine::IsKeyHeld(uint32_t key) const
	{
		return key < m_CurrentKeys.size() && m_CurrentKeys[key];
	}

	bool ScriptEngine::WasKeyPressed(uint32_t key) const
	{
		if (m_InputDispatchPhase == InputDispatchPhase::FixedUpdate)
			return m_FixedStepExposesTransitions && key < m_ActiveFixedInput.KeysPressed.size()
				&& m_ActiveFixedInput.KeysPressed[key];
		return key < m_KeysPressedThisFrame.size() && m_KeysPressedThisFrame[key];
	}

	bool ScriptEngine::WasKeyReleased(uint32_t key) const
	{
		if (m_InputDispatchPhase == InputDispatchPhase::FixedUpdate)
			return m_FixedStepExposesTransitions && key < m_ActiveFixedInput.KeysReleased.size()
				&& m_ActiveFixedInput.KeysReleased[key];
		return key < m_KeysReleasedThisFrame.size() && m_KeysReleasedThisFrame[key];
	}

	bool ScriptEngine::IsMouseButtonHeld(uint32_t button) const
	{
		return button < m_CurrentMouseButtons.size() && m_CurrentMouseButtons[button];
	}

	bool ScriptEngine::WasMouseButtonPressed(uint32_t button) const
	{
		if (m_InputDispatchPhase == InputDispatchPhase::FixedUpdate)
			return m_FixedStepExposesTransitions
				&& button < m_ActiveFixedInput.MouseButtonsPressed.size()
				&& m_ActiveFixedInput.MouseButtonsPressed[button];
		return button < m_MouseButtonsPressedThisFrame.size()
			&& m_MouseButtonsPressedThisFrame[button];
	}

	bool ScriptEngine::WasMouseButtonReleased(uint32_t button) const
	{
		if (m_InputDispatchPhase == InputDispatchPhase::FixedUpdate)
			return m_FixedStepExposesTransitions
				&& button < m_ActiveFixedInput.MouseButtonsReleased.size()
				&& m_ActiveFixedInput.MouseButtonsReleased[button];
		return button < m_MouseButtonsReleasedThisFrame.size()
			&& m_MouseButtonsReleasedThisFrame[button];
	}

	NativeVector2 ScriptEngine::GetMousePosition() const { return m_MousePosition; }
	NativeVector2 ScriptEngine::GetMouseDelta() const
	{
		return m_InputDispatchPhase == InputDispatchPhase::FixedUpdate
			? (m_FixedStepExposesTransitions ? m_ActiveFixedInput.MouseDelta
				: NativeVector2{}) : m_MouseDelta;
	}
	NativeVector2 ScriptEngine::GetScrollDelta() const
	{
		return m_InputDispatchPhase == InputDispatchPhase::FixedUpdate
			? (m_FixedStepExposesTransitions ? m_ActiveFixedInput.ScrollDelta
				: NativeVector2{}) : m_ScrollDelta;
	}
	bool ScriptEngine::IsWindowFocused() const { return m_WindowFocused; }

	bool ScriptEngine::IsGamepadConnected(uint32_t gamepad) const
	{
		return gamepad < m_CurrentGamepads.size() && m_CurrentGamepads[gamepad].Connected;
	}

	bool ScriptEngine::WasGamepadConnected(uint32_t gamepad) const
	{
		if (m_InputDispatchPhase == InputDispatchPhase::FixedUpdate)
			return m_FixedStepExposesTransitions
				&& gamepad < m_ActiveFixedInput.GamepadsConnected.size()
				&& m_ActiveFixedInput.GamepadsConnected[gamepad];
		return gamepad < m_GamepadsConnectedThisFrame.size()
			&& m_GamepadsConnectedThisFrame[gamepad];
	}

	bool ScriptEngine::WasGamepadDisconnected(uint32_t gamepad) const
	{
		if (m_InputDispatchPhase == InputDispatchPhase::FixedUpdate)
			return m_FixedStepExposesTransitions
				&& gamepad < m_ActiveFixedInput.GamepadsDisconnected.size()
				&& m_ActiveFixedInput.GamepadsDisconnected[gamepad];
		return gamepad < m_GamepadsDisconnectedThisFrame.size()
			&& m_GamepadsDisconnectedThisFrame[gamepad];
	}

	bool ScriptEngine::IsGamepadButtonHeld(uint32_t gamepad, uint32_t button) const
	{
		return gamepad < m_CurrentGamepads.size()
			&& button < m_CurrentGamepads[gamepad].Buttons.size()
			&& m_CurrentGamepads[gamepad].Buttons[button];
	}

	bool ScriptEngine::WasGamepadButtonPressed(uint32_t gamepad, uint32_t button) const
	{
		if (m_InputDispatchPhase == InputDispatchPhase::FixedUpdate)
			return m_FixedStepExposesTransitions
				&& gamepad < m_ActiveFixedInput.GamepadButtonsPressed.size()
				&& button < m_ActiveFixedInput.GamepadButtonsPressed[gamepad].size()
				&& m_ActiveFixedInput.GamepadButtonsPressed[gamepad][button];
		return gamepad < m_GamepadButtonsPressedThisFrame.size()
			&& button < m_GamepadButtonsPressedThisFrame[gamepad].size()
			&& m_GamepadButtonsPressedThisFrame[gamepad][button];
	}

	bool ScriptEngine::WasGamepadButtonReleased(uint32_t gamepad, uint32_t button) const
	{
		if (m_InputDispatchPhase == InputDispatchPhase::FixedUpdate)
			return m_FixedStepExposesTransitions
				&& gamepad < m_ActiveFixedInput.GamepadButtonsReleased.size()
				&& button < m_ActiveFixedInput.GamepadButtonsReleased[gamepad].size()
				&& m_ActiveFixedInput.GamepadButtonsReleased[gamepad][button];
		return gamepad < m_GamepadButtonsReleasedThisFrame.size()
			&& button < m_GamepadButtonsReleasedThisFrame[gamepad].size()
			&& m_GamepadButtonsReleasedThisFrame[gamepad][button];
	}

	float ScriptEngine::GetGamepadAxis(uint32_t gamepad, uint32_t axis) const
	{
		return gamepad < m_CurrentGamepads.size()
			&& axis < m_CurrentGamepads[gamepad].Axes.size()
			? m_CurrentGamepads[gamepad].Axes[axis] : 0.0f;
	}

	const std::string& ScriptEngine::GetGamepadName(uint32_t gamepad) const
	{
		static const std::string empty;
		return gamepad < m_CurrentGamepads.size() ? m_CurrentGamepads[gamepad].Name : empty;
	}

	uint32_t ScriptEngine::GetModifiers() const
	{
		uint32_t result = 0;
		if (IsKeyHeld(Key::LeftShift) || IsKeyHeld(Key::RightShift)) result |= 1u;
		if (IsKeyHeld(Key::LeftControl) || IsKeyHeld(Key::RightControl)) result |= 2u;
		if (IsKeyHeld(Key::LeftAlt) || IsKeyHeld(Key::RightAlt)) result |= 4u;
		if (IsKeyHeld(Key::LeftSuper) || IsKeyHeld(Key::RightSuper)) result |= 8u;
		return result;
	}

	NativeInputEventBatchInfoV1 ScriptEngine::GetInputEventBatchInfo() const
	{
		const bool fixed = m_InputDispatchPhase == InputDispatchPhase::FixedUpdate;
		const std::vector<NativeInputEventV1>& events = fixed
			? m_ActiveFixedInput.Events : m_InputEventsThisFrame;
		NativeInputEventBatchInfoV1 result;
		result.FirstFrameNumber = fixed ? m_ActiveFixedInput.FirstFrameNumber
			: m_LastCapturedInputFrame;
		result.LastFrameNumber = fixed ? m_ActiveFixedInput.LastFrameNumber
			: m_LastCapturedInputFrame;
		result.FirstSequence = events.empty() ? 0 : events.front().Sequence;
		result.LastSequence = events.empty() ? 0 : events.back().Sequence;
		result.DroppedEventCount = fixed ? m_ActiveFixedInput.DroppedEventCount
			: m_InputEventsDroppedThisFrame;
		result.EventCount = static_cast<uint32_t>(events.size());
		return result;
	}

	const std::vector<NativeInputEventV1>& ScriptEngine::GetInputEvents() const
	{
		return m_InputDispatchPhase == InputDispatchPhase::FixedUpdate
			? m_ActiveFixedInput.Events : m_InputEventsThisFrame;
	}

}
