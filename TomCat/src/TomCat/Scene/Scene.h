#pragma once
#include "entt.hpp"
#include "TomCat/Core/Timestep.h"
#include "TomCat/Core/UUID.h"
#include "TomCat/Renderer/EditorCamera.h"

#include <optional>
#include <string>
#include <unordered_map>
#include <vector>


class b2World;

namespace TomCat {

	class Entity;

	class Scene
	{
	public:
		Scene();
		~Scene();

		static Ref<Scene> Copy(Ref<Scene> other);

		const std::string& GetSceneName() const { return m_SceneName; }
		void SetSceneName(const std::string& sceneName) { m_SceneName = sceneName.empty() ? "Untitled" : sceneName; }

		Entity CreateEntity(const std::string& name = std::string());
		Entity CreateEntityWithUUID(UUID uuid, const std::string& name = std::string());
		void DestroyEntity(Entity entity);
		void SetParent(Entity child, Entity parent);
		void SetWorldTransform(Entity entity, const glm::mat4& worldTransform);
		void SetLocalTransform(Entity entity, const glm::mat4& localTransform);
		void SyncTransformHierarchy();
		Entity GetParent(Entity entity);
		std::vector<UUID> GetChildrenUUIDs(Entity entity);
		std::vector<UUID> GetRootEntityUUIDs();

		void OnRuntimeStart();
		void OnRuntimeStop();


		void OnUpdateEditor(Timestep ts,EditorCamera& camera);
		void OnUpdateRuntime(Timestep ts);
		void OnRenderRuntime();
		void OnViewportResize(uint32_t width, uint32_t height);

		Entity DuplicateEntity(Entity entity);

		Entity GetPrimaryCameraEntity();
		Entity FindEntityByUUID(UUID uuid);
	private:
		std::string MakeUniqueEntityName(const std::string& requestedName) const;
		template<typename T>
		void OnComponentAdded(Entity entity, T& component);
		void SyncTransformHierarchyRecursive(Entity entity);
		void SyncTransformHierarchyRecursive(Entity entity, const glm::mat4& parentWorldTransform);
	private:
		entt::registry m_Registry;
		std::string m_SceneName = "Untitled";
		uint32_t m_ViewportWidth = 0, m_ViewportHeight = 0;

		b2World* m_PhysicsWorld = nullptr;
		std::unordered_map<UUID, UUID> m_ParentMap;
		std::unordered_map<UUID, std::vector<UUID>> m_ChildrenMap;
		std::vector<UUID> m_EntityOrder;

		friend class Entity;
		friend class SceneSerializer;
		friend class SceneHierarchyPanel;

	};
}
