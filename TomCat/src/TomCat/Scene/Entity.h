#pragma once
#include "TomCat/Core/UUID.h"
#include "Scene.h"
#include "Components.h"
#include"entt.hpp"

#include <type_traits>

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
			if constexpr (std::is_same_v<T, NativeScript>)
			{
				if (m_Scene->m_Registry.all_of<T>(m_EntityHandle))
					m_Scene->QueueNativeScriptInstanceDestruction(
						m_Scene->m_Registry.get<T>(m_EntityHandle), "component replacement");
			}
			T& component = m_Scene->m_Registry.emplace_or_replace<T>(m_EntityHandle, std::forward<Args>(args)...);
			m_Scene->OnComponentAdded<T>(*this, component);
			if constexpr (std::is_same_v<T, NativeScript>)
				m_Scene->FlushDeferredNativeScriptMutations();
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
			if constexpr (std::is_same_v<T, NativeScript>)
				m_Scene->QueueNativeScriptInstanceDestruction(
					m_Scene->m_Registry.get<T>(m_EntityHandle), "component removal");
			m_Scene->m_Registry.remove<T>(m_EntityHandle);
			if constexpr (std::is_same_v<T, NativeScript>)
				m_Scene->FlushDeferredNativeScriptMutations();
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

}
