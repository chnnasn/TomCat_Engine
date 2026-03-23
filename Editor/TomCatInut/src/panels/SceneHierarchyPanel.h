#pragma once

#include "TomCat/Core/Base.h"
#include "TomCat/Core/Log.h"
#include "TomCat/Scene/Scene.h"
#include "TomCat/Scene/Entity.h"

#include <functional>

namespace TomCat {

	class SceneHierarchyPanel
	{
	public:
		using SceneLoadCallback = std::function<void(const std::filesystem::path&)>;
		using SpriteCreateCallback = std::function<void(const std::filesystem::path&)>;

		SceneHierarchyPanel() = default;
		SceneHierarchyPanel(const Ref<Scene>& scene);

		void SetContext(const Ref<Scene>& scene, bool clearSelection = true, bool remapSelection = false);

		void OnImGuiRender();

		Entity GetSelectedEntity() const { return m_SelectionContext; };

		void SetSelectedEntity(Entity entity);

		void SetSceneLoadCallback(const SceneLoadCallback& callback) { m_SceneLoadCallback = callback; }
		void SetSpriteCreateCallback(const SpriteCreateCallback& callback) { m_SpriteCreateCallback = callback; }
	private:
		void DrawEntityNode(Entity entity);
		void DrawComponents(Entity entity);
	private:
		Ref<Scene> m_Context;
		Entity m_SelectionContext;
		SceneLoadCallback m_SceneLoadCallback;
		SpriteCreateCallback m_SpriteCreateCallback;

	};

}