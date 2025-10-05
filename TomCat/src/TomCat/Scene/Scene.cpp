#include "tcpch.h"
#include "Scene.h"

#include "Components.h"
#include "TomCat/Renderer/Renderer2D.h"
#include "Entity.h"

#include <glm/glm.hpp>

namespace TomCat {

	Scene::Scene()
	{

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

		{
			m_Registry.view<NativeScript>().each([=](auto entity, auto& nsc)
				{
					if (!nsc.Instance)
					{
						nsc.Instance = nsc.InstantiateScript();
						nsc.Instance->m_Entity = Entity{ entity, this };
						nsc.Instance->OnCreate();
					}

					nsc.Instance->OnUpdate(ts);
				});
		}


		// Sprite
		Camera* MainCamera = nullptr;
		glm::mat4* cameraTransform = nullptr;

		{
			auto view = m_Registry.view<Transform, C_Camera>();

			view.each([this, &MainCamera, &cameraTransform](auto entity, Transform& transform, C_Camera& camera) {
				if (camera.Primary)
				{
					MainCamera = &camera._Camera;
					cameraTransform = &transform._Transform;
				}
			});
		}

		if (MainCamera) 
		{

			Renderer2D::BeginScene(MainCamera->GetProjection(), *cameraTransform);

			// SpriteRenderer
			auto group = m_Registry.group<Transform>(entt::get<SpriteRenderer>);

			group.each([this](auto entity, Transform& transform, SpriteRenderer& sprite) {

				Renderer2D::DrawQuad(transform, sprite._Color);

			});

			Renderer2D::EndScene();
		}

	}

	void Scene::OnViewportResize(uint32_t width, uint32_t height)
	{
		m_ViewportWidth = width;
		m_ViewportHeight = height;

		// Resize our non-FixedAspectRatio cameras
		auto view = m_Registry.view<C_Camera>();
		for (auto entity : view)
		{
			auto& camera = view.get<C_Camera>(entity);
			if (!camera.FixedAspectRatio)
				camera._Camera.SetViewportSize(width, height);
		}

	}
}