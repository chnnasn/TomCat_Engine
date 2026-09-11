#include "tcpch.h"
#include "Scene.h"

#include "Components.h"
#include "ScriptableEntity.h"
#include "TomCat/Renderer/Renderer2D.h"
#include "TomCat/Renderer/RenderCommand.h"
#include "TomCat/Math/Math.h"
#include "Entity.h"

#include <algorithm>
#include <cmath>
#include <exception>
#include <iterator>
#include <unordered_set>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_inverse.hpp>

// Box2D
#include "box2d/b2_world.h"
#include "box2d/b2_body.h"
#include "box2d/b2_fixture.h"
#include "box2d/b2_polygon_shape.h"

namespace TomCat {

	namespace {

		bool IsFinite(const glm::vec3& value)
		{
			return std::isfinite(value.x) && std::isfinite(value.y) && std::isfinite(value.z);
		}

		bool IsFinite(const glm::mat4& value)
		{
			for (glm::length_t column = 0; column < 4; ++column)
			{
				for (glm::length_t row = 0; row < 4; ++row)
				{
					if (!std::isfinite(value[column][row]))
						return false;
				}
			}
			return true;
		}

		bool TryDecomposeFiniteTransform(const glm::mat4& value,
			glm::vec3& translation, glm::vec3& rotation, glm::vec3& scale)
		{
			return IsFinite(value)
				&& Math::DecomposeTransform(value, translation, rotation, scale)
				&& IsFinite(translation) && IsFinite(rotation) && IsFinite(scale);
		}

		struct PendingWorldTransform
		{
			Entity Target;
			glm::vec3 Translation{};
			glm::vec3 Rotation{};
			glm::vec3 Scale{};
		};

		bool CollectPendingWorldTransforms(Scene* scene, Entity entity,
			const glm::mat4& parentWorldTransform, const glm::mat4* localTransformOverride,
			std::vector<PendingWorldTransform>& pendingTransforms,
			std::unordered_set<UUID>& visited)
		{
			if (!scene || !entity || !entity.HasComponent<ID>() || !entity.HasComponent<Transform>())
				return false;

			const UUID entityUUID = entity.GetUUID();
			if (!visited.emplace(entityUUID).second)
				return false;

			const auto& transform = entity.GetComponent<Transform>();
			const glm::mat4 localTransform = localTransformOverride
				? *localTransformOverride
				: transform.GetLocalTransform();
			const glm::mat4 worldTransform = parentWorldTransform * localTransform;

			PendingWorldTransform pending{ entity };
			if (!TryDecomposeFiniteTransform(worldTransform,
				pending.Translation, pending.Rotation, pending.Scale))
				return false;

			const glm::mat4 normalizedWorldTransform = Math::ComposeTransform(
				pending.Translation, pending.Rotation, pending.Scale);
			pendingTransforms.push_back(pending);
			for (UUID childUUID : scene->GetChildrenUUIDs(entity))
			{
				Entity child = scene->FindEntityByUUID(childUUID);
				if (!child || scene->GetParent(child) != entity
					|| !CollectPendingWorldTransforms(scene, child, normalizedWorldTransform, nullptr,
					pendingTransforms, visited))
					return false;
			}

			return true;
		}

		void ApplyPendingWorldTransforms(const std::vector<PendingWorldTransform>& pendingTransforms)
		{
			for (const auto& pending : pendingTransforms)
			{
				Entity target = pending.Target;
				auto& transform = target.GetComponent<Transform>();
				transform._Translation = pending.Translation;
				transform._Rotation = pending.Rotation;
				transform._Scale = pending.Scale;
			}
		}

		bool CollectScenePendingWorldTransforms(Scene* scene, const std::vector<UUID>& entityOrder,
			std::vector<PendingWorldTransform>& pendingTransforms)
		{
			if (!scene)
				return false;

			std::unordered_set<UUID> visited;
			for (UUID rootUUID : scene->GetRootEntityUUIDs())
			{
				Entity root = scene->FindEntityByUUID(rootUUID);
				if (!root || !CollectPendingWorldTransforms(scene, root, glm::mat4(1.0f), nullptr,
					pendingTransforms, visited))
					return false;
			}

			// Missing entities, cycles, orphaned child entries, and duplicate child
			// references all make a complete root traversal impossible.
			for (UUID entityUUID : entityOrder)
			{
				if (!scene->FindEntityByUUID(entityUUID)
					|| visited.find(entityUUID) == visited.end())
					return false;
			}

			return visited.size() == entityOrder.size();
		}

