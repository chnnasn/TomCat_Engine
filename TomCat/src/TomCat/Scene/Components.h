#pragma once

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

#include "SceneCamera.h"

#define GLM_ENABLE_EXPERIMENTAL
#include <glm/gtx/quaternion.hpp>

#include "ScriptableEntity.h"

namespace TomCat {

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

		SpriteRenderer() = default;
		SpriteRenderer(const SpriteRenderer&) = default;
		SpriteRenderer(const glm::vec4& color)
			: _Color(color) {
		}
	};

	struct C_Camera
	{
		SceneCamera _Camera;
		bool Primary = true; // TODO: think about moving to Scene
		bool FixedAspectRatio = false;

		C_Camera() = default;
		C_Camera(const C_Camera&) = default;
	};


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

}