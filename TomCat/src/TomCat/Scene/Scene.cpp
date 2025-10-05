#include "tcpch.h"
#include "Scene.h"

#include "Components.h"
#include "TomCat/Renderer/Renderer2D.h"
#include "Entity.h"

#include <glm/glm.hpp>

namespace TomCat {

	static void DoMath(const glm::mat4& transform)
	{

	}

	static void OnTransformConstruct(entt::registry& registry, entt::entity entity)
	{

	}

	Scene::Scene()
	{
#if ENTT_EXAMPLE_CODE
		entt::entity entity = m_Registry.create();
		m_Registry.emplace<Transform>(entity, glm::mat4(1.0f));

		m_Registry.on_construct<Transform>().connect<&OnTransformConstruct>();


		if (m_Registry.has<Transform>(entity))
			Transform& transform = m_Registry.get<Transform>(entity);


		auto view = m_Registry.view<Transform>();
		for (auto entity : view)
		{
			Transform& transform = view.get<Transform>(entity);
		}

		auto group = m_Registry.group<Transform>(entt::get<MeshComponent>);
		for (auto entity : group)
		{
			auto& [transform, mesh] = group.get<Transform, MeshComponent>(entity);
		}
#endif
	}

	Scene::~Scene()
	{
	}

	Entity Scene::CreateEntity(const std::string& name)
	{

		Entity entity = { m_Registry.create(),this };
		entity.AddComponent<Transform>();
		auto& tag = entity.AddComponent<Tag>();
		tag._Tag = name.empty() ? "Enitity" : name;
		return entity;
	}

	void Scene::OnUpdate(Timestep ts)
	{
		auto group = m_Registry.group<Transform>(entt::get<SpriteRenderer>);

		for (auto entity : group)
		{
			const auto& [transform, sprite] = group.get<Transform, SpriteRenderer>(entity);

			Renderer2D::DrawQuad(transform, sprite.Color);
		}


	}

}