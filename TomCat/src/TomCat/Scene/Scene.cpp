#include "tcpch.h"
#include "Scene.h"

#include "Components.h"
#include "TomCat/Renderer/Renderer2D.h"

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

	entt::entity Scene::CreateEntity()
	{
		return m_Registry.create();
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