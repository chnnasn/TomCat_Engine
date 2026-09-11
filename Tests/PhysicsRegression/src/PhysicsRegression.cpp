#include "TomCat/Core/Log.h"
#include "TomCat/Asset/AssetManager.h"
#include "TomCat/Project/Project.h"
#include "TomCat/Scene/Components.h"
#include "TomCat/Scene/Entity.h"
#include "TomCat/Scene/Scene.h"
#include "TomCat/Scene/SceneSerializer.h"
#include "TomCat/Scene/ScriptableEntity.h"

#include "box2d/b2_body.h"
#include "box2d/b2_fixture.h"

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <stdexcept>
#include <string>

namespace {

	void Require(bool condition, const char* message)
	{
		if (!condition)
			throw std::runtime_error(message);
	}

	bool Near(float actual, float expected, float tolerance = 1.0e-4f)
	{
		return std::abs(actual - expected) <= tolerance;
	}

	bool Near(const glm::vec2& actual, const glm::vec2& expected,
		float tolerance = 1.0e-4f)
	{
		return Near(actual.x, expected.x, tolerance)
			&& Near(actual.y, expected.y, tolerance);
	}

	bool Near(const glm::mat4& actual, const glm::mat4& expected,
		float tolerance = 1.0e-4f)
	{
		for (glm::length_t column = 0; column < 4; ++column)
			for (glm::length_t row = 0; row < 4; ++row)
				if (!Near(actual[column][row], expected[column][row], tolerance))
					return false;
		return true;
	}

	bool Contains(const TomCat::CollisionEnter2D& event, TomCat::UUID entityID)
	{
		return event.EntityA == entityID || event.EntityB == entityID;
	}

	bool Contains(const TomCat::CollisionExit2D& event, TomCat::UUID entityID)
	{
		return event.EntityA == entityID || event.EntityB == entityID;
	}

	bool Contains(const TomCat::TriggerEnter2D& event, TomCat::UUID entityID)
	{
		return event.EntityA == entityID || event.EntityB == entityID;
	}

	bool Contains(const TomCat::TriggerExit2D& event, TomCat::UUID entityID)
	{
		return event.EntityA == entityID || event.EntityB == entityID;
	}

	class TemporaryCookedProject final
	{
	public:
		TemporaryCookedProject()
		{
			TomCat::AssetManager::Get().Shutdown();
			Root = std::filesystem::temp_directory_path()
				/ ("tomcat_physics_cooked_"
					+ std::to_string(static_cast<uint64_t>(TomCat::UUID())));
		}

		~TemporaryCookedProject()
		{
			// A mounted package owns an open file handle on Windows, so always close
			// the process-global asset manager before removing this unique test tree.
			TomCat::AssetManager::Get().Shutdown();
			std::error_code error;
			std::filesystem::remove_all(Root, error);
		}

		std::filesystem::path Root;
	};

	class FixedStepProbe final : public TomCat::ScriptableEntity
	{
	public:
		inline static int Creates = 0;
		inline static int Updates = 0;
		inline static int Destroys = 0;
		inline static float LastDelta = 0.0f;

		static void Reset()
		{
			Creates = 0;
			Updates = 0;
			Destroys = 0;
			LastDelta = 0.0f;
		}

	protected:
		void OnCreate() override { ++Creates; }
		void OnUpdate(TomCat::Timestep timestep) override
		{
			++Updates;
			LastDelta = timestep.GetSeconds();
		}
		void OnDestroy() override { ++Destroys; }
	};

	class PhysicsEventProbe final : public TomCat::ScriptableEntity
	{
	public:
		inline static int CollisionEnters = 0;
		inline static int CollisionExits = 0;
		inline static int TriggerEnters = 0;
		inline static int TriggerExits = 0;
		inline static bool SawInvalidSelfPair = false;

		static void Reset()
		{
			CollisionEnters = 0;
			CollisionExits = 0;
			TriggerEnters = 0;
			TriggerExits = 0;
			SawInvalidSelfPair = false;
		}

	protected:
		void OnCollisionEnter2D(const TomCat::CollisionEnter2D& event) override
		{
			++CollisionEnters;
			SawInvalidSelfPair |= !Contains(event, GetComponent<TomCat::ID>().id);
		}
		void OnCollisionExit2D(const TomCat::CollisionExit2D& event) override
		{
			++CollisionExits;
			SawInvalidSelfPair |= !Contains(event, GetComponent<TomCat::ID>().id);
		}
		void OnTriggerEnter2D(const TomCat::TriggerEnter2D& event) override
		{
			++TriggerEnters;
			SawInvalidSelfPair |= !Contains(event, GetComponent<TomCat::ID>().id);
		}
		void OnTriggerExit2D(const TomCat::TriggerExit2D& event) override
		{
			++TriggerExits;
			SawInvalidSelfPair |= !Contains(event, GetComponent<TomCat::ID>().id);
		}
	};

	class ReplacementScriptProbe final : public TomCat::ScriptableEntity
	{
	public:
		inline static int Creates = 0;
		inline static int Updates = 0;
		inline static int Destroys = 0;

		static void Reset()
		{
			Creates = 0;
			Updates = 0;
			Destroys = 0;
		}

	protected:
		void OnCreate() override { ++Creates; }
		void OnUpdate(TomCat::Timestep) override { ++Updates; }
		void OnDestroy() override { ++Destroys; }
	};

	class CollisionMutationProbe final : public TomCat::ScriptableEntity
	{
	public:
		enum class Action
		{
			RemoveComponent,
			ReplaceComponent,
			DestroyEntity
		};

		inline static TomCat::Scene* ActiveScene = nullptr;
		inline static Action PendingAction = Action::RemoveComponent;
		inline static int CollisionEnters = 0;
		inline static int Destroys = 0;
		inline static bool InCollisionCallback = false;
		inline static bool DestroyedDuringCallback = false;
		inline static bool CallbackContinuedSafely = false;
		inline static bool EntityWasValidDuringDestroy = false;
		inline static TomCat::UUID Self{ 0 };

		static void Reset(TomCat::Scene& scene, Action action)
		{
			ActiveScene = &scene;
			PendingAction = action;
			CollisionEnters = 0;
			Destroys = 0;
			InCollisionCallback = false;
			DestroyedDuringCallback = false;
			CallbackContinuedSafely = false;
			EntityWasValidDuringDestroy = false;
			Self = TomCat::UUID(0);
			ReplacementScriptProbe::Reset();
		}

	protected:
		void OnCollisionEnter2D(const TomCat::CollisionEnter2D&) override
		{
			++CollisionEnters;
			InCollisionCallback = true;
			Self = GetComponent<TomCat::ID>().id;
			TomCat::Entity entity = ActiveScene ? ActiveScene->FindEntityByUUID(Self) : TomCat::Entity{};
			if (entity)
			{
				switch (PendingAction)
				{
				case Action::RemoveComponent:
					entity.RemoveComponent<TomCat::NativeScript>();
					break;
				case Action::ReplaceComponent:
					entity.AddOrReplaceComponent<TomCat::NativeScript>().Bind<ReplacementScriptProbe>();
					break;
				case Action::DestroyEntity:
					ActiveScene->DestroyEntity(entity);
					break;
				}
			}

			// All three operations must leave the active C++ object and its Entity
			// usable until this virtual callback returns.
			const TomCat::Entity stillAlive = ActiveScene
				? ActiveScene->FindEntityByUUID(Self) : TomCat::Entity{};
			CallbackContinuedSafely = stillAlive
				&& GetComponent<TomCat::ID>().id == Self;
			InCollisionCallback = false;
		}

		void OnDestroy() override
		{
			++Destroys;
			DestroyedDuringCallback |= InCollisionCallback;
			const TomCat::Entity entity = ActiveScene
				? ActiveScene->FindEntityByUUID(Self) : TomCat::Entity{};
			EntityWasValidDuringDestroy = entity
				&& GetComponent<TomCat::ID>().id == Self;
		}
	};

