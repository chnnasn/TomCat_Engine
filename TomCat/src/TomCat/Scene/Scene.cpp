#include "tcpch.h"
#include "Scene.h"

#include "Components.h"
#include "TomCat/Scripting/ScriptEngine.h"
#include "TomCat/Renderer/Renderer2D.h"
#include "TomCat/Renderer/RenderCommand.h"
#include "TomCat/Math/Math.h"
#include "Entity.h"
#include "Serialization/ComponentCodecs.h"

#include <algorithm>
#include <bit>
#include <cmath>
#include <cstdint>
#include <exception>
#include <iterator>
#include <limits>
#include <utility>
#include <unordered_set>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_inverse.hpp>
#include <glm/gtc/matrix_transform.hpp>

// Box2D
#include "box2d/b2_world.h"
#include "box2d/b2_body.h"
#include "box2d/b2_circle_shape.h"
#include "box2d/b2_contact.h"
#include "box2d/b2_fixture.h"
#include "box2d/b2_distance_joint.h"
#include "box2d/b2_joint.h"
#include "box2d/b2_polygon_shape.h"
#include "box2d/b2_world_callbacks.h"

namespace TomCat {
	static_assert(sizeof(uintptr_t) >= sizeof(uint64_t),
		"Box2D body user data must be able to store a complete entity UUID value");

	class SceneContactListener final : public b2ContactListener
	{
	public:
		enum class PendingType
		{
			Enter,
			Exit
		};

		struct EntityPair
		{
			uint64_t A = 0;
			uint64_t B = 0;
			bool IsTrigger = false;

			bool operator==(const EntityPair& other) const
			{
				return A == other.A && B == other.B && IsTrigger == other.IsTrigger;
			}
		};

		struct EntityPairHash
		{
			size_t operator()(const EntityPair& pair) const
			{
				const size_t first = std::hash<uint64_t>{}(pair.A);
				const size_t second = std::hash<uint64_t>{}(pair.B);
				const size_t pairHash = first ^ (second + 0x9e3779b9u + (first << 6) + (first >> 2));
				return pairHash ^ (std::hash<bool>{}(pair.IsTrigger) + 0x9e3779b9u
					+ (pairHash << 6) + (pairHash >> 2));
			}
		};

		struct PendingEvent
		{
			PendingType Type = PendingType::Enter;
			EntityPair Pair;
		};

		void BeginContact(b2Contact* contact) override
		{
			const std::optional<EntityPair> pair = GetEntityPair(contact);
			if (!pair)
				return;

			// Box2D may report the same contact more than once during continuous
			// collision processing. Count each concrete contact only once.
			if (!m_ContactPairs.emplace(contact, *pair).second)
				return;

			size_t& activeCount = m_ActivePairCounts[*pair];
			++activeCount;
			if (m_InStep)
			{
				MarkStepPair(*pair);
				m_StepSawBegin.insert(*pair);
			}
		}

		void EndContact(b2Contact* contact) override
		{
			auto contactIt = m_ContactPairs.find(contact);
			if (contactIt == m_ContactPairs.end())
				return;

			const EntityPair pair = contactIt->second;
			m_ContactPairs.erase(contactIt);
			auto countIt = m_ActivePairCounts.find(pair);
			if (countIt == m_ActivePairCounts.end())
				return;

			if (countIt->second > 1)
			{
				--countIt->second;
			}
			else
				m_ActivePairCounts.erase(countIt);

			if (m_InStep)
			{
				MarkStepPair(pair);
				m_StepSawEnd.insert(pair);
			}
		}

		void BeginStep()
		{
			m_InStep = true;
			m_StepTouched.clear();
			m_StepTouchedSet.clear();
			m_StepSawBegin.clear();
			m_StepSawEnd.clear();
			m_StepInitialActive.clear();
			for (const EntityPair& pair : m_RebuildCarryActive)
			{
				m_StepInitialActive.insert(pair);
				MarkStepPair(pair);
			}
			m_RebuildCarryActive.clear();
			for (const auto& [pair, count] : m_ActivePairCounts)
				if (count > 0)
					m_StepInitialActive.insert(pair);
		}

		void EndStep()
		{
			if (!m_InStep)
				return;
			m_InStep = false;

			for (const EntityPair& pair : m_StepTouched)
			{
				const bool initiallyActive = m_StepInitialActive.find(pair) != m_StepInitialActive.end();
				const auto finalIt = m_ActivePairCounts.find(pair);
				const bool finallyActive = finalIt != m_ActivePairCounts.end() && finalIt->second > 0;
				if (!initiallyActive && finallyActive)
					m_PendingEvents.push_back({ PendingType::Enter, pair });
				else if (initiallyActive && !finallyActive)
					m_PendingEvents.push_back({ PendingType::Exit, pair });
				else if (!initiallyActive && !finallyActive
					&& m_StepSawBegin.find(pair) != m_StepSawBegin.end()
					&& m_StepSawEnd.find(pair) != m_StepSawEnd.end())
				{
					// Preserve a complete transient contact, but coalesce any number
					// of callbacks into one Enter followed by one Exit.
					m_PendingEvents.push_back({ PendingType::Enter, pair });
					m_PendingEvents.push_back({ PendingType::Exit, pair });
				}
			}

			m_StepInitialActive.clear();
			m_StepTouched.clear();
			m_StepTouchedSet.clear();
			m_StepSawBegin.clear();
			m_StepSawEnd.clear();
		}

		std::vector<PendingEvent> TakePendingEvents()
		{
			std::vector<PendingEvent> events;
			events.swap(m_PendingEvents);
			return events;
		}

		void PrepareForWorldRebuild()
		{
			for (const auto& [pair, count] : m_ActivePairCounts)
				if (count > 0)
					m_RebuildCarryActive.insert(pair);
			m_ContactPairs.clear();
			m_ActivePairCounts.clear();
			m_PendingEvents.clear();
			m_InStep = false;
			m_StepInitialActive.clear();
			m_StepTouched.clear();
			m_StepTouchedSet.clear();
			m_StepSawBegin.clear();
			m_StepSawEnd.clear();
		}

		void DiscardEntity(UUID uuid)
		{
			const uint64_t value = static_cast<uint64_t>(uuid);
			auto containsEntity = [value](const EntityPair& pair)
			{
				return pair.A == value || pair.B == value;
			};

			m_PendingEvents.erase(std::remove_if(m_PendingEvents.begin(), m_PendingEvents.end(),
				[&](const PendingEvent& event) { return containsEntity(event.Pair); }),
				m_PendingEvents.end());
			for (auto it = m_ActivePairCounts.begin(); it != m_ActivePairCounts.end();)
			{
				if (containsEntity(it->first))
					it = m_ActivePairCounts.erase(it);
				else
					++it;
			}
			for (auto it = m_ContactPairs.begin(); it != m_ContactPairs.end();)
			{
				if (containsEntity(it->second))
					it = m_ContactPairs.erase(it);
				else
					++it;
			}
			auto eraseStepPairs = [&](auto& pairs)
			{
				for (auto it = pairs.begin(); it != pairs.end();)
				{
					if (containsEntity(*it))
						it = pairs.erase(it);
					else
						++it;
				}
			};
			eraseStepPairs(m_StepInitialActive);
			eraseStepPairs(m_StepTouchedSet);
			eraseStepPairs(m_StepSawBegin);
			eraseStepPairs(m_StepSawEnd);
			eraseStepPairs(m_RebuildCarryActive);
			m_StepTouched.erase(std::remove_if(m_StepTouched.begin(), m_StepTouched.end(),
				containsEntity), m_StepTouched.end());
		}

		void Clear()
		{
			m_ContactPairs.clear();
			m_ActivePairCounts.clear();
			m_PendingEvents.clear();
			m_InStep = false;
			m_StepInitialActive.clear();
			m_StepTouched.clear();
			m_StepTouchedSet.clear();
			m_StepSawBegin.clear();
			m_StepSawEnd.clear();
			m_RebuildCarryActive.clear();
		}

	private:
		void MarkStepPair(const EntityPair& pair)
		{
			if (m_StepTouchedSet.insert(pair).second)
				m_StepTouched.push_back(pair);
		}

		static std::optional<EntityPair> GetEntityPair(b2Contact* contact)
		{
			if (!contact || !contact->GetFixtureA() || !contact->GetFixtureB())
				return std::nullopt;

			b2Body* bodyA = contact->GetFixtureA()->GetBody();
			b2Body* bodyB = contact->GetFixtureB()->GetBody();
			if (!bodyA || !bodyB)
				return std::nullopt;

			const uint64_t uuidA = static_cast<uint64_t>(bodyA->GetUserData().pointer);
			const uint64_t uuidB = static_cast<uint64_t>(bodyB->GetUserData().pointer);
			if (uuidA == 0 || uuidB == 0 || uuidA == uuidB)
				return std::nullopt;

			return EntityPair{ std::min(uuidA, uuidB), std::max(uuidA, uuidB),
				contact->GetFixtureA()->IsSensor() || contact->GetFixtureB()->IsSensor() };
		}

		std::unordered_map<const b2Contact*, EntityPair> m_ContactPairs;
		std::unordered_map<EntityPair, size_t, EntityPairHash> m_ActivePairCounts;
		std::vector<PendingEvent> m_PendingEvents;
		bool m_InStep = false;
		std::unordered_set<EntityPair, EntityPairHash> m_StepInitialActive;
		std::vector<EntityPair> m_StepTouched;
		std::unordered_set<EntityPair, EntityPairHash> m_StepTouchedSet;
		std::unordered_set<EntityPair, EntityPairHash> m_StepSawBegin;
		std::unordered_set<EntityPair, EntityPairHash> m_StepSawEnd;
		std::unordered_set<EntityPair, EntityPairHash> m_RebuildCarryActive;
	};

	class SceneContactFilter2D final : public b2ContactFilter
	{
	public:
		explicit SceneContactFilter2D(Scene* scene)
			: m_Scene(scene)
		{
		}

		bool ShouldCollide(b2Fixture* fixtureA, b2Fixture* fixtureB) override
		{
			// Fixture category/mask filtering remains an independent first gate.
			if (!b2ContactFilter::ShouldCollide(fixtureA, fixtureB))
				return false;
			if (!m_Scene || !fixtureA || !fixtureB
				|| !fixtureA->GetBody() || !fixtureB->GetBody())
				return false;

			const UUID uuidA(static_cast<uint64_t>(
				fixtureA->GetBody()->GetUserData().pointer));
			const UUID uuidB(static_cast<uint64_t>(
				fixtureB->GetBody()->GetUserData().pointer));
			Entity entityA = m_Scene->FindEntityByUUID(uuidA);
			Entity entityB = m_Scene->FindEntityByUUID(uuidB);
			if (!entityA || !entityB || !entityA.HasComponent<EntityMetadata>()
				|| !entityB.HasComponent<EntityMetadata>())
				return false;

			const uint8_t layerA = entityA.GetComponent<EntityMetadata>().Layer;
			const uint8_t layerB = entityB.GetComponent<EntityMetadata>().Layer;
			// Project files and cooked packages reject asymmetric matrices. Requiring
			// both directed bits here also keeps direct Scene API input deterministic.
			return m_Scene->m_Physics2DSettings.CanLayersCollide(layerA, layerB)
				&& m_Scene->m_Physics2DSettings.CanLayersCollide(layerB, layerA);
		}

