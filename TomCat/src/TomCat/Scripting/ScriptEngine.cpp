#include "tcpch.h"
#include "ScriptEngine.h"

#include "ScriptGlue.h"

#include "TomCat/Core/Input.h"
#include "TomCat/Core/KeyCodes.h"
#include "TomCat/Core/Log.h"
#include "TomCat/Math/Math.h"
#include "TomCat/Scene/Components.h"
#include "TomCat/Scene/ComponentRegistry.h"
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

		bool WriteProjectedProperty(PropertyKind kind,
			const PropertyValue& source, NativePropertyValueV1& output)
		{
			output = {};
			switch (kind)
			{
				case PropertyKind::Bool:
					output.Kind = static_cast<uint32_t>(NativePropertyKindV1::Bool);
					output.Integer = std::get<bool>(source) ? 1 : 0;
					return true;
				case PropertyKind::Int32:
					output.Kind = static_cast<uint32_t>(NativePropertyKindV1::Int32);
					output.Integer = std::get<int32_t>(source);
					return true;
				case PropertyKind::Int64:
					output.Kind = static_cast<uint32_t>(NativePropertyKindV1::Int64);
					output.Integer = std::get<int64_t>(source);
					return true;
				case PropertyKind::UInt32:
					output.Kind = static_cast<uint32_t>(NativePropertyKindV1::UInt32);
					output.Integer = static_cast<int64_t>(std::get<uint32_t>(source));
					return true;
				case PropertyKind::UInt64:
				{
					output.Kind = static_cast<uint32_t>(NativePropertyKindV1::UInt64);
					const uint64_t value = std::get<uint64_t>(source);
					std::memcpy(&output.Integer, &value, sizeof(value));
					return true;
				}
				case PropertyKind::Float:
					output.Kind = static_cast<uint32_t>(NativePropertyKindV1::Float);
					output.Number = std::get<float>(source);
					return true;
				case PropertyKind::Double:
					output.Kind = static_cast<uint32_t>(NativePropertyKindV1::Double);
					output.Number = std::get<double>(source);
					return true;
				case PropertyKind::Vector2:
				{
					output.Kind = static_cast<uint32_t>(NativePropertyKindV1::Vector2);
					const glm::vec2 value = std::get<glm::vec2>(source);
					output.Vector = { value.x, value.y, 0.0f, 0.0f };
					return true;
				}
				case PropertyKind::Vector3:
				{
					output.Kind = static_cast<uint32_t>(NativePropertyKindV1::Vector3);
					const glm::vec3 value = std::get<glm::vec3>(source);
					output.Vector = { value.x, value.y, value.z, 0.0f };
					return true;
				}
				case PropertyKind::Vector4:
				{
					output.Kind = static_cast<uint32_t>(NativePropertyKindV1::Vector4);
					const glm::vec4 value = std::get<glm::vec4>(source);
					output.Vector = { value.x, value.y, value.z, value.w };
					return true;
				}
				case PropertyKind::String:
					return false;
			}
			return false;
		}

		bool SameEntity(const EntityHandleV1& left, const EntityHandleV1& right)
		{
			return left.SceneSessionId == right.SceneSessionId
				&& left.EntityId == right.EntityId
				&& left.RuntimeGeneration == right.RuntimeGeneration;
		}

		thread_local Scene* DeferredValidationScene = nullptr;
		thread_local uint64_t DeferredValidationSceneSessionId = 0;
		thread_local uint64_t DeferredValidationRuntimeGeneration = 0;

		class DeferredValidationSceneScope
		{
		public:
			DeferredValidationSceneScope(Scene& scene, uint64_t sceneSessionId,
				uint64_t runtimeGeneration)
				: m_PreviousScene(DeferredValidationScene),
				  m_PreviousSceneSessionId(DeferredValidationSceneSessionId),
				  m_PreviousRuntimeGeneration(DeferredValidationRuntimeGeneration)
			{
				DeferredValidationScene = &scene;
				DeferredValidationSceneSessionId = sceneSessionId;
				DeferredValidationRuntimeGeneration = runtimeGeneration;
			}

			~DeferredValidationSceneScope()
			{
				DeferredValidationScene = m_PreviousScene;
				DeferredValidationSceneSessionId = m_PreviousSceneSessionId;
				DeferredValidationRuntimeGeneration = m_PreviousRuntimeGeneration;
			}

		private:
			Scene* m_PreviousScene = nullptr;
			uint64_t m_PreviousSceneSessionId = 0;
			uint64_t m_PreviousRuntimeGeneration = 0;
		};

		enum class DeferredRuntimeEffectKind : uint8_t
		{
			SetEnabled,
			DestroyAttachment
		};

		struct DeferredRuntimeEffect
		{
			DeferredRuntimeEffectKind Kind = DeferredRuntimeEffectKind::SetEnabled;
			uint64_t AttachmentId = 0;
			bool Enabled = false;
		};

		thread_local std::vector<DeferredRuntimeEffect>* DeferredRuntimeEffects = nullptr;
		thread_local std::unordered_set<uint64_t>* DeferredRemovedBehaviourAttachments = nullptr;
		thread_local IScriptRuntime* DeferredRuntime = nullptr;
		thread_local ScriptStatus* DeferredRuntimeFailure = nullptr;

		class DeferredRuntimeEffectScope
		{
		public:
			explicit DeferredRuntimeEffectScope(
				std::vector<DeferredRuntimeEffect>* effects,
				std::unordered_set<uint64_t>* removedAttachments,
				IScriptRuntime* runtime = nullptr,
				ScriptStatus* runtimeFailure = nullptr)
				: m_PreviousEffects(DeferredRuntimeEffects),
				  m_PreviousRemovedAttachments(DeferredRemovedBehaviourAttachments),
				  m_PreviousRuntime(DeferredRuntime),
				  m_PreviousRuntimeFailure(DeferredRuntimeFailure)
			{
				DeferredRuntimeEffects = effects;
				DeferredRemovedBehaviourAttachments = removedAttachments;
				DeferredRuntime = runtime;
				DeferredRuntimeFailure = runtimeFailure;
			}
			~DeferredRuntimeEffectScope()
			{
				DeferredRuntimeEffects = m_PreviousEffects;
				DeferredRemovedBehaviourAttachments = m_PreviousRemovedAttachments;
				DeferredRuntime = m_PreviousRuntime;
				DeferredRuntimeFailure = m_PreviousRuntimeFailure;
			}
		private:
			std::vector<DeferredRuntimeEffect>* m_PreviousEffects = nullptr;
			std::unordered_set<uint64_t>* m_PreviousRemovedAttachments = nullptr;
			IScriptRuntime* m_PreviousRuntime = nullptr;
			ScriptStatus* m_PreviousRuntimeFailure = nullptr;
		};

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
		m_StoppingSceneSessions.clear();
		m_DeferredCommands.clear();
		m_OpenDeferredCallbackTransaction.reset();
		m_SealedDeferredCallbackTransactions.clear();
		m_DrainingDeferredCallbackTransactions = false;
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

	void ScriptEngine::StopSceneAfterRuntimeFailure(uint64_t sceneSessionId,
		const char* operation, ScriptStatus status)
	{
		ReportFailure(operation, status);
		TC_Core_Error("Scene session {0} entered a managed script protocol fault during {1}; Play is being stopped",
			sceneSessionId, operation ? operation : "<unknown>");

		Scene* scene = nullptr;
		{
			std::lock_guard<std::mutex> lock(m_Mutex);
			const auto binding = m_Scenes.find(sceneSessionId);
			if (binding != m_Scenes.end())
				scene = binding->second.ScenePointer;
		}
		if (scene && scene->IsRuntimeRunning())
			scene->OnRuntimeStop();
		// During StartScene the Scene has not received its session ID yet, so its
		// OnRuntimeStop cannot release the ScriptEngine binding. The explicit,
		// idempotent stop closes that startup window as well.
		StopScene(sceneSessionId);
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

		InvalidateProjectionSnapshots();
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
					// only after the managed batch callback has returned. A false result
					// is fatal and has already stopped the owning Scene session.
					FlushDeferredCommands(sceneSessionId);
					return;
				}
				bool ownsSession = false;
				{
					std::lock_guard<std::mutex> lock(m_Mutex);
					ownsSession = m_Scenes.find(sceneSessionId) != m_Scenes.end();
				}
				RollbackRuntimeEntityBatch(scene, entityIDs, ownsSession);
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
		{
			bool ownsSession = false;
			{
				std::lock_guard<std::mutex> lock(m_Mutex);
				ownsSession = m_Scenes.find(sceneSessionId) != m_Scenes.end();
			}
			RollbackRuntimeEntityBatch(scene, entityIDs, ownsSession);
			return ownsSession && scene.IsRuntimeRunning() ? sceneSessionId : 0;
		}
		if (!FlushDeferredCommands(sceneSessionId))
			return 0;
		{
			std::lock_guard<std::mutex> lock(m_Mutex);
			if (m_Scenes.find(sceneSessionId) == m_Scenes.end()
				|| !scene.IsRuntimeRunning())
				return 0;
		}
		return sceneSessionId;
	}

	uint64_t ScriptEngine::StartSceneCore(Scene& scene, uint64_t runtimeGeneration,
		std::span<const UUID> initialEntityIDs, bool flushPendingCreates)
	{
		auto runtime = GetRuntime();
		if (!runtime || !runtime->IsReady() || runtimeGeneration == 0)
			return 0;

		const bool sceneRuntimeWasRunning = scene.IsRuntimeRunning();
		uint64_t session = 0;
		{
			std::lock_guard<std::mutex> lock(m_Mutex);
			do { session = m_NextSceneSessionId++; }
			while (session == 0 || m_Scenes.find(session) != m_Scenes.end());
			m_Scenes.emplace(session, SceneBinding{ &scene, runtimeGeneration });
			m_PendingFixedInput.try_emplace(session);
			++m_ProjectionRevision;
			m_ProjectionSnapshots.clear();
		}
		InstallRuntimeEntityBatchCallback(scene, session);
		auto sessionIsHealthy = [&]()
		{
			std::lock_guard<std::mutex> lock(m_Mutex);
			const auto binding = m_Scenes.find(session);
			return binding != m_Scenes.end()
				&& binding->second.ScenePointer == &scene
				&& binding->second.RuntimeGeneration == runtimeGeneration
				&& (!sceneRuntimeWasRunning || scene.IsRuntimeRunning());
		};

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
		if (IsSuccess(status) && !FlushDeferredCommands(session))
			status = ScriptStatus::InvalidState;
		if (IsSuccess(status) && !sessionIsHealthy())
			status = ScriptStatus::InvalidState;
		if (!IsSuccess(status))
		{
			scene.SetRuntimeEntityBatchCreatedCallback({});
			ReportFailure("StartScene", status);
			// A synchronous callback protocol fault may already have stopped and
			// removed this session. Only the owner performs managed teardown.
			bool ownsSession = false;
			{
				std::lock_guard<std::mutex> lock(m_Mutex);
				ownsSession = m_Scenes.find(session) != m_Scenes.end();
			}
			if (ownsSession)
				StopScene(session);
			std::lock_guard<std::mutex> lock(m_Mutex);
			m_Scenes.erase(session);
			m_StoppingSceneSessions.erase(session);
			m_PendingFixedInput.erase(session);
			m_DeferredCommands.erase(std::remove_if(
				m_DeferredCommands.begin(), m_DeferredCommands.end(),
				[session](const DeferredCommand& command)
				{
					return command.Entity.SceneSessionId == session;
				}), m_DeferredCommands.end());
			if (m_OpenDeferredCallbackTransaction
				&& m_OpenDeferredCallbackTransaction->Context.SceneSessionId
					== session)
				m_OpenDeferredCallbackTransaction.reset();
			m_SealedDeferredCallbackTransactions.erase(
				std::remove_if(m_SealedDeferredCallbackTransactions.begin(),
					m_SealedDeferredCallbackTransactions.end(),
					[session](
						const SealedDeferredCallbackTransaction& transaction)
					{
						return transaction.Context.SceneSessionId == session;
					}),
				m_SealedDeferredCallbackTransactions.end());
			++m_ProjectionRevision;
			m_ProjectionSnapshots.clear();
			return 0;
		}
		if (flushPendingCreates)
		{
			scene.FlushPendingRuntimeEntityCreates();
			if (!sessionIsHealthy())
			{
				StopScene(session);
				return 0;
			}
		}
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
			// Session ownership makes teardown idempotent. A deferred protocol fault
			// may stop the Scene synchronously and then surface through the managed
			// dispatch boundary; the second stop must not destroy a newer/no runtime.
			if (binding == m_Scenes.end()
				|| !m_StoppingSceneSessions.emplace(sceneSessionId).second)
				return;
			scene = binding->second.ScenePointer;
		}
		if (scene)
			scene->SetRuntimeEntityBatchCreatedCallback({});
		InvalidateProjectionSnapshots();
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
		m_StoppingSceneSessions.erase(sceneSessionId);
		m_PendingFixedInput.erase(sceneSessionId);
		m_DeferredCommands.erase(std::remove_if(m_DeferredCommands.begin(),
			m_DeferredCommands.end(), [sceneSessionId](const DeferredCommand& command)
			{
				return command.Entity.SceneSessionId == sceneSessionId;
			}), m_DeferredCommands.end());
		if (m_OpenDeferredCallbackTransaction
			&& m_OpenDeferredCallbackTransaction->Context.SceneSessionId
				== sceneSessionId)
			m_OpenDeferredCallbackTransaction.reset();
		m_SealedDeferredCallbackTransactions.erase(
			std::remove_if(m_SealedDeferredCallbackTransactions.begin(),
				m_SealedDeferredCallbackTransactions.end(),
				[sceneSessionId](
					const SealedDeferredCallbackTransaction& transaction)
				{
					return transaction.Context.SceneSessionId == sceneSessionId;
				}),
			m_SealedDeferredCallbackTransactions.end());
		++m_ProjectionRevision;
		m_ProjectionSnapshots.clear();
	}

	void ScriptEngine::UpdateAll(uint64_t sceneSessionId, float deltaTime)
	{
		auto runtime = GetRuntime();
		if (!runtime || !std::isfinite(deltaTime) || deltaTime < 0.0f)
			return;
		InvalidateProjectionSnapshots();
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
		if (!IsSuccess(status))
		{
			StopSceneAfterRuntimeFailure(sceneSessionId, "UpdateAll", status);
			return;
		}
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

		InvalidateProjectionSnapshots();
		ScriptStatus status;
		try
		{
			status = runtime->FixedUpdateAll(fixedDeltaTime);
			if (!IsSuccess(status))
				StopSceneAfterRuntimeFailure(sceneSessionId,
					"FixedUpdateAll", status);
			else
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
		InvalidateProjectionSnapshots();
		const ScriptStatus status = runtime->DispatchPhysicsEvents(events);
		if (!IsSuccess(status))
		{
			StopSceneAfterRuntimeFailure(sceneSessionId,
				"DispatchPhysicsEvents", status);
			return;
		}
		FlushDeferredCommands(sceneSessionId);
	}

	Scene* ScriptEngine::ResolveScene(const EntityHandleV1& handle) const
	{
		if (handle.SceneSessionId == 0 || handle.EntityId == 0
			|| handle.RuntimeGeneration == 0)
			return nullptr;
		if (DeferredValidationScene
			&& handle.SceneSessionId == DeferredValidationSceneSessionId
			&& handle.RuntimeGeneration == DeferredValidationRuntimeGeneration)
			return DeferredValidationScene;
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
		bool present = false;
		if (!GetProjectedBehaviourPresence(attachmentId, present) || !present)
			return false;
		std::lock_guard<std::mutex> lock(m_Mutex);
		bool found = false;
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
						found = true;
						break;
					}
				}
				if (found)
					break;
			}
			if (found)
				break;
		}
		for (const DeferredCommand& command : m_DeferredCommands)
		{
			if (command.AttachmentId != attachmentId)
				continue;
			if (command.Kind == DeferredCommandKind::SetBehaviourEnabled)
			{
				enabled = command.Enabled;
				found = true;
			}
			else if (command.Kind == DeferredCommandKind::RemoveBehaviour)
				found = false;
		}
		return found;
	}

	bool ScriptEngine::GetProjectedBehaviourPresence(uint64_t attachmentId,
		bool& present, EntityHandleV1* ownerOutput) const
	{
		present = false;
		if (ownerOutput)
			*ownerOutput = {};
		if (attachmentId == 0)
			return false;
		if (DeferredRemovedBehaviourAttachments
			&& DeferredRemovedBehaviourAttachments->contains(attachmentId))
			return true;
		bool known = false;
		EntityHandleV1 owner;
		{
			std::lock_guard<std::mutex> lock(m_Mutex);
			for (const auto& [sceneSessionId, binding] : m_Scenes)
			{
				if (!binding.ScenePointer)
					continue;
				for (UUID entityId : binding.ScenePointer->m_EntityOrder)
				{
					Entity entity = binding.ScenePointer->FindEntityByUUID(entityId);
					if (!entity || !entity.HasComponent<CSharpScripts>())
						continue;
					const auto& scripts = entity.GetComponent<CSharpScripts>().Scripts;
					if (std::any_of(scripts.begin(), scripts.end(),
						[&](const CSharpScriptEntry& script)
						{
							return static_cast<uint64_t>(script.AttachmentID)
								== attachmentId;
						}))
					{
						known = true;
						present = true;
						owner = { sceneSessionId, static_cast<uint64_t>(entityId),
							binding.RuntimeGeneration };
						break;
					}
				}
				if (known)
					break;
			}
			if (!known)
				return false;

			for (const DeferredCommand& command : m_DeferredCommands)
			{
				if (command.AttachmentId == attachmentId
					&& command.Kind == DeferredCommandKind::RemoveBehaviour)
					present = false;
			}
		}
		if (ownerOutput)
			*ownerOutput = owner;
		if (!present)
			return true;
		bool ownerAlive = false;
		if (!GetProjectedEntityLiveness(owner, ownerAlive))
			return false;
		present = ownerAlive;
		return true;
	}

	bool ScriptEngine::QueueCommand(DeferredCommand command)
	{
		std::lock_guard<std::mutex> lock(m_Mutex);
		if (m_OpenDeferredCallbackTransaction
			&& (command.Entity.SceneSessionId
					!= m_OpenDeferredCallbackTransaction->Context.SceneSessionId
				|| command.Entity.RuntimeGeneration
					!= m_OpenDeferredCallbackTransaction->Context.RuntimeGeneration))
		{
			DeferredCommand abort;
			abort.Kind = DeferredCommandKind::AbortBatch;
			abort.Entity = m_OpenDeferredCallbackTransaction->Context;
			abort.Name = "Deferred callback attempted to mutate a different Scene";
			m_DeferredCommands.push_back(std::move(abort));
			++m_ProjectionRevision;
			m_ProjectionSnapshots.clear();
			return false;
		}

		const auto binding = m_Scenes.find(command.Entity.SceneSessionId);
		if (command.Entity.SceneSessionId == 0
			|| command.Entity.EntityId == 0
			|| command.Entity.RuntimeGeneration == 0
			|| binding == m_Scenes.end()
			|| binding->second.RuntimeGeneration
				!= command.Entity.RuntimeGeneration)
			return false;
		m_DeferredCommands.push_back(std::move(command));
		++m_ProjectionRevision;
		m_ProjectionSnapshots.clear();
		return true;
	}

	bool ScriptEngine::MarkDeferredCommandBatchFailed(
		const EntityHandleV1& context, std::string reason)
	{
		if (!IsMainThread() || context.SceneSessionId == 0
			|| context.EntityId == 0 || context.RuntimeGeneration == 0)
			return false;
		DeferredCommand command;
		command.Kind = DeferredCommandKind::AbortBatch;
		command.Entity = context;
		command.Name = reason.empty()
			? "Deferred mutation was rejected" : std::move(reason);
		return QueueCommand(std::move(command));
	}

	bool ScriptEngine::BeginDeferredCallbackTransaction(
		const EntityHandleV1& context, uint64_t& token)
	{
		token = 0;
		if (!IsMainThread() || context.SceneSessionId == 0
			|| context.EntityId == 0 || context.RuntimeGeneration == 0)
			return false;

		std::lock_guard<std::mutex> lock(m_Mutex);
		const auto binding = m_Scenes.find(context.SceneSessionId);
		if (m_OpenDeferredCallbackTransaction || binding == m_Scenes.end()
			|| !binding->second.ScenePointer
			|| binding->second.RuntimeGeneration != context.RuntimeGeneration)
			return false;

		do
		{
			token = m_NextDeferredCallbackTransactionToken++;
		} while (token == 0);
		m_OpenDeferredCallbackTransaction =
			OpenDeferredCallbackTransaction{ token, context,
				m_DeferredCommands.size() };
		return true;
	}

	bool ScriptEngine::CompleteDeferredCallbackTransaction(uint64_t token)
	{
		if (!IsMainThread() || token == 0)
			return false;

		bool drain = false;
		{
			std::lock_guard<std::mutex> lock(m_Mutex);
			if (!m_OpenDeferredCallbackTransaction
				|| m_OpenDeferredCallbackTransaction->Token != token
				|| m_OpenDeferredCallbackTransaction->CommandOffset
					> m_DeferredCommands.size())
				return false;

			SealedDeferredCallbackTransaction transaction;
			transaction.Token = token;
			transaction.Context =
				m_OpenDeferredCallbackTransaction->Context;
			const size_t offset =
				m_OpenDeferredCallbackTransaction->CommandOffset;
			transaction.Commands.reserve(m_DeferredCommands.size() - offset);
			for (size_t index = offset; index < m_DeferredCommands.size(); ++index)
				transaction.Commands.push_back(
					std::move(m_DeferredCommands[index]));
			m_DeferredCommands.erase(m_DeferredCommands.begin()
				+ static_cast<std::ptrdiff_t>(offset),
				m_DeferredCommands.end());
			m_OpenDeferredCallbackTransaction.reset();
			m_SealedDeferredCallbackTransactions.push_back(
				std::move(transaction));
			if (!m_SealedDeferredCallbackTransactions.back().Commands.empty())
			{
				++m_ProjectionRevision;
				m_ProjectionSnapshots.clear();
			}
			if (!m_DrainingDeferredCallbackTransactions)
			{
				m_DrainingDeferredCallbackTransactions = true;
				drain = true;
			}
		}
		// Reentrant completions only seal their transaction. The outermost drain
		// processes every sealed callback in FIFO order and reports any fatal
		// resolver/publication failure through the outermost CompleteCallback.
		return !drain || DrainDeferredCallbackTransactions();
	}

	bool ScriptEngine::DrainDeferredCallbackTransactions()
	{
		bool succeeded = true;
		while (true)
		{
			SealedDeferredCallbackTransaction transaction;
			{
				std::lock_guard<std::mutex> lock(m_Mutex);
				if (m_SealedDeferredCallbackTransactions.empty())
				{
					m_DrainingDeferredCallbackTransactions = false;
					return succeeded;
				}
				transaction = std::move(
					m_SealedDeferredCallbackTransactions.front());
				m_SealedDeferredCallbackTransactions.pop_front();
			}

			try
			{
				if (!CommitDeferredCommandBatch(
					transaction.Context.SceneSessionId,
					std::move(transaction.Commands), true))
					succeeded = false;
			}
			catch (const std::exception& exception)
			{
				succeeded = false;
				TC_Core_Error("Deferred callback transaction {0} threw while draining: {1}",
					transaction.Token, exception.what());
				StopSceneAfterRuntimeFailure(
					transaction.Context.SceneSessionId,
					"Drain deferred callback transaction", ScriptStatus::InvalidState);
			}
			catch (...)
			{
				succeeded = false;
				TC_Core_Error("Deferred callback transaction {0} threw while draining",
					transaction.Token);
				StopSceneAfterRuntimeFailure(
					transaction.Context.SceneSessionId,
					"Drain deferred callback transaction", ScriptStatus::InvalidState);
			}
		}
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


	std::shared_ptr<const ScriptEngine::ProjectionSnapshot>
		ScriptEngine::GetProjectionSnapshot(const EntityHandleV1& context) const
	{
		if (context.SceneSessionId == 0 || context.EntityId == 0
			|| context.RuntimeGeneration == 0 || !IsMainThread())
			return {};

		Scene* scene = nullptr;
		uint64_t revision = 0;
		std::vector<DeferredCommand> commands;
		{
			std::lock_guard<std::mutex> lock(m_Mutex);
			const auto binding = m_Scenes.find(context.SceneSessionId);
			if (binding == m_Scenes.end() || !binding->second.ScenePointer
				|| binding->second.RuntimeGeneration != context.RuntimeGeneration)
				return {};
			if (const auto cached = m_ProjectionSnapshots.find(
					context.SceneSessionId);
				cached != m_ProjectionSnapshots.end() && cached->second
				&& cached->second->Revision == m_ProjectionRevision
				&& cached->second->RuntimeGeneration
					== context.RuntimeGeneration)
				return cached->second;

			scene = binding->second.ScenePointer;
			revision = m_ProjectionRevision;
			commands.reserve(m_DeferredCommands.size());
			for (const DeferredCommand& command : m_DeferredCommands)
			{
				if (command.Entity.SceneSessionId == context.SceneSessionId
					&& command.Entity.RuntimeGeneration
						== context.RuntimeGeneration)
					commands.push_back(command);
			}
		}

		auto snapshot = std::make_shared<ProjectionSnapshot>();
		snapshot->Revision = revision;
		snapshot->RuntimeGeneration = context.RuntimeGeneration;
		snapshot->Order.reserve(scene->m_EntityOrder.size() + commands.size());
		snapshot->Entities.reserve(scene->m_EntityOrder.size() + commands.size());
		snapshot->Children.reserve(scene->m_ChildrenMap.size() + commands.size());

		auto failProjection = [&](std::string error)
		{
			if (snapshot->Valid)
			{
				snapshot->Valid = false;
				snapshot->Error = std::move(error);
			}
			return false;
		};

		auto native = [](const glm::vec3& value)
		{
			return NativeVector3{ value.x, value.y, value.z };
		};
		auto vector = [](const NativeVector3& value)
		{
			return glm::vec3(value.X, value.Y, value.Z);
		};
		for (UUID entityId : scene->m_EntityOrder)
		{
			Entity entity = scene->FindEntityByUUID(entityId);
			if (!entity || !entity.HasComponent<Tag>()
				|| !entity.HasComponent<EntityMetadata>()
				|| !entity.HasComponent<Transform>())
			{
				failProjection("Scene entity " + std::to_string(
					static_cast<uint64_t>(entityId))
					+ " is missing a required identity component");
				break;
			}
			const uint64_t id = static_cast<uint64_t>(entityId);
			if (id == 0 || snapshot->Entities.find(id)
				!= snapshot->Entities.end())
			{
				failProjection("Scene projection contains a duplicate or zero entity ID");
				break;
			}
			const auto& transform = entity.GetComponent<Transform>();
			ProjectedEntityState state;
			state.Handle = { context.SceneSessionId, id,
				context.RuntimeGeneration };
			state.Name = entity.GetName();
			state.GameplayTag = entity.GetGameplayTag();
			state.Layer = entity.GetLayer();
			state.ActiveSelf = entity.GetComponent<Tag>().ActiveSelf;
			state.Translation = native(transform._Translation);
			state.Rotation = native(transform._Rotation);
			state.Scale = native(transform._Scale);
			state.LocalTranslation = native(transform._LocalTranslation);
			state.LocalRotation = native(transform._LocalRotation);
			state.LocalScale = native(transform._LocalScale);
			if (const auto parent = scene->m_ParentMap.find(entityId);
				parent != scene->m_ParentMap.end())
				state.ParentId = static_cast<uint64_t>(parent->second);
			snapshot->Order.push_back(id);
			snapshot->Entities.emplace(id, std::move(state));
		}

		if (snapshot->Valid)
		{
			std::unordered_set<uint64_t> linkedChildren;
			linkedChildren.reserve(snapshot->Entities.size());
			for (const auto& [parentIdValue, sceneChildren] :
				scene->m_ChildrenMap)
			{
				const uint64_t parentId = static_cast<uint64_t>(parentIdValue);
				if (snapshot->Entities.find(parentId)
					== snapshot->Entities.end())
				{
					failProjection("Scene child index references a missing parent");
					break;
				}
				auto& children = snapshot->Children[parentId];
				children.reserve(sceneChildren.size());
				std::unordered_set<uint64_t> siblingIds;
				siblingIds.reserve(sceneChildren.size());
				for (UUID childIdValue : sceneChildren)
				{
					const uint64_t childId =
						static_cast<uint64_t>(childIdValue);
					const auto child = snapshot->Entities.find(childId);
					if (child == snapshot->Entities.end()
						|| child->second.ParentId != parentId
						|| !siblingIds.emplace(childId).second
						|| !linkedChildren.emplace(childId).second)
					{
						failProjection("Scene parent and child indexes disagree");
						break;
					}
					children.push_back(childId);
				}
				if (!snapshot->Valid)
					break;
			}
			if (snapshot->Valid)
			{
				for (const auto& [id, state] : snapshot->Entities)
				{
					if (state.ParentId == 0)
						continue;
					if (snapshot->Entities.find(state.ParentId)
							== snapshot->Entities.end()
						|| linkedChildren.find(id) == linkedChildren.end())
					{
						failProjection("Scene hierarchy contains an unindexed parent link");
						break;
					}
				}
			}
		}

		auto composeWorld = [&](const ProjectedEntityState& state)
		{
			return Math::ComposeTransform(vector(state.Translation),
				vector(state.Rotation), vector(state.Scale));
		};
		auto composeLocal = [&](const ProjectedEntityState& state)
		{
			return Math::ComposeTransform(vector(state.LocalTranslation),
				vector(state.LocalRotation), vector(state.LocalScale));
		};
		auto setWorld = [&](ProjectedEntityState& state, const glm::mat4& matrix)
		{
			glm::vec3 translation{}, rotation{}, scale{};
			if (!Math::DecomposeTransform(matrix, translation, rotation, scale))
				return false;
			state.Translation = native(translation);
			state.Rotation = native(rotation);
			state.Scale = native(scale);
			return true;
		};
		auto setLocal = [&](ProjectedEntityState& state, const glm::mat4& matrix)
		{
			glm::vec3 translation{}, rotation{}, scale{};
			if (!Math::DecomposeTransform(matrix, translation, rotation, scale))
				return false;
			state.LocalTranslation = native(translation);
			state.LocalRotation = native(rotation);
			state.LocalScale = native(scale);
			return true;
		};
		auto updateLocalFromWorld = [&](ProjectedEntityState& state)
		{
			if (state.ParentId == 0)
			{
				state.LocalTranslation = state.Translation;
				state.LocalRotation = state.Rotation;
				state.LocalScale = state.Scale;
				return true;
			}
			const auto parent = snapshot->Entities.find(state.ParentId);
			if (parent == snapshot->Entities.end() || !parent->second.Alive)
				return failProjection("Projected transform parent is unavailable");
			const glm::mat4 parentWorld = composeWorld(parent->second);
			const float determinant = glm::determinant(parentWorld);
			if (!std::isfinite(determinant)
				|| std::abs(determinant) <= 1.0e-8f)
				return failProjection(
					"Projected transform parent matrix is singular");
			if (!setLocal(state, glm::inverse(parentWorld)
				* composeWorld(state)))
				return failProjection(
					"Projected world transform cannot be represented locally");
			return true;
		};
		auto updateWorldSubtree = [&](uint64_t root)
		{
			std::vector<uint64_t> pending{ root };
			std::unordered_set<uint64_t> visited;
			while (!pending.empty())
			{
				const uint64_t id = pending.back();
				pending.pop_back();
				if (!visited.emplace(id).second)
					return failProjection(
						"Projected transform hierarchy contains a cycle");
				auto current = snapshot->Entities.find(id);
				if (current == snapshot->Entities.end() || !current->second.Alive)
					return failProjection(
						"Projected transform subtree contains a missing entity");
				glm::mat4 world = composeLocal(current->second);
				if (current->second.ParentId != 0)
				{
					const auto parent = snapshot->Entities.find(
						current->second.ParentId);
					if (parent == snapshot->Entities.end()
						|| !parent->second.Alive)
						return failProjection(
							"Projected transform subtree has a missing parent");
					world = composeWorld(parent->second) * world;
				}
				if (!setWorld(current->second, world))
					return failProjection(
						"Projected local transform cannot be represented in world space");
				if (const auto children = snapshot->Children.find(id);
					children != snapshot->Children.end())
				{
					for (auto childId = children->second.rbegin();
						childId != children->second.rend(); ++childId)
					{
						const auto child = snapshot->Entities.find(*childId);
						if (child == snapshot->Entities.end()
							|| child->second.ParentId != id)
							return failProjection(
								"Projected parent and child indexes disagree");
						if (child->second.Alive)
							pending.push_back(*childId);
					}
				}
			}
			return true;
		};
		auto validateParentAssignment = [&](uint64_t childId,
			uint64_t parentId)
		{
			if (parentId == 0)
				return true;
			std::unordered_set<uint64_t> visited;
			uint64_t currentId = parentId;
			while (currentId != 0)
			{
				if (currentId == childId)
					return failProjection(
						"Projected reparenting would create a cycle");
				if (!visited.emplace(currentId).second)
					return failProjection(
						"Projected hierarchy already contains a cycle");
				const auto current = snapshot->Entities.find(currentId);
				if (current == snapshot->Entities.end() || !current->second.Alive)
					return failProjection(
						"Projected reparent destination is unavailable");
				currentId = current->second.ParentId;
			}
			return true;
		};
		auto destroySubtree = [&](uint64_t root)
		{
			std::vector<uint64_t> pending{ root };
			std::unordered_set<uint64_t> visited;
			while (!pending.empty())
			{
				const uint64_t id = pending.back();
				pending.pop_back();
				if (!visited.emplace(id).second)
					return failProjection(
						"Projected destroy subtree contains a cycle");
				auto entity = snapshot->Entities.find(id);
				if (entity == snapshot->Entities.end())
					return failProjection(
						"Projected destroy subtree contains a missing entity");
				if (!entity->second.Alive)
					continue;
				entity->second.Alive = false;
				if (const auto children = snapshot->Children.find(id);
					children != snapshot->Children.end())
				{
					for (auto childId = children->second.rbegin();
						childId != children->second.rend(); ++childId)
					{
						const auto child = snapshot->Entities.find(*childId);
						if (child == snapshot->Entities.end()
							|| child->second.ParentId != id)
							return failProjection(
								"Projected parent and child indexes disagree");
						pending.push_back(*childId);
					}
				}
			}
			return true;
		};
		auto makeUniqueName = [&](const std::string& requestedName)
		{
			const std::string baseName = requestedName.empty()
				? "Entity" : requestedName;
			auto nameExists = [&](const std::string& candidate)
			{
				return std::any_of(snapshot->Entities.begin(),
					snapshot->Entities.end(), [&](const auto& item)
					{
						return item.second.Alive
							&& item.second.Name == candidate;
					});
			};
			if (!nameExists(baseName))
				return baseName;
			for (uint32_t suffix = 1; ; ++suffix)
			{
				std::string candidate = baseName + " ("
					+ std::to_string(suffix) + ")";
				if (!nameExists(candidate))
					return candidate;
			}
		};

		using namespace ComponentIds::TransformProperties;
		for (const DeferredCommand& command : commands)
		{
			if (!snapshot->Valid)
				break;
			const uint64_t id = command.Entity.EntityId;
			if (command.Kind == DeferredCommandKind::AbortBatch)
			{
				failProjection(command.Name.empty()
					? "Deferred mutation rejected the command batch"
					: command.Name);
				break;
			}
			if (command.Kind == DeferredCommandKind::CreateEntity)
			{
				const bool hasParent = command.Parent.SceneSessionId != 0
					|| command.Parent.EntityId != 0
					|| command.Parent.RuntimeGeneration != 0;
				if (id == 0 || snapshot->Entities.find(id)
						!= snapshot->Entities.end()
					|| (hasParent && (command.Parent.SceneSessionId
							!= context.SceneSessionId
						|| command.Parent.RuntimeGeneration
							!= context.RuntimeGeneration
						|| command.Parent.EntityId == 0)))
				{
					failProjection(
						"Projected create command has an invalid reserved entity");
					break;
				}
				const uint64_t parentId = hasParent
					? command.Parent.EntityId : 0;
				if (!validateParentAssignment(id, parentId))
					break;
				ProjectedEntityState state;
				state.Handle = command.Entity;
				state.ParentId = parentId;
				state.Name = makeUniqueName(command.Name);
				state.Translation = command.WorldPosition;
				state.LocalTranslation = command.WorldPosition;
				snapshot->Order.push_back(id);
				snapshot->Entities[id] = std::move(state);
				if (parentId != 0)
					snapshot->Children[parentId].push_back(id);
				if (!updateLocalFromWorld(snapshot->Entities.at(id))
					|| !updateWorldSubtree(id))
					break;
				continue;
			}
			auto entity = snapshot->Entities.find(id);
			if (entity == snapshot->Entities.end())
			{
				failProjection("Projected command target is unavailable");
				break;
			}
			ProjectedEntityState& state = entity->second;
			if (command.Kind == DeferredCommandKind::DestroyEntity)
			{
				if (!state.Alive)
					continue;
				if (!destroySubtree(id))
					break;
			}
			else if (state.Alive && command.Kind == DeferredCommandKind::SetParent)
			{
				const bool hasParent = command.Parent.SceneSessionId != 0
					|| command.Parent.EntityId != 0
					|| command.Parent.RuntimeGeneration != 0;
				if (hasParent && (command.Parent.SceneSessionId
						!= context.SceneSessionId
					|| command.Parent.RuntimeGeneration
						!= context.RuntimeGeneration
					|| command.Parent.EntityId == 0))
				{
					failProjection(
						"Projected reparent command has an invalid destination");
					break;
				}
				const uint64_t newParentId = hasParent
					? command.Parent.EntityId : 0;
				if (newParentId == state.ParentId)
					continue;
				if (!validateParentAssignment(id, newParentId))
					break;

				ProjectedEntityState updatedState = state;
				updatedState.ParentId = newParentId;
				if (!updateLocalFromWorld(updatedState))
					break;
				if (state.ParentId != 0)
				{
					auto oldChildren = snapshot->Children.find(state.ParentId);
					if (oldChildren == snapshot->Children.end())
					{
						failProjection(
							"Projected reparent source index is unavailable");
						break;
					}
					const size_t oldSize = oldChildren->second.size();
					std::erase(oldChildren->second, id);
					if (oldChildren->second.size() == oldSize)
					{
						failProjection(
							"Projected reparent source index is inconsistent");
						break;
					}
				}
				if (newParentId != 0)
				{
					auto& newChildren = snapshot->Children[newParentId];
					std::erase(newChildren, id);
					newChildren.push_back(id);
				}
				state = std::move(updatedState);
				if (!updateWorldSubtree(id))
					break;
			}
			else if (state.Alive
				&& command.Kind == DeferredCommandKind::SetActiveSelf)
				state.ActiveSelf = command.Enabled;
			else if (state.Alive
				&& command.Kind == DeferredCommandKind::SetEntityName)
			{
				if (state.Name != command.Name)
					state.Name = makeUniqueName(command.Name);
			}
			else if (state.Alive
				&& command.Kind == DeferredCommandKind::SetGameplayTag)
				state.GameplayTag = command.Name;
			else if (state.Alive
				&& command.Kind == DeferredCommandKind::SetLayer)
				state.Layer = command.Layer;
			else if (state.Alive
				&& command.Kind == DeferredCommandKind::SetComponentProperty
				&& command.ComponentType == NativeComponentType::Transform)
			{
				NativeVector3 value{ command.PropertyValue.Vector.X,
					command.PropertyValue.Vector.Y,
					command.PropertyValue.Vector.Z };
				if (command.PropertyId == static_cast<uint32_t>(Translation))
				state.Translation = value;
				else if (command.PropertyId == static_cast<uint32_t>(Rotation))
					state.Rotation = value;
				else if (command.PropertyId == static_cast<uint32_t>(Scale))
					state.Scale = value;
				else if (command.PropertyId
					== static_cast<uint32_t>(LocalTranslation))
					state.LocalTranslation = value;
				else if (command.PropertyId
					== static_cast<uint32_t>(LocalRotation))
					state.LocalRotation = value;
				else if (command.PropertyId == static_cast<uint32_t>(LocalScale))
					state.LocalScale = value;
				if (command.PropertyId == static_cast<uint32_t>(Translation)
					|| command.PropertyId == static_cast<uint32_t>(Rotation)
					|| command.PropertyId == static_cast<uint32_t>(Scale))
				{
					if (!updateLocalFromWorld(state)
						|| !updateWorldSubtree(id))
						break;
				}
				else if (!updateWorldSubtree(id))
					break;
			}
		}

		if (snapshot->Valid)
		{
			try
			{
				ComponentMutationPhaseScope validationPhase(
					ComponentMutationPhase::Validation);
				std::shared_ptr<Scene> nonOwning(scene, [](Scene*) {});
				snapshot->ProjectedScene = Scene::Copy(nonOwning);
				if (!snapshot->ProjectedScene)
					failProjection(
						"Projected component Scene could not be copied");
				else
				{
					std::string componentProjectionError;
					DeferredValidationSceneScope validationScope(
						*snapshot->ProjectedScene, context.SceneSessionId,
						context.RuntimeGeneration);
					if (!ApplyDeferredCommands(*snapshot->ProjectedScene,
						context.SceneSessionId, commands, false,
						componentProjectionError))
					{
						failProjection(
							"Projected component command replay failed: "
							+ componentProjectionError);
					}
				}
			}
			catch (const std::exception& exception)
			{
				failProjection(
					"Projected component command replay threw: "
					+ std::string(exception.what()));
			}
			catch (...)
			{
				failProjection(
					"Projected component command replay threw an unknown exception");
			}
		}

		{
			std::lock_guard<std::mutex> lock(m_Mutex);
			const auto binding = m_Scenes.find(context.SceneSessionId);
			if (binding == m_Scenes.end()
				|| binding->second.ScenePointer != scene
				|| binding->second.RuntimeGeneration
					!= context.RuntimeGeneration
				|| m_ProjectionRevision != revision)
				return {};
			m_ProjectionSnapshots[context.SceneSessionId] = snapshot;
		}
		return snapshot;
	}

	void ScriptEngine::InvalidateProjectionSnapshots() const
	{
		std::lock_guard<std::mutex> lock(m_Mutex);
		++m_ProjectionRevision;
		m_ProjectionSnapshots.clear();
	}

	bool ScriptEngine::GetProjectedEntityLiveness(const EntityHandleV1& entity,
		bool& alive) const
	{
		alive = false;
		const auto snapshot = GetProjectionSnapshot(entity);
		if (!snapshot || !snapshot->Valid)
			return false;
		const auto found = snapshot->Entities.find(entity.EntityId);
		if (found == snapshot->Entities.end())
			return false;
		alive = found->second.Alive;
		return true;
	}

	bool ScriptEngine::GetProjectedParent(const EntityHandleV1& entity,
		EntityHandleV1& parent) const
	{
		parent = {};
		const auto snapshot = GetProjectionSnapshot(entity);
		if (!snapshot || !snapshot->Valid)
			return false;
		const auto found = snapshot->Entities.find(entity.EntityId);
		if (found == snapshot->Entities.end() || !found->second.Alive)
			return false;
		if (found->second.ParentId != 0)
			parent = { entity.SceneSessionId, found->second.ParentId,
				entity.RuntimeGeneration };
		return true;
	}

	bool ScriptEngine::GetProjectedChildren(const EntityHandleV1& entity,
		std::vector<EntityHandleV1>& children) const
	{
		children.clear();
		const auto snapshot = GetProjectionSnapshot(entity);
		if (!snapshot || !snapshot->Valid)
			return false;
		const auto parent = snapshot->Entities.find(entity.EntityId);
		if (parent == snapshot->Entities.end() || !parent->second.Alive)
			return false;
		const auto projectedChildren = snapshot->Children.find(entity.EntityId);
		if (projectedChildren == snapshot->Children.end())
			return true;
		children.reserve(projectedChildren->second.size());
		for (uint64_t id : projectedChildren->second)
		{
			const auto child = snapshot->Entities.find(id);
			if (child != snapshot->Entities.end() && child->second.Alive
				&& child->second.ParentId == entity.EntityId)
				children.push_back(child->second.Handle);
		}
		return true;
	}

	bool ScriptEngine::GetProjectedEntities(const EntityHandleV1& context,
		std::vector<EntityHandleV1>& entities) const
	{
		entities.clear();
		const auto snapshot = GetProjectionSnapshot(context);
		if (!snapshot || !snapshot->Valid)
			return false;
		const auto source = snapshot->Entities.find(context.EntityId);
		if (source == snapshot->Entities.end() || !source->second.Alive)
			return false;
		for (uint64_t id : snapshot->Order)
		{
			const auto item = snapshot->Entities.find(id);
			if (item != snapshot->Entities.end() && item->second.Alive)
				entities.push_back(item->second.Handle);
		}
		return true;
	}

	bool ScriptEngine::GetProjectedEntityName(const EntityHandleV1& entity,
		std::string& name) const
	{
		const auto snapshot = GetProjectionSnapshot(entity);
		if (!snapshot || !snapshot->Valid)
			return false;
		const auto found = snapshot->Entities.find(entity.EntityId);
		if (found == snapshot->Entities.end() || !found->second.Alive)
			return false;
		name = found->second.Name;
		return true;
	}

	bool ScriptEngine::GetProjectedGameplayTag(const EntityHandleV1& entity,
		std::string& tag) const
	{
		const auto snapshot = GetProjectionSnapshot(entity);
		if (!snapshot || !snapshot->Valid)
			return false;
		const auto found = snapshot->Entities.find(entity.EntityId);
		if (found == snapshot->Entities.end() || !found->second.Alive)
			return false;
		tag = found->second.GameplayTag;
		return true;
	}

	bool ScriptEngine::GetProjectedLayer(const EntityHandleV1& entity,
		uint32_t& layer) const
	{
		const auto snapshot = GetProjectionSnapshot(entity);
		if (!snapshot || !snapshot->Valid)
			return false;
		const auto found = snapshot->Entities.find(entity.EntityId);
		if (found == snapshot->Entities.end() || !found->second.Alive)
			return false;
		layer = found->second.Layer;
		return true;
	}

	bool ScriptEngine::GetProjectedActiveInHierarchy(const EntityHandleV1& entity,
		bool& active) const
	{
		active = false;
		const auto snapshot = GetProjectionSnapshot(entity);
		if (!snapshot || !snapshot->Valid)
			return false;
		uint64_t current = entity.EntityId;
		std::unordered_set<uint64_t> visited;
		while (current != 0 && visited.emplace(current).second)
		{
			const auto found = snapshot->Entities.find(current);
			if (found == snapshot->Entities.end() || !found->second.Alive)
				return false;
			if (!found->second.ActiveSelf)
				return true;
			current = found->second.ParentId;
		}
		if (current != 0)
			return false;
		active = true;
		return true;
	}

	bool ScriptEngine::GetProjectedTransformProperty(const EntityHandleV1& entity,
		uint32_t propertyId, NativeVector3& value) const
	{
		const auto snapshot = GetProjectionSnapshot(entity);
		if (!snapshot || !snapshot->Valid)
			return false;
		const auto found = snapshot->Entities.find(entity.EntityId);
		if (found == snapshot->Entities.end() || !found->second.Alive)
			return false;
		using namespace ComponentIds::TransformProperties;
		if (propertyId == static_cast<uint32_t>(Translation))
			value = found->second.Translation;
		else if (propertyId == static_cast<uint32_t>(Rotation))
			value = found->second.Rotation;
		else if (propertyId == static_cast<uint32_t>(Scale))
			value = found->second.Scale;
		else if (propertyId == static_cast<uint32_t>(LocalTranslation))
			value = found->second.LocalTranslation;
		else if (propertyId == static_cast<uint32_t>(LocalRotation))
			value = found->second.LocalRotation;
		else if (propertyId == static_cast<uint32_t>(LocalScale))
			value = found->second.LocalScale;
		else
			return false;
		return true;
	}

	bool ScriptEngine::QueueSetEntityName(const EntityHandleV1& entity,
		std::string name)
	{
		bool alive = false;
		if (!IsMainThread() || name.empty() || name.size() > 1024
			|| !GetProjectedEntityLiveness(entity, alive) || !alive)
			return false;
		DeferredCommand command;
		command.Kind = DeferredCommandKind::SetEntityName;
		command.Entity = entity;
		command.Name = std::move(name);
		return QueueCommand(std::move(command));
	}

	bool ScriptEngine::QueueSetGameplayTag(const EntityHandleV1& entity,
		std::string tag)
	{
		bool alive = false;
		if (!IsMainThread() || tag.empty() || tag.size() > 1024
			|| !GetProjectedEntityLiveness(entity, alive) || !alive)
			return false;
		DeferredCommand command;
		command.Kind = DeferredCommandKind::SetGameplayTag;
		command.Entity = entity;
		command.Name = std::move(tag);
		return QueueCommand(std::move(command));
	}

	bool ScriptEngine::QueueSetLayer(const EntityHandleV1& entity, uint32_t layer)
	{
		bool alive = false;
		if (!IsMainThread() || layer >= Physics2DLayerCount
			|| !GetProjectedEntityLiveness(entity, alive) || !alive)
			return false;
		DeferredCommand command;
		command.Kind = DeferredCommandKind::SetLayer;
		command.Entity = entity;
		command.Layer = layer;
		return QueueCommand(std::move(command));
	}

	bool ScriptEngine::GetProjectedComponentPresence(const EntityHandleV1& entity,
		NativeComponentType componentType, bool& present) const
	{
		present = false;
		if (!IsSupportedComponentType(componentType))
			return false;

		bool alive = false;
		if (!GetProjectedEntityLiveness(entity, alive))
			return false;
		if (!alive)
			return true;
		Entity resolved = ResolveEntity(entity);
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

	bool ScriptEngine::TryGetProjectedComponentProperty(
		const EntityHandleV1& entity, NativeComponentType componentType,
		uint32_t propertyId, NativePropertyValueV1& value, bool& useDefault) const
	{
		useDefault = false;
		if (!IsSupportedComponentType(componentType) || propertyId == 0)
			return false;
		Entity resolved = ResolveEntity(entity);
		bool present = resolved ? HasNativeComponent(resolved, componentType)
			: componentType == NativeComponentType::Transform;
		useDefault = !resolved && present;
		bool found = false;
		std::lock_guard<std::mutex> lock(m_Mutex);
		for (const DeferredCommand& command : m_DeferredCommands)
		{
			if (!SameEntity(command.Entity, entity)
				|| command.ComponentType != componentType)
				continue;
			if (command.Kind == DeferredCommandKind::AddComponent)
			{
				if (!present)
				{
					present = true;
					useDefault = true;
					found = false;
				}
			}
			else if (command.Kind == DeferredCommandKind::RemoveComponent)
			{
				present = false;
				useDefault = false;
				found = false;
			}
			else if (command.Kind == DeferredCommandKind::SetComponentProperty
				&& present && command.PropertyId == propertyId)
			{
				value = command.PropertyValue;
				found = true;
			}
		}
		return found;
	}

	bool ScriptEngine::QueueBehaviourEnabled(uint64_t attachmentId, bool enabled)
	{
		bool present = false;
		EntityHandleV1 owner;
		if (!GetProjectedBehaviourPresence(attachmentId, present, &owner) || !present)
			return false;
		DeferredCommand command;
		command.Kind = DeferredCommandKind::SetBehaviourEnabled;
		command.Entity = owner;
		command.AttachmentId = attachmentId;
		command.Enabled = enabled;
		return QueueCommand(command);
	}

	bool ScriptEngine::QueueRemoveBehaviour(uint64_t attachmentId)
	{
		bool present = false;
		EntityHandleV1 owner;
		if (!GetProjectedBehaviourPresence(attachmentId, present, &owner))
			return false;
		if (!present)
			return true;
		DeferredCommand command;
		command.Kind = DeferredCommandKind::RemoveBehaviour;
		command.Entity = owner;
		command.AttachmentId = attachmentId;
		return QueueCommand(command);
	}

	bool ScriptEngine::QueueDestroyEntity(const EntityHandleV1& entity)
	{
		bool alive = false;
		if (!GetProjectedEntityLiveness(entity, alive))
			return false;
		if (!alive)
			return true;
		DeferredCommand command;
		command.Kind = DeferredCommandKind::DestroyEntity;
		command.Entity = entity;
		return QueueCommand(command);
	}

	bool ScriptEngine::QueueCreateEntity(const EntityHandleV1& context,
		std::string name, NativeVector3 worldPosition,
		const EntityHandleV1& parent, EntityHandleV1& reservedEntity)
	{
		reservedEntity = {};
		bool contextAlive = false;
		if (!IsMainThread()
			|| !GetProjectedEntityLiveness(context, contextAlive) || !contextAlive
			|| name.size() > 1024
			|| !std::isfinite(worldPosition.X) || !std::isfinite(worldPosition.Y)
			|| !std::isfinite(worldPosition.Z))
			return false;
		const bool hasParent = parent.SceneSessionId != 0 || parent.EntityId != 0
			|| parent.RuntimeGeneration != 0;
		if (hasParent && (parent.SceneSessionId != context.SceneSessionId
			|| parent.RuntimeGeneration != context.RuntimeGeneration
			|| parent.EntityId == 0
			|| ([&]() { bool parentAlive = false;
				return !GetProjectedEntityLiveness(parent, parentAlive) || !parentAlive; })()))
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
		if (!IsMainThread())
			return false;
		auto reject = [&](const char* reason)
		{
			MarkDeferredCommandBatchFailed(entity, reason);
			return false;
		};
		const auto snapshot = GetProjectionSnapshot(entity);
		if (!snapshot || !snapshot->Valid)
			return reject(snapshot && !snapshot->Error.empty()
				? snapshot->Error.c_str() : "Projected hierarchy is unavailable");
		const auto projectedEntity = snapshot->Entities.find(entity.EntityId);
		if (projectedEntity == snapshot->Entities.end()
			|| !projectedEntity->second.Alive)
			return reject("Reparent target is unavailable");
		const bool hasParent = parent.SceneSessionId != 0 || parent.EntityId != 0
			|| parent.RuntimeGeneration != 0;
		if (hasParent && (parent.SceneSessionId != entity.SceneSessionId
			|| parent.RuntimeGeneration != entity.RuntimeGeneration
			|| parent.EntityId == 0))
			return reject("Reparent destination belongs to a different Scene");
		if (hasParent)
		{
			uint64_t current = parent.EntityId;
			std::unordered_set<uint64_t> visited;
			while (current != 0)
			{
				if (current == entity.EntityId
					|| !visited.emplace(current).second)
					return reject("Reparenting would create a hierarchy cycle");
				const auto projectedParent = snapshot->Entities.find(current);
				if (projectedParent == snapshot->Entities.end()
					|| !projectedParent->second.Alive)
					return reject("Reparent destination is unavailable");
				current = projectedParent->second.ParentId;
			}
		}
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
		bool alive = false;
		return GetProjectedEntityLiveness(entity, alive) && alive
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
		bool alive = false;
		return GetProjectedEntityLiveness(entity, alive) && alive
			&& QueueCommand(command);
	}

	bool ScriptEngine::QueueSetComponentProperty(const EntityHandleV1& entity,
		NativeComponentType componentType, uint32_t propertyId,
		NativePropertyValueV1 value)
	{
		if (!IsMainThread() || !IsSupportedComponentType(componentType)
			|| propertyId == 0
			)
			return false;
		bool alive = false;
		if (!GetProjectedEntityLiveness(entity, alive) || !alive)
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
		bool alive = false;
		if (!IsMainThread() || !GetProjectedEntityLiveness(entity, alive) || !alive)
			return false;
		DeferredCommand command;
		command.Kind = DeferredCommandKind::SetActiveSelf;
		command.Entity = entity;
		command.Enabled = active;
		return QueueCommand(std::move(command));
	}

	bool ScriptEngine::GetProjectedActiveSelf(const EntityHandleV1& entity,
		bool& active) const
	{
		const auto snapshot = GetProjectionSnapshot(entity);
		if (!snapshot || !snapshot->Valid)
			return false;
		const auto found = snapshot->Entities.find(entity.EntityId);
		if (found == snapshot->Entities.end() || !found->second.Alive)
			return false;
		active = found->second.ActiveSelf;
		return true;
	}

	bool ScriptEngine::GetProjectedRegisteredComponentPresence(
		const EntityHandleV1& entity, uint64_t componentTypeId, bool& present) const
	{
		present = false;
		const ComponentDescriptor* descriptor = componentTypeId == 0 ? nullptr
			: ComponentRegistry::Get().Find(UUID(componentTypeId));
		if (!descriptor || !descriptor->ScriptAccessible)
			return false;
		const auto snapshot = GetProjectionSnapshot(entity);
		if (!snapshot || !snapshot->Valid || !snapshot->ProjectedScene)
			return false;
		const auto state = snapshot->Entities.find(entity.EntityId);
		if (state == snapshot->Entities.end())
			return false;
		if (!state->second.Alive)
			return true;
		Entity projected = snapshot->ProjectedScene->FindEntityByUUID(
			UUID(entity.EntityId));
		if (!projected)
			return false;
		present = descriptor->Has(projected);
		return true;
	}

	bool ScriptEngine::TryGetProjectedRegisteredComponentProperty(
		const EntityHandleV1& entity, uint64_t componentTypeId,
		uint64_t propertyId, NativePropertyValueV1& value,
		bool& useDefault) const
	{
		useDefault = false;
		if (componentTypeId == 0 || propertyId == 0)
			return false;
		const ComponentDescriptor* descriptor = ComponentRegistry::Get().Find(
			UUID(componentTypeId));
		if (!descriptor || !descriptor->ScriptAccessible)
			return false;
		const auto property = std::find_if(descriptor->Properties.begin(),
			descriptor->Properties.end(), [propertyId](const PropertyDescriptor& item)
			{
				return static_cast<uint64_t>(item.PropertyId) == propertyId;
			});
		if (property == descriptor->Properties.end() || !property->Get
			|| property->Kind == PropertyKind::String)
			return false;
		const auto snapshot = GetProjectionSnapshot(entity);
		if (!snapshot || !snapshot->Valid || !snapshot->ProjectedScene)
			return false;
		const auto state = snapshot->Entities.find(entity.EntityId);
		if (state == snapshot->Entities.end() || !state->second.Alive)
			return false;
		Entity projected = snapshot->ProjectedScene->FindEntityByUUID(
			UUID(entity.EntityId));
		return projected && descriptor->Has(projected)
			&& WriteProjectedProperty(property->Kind, property->Get(projected),
				value);
	}

	bool ScriptEngine::TryGetProjectedRegisteredComponentStringProperty(
		const EntityHandleV1& entity, uint64_t componentTypeId,
		uint64_t propertyId, std::string& value, bool& useDefault) const
	{
		useDefault = false;
		if (componentTypeId == 0 || propertyId == 0)
			return false;
		const ComponentDescriptor* descriptor = ComponentRegistry::Get().Find(
			UUID(componentTypeId));
		if (!descriptor || !descriptor->ScriptAccessible)
			return false;
		const auto property = std::find_if(descriptor->Properties.begin(),
			descriptor->Properties.end(), [propertyId](const PropertyDescriptor& item)
			{
				return static_cast<uint64_t>(item.PropertyId) == propertyId;
			});
		if (property == descriptor->Properties.end() || !property->Get
			|| property->Kind != PropertyKind::String)
			return false;
		const auto snapshot = GetProjectionSnapshot(entity);
		if (!snapshot || !snapshot->Valid || !snapshot->ProjectedScene)
			return false;
		const auto state = snapshot->Entities.find(entity.EntityId);
		if (state == snapshot->Entities.end() || !state->second.Alive)
			return false;
		Entity projected = snapshot->ProjectedScene->FindEntityByUUID(
			UUID(entity.EntityId));
		if (!projected || !descriptor->Has(projected))
			return false;
		PropertyValue projectedValue = property->Get(projected);
		const auto projectedString = std::get_if<std::string>(&projectedValue);
		if (!projectedString)
			return false;
		value = *projectedString;
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

	bool ScriptEngine::QueueSetRegisteredComponentStringProperty(
		const EntityHandleV1& entity, uint64_t componentTypeId,
		uint64_t propertyId, std::string value)
	{
		if (!IsMainThread() || propertyId == 0)
			return false;
		const ComponentDescriptor* descriptor = componentTypeId == 0 ? nullptr
			: ComponentRegistry::Get().Find(UUID(componentTypeId));
		if (!descriptor || !descriptor->ScriptAccessible)
			return false;
		const auto property = std::find_if(descriptor->Properties.begin(),
			descriptor->Properties.end(), [propertyId](const PropertyDescriptor& item)
			{ return static_cast<uint64_t>(item.PropertyId) == propertyId; });
		if (property == descriptor->Properties.end()
			|| property->Kind != PropertyKind::String)
			return false;
		bool present = false;
		if (!GetProjectedRegisteredComponentPresence(entity, componentTypeId, present)
			|| !present)
			return false;
		DeferredCommand command;
		command.Kind = DeferredCommandKind::SetRegisteredComponentStringProperty;
		command.Entity = entity;
		command.RegisteredTypeId = componentTypeId;
		command.RegisteredPropertyId = propertyId;
		command.Name = std::move(value);
		return QueueCommand(std::move(command));
	}

	bool ScriptEngine::QueueInstantiatePrefab(const EntityHandleV1& context,
		uint64_t prefabHandle, NativeVector3 worldPosition,
		const EntityHandleV1& parent)
	{
		if (prefabHandle == 0 || !std::isfinite(worldPosition.X)
			|| !std::isfinite(worldPosition.Y) || !std::isfinite(worldPosition.Z)
			)
			return false;
		bool contextAlive = false;
		if (!GetProjectedEntityLiveness(context, contextAlive) || !contextAlive)
			return false;
		const bool hasParent = parent.SceneSessionId != 0 || parent.EntityId != 0
			|| parent.RuntimeGeneration != 0;
		if (hasParent && (parent.SceneSessionId != context.SceneSessionId
			|| parent.RuntimeGeneration != context.RuntimeGeneration
			|| parent.EntityId == 0
			|| ([&]() { bool parentAlive = false;
				return !GetProjectedEntityLiveness(parent, parentAlive) || !parentAlive; })()))
			return false;

		DeferredCommand command;
		command.Kind = DeferredCommandKind::InstantiatePrefab;
		command.Entity = context;
		command.AssetHandle = prefabHandle;
		command.WorldPosition = worldPosition;
		command.Parent = parent;
		return QueueCommand(command);
	}

	bool ScriptEngine::ApplyDeferredCommands(Scene& targetScene,
		uint64_t sceneSessionId, const std::vector<DeferredCommand>& commands,
		bool publishRuntimeSideEffects, std::string& error) const
	{
		error.clear();
		auto runtime = publishRuntimeSideEffects ? GetRuntime() : nullptr;
		std::vector<UUID> createdEntities;
		std::vector<DeferredRuntimeEffect> runtimeEffects;
		std::unordered_set<uint64_t> removedBehaviourAttachments;
		ScriptStatus immediateRuntimeFailure = ScriptStatus::Success;
		DeferredRuntimeEffectScope runtimeEffectScope(
			publishRuntimeSideEffects ? &runtimeEffects : nullptr,
			publishRuntimeSideEffects ? &removedBehaviourAttachments : nullptr,
			publishRuntimeSideEffects ? runtime.get() : nullptr,
			publishRuntimeSideEffects ? &immediateRuntimeFailure : nullptr);
		for (size_t commandIndex = 0; commandIndex < commands.size();
			++commandIndex)
		{
			const DeferredCommand& command = commands[commandIndex];
			auto fail = [&](std::string reason)
			{
				error = "Deferred script command " + std::to_string(commandIndex)
					+ " failed: " + std::move(reason);
				return false;
			};
			if (command.Entity.SceneSessionId != 0
				&& command.Entity.SceneSessionId != sceneSessionId)
				return fail("command belongs to a different Scene session");
			if (command.Kind == DeferredCommandKind::AbortBatch)
				return fail(command.Name.empty()
					? "a mutation was rejected while recording the batch"
					: command.Name);
			if (command.Kind == DeferredCommandKind::CreateEntity)
			{
				Scene* scene = ResolveScene(command.Entity);
				if (!scene || scene->FindEntityByUUID(UUID(command.Entity.EntityId)))
					return fail("reserved entity target is unavailable or already exists");
				const bool hasParent = command.Parent.SceneSessionId != 0
					|| command.Parent.EntityId != 0
					|| command.Parent.RuntimeGeneration != 0;
				std::optional<UUID> parent;
				if (hasParent)
				{
					Entity parentEntity = ResolveEntity(command.Parent);
					if (!parentEntity)
						return fail("reserved entity parent is unavailable");
					parent = parentEntity.GetUUID();
				}
				Entity created = scene->CreateEntityWithUUID(
					UUID(command.Entity.EntityId), command.Name);
				if (!created)
					return fail("could not create the reserved entity");
				if (parent && !scene->SetParent(created,
					scene->FindEntityByUUID(*parent)))
					return fail("could not assign the reserved entity parent");
				if (!created || !scene->SetWorldTransform(created,
					Math::ComposeTransform(glm::vec3(command.WorldPosition.X,
						command.WorldPosition.Y, command.WorldPosition.Z),
						glm::vec3(0.0f), glm::vec3(1.0f))))
					return fail("could not apply the reserved entity world transform");
				createdEntities.push_back(created.GetUUID());
				continue;
			}
			if (command.Kind == DeferredCommandKind::SetComponentProperty)
			{
				const int32_t status = ApplyGameplayComponentPropertyNow(command.Entity,
					command.ComponentType, command.PropertyId, command.PropertyValue);
				if (status != static_cast<int32_t>(ScriptStatus::Success))
					return fail("component property " + std::to_string(command.PropertyId)
						+ " returned status " + std::to_string(status));
				continue;
			}
			if (command.Kind == DeferredCommandKind::SetActiveSelf)
			{
				Entity entity = ResolveEntity(command.Entity);
				if (!entity || !entity.HasComponent<Tag>())
					return fail("ActiveSelf target is unavailable");
				entity.GetComponent<Tag>().ActiveSelf = command.Enabled;
				continue;
			}
			if (command.Kind == DeferredCommandKind::SetEntityName)
			{
				Entity entity = ResolveEntity(command.Entity);
				if (!entity || !entity.HasComponent<Tag>() || command.Name.empty())
					return fail("entity name target or value is unavailable");
				if (entity.GetName() != command.Name
					&& !targetScene.RenameEntity(entity, command.Name))
					return fail("entity name could not be made unique");
				continue;
			}
			if (command.Kind == DeferredCommandKind::SetGameplayTag)
			{
				Entity entity = ResolveEntity(command.Entity);
				if (!entity || !entity.SetGameplayTag(command.Name))
					return fail("gameplay tag target or value is unavailable");
				continue;
			}
			if (command.Kind == DeferredCommandKind::SetLayer)
			{
				Entity entity = ResolveEntity(command.Entity);
				if (!entity || command.Layer >= Physics2DLayerCount
					|| !entity.SetLayer(static_cast<uint8_t>(command.Layer)))
					return fail("entity layer target or value is unavailable");
				continue;
			}
			if (command.Kind == DeferredCommandKind::SetRegisteredComponentProperty)
			{
				const int32_t status = ApplyRegisteredComponentPropertyNow(command.Entity,
					command.RegisteredTypeId, command.RegisteredPropertyId,
					command.PropertyValue);
				if (status != static_cast<int32_t>(ScriptStatus::Success))
					return fail("registered property "
						+ std::to_string(command.RegisteredPropertyId)
						+ " returned status " + std::to_string(status));
				continue;
			}
			if (command.Kind
				== DeferredCommandKind::SetRegisteredComponentStringProperty)
			{
				const NativeUtf8View value{
					reinterpret_cast<const uint8_t*>(command.Name.data()),
					static_cast<uint64_t>(command.Name.size()) };
				const int32_t status = ApplyRegisteredComponentStringPropertyNow(
					command.Entity, command.RegisteredTypeId,
					command.RegisteredPropertyId, value);
				if (status != static_cast<int32_t>(ScriptStatus::Success))
					return fail("registered string property "
						+ std::to_string(command.RegisteredPropertyId)
						+ " returned status " + std::to_string(status));
				continue;
			}
			if (command.Kind == DeferredCommandKind::AddRegisteredComponent
				|| command.Kind == DeferredCommandKind::RemoveRegisteredComponent)
			{
				Entity entity = ResolveEntity(command.Entity);
				const ComponentDescriptor* descriptor = ComponentRegistry::Get().Find(
					UUID(command.RegisteredTypeId));
				std::string componentError;
				const bool adding = command.Kind
					== DeferredCommandKind::AddRegisteredComponent;
				if (!entity || !descriptor || !descriptor->ScriptAccessible
					|| !(adding ? descriptor->Add(entity, componentError)
						: descriptor->Remove(entity, componentError)))
					return fail(std::string(adding ? "could not add registered component "
						: "could not remove registered component ")
						+ std::to_string(command.RegisteredTypeId)
						+ (componentError.empty() ? std::string()
							: ": " + componentError));
				continue;
			}
			if (command.Kind == DeferredCommandKind::SetParent)
			{
				Scene* scene = ResolveScene(command.Entity);
				Entity child = ResolveEntity(command.Entity);
				if (!scene || !child)
					return fail("reparent target is unavailable");
				const bool hasParent = command.Parent.SceneSessionId != 0
					|| command.Parent.EntityId != 0
					|| command.Parent.RuntimeGeneration != 0;
				std::optional<UUID> parent;
				if (hasParent)
				{
					Entity parentEntity = ResolveEntity(command.Parent);
					if (!parentEntity)
						return fail("reparent destination is unavailable");
					parent = parentEntity.GetUUID();
				}
				Entity parentEntity = parent
					? scene->FindEntityByUUID(*parent) : Entity{};
				if (!scene->SetParent(child, parentEntity))
					return fail("reparenting would create an invalid hierarchy");
				continue;
			}
			if (command.Kind == DeferredCommandKind::SetBehaviourEnabled)
			{
				bool applied = false;
				for (UUID entityId : targetScene.m_EntityOrder)
				{
					Entity entity = targetScene.FindEntityByUUID(entityId);
					if (!entity || !entity.HasComponent<CSharpScripts>()) continue;
					for (auto& script : entity.GetComponent<CSharpScripts>().Scripts)
					{
						if (static_cast<uint64_t>(script.AttachmentID)
							== command.AttachmentId)
						{
							script.Enabled = command.Enabled;
							applied = true;
							break;
						}
					}
					if (applied) break;
				}
				if (!applied)
					return fail("managed behaviour attachment is unavailable");
				if (publishRuntimeSideEffects)
					runtimeEffects.push_back({ DeferredRuntimeEffectKind::SetEnabled,
						command.AttachmentId, command.Enabled });
				continue;
			}
			if (command.Kind == DeferredCommandKind::RemoveBehaviour)
			{
				Entity owner;
				for (UUID entityId : targetScene.m_EntityOrder)
				{
					Entity candidate = targetScene.FindEntityByUUID(entityId);
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
					return fail("managed behaviour attachment is unavailable");
				if (publishRuntimeSideEffects)
					removedBehaviourAttachments.insert(command.AttachmentId);

				auto& scripts = owner.GetComponent<CSharpScripts>().Scripts;
				scripts.erase(std::remove_if(scripts.begin(), scripts.end(),
					[&](const CSharpScriptEntry& script)
					{
						return static_cast<uint64_t>(script.AttachmentID)
							== command.AttachmentId;
					}), scripts.end());
				if (publishRuntimeSideEffects)
					runtimeEffects.push_back({
						DeferredRuntimeEffectKind::DestroyAttachment,
						command.AttachmentId, false });
				continue;
			}
			if (command.Kind == DeferredCommandKind::InstantiatePrefab)
			{
				Scene* scene = ResolveScene(command.Entity);
				if (!scene || !ResolveEntity(command.Entity))
					return fail("Prefab context is unavailable");
				PrefabArchive archive;
				std::string prefabError;
				if (!PrefabArchiveCodec::Load(TomCat::AssetHandle(command.AssetHandle),
					archive, prefabError))
					return fail("could not load Prefab "
						+ std::to_string(command.AssetHandle) + ": " + prefabError);

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
						return fail("Prefab parent is unavailable");
					options.Parent = UUID(command.Parent.EntityId);
				}
				PrefabInstantiationResult instance;
				if (!PrefabArchiveCodec::Instantiate(archive, *scene, options,
					instance, prefabError))
					return fail("could not instantiate Prefab "
						+ std::to_string(command.AssetHandle) + ": " + prefabError);
				continue;
			}

			Entity entity = ResolveEntity(command.Entity);
			if (command.Kind == DeferredCommandKind::DestroyEntity)
			{
				// QueueDestroyEntity projects subtree destruction and suppresses
				// duplicates. Treat an already-destroyed target as the same
				// successful no-op during validation and live replay.
				if (!entity)
					continue;
				Scene* scene = ResolveScene(command.Entity);
				if (!scene)
					return fail("destroy target Scene is unavailable");
				scene->DestroyEntity(entity);
				if (!IsSuccess(immediateRuntimeFailure))
				{
					ReportFailure("DestroyAttachments", immediateRuntimeFailure);
					return fail("managed destruction lifecycle returned status "
						+ std::to_string(static_cast<int32_t>(immediateRuntimeFailure)));
				}
				if (scene->FindEntityByUUID(UUID(command.Entity.EntityId)))
					return fail("entity destruction did not complete");
				continue;
			}
			if (!entity)
				return fail("entity target is unavailable");

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
				default: return fail("component type is unsupported");
			}
		}
		if (runtime)
		{
			InvalidateProjectionSnapshots();
			for (const DeferredRuntimeEffect& effect : runtimeEffects)
			{
				ScriptStatus status = ScriptStatus::Success;
				if (effect.Kind == DeferredRuntimeEffectKind::SetEnabled)
					status = runtime->SetEnabled(effect.AttachmentId, effect.Enabled);
				else
				{
					const std::array<uint64_t, 1> ids{ effect.AttachmentId };
					status = runtime->DestroyAttachments(ids);
				}
				if (!IsSuccess(status))
				{
					ReportFailure(effect.Kind == DeferredRuntimeEffectKind::SetEnabled
						? "SetEnabled" : "DestroyAttachments", status);
					error = "managed lifecycle publication returned status "
						+ std::to_string(static_cast<int32_t>(status));
					return false;
				}
			}
		}
		if (publishRuntimeSideEffects && targetScene.IsRuntimeRunning()
			&& !createdEntities.empty())
			targetScene.QueueRuntimeEntityBatchCreated(std::move(createdEntities));
		return true;
	}

	bool ScriptEngine::FlushDeferredCommands(uint64_t sceneSessionId)
	{
		if (sceneSessionId == 0)
			return true;

		std::vector<DeferredCommand> commands;
		uint64_t blockingCallbackToken = 0;
		{
			std::lock_guard<std::mutex> lock(m_Mutex);
			// Callback-owned commands must be sealed by CompleteCallback so their
			// boundary cannot be merged into the legacy Scene-wide batch.
			if (m_OpenDeferredCallbackTransaction)
				blockingCallbackToken = m_OpenDeferredCallbackTransaction->Token;
			else
			{
				for (auto iterator = m_DeferredCommands.begin();
					iterator != m_DeferredCommands.end();)
				{
					if (iterator->Entity.SceneSessionId == sceneSessionId)
					{
						commands.push_back(std::move(*iterator));
						iterator = m_DeferredCommands.erase(iterator);
					}
					else ++iterator;
				}
				if (!commands.empty())
				{
					++m_ProjectionRevision;
					m_ProjectionSnapshots.clear();
				}
			}
		}
		if (blockingCallbackToken != 0)
		{
			TC_Core_Error("Cannot flush legacy deferred commands while managed callback transaction {0} is open",
				blockingCallbackToken);
			StopSceneAfterRuntimeFailure(sceneSessionId,
				"Flush deferred commands", ScriptStatus::InvalidState);
			return false;
		}
		return CommitDeferredCommandBatch(
			sceneSessionId, std::move(commands), false);
	}

	bool ScriptEngine::CommitDeferredCommandBatch(uint64_t sceneSessionId,
		std::vector<DeferredCommand> commands, bool resolveEmpty)
	{
		Scene* liveScene = nullptr;
		uint64_t runtimeGeneration = 0;
		{
			std::lock_guard<std::mutex> lock(m_Mutex);
			const auto binding = m_Scenes.find(sceneSessionId);
			if (binding != m_Scenes.end())
			{
				liveScene = binding->second.ScenePointer;
				runtimeGeneration = binding->second.RuntimeGeneration;
			}
		}
		auto runtime = GetRuntime();
		auto resolveManagedProjection = [&](bool committed)
		{
			if (!runtime)
			{
				TC_Core_Error("Scene session {0} has no managed runtime to {1} its deferred projection",
					sceneSessionId, committed ? "commit" : "abort");
				return false;
			}
			ScriptStatus status = ScriptStatus::InvalidState;
			try
			{
				status = runtime->ResolveDeferredCommandBatch(committed);
			}
			catch (const std::exception& exception)
			{
				TC_Core_Error("C# script operation {0} threw: {1}",
					committed ? "Commit deferred managed projection"
						: "Abort deferred managed projection",
					exception.what());
				return false;
			}
			catch (...)
			{
				TC_Core_Error("C# script operation {0} threw",
					committed ? "Commit deferred managed projection"
						: "Abort deferred managed projection");
				return false;
			}
			if (!IsSuccess(status))
				ReportFailure(committed ? "Commit deferred managed projection"
					: "Abort deferred managed projection", status);
			return IsSuccess(status);
		};
		auto stopAfterFatalPublication = [&](const char* phase)
		{
			TC_Core_Error("Scene session {0} entered a script runtime fault during {1}; Play is being stopped",
				sceneSessionId, phase ? phase : "deferred command publication");
			// OnRuntimeStart assigns its ScriptEngine session only after StartScene
			// returns. Stop native runtime systems first, then explicitly stop the
			// still-owned ScriptEngine session. StopScene is session-idempotent.
			if (liveScene && liveScene->IsRuntimeRunning())
				liveScene->OnRuntimeStop();
			StopScene(sceneSessionId);
		};
		auto resolveRejectedBatch = [&]()
		{
			if (resolveManagedProjection(false))
				return true;
			stopAfterFatalPublication("deferred abort acknowledgement");
			return false;
		};

		if (commands.empty())
		{
			if (!resolveEmpty)
				return true;
			if (resolveManagedProjection(
				liveScene && runtimeGeneration != 0))
				return true;
			stopAfterFatalPublication("empty deferred callback acknowledgement");
			return false;
		}
		if (!liveScene || runtimeGeneration == 0)
		{
			TC_Core_Error("Could not commit {0} deferred C# commands because Scene session {1} is unavailable",
				commands.size(), sceneSessionId);
			return resolveRejectedBatch();
		}

		Ref<Scene> nonOwning(liveScene, [](Scene*) {});
		Ref<Scene> staged;
		try
		{
			ComponentMutationPhaseScope validationPhase(
				ComponentMutationPhase::Validation);
			staged = Scene::Copy(nonOwning);
		}
		catch (const std::exception& exception)
		{
			TC_Core_Error("Deferred C# command batch for Scene session {0} threw while creating its validation snapshot and was aborted before live mutation: {1}",
				sceneSessionId, exception.what());
			return resolveRejectedBatch();
		}
		catch (...)
		{
			TC_Core_Error("Deferred C# command batch for Scene session {0} threw while creating its validation snapshot and was aborted before live mutation",
				sceneSessionId);
			return resolveRejectedBatch();
		}
		if (!staged)
		{
			TC_Core_Error("Deferred C# command batch for Scene session {0} was aborted because its validation snapshot could not be created",
				sceneSessionId);
			return resolveRejectedBatch();
		}

		std::string error;
		try
		{
			ComponentMutationPhaseScope componentValidationPhase(
				ComponentMutationPhase::Validation);
			DeferredValidationSceneScope validationScope(*staged,
				sceneSessionId, runtimeGeneration);
			if (!ApplyDeferredCommands(*staged, sceneSessionId, commands,
				false, error))
			{
				TC_Core_Error("Deferred C# command batch for Scene session {0} was aborted before live mutation: {1}",
					sceneSessionId, error);
				return resolveRejectedBatch();
			}
		}
		catch (const std::exception& exception)
		{
			TC_Core_Error("Deferred C# command batch for Scene session {0} threw during validation and was aborted before live mutation: {1}",
				sceneSessionId, exception.what());
			return resolveRejectedBatch();
		}
		catch (...)
		{
			TC_Core_Error("Deferred C# command batch for Scene session {0} threw during validation and was aborted before live mutation",
				sceneSessionId);
			return resolveRejectedBatch();
		}

		bool replaySucceeded = false;
		try
		{
			replaySucceeded = ApplyDeferredCommands(*liveScene, sceneSessionId,
				commands, true, error);
			InvalidateProjectionSnapshots();
			if (!replaySucceeded)
				TC_Core_Error("Validated deferred C# command batch for Scene session {0} unexpectedly failed during live replay: {1}",
					sceneSessionId, error);
		}
		catch (const std::exception& exception)
		{
			replaySucceeded = false;
			TC_Core_Error("Validated deferred C# command batch for Scene session {0} unexpectedly threw during live replay: {1}",
				sceneSessionId, exception.what());
		}
		catch (...)
		{
			replaySucceeded = false;
			TC_Core_Error("Validated deferred C# command batch for Scene session {0} unexpectedly threw during live replay",
				sceneSessionId);
		}
		if (replaySucceeded && resolveManagedProjection(true))
			return true;

		// Once the commit acknowledgement was attempted, a second abort
		// acknowledgement would consume the next callback's projection frame.
		if (!replaySucceeded)
			resolveManagedProjection(false);
		// The validation pass guarantees deterministic command failures occur
		// before live mutation. A provider callback that changes its result on the
		// second invocation, a managed lifecycle publication failure, or a failed
		// commit acknowledgement leaves native and managed state unsynchronized.
		stopAfterFatalPublication("deferred command publication");
		return false;
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

		InvalidateProjectionSnapshots();
		Entity entity = scene.FindEntityByUUID(UUID(entityId));
		std::vector<uint64_t> ids;
		if (entity && entity.HasComponent<CSharpScripts>())
		{
			for (const CSharpScriptEntry& script :
				entity.GetComponent<CSharpScripts>().Scripts)
			{
				const uint64_t id = static_cast<uint64_t>(script.AttachmentID);
				if (id != 0)
					ids.push_back(id);
			}
		}
		if (DeferredRuntimeEffects)
		{
			for (uint64_t id : ids)
				if (DeferredRemovedBehaviourAttachments)
					DeferredRemovedBehaviourAttachments->insert(id);
			// Every earlier managed effect must publish before any Entity identity
			// disappears, including RemoveBehaviour(last) followed by DestroyEntity.
			// Drain the ordered effects even when this Entity no longer has script
			// entries, then destroy its remaining attachments while getters can still
			// inspect the Entity and its components.
			if (DeferredRuntime && DeferredRuntimeFailure)
			{
				for (const DeferredRuntimeEffect& effect : *DeferredRuntimeEffects)
				{
					ScriptStatus status = ScriptStatus::Success;
					if (effect.Kind == DeferredRuntimeEffectKind::SetEnabled)
						status = DeferredRuntime->SetEnabled(
							effect.AttachmentId, effect.Enabled);
					else
					{
						const std::array<uint64_t, 1> effectIds{
							effect.AttachmentId };
						status = DeferredRuntime->DestroyAttachments(effectIds);
					}
					if (!IsSuccess(status)
						&& IsSuccess(*DeferredRuntimeFailure))
						*DeferredRuntimeFailure = status;
				}
				DeferredRuntimeEffects->clear();
				if (!ids.empty())
				{
					const ScriptStatus status =
						DeferredRuntime->DestroyAttachments(ids);
					if (!IsSuccess(status)
						&& IsSuccess(*DeferredRuntimeFailure))
						*DeferredRuntimeFailure = status;
				}
			}
			else
			{
				for (uint64_t id : ids)
					DeferredRuntimeEffects->push_back({
						DeferredRuntimeEffectKind::DestroyAttachment, id, false });
			}
			return;
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