		void Render2DComponents(entt::registry& registry)
		{
			auto spriteView = registry.view<Transform, SpriteRenderer>();
			for (const entt::entity entity : spriteView)
			{
				auto [transform, sprite] = spriteView.get<Transform, SpriteRenderer>(entity);
				if (!registry.get<Tag>(entity).Visible || !sprite.Enabled)
					continue;

				Renderer2D::DrawSprite(transform.GetTransform(), sprite, static_cast<int>(entity));
			}

			const float previousLineWidth = Renderer2D::GetLineWidth();
			bool renderedLine = false;
			auto lineView = registry.view<Transform, LineRenderer>();
			for (const entt::entity entity : lineView)
			{
				auto [transform, line] = lineView.get<Transform, LineRenderer>(entity);
				if (!registry.get<Tag>(entity).Visible || !line.Enabled
					|| !std::isfinite(line.Width) || line.Width <= 0.0f)
					continue;

				const glm::mat4 worldTransform = transform.GetTransform();
				const glm::vec3 worldStart = glm::vec3(worldTransform * glm::vec4(line.Start, 1.0f));
				const glm::vec3 worldEnd = glm::vec3(worldTransform * glm::vec4(line.End, 1.0f));
				Renderer2D::SetLineWidth(line.Width);
				Renderer2D::DrawLine(worldStart, worldEnd, line._Color, static_cast<int>(entity));
				renderedLine = true;
			}

			if (renderedLine)
				Renderer2D::SetLineWidth(previousLineWidth);
		}

	}

	void Scene::DestroyNativeScriptInstance(NativeScript& script, const char* context) noexcept
	{
		if (!script.Instance)
			return;

		try
		{
			script.Instance->OnDestroy();
		}
		catch (const std::exception& exception)
		{
			TC_Core_Error("Native script OnDestroy failed during {0}: {1}", context, exception.what());
		}
		catch (...)
		{
			TC_Core_Error("Native script OnDestroy failed during {0} with an unknown exception", context);
		}

		try
		{
			if (script.DestroyScript)
				script.DestroyScript(&script);
			else
				delete script.Instance;
		}
		catch (const std::exception& exception)
		{
			TC_Core_Error("Native script destruction failed during {0}: {1}", context, exception.what());
		}
		catch (...)
		{
			TC_Core_Error("Native script destruction failed during {0} with an unknown exception", context);
		}

		// Never retry a partially completed custom destroy callback. Runtime and
		// physics teardown must continue even when user code violates the boundary.
		script.Instance = nullptr;
	}

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
		OnRuntimeStop();
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
		// CreateEntity has already assigned the duplicate a unique name. Copying the
		// whole Tag would silently overwrite that name with the source name.
		if (src.HasComponent<Tag>())
			dst.GetComponent<Tag>().Visible = src.GetComponent<Tag>().Visible;
		CopyComponentIfExists<Transform>(dst, src);
		CopyComponentIfExists<SpriteRenderer>(dst, src);
		CopyComponentIfExists<LineRenderer>(dst, src);
		CopyComponentIfExists<C_Camera>(dst, src);
		CopyComponentIfExists<NativeScript>(dst, src);
		CopyComponentIfExists<Rigidbody2D>(dst, src);
		CopyComponentIfExists<BoxCollider2D>(dst, src);

