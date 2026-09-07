#include "tcpch.h"
#include "Scene.h"

#include "Components.h"
#include "ScriptableEntity.h"
#include "TomCat/Renderer/Renderer2D.h"
#include "TomCat/Renderer/RenderCommand.h"
#include "Entity.h"

#include <glm/glm.hpp>

// Box2D
#include "box2d/b2_world.h"
#include "box2d/b2_body.h"
#include "box2d/b2_fixture.h"
#include "box2d/b2_polygon_shape.h"

namespace TomCat {

	static b2BodyType Rigidbody2DTypeToBox2DBody(Rigidbody2D::BodyType bodyType)
	{
		switch (bodyType)
		{
		case Rigidbody2D::BodyType::Static:    return b2_staticBody;
		case Rigidbody2D::BodyType::Dynamic:   return b2_dynamicBody;
		case Rigidbody2D::BodyType::Kinematic: return b2_kinematicBody;
		}

		TC_Core_Assert(false, "Unknown body type");
		return b2_staticBody;
	}



	Scene::Scene()
	{

	}

	Scene::~Scene()
	{
	}

	template<typename Component>
	static void CopyComponent(entt::registry& dst, entt::registry& src, const std::unordered_map<UUID, entt::entity>& enttMap)
	{
		auto view = src.view<Component>();
		for (auto e : view)
		{
			UUID uuid = src.get<ID>(e).id;
			TC_Core_Assert(enttMap.find(uuid) != enttMap.end());
			entt::entity dstEnttID = enttMap.at(uuid);

			auto& component = src.get<Component>(e);
			dst.emplace_or_replace<Component>(dstEnttID, component);
		}
	}

	template<typename Component>
	static void CopyComponentIfExists(Entity dst, Entity src)
	{
		if (src.HasComponent<Component>())
			dst.AddOrReplaceComponent<Component>(src.GetComponent<Component>());
	}

	Ref<Scene> Scene::Copy(Ref<Scene> other)
	{
		if (!other)
		{
			TC_Core_Assert(false, "Scene::Copy called with nullptr");
			return CreateRef<Scene>();
		}

		Ref<Scene> newScene = CreateRef<Scene>();

		newScene->m_ViewportWidth = other->m_ViewportWidth;
		newScene->m_ViewportHeight = other->m_ViewportHeight;

		auto& srcSceneRegistry = other->m_Registry;
		auto& dstSceneRegistry = newScene->m_Registry;
		std::unordered_map<UUID, entt::entity> enttMap;

		// Create entities in new scene
		auto idView = srcSceneRegistry.view<ID>();
		for (auto e : idView)
		{
			UUID uuid = srcSceneRegistry.get<ID>(e).id;
			const auto& name = srcSceneRegistry.get<Tag>(e)._Tag;
			Entity newEntity = newScene->CreateEntityWithUUID(uuid, name);
			enttMap[uuid] = (entt::entity)newEntity;
		}

		// Copy components (except IDComponent and TagComponent)
		CopyComponent<Transform>(dstSceneRegistry, srcSceneRegistry, enttMap);
		CopyComponent<Tag>(dstSceneRegistry, srcSceneRegistry, enttMap);
		CopyComponent<SpriteRenderer>(dstSceneRegistry, srcSceneRegistry, enttMap);
		CopyComponent<C_Camera>(dstSceneRegistry, srcSceneRegistry, enttMap);
		CopyComponent<NativeScript>(dstSceneRegistry, srcSceneRegistry, enttMap);
		CopyComponent<Rigidbody2D>(dstSceneRegistry, srcSceneRegistry, enttMap);
		CopyComponent<BoxCollider2D>(dstSceneRegistry, srcSceneRegistry, enttMap);

		return newScene;
	}

	Entity Scene::CreateEntity(const std::string& name)
	{
		return CreateEntityWithUUID(UUID(), name);
	}

	Entity Scene::CreateEntityWithUUID(UUID uuid, const std::string& name)
	{
		Entity entity = { m_Registry.create(), this };
		entity.AddComponent<ID>(uuid);
		entity.AddComponent<Transform>();
		auto& tag = entity.AddComponent<Tag>();
		tag._Tag = name.empty() ? "Entity" : name;
		return entity;
	}


	void Scene::DestroyEntity(Entity entity)
	{
		m_Registry.destroy(entity);
	}