	TomCat::Entity AddCircleBody(TomCat::Scene& scene, const char* name,
		TomCat::Rigidbody2D::BodyType bodyType, const glm::vec2& position,
		float radius = 0.5f)
	{
		TomCat::Entity entity = scene.CreateEntity(name);
		auto& transform = entity.GetComponent<TomCat::Transform>();
		transform._Translation = { position.x, position.y, 0.0f };
		transform._LocalTranslation = transform._Translation;
		auto& body = entity.AddComponent<TomCat::Rigidbody2D>();
		body.Type = bodyType;
		entity.AddComponent<TomCat::CircleCollider2D>().Radius = radius;
		return entity;
	}

	void MoveRuntimeBody(TomCat::Entity entity, const glm::vec2& position)
	{
		auto* body = static_cast<b2Body*>(entity.GetComponent<TomCat::Rigidbody2D>().RuntimeBody);
		Require(body != nullptr, "entity has no runtime body");
		body->SetTransform({ position.x, position.y }, body->GetAngle());
	}

	void TestFixedAccumulatorAndStep()
	{
		FixedStepProbe::Reset();
		TomCat::Scene scene;
		TomCat::Entity entity = scene.CreateEntity("Fixed step probe");
		entity.AddComponent<TomCat::NativeScript>().Bind<FixedStepProbe>();
		scene.OnRuntimeStart();

		scene.OnUpdateRuntime(TomCat::Timestep(1.0f / 120.0f));
		Require(FixedStepProbe::Updates == 0, "half step advanced simulation early");
		scene.OnUpdateRuntime(TomCat::Timestep(1.0f / 120.0f));
		Require(FixedStepProbe::Creates == 1, "script was not created exactly once");
		Require(FixedStepProbe::Updates == 1, "two half steps did not produce one fixed update");
		Require(Near(FixedStepProbe::LastDelta, TomCat::Scene::FixedRuntimeTimestep),
			"script did not receive the fixed timestep");

		scene.OnUpdateRuntime(TomCat::Timestep(1.0f));
		Require(FixedStepProbe::Updates == 1 + static_cast<int>(TomCat::Scene::MaximumRuntimeSubsteps),
			"hitch was not capped at the maximum substep count");
		scene.OnUpdateRuntime(TomCat::Timestep(0.0f));
		Require(FixedStepProbe::Updates == 1 + static_cast<int>(TomCat::Scene::MaximumRuntimeSubsteps),
			"overdue whole steps were not discarded after substep cap");

		scene.OnRuntimeStep();
		Require(FixedStepProbe::Updates == 2 + static_cast<int>(TomCat::Scene::MaximumRuntimeSubsteps),
			"single-step did not advance exactly one fixed update");
		scene.OnRuntimeStop();
		Require(FixedStepProbe::Destroys == 1, "script was not destroyed exactly once on stop");
	}

	struct FallingBodyResult
	{
		float PositionY = 0.0f;
		float VelocityY = 0.0f;
	};

	FallingBodyResult SimulateFallingBody(int displayHz, int seconds)
	{
		TomCat::Scene scene;
		TomCat::Entity entity = AddCircleBody(scene, "Falling body",
			TomCat::Rigidbody2D::BodyType::Dynamic, { 0.0f, 0.0f });
		scene.OnRuntimeStart();
		for (int frame = 0; frame < displayHz * seconds; ++frame)
			scene.OnUpdateRuntime(TomCat::Timestep(1.0f / static_cast<float>(displayHz)));

		auto* body = static_cast<b2Body*>(entity.GetComponent<TomCat::Rigidbody2D>().RuntimeBody);
		Require(body != nullptr, "falling body did not receive a runtime body");
		const FallingBodyResult result{ body->GetPosition().y, body->GetLinearVelocity().y };
		scene.OnRuntimeStop();
		return result;
	}

	void CompareFrameRatePhysics(int simulationSeconds)
	{
		const FallingBodyResult at30Hz = SimulateFallingBody(30, simulationSeconds);
		const FallingBodyResult at60Hz = SimulateFallingBody(60, simulationSeconds);
		const FallingBodyResult at144Hz = SimulateFallingBody(144, simulationSeconds);
		Require(Near(at30Hz.PositionY, at60Hz.PositionY) && Near(at30Hz.VelocityY, at60Hz.VelocityY),
			"30Hz and 60Hz produced different physics results");
		Require(Near(at30Hz.PositionY, at144Hz.PositionY) && Near(at30Hz.VelocityY, at144Hz.VelocityY),
			"30Hz and 144Hz produced different physics results");
	}

	void TestFrameRateIndependentPhysics()
	{
		CompareFrameRatePhysics(1);
		CompareFrameRatePhysics(10);
	}

	void TestPauseRenderPathAndSingleStep()
	{
		FixedStepProbe::Reset();
		TomCat::Scene scene;
		TomCat::Entity entity = AddCircleBody(scene, "Paused body",
			TomCat::Rigidbody2D::BodyType::Dynamic, { 0.0f, 0.0f });
		entity.AddComponent<TomCat::NativeScript>().Bind<FixedStepProbe>();
		scene.OnRuntimeStart();
		scene.OnRuntimeStep();
		auto* body = static_cast<b2Body*>(entity.GetComponent<TomCat::Rigidbody2D>().RuntimeBody);
		Require(body != nullptr, "paused test body did not receive a runtime body");
		const float pausedPosition = body->GetPosition().y;
		const int pausedUpdates = FixedStepProbe::Updates;

		for (int frame = 0; frame < 20; ++frame)
			scene.OnRenderRuntime();
		Require(FixedStepProbe::Updates == pausedUpdates,
			"render-only pause path advanced scripts");
		Require(Near(body->GetPosition().y, pausedPosition),
			"render-only pause path advanced physics");

		scene.OnRuntimeStep();
		Require(FixedStepProbe::Updates == pausedUpdates + 1,
			"paused single-step did not advance scripts exactly once");
		Require(body->GetPosition().y < pausedPosition,
			"paused single-step did not advance physics exactly one step");
		scene.OnRuntimeStop();
	}

	void TestColliderOnlyStaticBody()
	{
		TomCat::Scene scene;
		TomCat::Entity ground = scene.CreateEntity("Collider-only ground");
		auto& groundTransform = ground.GetComponent<TomCat::Transform>();
		groundTransform._Translation.y = -1.0f;
		groundTransform._LocalTranslation = groundTransform._Translation;
		auto& box = ground.AddComponent<TomCat::BoxCollider2D>();
		box.Size = { 5.0f, 0.5f };
		TomCat::Entity ball = AddCircleBody(scene, "Ball",
			TomCat::Rigidbody2D::BodyType::Dynamic, { 0.0f, 2.0f });

		int enters = 0;
		scene.AddCollisionEnter2DListener([&](const TomCat::CollisionEnter2D&) { ++enters; });
		scene.OnRuntimeStart();
		Require(box.RuntimeFixture != nullptr,
			"collider without Rigidbody2D did not create a static fixture");
		for (int step = 0; step < 120; ++step)
			scene.OnRuntimeStep();
		Require(enters == 1, "dynamic body did not collide exactly once with collider-only static body");
		auto* body = static_cast<b2Body*>(ball.GetComponent<TomCat::Rigidbody2D>().RuntimeBody);
		Require(body && body->GetPosition().y > -0.1f,
			"dynamic body passed through collider-only static body");
		scene.OnRuntimeStop();
	}

	void TestCircleNonUniformScaleFixture()
	{
		TomCat::Scene scene;
		TomCat::Entity entity = scene.CreateEntity("Scaled circle");
		auto& transform = entity.GetComponent<TomCat::Transform>();
		transform._Translation = { 4.0f, 5.0f, 2.0f };
		transform._Rotation = { 0.0f, 0.0f, 1.57079632679f };
		transform._Scale = { 2.0f, 3.0f, 1.0f };
		entity.AddComponent<TomCat::Rigidbody2D>();
		auto& circle = entity.AddComponent<TomCat::CircleCollider2D>();
		circle.Offset = { 1.0f, -2.0f };
		circle.Radius = 0.5f;

		scene.OnRuntimeStart();
		Require(circle.RuntimeFixture != nullptr, "CircleCollider2D did not create a fixture");
		const auto shapes = scene.GetColliderDebugShapes(true);
		Require(shapes.size() == 1, "runtime did not expose exactly one circle fixture");
		Require(shapes[0].Type == TomCat::ColliderDebugShapeType::Circle,
			"runtime fixture was not reported as a circle");
		Require(Near(shapes[0].Radius, 1.5f),
			"circle radius did not use max(abs(scale.x), abs(scale.y))");
		Require(Near(shapes[0].Center.x, 10.0f) && Near(shapes[0].Center.y, 7.0f),
			"scaled/rotated circle offset did not match the Box2D fixture");
		scene.OnRuntimeStop();
	}