	private:
		Scene* m_Scene = nullptr;
	};

	namespace {

		bool IsValidPhysicsMaterial(float density, float friction, float restitution)
		{
			return std::isfinite(density) && density >= 0.0f
				&& std::isfinite(friction) && friction >= 0.0f
				&& std::isfinite(restitution) && restitution >= 0.0f && restitution <= 1.0f;
		}

		void HashPhysicsValue(uint64_t& hash, uint64_t value)
		{
			// 64-bit FNV-1a over a fixed-width value.
			for (uint32_t byte = 0; byte < 8; ++byte)
			{
				hash ^= (value >> (byte * 8)) & 0xffu;
				hash *= 1099511628211ull;
			}
		}

		void HashPhysicsFloat(uint64_t& hash, float value)
		{
			HashPhysicsValue(hash, std::bit_cast<uint32_t>(value));
		}

		struct RuntimeBodyState
		{
			b2Vec2 Position{ 0.0f, 0.0f };
			float Angle = 0.0f;
			b2Vec2 LinearVelocity{ 0.0f, 0.0f };
			float AngularVelocity = 0.0f;
			bool Awake = true;
		};

		glm::mat4 MakeColliderDebugTransform(const glm::vec2& center, float z,
			float rotation, const glm::vec2& size)
		{
			return glm::translate(glm::mat4(1.0f), glm::vec3(center, z))
				* glm::rotate(glm::mat4(1.0f), rotation, glm::vec3(0.0f, 0.0f, 1.0f))
				* glm::scale(glm::mat4(1.0f), glm::vec3(size, 1.0f));
		}

		bool IsValidColliderTransform2D(const Transform& transform)
		{
			return std::isfinite(transform._Translation.x)
				&& std::isfinite(transform._Translation.y)
				&& std::isfinite(transform._Translation.z)
				&& std::isfinite(transform._Rotation.z)
				&& std::isfinite(transform._Scale.x)
				&& std::isfinite(transform._Scale.y);
		}

		glm::vec2 TransformColliderOffset2D(const Transform& transform, const glm::vec2& offset)
		{
			const glm::vec2 scaledOffset = offset * glm::vec2(transform._Scale);
			const float cosine = std::cos(transform._Rotation.z);
			const float sine = std::sin(transform._Rotation.z);
			return glm::vec2(transform._Translation)
				+ glm::vec2(cosine * scaledOffset.x - sine * scaledOffset.y,
					sine * scaledOffset.x + cosine * scaledOffset.y);
		}

		bool IsFinite(const glm::vec3& value)
		{
			return std::isfinite(value.x) && std::isfinite(value.y) && std::isfinite(value.z);
		}

		bool IsFinite(const glm::vec2& value)
		{
			return std::isfinite(value.x) && std::isfinite(value.y);
		}

		bool TryResolveEntityLayerBit(Scene* scene, b2Fixture* fixture,
			UUID& entityID, uint16_t& layerBit)
		{
			if (!scene || !fixture)
				return false;
			b2Body* body = fixture->GetBody();
			const uint64_t rawUUID = body
				? static_cast<uint64_t>(body->GetUserData().pointer)
				: 0;
			if (rawUUID == 0)
				return false;

			Entity entity = scene->FindEntityByUUID(UUID(rawUUID));
			if (!entity || !entity.HasComponent<EntityMetadata>())
				return false;
			const uint8_t layer = entity.GetComponent<EntityMetadata>().Layer;
			if (layer >= Physics2DLayerCount)
				return false;

			entityID = UUID(rawUUID);
			layerBit = static_cast<uint16_t>(uint16_t(1) << layer);
			return true;
		}

		class ClosestRaycastCallback2D final : public b2RayCastCallback
		{
		public:
			ClosestRaycastCallback2D(Scene* scene, uint16_t layerMask, bool includeTriggers)
				: m_Scene(scene), m_LayerMask(layerMask), m_IncludeTriggers(includeTriggers)
			{
			}

			float ReportFixture(b2Fixture* fixture, const b2Vec2& point,
				const b2Vec2& normal, float fraction) override
			{
				if (!fixture || (!m_IncludeTriggers && fixture->IsSensor()))
					return -1.0f;
				UUID entityID{ 0 };
				uint16_t layerBit = 0;
				if (!TryResolveEntityLayerBit(m_Scene, fixture, entityID, layerBit)
					|| (layerBit & m_LayerMask) == 0)
					return -1.0f;

				Hit = RaycastHit2D{ entityID, { point.x, point.y },
					{ normal.x, normal.y }, fraction, fixture->IsSensor(), layerBit };
				return fraction;
			}

			std::optional<RaycastHit2D> Hit;

		private:
			Scene* m_Scene = nullptr;
			uint16_t m_LayerMask = 0xFFFF;
			bool m_IncludeTriggers = true;
		};

		class AABBQueryCallback2D final : public b2QueryCallback
		{
		public:
			AABBQueryCallback2D(Scene* scene, uint16_t layerMask, bool includeTriggers)
				: m_Scene(scene), m_LayerMask(layerMask), m_IncludeTriggers(includeTriggers)
			{
			}

			bool ReportFixture(b2Fixture* fixture) override
			{
				if (!fixture || (!m_IncludeTriggers && fixture->IsSensor()))
					return true;
				UUID entityID{ 0 };
				uint16_t layerBit = 0;
				if (TryResolveEntityLayerBit(m_Scene, fixture, entityID, layerBit)
					&& (layerBit & m_LayerMask) != 0)
					Hits.push_back({ entityID, fixture->IsSensor(), layerBit });
				return true;
			}

			std::vector<PhysicsQueryHit2D> Hits;

		private:
			Scene* m_Scene = nullptr;
			uint16_t m_LayerMask = 0xFFFF;
			bool m_IncludeTriggers = true;
		};

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

