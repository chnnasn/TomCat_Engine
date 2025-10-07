#pragma once

#include "TomCat/Core/Base.h"
#include "TomCat/Core/Log.h"
#include "TomCat/Scene/Scene.h"
#include "TomCat/Scene/Entity.h"

namespace TomCat {

	class SceneHierarchyPanel
	{
	public:
		SceneHierarchyPanel() = default;
		SceneHierarchyPanel(const Ref<Scene>& scene);

		void SetContext(const Ref<Scene>& scene);

		void OnImGuiRender();

		Entity GetSelectedEntity() const { return m_SelectionContext; };

		void SetSelectedEntity(Entity entity);

	private:
		void DrawEntityNode(Entity entity);
		void DrawComponents(Entity entity);
	private:
		Ref<Scene> m_Context;
		Entity m_SelectionContext;
	};

}