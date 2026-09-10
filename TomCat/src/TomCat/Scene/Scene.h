#pragma once
#include "entt.hpp"
#include "TomCat/Core/Timestep.h"
#include "TomCat/Core/UUID.h"
#include "TomCat/Asset/Asset.h"
#include "TomCat/Renderer/EditorCamera.h"

#include <optional>
#include <string>
#include <unordered_map>
#include <vector>


class b2World;

namespace TomCat {

	class Entity;
	struct NativeScript;

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
		bool RenameEntity(Entity entity, const std::string& requestedName);
		void DestroyEntity(Entity entity);
		bool SetParent(Entity child, Entity parent);
		bool SetWorldTransform(Entity entity, const glm::mat4& worldTransform);
		bool SetLocalTransform(Entity entity, const glm::mat4& localTransform);
		bool SyncTransformHierarchy();
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
		std::vector<AssetReference> FindAssetReferences(AssetHandle handle);
	private:
		std::string MakeUniqueEntityName(const std::string& requestedName) const;
		bool ValidateTransformHierarchy();
		static void DestroyNativeScriptInstance(NativeScript& script, const char* context) noexcept;
		template<typename T>
		void OnComponentAdded(Entity entity, T& component);
	private:
		entt::registry m_Registry;
		std::string m_SceneName = "Untitled";
		uint32_t m_ViewportWidth = 0, m_ViewportHeight = 0;

		b2World* m_PhysicsWorld = nullptr;
		bool m_RuntimeRunning = false;
		std::unordered_map<UUID, entt::entity> m_EntityMap;
		std::unordered_map<UUID, UUID> m_ParentMap;
		std::unordered_map<UUID, std::vector<UUID>> m_ChildrenMap;
		std::vector<UUID> m_EntityOrder;

		friend class Entity;
		friend class SceneSerializer;
		friend class SceneHierarchyPanel;

	};
}
