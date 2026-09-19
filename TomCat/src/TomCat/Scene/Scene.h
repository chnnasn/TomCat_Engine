#pragma once
#include "entt.hpp"
#include "TomCat/Core/Timestep.h"
#include "TomCat/Core/UUID.h"
#include "TomCat/Asset/Asset.h"
#include "TomCat/Project/ProjectSettings.h"
#include "TomCat/Renderer/EditorCamera.h"
#include "Physics2DEvents.h"

#include <cstdint>
#include <functional>
#include <optional>
#include <span>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>


class b2World;
class b2Body;
class b2Fixture;
class b2Joint;

namespace TomCat {
	namespace Scripting { class ScriptEngine; }

	class Entity;
	class SceneContactFilter2D;
	class SceneContactListener;

	enum class ColliderDebugShapeType
	{
		Box,
		Circle
	};

	struct ColliderDebugShape
	{
		ColliderDebugShapeType Type = ColliderDebugShapeType::Box;
		UUID EntityID{ 0 };
		bool Enabled = true;
		bool IsTrigger = false;
		uint16_t CollisionLayer = 0x0001;
		// Unit quad/circle transform, ready for Renderer2D::DrawRect/DrawCircle.
		glm::mat4 Transform{ 1.0f };
		glm::vec2 Center{ 0.0f };
		glm::vec2 HalfSize{ 0.0f };
		float Radius = 0.0f;
		float Rotation = 0.0f;
	};

	struct RaycastHit2D
	{
		UUID EntityID{ 0 };
		glm::vec2 Point{ 0.0f };
		glm::vec2 Normal{ 0.0f };
		float Fraction = 0.0f;
		bool IsTrigger = false;
		// Bit corresponding to EntityMetadata::Layer, not the fixture's raw
		// Box2D categoryBits value.
		uint16_t CollisionLayer = 0;
	};

	struct PhysicsQueryHit2D
	{
		UUID EntityID{ 0 };
		bool IsTrigger = false;
		// Bit corresponding to EntityMetadata::Layer, not the fixture's raw
		// Box2D categoryBits value.
		uint16_t CollisionLayer = 0;
	};

	// Cumulative counters for runtime physics definition synchronization. These
	// count definition snapshot scans and Box2D mutations, rather than simulation
	// steps, so regressions can distinguish safe-point coalescing from object work.
	struct RuntimePhysicsSyncStatistics
	{
		uint64_t DefinitionScans = 0;
		uint64_t CoalescedRequests = 0;
		uint64_t WorldRebuilds = 0;
		uint64_t BodiesCreated = 0;
		uint64_t BodiesDestroyed = 0;
		uint64_t BodiesUpdatedInPlace = 0;
		uint64_t BoxFixturesCreated = 0;
		uint64_t BoxFixturesDestroyed = 0;
		uint64_t CircleFixturesCreated = 0;
		uint64_t CircleFixturesDestroyed = 0;
		uint64_t DistanceJointsCreated = 0;
		uint64_t DistanceJointsDestroyed = 0;
	};

	class Scene
	{
	public:
		using CollisionListenerHandle = uint64_t;
		using CollisionEnter2DCallback = std::function<void(const CollisionEnter2D&)>;
		using CollisionExit2DCallback = std::function<void(const CollisionExit2D&)>;
		using TriggerEnter2DCallback = std::function<void(const TriggerEnter2D&)>;
		using TriggerExit2DCallback = std::function<void(const TriggerExit2D&)>;
		using RuntimeEntityBatchCreatedCallback =
			std::function<void(std::span<const UUID>)>;

		static constexpr float FixedRuntimeTimestep = 1.0f / 60.0f;
		static constexpr float MaximumRuntimeFrameDelta = 0.25f;
		static constexpr uint32_t MaximumRuntimeSubsteps = 8;

		enum class EntityPlacement
		{
			Before,
			Child,
			After,
			Root
		};

		Scene();
		~Scene();

		static Ref<Scene> Copy(Ref<Scene> other);

