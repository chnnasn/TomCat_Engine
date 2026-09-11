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
#include <string>
#include <unordered_map>
#include <vector>


class b2World;
class b2Body;

namespace TomCat {

	class Entity;
	class SceneContactFilter2D;
	class SceneContactListener;
	class ScriptableEntity;
	struct NativeScript;

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

	class Scene
	{
	public:
		using CollisionListenerHandle = uint64_t;
		using CollisionEnter2DCallback = std::function<void(const CollisionEnter2D&)>;
		using CollisionExit2DCallback = std::function<void(const CollisionExit2D&)>;
		using TriggerEnter2DCallback = std::function<void(const TriggerEnter2D&)>;
		using TriggerExit2DCallback = std::function<void(const TriggerExit2D&)>;

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

		void OnRuntimeStart();
		void OnRuntimeStop();
		// Advances scripts and physics by exactly one fixed 1/60 second step,
		// independent of the most recent display-frame delta.
		void OnRuntimeStep();

		void OnUpdateEditor(Timestep ts,EditorCamera& camera);
		// Accumulates display-frame time and advances scripts and physics in fixed
		// increments. Rendering still occurs once per display frame.
		void OnUpdateRuntime(Timestep ts);
		void OnRenderRuntime();
		void OnViewportResize(uint32_t width, uint32_t height);
		bool IsRuntimeRunning() const { return m_RuntimeRunning; }
		void SetPhysics2DSettings(const Physics2DSettings& settings);
		const Physics2DSettings& GetPhysics2DSettings() const { return m_Physics2DSettings; }

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
		struct DeferredNativeScriptDestruction
		{
			ScriptableEntity* Instance = nullptr;
			ScriptableEntity* (*InstantiateScript)() = nullptr;
			void (*DestroyScript)(NativeScript*) = nullptr;
			std::string Context;
		};

		std::string MakeUniqueEntityName(const std::string& requestedName) const;
		bool ValidateTransformHierarchy();
		void QueueNativeScriptInstanceDestruction(NativeScript& script, const char* context);
		void DestroyNativeScriptInstance(NativeScript& script, const char* context) noexcept;
		void DestroyDetachedNativeScriptInstance(
			const DeferredNativeScriptDestruction& script) noexcept;
		void FlushDeferredNativeScriptMutations();
		bool RunFixedRuntimeStep();
		void UpdateRuntimeScripts(Timestep fixedTimestep);
		bool SynchronizeRuntimePhysicsDefinitions();
		uint64_t ComputeRuntimePhysicsDefinitionHash() const;
		bool RebuildRuntimePhysicsWorld(bool preserveState);
		void ResetRuntimePhysicsPointers();
		b2Body* FindRuntimeBody(UUID entityID) const;
		void SynchronizeRuntimeTransforms();
		void DispatchPendingCollisionEvents();
		void RenderRuntimeScene();
		template<typename T>
		void OnComponentAdded(Entity entity, T& component);
	private:
		entt::registry m_Registry;
		std::string m_SceneName = "Untitled";
		uint32_t m_ViewportWidth = 0, m_ViewportHeight = 0;

		b2World* m_PhysicsWorld = nullptr;
		SceneContactFilter2D* m_ContactFilter = nullptr;
		SceneContactListener* m_ContactListener = nullptr;
		bool m_RuntimeRunning = false;
		double m_RuntimeAccumulator = 0.0;
		uint64_t m_RuntimeSessionGeneration = 0;
		uint32_t m_NativeCollisionCallbackDepth = 0;
		bool m_FlushingNativeScriptMutations = false;
		std::vector<DeferredNativeScriptDestruction> m_DeferredNativeScriptDestructions;
		std::vector<UUID> m_DeferredEntityDestructions;
		std::vector<UUID> m_EntitiesBeingDestroyed;
		CollisionListenerHandle m_NextCollisionListenerHandle = 1;
		std::unordered_map<CollisionListenerHandle, CollisionEnter2DCallback> m_CollisionEnterListeners;
		std::unordered_map<CollisionListenerHandle, CollisionExit2DCallback> m_CollisionExitListeners;
		std::unordered_map<CollisionListenerHandle, TriggerEnter2DCallback> m_TriggerEnterListeners;
		std::unordered_map<CollisionListenerHandle, TriggerExit2DCallback> m_TriggerExitListeners;
		std::unordered_map<UUID, b2Body*> m_RuntimeBodies;
		uint64_t m_RuntimePhysicsDefinitionHash = 0;
		bool m_HasRuntimePhysicsDefinition = false;
		Physics2DSettings m_Physics2DSettings;
		std::unordered_map<UUID, entt::entity> m_EntityMap;
		std::unordered_map<UUID, UUID> m_ParentMap;
		std::unordered_map<UUID, std::vector<UUID>> m_ChildrenMap;
		std::vector<UUID> m_EntityOrder;

		friend class Entity;
		friend class SceneContactFilter2D;
		friend class SceneSerializer;
		friend class SceneHierarchyPanel;

	};
}