	void TestAuthoringAndRuntimeOutlinesMatch()
	{
		TomCat::Scene scene;
		TomCat::Entity entity = scene.CreateEntity("Outline parity");
		auto& transform = entity.GetComponent<TomCat::Transform>();
		transform._Translation = { 3.0f, -4.0f, 2.0f };
		transform._LocalTranslation = transform._Translation;
		transform._Rotation.z = 0.7f;
		transform._LocalRotation = transform._Rotation;
		transform._Scale = { -2.0f, 3.0f, 1.0f };
		transform._LocalScale = transform._Scale;
		entity.AddComponent<TomCat::Rigidbody2D>();
		auto& box = entity.AddComponent<TomCat::BoxCollider2D>();
		box.Offset = { 1.0f, -0.5f };
		box.Size = { 1.25f, 0.75f };
		box.IsTrigger = true;
		box.CollisionLayer = 0x0004;
		auto& circle = entity.AddComponent<TomCat::CircleCollider2D>();
		circle.Offset = { -0.25f, 1.5f };
		circle.Radius = 0.4f;
		circle.CollisionLayer = 0x0008;

		const auto authoringShapes = scene.GetColliderDebugShapes(false);
		Require(authoringShapes.size() == 2,
			"authoring visualization did not expose both colliders");
		scene.OnRuntimeStart();
		const auto runtimeShapes = scene.GetColliderDebugShapes(true);
		Require(runtimeShapes.size() == authoringShapes.size(),
			"runtime fixture visualization count differs from authoring visualization");
		for (const auto& authoring : authoringShapes)
		{
			const auto runtimeIt = std::find_if(runtimeShapes.begin(), runtimeShapes.end(),
				[&](const auto& runtime)
				{
					return runtime.EntityID == authoring.EntityID && runtime.Type == authoring.Type;
				});
			Require(runtimeIt != runtimeShapes.end(),
				"runtime fixture visualization omitted an authoring collider");
			Require(runtimeIt->Enabled == authoring.Enabled
				&& runtimeIt->IsTrigger == authoring.IsTrigger
				&& runtimeIt->CollisionLayer == authoring.CollisionLayer
				&& Near(runtimeIt->Center, authoring.Center)
				&& Near(runtimeIt->HalfSize, authoring.HalfSize)
				&& Near(runtimeIt->Radius, authoring.Radius)
				&& Near(runtimeIt->Rotation, authoring.Rotation)
				&& Near(runtimeIt->Transform, authoring.Transform),
				"authoring collider outline does not exactly match its Box2D fixture outline");
		}
		scene.OnRuntimeStop();
	}

	void TestTriggerAndScriptCallbacks()
	{
		PhysicsEventProbe::Reset();
		TomCat::Scene scene;
		TomCat::Entity trigger = scene.CreateEntity("Trigger");
		auto& triggerCollider = trigger.AddComponent<TomCat::CircleCollider2D>();
		triggerCollider.Radius = 2.0f;
		triggerCollider.IsTrigger = true;
		trigger.AddComponent<TomCat::NativeScript>().Bind<PhysicsEventProbe>();
		TomCat::Entity body = AddCircleBody(scene, "Trigger visitor",
			TomCat::Rigidbody2D::BodyType::Dynamic, { 0.0f, 0.0f }, 0.5f);
		body.AddComponent<TomCat::NativeScript>().Bind<PhysicsEventProbe>();

		int collisionEnters = 0;
		int triggerEnters = 0;
		int triggerExits = 0;
		scene.AddCollisionEnter2DListener([&](const TomCat::CollisionEnter2D&) { ++collisionEnters; });
		scene.AddTriggerEnter2DListener([&](const TomCat::TriggerEnter2D&) { ++triggerEnters; });
		scene.AddTriggerExit2DListener([&](const TomCat::TriggerExit2D&) { ++triggerExits; });
		scene.OnRuntimeStart();
		scene.OnRuntimeStep();
		Require(triggerEnters == 1 && collisionEnters == 0,
			"sensor contact was not routed exclusively as TriggerEnter2D");
		Require(PhysicsEventProbe::TriggerEnters == 2
			&& PhysicsEventProbe::CollisionEnters == 0,
			"both participating scripts did not receive TriggerEnter2D exactly once");
		Require(!PhysicsEventProbe::SawInvalidSelfPair,
			"script received a physics event for an unrelated entity pair");

		MoveRuntimeBody(body, { 10.0f, 0.0f });
		scene.OnRuntimeStep();
		Require(triggerExits == 1 && PhysicsEventProbe::TriggerExits == 2,
			"TriggerExit2D was not delivered exactly once globally and once per script");
		scene.OnRuntimeStep();
		Require(triggerEnters == 1 && triggerExits == 1,
			"persistent separation emitted duplicate trigger events");
		scene.OnRuntimeStop();
	}