		const std::string& GetSceneName() const { return m_SceneName; }
		void SetSceneName(const std::string& sceneName) { m_SceneName = sceneName.empty() ? "Untitled" : sceneName; }

		Entity CreateEntity(const std::string& name = std::string());
		Entity CreateEntityWithUUID(UUID uuid, const std::string& name = std::string());
		bool RenameEntity(Entity entity, const std::string& requestedName);
		void DestroyEntity(Entity entity);
		bool SetParent(Entity child, Entity parent);
		bool MoveEntity(Entity entity, Entity target, EntityPlacement placement);
		bool SetWorldTransform(Entity entity, const glm::mat4& worldTransform);
		bool SetLocalTransform(Entity entity, const glm::mat4& localTransform);
		bool SyncTransformHierarchy();
		Entity GetParent(Entity entity);
		std::vector<UUID> GetChildrenUUIDs(Entity entity);
		std::vector<UUID> GetRootEntityUUIDs();
		bool IsActiveInHierarchy(Entity entity) const;
		bool IsEditorHidden(Entity entity) const;
		bool IsVisibleInEditorHierarchy(Entity entity) const;
		bool SetEditorHidden(Entity entity, bool hidden);
		bool HasAuthoredPrimaryCamera() const;
		bool HasActiveCanvas();
		bool HasGameViewRenderSource();
		bool SetCameraPrimary(Entity entity, bool primary);

		// Starts physics and the managed scripting scene transactionally. A false
		// result means all partially-created runtime state has already been rolled
		// back and the caller must remain outside Play mode.
		bool OnRuntimeStart();
		void OnRuntimeStop();
		// Advances scripts and physics by exactly one fixed 1/60 second step,
		// independent of the most recent display-frame delta.
		void OnRuntimeStep(bool render = true);

		// Screen-space Canvas content is rendered on its reference-resolution
		// authoring plane in Scene view, observed by the EditorCamera. Runtime and
		// Game view rendering continue to map the same content to the real viewport.
		void OnUpdateEditor(Timestep ts, EditorCamera& camera);
		// Accumulates display-frame time and advances scripts and physics in fixed
		// increments. Rendering occurs once per display frame unless a headless
		// runtime explicitly disables it.
		void OnUpdateRuntime(Timestep ts, bool render = true);
		void OnRenderRuntime();
		// Physics simulation keeps authoritative current transforms in the ECS.
		// Rendering reads this matrix to interpolate between the two most recent
		// fixed poses without feeding a presentation-only pose back into physics.
		glm::mat4 GetRuntimeRenderTransform(UUID entityID) const;
		float GetRuntimeInterpolationAlpha() const { return m_RuntimeInterpolationAlpha; }
		void OnViewportResize(uint32_t width, uint32_t height);
		uint32_t GetViewportWidth() const { return m_ViewportWidth; }
		uint32_t GetViewportHeight() const { return m_ViewportHeight; }
		void SetRuntimeUIViewportMetrics(const glm::vec2& screenOrigin,
			float dpiScale,
			const glm::vec2& screenToFramebufferScale = glm::vec2(1.0f));
		bool IsRuntimeRunning() const { return m_RuntimeRunning; }
		// Prefab commits enqueue a complete UUID batch. Delivery occurs only after
		// managed callbacks / Box2D locked regions have returned to a Scene safe
		// point, allowing ScriptEngine to attach dynamic managed instances safely.
		void SetRuntimeEntityBatchCreatedCallback(
			RuntimeEntityBatchCreatedCallback callback);
		void QueueRuntimeEntityBatchCreated(std::vector<UUID> entityIDs);
		void FlushPendingRuntimeEntityCreates();
		size_t GetPendingRuntimeEntityCreateCount() const
		{
			return m_PendingRuntimeEntityCreates.size();
		}
		void SetPhysics2DSettings(const Physics2DSettings& settings);
		const Physics2DSettings& GetPhysics2DSettings() const { return m_Physics2DSettings; }
		const RuntimePhysicsSyncStatistics& GetRuntimePhysicsSyncStatistics() const
		{
			return m_RuntimePhysicsSyncStatistics;
		}
		void ResetRuntimePhysicsSyncStatistics()
		{
			m_RuntimePhysicsSyncStatistics = {};
		}