		// Runtime-owned pointers must never be shared by an authoring copy or a
		// duplicated entity.
		if (dst.HasComponent<NativeScript>())
			dst.GetComponent<NativeScript>().Instance = nullptr;
		if (dst.HasComponent<Rigidbody2D>())
			dst.GetComponent<Rigidbody2D>().RuntimeBody = nullptr;
		if (dst.HasComponent<BoxCollider2D>())
			dst.GetComponent<BoxCollider2D>().RuntimeFixture = nullptr;
	}

	static Entity DuplicateEntityRecursive(Scene* scene, Entity source, Entity parent)
	{
		if (!scene || !source)
			return {};

		Entity duplicate = scene->CreateEntity(source.GetName());
		CopyEntityComponents(duplicate, source);

		if (parent && !scene->SetParent(duplicate, parent))
		{
			scene->DestroyEntity(duplicate);
			return {};
		}

		for (UUID childUUID : scene->GetChildrenUUIDs(source))
		{
			Entity child = scene->FindEntityByUUID(childUUID);
			if (child && !DuplicateEntityRecursive(scene, child, duplicate))
			{
				scene->DestroyEntity(duplicate);
				return {};
			}
		}

		return duplicate;
	}

	Ref<Scene> Scene::Copy(Ref<Scene> other)
	{
		if (!other)
		{
			TC_Core_Error("Scene::Copy requires a valid source scene");
			return nullptr;
		}
		if (other->m_EntityMap.size() != other->m_EntityOrder.size())
		{
			TC_Core_Error("Could not copy scene '{0}' because its entity index is inconsistent",
				other->m_SceneName);
			return nullptr;
		}
		std::unordered_set<UUID> sourceUUIDs;
		for (UUID uuid : other->m_EntityOrder)
		{
			Entity sourceEntity = other->FindEntityByUUID(uuid);
			if ((uint64_t)uuid == 0 || !sourceEntity || !sourceEntity.HasComponent<Tag>()
				|| !sourceEntity.HasComponent<Transform>() || !sourceUUIDs.emplace(uuid).second)
			{
				TC_Core_Error("Could not copy scene '{0}' because entity UUID {1} is invalid or duplicated",
					other->m_SceneName, (uint64_t)uuid);
				return nullptr;
			}
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
			const std::string name = newScene->MakeUniqueEntityName(sourceEntity.GetName());
			Entity newEntity = newScene->CreateEntityWithUUID(uuid, name);
			enttMap[uuid] = (entt::entity)newEntity;
		}

		// ID and the newly created Tag names stay owned by the destination scene.
		CopyComponent<Transform>(dstSceneRegistry, srcSceneRegistry, enttMap);
		for (const auto& [uuid, destinationEntity] : enttMap)
		{
			Entity sourceEntity = other->FindEntityByUUID(uuid);
			if (sourceEntity && sourceEntity.HasComponent<Tag>())
				dstSceneRegistry.get<Tag>(destinationEntity).Visible = sourceEntity.GetComponent<Tag>().Visible;
		}
		CopyComponent<SpriteRenderer>(dstSceneRegistry, srcSceneRegistry, enttMap);
		CopyComponent<LineRenderer>(dstSceneRegistry, srcSceneRegistry, enttMap);
		CopyComponent<C_Camera>(dstSceneRegistry, srcSceneRegistry, enttMap);
		CopyComponent<NativeScript>(dstSceneRegistry, srcSceneRegistry, enttMap);
		CopyComponent<Rigidbody2D>(dstSceneRegistry, srcSceneRegistry, enttMap);
		CopyComponent<BoxCollider2D>(dstSceneRegistry, srcSceneRegistry, enttMap);

		for (auto entity : dstSceneRegistry.view<NativeScript>())
			dstSceneRegistry.get<NativeScript>(entity).Instance = nullptr;
		for (auto entity : dstSceneRegistry.view<Rigidbody2D>())
			dstSceneRegistry.get<Rigidbody2D>(entity).RuntimeBody = nullptr;
		for (auto entity : dstSceneRegistry.view<BoxCollider2D>())
			dstSceneRegistry.get<BoxCollider2D>(entity).RuntimeFixture = nullptr;

		for (UUID childUUID : other->m_EntityOrder)
		{
			auto sourceParentIt = other->m_ParentMap.find(childUUID);
			if (sourceParentIt == other->m_ParentMap.end())
				continue;
			const UUID parentUUID = sourceParentIt->second;
			auto childIt = enttMap.find(childUUID);
			auto parentIt = enttMap.find(parentUUID);
			if (childIt == enttMap.end() || parentIt == enttMap.end())
				continue;

			newScene->m_ParentMap[childUUID] = parentUUID;
			newScene->m_ChildrenMap[parentUUID].push_back(childUUID);
		}
		if (!newScene->SyncTransformHierarchy())
		{
			TC_Core_Error("Could not copy scene '{0}' because its transform hierarchy is invalid",
				other->m_SceneName);
			return nullptr;
		}

		return newScene;
	}

	Entity Scene::CreateEntity(const std::string& name)
	{
		const std::string baseName = name.empty() ? "Entity" : name;
		UUID uuid;
		while ((uint64_t)uuid == 0 || m_EntityMap.find(uuid) != m_EntityMap.end())
			uuid = UUID();
		return CreateEntityWithUUID(uuid, MakeUniqueEntityName(baseName));
	}

	Entity Scene::CreateEntityWithUUID(UUID uuid, const std::string& name)
	{
		if ((uint64_t)uuid == 0)
		{
			TC_Core_Error("Cannot create an entity with reserved UUID 0");
			return {};
		}
		if (m_EntityMap.find(uuid) != m_EntityMap.end())
		{
			TC_Core_Error("Cannot create duplicate entity UUID {0}", (uint64_t)uuid);
			return {};
		}

		Entity entity = { m_Registry.create(), this };
		entity.AddComponent<ID>(uuid);
		entity.AddComponent<Transform>();
		auto& tag = entity.AddComponent<Tag>();
		tag._Tag = MakeUniqueEntityName(name.empty() ? "Entity" : name);
		m_EntityMap.emplace(uuid, (entt::entity)entity);
		m_EntityOrder.push_back(uuid);
		return entity;
	}

	bool Scene::RenameEntity(Entity entity, const std::string& requestedName)
	{
		if (!entity || entity.m_Scene != this || !m_Registry.valid(entity.m_EntityHandle)
			|| !entity.HasComponent<Tag>())
			return false;

		auto& tag = entity.GetComponent<Tag>()._Tag;
		const std::string baseName = requestedName.empty() ? "Entity" : requestedName;
		if (tag == baseName)
			return false;
		tag = MakeUniqueEntityName(baseName);
		return true;
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

	bool Scene::SetWorldTransform(Entity entity, const glm::mat4& worldTransform)
	{
		if (!entity || entity.m_Scene != this || !m_Registry.valid(entity.m_EntityHandle)
			|| !entity.HasComponent<ID>() || !entity.HasComponent<Transform>() || !IsFinite(worldTransform))
			return false;

		const Entity parent = GetParent(entity);
		const glm::mat4 parentWorld = parent ? parent.GetComponent<Transform>().GetTransform() : glm::mat4(1.0f);
		const float parentDeterminant = glm::determinant(parentWorld);
		if (!IsFinite(parentWorld) || !std::isfinite(parentDeterminant)
			|| std::abs(parentDeterminant) <= 1.0e-8f)
		{
			TC_Core_Warn("Cannot set world transform under a singular parent");
			return false;
		}

		const glm::mat4 localTransform = glm::inverse(parentWorld) * worldTransform;
		glm::vec3 worldTranslation{}, worldRotation{}, worldScale{};
		glm::vec3 localTranslation{}, localRotation{}, localScale{};
		if (!TryDecomposeFiniteTransform(worldTransform, worldTranslation, worldRotation, worldScale)
			|| !TryDecomposeFiniteTransform(localTransform, localTranslation, localRotation, localScale))
			return false;

		const glm::mat4 normalizedLocalTransform = Math::ComposeTransform(
			localTranslation, localRotation, localScale);
		std::vector<PendingWorldTransform> pendingTransforms;
		std::unordered_set<UUID> visited;
		if (!CollectPendingWorldTransforms(this, entity, parentWorld, &normalizedLocalTransform,
			pendingTransforms, visited))
			return false;

		auto& transform = entity.GetComponent<Transform>();
		transform._LocalTranslation = localTranslation;
		transform._LocalRotation = localRotation;
		transform._LocalScale = localScale;
		ApplyPendingWorldTransforms(pendingTransforms);
		return true;
	}

	bool Scene::SetLocalTransform(Entity entity, const glm::mat4& localTransform)
	{
		if (!entity || entity.m_Scene != this || !m_Registry.valid(entity.m_EntityHandle)
			|| !entity.HasComponent<ID>() || !entity.HasComponent<Transform>() || !IsFinite(localTransform))
			return false;

		const Entity parent = GetParent(entity);
		const glm::mat4 parentWorld = parent ? parent.GetComponent<Transform>().GetTransform() : glm::mat4(1.0f);
		glm::vec3 localTranslation{}, localRotation{}, localScale{};
		if (!TryDecomposeFiniteTransform(localTransform, localTranslation, localRotation, localScale))
			return false;

		const glm::mat4 normalizedLocalTransform = Math::ComposeTransform(
			localTranslation, localRotation, localScale);
		std::vector<PendingWorldTransform> pendingTransforms;
		std::unordered_set<UUID> visited;
		if (!CollectPendingWorldTransforms(this, entity, parentWorld, &normalizedLocalTransform,
			pendingTransforms, visited))
			return false;

		auto& transform = entity.GetComponent<Transform>();
		transform._LocalTranslation = localTranslation;
		transform._LocalRotation = localRotation;
		transform._LocalScale = localScale;
		ApplyPendingWorldTransforms(pendingTransforms);
		return true;
	}

	bool Scene::SyncTransformHierarchy()
	{
		std::vector<PendingWorldTransform> pendingTransforms;
		if (!CollectScenePendingWorldTransforms(this, m_EntityOrder, pendingTransforms))
			return false;
		ApplyPendingWorldTransforms(pendingTransforms);
		return true;
	}

	bool Scene::ValidateTransformHierarchy()
	{
		std::vector<PendingWorldTransform> pendingTransforms;
		return CollectScenePendingWorldTransforms(this, m_EntityOrder, pendingTransforms);
	}


	void Scene::DestroyEntity(Entity entity)
	{
		if (!entity || entity.m_Scene != this || !m_Registry.valid(entity.m_EntityHandle))
			return;

		const auto entityMapIt = std::find_if(m_EntityMap.begin(), m_EntityMap.end(),
			[handle = entity.m_EntityHandle](const auto& entry) { return entry.second == handle; });
		if (entityMapIt == m_EntityMap.end())
		{
			TC_Core_Error("Cannot destroy an entity that is missing from the scene UUID index");
			return;
		}
		const UUID entityUUID = entityMapIt->first;
		std::vector<UUID> children;
		if (auto childrenIt = m_ChildrenMap.find(entityUUID); childrenIt != m_ChildrenMap.end())
			children = childrenIt->second;
		for (UUID childUUID : children)
		{
			Entity child;
			if (auto childIt = m_EntityMap.find(childUUID);
				childIt != m_EntityMap.end() && m_Registry.valid(childIt->second))
				child = Entity(childIt->second, this);
			if (child)
				DestroyEntity(child);
			m_ParentMap.erase(childUUID);
		}

		auto parentIt = m_ParentMap.find(entityUUID);
		if (parentIt != m_ParentMap.end())
		{
			auto childrenIt = m_ChildrenMap.find(parentIt->second);
			if (childrenIt != m_ChildrenMap.end())
			{
				auto& siblings = childrenIt->second;
				siblings.erase(std::remove(siblings.begin(), siblings.end(), entityUUID), siblings.end());
				if (siblings.empty())
					m_ChildrenMap.erase(childrenIt);
			}
			m_ParentMap.erase(parentIt);
		}
		m_ChildrenMap.erase(entityUUID);
		m_EntityOrder.erase(std::remove(m_EntityOrder.begin(), m_EntityOrder.end(), entityUUID), m_EntityOrder.end());

		if (entity.HasComponent<NativeScript>())
			DestroyNativeScriptInstance(entity.GetComponent<NativeScript>(), "entity destruction");

		if (entity.HasComponent<BoxCollider2D>())
			entity.GetComponent<BoxCollider2D>().RuntimeFixture = nullptr;
		if (entity.HasComponent<Rigidbody2D>())
		{
			auto& rigidbody = entity.GetComponent<Rigidbody2D>();
			if (m_PhysicsWorld && rigidbody.RuntimeBody)
				m_PhysicsWorld->DestroyBody(static_cast<b2Body*>(rigidbody.RuntimeBody));
			rigidbody.RuntimeBody = nullptr;
		}

		m_EntityMap.erase(entityUUID);
		m_Registry.destroy(entity);
	}

	bool Scene::SetParent(Entity child, Entity parent)
	{
		if (!child || child.m_Scene != this || !m_Registry.valid(child.m_EntityHandle)
			|| !child.HasComponent<ID>() || !child.HasComponent<Transform>())
			return false;

		const UUID childUUID = child.GetUUID();
		const bool hasNewParent = static_cast<bool>(parent);
		if (hasNewParent && (parent.m_Scene != this || !m_Registry.valid(parent.m_EntityHandle)
			|| !parent.HasComponent<ID>() || !parent.HasComponent<Transform>()))
			return false;

		const UUID newParentUUID = hasNewParent ? parent.GetUUID() : UUID(0);
		auto existingParentIt = m_ParentMap.find(childUUID);
		if ((existingParentIt == m_ParentMap.end() && !hasNewParent)
			|| (existingParentIt != m_ParentMap.end() && hasNewParent && existingParentIt->second == newParentUUID))
			return true;

		if (hasNewParent)
		{
			if (newParentUUID == childUUID)
				return false;

			UUID cursor = newParentUUID;
			std::unordered_set<UUID> visited;
			while (true)
			{
				if (cursor == childUUID || !visited.emplace(cursor).second)
					return false;

				auto parentIt = m_ParentMap.find(cursor);
				if (parentIt == m_ParentMap.end())
					break;

				cursor = parentIt->second;
			}
		}

		const auto& childTransform = child.GetComponent<Transform>();
		const glm::mat4 childWorld = childTransform.GetTransform();
		glm::vec3 localTranslation = childTransform._Translation;
		glm::vec3 localRotation = childTransform._Rotation;
		glm::vec3 localScale = childTransform._Scale;
		glm::mat4 newParentWorld(1.0f);
		std::vector<PendingWorldTransform> pendingTransforms;
		if (!IsFinite(childWorld) || !IsFinite(localTranslation)
			|| !IsFinite(localRotation) || !IsFinite(localScale))
			return false;

		if (hasNewParent)
		{
			newParentWorld = parent.GetComponent<Transform>().GetTransform();
			const float determinant = glm::determinant(newParentWorld);
			if (!IsFinite(newParentWorld) || !std::isfinite(determinant) || std::abs(determinant) <= 1.0e-8f)
			{
				TC_Core_Warn("Cannot parent an entity under a singular transform");
				return false;
			}

			const glm::mat4 newLocal = glm::inverse(newParentWorld) * childWorld;
			if (!TryDecomposeFiniteTransform(newLocal, localTranslation, localRotation, localScale))
				return false;

			const glm::mat4 normalizedLocalTransform = Math::ComposeTransform(
				localTranslation, localRotation, localScale);
			std::unordered_set<UUID> visited;
			if (!CollectPendingWorldTransforms(this, child, newParentWorld, &normalizedLocalTransform,
				pendingTransforms, visited))
				return false;
		}

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

		auto& mutableTransform = child.GetComponent<Transform>();
		mutableTransform._LocalTranslation = localTranslation;
		mutableTransform._LocalRotation = localRotation;
		mutableTransform._LocalScale = localScale;
		if (hasNewParent)
			ApplyPendingWorldTransforms(pendingTransforms);
		return true;
	}

	bool Scene::MoveEntity(Entity entity, Entity target, EntityPlacement placement)
	{
		auto isValidSceneEntity = [this](Entity candidate)
		{
			return candidate && candidate.m_Scene == this
				&& m_Registry.valid(candidate.m_EntityHandle)
				&& candidate.HasComponent<ID>() && candidate.HasComponent<Transform>()
				&& static_cast<uint64_t>(candidate.GetUUID()) != 0
				&& FindEntityByUUID(candidate.GetUUID()) == candidate;
		};

		if (!isValidSceneEntity(entity))
			return false;
		if (placement != EntityPlacement::Root && !isValidSceneEntity(target))
			return false;
		if (placement != EntityPlacement::Root && entity == target)
			return false;
		if (placement != EntityPlacement::Before && placement != EntityPlacement::Child
			&& placement != EntityPlacement::After && placement != EntityPlacement::Root)
			return false;

		if (m_EntityOrder.size() != m_EntityMap.size() || !ValidateTransformHierarchy())
			return false;
		std::unordered_set<UUID> orderedEntities;
		for (UUID uuid : m_EntityOrder)
		{
			Entity orderedEntity = FindEntityByUUID(uuid);
			if (!orderedEntity || !orderedEntity.HasComponent<Transform>()
				|| !orderedEntities.emplace(uuid).second)
				return false;
		}

		auto collectSubtree = [this](UUID root, std::unordered_set<UUID>& subtree)
		{
			std::vector<UUID> pending{ root };
			while (!pending.empty())
			{
				const UUID current = pending.back();
				pending.pop_back();
				Entity currentEntity = FindEntityByUUID(current);
				if (!currentEntity || !currentEntity.HasComponent<Transform>()
					|| !subtree.emplace(current).second)
					return false;

				auto childrenIt = m_ChildrenMap.find(current);
				if (childrenIt == m_ChildrenMap.end())
					continue;
				for (UUID child : childrenIt->second)
				{
					auto parentIt = m_ParentMap.find(child);
					if (parentIt == m_ParentMap.end() || parentIt->second != current)
						return false;
					pending.push_back(child);
				}
			}
			return true;
		};

		const UUID entityUUID = entity.GetUUID();
		std::unordered_set<UUID> movingSubtree;
		if (!collectSubtree(entityUUID, movingSubtree))
			return false;

		UUID targetUUID(0);
		if (placement != EntityPlacement::Root)
		{
			targetUUID = target.GetUUID();
			if (movingSubtree.find(targetUUID) != movingSubtree.end())
				return false;
		}

		bool hasNewParent = false;
		UUID newParentUUID(0);
		if (placement == EntityPlacement::Child)
		{
			hasNewParent = true;
			newParentUUID = targetUUID;
		}
		else if (placement == EntityPlacement::Before || placement == EntityPlacement::After)
		{
			auto targetParentIt = m_ParentMap.find(targetUUID);
			if (targetParentIt != m_ParentMap.end())
			{
				hasNewParent = true;
				newParentUUID = targetParentIt->second;
			}
		}
		if (hasNewParent && movingSubtree.find(newParentUUID) != movingSubtree.end())
			return false;

		auto candidateParentMap = m_ParentMap;
		auto candidateChildrenMap = m_ChildrenMap;
		auto oldParentIt = candidateParentMap.find(entityUUID);
		if (oldParentIt != candidateParentMap.end())
		{
			auto oldSiblingsIt = candidateChildrenMap.find(oldParentIt->second);
			if (oldSiblingsIt == candidateChildrenMap.end())
				return false;
			auto& oldSiblings = oldSiblingsIt->second;
			const size_t oldSiblingCount = oldSiblings.size();
			oldSiblings.erase(std::remove(oldSiblings.begin(), oldSiblings.end(), entityUUID),
				oldSiblings.end());
			if (oldSiblings.size() + 1 != oldSiblingCount)
				return false;
			if (oldSiblings.empty())
				candidateChildrenMap.erase(oldSiblingsIt);
			candidateParentMap.erase(oldParentIt);
		}

		if (hasNewParent)
		{
			auto& newSiblings = candidateChildrenMap[newParentUUID];
			newSiblings.erase(std::remove(newSiblings.begin(), newSiblings.end(), entityUUID),
				newSiblings.end());
			if (placement == EntityPlacement::Child)
				newSiblings.push_back(entityUUID);
			else
			{
				auto targetIt = std::find(newSiblings.begin(), newSiblings.end(), targetUUID);
				if (targetIt == newSiblings.end())
					return false;
				newSiblings.insert(placement == EntityPlacement::Before ? targetIt : std::next(targetIt),
					entityUUID);
			}
			candidateParentMap[entityUUID] = newParentUUID;
		}

		std::vector<UUID> movingBlock;
		std::vector<UUID> candidateOrder;
		movingBlock.reserve(movingSubtree.size());
		candidateOrder.reserve(m_EntityOrder.size());
		for (UUID uuid : m_EntityOrder)
		{
			if (movingSubtree.find(uuid) != movingSubtree.end())
				movingBlock.push_back(uuid);
			else
				candidateOrder.push_back(uuid);
		}
		if (movingBlock.size() != movingSubtree.size())
			return false;

		size_t insertionIndex = candidateOrder.size();
		if (placement == EntityPlacement::Before)
		{
			auto targetIt = std::find(candidateOrder.begin(), candidateOrder.end(), targetUUID);
			if (targetIt == candidateOrder.end())
				return false;
			insertionIndex = static_cast<size_t>(std::distance(candidateOrder.begin(), targetIt));
		}
		else if (placement == EntityPlacement::After || placement == EntityPlacement::Child)
		{
			std::unordered_set<UUID> targetSubtree;
			if (!collectSubtree(targetUUID, targetSubtree))
				return false;
			bool foundTargetSubtree = false;
			for (size_t index = 0; index < candidateOrder.size(); ++index)
			{
				if (targetSubtree.find(candidateOrder[index]) != targetSubtree.end())
				{
					insertionIndex = index + 1;
					foundTargetSubtree = true;
				}
			}
			if (!foundTargetSubtree)
				return false;
		}
		candidateOrder.insert(candidateOrder.begin() + static_cast<std::ptrdiff_t>(insertionIndex),
			movingBlock.begin(), movingBlock.end());

		if (candidateOrder == m_EntityOrder && candidateParentMap == m_ParentMap
			&& candidateChildrenMap == m_ChildrenMap)
			return false;

		Entity newParent = hasNewParent ? FindEntityByUUID(newParentUUID) : Entity{};
		if (hasNewParent && !newParent)
			return false;
		if (!SetParent(entity, newParent))
			return false;

		m_ParentMap.swap(candidateParentMap);
		m_ChildrenMap.swap(candidateChildrenMap);
		m_EntityOrder.swap(candidateOrder);
		return true;
	}

	Entity Scene::GetParent(Entity entity)
	{
		if (!entity || entity.m_Scene != this || !m_Registry.valid(entity.m_EntityHandle)
			|| !entity.HasComponent<ID>())
			return {};

		auto parentIt = m_ParentMap.find(entity.GetUUID());
		if (parentIt == m_ParentMap.end())
			return {};

		return FindEntityByUUID(parentIt->second);
	}

	std::vector<UUID> Scene::GetChildrenUUIDs(Entity entity)
	{
		if (!entity || entity.m_Scene != this || !m_Registry.valid(entity.m_EntityHandle)
			|| !entity.HasComponent<ID>())
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
		if (m_RuntimeRunning)
			return;

		m_PhysicsWorld = new b2World({ 0.0f, -9.8f });
		m_RuntimeRunning = true;

		auto view = m_Registry.view<Rigidbody2D>();
		for (auto e : view)
		{
			Entity entity = { e, this };
			auto& transform = entity.GetComponent<Transform>();
			auto& rb2d = entity.GetComponent<Rigidbody2D>();
			rb2d.RuntimeBody = nullptr;
			if (entity.HasComponent<BoxCollider2D>())
				entity.GetComponent<BoxCollider2D>().RuntimeFixture = nullptr;
			if (!rb2d.Enabled)
				continue;
			if (!std::isfinite(transform._Translation.x) || !std::isfinite(transform._Translation.y)
				|| !std::isfinite(transform._Rotation.z))
			{
				TC_Core_Warn("Skipping Rigidbody2D with a non-finite transform on entity '{0}'", entity.GetName());
				continue;
			}

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
				const bool validCollider = std::isfinite(bc2d.Offset.x) && std::isfinite(bc2d.Offset.y)
					&& std::isfinite(bc2d.Size.x) && std::isfinite(bc2d.Size.y)
					&& bc2d.Size.x > 0.0f && bc2d.Size.y > 0.0f
					&& std::isfinite(transform._Scale.x) && std::isfinite(transform._Scale.y)
					&& std::isfinite(bc2d.Density) && bc2d.Density >= 0.0f
					&& std::isfinite(bc2d.Friction) && bc2d.Friction >= 0.0f && bc2d.Friction <= 1.0f
					&& std::isfinite(bc2d.Restitution) && bc2d.Restitution >= 0.0f && bc2d.Restitution <= 1.0f
					&& std::isfinite(bc2d.RestitutionThreshold) && bc2d.RestitutionThreshold >= 0.0f;
				if (!validCollider)
				{
					TC_Core_Warn("Skipping invalid BoxCollider2D on entity '{0}'", entity.GetName());
					continue;
				}

				const float halfWidth = std::abs(bc2d.Size.x * transform._Scale.x);
				const float halfHeight = std::abs(bc2d.Size.y * transform._Scale.y);
				const float centerX = bc2d.Offset.x * transform._Scale.x;
				const float centerY = bc2d.Offset.y * transform._Scale.y;
				if (!std::isfinite(halfWidth) || !std::isfinite(halfHeight)
					|| !std::isfinite(centerX) || !std::isfinite(centerY)
					|| halfWidth <= b2_epsilon || halfHeight <= b2_epsilon)
				{
					TC_Core_Warn("Skipping degenerate BoxCollider2D on entity '{0}'", entity.GetName());
					continue;
				}

				b2PolygonShape boxShape;
				const b2Vec2 center(centerX, centerY);
				boxShape.SetAsBox(halfWidth, halfHeight, center, 0.0f);

				b2FixtureDef fixtureDef;
				fixtureDef.shape = &boxShape;
				fixtureDef.density = bc2d.Density;
				fixtureDef.friction = bc2d.Friction;
				fixtureDef.restitution = bc2d.Restitution;
				fixtureDef.restitutionThreshold = bc2d.RestitutionThreshold;
				bc2d.RuntimeFixture = body->CreateFixture(&fixtureDef);
			}
		}
	}

	void Scene::OnRuntimeStop()
	{
		auto scriptView = m_Registry.view<NativeScript>();
		for (auto entity : scriptView)
		{
			auto& script = scriptView.get<NativeScript>(entity);
			if (!script.Instance)
				continue;

			DestroyNativeScriptInstance(script, "runtime shutdown");
		}

		auto rigidbodyView = m_Registry.view<Rigidbody2D>();
		for (auto entity : rigidbodyView)
			rigidbodyView.get<Rigidbody2D>(entity).RuntimeBody = nullptr;

		auto colliderView = m_Registry.view<BoxCollider2D>();
		for (auto entity : colliderView)
			colliderView.get<BoxCollider2D>(entity).RuntimeFixture = nullptr;

		delete m_PhysicsWorld;
		m_PhysicsWorld = nullptr;
		m_RuntimeRunning = false;
	}



	void Scene::OnUpdateRuntime(Timestep ts)
	{
		if (!m_RuntimeRunning || !m_PhysicsWorld)
		{
			TC_Core_Warn("Ignoring runtime update for a scene that has not been started");
			return;
		}

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
						if (!nsc.InstantiateScript)
						{
							TC_Core_Warn("NativeScript on entity {0} has not been bound", (uint32_t)entity);
							return;
						}
						nsc.Instance = nsc.InstantiateScript();
						if (!nsc.Instance)
							return;
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
				glm::vec3 translation = transform._Translation;
				glm::vec3 rotation = transform._Rotation;
				const glm::vec3 scale = transform._Scale;
				translation.x = position.x;
				translation.y = position.y;
				rotation.z = body->GetAngle();
				if (!SetWorldTransform(entity, Math::ComposeTransform(translation, rotation, scale)))
					TC_Core_Warn("Could not apply the physics transform to entity '{0}'", entity.GetName());
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

			Render2DComponents(m_Registry);

			Renderer2D::EndScene();
		}

	}

	void Scene::OnUpdateEditor(Timestep ts, EditorCamera& camera)
	{
		Renderer2D::BeginScene(camera);

		Render2DComponents(m_Registry);

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

			Render2DComponents(m_Registry);

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
		if (!entity || entity.m_Scene != this || !m_Registry.valid(entity.m_EntityHandle)
			|| !entity.HasComponent<ID>() || !entity.HasComponent<Tag>() || !entity.HasComponent<Transform>())
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
		auto entityIt = m_EntityMap.find(uuid);
		if (entityIt != m_EntityMap.end() && m_Registry.valid(entityIt->second)
			&& m_Registry.all_of<ID>(entityIt->second)
			&& m_Registry.get<ID>(entityIt->second).id == uuid)
			return Entity(entityIt->second, this);
		return {};
	}

	std::vector<AssetReference> Scene::FindAssetReferences(AssetHandle handle)
	{
		std::vector<AssetReference> references;
		if (static_cast<uint64_t>(handle) == 0)
			return references;

		auto view = m_Registry.view<ID, SpriteRenderer>();
		for (const entt::entity entity : view)
		{
			const auto& sprite = view.get<SpriteRenderer>(entity);
			if (sprite.SpriteHandle != handle)
				continue;
			const uint64_t entityID = static_cast<uint64_t>(view.get<ID>(entity).id);
			AssetReference reference;
			reference.ReferencedAsset = handle;
			reference.PropertyPath = "Entity " + std::to_string(entityID) +
				".SpriteRenderer.SpriteHandle";
			references.push_back(std::move(reference));
		}
		return references;
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
	void Scene::OnComponentAdded<LineRenderer>(Entity entity, LineRenderer& component)
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