	void Scene::OnRuntimeStart()
	{
		m_PhysicsWorld = new b2World({ 0.0f, -9.8f });

		auto view = m_Registry.view<Rigidbody2D>();
		for (auto e : view)
		{
			Entity entity = { e, this };
			auto& transform = entity.GetComponent<Transform>();
			auto& rb2d = entity.GetComponent<Rigidbody2D>();
			if (!rb2d.Enabled)
				continue;

			b2BodyDef bodyDef;
			bodyDef.type = Rigidbody2DTypeToBox2DBody(rb2d.Type);
			bodyDef.position.Set(transform._Translation.x, transform._Translation.y);
			bodyDef.angle = transform._Rotation.z;

			b2Body* body = m_PhysicsWorld->CreateBody(&bodyDef);
			body->SetFixedRotation(rb2d.FixedRotation);
			rb2d.RuntimeBody = body;

			if (entity.HasComponent<BoxCollider2D>() && entity.GetComponent<BoxCollider2D>().Enabled)
			{
				auto& bc2d = entity.GetComponent<BoxCollider2D>();

				b2PolygonShape boxShape;
				boxShape.SetAsBox(bc2d.Size.x * transform._Scale.x, bc2d.Size.y * transform._Scale.y);

				b2FixtureDef fixtureDef;
				fixtureDef.shape = &boxShape;
				fixtureDef.density = bc2d.Density;
				fixtureDef.friction = bc2d.Friction;
				fixtureDef.restitution = bc2d.Restitution;
				fixtureDef.restitutionThreshold = bc2d.RestitutionThreshold;
				body->CreateFixture(&fixtureDef);
			}
		}
	}

	void Scene::OnRuntimeStop()
	{
		delete m_PhysicsWorld;
		m_PhysicsWorld = nullptr;
	}



	void Scene::OnUpdateRuntime(Timestep ts)
	{
		// Set background color from primary camera
		{
			auto view = m_Registry.view<Transform, C_Camera>();
			view.each([this](auto entity, Transform& transform, C_Camera& camera) {
				if (camera.Primary && m_Registry.get<Tag>(entity).Visible)
				{
					RenderCommand::SetClearColor(camera.BackgroundColor);
					RenderCommand::Clear();
				}
			});
		}

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


		// Physics
		{
			const int32_t velocityIterations = 6;
			const int32_t positionIterations = 2;
			m_PhysicsWorld->Step(ts, velocityIterations, positionIterations);

			// Retrieve transform from Box2D
			auto view = m_Registry.view<Rigidbody2D>();
			for (auto e : view)
			{
				Entity entity = { e, this };
				auto& transform = entity.GetComponent<Transform>();
				auto& rb2d = entity.GetComponent<Rigidbody2D>();
				if (!rb2d.Enabled || !rb2d.RuntimeBody)
					continue;

				b2Body* body = (b2Body*)rb2d.RuntimeBody;
				const auto& position = body->GetPosition();
				transform._Translation.x = position.x;
				transform._Translation.y = position.y;
				transform._Rotation.z = body->GetAngle();
			}
		}


		// Sprite
		Camera* MainCamera = nullptr;
		glm::mat4 cameraTransform;

		{
			auto view = m_Registry.view<Transform, C_Camera>();

			view.each([this, &MainCamera, &cameraTransform](auto entity, Transform& transform, C_Camera& camera) {
				if (camera.Primary && m_Registry.get<Tag>(entity).Visible)
				{
					MainCamera = &camera._Camera;
					cameraTransform = transform.GetTransform();
				}
			});
		}

		if (MainCamera) 
		{
			Renderer2D::BeginScene(*MainCamera, cameraTransform);

			auto group = m_Registry.group<Transform>(entt::get<SpriteRenderer>);
			for (auto entity : group)
			{
				auto [transform, sprite] = group.get<Transform, SpriteRenderer>(entity);
				if (!m_Registry.get<Tag>(entity).Visible || !sprite.Enabled)
					continue;

				Renderer2D::DrawSprite(transform.GetTransform(), sprite, (int)entity);
			}

			Renderer2D::EndScene();
		}

	}

