#pragma once

#include "SceneCamera.h"
#include "TomCat/Core/UUID.h"
#include "TomCat/Renderer/Texture.h"
#include "TomCat/Renderer/Mesh.h"

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

		Tag() = default;
		Tag(const Tag&) = default;
		Tag(const std::string& tag)
			: _Tag(tag) {
		}
	};


	struct Transform
	{
		glm::vec3 _Translation{0.0f,0.0f,0.0f };
		glm::vec3 _Rotation{0.0f,0.0f,0.0f };
		glm::vec3 _Scale{1.0f,1.0f,1.0f };

		Transform() = default;
		Transform(const Transform&) = default;
		Transform(const glm::vec3& translation)
			: _Translation(translation) {
		}

		glm::mat4 GetTransform() const
		{
			glm::mat4 rotation = glm::toMat4(glm::quat(_Rotation));

			return glm::translate(glm::mat4(1.0f), _Translation)
				* rotation
				* glm::scale(glm::mat4(1.0f), _Scale);
		}

	};

	struct SpriteRenderer
	{
		glm::vec4 _Color{ 1.0f, 1.0f, 1.0f, 1.0f };
		Ref<Texture2D>Texture;
		float TilingFactor = 1.0f;

		SpriteRenderer() = default;
		SpriteRenderer(const SpriteRenderer&) = default;
		SpriteRenderer(const glm::vec4& color)
			: _Color(color) {
		}
	};

	struct MeshComponent
	{
		Ref<Mesh> MeshAsset;
		Ref<Texture2D> AlbedoTexture;
		glm::vec4 Color{ 1.0f, 1.0f, 1.0f, 1.0f };
		bool UseTexture = false;
		std::string ModelPath;

		MeshComponent() = default;
		MeshComponent(const MeshComponent&) = default;
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

		ScriptableEntity* (*InstantiateScript)();
		void (*DestroyScript)(NativeScript*);

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

}