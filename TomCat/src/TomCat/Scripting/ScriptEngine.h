#pragma once

#include "IScriptRuntime.h"

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
				RemoveBehaviour
			};

			struct DeferredCommand
			{
				DeferredCommandKind Kind = DeferredCommandKind::DestroyEntity;
				EntityHandleV1 Entity;
				NativeComponentType ComponentType = NativeComponentType::Transform;
				uint64_t AttachmentId = 0;
				bool Enabled = false;
			};

			bool QueueCommand(DeferredCommand command);
			std::string SerializeFields(Scene& scene) const;
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