		CollisionListenerHandle AddCollisionEnter2DListener(CollisionEnter2DCallback callback);
		CollisionListenerHandle AddCollisionExit2DListener(CollisionExit2DCallback callback);
		CollisionListenerHandle AddTriggerEnter2DListener(TriggerEnter2DCallback callback);
		CollisionListenerHandle AddTriggerExit2DListener(TriggerExit2DCallback callback);
		bool RemoveCollision2DListener(CollisionListenerHandle handle);

		// layerMask addresses EntityMetadata::Layer slots (bit N selects layer N).
		std::optional<RaycastHit2D> Raycast2D(const glm::vec2& start, const glm::vec2& end,
			uint16_t layerMask = 0xFFFF, bool includeTriggers = true);
		// Returns at most one result per entity from Box2D's broad-phase AABB query;
		// a solid fixture is preferred when an entity also has a sensor. Rotated
		// fixture bounds can be conservative. layerMask addresses
		// EntityMetadata::Layer slots (bit N selects layer N).
		std::vector<PhysicsQueryHit2D> QueryAABB2D(const glm::vec2& lowerBound,
			const glm::vec2& upperBound, uint16_t layerMask = 0xFFFF,
			bool includeTriggers = true);

		bool ApplyForce2D(UUID entityID, const glm::vec2& force, bool wake = true);
		bool ApplyForceAtPoint2D(UUID entityID, const glm::vec2& force,
			const glm::vec2& worldPoint, bool wake = true);
		bool ApplyLinearImpulse2D(UUID entityID, const glm::vec2& impulse, bool wake = true);
		bool ApplyLinearImpulseAtPoint2D(UUID entityID, const glm::vec2& impulse,
			const glm::vec2& worldPoint, bool wake = true);
		bool SetLinearVelocity2D(UUID entityID, const glm::vec2& velocity);
		std::optional<glm::vec2> GetLinearVelocity2D(UUID entityID);

		// Edit mode derives outlines from authoring components. Runtime mode reads
		// the actual Box2D fixtures, making this suitable for exact debug overlays.
		std::vector<ColliderDebugShape> GetColliderDebugShapes(bool useRuntimeFixtures) const;

		Entity DuplicateEntity(Entity entity);

		Entity GetPrimaryCameraEntity();
		Entity FindEntityByUUID(UUID uuid);
		std::vector<AssetReference> FindAssetReferences(AssetHandle handle);
	private:
		std::string MakeUniqueEntityName(const std::string& requestedName) const;
		bool ValidateTransformHierarchy();
		bool RunFixedRuntimeStep();
		bool SynchronizeRuntimePhysicsDefinitions(
			bool observeDirectComponentWrites = false);
		void InvalidateRuntimePhysicsDefinitionScan();
		void FlushPendingRuntimeEntityCreatesAtSafePoint();
		uint64_t ComputeRuntimePhysicsDefinitionHash() const;
		uint64_t ComputeRuntimePhysicsSettingsHash() const;
		uint64_t ComputeRuntimeBodyDefinitionHash(UUID entityID) const;
		struct RuntimePhysicsDefinitions
		{
			uint64_t SettingsHash = 0;
			std::unordered_map<UUID, uint64_t> Bodies;
			std::unordered_map<UUID, uint64_t> BoxFixtures;
			std::unordered_map<UUID, uint64_t> CircleFixtures;
			std::unordered_map<UUID, uint64_t> DistanceJoints;
		};
		RuntimePhysicsDefinitions BuildRuntimePhysicsDefinitions() const;
		bool RebuildRuntimePhysicsWorld(bool preserveState);
		void ResetRuntimePhysicsPointers();
		void ArmRuntimeScriptBatchCallback();
		b2Body* FindRuntimeBody(UUID entityID) const;
		void SynchronizeRuntimeTransforms();
		bool DispatchPendingCollisionEvents();
		void RenderRuntimeScene();
		template<typename T>
		void OnComponentAdded(Entity entity, T& component);
	private:
		entt::registry m_Registry;
		std::string m_SceneName = "Untitled";
		uint32_t m_ViewportWidth = 0, m_ViewportHeight = 0;
		glm::vec2 m_RuntimeUIViewportOrigin{ 0.0f };
		float m_RuntimeUIDPIScale = 1.0f;
		glm::vec2 m_RuntimeUIScreenToFramebufferScale{ 1.0f };

