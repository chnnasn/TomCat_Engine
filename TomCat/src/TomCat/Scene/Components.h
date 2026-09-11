#pragma once

#include "TomCat/Asset/Asset.h"
#include "SceneCamera.h"
#include "TomCat/Core/UUID.h"
#include "TomCat/Math/Math.h"
#include "TomCat/Renderer/Texture.h"

#include <cstdint>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>


#define GLM_ENABLE_EXPERIMENTAL
#include <glm/gtx/quaternion.hpp>



namespace TomCat {
	

	struct ID
	{
		UUID id;

		ID() = default;
		ID(const ID&) = default;
		ID(const UUID& uuid)
			: id(uuid) {}
	};

	struct Tag
	{
		std::string _Tag;
		bool Visible = true;

		Tag() = default;
		Tag(const Tag&) = default;
		Tag(const std::string& tag)
			: _Tag(tag) {
		}
	};


	struct Transform
	{
		// World-space transform.
		glm::vec3 _Translation{ 0.0f, 0.0f, 0.0f };
		glm::vec3 _Rotation{ 0.0f, 0.0f, 0.0f };
		glm::vec3 _Scale{ 1.0f, 1.0f, 1.0f };

		// Local transform relative to the direct parent.
		glm::vec3 _LocalTranslation{ 0.0f, 0.0f, 0.0f };
		glm::vec3 _LocalRotation{ 0.0f, 0.0f, 0.0f };
		glm::vec3 _LocalScale{ 1.0f, 1.0f, 1.0f };

		Transform() = default;
		Transform(const Transform&) = default;
		Transform(const glm::vec3& translation)
			: _Translation(translation), _LocalTranslation(translation) {
		}

		glm::mat4 GetTransform() const
		{
			return Math::ComposeTransform(_Translation, _Rotation, _Scale);
		}

		glm::mat4 GetLocalTransform() const
		{
			return Math::ComposeTransform(_LocalTranslation, _LocalRotation, _LocalScale);
		}

		bool SetTransform(const glm::mat4& transform)
		{
			return Math::DecomposeTransform(transform, _Translation, _Rotation, _Scale);
		}

		bool SetLocalTransform(const glm::mat4& transform)
		{
			return Math::DecomposeTransform(transform, _LocalTranslation, _LocalRotation, _LocalScale);
		}

	};

	struct SpriteRenderer
	{
		bool Enabled = true;
		glm::vec4 _Color{ 1.0f, 1.0f, 1.0f, 1.0f };
		AssetHandle SpriteHandle = AssetHandle(0);
		// Runtime-only resolved sprite. SpriteHandle is the serialized source of truth.
		Ref<Texture2D> Sprite;
		float TilingFactor = 1.0f;

		SpriteRenderer() = default;
		SpriteRenderer(const SpriteRenderer&) = default;
		SpriteRenderer(const glm::vec4& color)
			: _Color(color) {
		}
	};

	// Stable scene data used by the editor to choose an Entity icon. Entity is the
	// Unity-style default; Automatic may be selected explicitly to resolve from the
	// current components.
	enum class EntityIconMode : uint8_t
	{
		Automatic = 0,
		Entity,
		Camera,
		Sprite,
		Rigidbody2D,
		Collider2D
	};

	// Project-defined gameplay identity shared by every Entity, including entities
	// that currently have no renderer or collider. Layer is a stable 0-based slot;
	// the project's Physics2DSettings decides which entity layers may interact.
	struct EntityMetadata
	{
		std::string GameplayTag = "Untagged";
		uint8_t Layer = 0;
		EntityIconMode HierarchyIcon = EntityIconMode::Entity;

		EntityMetadata() = default;
		EntityMetadata(const EntityMetadata&) = default;
	};

	struct LineRenderer
	{
		bool Enabled = true;
		glm::vec4 _Color{ 1.0f, 1.0f, 1.0f, 1.0f };
		glm::vec3 Start{ -0.5f, 0.0f, 0.0f };
		glm::vec3 End{ 0.5f, 0.0f, 0.0f };
		float Width = 1.0f;

