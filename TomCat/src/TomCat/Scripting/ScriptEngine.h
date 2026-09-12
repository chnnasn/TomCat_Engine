#pragma once

#include "IScriptRuntime.h"
#include "TomCat/Core/UUID.h"

#include <array>
#include <memory>
#include <mutex>
#include <thread>
#include <unordered_map>
#include <vector>

namespace TomCat {

	class Entity;
	class Scene;

	namespace Scripting {

		class ScriptEngine
		{
		public:
			static ScriptEngine& Get();

			void SetRuntime(std::shared_ptr<IScriptRuntime> runtime);
			std::shared_ptr<IScriptRuntime> GetRuntime() const;
			// Returns a nonzero SceneSessionID when all instances were created, bound,
			// restored, OnCreate'd and initially enabled successfully.
			uint64_t StartScene(Scene& scene, uint64_t runtimeGeneration);
			// Safe-point entry for a running Scene that started without scripts. The
			// existing entities bootstrap transactionally, while entityIDs are sent
			// through InstantiateAttachments. A failed dynamic batch is rolled back;
			// a nonzero return means the Scene runtime itself remains active.
			uint64_t StartSceneForRuntimeBatch(Scene& scene, uint64_t runtimeGeneration,
				std::span<const UUID> entityIDs);
			void StopScene(uint64_t sceneSessionId);
			void UpdateAll(uint64_t sceneSessionId, float deltaTime);
			void FixedUpdateAll(uint64_t sceneSessionId, float fixedDeltaTime);
			void DispatchPhysicsEvents(uint64_t sceneSessionId,
				std::span<const NativePhysicsEventV1> events);

			Scene* ResolveScene(const EntityHandleV1& handle) const;
			Entity ResolveEntity(const EntityHandleV1& handle) const;
			uint64_t GetRuntimeGeneration(uint64_t sceneSessionId) const;
			bool IsMainThread() const;

			bool GetBehaviourEnabled(uint64_t attachmentId, bool& enabled) const;
			bool QueueBehaviourEnabled(uint64_t attachmentId, bool enabled);
			bool QueueRemoveBehaviour(uint64_t attachmentId);
			bool QueueDestroyEntity(const EntityHandleV1& entity);
			bool QueueAddComponent(const EntityHandleV1& entity,
				NativeComponentType componentType);
			bool QueueRemoveComponent(const EntityHandleV1& entity,
				NativeComponentType componentType);
			bool QueueInstantiatePrefab(const EntityHandleV1& context,
				uint64_t prefabHandle, NativeVector3 worldPosition,
				const EntityHandleV1& parent);
			void FlushDeferredCommands(uint64_t sceneSessionId);
			// Called by Scene while the entity and its pure-data script entries are
			// still alive, so managed OnDisable/OnDestroy can safely inspect Entity.
			void NotifyEntityDestroyed(Scene& scene, uint64_t entityId);

			// Called once at the start of each display frame by Editor/Player.
			void CaptureInputState();
			bool IsKeyHeld(uint32_t key) const;
			bool WasKeyPressed(uint32_t key) const;
			bool WasKeyReleased(uint32_t key) const;
			NativeVector2 GetMousePosition() const;
			NativeVector2 GetMouseDelta() const;
			uint32_t GetModifiers() const;

		private:
			ScriptEngine();

			struct SceneBinding
			{
				Scene* ScenePointer = nullptr;
				uint64_t RuntimeGeneration = 0;
			};

			enum class DeferredCommandKind : uint8_t
			{
				DestroyEntity,
				AddComponent,
				RemoveComponent,
				SetBehaviourEnabled,
				RemoveBehaviour,
				InstantiatePrefab
			};

			struct DeferredCommand
			{
				DeferredCommandKind Kind = DeferredCommandKind::DestroyEntity;
				EntityHandleV1 Entity;
				NativeComponentType ComponentType = NativeComponentType::Transform;
				uint64_t AttachmentId = 0;
				uint64_t AssetHandle = 0;
				NativeVector3 WorldPosition;
				EntityHandleV1 Parent;
				bool Enabled = false;
			};

			bool QueueCommand(DeferredCommand command);
			uint64_t StartSceneCore(Scene& scene, uint64_t runtimeGeneration,
				std::span<const UUID> initialEntityIDs, bool flushPendingCreates);
			void InstallRuntimeEntityBatchCallback(Scene& scene, uint64_t sceneSessionId);
			void RollbackRuntimeEntityBatch(Scene& scene,
				std::span<const UUID> entityIDs, bool destroyManagedAttachments);
			std::string SerializeFields(Scene& scene) const;
			std::string SerializeFields(Scene& scene,
				std::span<const UUID> entityIDs) const;
			bool InstantiateRuntimeAttachments(Scene& scene, uint64_t sceneSessionId,
				std::span<const UUID> entityIDs);
			void ReportFailure(const char* operation, ScriptStatus status) const;

		private:
			mutable std::mutex m_Mutex;
			std::shared_ptr<IScriptRuntime> m_Runtime;
			std::unordered_map<uint64_t, SceneBinding> m_Scenes;
			std::vector<DeferredCommand> m_DeferredCommands;
			uint64_t m_NextSceneSessionId = 1;
			std::thread::id m_MainThread;
			std::array<bool, 512> m_CurrentKeys{};
			std::array<bool, 512> m_PreviousKeys{};
			NativeVector2 m_MousePosition{};
			NativeVector2 m_PreviousMousePosition{};
			NativeVector2 m_MouseDelta{};
		};

	}

}