	void TestCollisionFilteringAndRuntimeRebuild()
	{
		TomCat::Scene scene;
		TomCat::Entity staticEntity = scene.CreateEntity("Filtered static");
		auto& staticCollider = staticEntity.AddComponent<TomCat::CircleCollider2D>();
		staticCollider.Radius = 2.0f;
		staticCollider.CollisionLayer = 0x0001;
		staticCollider.CollisionMask = 0x0002;
		TomCat::Entity dynamicEntity = AddCircleBody(scene, "Filtered dynamic",
			TomCat::Rigidbody2D::BodyType::Dynamic, { 0.0f, 0.0f }, 1.0f);
		auto& dynamicCollider = dynamicEntity.GetComponent<TomCat::CircleCollider2D>();
		dynamicCollider.CollisionLayer = 0x0002;
		dynamicCollider.CollisionMask = 0x0000;

		int enters = 0;
		scene.AddCollisionEnter2DListener([&](const TomCat::CollisionEnter2D&) { ++enters; });
		scene.OnRuntimeStart();
		scene.OnRuntimeStep();
		Require(enters == 0, "collision layer/mask rejected pair still collided");

		dynamicCollider.CollisionMask = 0x0001;
		scene.OnRuntimeStep();
		Require(enters == 1,
			"runtime filter edit did not rebuild fixtures and enable contact at a safe step boundary");

		dynamicCollider.Radius = 3.0f;
		scene.OnRuntimeStep();
		const auto resized = scene.GetColliderDebugShapes(true);
		const auto resizedIt = std::find_if(resized.begin(), resized.end(), [&](const auto& shape)
		{
			return shape.EntityID == dynamicEntity.GetUUID()
				&& shape.Type == TomCat::ColliderDebugShapeType::Circle;
		});
		Require(resizedIt != resized.end() && Near(resizedIt->Radius, 3.0f),
			"runtime collider data edit did not rebuild the Box2D fixture");

		dynamicEntity.RemoveComponent<TomCat::CircleCollider2D>();
		scene.OnRuntimeStep();
		const auto removed = scene.GetColliderDebugShapes(true);
		Require(std::none_of(removed.begin(), removed.end(), [&](const auto& shape)
		{
			return shape.EntityID == dynamicEntity.GetUUID();
		}), "runtime collider removal left a stale Box2D fixture");

		auto& box = dynamicEntity.AddComponent<TomCat::BoxCollider2D>();
		box.Size = { 0.75f, 1.25f };
		scene.OnRuntimeStep();
		Require(box.RuntimeFixture != nullptr,
			"runtime collider addition did not create a Box2D fixture");
		box.Friction = 1.25f;
		scene.OnRuntimeStep();
		Require(box.RuntimeFixture != nullptr
			&& Near(static_cast<b2Fixture*>(box.RuntimeFixture)->GetFriction(), 1.25f),
			"runtime collider material edit did not rebuild the Box2D fixture");
		box.Enabled = false;
		scene.OnRuntimeStep();
		Require(box.RuntimeFixture == nullptr,
			"disabling a collider at runtime left its Box2D fixture enabled");
		box.Enabled = true;
		scene.OnRuntimeStep();
		Require(box.RuntimeFixture != nullptr,
			"re-enabling a collider at runtime did not recreate its Box2D fixture");

		auto& runtimeRigidbody = dynamicEntity.GetComponent<TomCat::Rigidbody2D>();
		runtimeRigidbody.Type = TomCat::Rigidbody2D::BodyType::Kinematic;
		runtimeRigidbody.FixedRotation = true;
		scene.OnRuntimeStep();
		Require(runtimeRigidbody.RuntimeBody != nullptr
			&& static_cast<b2Body*>(runtimeRigidbody.RuntimeBody)->GetType() == b2_kinematicBody
			&& static_cast<b2Body*>(runtimeRigidbody.RuntimeBody)->IsFixedRotation(),
			"runtime Rigidbody2D type/fixed-rotation edit was not rebuilt");
		runtimeRigidbody.Enabled = false;
		scene.OnRuntimeStep();
		Require(runtimeRigidbody.RuntimeBody == nullptr && box.RuntimeFixture == nullptr,
			"disabling Rigidbody2D did not remove its body and fixtures");
		runtimeRigidbody.Enabled = true;
		runtimeRigidbody.Type = TomCat::Rigidbody2D::BodyType::Dynamic;
		scene.OnRuntimeStep();
		Require(runtimeRigidbody.RuntimeBody != nullptr && box.RuntimeFixture != nullptr,
			"re-enabling Rigidbody2D did not recreate its body and fixtures");

		dynamicEntity.RemoveComponent<TomCat::Rigidbody2D>();
		scene.OnRuntimeStep();
		Require(box.RuntimeFixture != nullptr,
			"removing Rigidbody2D did not retain collider as a static runtime body");
		Require(!scene.ApplyLinearImpulse2D(dynamicEntity.GetUUID(), { 1.0f, 0.0f }),
			"force API accepted a collider-only static body");
		auto& addedRigidbody = dynamicEntity.AddComponent<TomCat::Rigidbody2D>();
		addedRigidbody.Type = TomCat::Rigidbody2D::BodyType::Dynamic;
		scene.OnRuntimeStep();
		Require(addedRigidbody.RuntimeBody != nullptr
			&& static_cast<b2Body*>(addedRigidbody.RuntimeBody)->GetType() == b2_dynamicBody,
			"runtime Rigidbody2D addition did not replace the implicit static body");
		scene.OnRuntimeStop();
	}

	void TestQueriesAndMotionAPI()
	{
		TomCat::Scene scene;
		TomCat::Entity solid = scene.CreateEntity("Query solid");
		auto& solidTransform = solid.GetComponent<TomCat::Transform>();
		solidTransform._Translation.x = 3.0f;
		solidTransform._LocalTranslation = solidTransform._Translation;
		auto& solidBox = solid.AddComponent<TomCat::BoxCollider2D>();
		solidBox.Size = { 0.5f, 0.5f };
		solidBox.CollisionLayer = 0x0004;

		TomCat::Entity trigger = scene.CreateEntity("Query trigger");
		auto& triggerTransform = trigger.GetComponent<TomCat::Transform>();
		triggerTransform._Translation.x = 6.0f;
		triggerTransform._LocalTranslation = triggerTransform._Translation;
		auto& triggerCircle = trigger.AddComponent<TomCat::CircleCollider2D>();
		triggerCircle.Radius = 1.0f;
		triggerCircle.IsTrigger = true;
		triggerCircle.CollisionLayer = 0x0008;

		TomCat::Entity dynamicEntity = AddCircleBody(scene, "Motion API body",
			TomCat::Rigidbody2D::BodyType::Dynamic, { 20.0f, 0.0f }, 1.0f);
		scene.OnRuntimeStart();

		const auto rayHit = scene.Raycast2D({ 0.0f, 0.0f }, { 10.0f, 0.0f }, 0x000C, true);
		Require(rayHit && rayHit->EntityID == solid.GetUUID() && !rayHit->IsTrigger,
			"raycast did not return the nearest matching fixture");
		const auto triggerRay = scene.Raycast2D({ 4.0f, 0.0f }, { 10.0f, 0.0f }, 0x0008, true);
		Require(triggerRay && triggerRay->EntityID == trigger.GetUUID() && triggerRay->IsTrigger,
			"raycast did not include a matching trigger");
		Require(!scene.Raycast2D({ 4.0f, 0.0f }, { 10.0f, 0.0f }, 0x0008, false),
			"raycast includeTriggers=false still returned a sensor");

		const auto allHits = scene.QueryAABB2D({ 1.0f, -2.0f }, { 8.0f, 2.0f }, 0x000C, true);
		Require(allHits.size() == 2, "AABB query did not return both matching entities");
		const auto solidHits = scene.QueryAABB2D({ 1.0f, -2.0f }, { 8.0f, 2.0f }, 0x000C, false);
		Require(solidHits.size() == 1 && solidHits[0].EntityID == solid.GetUUID(),
			"AABB query trigger exclusion failed");

		Require(scene.SetLinearVelocity2D(dynamicEntity.GetUUID(), { 3.0f, 4.0f }),
			"SetLinearVelocity2D rejected a dynamic body");
		const auto initialVelocity = scene.GetLinearVelocity2D(dynamicEntity.GetUUID());
		Require(initialVelocity && Near(initialVelocity->x, 3.0f) && Near(initialVelocity->y, 4.0f),
			"GetLinearVelocity2D did not return the assigned velocity");
		Require(scene.ApplyLinearImpulse2D(dynamicEntity.GetUUID(), { 2.0f, 0.0f }),
			"ApplyLinearImpulse2D rejected a dynamic body");
		const auto impulseVelocity = scene.GetLinearVelocity2D(dynamicEntity.GetUUID());
		Require(impulseVelocity && impulseVelocity->x > initialVelocity->x,
			"linear impulse did not change velocity immediately");
		Require(scene.ApplyForce2D(dynamicEntity.GetUUID(), { 60.0f, 0.0f }),
			"ApplyForce2D rejected a dynamic body");
		Require(scene.ApplyForceAtPoint2D(dynamicEntity.GetUUID(), { 0.0f, 20.0f },
			{ 21.0f, 0.0f }), "ApplyForceAtPoint2D rejected a dynamic body");
		Require(scene.ApplyLinearImpulseAtPoint2D(dynamicEntity.GetUUID(), { 0.0f, 2.0f },
			{ 21.0f, 0.0f }), "ApplyLinearImpulseAtPoint2D rejected a dynamic body");
		auto* motionBody = static_cast<b2Body*>(
			dynamicEntity.GetComponent<TomCat::Rigidbody2D>().RuntimeBody);
		Require(motionBody && std::abs(motionBody->GetAngularVelocity()) > 0.0f,
			"at-point impulse did not produce angular velocity");
		const float beforeForceStep = impulseVelocity->x;
		scene.OnRuntimeStep();
		const auto forceVelocity = scene.GetLinearVelocity2D(dynamicEntity.GetUUID());
		Require(forceVelocity && forceVelocity->x > beforeForceStep,
			"force did not change velocity on the next fixed step");
		Require(!scene.SetLinearVelocity2D(solid.GetUUID(), { 1.0f, 0.0f }),
			"velocity API accepted a collider-only static body");
		scene.OnRuntimeStop();
	}

