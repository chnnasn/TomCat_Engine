#pragma once

#include "IScriptRuntime.h"
#include "TomCat/Core/UUID.h"

#include <array>
#include <cstddef>
#include <deque>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <thread>
#include <unordered_map>
#include <unordered_set>
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
			// Invokes one persistent UI/event callback before the regular OnUpdate
			// phase. The exact attachment identity prevents duplicate components from
			// receiving another instance's callback.
			ScriptStatus InvokeMethod(Scene& scene, UUID targetEntity,
				UUID targetAttachmentId, uint64_t expectedScriptAsset,
				std::string_view methodName);
			// A fixed-step scope owns the scene's pending input batch until Box2D
			// and its resulting script callbacks have both completed.
			bool BeginFixedStep(uint64_t sceneSessionId);
			void EndFixedStep(uint64_t sceneSessionId);
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
			// Appends an explicit abort marker for the current callback batch.
			// This preserves atomicity when a later mutation is rejected before it
			// can become a normal deferred command.
			bool MarkDeferredCommandBatchFailed(const EntityHandleV1& context,
				std::string reason);
			// Callback-scoped transactions are layered over the legacy Scene queue.
			// Begin records the queue suffix owned by one managed callback; Complete
			// seals it and the outermost completion drains all reentrant batches.
			bool BeginDeferredCallbackTransaction(const EntityHandleV1& context,
				uint64_t& token);
			bool CompleteDeferredCallbackTransaction(uint64_t token);
			bool QueueCreateEntity(const EntityHandleV1& context, std::string name,
				NativeVector3 worldPosition, const EntityHandleV1& parent,
				EntityHandleV1& reservedEntity);
			bool IsPendingCreate(const EntityHandleV1& entity) const;
			// Resolves the entity against the ordered command stream. A known entity
			// that has already been destroyed (including by an ancestor destroy) is
			// reported with alive=false so duplicate Destroy calls can be no-ops while
			// every other mutation rejects the projected-dead target.
			bool GetProjectedEntityLiveness(const EntityHandleV1& entity,
				bool& alive) const;
			bool GetProjectedParent(const EntityHandleV1& entity,
				EntityHandleV1& parent) const;
			bool GetProjectedChildren(const EntityHandleV1& entity,
				std::vector<EntityHandleV1>& children) const;
			bool GetProjectedEntities(const EntityHandleV1& context,
				std::vector<EntityHandleV1>& entities) const;
			bool GetProjectedEntityName(const EntityHandleV1& entity,
				std::string& name) const;
			bool GetProjectedGameplayTag(const EntityHandleV1& entity,
				std::string& tag) const;
			bool GetProjectedLayer(const EntityHandleV1& entity,
				uint32_t& layer) const;
			bool GetProjectedActiveInHierarchy(const EntityHandleV1& entity,
				bool& active) const;
			bool GetProjectedTransformProperty(const EntityHandleV1& entity,
				uint32_t propertyId, NativeVector3& value) const;
			bool QueueSetEntityName(const EntityHandleV1& entity, std::string name);
			bool QueueSetGameplayTag(const EntityHandleV1& entity, std::string tag);
			bool QueueSetLayer(const EntityHandleV1& entity, uint32_t layer);
			bool GetProjectedComponentPresence(const EntityHandleV1& entity,
				NativeComponentType componentType, bool& present) const;
			bool TryGetProjectedComponentProperty(const EntityHandleV1& entity,
				NativeComponentType componentType, uint32_t propertyId,
				NativePropertyValueV1& value, bool& useDefault) const;
			bool QueueSetParent(const EntityHandleV1& entity,
				const EntityHandleV1& parent);
			bool QueueAddComponent(const EntityHandleV1& entity,
				NativeComponentType componentType);
			bool QueueRemoveComponent(const EntityHandleV1& entity,
				NativeComponentType componentType);
			bool QueueSetComponentProperty(const EntityHandleV1& entity,
				NativeComponentType componentType, uint32_t propertyId,
				NativePropertyValueV1 value);
			bool QueueSetActiveSelf(const EntityHandleV1& entity, bool active);
			bool GetProjectedActiveSelf(const EntityHandleV1& entity,
				bool& active) const;
			bool GetProjectedRegisteredComponentPresence(const EntityHandleV1& entity,
				uint64_t componentTypeId, bool& present) const;
			bool TryGetProjectedRegisteredComponentProperty(
				const EntityHandleV1& entity, uint64_t componentTypeId,
				uint64_t propertyId, NativePropertyValueV1& value,
				bool& useDefault) const;
			bool TryGetProjectedRegisteredComponentStringProperty(
				const EntityHandleV1& entity, uint64_t componentTypeId,
				uint64_t propertyId, std::string& value, bool& useDefault) const;
			bool QueueAddRegisteredComponent(const EntityHandleV1& entity,
				uint64_t componentTypeId);
			bool QueueRemoveRegisteredComponent(const EntityHandleV1& entity,
				uint64_t componentTypeId);
			bool QueueSetRegisteredComponentProperty(const EntityHandleV1& entity,
				uint64_t componentTypeId, uint64_t propertyId,
				NativePropertyValueV1 value);
			bool QueueSetRegisteredComponentStringProperty(
				const EntityHandleV1& entity, uint64_t componentTypeId,
				uint64_t propertyId, std::string value);
			bool QueueInstantiatePrefab(const EntityHandleV1& context,
				uint64_t prefabHandle, NativeVector3 worldPosition,
				const EntityHandleV1& parent);
			bool FlushDeferredCommands(uint64_t sceneSessionId);
			// Called by Scene while the entity and its pure-data script entries are
			// still alive, so managed OnDisable/OnDestroy can safely inspect Entity.
			void NotifyEntityDestroyed(Scene& scene, uint64_t entityId);

			// Called once by Application after native events have been polled and the
			// display-frame Input snapshot has been frozen.
			void CaptureInputState();
			bool IsKeyHeld(uint32_t key) const;
			bool WasKeyPressed(uint32_t key) const;
			bool WasKeyReleased(uint32_t key) const;
			bool IsMouseButtonHeld(uint32_t button) const;
			bool WasMouseButtonPressed(uint32_t button) const;
			bool WasMouseButtonReleased(uint32_t button) const;
			NativeVector2 GetMousePosition() const;
			NativeVector2 GetMouseDelta() const;
			NativeVector2 GetScrollDelta() const;
			bool IsWindowFocused() const;
			bool IsGamepadConnected(uint32_t gamepad) const;
			bool WasGamepadConnected(uint32_t gamepad) const;
			bool WasGamepadDisconnected(uint32_t gamepad) const;
			bool IsGamepadButtonHeld(uint32_t gamepad, uint32_t button) const;
			bool WasGamepadButtonPressed(uint32_t gamepad, uint32_t button) const;
			bool WasGamepadButtonReleased(uint32_t gamepad, uint32_t button) const;
			float GetGamepadAxis(uint32_t gamepad, uint32_t axis) const;
			const std::string& GetGamepadName(uint32_t gamepad) const;
			uint32_t GetModifiers() const;
			NativeInputEventBatchInfoV1 GetInputEventBatchInfo() const;
			const std::vector<NativeInputEventV1>& GetInputEvents() const;

		private:
			ScriptEngine();

			struct SceneBinding
			{
				Scene* ScenePointer = nullptr;
				uint64_t RuntimeGeneration = 0;
			};

			enum class DeferredCommandKind : uint8_t
			{
				AbortBatch,
				DestroyEntity,
				AddComponent,
				RemoveComponent,
				SetBehaviourEnabled,
				RemoveBehaviour,
				InstantiatePrefab,
				CreateEntity,
				SetParent,
				SetComponentProperty,
				SetActiveSelf,
				SetEntityName,
				SetGameplayTag,
				SetLayer,
				AddRegisteredComponent,
				RemoveRegisteredComponent,
				SetRegisteredComponentProperty,
				SetRegisteredComponentStringProperty
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
				NativePropertyValueV1 PropertyValue;
				uint32_t PropertyId = 0;
				uint64_t RegisteredTypeId = 0;
				uint64_t RegisteredPropertyId = 0;
				std::string Name;
				uint32_t Layer = 0;
				bool Enabled = false;
			};

			struct OpenDeferredCallbackTransaction
			{
				uint64_t Token = 0;
				EntityHandleV1 Context;
				size_t CommandOffset = 0;
			};

			struct SealedDeferredCallbackTransaction
			{
				uint64_t Token = 0;
				EntityHandleV1 Context;
				std::vector<DeferredCommand> Commands;
			};

			struct ProjectedEntityState
			{
				EntityHandleV1 Handle;
				uint64_t ParentId = 0;
				std::string Name;
				std::string GameplayTag = "Untagged";
				uint32_t Layer = 0;
				bool ActiveSelf = true;
				bool Alive = true;
				NativeVector3 Translation{};
				NativeVector3 Rotation{};
				NativeVector3 Scale{ 1.0f, 1.0f, 1.0f };
				NativeVector3 LocalTranslation{};
				NativeVector3 LocalRotation{};
				NativeVector3 LocalScale{ 1.0f, 1.0f, 1.0f };
			};

			struct ProjectionSnapshot
			{
				uint64_t Revision = 0;
				uint64_t RuntimeGeneration = 0;
				bool Valid = true;
				std::string Error;
				std::vector<uint64_t> Order;
				std::unordered_map<uint64_t, ProjectedEntityState> Entities;
				std::unordered_map<uint64_t, std::vector<uint64_t>> Children;
				// Validation-only Scene produced by replaying the same ordered command
				// stream used at commit. Registered component queries read this copy so
				// descriptor Add side effects are visible inside the recording callback.
				std::shared_ptr<Scene> ProjectedScene;
			};

			std::shared_ptr<const ProjectionSnapshot> GetProjectionSnapshot(
				const EntityHandleV1& context) const;
			void InvalidateProjectionSnapshots() const;
			bool DrainDeferredCallbackTransactions();
			bool CommitDeferredCommandBatch(uint64_t sceneSessionId,
				std::vector<DeferredCommand> commands, bool resolveEmpty);

			bool QueueCommand(DeferredCommand command);
			// Returns false for an attachment that never existed in any bound Scene.
			// A known attachment remains distinguishable after a queued removal so a
			// duplicate Remove can be an idempotent success while later setters fail.
			bool GetProjectedBehaviourPresence(uint64_t attachmentId,
				bool& present, EntityHandleV1* owner = nullptr) const;
			bool ApplyDeferredCommands(Scene& scene, uint64_t sceneSessionId,
				const std::vector<DeferredCommand>& commands,
				bool publishRuntimeSideEffects, std::string& error) const;
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
			void StopSceneAfterRuntimeFailure(uint64_t sceneSessionId,
				const char* operation, ScriptStatus status);
			enum class InputDispatchPhase : uint8_t
			{
				DisplayFrame,
				FixedUpdate
			};

			struct FixedInputBatch
			{
				uint64_t FirstFrameNumber = 0;
				uint64_t LastFrameNumber = 0;
				uint64_t DroppedEventCount = 0;
				std::vector<NativeInputEventV1> Events;
				std::array<bool, 512> KeysPressed{};
				std::array<bool, 512> KeysReleased{};
				std::array<bool, 8> MouseButtonsPressed{};
				std::array<bool, 8> MouseButtonsReleased{};
				std::array<bool, 16> GamepadsConnected{};
				std::array<bool, 16> GamepadsDisconnected{};
				std::array<std::array<bool, 15>, 16> GamepadButtonsPressed{};
				std::array<std::array<bool, 15>, 16> GamepadButtonsReleased{};
				NativeVector2 MouseDelta{};
				NativeVector2 ScrollDelta{};
			};

			void AccumulateCurrentInput(FixedInputBatch& batch) const;

		private:
			mutable std::mutex m_Mutex;
			std::shared_ptr<IScriptRuntime> m_Runtime;
			std::unordered_map<uint64_t, SceneBinding> m_Scenes;
			std::unordered_set<uint64_t> m_StoppingSceneSessions;
			std::vector<DeferredCommand> m_DeferredCommands;
			std::optional<OpenDeferredCallbackTransaction>
				m_OpenDeferredCallbackTransaction;
			std::deque<SealedDeferredCallbackTransaction>
				m_SealedDeferredCallbackTransactions;
			uint64_t m_NextDeferredCallbackTransactionToken = 1;
			bool m_DrainingDeferredCallbackTransactions = false;
			mutable uint64_t m_ProjectionRevision = 1;
			mutable std::unordered_map<uint64_t,
				std::shared_ptr<const ProjectionSnapshot>> m_ProjectionSnapshots;
			uint64_t m_NextSceneSessionId = 1;
			std::thread::id m_MainThread;
			std::array<bool, 512> m_CurrentKeys{};
			std::array<bool, 512> m_KeysPressedThisFrame{};
			std::array<bool, 512> m_KeysReleasedThisFrame{};
			std::array<bool, 8> m_CurrentMouseButtons{};
			std::array<bool, 8> m_MouseButtonsPressedThisFrame{};
			std::array<bool, 8> m_MouseButtonsReleasedThisFrame{};
			NativeVector2 m_MousePosition{};
			NativeVector2 m_PreviousMousePosition{};
			NativeVector2 m_MouseDelta{};
			NativeVector2 m_ScrollDelta{};
			bool m_WindowFocused = true;
			bool m_PreviousWindowFocused = true;

			struct GamepadState
			{
				bool Connected = false;
				std::array<bool, 15> Buttons{};
				std::array<float, 6> Axes{};
				std::string Name;
			};
			std::array<GamepadState, 16> m_CurrentGamepads{};
			std::array<GamepadState, 16> m_PreviousGamepads{};
			std::array<bool, 16> m_GamepadsConnectedThisFrame{};
			std::array<bool, 16> m_GamepadsDisconnectedThisFrame{};
			std::array<std::array<bool, 15>, 16> m_GamepadButtonsPressedThisFrame{};
			std::array<std::array<bool, 15>, 16> m_GamepadButtonsReleasedThisFrame{};
			uint64_t m_LastCapturedInputFrame = 0;
			uint64_t m_InputEventsDroppedThisFrame = 0;
			std::vector<NativeInputEventV1> m_InputEventsThisFrame;
			InputDispatchPhase m_InputDispatchPhase = InputDispatchPhase::DisplayFrame;
			bool m_FixedStepExposesTransitions = false;
			uint64_t m_ActiveFixedStepSceneSessionId = 0;
			InputDispatchPhase m_PreviousFixedStepInputDispatchPhase =
				InputDispatchPhase::DisplayFrame;
			bool m_PreviousFixedStepExposesTransitions = false;
			std::unordered_map<uint64_t, FixedInputBatch> m_PendingFixedInput;
			FixedInputBatch m_ActiveFixedInput;
		};

	}

}
