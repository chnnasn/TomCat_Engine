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
		void HandleShortcut(int keyCode, bool control);

		void SetSceneLoadCallback(const SceneLoadCallback& callback) { m_SceneLoadCallback = callback; }
		void SetSpriteCreateCallback(const SpriteCreateCallback& callback) { m_SpriteCreateCallback = callback; }
	private:
		void DrawEntityNode(Entity entity);
		void DrawComponents(Entity entity);
		void DrawEntityOperationsMenu();
		void BeginRename(Entity entity);
		void CutSelectedEntity();
		void CopySelectedEntity();
		void PasteEntity();
		void DuplicateSelectedEntity();
		void DeleteSelectedEntity();
		bool CanPaste() const;
	private:
		Ref<Scene> m_Context;
		Entity m_SelectionContext;
		Entity m_EntityToDelete;
		Entity m_ClipboardEntity;
		Ref<Scene> m_ClipboardScene;
		bool m_ClipboardIsCut = false;
		Entity m_RenameEntity;
		char m_RenameBuffer[256] = {};
		bool m_RenameFocus = false;
		SceneLoadCallback m_SceneLoadCallback;
		SpriteCreateCallback m_SpriteCreateCallback;

	};

}