		b2World* m_PhysicsWorld = nullptr;
		SceneContactFilter2D* m_ContactFilter = nullptr;
		SceneContactListener* m_ContactListener = nullptr;
		bool m_RuntimeRunning = false;
		double m_RuntimeAccumulator = 0.0;
		float m_RuntimeInterpolationAlpha = 1.0f;
		uint64_t m_RuntimeSessionGeneration = 0;
		uint64_t m_ScriptSceneSessionID = 0;
		std::vector<UUID> m_EntitiesBeingDestroyed;
		CollisionListenerHandle m_NextCollisionListenerHandle = 1;
		std::unordered_map<CollisionListenerHandle, CollisionEnter2DCallback> m_CollisionEnterListeners;
		std::unordered_map<CollisionListenerHandle, CollisionExit2DCallback> m_CollisionExitListeners;
		std::unordered_map<CollisionListenerHandle, TriggerEnter2DCallback> m_TriggerEnterListeners;
		std::unordered_map<CollisionListenerHandle, TriggerExit2DCallback> m_TriggerExitListeners;
		std::unordered_map<UUID, b2Body*> m_RuntimeBodies;
		std::unordered_map<UUID, b2Fixture*> m_RuntimeBoxFixtures;
		std::unordered_map<UUID, b2Fixture*> m_RuntimeCircleFixtures;
		std::unordered_map<UUID, b2Joint*> m_RuntimeDistanceJoints;
		RuntimePhysicsDefinitions m_RuntimePhysicsDefinitions;
		struct RuntimePhysicsPose
		{
			glm::vec2 PreviousPosition{ 0.0f };
			glm::vec2 CurrentPosition{ 0.0f };
			float PreviousAngle = 0.0f;
			float CurrentAngle = 0.0f;
		};
		std::unordered_map<UUID, RuntimePhysicsPose> m_RuntimePhysicsPoses;
		struct SuspendedRuntimeBodyState
		{
			float LinearVelocityX = 0.0f;
			float LinearVelocityY = 0.0f;
			float AngularVelocity = 0.0f;
			bool Awake = true;
		};
		std::unordered_map<UUID, SuspendedRuntimeBodyState>
			m_SuspendedRuntimeBodyStates;
		uint64_t m_RuntimePhysicsDefinitionHash = 0;
		bool m_HasRuntimePhysicsDefinition = false;
		bool m_RuntimePhysicsDefinitionScanRequired = true;
		RuntimePhysicsSyncStatistics m_RuntimePhysicsSyncStatistics;
		RuntimeEntityBatchCreatedCallback m_RuntimeEntityBatchCreatedCallback;
		std::vector<UUID> m_PendingRuntimeEntityCreates;
		bool m_FlushingRuntimeEntityCreates = false;
		Physics2DSettings m_Physics2DSettings;
		std::unordered_map<UUID, entt::entity> m_EntityMap;
		std::unordered_map<UUID, UUID> m_ParentMap;
		std::unordered_map<UUID, std::vector<UUID>> m_ChildrenMap;
		std::vector<UUID> m_EntityOrder;

		friend class Entity;
		friend class RuntimeUISystem;
		friend class SceneContactFilter2D;
		friend class SceneSerializer;
		friend class SceneHierarchyPanel;
		friend class EditorLayer;
		friend class Scripting::ScriptEngine;

	};
}