	void Scene::OnUpdateEditor(Timestep ts, EditorCamera& camera)
	{
		Renderer2D::BeginScene(camera);

		// SpriteRenderer
		auto group = m_Registry.group<Transform>(entt::get<SpriteRenderer>);

		group.each([this](auto entity, Transform& transform, SpriteRenderer& sprite) {
			if (!m_Registry.get<Tag>(entity).Visible || !sprite.Enabled)
				return;

			Renderer2D::DrawSprite(transform.GetTransform(), sprite, (int)entity);

			});

		Renderer2D::EndScene();
	}

	void Scene::OnRenderRuntime()
	{
		// Set background color from primary camera
		{
			auto view = m_Registry.view<Transform, C_Camera>();
			view.each([this](auto entity, Transform& transform, C_Camera& camera) {
				if (camera.Primary && m_Registry.get<Tag>(entity).Visible)
				{
					RenderCommand::SetClearColor(camera.BackgroundColor);
					RenderCommand::Clear();
				}
			});
		}

		// Sprite
		Camera* MainCamera = nullptr;
		glm::mat4 cameraTransform;

		{
			auto view = m_Registry.view<Transform, C_Camera>();

			view.each([this, &MainCamera, &cameraTransform](auto entity, Transform& transform, C_Camera& camera) {
				if (camera.Primary && m_Registry.get<Tag>(entity).Visible)
				{
					MainCamera = &camera._Camera;
					cameraTransform = transform.GetTransform();
				}
			});
		}

		if (MainCamera)
		{
			Renderer2D::BeginScene(*MainCamera, cameraTransform);

			auto group = m_Registry.group<Transform>(entt::get<SpriteRenderer>);
			for (auto entity : group)
			{
				auto [transform, sprite] = group.get<Transform, SpriteRenderer>(entity);
				if (!m_Registry.get<Tag>(entity).Visible || !sprite.Enabled)
					continue;

				Renderer2D::DrawSprite(transform.GetTransform(), sprite, (int)entity);
			}

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

	Entity Scene::DuplicateEntity(Entity entity)
	{
		std::string name = entity.GetName();
		Entity newEntity = CreateEntity(name);

		CopyComponentIfExists<Transform>(newEntity, entity);
		CopyComponentIfExists<SpriteRenderer>(newEntity, entity);
		CopyComponentIfExists<C_Camera>(newEntity, entity);
		CopyComponentIfExists<NativeScript>(newEntity, entity);
		CopyComponentIfExists<Rigidbody2D>(newEntity, entity);
		CopyComponentIfExists<BoxCollider2D>(newEntity, entity);
		return newEntity;
	}


	Entity Scene::GetPrimaryCameraEntity()
	{
		auto view = m_Registry.view<C_Camera>();

		for (auto entity:view)
		{
			const auto& camera = view.get<C_Camera>(entity);

			if (camera.Primary && m_Registry.get<Tag>(entity).Visible)
				return Entity(entity,this);

		}

		return {};
	}

	Entity Scene::FindEntityByUUID(UUID uuid)
	{
		auto view = m_Registry.view<ID>();
		for (auto entity : view)
		{
			if (view.get<ID>(entity).id == uuid)
				return Entity(entity, this);
		}
		return {};
	}

	template<typename T>
	void Scene::OnComponentAdded(Entity entity, T& component)
	{
		//static_assert(false);
	}

	template<>
	void Scene::OnComponentAdded<ID>(Entity entity, ID& component)
	{
	}

	template<>
	void Scene::OnComponentAdded<Transform>(Entity entity, Transform& component)
	{
	}

	template<>
	void Scene::OnComponentAdded<C_Camera>(Entity entity, C_Camera& component)
	{
		if (m_ViewportWidth > 0 && m_ViewportHeight > 0)
			component._Camera.SetViewportSize(m_ViewportWidth, m_ViewportHeight);
	}

	template<>
	void Scene::OnComponentAdded<SpriteRenderer>(Entity entity, SpriteRenderer& component)
	{
	}

	template<>
	void Scene::OnComponentAdded<Tag>(Entity entity, Tag& component)
	{
	}

	template<>
	void Scene::OnComponentAdded<NativeScript>(Entity entity, NativeScript& component)
	{
	}

	template<>
	void Scene::OnComponentAdded<Rigidbody2D>(Entity entity, Rigidbody2D& component)
	{
	}

	template<>
	void Scene::OnComponentAdded<BoxCollider2D>(Entity entity, BoxCollider2D& component)
	{
	}
}