		LineRenderer() = default;
		LineRenderer(const LineRenderer&) = default;
		LineRenderer(const glm::vec4& color)
			: _Color(color) {
		}
	};

	struct C_Camera
	{
		SceneCamera _Camera;
		bool Primary = true; // TODO: think about moving to Scene
		bool FixedAspectRatio = false;
		glm::vec4 BackgroundColor = glm::vec4(0.53f, 0.81f, 0.92f, 1.0f);  // 默认天蓝色

		C_Camera() = default;
		C_Camera(const C_Camera&) = default;
	};

	// Forward declaration
	class ScriptableEntity;
	struct NativeScript
	{
		ScriptableEntity* Instance = nullptr;

		ScriptableEntity* (*InstantiateScript)() = nullptr;
		void (*DestroyScript)(NativeScript*) = nullptr;

		template<typename T>
		void Bind()
		{
			InstantiateScript = []() { return static_cast<ScriptableEntity* >(new T()); };
			DestroyScript = [](NativeScript* nsc) { delete nsc->Instance; nsc->Instance = nullptr; };
		}
	};


	// Physics
	struct Rigidbody2D
	{
		bool Enabled = true;
		enum class BodyType { Static = 0, Dynamic, Kinematic };
		BodyType Type = BodyType::Static;
		bool FixedRotation = false;

		// Storage for runtime
		void* RuntimeBody = nullptr;

		Rigidbody2D() = default;
		Rigidbody2D(const Rigidbody2D&) = default;
	};

	struct BoxCollider2D
	{
		bool Enabled = true;
		bool IsTrigger = false;
		// Box2D category/mask bits. CollisionLayer must contain at least one bit.
		uint16_t CollisionLayer = 0x0001;
		uint16_t CollisionMask = 0xFFFF;
		glm::vec2 Offset = { 0.0f, 0.0f };
		glm::vec2 Size = { 0.5f, 0.5f };

		// TODO(Yan): move into physics material in the future maybe
		float Density = 1.0f;
		float Friction = 0.5f;
		float Restitution = 0.0f;
		float RestitutionThreshold = 0.5f;

		// Storage for runtime
		void* RuntimeFixture = nullptr;

		BoxCollider2D() = default;
		BoxCollider2D(const BoxCollider2D&) = default;
	};

	struct CircleCollider2D
	{
		bool Enabled = true;
		bool IsTrigger = false;
		// Box2D category/mask bits. CollisionLayer must contain at least one bit.
		uint16_t CollisionLayer = 0x0001;
		uint16_t CollisionMask = 0xFFFF;
		glm::vec2 Offset = { 0.0f, 0.0f };
		float Radius = 0.5f;

		// Keep these rules in sync with BoxCollider2D. A Box2D circle cannot become
		// an ellipse, so runtime/editor geometry uses Radius multiplied by the
		// largest absolute world X/Y scale component. Offset uses the same 2D
		// transform as Box2D: translation XY, rotation Z, and signed scale XY.
		float Density = 1.0f;
		float Friction = 0.5f;
		float Restitution = 0.0f;

		// Storage for runtime
		void* RuntimeFixture = nullptr;

		CircleCollider2D() = default;
		CircleCollider2D(const CircleCollider2D&) = default;
	};

	struct DistanceJoint2D
	{
		bool Enabled = true;
		UUID ConnectedEntity{ 0 };
		// Local anchors, in each body's unscaled local coordinate system.
		glm::vec2 Anchor{ 0.0f, 0.0f };
		glm::vec2 ConnectedAnchor{ 0.0f, 0.0f };
		float Distance = 1.0f;
		float Frequency = 0.0f;
		// Box2D damping ratio in the inclusive [0, 1] range.
		float Damping = 0.0f;
		bool CollideConnected = false;

		// Storage for runtime
		void* RuntimeJoint = nullptr;

		DistanceJoint2D() = default;
		DistanceJoint2D(const DistanceJoint2D&) = default;
	};

}
