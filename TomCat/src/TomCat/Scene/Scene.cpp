#include "tcpch.h"
#include "Scene.h"

#include "Components.h"
#include "ScriptableEntity.h"
#include "TomCat/Renderer/Renderer2D.h"
#include "TomCat/Renderer/RenderCommand.h"
#include "TomCat/Math/Math.h"
#include "Entity.h"

#include <algorithm>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_inverse.hpp>

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

	static void CopyEntityComponents(Entity dst, Entity src)
	{
		CopyComponentIfExists<Tag>(dst, src);
		CopyComponentIfExists<Transform>(dst, src);
		CopyComponentIfExists<SpriteRenderer>(dst, src);
		CopyComponentIfExists<C_Camera>(dst, src);
		CopyComponentIfExists<NativeScript>(dst, src);
		CopyComponentIfExists<Rigidbody2D>(dst, src);
		CopyComponentIfExists<BoxCollider2D>(dst, src);
	}

	static Entity DuplicateEntityRecursive(Scene* scene, Entity source, Entity parent)
	{
		if (!scene || !source)
			return {};

		Entity duplicate = scene->CreateEntity(source.GetName());
		CopyEntityComponents(duplicate, source);

		if (parent)
			scene->SetParent(duplicate, parent);

		for (UUID childUUID : scene->GetChildrenUUIDs(source))
		{
			Entity child = scene->FindEntityByUUID(childUUID);
			if (child)
				DuplicateEntityRecursive(scene, child, duplicate);
		}

		return duplicate;
	}

	Ref<Scene> Scene::Copy(Ref<Scene> other)
	{
		if (!other)
		{
			TC_Core_Assert(false, "Scene::Copy called with nullptr");
			return CreateRef<Scene>();
		}

		Ref<Scene> newScene = CreateRef<Scene>();

		newScene->m_SceneName = other->m_SceneName;
		newScene->m_ViewportWidth = other->m_ViewportWidth;
		newScene->m_ViewportHeight = other->m_ViewportHeight;

		auto& srcSceneRegistry = other->m_Registry;
		auto& dstSceneRegistry = newScene->m_Registry;
		std::unordered_map<UUID, entt::entity> enttMap;

		// Create entities in their original creation order so the hierarchy keeps
		// newly created items at the bottom after a scene copy.
		for (UUID uuid : other->m_EntityOrder)
		{
			Entity sourceEntity = other->FindEntityByUUID(uuid);
			if (!sourceEntity)
				continue;
			const auto& name = sourceEntity.GetName();
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

		for (const auto& [childUUID, parentUUID] : other->m_ParentMap)
		{
			auto childIt = enttMap.find(childUUID);
			auto parentIt = enttMap.find(parentUUID);
			if (childIt == enttMap.end() || parentIt == enttMap.end())
				continue;

			Entity childEntity = { childIt->second, newScene.get() };
			Entity parentEntity = { parentIt->second, newScene.get() };
			newScene->SetParent(childEntity, parentEntity);
		}

		return newScene;
	}

	Entity Scene::CreateEntity(const std::string& name)
	{
		const std::string baseName = name.empty() ? "Entity" : name;
		return CreateEntityWithUUID(UUID(), MakeUniqueEntityName(baseName));
	}

	Entity Scene::CreateEntityWithUUID(UUID uuid, const std::string& name)
	{
		Entity entity = { m_Registry.create(), this };
		entity.AddComponent<ID>(uuid);
		entity.AddComponent<Transform>();
		auto& tag = entity.AddComponent<Tag>();
		tag._Tag = name.empty() ? "Entity" : name;
		m_EntityOrder.push_back(uuid);
		return entity;
	}

	std::string Scene::MakeUniqueEntityName(const std::string& requestedName) const
	{
		const std::string baseName = requestedName.empty() ? "Entity" : requestedName;
		auto nameExists = [this](const std::string& candidate)
		{
			auto view = m_Registry.view<Tag>();
			for (auto entityID : view)
			{
				if (view.get<Tag>(entityID)._Tag == candidate)
					return true;
			}
			return false;
		};

		if (!nameExists(baseName))
			return baseName;

		for (uint32_t suffix = 1; ; ++suffix)
		{
			std::string candidate = baseName + " (" + std::to_string(suffix) + ")";
			if (!nameExists(candidate))
				return candidate;
		}
	}

	void Scene::SetWorldTransform(Entity entity, const glm::mat4& worldTransform)
	{
		if (!entity || !m_Registry.valid(entity) || !entity.HasComponent<Transform>())
			return;

		auto& transform = entity.GetComponent<Transform>();
		const Entity parent = GetParent(entity);
		const glm::mat4 parentWorld = parent ? parent.GetComponent<Transform>().GetTransform() : glm::mat4(1.0f);
		const glm::mat4 localTransform = glm::inverse(parentWorld) * worldTransform;

		transform.SetTransform(worldTransform);
		transform.SetLocalTransform(localTransform);

		SyncTransformHierarchyRecursive(entity, parentWorld);
	}

	void Scene::SetLocalTransform(Entity entity, const glm::mat4& localTransform)
	{
		if (!entity || !m_Registry.valid(entity) || !entity.HasComponent<Transform>())
			return;

		auto& transform = entity.GetComponent<Transform>();
		const Entity parent = GetParent(entity);
		const glm::mat4 parentWorld = parent ? parent.GetComponent<Transform>().GetTransform() : glm::mat4(1.0f);
		const glm::mat4 worldTransform = parentWorld * localTransform;

		transform.SetLocalTransform(localTransform);
		transform.SetTransform(worldTransform);

		SyncTransformHierarchyRecursive(entity, parentWorld);
	}

	void Scene::SyncTransformHierarchy()
	{
		for (UUID rootUUID : GetRootEntityUUIDs())
		{
			Entity root = FindEntityByUUID(rootUUID);
			if (root)
				SyncTransformHierarchyRecursive(root, glm::mat4(1.0f));
		}
	}

	void Scene::SyncTransformHierarchyRecursive(Entity entity)
	{
		SyncTransformHierarchyRecursive(entity, glm::mat4(1.0f));
	}

	void Scene::SyncTransformHierarchyRecursive(Entity entity, const glm::mat4& parentWorldTransform)
	{
		if (!entity || !m_Registry.valid(entity) || !entity.HasComponent<Transform>())
			return;

		auto& transform = entity.GetComponent<Transform>();
		const glm::mat4 worldTransform = parentWorldTransform * transform.GetLocalTransform();
		transform.SetTransform(worldTransform);

		for (UUID childUUID : GetChildrenUUIDs(entity))
		{
			Entity child = FindEntityByUUID(childUUID);
			if (child)
				SyncTransformHierarchyRecursive(child, worldTransform);
		}
	}


	void Scene::DestroyEntity(Entity entity)
	{
		if (!entity || !m_Registry.valid(entity))
			return;

		const UUID entityUUID = entity.GetUUID();
		auto children = GetChildrenUUIDs(entity);
		for (UUID childUUID : children)
		{
			Entity child = FindEntityByUUID(childUUID);
			if (child)
				DestroyEntity(child);
		}

		SetParent(entity, Entity{});
		m_ChildrenMap.erase(entityUUID);
		m_EntityOrder.erase(std::remove(m_EntityOrder.begin(), m_EntityOrder.end(), entityUUID), m_EntityOrder.end());
		m_Registry.destroy(entity);
	}

	void Scene::SetParent(Entity child, Entity parent)
	{
		if (!child || !m_Registry.valid(child))
			return;

		const UUID childUUID = child.GetUUID();
		glm::mat4 childWorldTransform = child.GetComponent<Transform>().GetTransform();
		const bool hasNewParent = parent && m_Registry.valid(parent);
		const UUID newParentUUID = hasNewParent ? parent.GetUUID() : UUID(0);

		if (hasNewParent)
		{
			if (newParentUUID == childUUID)
				return;

			UUID cursor = newParentUUID;
			while (true)
			{
				if (cursor == childUUID)
					return;

				auto parentIt = m_ParentMap.find(cursor);
				if (parentIt == m_ParentMap.end())
					break;

				cursor = parentIt->second;
			}
		}

		auto existingParentIt = m_ParentMap.find(childUUID);
		if (existingParentIt != m_ParentMap.end())
		{
			const UUID oldParentUUID = existingParentIt->second;
			auto oldChildrenIt = m_ChildrenMap.find(oldParentUUID);
			if (oldChildrenIt != m_ChildrenMap.end())
			{
				auto& oldChildren = oldChildrenIt->second;
				oldChildren.erase(std::remove(oldChildren.begin(), oldChildren.end(), childUUID), oldChildren.end());
				if (oldChildren.empty())
					m_ChildrenMap.erase(oldChildrenIt);
			}
			m_ParentMap.erase(existingParentIt);
		}

		if (hasNewParent)
		{
			auto& children = m_ChildrenMap[newParentUUID];
			children.erase(std::remove(children.begin(), children.end(), childUUID), children.end());
			children.push_back(childUUID);
			m_ParentMap[childUUID] = newParentUUID;
		}

		SetWorldTransform(child, childWorldTransform);
	}

	Entity Scene::GetParent(Entity entity)
	{
		if (!entity || !m_Registry.valid(entity))
			return {};

		auto parentIt = m_ParentMap.find(entity.GetUUID());
		if (parentIt == m_ParentMap.end())
			return {};

		return FindEntityByUUID(parentIt->second);
	}

	std::vector<UUID> Scene::GetChildrenUUIDs(Entity entity)
	{
		if (!entity || !m_Registry.valid(entity))
			return {};

		auto childrenIt = m_ChildrenMap.find(entity.GetUUID());
		if (childrenIt == m_ChildrenMap.end())
			return {};

		return childrenIt->second;
	}

	std::vector<UUID> Scene::GetRootEntityUUIDs()
	{
		std::vector<UUID> result;
		for (UUID uuid : m_EntityOrder)
		{
			auto parentIt = m_ParentMap.find(uuid);
			if (parentIt == m_ParentMap.end() || !FindEntityByUUID(parentIt->second))
				result.push_back(uuid);
		}
		return result;
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
				glm::vec3 translation, rotation, scale;
				Math::DecomposeTransform(transform.GetTransform(), translation, rotation, scale);
				translation.x = position.x;
				translation.y = position.y;
				rotation.z = body->GetAngle();
				SetWorldTransform(entity, Math::ComposeTransform(translation, rotation, scale));
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
		if (!entity || !m_Registry.valid(entity))
			return {};

		Entity parent = GetParent(entity);
		return DuplicateEntityRecursive(this, entity, parent);
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
