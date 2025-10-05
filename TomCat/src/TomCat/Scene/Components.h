#pragma once

#include <glm/glm.hpp>

#include "SceneCamera.h"

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
		glm::mat4 _Transform{ 1.0f };

		Transform() = default;
		Transform(const Transform&) = default;
		Transform(const glm::mat4& transform)
			: _Transform(transform) {
		}

		operator glm::mat4& () { return _Transform; }
		operator const glm::mat4& () const { return _Transform; }
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

		void(*InstantiateFunction)(ScriptableEntity*&) = nullptr;
		void(*DestroyInstanceFunction)(ScriptableEntity*&) = nullptr;

		void(*OnCreateFunction)(ScriptableEntity*) = nullptr;
		void(*OnDestroyFunction)(ScriptableEntity*) = nullptr;
		void(*OnUpdateFunction)(ScriptableEntity*, Timestep) = nullptr;

		template<typename T>
		void Bind()
		{
			InstantiateFunction = [](ScriptableEntity*& instance) { instance = new T(); };
			DestroyInstanceFunction = [](ScriptableEntity*& instance) { delete static_cast<T*>(instance); instance = nullptr; };


			OnCreateFunction = [](ScriptableEntity* instance) { static_cast<T*>(instance)->OnCreate(); };
			OnDestroyFunction = [](ScriptableEntity* instance) { static_cast<T*>(instance)->OnDestroy(); };
			OnUpdateFunction = [](ScriptableEntity* instance, Timestep ts) { static_cast<T*>(instance)->OnUpdate(ts); };
		}
	};

}