	void TestDistanceJoint()
	{
		TomCat::Scene scene;
		TomCat::Entity bodyA = AddCircleBody(scene, "Joint A",
			TomCat::Rigidbody2D::BodyType::Dynamic, { -2.0f, 5.0f });
		TomCat::Entity bodyB = AddCircleBody(scene, "Joint B",
			TomCat::Rigidbody2D::BodyType::Dynamic, { 2.0f, 5.0f });
		auto& joint = bodyA.AddComponent<TomCat::DistanceJoint2D>();
		joint.ConnectedEntity = bodyB.GetUUID();
		joint.Distance = 2.0f;
		joint.Frequency = 0.0f;
		joint.Damping = 0.0f;

		scene.OnRuntimeStart();
		Require(joint.RuntimeJoint != nullptr, "DistanceJoint2D did not create a Box2D joint");
		for (int step = 0; step < 120; ++step)
			scene.OnRuntimeStep();
		auto* runtimeA = static_cast<b2Body*>(bodyA.GetComponent<TomCat::Rigidbody2D>().RuntimeBody);
		auto* runtimeB = static_cast<b2Body*>(bodyB.GetComponent<TomCat::Rigidbody2D>().RuntimeBody);
		Require(runtimeA && runtimeB, "joint bodies were not created");
		const float distance = (runtimeB->GetPosition() - runtimeA->GetPosition()).Length();
		Require(Near(distance, 2.0f, 2.0e-2f), "DistanceJoint2D did not maintain its configured length");

		joint.Distance = 1.0f;
		scene.OnRuntimeStep();
		Require(joint.RuntimeJoint != nullptr, "runtime joint edit did not recreate the Box2D joint");
		for (int step = 0; step < 120; ++step)
			scene.OnRuntimeStep();
		runtimeA = static_cast<b2Body*>(bodyA.GetComponent<TomCat::Rigidbody2D>().RuntimeBody);
		runtimeB = static_cast<b2Body*>(bodyB.GetComponent<TomCat::Rigidbody2D>().RuntimeBody);
		Require(Near((runtimeB->GetPosition() - runtimeA->GetPosition()).Length(), 1.0f, 2.0e-2f),
			"runtime Distance edit was not reflected in the Box2D joint");

		bodyA.RemoveComponent<TomCat::DistanceJoint2D>();
		scene.OnRuntimeStep();
		auto& replacementJoint = bodyA.AddComponent<TomCat::DistanceJoint2D>();
		replacementJoint.ConnectedEntity = bodyB.GetUUID();
		replacementJoint.Distance = 1.0f;
		scene.OnRuntimeStep();
		Require(replacementJoint.RuntimeJoint != nullptr,
			"runtime DistanceJoint2D removal/addition did not rebuild safely");
		scene.DestroyEntity(bodyB);
		scene.OnRuntimeStep();
		Require(replacementJoint.RuntimeJoint == nullptr
			&& static_cast<uint64_t>(replacementJoint.ConnectedEntity) == 0,
			"deleting a connected entity left a dangling joint reference or runtime joint");
		scene.OnRuntimeStop();
	}

	void RequireSameCircleFields(const TomCat::CircleCollider2D& actual,
		const TomCat::CircleCollider2D& expected, const char* context)
	{
		Require(actual.Enabled == expected.Enabled, context);
		Require(actual.IsTrigger == expected.IsTrigger, context);
		Require(actual.CollisionLayer == expected.CollisionLayer, context);
		Require(actual.CollisionMask == expected.CollisionMask, context);
		Require(Near(actual.Offset.x, expected.Offset.x) && Near(actual.Offset.y, expected.Offset.y), context);
		Require(Near(actual.Radius, expected.Radius), context);
		Require(Near(actual.Density, expected.Density), context);
		Require(Near(actual.Friction, expected.Friction), context);
		Require(Near(actual.Restitution, expected.Restitution), context);
	}

	void RequireSameBoxFields(const TomCat::BoxCollider2D& actual,
		const TomCat::BoxCollider2D& expected, const char* context)
	{
		Require(actual.Enabled == expected.Enabled, context);
		Require(actual.IsTrigger == expected.IsTrigger, context);
		Require(actual.CollisionLayer == expected.CollisionLayer, context);
		Require(actual.CollisionMask == expected.CollisionMask, context);
		Require(Near(actual.Offset.x, expected.Offset.x) && Near(actual.Offset.y, expected.Offset.y), context);
		Require(Near(actual.Size.x, expected.Size.x) && Near(actual.Size.y, expected.Size.y), context);
		Require(Near(actual.Density, expected.Density), context);
		Require(Near(actual.Friction, expected.Friction), context);
		Require(Near(actual.Restitution, expected.Restitution), context);
		Require(Near(actual.RestitutionThreshold, expected.RestitutionThreshold), context);
	}

	void RequireSameJointFields(const TomCat::DistanceJoint2D& actual,
		const TomCat::DistanceJoint2D& expected, const char* context)
	{
		Require(actual.Enabled == expected.Enabled, context);
		Require(actual.ConnectedEntity == expected.ConnectedEntity, context);
		Require(Near(actual.Anchor.x, expected.Anchor.x) && Near(actual.Anchor.y, expected.Anchor.y), context);
		Require(Near(actual.ConnectedAnchor.x, expected.ConnectedAnchor.x)
			&& Near(actual.ConnectedAnchor.y, expected.ConnectedAnchor.y), context);
		Require(Near(actual.Distance, expected.Distance), context);
		Require(Near(actual.Frequency, expected.Frequency), context);
		Require(Near(actual.Damping, expected.Damping), context);
		Require(actual.CollideConnected == expected.CollideConnected, context);
	}