	void Scene::SetPhysics2DSettings(const Physics2DSettings& settings)
	{
		if (m_Physics2DSettings == settings)
			return;
		m_Physics2DSettings = settings;
		// Existing Box2D contacts must be re-evaluated after a matrix change.
		// Reuse the normal safe definition boundary instead of mutating a locked world.
		if (m_RuntimeRunning)
			m_HasRuntimePhysicsDefinition = false;
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

	static Entity DuplicateEntityRecursive(Scene* scene, Entity source, Entity parent,
		std::unordered_map<UUID, UUID>& duplicateUUIDs)
	{
		if (!scene || !source)
			return {};

		Entity duplicate = scene->CreateEntity(source.GetName());
		std::string copyError;
		if (!ComponentCodecs::CopyAuthoringComponents(source, duplicate, false,
			copyError))
		{
			TC_Core_Error("Could not duplicate entity '{0}': {1}",
				source.GetName(), copyError);
			scene->DestroyEntity(duplicate);
			return {};
		}
		duplicateUUIDs[source.GetUUID()] = duplicate.GetUUID();

		if (parent && !scene->SetParent(duplicate, parent))
		{
			scene->DestroyEntity(duplicate);
			return {};
		}

		for (UUID childUUID : scene->GetChildrenUUIDs(source))
		{
			Entity child = scene->FindEntityByUUID(childUUID);
			if (child && !DuplicateEntityRecursive(scene, child, duplicate, duplicateUUIDs))
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
				|| !sourceEntity.HasComponent<EntityMetadata>()
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
		newScene->m_Physics2DSettings = other->m_Physics2DSettings;

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
		CopyComponent<EntityMetadata>(dstSceneRegistry, srcSceneRegistry, enttMap);
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
		CopyComponent<CSharpScripts>(dstSceneRegistry, srcSceneRegistry, enttMap);
		CopyComponent<Rigidbody2D>(dstSceneRegistry, srcSceneRegistry, enttMap);
		CopyComponent<BoxCollider2D>(dstSceneRegistry, srcSceneRegistry, enttMap);
		CopyComponent<CircleCollider2D>(dstSceneRegistry, srcSceneRegistry, enttMap);
		CopyComponent<DistanceJoint2D>(dstSceneRegistry, srcSceneRegistry, enttMap);

		for (auto entity : dstSceneRegistry.view<Rigidbody2D>())
			dstSceneRegistry.get<Rigidbody2D>(entity).RuntimeBody = nullptr;
		for (auto entity : dstSceneRegistry.view<BoxCollider2D>())
			dstSceneRegistry.get<BoxCollider2D>(entity).RuntimeFixture = nullptr;
		for (auto entity : dstSceneRegistry.view<CircleCollider2D>())
			dstSceneRegistry.get<CircleCollider2D>(entity).RuntimeFixture = nullptr;
		for (auto entity : dstSceneRegistry.view<DistanceJoint2D>())
			dstSceneRegistry.get<DistanceJoint2D>(entity).RuntimeJoint = nullptr;

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
		entity.AddComponent<EntityMetadata>();
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
		if (std::find(m_EntitiesBeingDestroyed.begin(), m_EntitiesBeingDestroyed.end(), entityUUID)
			!= m_EntitiesBeingDestroyed.end())
			return;
		m_EntitiesBeingDestroyed.push_back(entityUUID);
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

		// Keep the Entity and its serialized attachment records alive while managed
		// OnDisable/OnDestroy execute. ScriptEngine is a no-op for edit-time scenes.
		Scripting::ScriptEngine::Get().NotifyEntityDestroyed(
			*this, static_cast<uint64_t>(entityUUID));

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

		if (entity.HasComponent<BoxCollider2D>())
			entity.GetComponent<BoxCollider2D>().RuntimeFixture = nullptr;
		if (entity.HasComponent<CircleCollider2D>())
			entity.GetComponent<CircleCollider2D>().RuntimeFixture = nullptr;
		if (entity.HasComponent<DistanceJoint2D>())
			entity.GetComponent<DistanceJoint2D>().RuntimeJoint = nullptr;
		if (entity.HasComponent<Rigidbody2D>())
			entity.GetComponent<Rigidbody2D>().RuntimeBody = nullptr;

		bool affectsRuntimePhysics = m_RuntimeBodies.find(entityUUID) != m_RuntimeBodies.end()
			|| entity.HasComponent<Rigidbody2D>() || entity.HasComponent<BoxCollider2D>()
			|| entity.HasComponent<CircleCollider2D>() || entity.HasComponent<DistanceJoint2D>();
		for (entt::entity jointEntity : m_Registry.view<DistanceJoint2D>())
		{
			auto& joint = m_Registry.get<DistanceJoint2D>(jointEntity);
			if (joint.ConnectedEntity == entityUUID)
			{
				// Match Unity's Connected Body=None behavior and keep the authoring
				// scene serializable after deleting a joint endpoint.
				joint.ConnectedEntity = UUID(0);
				joint.RuntimeJoint = nullptr;
				affectsRuntimePhysics = true;
			}
		}

		// Defer Box2D destruction to the next safe synchronization point. This
		// avoids invalidating joints implicitly (DestroyBody destroys every attached
		// joint) and also keeps all world mutation outside a locked Step callback.
		m_RuntimeBodies.erase(entityUUID);
		if (affectsRuntimePhysics)
			m_HasRuntimePhysicsDefinition = false;
		// The entity is being removed, so discard every queued/active pair that
		// references it rather than exposing a stale UUID during dispatch.
		if (m_ContactListener)
			m_ContactListener->DiscardEntity(entityUUID);

		m_EntityMap.erase(entityUUID);
		m_Registry.destroy(entity);
		m_EntitiesBeingDestroyed.erase(std::remove(m_EntitiesBeingDestroyed.begin(),
			m_EntitiesBeingDestroyed.end(), entityUUID), m_EntitiesBeingDestroyed.end());
	}

	void Scene::SetRuntimeEntityBatchCreatedCallback(
		RuntimeEntityBatchCreatedCallback callback)
	{
		m_RuntimeEntityBatchCreatedCallback = std::move(callback);
	}

	void Scene::ArmRuntimeScriptBatchCallback()
	{
		const uint64_t runtimeGeneration = m_RuntimeSessionGeneration;
		SetRuntimeEntityBatchCreatedCallback(
			[this, runtimeGeneration](std::span<const UUID> entityIDs)
			{
				if (!m_RuntimeRunning || m_ScriptSceneSessionID != 0
					|| runtimeGeneration != m_RuntimeSessionGeneration)
					return;

				const bool hasManagedAttachments = std::any_of(entityIDs.begin(),
					entityIDs.end(), [this](UUID entityID)
					{
						Entity entity = FindEntityByUUID(entityID);
						return entity && entity.HasComponent<CSharpScripts>()
							&& !entity.GetComponent<CSharpScripts>().Scripts.empty();
					});
				if (!hasManagedAttachments)
					return;

				m_ScriptSceneSessionID =
					Scripting::ScriptEngine::Get().StartSceneForRuntimeBatch(
						*this, runtimeGeneration, entityIDs);
				if (m_ScriptSceneSessionID != 0)
					return;

				TC_Core_Error("A scripted runtime entity batch was rolled back because the managed runtime or current project assembly is unavailable");
				// StartSceneCore replaces the lazy callback while attempting its
				// transaction and clears it on failure. Re-arm so a later valid batch
				// can retry without restarting the native Scene.
				if (m_RuntimeRunning && runtimeGeneration == m_RuntimeSessionGeneration)
					ArmRuntimeScriptBatchCallback();
			});
	}

	void Scene::QueueRuntimeEntityBatchCreated(std::vector<UUID> entityIDs)
	{
		if (!m_RuntimeRunning || entityIDs.empty())
			return;
		for (UUID entityID : entityIDs)
		{
			if (static_cast<uint64_t>(entityID) != 0 && FindEntityByUUID(entityID))
				m_PendingRuntimeEntityCreates.push_back(entityID);
		}
		if (!m_PendingRuntimeEntityCreates.empty())
			m_HasRuntimePhysicsDefinition = false;
	}

	void Scene::FlushPendingRuntimeEntityCreates()
	{
		if (!m_RuntimeRunning)
		{
			m_PendingRuntimeEntityCreates.clear();
			return;
		}
		if (m_FlushingRuntimeEntityCreates || m_PendingRuntimeEntityCreates.empty())
			return;
		// This function is the authoritative safe-point commit. Never expose a
		// just-created entity to managed OnCreate until its physics proxies exist.
		// Keep the pending batch intact when physics cannot be synchronized so the
		// next safe point can retry instead of silently losing lifecycle delivery.
		if (!SynchronizeRuntimePhysicsDefinitions())
		{
			TC_Core_Error("Could not synchronize runtime physics before delivering a created-entity batch");
			return;
		}

		std::vector<UUID> batch;
		batch.swap(m_PendingRuntimeEntityCreates);
		batch.erase(std::remove_if(batch.begin(), batch.end(), [this](UUID entityID)
		{
			return !FindEntityByUUID(entityID);
		}), batch.end());
		if (batch.empty())
			return;

		// Copy the callback before invoking it. Lazy managed-runtime startup
		// replaces this callback from inside the call, and the active target must
		// remain alive until it returns.
		RuntimeEntityBatchCreatedCallback callback =
			m_RuntimeEntityBatchCreatedCallback;
		if (!callback)
		{
			const bool hasManagedAttachments = std::any_of(batch.begin(), batch.end(),
				[this](UUID entityID)
				{
					Entity entity = FindEntityByUUID(entityID);
					return entity && entity.HasComponent<CSharpScripts>()
						&& !entity.GetComponent<CSharpScripts>().Scripts.empty();
				});
			if (!hasManagedAttachments)
				return;

			// This is a defensive invariant path (normal runtime startup always
			// installs either a lazy or active callback). Never leave scripted
			// entities alive without their managed lifecycle.
			TC_Core_Error("Discarding a scripted runtime entity batch because no managed batch callback is installed");
			for (UUID entityID : batch)
			{
				Entity entity = FindEntityByUUID(entityID);
				if (entity && entity.HasComponent<CSharpScripts>())
					entity.RemoveComponent<CSharpScripts>();
			}
			for (auto iterator = batch.rbegin(); iterator != batch.rend(); ++iterator)
			{
				Entity entity = FindEntityByUUID(*iterator);
				if (entity)
					DestroyEntity(entity);
			}
			if (!SynchronizeRuntimePhysicsDefinitions())
				TC_Core_Error("Could not synchronize runtime physics after rolling back an undeliverable entity batch");
			return;
		}

		m_FlushingRuntimeEntityCreates = true;
		try
		{
			callback(batch);
		}
		catch (const std::exception& exception)
		{
			TC_Core_Error("Runtime entity-created callback failed: {0}", exception.what());
		}
		catch (...)
		{
			TC_Core_Error("Runtime entity-created callback failed with an unknown exception");
		}
		m_FlushingRuntimeEntityCreates = false;
		// OnCreate/OnEnable may mutate authoring physics or queue another Prefab.
		// Reconcile those changes before returning from the same safe boundary;
		// any newly queued lifecycle batch remains pending for a later flush.
		if (!SynchronizeRuntimePhysicsDefinitions())
			TC_Core_Error("Could not synchronize runtime physics after delivering a created-entity batch");
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

	Scene::CollisionListenerHandle Scene::AddCollisionEnter2DListener(
		CollisionEnter2DCallback callback)
	{
		if (!callback)
			return 0;

		CollisionListenerHandle handle = 0;
		do
		{
			handle = m_NextCollisionListenerHandle++;
		} while (handle == 0 || m_CollisionEnterListeners.find(handle) != m_CollisionEnterListeners.end()
			|| m_CollisionExitListeners.find(handle) != m_CollisionExitListeners.end()
			|| m_TriggerEnterListeners.find(handle) != m_TriggerEnterListeners.end()
			|| m_TriggerExitListeners.find(handle) != m_TriggerExitListeners.end());
		m_CollisionEnterListeners.emplace(handle, std::move(callback));
		return handle;
	}

	Scene::CollisionListenerHandle Scene::AddCollisionExit2DListener(
		CollisionExit2DCallback callback)
	{
		if (!callback)
			return 0;

		CollisionListenerHandle handle = 0;
		do
		{
			handle = m_NextCollisionListenerHandle++;
		} while (handle == 0 || m_CollisionEnterListeners.find(handle) != m_CollisionEnterListeners.end()
			|| m_CollisionExitListeners.find(handle) != m_CollisionExitListeners.end()
			|| m_TriggerEnterListeners.find(handle) != m_TriggerEnterListeners.end()
			|| m_TriggerExitListeners.find(handle) != m_TriggerExitListeners.end());
		m_CollisionExitListeners.emplace(handle, std::move(callback));
		return handle;
	}

	Scene::CollisionListenerHandle Scene::AddTriggerEnter2DListener(
		TriggerEnter2DCallback callback)
	{
		if (!callback)
			return 0;

		CollisionListenerHandle handle = 0;
		do
		{
			handle = m_NextCollisionListenerHandle++;
		} while (handle == 0 || m_CollisionEnterListeners.find(handle) != m_CollisionEnterListeners.end()
			|| m_CollisionExitListeners.find(handle) != m_CollisionExitListeners.end()
			|| m_TriggerEnterListeners.find(handle) != m_TriggerEnterListeners.end()
			|| m_TriggerExitListeners.find(handle) != m_TriggerExitListeners.end());
		m_TriggerEnterListeners.emplace(handle, std::move(callback));
		return handle;
	}

	Scene::CollisionListenerHandle Scene::AddTriggerExit2DListener(
		TriggerExit2DCallback callback)
	{
		if (!callback)
			return 0;

		CollisionListenerHandle handle = 0;
		do
		{
			handle = m_NextCollisionListenerHandle++;
		} while (handle == 0 || m_CollisionEnterListeners.find(handle) != m_CollisionEnterListeners.end()
			|| m_CollisionExitListeners.find(handle) != m_CollisionExitListeners.end()
			|| m_TriggerEnterListeners.find(handle) != m_TriggerEnterListeners.end()
			|| m_TriggerExitListeners.find(handle) != m_TriggerExitListeners.end());
		m_TriggerExitListeners.emplace(handle, std::move(callback));
		return handle;
	}

	bool Scene::RemoveCollision2DListener(CollisionListenerHandle handle)
	{
		if (handle == 0)
			return false;
		const size_t enterRemoved = m_CollisionEnterListeners.erase(handle);
		const size_t exitRemoved = m_CollisionExitListeners.erase(handle);
		const size_t triggerEnterRemoved = m_TriggerEnterListeners.erase(handle);
		const size_t triggerExitRemoved = m_TriggerExitListeners.erase(handle);
		return enterRemoved != 0 || exitRemoved != 0
			|| triggerEnterRemoved != 0 || triggerExitRemoved != 0;
	}

	void Scene::DispatchPendingCollisionEvents()
	{
		if (!m_ContactListener)
			return;

		const uint64_t runtimeGeneration = m_RuntimeSessionGeneration;
		const std::vector<SceneContactListener::PendingEvent> events =
			m_ContactListener->TakePendingEvents();
		if (m_ScriptSceneSessionID != 0 && !events.empty())
		{
			std::vector<Scripting::NativePhysicsEventV1> managedEvents;
			managedEvents.reserve(events.size());
			for (const SceneContactListener::PendingEvent& pending : events)
			{
				Scripting::NativePhysicsEventV1 event;
				if (pending.Pair.IsTrigger)
					event.Kind = static_cast<uint32_t>(pending.Type
						== SceneContactListener::PendingType::Enter
						? Scripting::NativePhysicsEventKind::TriggerEnter
						: Scripting::NativePhysicsEventKind::TriggerExit);
				else
					event.Kind = static_cast<uint32_t>(pending.Type
						== SceneContactListener::PendingType::Enter
						? Scripting::NativePhysicsEventKind::CollisionEnter
						: Scripting::NativePhysicsEventKind::CollisionExit);
				event.EntityA = { m_ScriptSceneSessionID, pending.Pair.A,
					m_RuntimeSessionGeneration };
				event.EntityB = { m_ScriptSceneSessionID, pending.Pair.B,
					m_RuntimeSessionGeneration };
				managedEvents.push_back(event);
			}
			Scripting::ScriptEngine::Get().DispatchPhysicsEvents(
				m_ScriptSceneSessionID, managedEvents);
		}

		// Snapshot callbacks so listener add/remove operations are iteration-safe.
		// A removed handle is checked again before invocation; listeners added by a
		// callback begin receiving events on the next dispatch batch.
		std::vector<std::pair<CollisionListenerHandle, CollisionEnter2DCallback>> enterListeners;
		enterListeners.reserve(m_CollisionEnterListeners.size());
		for (const auto& listener : m_CollisionEnterListeners)
			enterListeners.push_back(listener);
		std::vector<std::pair<CollisionListenerHandle, CollisionExit2DCallback>> exitListeners;
		exitListeners.reserve(m_CollisionExitListeners.size());
		for (const auto& listener : m_CollisionExitListeners)
			exitListeners.push_back(listener);
		std::vector<std::pair<CollisionListenerHandle, TriggerEnter2DCallback>> triggerEnterListeners;
		triggerEnterListeners.reserve(m_TriggerEnterListeners.size());
		for (const auto& listener : m_TriggerEnterListeners)
			triggerEnterListeners.push_back(listener);
		std::vector<std::pair<CollisionListenerHandle, TriggerExit2DCallback>> triggerExitListeners;
		triggerExitListeners.reserve(m_TriggerExitListeners.size());
		for (const auto& listener : m_TriggerExitListeners)
			triggerExitListeners.push_back(listener);
		std::sort(enterListeners.begin(), enterListeners.end(),
			[](const auto& left, const auto& right) { return left.first < right.first; });
		std::sort(exitListeners.begin(), exitListeners.end(),
			[](const auto& left, const auto& right) { return left.first < right.first; });
		std::sort(triggerEnterListeners.begin(), triggerEnterListeners.end(),
			[](const auto& left, const auto& right) { return left.first < right.first; });
		std::sort(triggerExitListeners.begin(), triggerExitListeners.end(),
			[](const auto& left, const auto& right) { return left.first < right.first; });

		for (const SceneContactListener::PendingEvent& pending : events)
		{
			if (!m_RuntimeRunning || runtimeGeneration != m_RuntimeSessionGeneration)
				break;

			const UUID uuidA(pending.Pair.A);
			const UUID uuidB(pending.Pair.B);
			const Entity originalA = FindEntityByUUID(uuidA);
			const Entity originalB = FindEntityByUUID(uuidB);
			if (!originalA || !originalB)
				continue;

			auto pairStillExists = [&]()
			{
				return m_RuntimeRunning && runtimeGeneration == m_RuntimeSessionGeneration
					&& FindEntityByUUID(uuidA) == originalA && FindEntityByUUID(uuidB) == originalB;
			};

			if (pending.Pair.IsTrigger)
			{
				if (pending.Type == SceneContactListener::PendingType::Enter)
				{
					const TriggerEnter2D event{ uuidA, uuidB };
					for (const auto& [handle, callback] : triggerEnterListeners)
					{
						if (!pairStillExists())
							break;
						if (m_TriggerEnterListeners.find(handle) != m_TriggerEnterListeners.end())
							callback(event);
					}
				}
				else
				{
					const TriggerExit2D event{ uuidA, uuidB };
					for (const auto& [handle, callback] : triggerExitListeners)
					{
						if (!pairStillExists())
							break;
						if (m_TriggerExitListeners.find(handle) != m_TriggerExitListeners.end())
							callback(event);
					}
				}
			}
			else if (pending.Type == SceneContactListener::PendingType::Enter)
			{
				const CollisionEnter2D event{ uuidA, uuidB };
				for (const auto& [handle, callback] : enterListeners)
				{
					if (!pairStillExists())
						break;
					if (m_CollisionEnterListeners.find(handle) != m_CollisionEnterListeners.end())
						callback(event);
				}
			}
			else
			{
				const CollisionExit2D event{ uuidA, uuidB };
				for (const auto& [handle, callback] : exitListeners)
				{
					if (!pairStillExists())
						break;
					if (m_CollisionExitListeners.find(handle) != m_CollisionExitListeners.end())
						callback(event);
				}
			}
		}
	}

	void Scene::ResetRuntimePhysicsPointers()
	{
		for (entt::entity entity : m_Registry.view<Rigidbody2D>())
			m_Registry.get<Rigidbody2D>(entity).RuntimeBody = nullptr;
		for (entt::entity entity : m_Registry.view<BoxCollider2D>())
			m_Registry.get<BoxCollider2D>(entity).RuntimeFixture = nullptr;
		for (entt::entity entity : m_Registry.view<CircleCollider2D>())
			m_Registry.get<CircleCollider2D>(entity).RuntimeFixture = nullptr;
		for (entt::entity entity : m_Registry.view<DistanceJoint2D>())
			m_Registry.get<DistanceJoint2D>(entity).RuntimeJoint = nullptr;
	}

	b2Body* Scene::FindRuntimeBody(UUID entityID) const
	{
		auto bodyIt = m_RuntimeBodies.find(entityID);
		return bodyIt == m_RuntimeBodies.end() ? nullptr : bodyIt->second;
	}

	uint64_t Scene::ComputeRuntimePhysicsDefinitionHash() const
	{
		uint64_t hash = 1469598103934665603ull;
		for (uint16_t mask : m_Physics2DSettings.CollisionMasks)
			HashPhysicsValue(hash, mask);
		std::unordered_set<UUID> physicsEntities;
		for (UUID uuid : m_EntityOrder)
		{
			auto mapIt = m_EntityMap.find(uuid);
			if (mapIt == m_EntityMap.end() || !m_Registry.valid(mapIt->second))
				continue;
			const entt::entity entity = mapIt->second;
			if (m_Registry.any_of<Rigidbody2D, BoxCollider2D, CircleCollider2D, DistanceJoint2D>(entity))
				physicsEntities.insert(uuid);
			if (m_Registry.all_of<DistanceJoint2D>(entity))
			{
				const UUID connected = m_Registry.get<DistanceJoint2D>(entity).ConnectedEntity;
				if (static_cast<uint64_t>(connected) != 0)
					physicsEntities.insert(connected);
			}
		}

		HashPhysicsValue(hash, physicsEntities.size());
		for (UUID uuid : m_EntityOrder)
		{
			if (physicsEntities.find(uuid) == physicsEntities.end())
				continue;
			auto mapIt = m_EntityMap.find(uuid);
			if (mapIt == m_EntityMap.end() || !m_Registry.valid(mapIt->second))
				continue;
			const entt::entity entity = mapIt->second;
			HashPhysicsValue(hash, static_cast<uint64_t>(uuid));
			const bool hasMetadata = m_Registry.all_of<EntityMetadata>(entity);
			HashPhysicsValue(hash, hasMetadata);
			if (hasMetadata)
				HashPhysicsValue(hash, m_Registry.get<EntityMetadata>(entity).Layer);

			const bool hasTransform = m_Registry.all_of<Transform>(entity);
			HashPhysicsValue(hash, hasTransform);
			if (hasTransform)
			{
				const auto& transform = m_Registry.get<Transform>(entity);
				// The accepted hash is refreshed immediately after physics writes its
				// pose back. Any subsequent change therefore represents an authored or
				// scripted teleport and is applied at the next safe step boundary.
				HashPhysicsFloat(hash, transform._Translation.x);
				HashPhysicsFloat(hash, transform._Translation.y);
				HashPhysicsFloat(hash, transform._Rotation.z);
				HashPhysicsFloat(hash, transform._Scale.x);
				HashPhysicsFloat(hash, transform._Scale.y);
			}

			const bool hasRigidbody = m_Registry.all_of<Rigidbody2D>(entity);
			HashPhysicsValue(hash, hasRigidbody);
			if (hasRigidbody)
			{
				const auto& rigidbody = m_Registry.get<Rigidbody2D>(entity);
				HashPhysicsValue(hash, rigidbody.Enabled);
				HashPhysicsValue(hash, static_cast<uint64_t>(rigidbody.Type));
				HashPhysicsValue(hash, rigidbody.FixedRotation);
			}

			const bool hasBox = m_Registry.all_of<BoxCollider2D>(entity);
			HashPhysicsValue(hash, hasBox);
			if (hasBox)
			{
				const auto& collider = m_Registry.get<BoxCollider2D>(entity);
				HashPhysicsValue(hash, collider.Enabled);
				HashPhysicsValue(hash, collider.IsTrigger);
				HashPhysicsValue(hash, collider.CollisionLayer);
				HashPhysicsValue(hash, collider.CollisionMask);
				HashPhysicsFloat(hash, collider.Offset.x);
				HashPhysicsFloat(hash, collider.Offset.y);
				HashPhysicsFloat(hash, collider.Size.x);
				HashPhysicsFloat(hash, collider.Size.y);
				HashPhysicsFloat(hash, collider.Density);
				HashPhysicsFloat(hash, collider.Friction);
				HashPhysicsFloat(hash, collider.Restitution);
				HashPhysicsFloat(hash, collider.RestitutionThreshold);
			}

			const bool hasCircle = m_Registry.all_of<CircleCollider2D>(entity);
			HashPhysicsValue(hash, hasCircle);
			if (hasCircle)
			{
				const auto& collider = m_Registry.get<CircleCollider2D>(entity);
				HashPhysicsValue(hash, collider.Enabled);
				HashPhysicsValue(hash, collider.IsTrigger);
				HashPhysicsValue(hash, collider.CollisionLayer);
				HashPhysicsValue(hash, collider.CollisionMask);
				HashPhysicsFloat(hash, collider.Offset.x);
				HashPhysicsFloat(hash, collider.Offset.y);
				HashPhysicsFloat(hash, collider.Radius);
				HashPhysicsFloat(hash, collider.Density);
				HashPhysicsFloat(hash, collider.Friction);
				HashPhysicsFloat(hash, collider.Restitution);
			}

			const bool hasJoint = m_Registry.all_of<DistanceJoint2D>(entity);
			HashPhysicsValue(hash, hasJoint);
			if (hasJoint)
			{
				const auto& joint = m_Registry.get<DistanceJoint2D>(entity);
				HashPhysicsValue(hash, joint.Enabled);
				HashPhysicsValue(hash, static_cast<uint64_t>(joint.ConnectedEntity));
				HashPhysicsFloat(hash, joint.Anchor.x);
				HashPhysicsFloat(hash, joint.Anchor.y);
				HashPhysicsFloat(hash, joint.ConnectedAnchor.x);
				HashPhysicsFloat(hash, joint.ConnectedAnchor.y);
				HashPhysicsFloat(hash, joint.Distance);
				HashPhysicsFloat(hash, joint.Frequency);
				HashPhysicsFloat(hash, joint.Damping);
				HashPhysicsValue(hash, joint.CollideConnected);
			}
		}
		return hash;
	}

	bool Scene::RebuildRuntimePhysicsWorld(bool preserveState)
	{
		if (!m_RuntimeRunning || !m_ContactListener || !m_ContactFilter)
			return false;
		if (m_PhysicsWorld && m_PhysicsWorld->IsLocked())
			return false;

		std::unordered_map<UUID, RuntimeBodyState> previousStates;
		if (preserveState)
		{
			previousStates.reserve(m_RuntimeBodies.size());
			for (const auto& [uuid, body] : m_RuntimeBodies)
			{
				if (!body)
					continue;
				previousStates.emplace(uuid, RuntimeBodyState{ body->GetPosition(), body->GetAngle(),
					body->GetLinearVelocity(), body->GetAngularVelocity(), body->IsAwake() });
			}
		}

		if (m_PhysicsWorld)
		{
			m_PhysicsWorld->SetContactListener(nullptr);
			m_ContactListener->PrepareForWorldRebuild();
			delete m_PhysicsWorld;
		}
		ResetRuntimePhysicsPointers();
		m_RuntimeBodies.clear();
		m_PhysicsWorld = new b2World({ 0.0f, -9.8f });
		m_PhysicsWorld->SetContactFilter(m_ContactFilter);
		m_PhysicsWorld->SetContactListener(m_ContactListener);

		std::unordered_set<UUID> requiredBodies;
		for (UUID uuid : m_EntityOrder)
		{
			Entity entity = FindEntityByUUID(uuid);
			if (!entity)
				continue;
			const bool hasRigidbody = entity.HasComponent<Rigidbody2D>();
			const bool rigidbodyEnabled = hasRigidbody && entity.GetComponent<Rigidbody2D>().Enabled;
			const bool colliderEnabled = (entity.HasComponent<BoxCollider2D>()
				&& entity.GetComponent<BoxCollider2D>().Enabled)
				|| (entity.HasComponent<CircleCollider2D>()
					&& entity.GetComponent<CircleCollider2D>().Enabled);
			if (rigidbodyEnabled || (!hasRigidbody && colliderEnabled))
				requiredBodies.insert(uuid);
		}

		for (UUID uuid : m_EntityOrder)
		{
			Entity entity = FindEntityByUUID(uuid);
			if (!entity || !entity.HasComponent<DistanceJoint2D>())
				continue;
			const auto& joint = entity.GetComponent<DistanceJoint2D>();
			Entity connected = FindEntityByUUID(joint.ConnectedEntity);
			if (!joint.Enabled || !connected || joint.ConnectedEntity == uuid)
				continue;
			const bool ownerBlocked = entity.HasComponent<Rigidbody2D>()
				&& !entity.GetComponent<Rigidbody2D>().Enabled;
			const bool connectedBlocked = connected.HasComponent<Rigidbody2D>()
				&& !connected.GetComponent<Rigidbody2D>().Enabled;
			if (!ownerBlocked && !connectedBlocked)
			{
				requiredBodies.insert(uuid);
				requiredBodies.insert(joint.ConnectedEntity);
			}
		}

		for (UUID uuid : m_EntityOrder)
		{
			if (requiredBodies.find(uuid) == requiredBodies.end())
				continue;
			Entity entity = FindEntityByUUID(uuid);
			if (!entity || !entity.HasComponent<Transform>())
				continue;
			auto& transform = entity.GetComponent<Transform>();
			if (!std::isfinite(transform._Translation.x) || !std::isfinite(transform._Translation.y)
				|| !std::isfinite(transform._Rotation.z))
			{
				TC_Core_Warn("Skipping 2D physics body with a non-finite transform on entity '{0}'",
					entity.GetName());
				continue;
			}

			b2BodyDef bodyDef;
			if (entity.HasComponent<Rigidbody2D>() && entity.GetComponent<Rigidbody2D>().Enabled)
			{
				const auto& rigidbody = entity.GetComponent<Rigidbody2D>();
				bodyDef.type = Rigidbody2DTypeToBox2DBody(rigidbody.Type);
				bodyDef.fixedRotation = rigidbody.FixedRotation;
			}
			else
				bodyDef.type = b2_staticBody;
			bodyDef.position.Set(transform._Translation.x, transform._Translation.y);
			bodyDef.angle = transform._Rotation.z;
			if (auto stateIt = previousStates.find(uuid); stateIt != previousStates.end())
			{
				// ECS is the authoritative pose at a definition boundary. It already
				// contains the last physics pose unless a script intentionally moved the
				// entity. Preserve motion state only for bodies that can move.
				if (bodyDef.type != b2_staticBody)
				{
					bodyDef.linearVelocity = stateIt->second.LinearVelocity;
					bodyDef.angularVelocity = bodyDef.fixedRotation
						? 0.0f
						: stateIt->second.AngularVelocity;
				}
				bodyDef.awake = stateIt->second.Awake;
			}
			bodyDef.userData.pointer = static_cast<uintptr_t>(static_cast<uint64_t>(uuid));

			b2Body* body = m_PhysicsWorld->CreateBody(&bodyDef);
			m_RuntimeBodies.emplace(uuid, body);
			if (entity.HasComponent<Rigidbody2D>() && entity.GetComponent<Rigidbody2D>().Enabled)
				entity.GetComponent<Rigidbody2D>().RuntimeBody = body;
		}

		for (UUID uuid : m_EntityOrder)
		{
			b2Body* body = FindRuntimeBody(uuid);
			Entity entity = FindEntityByUUID(uuid);
			if (!body || !entity || !entity.HasComponent<Transform>())
				continue;
			const auto& transform = entity.GetComponent<Transform>();

			if (entity.HasComponent<BoxCollider2D>())
			{
				auto& collider = entity.GetComponent<BoxCollider2D>();
				const float halfWidth = std::abs(collider.Size.x * transform._Scale.x);
				const float halfHeight = std::abs(collider.Size.y * transform._Scale.y);
				const float centerX = collider.Offset.x * transform._Scale.x;
				const float centerY = collider.Offset.y * transform._Scale.y;
				const bool valid = collider.Enabled
					&& std::isfinite(collider.Offset.x) && std::isfinite(collider.Offset.y)
					&& std::isfinite(collider.Size.x) && std::isfinite(collider.Size.y)
					&& collider.Size.x > 0.0f && collider.Size.y > 0.0f
					&& std::isfinite(transform._Scale.x) && std::isfinite(transform._Scale.y)
					&& IsValidPhysicsMaterial(collider.Density, collider.Friction, collider.Restitution)
					&& std::isfinite(collider.RestitutionThreshold)
					&& collider.RestitutionThreshold >= 0.0f
					&& collider.CollisionLayer != 0
					&& std::isfinite(halfWidth) && std::isfinite(halfHeight)
					&& std::isfinite(centerX) && std::isfinite(centerY)
					&& halfWidth > b2_epsilon && halfHeight > b2_epsilon;
				if (valid)
				{
					b2PolygonShape shape;
					shape.SetAsBox(halfWidth, halfHeight, { centerX, centerY }, 0.0f);
					b2FixtureDef fixtureDef;
					fixtureDef.shape = &shape;
					fixtureDef.density = collider.Density;
					fixtureDef.friction = collider.Friction;
					fixtureDef.restitution = collider.Restitution;
					fixtureDef.restitutionThreshold = collider.RestitutionThreshold;
					fixtureDef.isSensor = collider.IsTrigger;
					fixtureDef.filter.categoryBits = collider.CollisionLayer;
					fixtureDef.filter.maskBits = collider.CollisionMask;
					collider.RuntimeFixture = body->CreateFixture(&fixtureDef);
				}
				else if (collider.Enabled)
					TC_Core_Warn("Skipping invalid BoxCollider2D on entity '{0}'", entity.GetName());
			}

			if (entity.HasComponent<CircleCollider2D>())
			{
				auto& collider = entity.GetComponent<CircleCollider2D>();
				const float radiusScale = std::max(std::abs(transform._Scale.x),
					std::abs(transform._Scale.y));
				const float radius = collider.Radius * radiusScale;
				const float centerX = collider.Offset.x * transform._Scale.x;
				const float centerY = collider.Offset.y * transform._Scale.y;
				const bool valid = collider.Enabled
					&& std::isfinite(collider.Offset.x) && std::isfinite(collider.Offset.y)
					&& std::isfinite(collider.Radius) && collider.Radius > 0.0f
					&& std::isfinite(radiusScale)
					&& IsValidPhysicsMaterial(collider.Density, collider.Friction, collider.Restitution)
					&& collider.CollisionLayer != 0
					&& std::isfinite(radius) && std::isfinite(centerX) && std::isfinite(centerY)
					&& radius > b2_epsilon;
				if (valid)
				{
					b2CircleShape shape;
					shape.m_p.Set(centerX, centerY);
					shape.m_radius = radius;
					b2FixtureDef fixtureDef;
					fixtureDef.shape = &shape;
					fixtureDef.density = collider.Density;
					fixtureDef.friction = collider.Friction;
					fixtureDef.restitution = collider.Restitution;
					fixtureDef.isSensor = collider.IsTrigger;
					fixtureDef.filter.categoryBits = collider.CollisionLayer;
					fixtureDef.filter.maskBits = collider.CollisionMask;
					collider.RuntimeFixture = body->CreateFixture(&fixtureDef);
				}
				else if (collider.Enabled)
					TC_Core_Warn("Skipping invalid CircleCollider2D on entity '{0}'", entity.GetName());
			}
		}

		for (UUID uuid : m_EntityOrder)
		{
			Entity entity = FindEntityByUUID(uuid);
			if (!entity || !entity.HasComponent<DistanceJoint2D>())
				continue;
			auto& joint = entity.GetComponent<DistanceJoint2D>();
			if (!joint.Enabled || static_cast<uint64_t>(joint.ConnectedEntity) == 0)
				continue;
			Entity connected = FindEntityByUUID(joint.ConnectedEntity);
			b2Body* bodyA = FindRuntimeBody(uuid);
			b2Body* bodyB = FindRuntimeBody(joint.ConnectedEntity);
			const bool hasTransforms = entity.HasComponent<Transform>()
				&& connected && connected.HasComponent<Transform>();
			const glm::vec2 scaleA = hasTransforms
				? glm::vec2(entity.GetComponent<Transform>()._Scale)
				: glm::vec2(0.0f);
			const glm::vec2 scaleB = hasTransforms
				? glm::vec2(connected.GetComponent<Transform>()._Scale)
				: glm::vec2(0.0f);
			const glm::vec2 scaledAnchorA = joint.Anchor * scaleA;
			const glm::vec2 scaledAnchorB = joint.ConnectedAnchor * scaleB;
			const bool valid = connected && bodyA && bodyB && uuid != joint.ConnectedEntity
				&& std::isfinite(joint.Anchor.x) && std::isfinite(joint.Anchor.y)
				&& std::isfinite(joint.ConnectedAnchor.x) && std::isfinite(joint.ConnectedAnchor.y)
				&& IsFinite(scaleA) && IsFinite(scaleB)
				&& IsFinite(scaledAnchorA) && IsFinite(scaledAnchorB)
				&& std::isfinite(joint.Distance) && joint.Distance > 0.0f
				&& std::isfinite(joint.Frequency) && joint.Frequency >= 0.0f
				&& std::isfinite(joint.Damping) && joint.Damping >= 0.0f && joint.Damping <= 1.0f
				&& hasTransforms;
			if (!valid)
			{
				TC_Core_Warn("Skipping invalid DistanceJoint2D on entity '{0}'", entity.GetName());
				continue;
			}

			b2DistanceJointDef jointDef;
			jointDef.bodyA = bodyA;
			jointDef.bodyB = bodyB;
			jointDef.localAnchorA.Set(scaledAnchorA.x, scaledAnchorA.y);
			jointDef.localAnchorB.Set(scaledAnchorB.x, scaledAnchorB.y);
			const float box2DDistance = std::max(joint.Distance, b2_linearSlop);
			jointDef.length = box2DDistance;
			jointDef.collideConnected = joint.CollideConnected;
			jointDef.userData.pointer = static_cast<uintptr_t>(static_cast<uint64_t>(uuid));
			if (joint.Frequency > 0.0f)
				b2LinearStiffness(jointDef.stiffness, jointDef.damping, joint.Frequency,
					joint.Damping, bodyA, bodyB);
			else
			{
				// Zero frequency means a rigid distance constraint. A positive
				// frequency uses Damping as Box2D's damping ratio for a soft spring.
				jointDef.minLength = box2DDistance;
				jointDef.maxLength = box2DDistance;
			}
			joint.RuntimeJoint = m_PhysicsWorld->CreateJoint(&jointDef);
		}

		m_RuntimePhysicsDefinitionHash = ComputeRuntimePhysicsDefinitionHash();
		m_HasRuntimePhysicsDefinition = true;
		return true;
	}

	bool Scene::SynchronizeRuntimePhysicsDefinitions()
	{
		if (!m_RuntimeRunning || !m_ContactListener)
			return false;
		const uint64_t definitionHash = ComputeRuntimePhysicsDefinitionHash();
		if (m_HasRuntimePhysicsDefinition && m_PhysicsWorld
			&& definitionHash == m_RuntimePhysicsDefinitionHash)
			return true;
		if (m_PhysicsWorld && m_PhysicsWorld->IsLocked())
			return false;
		return RebuildRuntimePhysicsWorld(m_PhysicsWorld != nullptr);
	}

	std::optional<RaycastHit2D> Scene::Raycast2D(const glm::vec2& start,
		const glm::vec2& end, uint16_t layerMask, bool includeTriggers)
	{
		if (!m_RuntimeRunning || layerMask == 0 || !IsFinite(start) || !IsFinite(end))
			return std::nullopt;
		const glm::vec2 ray = end - start;
		if (glm::dot(ray, ray) <= std::numeric_limits<float>::epsilon())
			return std::nullopt;
		if (!SynchronizeRuntimePhysicsDefinitions() || !m_PhysicsWorld
			|| m_PhysicsWorld->IsLocked())
			return std::nullopt;

		ClosestRaycastCallback2D callback(this, layerMask, includeTriggers);
		m_PhysicsWorld->RayCast(&callback, { start.x, start.y }, { end.x, end.y });
		if (!callback.Hit || !FindEntityByUUID(callback.Hit->EntityID))
			return std::nullopt;
		return callback.Hit;
	}

	std::vector<PhysicsQueryHit2D> Scene::QueryAABB2D(const glm::vec2& lowerBound,
		const glm::vec2& upperBound, uint16_t layerMask, bool includeTriggers)
	{
		std::vector<PhysicsQueryHit2D> result;
		if (!m_RuntimeRunning || layerMask == 0
			|| !IsFinite(lowerBound) || !IsFinite(upperBound))
			return result;
		if (!SynchronizeRuntimePhysicsDefinitions() || !m_PhysicsWorld
			|| m_PhysicsWorld->IsLocked())
			return result;

		b2AABB bounds;
		bounds.lowerBound.Set(std::min(lowerBound.x, upperBound.x),
			std::min(lowerBound.y, upperBound.y));
		bounds.upperBound.Set(std::max(lowerBound.x, upperBound.x),
			std::max(lowerBound.y, upperBound.y));
		AABBQueryCallback2D callback(this, layerMask, includeTriggers);
		m_PhysicsWorld->QueryAABB(&callback, bounds);

		result.reserve(callback.Hits.size());
		for (const PhysicsQueryHit2D& hit : callback.Hits)
			if (FindEntityByUUID(hit.EntityID))
				result.push_back(hit);
		std::sort(result.begin(), result.end(), [](const auto& left, const auto& right)
		{
			const uint64_t leftID = static_cast<uint64_t>(left.EntityID);
			const uint64_t rightID = static_cast<uint64_t>(right.EntityID);
			if (leftID != rightID)
				return leftID < rightID;
			if (left.IsTrigger != right.IsTrigger)
				return left.IsTrigger < right.IsTrigger;
			return left.CollisionLayer < right.CollisionLayer;
		});
		result.erase(std::unique(result.begin(), result.end(), [](const auto& left, const auto& right)
		{
			return left.EntityID == right.EntityID;
		}), result.end());
		return result;
	}

	bool Scene::ApplyForce2D(UUID entityID, const glm::vec2& force, bool wake)
	{
		if (!IsFinite(force) || !SynchronizeRuntimePhysicsDefinitions()
			|| !m_PhysicsWorld || m_PhysicsWorld->IsLocked())
			return false;
		b2Body* body = FindRuntimeBody(entityID);
		if (!body || body->GetType() != b2_dynamicBody)
			return false;
		body->ApplyForceToCenter({ force.x, force.y }, wake);
		return true;
	}

	bool Scene::ApplyForceAtPoint2D(UUID entityID, const glm::vec2& force,
		const glm::vec2& worldPoint, bool wake)
	{
		if (!IsFinite(force) || !IsFinite(worldPoint)
			|| !SynchronizeRuntimePhysicsDefinitions()
			|| !m_PhysicsWorld || m_PhysicsWorld->IsLocked())
			return false;
		b2Body* body = FindRuntimeBody(entityID);
		if (!body || body->GetType() != b2_dynamicBody)
			return false;
		body->ApplyForce({ force.x, force.y }, { worldPoint.x, worldPoint.y }, wake);
		return true;
	}

	bool Scene::ApplyLinearImpulse2D(UUID entityID, const glm::vec2& impulse, bool wake)
	{
		if (!IsFinite(impulse) || !SynchronizeRuntimePhysicsDefinitions()
			|| !m_PhysicsWorld || m_PhysicsWorld->IsLocked())
			return false;
		b2Body* body = FindRuntimeBody(entityID);
		if (!body || body->GetType() != b2_dynamicBody)
			return false;
		body->ApplyLinearImpulseToCenter({ impulse.x, impulse.y }, wake);
		return true;
	}

	bool Scene::ApplyLinearImpulseAtPoint2D(UUID entityID, const glm::vec2& impulse,
		const glm::vec2& worldPoint, bool wake)
	{
		if (!IsFinite(impulse) || !IsFinite(worldPoint)
			|| !SynchronizeRuntimePhysicsDefinitions()
			|| !m_PhysicsWorld || m_PhysicsWorld->IsLocked())
			return false;
		b2Body* body = FindRuntimeBody(entityID);
		if (!body || body->GetType() != b2_dynamicBody)
			return false;
		body->ApplyLinearImpulse({ impulse.x, impulse.y },
			{ worldPoint.x, worldPoint.y }, wake);
		return true;
	}

	bool Scene::SetLinearVelocity2D(UUID entityID, const glm::vec2& velocity)
	{
		if (!IsFinite(velocity) || !SynchronizeRuntimePhysicsDefinitions()
			|| !m_PhysicsWorld || m_PhysicsWorld->IsLocked())
			return false;
		b2Body* body = FindRuntimeBody(entityID);
		if (!body || body->GetType() == b2_staticBody)
			return false;
		body->SetLinearVelocity({ velocity.x, velocity.y });
		return true;
	}

	std::optional<glm::vec2> Scene::GetLinearVelocity2D(UUID entityID)
	{
		if (!SynchronizeRuntimePhysicsDefinitions() || !m_PhysicsWorld
			|| m_PhysicsWorld->IsLocked())
			return std::nullopt;
		b2Body* body = FindRuntimeBody(entityID);
		if (!body)
			return std::nullopt;
		const b2Vec2 velocity = body->GetLinearVelocity();
		return glm::vec2(velocity.x, velocity.y);
	}

	bool Scene::OnRuntimeStart()
	{
		if (m_RuntimeRunning)
			return true;

		m_RuntimeAccumulator = 0.0;
		m_RuntimeBodies.clear();
		m_HasRuntimePhysicsDefinition = false;
		ResetRuntimePhysicsPointers();
		m_PendingRuntimeEntityCreates.clear();
		m_FlushingRuntimeEntityCreates = false;
		m_ContactFilter = new SceneContactFilter2D(this);
		m_ContactListener = new SceneContactListener();
		++m_RuntimeSessionGeneration;
		m_RuntimeRunning = true;
		if (!RebuildRuntimePhysicsWorld(false))
		{
			TC_Core_Error("Failed to initialize the runtime 2D physics world");
			OnRuntimeStop();
			return false;
		}

		bool hasManagedScripts = false;
		for (const entt::entity entity : m_Registry.view<CSharpScripts>())
		{
			if (!m_Registry.get<CSharpScripts>(entity).Scripts.empty())
			{
				hasManagedScripts = true;
				break;
			}
		}
		if (hasManagedScripts)
		{
			m_ScriptSceneSessionID = Scripting::ScriptEngine::Get().StartScene(
				*this, m_RuntimeSessionGeneration);
			if (m_ScriptSceneSessionID == 0)
			{
				TC_Core_Error("C# scripts are attached, but the managed runtime or current project assembly is unavailable");
				OnRuntimeStop();
				return false;
			}
		}
		else
		{
			// Do not create CoreCLR state for an empty Scene. The callback consumes
			// script-free batches and lazily starts a managed Scene only when a
			// batch with actual attachments reaches this safe point.
			ArmRuntimeScriptBatchCallback();
		}
		return true;
	}

	void Scene::OnRuntimeStop()
	{
		// Disable collection before any script/body teardown. Stop never emits
		// synthetic Exit events for a world that is being discarded.
		m_RuntimeRunning = false;
		m_RuntimeAccumulator = 0.0;
		m_PendingRuntimeEntityCreates.clear();
		m_FlushingRuntimeEntityCreates = false;
		if (m_ScriptSceneSessionID != 0)
		{
			Scripting::ScriptEngine::Get().StopScene(m_ScriptSceneSessionID);
			m_ScriptSceneSessionID = 0;
		}
		SetRuntimeEntityBatchCreatedCallback({});
		++m_RuntimeSessionGeneration;
		if (m_PhysicsWorld)
			m_PhysicsWorld->SetContactFilter(nullptr);
		if (m_PhysicsWorld)
			m_PhysicsWorld->SetContactListener(nullptr);
		if (m_ContactListener)
			m_ContactListener->Clear();

		ResetRuntimePhysicsPointers();
		m_RuntimeBodies.clear();
		m_HasRuntimePhysicsDefinition = false;
		m_RuntimePhysicsDefinitionHash = 0;

		delete m_PhysicsWorld;
		m_PhysicsWorld = nullptr;
		delete m_ContactListener;
		m_ContactListener = nullptr;
		delete m_ContactFilter;
		m_ContactFilter = nullptr;
	}

	void Scene::SynchronizeRuntimeTransforms()
	{
		auto view = m_Registry.view<Transform, Rigidbody2D>();
		for (auto e : view)
		{
			Entity entity = { e, this };
			auto& transform = view.get<Transform>(e);
			auto& rb2d = view.get<Rigidbody2D>(e);
			if (!rb2d.Enabled || !rb2d.RuntimeBody)
				continue;

			b2Body* body = static_cast<b2Body*>(rb2d.RuntimeBody);
			const b2Vec2& position = body->GetPosition();
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

	bool Scene::RunFixedRuntimeStep()
	{
		if (!m_RuntimeRunning || !m_PhysicsWorld)
			return false;
		if (!SynchronizeRuntimePhysicsDefinitions())
			return false;

		if (m_ScriptSceneSessionID != 0)
			Scripting::ScriptEngine::Get().FixedUpdateAll(m_ScriptSceneSessionID,
				FixedRuntimeTimestep);
		if (!m_RuntimeRunning || !m_PhysicsWorld)
			return false;
		// Managed callbacks may add, remove, replace, or directly edit physics
		// components. Reconcile again before entering Box2D's locked Step region.
		if (!SynchronizeRuntimePhysicsDefinitions())
			return false;
		// Materialize bodies/joints before dynamic managed OnCreate. Scripts may
		// then inspect the new physics proxies immediately; reconcile once more in
		// case OnCreate changes authoring components.
		FlushPendingRuntimeEntityCreates();
		if (!SynchronizeRuntimePhysicsDefinitions())
			return false;

		constexpr int32_t velocityIterations = 6;
		constexpr int32_t positionIterations = 2;
		m_ContactListener->BeginStep();
		m_PhysicsWorld->Step(FixedRuntimeTimestep, velocityIterations, positionIterations);
		m_ContactListener->EndStep();
		SynchronizeRuntimeTransforms();
		// Accept the pose written by physics before invoking user callbacks. A
		// callback-side teleport then differs from this snapshot on the next step.
		// If hierarchy propagation moved an implicit/static physics entity whose
		// Box2D body was not written back, leave the old hash in place so the next
		// boundary rebuilds that body at its new ECS pose.
		bool runtimePosesMatchScene = true;
		for (const auto& [uuid, body] : m_RuntimeBodies)
		{
			Entity entity = FindEntityByUUID(uuid);
			if (!body || !entity || !entity.HasComponent<Transform>())
			{
				runtimePosesMatchScene = false;
				break;
			}
			const auto& transform = entity.GetComponent<Transform>();
			const b2Vec2 position = body->GetPosition();
			const float angleDelta = std::remainder(transform._Rotation.z - body->GetAngle(),
				2.0f * b2_pi);
			if (std::abs(transform._Translation.x - position.x) > 1.0e-5f
				|| std::abs(transform._Translation.y - position.y) > 1.0e-5f
				|| std::abs(angleDelta) > 1.0e-5f)
			{
				runtimePosesMatchScene = false;
				break;
			}
		}
		if (runtimePosesMatchScene)
		{
			m_RuntimePhysicsDefinitionHash = ComputeRuntimePhysicsDefinitionHash();
			m_HasRuntimePhysicsDefinition = true;
		}
		DispatchPendingCollisionEvents();
		if (!SynchronizeRuntimePhysicsDefinitions())
			return false;
		FlushPendingRuntimeEntityCreates();
		if (!SynchronizeRuntimePhysicsDefinitions())
			return false;
		return m_RuntimeRunning && m_PhysicsWorld;
	}

	void Scene::OnUpdateRuntime(Timestep ts)
	{
		if (!m_RuntimeRunning || !m_PhysicsWorld)
		{
			TC_Core_Warn("Ignoring runtime update for a scene that has not been started");
			return;
		}

		const float rawFrameDelta = ts.GetSeconds();
		const float frameDelta = std::isfinite(rawFrameDelta) && rawFrameDelta > 0.0f
			? std::min(rawFrameDelta, MaximumRuntimeFrameDelta)
			: 0.0f;
		m_RuntimeAccumulator += static_cast<double>(frameDelta);

		uint32_t substeps = 0;
		const double fixedTimestep = static_cast<double>(FixedRuntimeTimestep);
		// Display deltas enter through a float Timestep. Allow one float epsilon
		// when recognizing a fixed-step boundary (for example 144 * float(1/144))
		// and discard only that sub-microsecond negative remainder after stepping.
		const double stepBoundaryTolerance =
			static_cast<double>(std::numeric_limits<float>::epsilon());
		while (m_RuntimeAccumulator + stepBoundaryTolerance >= fixedTimestep
			&& substeps < MaximumRuntimeSubsteps)
		{
			m_RuntimeAccumulator = std::max(0.0, m_RuntimeAccumulator - fixedTimestep);
			++substeps;
			if (!RunFixedRuntimeStep())
				break;
		}

		// Drop whole overdue steps once the per-frame budget is exhausted. Keep
		// only the fractional remainder so a hitch cannot cause a spiral of death.
		if (substeps == MaximumRuntimeSubsteps
			&& m_RuntimeAccumulator + stepBoundaryTolerance >= fixedTimestep)
		{
			m_RuntimeAccumulator = std::fmod(m_RuntimeAccumulator, fixedTimestep);
			if (m_RuntimeAccumulator + stepBoundaryTolerance >= fixedTimestep)
				m_RuntimeAccumulator = 0.0;
		}

		if (m_RuntimeRunning)
		{
			if (m_ScriptSceneSessionID != 0)
				Scripting::ScriptEngine::Get().UpdateAll(m_ScriptSceneSessionID, frameDelta);
			if (!SynchronizeRuntimePhysicsDefinitions())
				return;
			FlushPendingRuntimeEntityCreates();
			if (!SynchronizeRuntimePhysicsDefinitions())
				return;
		}

		RenderRuntimeScene();
	}

	void Scene::OnRuntimeStep()
	{
		if (!m_RuntimeRunning || !m_PhysicsWorld)
		{
			TC_Core_Warn("Ignoring runtime step for a scene that has not been started");
			return;
		}

		// Single-step has no relationship to the most recent display-frame dt.
		m_RuntimeAccumulator = 0.0;
		RunFixedRuntimeStep();
		m_RuntimeAccumulator = 0.0;
		RenderRuntimeScene();
	}

	void Scene::RenderRuntimeScene()
	{
		// Set background color from primary camera.
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
		RenderRuntimeScene();
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

	std::vector<ColliderDebugShape> Scene::GetColliderDebugShapes(bool useRuntimeFixtures) const
	{
		std::vector<ColliderDebugShape> result;

		if (useRuntimeFixtures)
		{
			if (!m_PhysicsWorld)
				return result;

			for (b2Body* body = m_PhysicsWorld->GetBodyList(); body; body = body->GetNext())
			{
				const uint64_t rawUUID = static_cast<uint64_t>(body->GetUserData().pointer);
				if (rawUUID == 0)
					continue;
				const UUID uuid(rawUUID);
				auto entityIt = m_EntityMap.find(uuid);
				if (entityIt == m_EntityMap.end() || !m_Registry.valid(entityIt->second)
					|| !m_Registry.all_of<ID>(entityIt->second)
					|| m_Registry.get<ID>(entityIt->second).id != uuid)
					continue;

				float z = 0.0f;
				if (m_Registry.all_of<Transform>(entityIt->second))
					z = m_Registry.get<Transform>(entityIt->second)._Translation.z;

				for (b2Fixture* fixture = body->GetFixtureList(); fixture; fixture = fixture->GetNext())
				{
					const b2Shape* shape = fixture->GetShape();
					if (!shape)
						continue;

					if (shape->GetType() == b2Shape::e_polygon)
					{
						const auto* polygon = static_cast<const b2PolygonShape*>(shape);
						if (polygon->m_count != 4)
							continue;

						b2Vec2 vertices[4];
						for (int32_t index = 0; index < 4; ++index)
							vertices[index] = b2Mul(body->GetTransform(), polygon->m_vertices[index]);
						const b2Vec2 edgeX = vertices[1] - vertices[0];
						const b2Vec2 edgeY = vertices[2] - vertices[1];
						const float width = edgeX.Length();
						const float height = edgeY.Length();
						if (!std::isfinite(width) || !std::isfinite(height)
							|| width <= b2_epsilon || height <= b2_epsilon)
							continue;

						const b2Vec2 center = 0.25f * (vertices[0] + vertices[1] + vertices[2] + vertices[3]);
						const float rotation = std::atan2(edgeX.y, edgeX.x);
						ColliderDebugShape debugShape;
						debugShape.Type = ColliderDebugShapeType::Box;
						debugShape.EntityID = uuid;
						debugShape.Enabled = true;
						debugShape.IsTrigger = fixture->IsSensor();
						debugShape.CollisionLayer = fixture->GetFilterData().categoryBits;
						debugShape.Center = { center.x, center.y };
						debugShape.HalfSize = { width * 0.5f, height * 0.5f };
						debugShape.Rotation = rotation;
						debugShape.Transform = MakeColliderDebugTransform(debugShape.Center, z,
							rotation, { width, height });
						result.push_back(debugShape);
					}
					else if (shape->GetType() == b2Shape::e_circle)
					{
						const auto* circle = static_cast<const b2CircleShape*>(shape);
						const b2Vec2 center = b2Mul(body->GetTransform(), circle->m_p);
						const float radius = circle->m_radius;
						if (!std::isfinite(center.x) || !std::isfinite(center.y)
							|| !std::isfinite(radius) || radius <= b2_epsilon)
							continue;

						ColliderDebugShape debugShape;
						debugShape.Type = ColliderDebugShapeType::Circle;
						debugShape.EntityID = uuid;
						debugShape.Enabled = true;
						debugShape.IsTrigger = fixture->IsSensor();
						debugShape.CollisionLayer = fixture->GetFilterData().categoryBits;
						debugShape.Center = { center.x, center.y };
						debugShape.Radius = radius;
						debugShape.Rotation = body->GetAngle();
						debugShape.Transform = MakeColliderDebugTransform(debugShape.Center, z,
							0.0f, glm::vec2(radius * 2.0f));
						result.push_back(debugShape);
					}
				}
			}
			return result;
		}

		const auto boxView = m_Registry.view<ID, Transform, BoxCollider2D>();
		result.reserve(boxView.size_hint());
		for (entt::entity entity : boxView)
		{
			const auto& transform = boxView.get<Transform>(entity);
			const auto& collider = boxView.get<BoxCollider2D>(entity);
			if (!std::isfinite(collider.Offset.x) || !std::isfinite(collider.Offset.y)
				|| !std::isfinite(collider.Size.x) || !std::isfinite(collider.Size.y)
				|| collider.Size.x <= 0.0f || collider.Size.y <= 0.0f
				|| !IsValidColliderTransform2D(transform))
				continue;

			const glm::vec2 halfSize = glm::abs(collider.Size * glm::vec2(transform._Scale));
			const glm::vec2 center = TransformColliderOffset2D(transform, collider.Offset);
			if (!std::isfinite(center.x) || !std::isfinite(center.y)
				|| !std::isfinite(halfSize.x) || !std::isfinite(halfSize.y)
				|| halfSize.x <= b2_epsilon || halfSize.y <= b2_epsilon)
				continue;

			ColliderDebugShape debugShape;
			debugShape.Type = ColliderDebugShapeType::Box;
			debugShape.EntityID = boxView.get<ID>(entity).id;
			debugShape.Enabled = collider.Enabled;
			debugShape.IsTrigger = collider.IsTrigger;
			debugShape.CollisionLayer = collider.CollisionLayer;
			debugShape.Center = center;
			debugShape.HalfSize = halfSize;
			debugShape.Rotation = transform._Rotation.z;
			debugShape.Transform = MakeColliderDebugTransform(debugShape.Center, transform._Translation.z,
				debugShape.Rotation, halfSize * 2.0f);
			result.push_back(debugShape);
		}

		const auto circleView = m_Registry.view<ID, Transform, CircleCollider2D>();
		result.reserve(result.size() + circleView.size_hint());
		for (entt::entity entity : circleView)
		{
			const auto& transform = circleView.get<Transform>(entity);
			const auto& collider = circleView.get<CircleCollider2D>(entity);
			if (!std::isfinite(collider.Offset.x) || !std::isfinite(collider.Offset.y)
				|| !std::isfinite(collider.Radius) || collider.Radius <= 0.0f
				|| !IsValidColliderTransform2D(transform))
				continue;

			const float radiusScale = std::max(std::abs(transform._Scale.x),
				std::abs(transform._Scale.y));
			const float radius = collider.Radius * radiusScale;
			const glm::vec2 center = TransformColliderOffset2D(transform, collider.Offset);
			if (!std::isfinite(center.x) || !std::isfinite(center.y)
				|| !std::isfinite(radius) || radius <= b2_epsilon)
				continue;

			ColliderDebugShape debugShape;
			debugShape.Type = ColliderDebugShapeType::Circle;
			debugShape.EntityID = circleView.get<ID>(entity).id;
			debugShape.Enabled = collider.Enabled;
			debugShape.IsTrigger = collider.IsTrigger;
			debugShape.CollisionLayer = collider.CollisionLayer;
			debugShape.Center = center;
			debugShape.Radius = radius;
			debugShape.Rotation = transform._Rotation.z;
			debugShape.Transform = MakeColliderDebugTransform(debugShape.Center, transform._Translation.z,
				0.0f, glm::vec2(radius * 2.0f));
			result.push_back(debugShape);
		}

		return result;
	}

	Entity Scene::DuplicateEntity(Entity entity)
	{
		if (!entity || entity.m_Scene != this || !m_Registry.valid(entity.m_EntityHandle)
			|| !entity.HasComponent<ID>() || !entity.HasComponent<Tag>()
			|| !entity.HasComponent<EntityMetadata>() || !entity.HasComponent<Transform>())
			return {};

		Entity parent = GetParent(entity);
		std::unordered_set<uint64_t> attachmentIDs;
		for (UUID existingUUID : m_EntityOrder)
		{
			Entity existing = FindEntityByUUID(existingUUID);
			if (!existing || !existing.HasComponent<CSharpScripts>())
				continue;
			for (const CSharpScriptEntry& script :
				existing.GetComponent<CSharpScripts>().Scripts)
			{
				const uint64_t attachment = static_cast<uint64_t>(script.AttachmentID);
				if (attachment != 0)
					attachmentIDs.emplace(attachment);
			}
		}
		std::unordered_map<UUID, UUID> duplicateUUIDs;
		Entity duplicate = DuplicateEntityRecursive(this, entity, parent, duplicateUUIDs);
		if (!duplicate)
			return {};

		for (const auto& [sourceUUID, duplicateUUID] : duplicateUUIDs)
		{
			(void)sourceUUID;
			Entity duplicatedEntity = FindEntityByUUID(duplicateUUID);
			std::string remapError;
			if (!ComponentCodecs::RemapInstanceReferences(duplicatedEntity,
				duplicateUUIDs,
				ComponentCodecs::MissingEntityReferencePolicy::Preserve,
				attachmentIDs, true, remapError))
			{
				TC_Core_Error("Could not remap duplicated entity references: {0}",
					remapError);
				DestroyEntity(duplicate);
				return {};
			}
		}
		return duplicate;
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

		auto scriptsView = m_Registry.view<ID, CSharpScripts>();
		for (const entt::entity entity : scriptsView)
		{
			const uint64_t entityID = static_cast<uint64_t>(scriptsView.get<ID>(entity).id);
			const auto& scripts = scriptsView.get<CSharpScripts>(entity).Scripts;
			for (size_t scriptIndex = 0; scriptIndex < scripts.size(); ++scriptIndex)
			{
				const CSharpScriptEntry& script = scripts[scriptIndex];
				if (script.ScriptAsset == handle)
				{
					AssetReference reference;
					reference.ReferencedAsset = handle;
					reference.PropertyPath = "Entity " + std::to_string(entityID)
						+ ".CSharpScripts.Scripts[" + std::to_string(scriptIndex)
						+ "].ScriptHandle";
					references.push_back(std::move(reference));
				}
				for (const ScriptField& field : script.Fields)
				{
					if (field.Type != ScriptFieldType::AssetRef
						|| !std::holds_alternative<uint64_t>(field.Value)
						|| std::get<uint64_t>(field.Value) != static_cast<uint64_t>(handle))
						continue;
					AssetReference reference;
					reference.ReferencedAsset = handle;
					reference.PropertyPath = "Entity " + std::to_string(entityID)
						+ ".CSharpScripts.Scripts[" + std::to_string(scriptIndex)
						+ "].Fields." + field.Name;
					references.push_back(std::move(reference));
				}
			}
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
		if (m_RuntimeRunning && entity
			&& (entity.HasComponent<Rigidbody2D>() || entity.HasComponent<BoxCollider2D>()
				|| entity.HasComponent<CircleCollider2D>() || entity.HasComponent<DistanceJoint2D>()))
			m_HasRuntimePhysicsDefinition = false;
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
	void Scene::OnComponentAdded<EntityMetadata>(Entity entity, EntityMetadata& component)
	{
		if (component.GameplayTag.empty())
			component.GameplayTag = "Untagged";
		if (component.Layer >= Physics2DLayerCount)
			component.Layer = 0;
		if (m_RuntimeRunning)
			m_HasRuntimePhysicsDefinition = false;
	}

	template<>
	void Scene::OnComponentAdded<CSharpScripts>(Entity entity, CSharpScripts& component)
	{
	}

	template<>
	void Scene::OnComponentAdded<Rigidbody2D>(Entity entity, Rigidbody2D& component)
	{
		component.RuntimeBody = nullptr;
		if (m_RuntimeRunning)
			m_HasRuntimePhysicsDefinition = false;
	}

	template<>
	void Scene::OnComponentAdded<BoxCollider2D>(Entity entity, BoxCollider2D& component)
	{
		component.RuntimeFixture = nullptr;
		if (m_RuntimeRunning)
			m_HasRuntimePhysicsDefinition = false;
	}

	template<>
	void Scene::OnComponentAdded<CircleCollider2D>(Entity entity, CircleCollider2D& component)
	{
		component.RuntimeFixture = nullptr;
		if (m_RuntimeRunning)
			m_HasRuntimePhysicsDefinition = false;
	}

	template<>
	void Scene::OnComponentAdded<DistanceJoint2D>(Entity entity, DistanceJoint2D& component)
	{
		component.RuntimeJoint = nullptr;
		if (m_RuntimeRunning)
			m_HasRuntimePhysicsDefinition = false;
	}
}
