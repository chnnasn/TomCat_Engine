#pragma once
#include "TomCat/Core/UUID.h"
#include "Scene.h"
#include "Components.h"
#include"entt.hpp"

namespace TomCat {

	class Entity
	{

	public:
		Entity() = default;

		Entity(entt::entity handle,Scene* scene);
		Entity(const Entity& other) = default;

		template<typename T, typename... Args>
		T& AddComponent(Args&&... args)
		{
			T& component = m_Scene->m_Registry.emplace<T>(m_EntityHandle, std::forward<Args>(args)...);
			m_Scene->OnComponentAdded<T>(*this, component);
			return component;
		}

		template<typename T, typename... Args>
		T& AddOrReplaceComponent(Args&&... args)
		{
			T& component = m_Scene->m_Registry.emplace_or_replace<T>(m_EntityHandle, std::forward<Args>(args)...);
			m_Scene->OnComponentAdded<T>(*this, component);
			return component;
		}


		template<typename T>
		T& GetComponent()
		{
			return m_Scene->m_Registry.get<T>(m_EntityHandle);
		}

		template<typename T>
		const T& GetComponent() const
		{
			return m_Scene->m_Registry.get<T>(m_EntityHandle);
		}

		template<typename T>
		bool HasComponent()
		{
			return m_Scene->m_Registry.all_of<T>(m_EntityHandle);
		}

		template<typename T>
		bool HasComponent() const
		{
			return m_Scene->m_Registry.all_of<T>(m_EntityHandle);
		}

		template<typename T>
		void RemoveComponent()
		{
			if (!m_Scene || !m_Scene->m_Registry.valid(m_EntityHandle)
				|| !m_Scene->m_Registry.all_of<T>(m_EntityHandle))
				return;
			m_Scene->m_Registry.remove<T>(m_EntityHandle);
		}

		operator bool() const { return m_EntityHandle != entt::null; }

		operator entt::entity() const { return m_EntityHandle; }

		operator uint32_t() const { return (uint32_t)m_EntityHandle; }

		UUID GetUUID() const { return GetComponent<ID>().id; }

		const std::string& GetName() const { return GetComponent<Tag>()._Tag; }

		const std::string& GetGameplayTag() const
		{
			return GetComponent<EntityMetadata>().GameplayTag;
		}

		bool SetGameplayTag(const std::string& gameplayTag)
		{
			if (gameplayTag.empty())
				return false;
			GetComponent<EntityMetadata>().GameplayTag = gameplayTag;
			return true;
		}

		uint8_t GetLayer() const { return GetComponent<EntityMetadata>().Layer; }

		bool IsActiveInHierarchy() const
		{
			return m_Scene && m_Scene->IsActiveInHierarchy(*this);
		}

		bool SetLayer(uint8_t layer)
		{
			if (layer >= Physics2DLayerCount)
				return false;
			GetComponent<EntityMetadata>().Layer = layer;
			return true;
		}

		bool operator==(const Entity& other) const
		{
			return m_EntityHandle == other.m_EntityHandle && m_Scene == other.m_Scene;
		}

		bool operator!=(const Entity& other) const
		{
			return !(*this == other);
		}
	private:
		entt::entity m_EntityHandle{ entt::null };
		Scene* m_Scene = nullptr;

		friend class Scene;

	};

	// The default hook is header-defined so a newly registered component can use
	// Entity::AddComponent without adding a Scene.cpp specialization. Built-in
	// components with subsystem side effects keep their explicit specializations.
	template<typename T>
	void Scene::OnComponentAdded(Entity, T&)
	{
	}

	template<> void Scene::OnComponentAdded<ID>(Entity, ID&);
	template<> void Scene::OnComponentAdded<Transform>(Entity, Transform&);
	template<> void Scene::OnComponentAdded<C_Camera>(Entity, C_Camera&);
	template<> void Scene::OnComponentAdded<SpriteRenderer>(Entity, SpriteRenderer&);
	template<> void Scene::OnComponentAdded<SpriteAnimator>(Entity, SpriteAnimator&);
	template<> void Scene::OnComponentAdded<LineRenderer>(Entity, LineRenderer&);
	template<> void Scene::OnComponentAdded<Tag>(Entity, Tag&);
	template<> void Scene::OnComponentAdded<EntityMetadata>(Entity, EntityMetadata&);
	template<> void Scene::OnComponentAdded<CSharpScripts>(Entity, CSharpScripts&);
	template<> void Scene::OnComponentAdded<Rigidbody2D>(Entity, Rigidbody2D&);
	template<> void Scene::OnComponentAdded<BoxCollider2D>(Entity, BoxCollider2D&);
	template<> void Scene::OnComponentAdded<CircleCollider2D>(Entity, CircleCollider2D&);
	template<> void Scene::OnComponentAdded<DistanceJoint2D>(Entity, DistanceJoint2D&);

}