	void TestSchemaV7PersistenceAndCopies()
	{
		const std::filesystem::path scenePath =
			std::filesystem::temp_directory_path() / "tomcat_physics_v7_roundtrip.tomcat";
		auto source = TomCat::CreateRef<TomCat::Scene>();
		source->SetSceneName("Physics v7 roundtrip");
		TomCat::Entity entity = source->CreateEntity("Circle source");
		const TomCat::UUID sourceUUID = entity.GetUUID();
		auto& circle = entity.AddComponent<TomCat::CircleCollider2D>();
		circle.Enabled = false;
		circle.IsTrigger = true;
		circle.CollisionLayer = 0x0040;
		circle.CollisionMask = 0x0081;
		circle.Offset = { 1.25f, -2.5f };
		circle.Radius = 3.75f;
		circle.Density = 2.25f;
		circle.Friction = 1.25f;
		circle.Restitution = 0.65f;
		circle.RuntimeFixture = reinterpret_cast<void*>(static_cast<uintptr_t>(0x1234));
		const TomCat::CircleCollider2D expectedCircle = circle;

		TomCat::Entity target = source->CreateEntity("Joint target");
		const TomCat::UUID targetUUID = target.GetUUID();
		auto& box = target.AddComponent<TomCat::BoxCollider2D>();
		box.Enabled = false;
		box.IsTrigger = true;
		box.CollisionLayer = 0x0200;
		box.CollisionMask = 0x0104;
		box.Offset = { -1.5f, 2.75f };
		box.Size = { 4.0f, 5.0f };
		box.Density = 3.0f;
		box.Friction = 0.25f;
		box.Restitution = 0.8f;
		box.RestitutionThreshold = 1.75f;
		box.RuntimeFixture = reinterpret_cast<void*>(static_cast<uintptr_t>(0x3456));
		const TomCat::BoxCollider2D expectedBox = box;
		auto& joint = entity.AddComponent<TomCat::DistanceJoint2D>();
		joint.Enabled = false;
		joint.ConnectedEntity = targetUUID;
		joint.Anchor = { 1.0f, 2.0f };
		joint.ConnectedAnchor = { -3.0f, 4.0f };
		joint.Distance = 5.0f;
		joint.Frequency = 2.0f;
		joint.Damping = 0.75f;
		joint.CollideConnected = true;
		joint.RuntimeJoint = reinterpret_cast<void*>(static_cast<uintptr_t>(0x5678));
		const TomCat::DistanceJoint2D expectedJoint = joint;

		auto copiedScene = TomCat::Scene::Copy(source);
		Require(copiedScene != nullptr, "Scene::Copy failed for physics v7 components");
		TomCat::Entity copiedEntity = copiedScene->FindEntityByUUID(sourceUUID);
		Require(copiedEntity && copiedEntity.HasComponent<TomCat::CircleCollider2D>()
			&& copiedEntity.HasComponent<TomCat::DistanceJoint2D>(),
			"Scene::Copy omitted a physics v7 component");
		RequireSameCircleFields(copiedEntity.GetComponent<TomCat::CircleCollider2D>(), expectedCircle,
			"Scene::Copy changed CircleCollider2D fields");
		RequireSameJointFields(copiedEntity.GetComponent<TomCat::DistanceJoint2D>(), expectedJoint,
			"Scene::Copy changed DistanceJoint2D fields");
		Require(copiedEntity.GetComponent<TomCat::CircleCollider2D>().RuntimeFixture == nullptr
			&& copiedEntity.GetComponent<TomCat::DistanceJoint2D>().RuntimeJoint == nullptr,
			"Scene::Copy retained runtime physics pointers");
		TomCat::Entity copiedTarget = copiedScene->FindEntityByUUID(targetUUID);
		Require(copiedTarget && copiedTarget.HasComponent<TomCat::BoxCollider2D>(),
			"Scene::Copy omitted BoxCollider2D");
		RequireSameBoxFields(copiedTarget.GetComponent<TomCat::BoxCollider2D>(), expectedBox,
			"Scene::Copy changed BoxCollider2D fields");
		Require(copiedTarget.GetComponent<TomCat::BoxCollider2D>().RuntimeFixture == nullptr,
			"Scene::Copy retained BoxCollider2D RuntimeFixture");

		TomCat::Entity duplicate = source->DuplicateEntity(entity);
		Require(duplicate && duplicate.HasComponent<TomCat::CircleCollider2D>()
			&& duplicate.HasComponent<TomCat::DistanceJoint2D>(),
			"DuplicateEntity omitted a physics v7 component");
		RequireSameCircleFields(duplicate.GetComponent<TomCat::CircleCollider2D>(), expectedCircle,
			"DuplicateEntity changed CircleCollider2D fields");
		RequireSameJointFields(duplicate.GetComponent<TomCat::DistanceJoint2D>(), expectedJoint,
			"DuplicateEntity changed DistanceJoint2D fields");
		Require(duplicate.GetComponent<TomCat::CircleCollider2D>().RuntimeFixture == nullptr
			&& duplicate.GetComponent<TomCat::DistanceJoint2D>().RuntimeJoint == nullptr,
			"DuplicateEntity retained runtime physics pointers");
		TomCat::Entity duplicateTarget = source->DuplicateEntity(target);
		Require(duplicateTarget && duplicateTarget.HasComponent<TomCat::BoxCollider2D>(),
			"DuplicateEntity omitted BoxCollider2D");
		RequireSameBoxFields(duplicateTarget.GetComponent<TomCat::BoxCollider2D>(), expectedBox,
			"DuplicateEntity changed BoxCollider2D fields");
		Require(duplicateTarget.GetComponent<TomCat::BoxCollider2D>().RuntimeFixture == nullptr,
			"DuplicateEntity retained BoxCollider2D RuntimeFixture");

		TomCat::SceneSerializer writer(source);
		Require(writer.Serialize(scenePath), "physics v7 scene serialization failed");
		Require(TomCat::SceneSerializer::ValidateCurrentFormat(scenePath),
			"serialized physics v7 scene failed strict validation");
		std::ifstream serializedFile(scenePath, std::ios::binary);
		const std::string serialized((std::istreambuf_iterator<char>(serializedFile)),
			std::istreambuf_iterator<char>());
		serializedFile.close();
		Require(serialized.find("SchemaVersion: 7") != std::string::npos,
			"serialized scene did not declare schema v7");
		Require(serialized.find("IsTrigger:") != std::string::npos
			&& serialized.find("CollisionLayer:") != std::string::npos
			&& serialized.find("CollisionMask:") != std::string::npos
			&& serialized.find("DistanceJoint2D:") != std::string::npos,
			"serialized scene omitted physics v7 fields");

		auto loaded = TomCat::CreateRef<TomCat::Scene>();
		TomCat::SceneSerializer reader(loaded);
		Require(reader.Deserialize(scenePath), "physics v7 scene deserialization failed");
		TomCat::Entity loadedEntity = loaded->FindEntityByUUID(sourceUUID);
		Require(loadedEntity && loadedEntity.HasComponent<TomCat::CircleCollider2D>()
			&& loadedEntity.HasComponent<TomCat::DistanceJoint2D>(),
			"loaded scene omitted a physics v7 component");
		RequireSameCircleFields(loadedEntity.GetComponent<TomCat::CircleCollider2D>(), expectedCircle,
			"save/load changed CircleCollider2D fields");
		RequireSameJointFields(loadedEntity.GetComponent<TomCat::DistanceJoint2D>(), expectedJoint,
			"save/load changed DistanceJoint2D fields");
		Require(loadedEntity.GetComponent<TomCat::CircleCollider2D>().RuntimeFixture == nullptr
			&& loadedEntity.GetComponent<TomCat::DistanceJoint2D>().RuntimeJoint == nullptr,
			"save/load restored runtime physics pointers");
		TomCat::Entity loadedTarget = loaded->FindEntityByUUID(targetUUID);
		Require(loadedTarget && loadedTarget.HasComponent<TomCat::BoxCollider2D>(),
			"loaded scene omitted BoxCollider2D");
		RequireSameBoxFields(loadedTarget.GetComponent<TomCat::BoxCollider2D>(), expectedBox,
			"save/load changed BoxCollider2D fields");
		Require(loadedTarget.GetComponent<TomCat::BoxCollider2D>().RuntimeFixture == nullptr,
			"save/load restored BoxCollider2D RuntimeFixture");

		std::string obsolete = serialized;
		const size_t version = obsolete.find("SchemaVersion: 7");
		Require(version != std::string::npos, "could not locate serialized schema version");
		obsolete.replace(version, std::string("SchemaVersion: 7").size(), "SchemaVersion: 6");
		const std::filesystem::path obsoletePath =
			std::filesystem::temp_directory_path() / "tomcat_physics_v6_rejected.tomcat";
		{
			std::ofstream obsoleteFile(obsoletePath, std::ios::binary | std::ios::trunc);
			obsoleteFile << obsolete;
		}
		Require(!TomCat::SceneSerializer::ValidateCurrentFormat(obsoletePath),
			"strict current-format validation accepted schema v6");

		auto invalidScene = TomCat::CreateRef<TomCat::Scene>();
		TomCat::Entity invalidOwner = invalidScene->CreateEntity("Invalid joint owner");
		invalidOwner.AddComponent<TomCat::DistanceJoint2D>().ConnectedEntity = TomCat::UUID(9999999);
		const std::filesystem::path invalidPath =
			std::filesystem::temp_directory_path() / "tomcat_physics_invalid_joint.tomcat";
		TomCat::SceneSerializer invalidWriter(invalidScene);
		Require(!invalidWriter.Serialize(invalidPath),
			"scene writer emitted an unresolved DistanceJoint2D reference");
		std::error_code error;
		std::filesystem::remove(scenePath, error);
		std::filesystem::remove(obsoletePath, error);
		std::filesystem::remove(invalidPath, error);
	}

