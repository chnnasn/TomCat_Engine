#pragma once

#include "TomCat/Core/Base.h"
#include "TomCat/Core/Log.h"
#include "TomCat/Asset/Asset.h"
#include "TomCat/Scene/Scene.h"
#include "TomCat/Scene/Entity.h"

#include <functional>

namespace TomCat {

	class SceneHierarchyPanel
	{
	public:
		using SceneLoadCallback = std::function<void(AssetHandle)>;
		using SpriteCreateCallback = std::function<void(AssetHandle)>;
		using SceneModifiedCallback = std::function<void()>;

		SceneHierarchyPanel() = default;
		SceneHierarchyPanel(const Ref<Scene>& scene);

		void SetContext(const Ref<Scene>& scene, bool clearSelection = true, bool remapSelection = false);

		void OnImGuiRender(bool* hierarchyOpen = nullptr, bool* inspectorOpen = nullptr);

		Entity GetSelectedEntity() const { return m_SelectionContext; };

		void SetSelectedEntity(Entity entity);
		bool HandleShortcut(int keyCode, bool control);
		bool IsHierarchyFocused() const { return m_HierarchyFocused; }
		void SetSceneLoadCallback(const SceneLoadCallback& callback) { m_SceneLoadCallback = callback; }
		void SetSpriteCreateCallback(const SpriteCreateCallback& callback) { m_SpriteCreateCallback = callback; }
		void SetSceneModifiedCallback(const SceneModifiedCallback& callback) { m_SceneModifiedCallback = callback; }
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
		bool FlushPendingDeletion();
		void ClearClipboard();
		void MarkModified();
	private:
		Ref<Scene> m_Context;
		Entity m_SelectionContext;
		// 创建子对象后用于强制展开父节点的一次性标记。
		Entity m_ForceExpandParent;
		bool m_ForceOpenSceneRoot = false;
		Entity m_EntityToDelete;
		Entity m_ClipboardEntity;
		Ref<Scene> m_ClipboardScene;
		bool m_ClipboardIsCut = false;
		Entity m_RenameEntity;
		char m_RenameBuffer[256] = {};
		bool m_RenameFocus = false;
		Entity m_TagEditingEntity;
		char m_TagEditBuffer[256] = {};
		bool m_HierarchyFocused = false;
		SceneLoadCallback m_SceneLoadCallback;
		SpriteCreateCallback m_SpriteCreateCallback;
		SceneModifiedCallback m_SceneModifiedCallback;

	};

}