	void TestCookedPlayerPhysicsRoundtrip()
	{
		TemporaryCookedProject environment;
		TomCat::ProjectConfig config;
		config.Name = "Physics Cook Regression";
		config.Template = "2D";
		config.AssetDirectory = "Assets";
		config.StartScene = "Main.tomcat";
		config.StartSceneHandle = TomCat::AssetHandle(0);
		auto project = TomCat::Project::CreateNew(
			environment.Root / "Project.tcproj", config);
		Require(project != nullptr, "could not create temporary project for cooked physics test");

		TomCat::AssetManager& assets = TomCat::AssetManager::Get();
		Require(assets.SetProject(project),
			"could not initialize AssetManager for cooked physics test");

		auto source = TomCat::CreateRef<TomCat::Scene>();
		source->SetSceneName("Cooked physics v7");
		TomCat::Entity ground = source->CreateEntity("Cooked box");
		const TomCat::UUID groundUUID = ground.GetUUID();
		ground.AddComponent<TomCat::Rigidbody2D>().Type =
			TomCat::Rigidbody2D::BodyType::Static;
		auto& box = ground.AddComponent<TomCat::BoxCollider2D>();
		box.IsTrigger = true;
		box.CollisionLayer = 0x0010;
		box.CollisionMask = 0x0020;
		box.Offset = { 0.5f, -0.25f };
		box.Size = { 2.0f, 0.75f };
		box.Density = 1.5f;
		box.Friction = 0.35f;
		box.Restitution = 0.45f;
		box.RestitutionThreshold = 1.25f;
		const TomCat::BoxCollider2D expectedBox = box;

		TomCat::Entity ball = source->CreateEntity("Cooked circle");
		const TomCat::UUID ballUUID = ball.GetUUID();
		ball.GetComponent<TomCat::Transform>()._Translation = { 2.0f, 3.0f, 0.0f };
		ball.GetComponent<TomCat::Transform>()._LocalTranslation =
			ball.GetComponent<TomCat::Transform>()._Translation;
		ball.AddComponent<TomCat::Rigidbody2D>().Type =
			TomCat::Rigidbody2D::BodyType::Dynamic;
		auto& circle = ball.AddComponent<TomCat::CircleCollider2D>();
		circle.CollisionLayer = 0x0020;
		circle.CollisionMask = 0x0010;
		circle.Offset = { -0.75f, 0.25f };
		circle.Radius = 0.625f;
		circle.Density = 2.0f;
		circle.Friction = 0.2f;
		circle.Restitution = 0.6f;
		const TomCat::CircleCollider2D expectedCircle = circle;

		auto& joint = ground.AddComponent<TomCat::DistanceJoint2D>();
		joint.ConnectedEntity = ballUUID;
		joint.Anchor = { 0.25f, 0.5f };
		joint.ConnectedAnchor = { -0.5f, -0.25f };
		joint.Distance = 3.0f;
		joint.Frequency = 2.5f;
		joint.Damping = 0.4f;
		joint.CollideConnected = true;
		const TomCat::DistanceJoint2D expectedJoint = joint;

		const std::filesystem::path sourceScenePath = project->GetAssetPath() / "Main.tomcat";
		TomCat::SceneSerializer writer(source);
		Require(writer.Serialize(sourceScenePath),
			"could not serialize/import schema-v7 physics scene for cooking");
		const TomCat::AssetMetadata* sceneMetadata = assets.Registry().GetMetadata(sourceScenePath);
		Require(sceneMetadata && sceneMetadata->Type == TomCat::AssetType::Scene
			&& static_cast<uint64_t>(sceneMetadata->Handle) != 0,
			"serialized physics scene was not registered as a Scene asset");
		const TomCat::AssetHandle sceneHandle = sceneMetadata->Handle;
		Require(project->SetStartScene("Main.tomcat"),
			"could not set temporary project's start-scene path");
		project->SetStartSceneHandle(sceneHandle);
		Require(project->Save(), "could not save temporary project's start-scene handle");

		const std::filesystem::path packagePath = environment.Root / "Build" / "Game.tcpak";
		Require(assets.CookToPackage(packagePath),
			"schema-v7 physics scene did not cook into a Player package");
		assets.Shutdown();

		Require(assets.MountCookedPackage(packagePath),
			"Player path could not mount the cooked physics package");
		Require(assets.GetCookedStartSceneHandle() == sceneHandle,
			"cooked package did not preserve its start-scene handle");
		std::vector<uint8_t> cookedBytes;
		TomCat::AssetType cookedType = TomCat::AssetType::None;
		Require(assets.ReadAssetBytes(sceneHandle, cookedBytes, &cookedType)
			&& cookedType == TomCat::AssetType::Scene
			&& TomCat::SceneSerializer::ValidateCurrentFormat(cookedBytes, "CookedPhysicsRegression"),
			"cooked scene payload was not a complete schema-v7 Scene");

		auto loaded = TomCat::CreateRef<TomCat::Scene>();
		TomCat::SceneSerializer reader(loaded);
		Require(reader.Deserialize(sceneHandle),
			"Cooked Player scene path could not deserialize physics components");
		TomCat::Entity loadedGround = loaded->FindEntityByUUID(groundUUID);
		TomCat::Entity loadedBall = loaded->FindEntityByUUID(ballUUID);
		Require(loadedGround && loadedBall
			&& loadedGround.HasComponent<TomCat::BoxCollider2D>()
			&& loadedGround.HasComponent<TomCat::DistanceJoint2D>()
			&& loadedBall.HasComponent<TomCat::CircleCollider2D>(),
			"Cooked Player scene omitted Box/Circle/DistanceJoint components");
		RequireSameBoxFields(loadedGround.GetComponent<TomCat::BoxCollider2D>(), expectedBox,
			"Cooked Player changed BoxCollider2D fields");
		RequireSameCircleFields(loadedBall.GetComponent<TomCat::CircleCollider2D>(), expectedCircle,
			"Cooked Player changed CircleCollider2D fields");
		RequireSameJointFields(loadedGround.GetComponent<TomCat::DistanceJoint2D>(), expectedJoint,
			"Cooked Player changed DistanceJoint2D fields");
		Require(loadedGround.GetComponent<TomCat::BoxCollider2D>().RuntimeFixture == nullptr
			&& loadedBall.GetComponent<TomCat::CircleCollider2D>().RuntimeFixture == nullptr
			&& loadedGround.GetComponent<TomCat::DistanceJoint2D>().RuntimeJoint == nullptr,
			"cooked deserialization restored runtime physics pointers");

		loaded->OnRuntimeStart();
		Require(loadedGround.GetComponent<TomCat::BoxCollider2D>().RuntimeFixture != nullptr
			&& loadedBall.GetComponent<TomCat::CircleCollider2D>().RuntimeFixture != nullptr
			&& loadedGround.GetComponent<TomCat::DistanceJoint2D>().RuntimeJoint != nullptr,
			"Cooked Player runtime did not create Box/Circle fixtures and DistanceJoint");
		loaded->OnRuntimeStep();
		loaded->OnRuntimeStop();
		Require(loadedGround.GetComponent<TomCat::BoxCollider2D>().RuntimeFixture == nullptr
			&& loadedBall.GetComponent<TomCat::CircleCollider2D>().RuntimeFixture == nullptr
			&& loadedGround.GetComponent<TomCat::DistanceJoint2D>().RuntimeJoint == nullptr,
			"Cooked Player Stop did not clear runtime physics pointers");
	}

	TomCat::Entity MakeMultiFixtureBody(TomCat::Scene& scene, const char* name,
		TomCat::Rigidbody2D::BodyType type)
	{
		TomCat::Entity entity = scene.CreateEntity(name);
		auto& body = entity.AddComponent<TomCat::Rigidbody2D>();
		body.Type = type;
		auto& box = entity.AddComponent<TomCat::BoxCollider2D>();
		box.Size = { 5.0f, 5.0f };
		entity.AddComponent<TomCat::CircleCollider2D>().Radius = 4.0f;
		return entity;
	}

	void TestCollisionPairDeduplication()
	{
		PhysicsEventProbe::Reset();
		TomCat::Scene scene;
		TomCat::Entity entityA = MakeMultiFixtureBody(scene, "Static",
			TomCat::Rigidbody2D::BodyType::Static);
		TomCat::Entity entityB = MakeMultiFixtureBody(scene, "Dynamic",
			TomCat::Rigidbody2D::BodyType::Dynamic);
		entityA.AddComponent<TomCat::NativeScript>().Bind<PhysicsEventProbe>();
		entityB.AddComponent<TomCat::NativeScript>().Bind<PhysicsEventProbe>();
		int enters = 0;
		int exits = 0;
		uint64_t lastA = 0;
		uint64_t lastB = 0;
		scene.AddCollisionEnter2DListener([&](const TomCat::CollisionEnter2D& event)
		{
			++enters;
			lastA = static_cast<uint64_t>(event.EntityA);
			lastB = static_cast<uint64_t>(event.EntityB);
		});
		scene.AddCollisionExit2DListener([&](const TomCat::CollisionExit2D&) { ++exits; });

		scene.OnRuntimeStart();
		scene.OnRuntimeStep();
		Require(enters == 1, "multiple fixtures emitted more than one entity-pair Enter");
		Require(PhysicsEventProbe::CollisionEnters == 2,
			"collision Enter did not reach both participating scripts exactly once");
		Require(lastA < lastB, "collision UUID pair was not canonicalized");
		scene.OnRuntimeStep();
		Require(enters == 1 && exits == 0 && PhysicsEventProbe::CollisionEnters == 2,
			"persistent contact emitted duplicate events");

		MoveRuntimeBody(entityB, { 100.0f, 0.0f });
		scene.OnRuntimeStep();
		Require(enters == 1 && exits == 1, "entity-pair Exit was not emitted exactly once");
		Require(PhysicsEventProbe::CollisionExits == 2,
			"collision Exit did not reach both participating scripts exactly once");
		scene.OnRuntimeStop();
	}

	void TestDeletionDuringCollisionDispatch()
	{
		TomCat::Scene scene;
		TomCat::Entity entityA = AddCircleBody(scene, "Delete A",
			TomCat::Rigidbody2D::BodyType::Static, { 0.0f, 0.0f }, 2.0f);
		TomCat::Entity entityB = AddCircleBody(scene, "Delete B",
			TomCat::Rigidbody2D::BodyType::Dynamic, { 0.0f, 0.0f }, 2.0f);

		int destructiveListenerCalls = 0;
		int laterListenerCalls = 0;
		int exitCalls = 0;
		TomCat::UUID deletedUUID(0);
		scene.AddCollisionEnter2DListener([&](const TomCat::CollisionEnter2D& event)
		{
			++destructiveListenerCalls;
			deletedUUID = event.EntityB;
			TomCat::Entity victim = scene.FindEntityByUUID(event.EntityB);
			if (victim)
				scene.DestroyEntity(victim);
		});
		scene.AddCollisionEnter2DListener([&](const TomCat::CollisionEnter2D&)
		{
			++laterListenerCalls;
		});
		scene.AddCollisionExit2DListener([&](const TomCat::CollisionExit2D&) { ++exitCalls; });

		scene.OnRuntimeStart();
		scene.OnRuntimeStep();
		Require(destructiveListenerCalls == 1, "destructive collision listener did not run once");
		Require(laterListenerCalls == 0, "listener received an event after an entity was deleted");
		Require(static_cast<uint64_t>(deletedUUID) != 0 && !scene.FindEntityByUUID(deletedUUID),
			"collision callback did not delete its target");
		scene.OnRuntimeStep();
		Require(exitCalls == 0, "entity deletion leaked a stale Exit event");
		scene.OnRuntimeStop();
	}

	void TestNativeScriptMutationDuringCollisionDispatch()
	{
		auto runMutation = [](CollisionMutationProbe::Action action)
		{
			TomCat::Scene scene;
			CollisionMutationProbe::Reset(scene, action);
			TomCat::Entity scripted = AddCircleBody(scene, "Mutating script",
				TomCat::Rigidbody2D::BodyType::Static, { 0.0f, 0.0f }, 2.0f);
			const TomCat::UUID scriptedUUID = scripted.GetUUID();
			scripted.AddComponent<TomCat::NativeScript>().Bind<CollisionMutationProbe>();
			AddCircleBody(scene, "Mutation contact",
				TomCat::Rigidbody2D::BodyType::Dynamic, { 0.0f, 0.0f }, 2.0f);

			int laterListenerCalls = 0;
			scene.AddCollisionEnter2DListener(
				[&](const TomCat::CollisionEnter2D&) { ++laterListenerCalls; });
			scene.OnRuntimeStart();
			scene.OnRuntimeStep();

			Require(CollisionMutationProbe::CollisionEnters == 1,
				"mutating native script did not receive CollisionEnter2D exactly once");
			Require(CollisionMutationProbe::CallbackContinuedSafely,
				"native script or Entity died before its collision callback returned");
			Require(CollisionMutationProbe::Destroys == 1
				&& !CollisionMutationProbe::DestroyedDuringCallback
				&& CollisionMutationProbe::EntityWasValidDuringDestroy,
				"native script destruction did not run once at a safe callback boundary");

			TomCat::Entity survivingScripted = scene.FindEntityByUUID(scriptedUUID);
			if (action == CollisionMutationProbe::Action::DestroyEntity)
			{
				Require(!survivingScripted,
					"DestroyEntity requested by a native collision callback was not flushed");
				Require(laterListenerCalls == 0,
					"collision dispatch continued after a script deleted a participant");
			}
			else
			{
				Require(survivingScripted, "component mutation unexpectedly deleted its Entity");
				Require(laterListenerCalls == 1,
					"component-only script mutation incorrectly cancelled the Scene event");
				if (action == CollisionMutationProbe::Action::RemoveComponent)
					Require(!survivingScripted.HasComponent<TomCat::NativeScript>(),
						"native script component removal was not committed");
				else
				{
					Require(survivingScripted.HasComponent<TomCat::NativeScript>()
						&& survivingScripted.GetComponent<TomCat::NativeScript>().Instance == nullptr,
						"native script replacement retained the destroyed runtime instance");
					scene.OnRuntimeStep();
					Require(ReplacementScriptProbe::Creates == 1
						&& ReplacementScriptProbe::Updates == 1,
						"replacement native script was not created on the next fixed step");
				}
			}

			scene.OnRuntimeStop();
			Require(CollisionMutationProbe::Destroys == 1,
				"mutated native script instance was destroyed more than once");
			if (action == CollisionMutationProbe::Action::ReplaceComponent)
				Require(ReplacementScriptProbe::Destroys == 1,
					"replacement native script was not destroyed on runtime stop");
		};

		runMutation(CollisionMutationProbe::Action::RemoveComponent);
		runMutation(CollisionMutationProbe::Action::ReplaceComponent);
		runMutation(CollisionMutationProbe::Action::DestroyEntity);
	}

}

int main()
{
	TomCat::Log::Init();
	int failures = 0;
	auto run = [&](const char* name, auto&& test)
	{
		try
		{
			test();
			std::cout << "PASS " << name << '\n';
		}
		catch (const std::exception& exception)
		{
			++failures;
			std::cerr << "FAIL " << name << ": " << exception.what() << '\n';
		}
	};

	run("fixed accumulator and exact Step", TestFixedAccumulatorAndStep);
	run("30/60/144Hz one- and ten-second consistency", TestFrameRateIndependentPhysics);
	run("Pause render path and exact single-step", TestPauseRenderPathAndSingleStep);
	run("collider-only static runtime body", TestColliderOnlyStaticBody);
	run("CircleCollider2D non-uniform fixture", TestCircleNonUniformScaleFixture);
	run("authoring/runtime collider outline parity", TestAuthoringAndRuntimeOutlinesMatch);
	run("Trigger events and native-script callbacks", TestTriggerAndScriptCallbacks);
	run("layer/mask filtering and runtime rebuild", TestCollisionFilteringAndRuntimeRebuild);
	run("raycast, AABB query, force, impulse, and velocity", TestQueriesAndMotionAPI);
	run("DistanceJoint2D runtime creation and rebuild", TestDistanceJoint);
	run("schema v7 save/load/copy/duplicate", TestSchemaV7PersistenceAndCopies);
	run("Cooked Player physics roundtrip", TestCookedPlayerPhysicsRoundtrip);
	run("collision Enter/Exit entity-pair de-duplication", TestCollisionPairDeduplication);
	run("collision callback deletion safety", TestDeletionDuringCollisionDispatch);
	run("native-script collision mutation safety", TestNativeScriptMutationDuringCollisionDispatch);
	return failures == 0 ? 0 : 1;
}
