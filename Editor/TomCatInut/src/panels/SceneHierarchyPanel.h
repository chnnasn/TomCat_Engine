#pragma once

#include "TomCat/Core/Base.h"
#include "TomCat/Core/Log.h"
#include "TomCat/Asset/Asset.h"
#include "TomCat/Scene/Scene.h"
#include "TomCat/Scene/Entity.h"
#include "../EditorIcons.h"

#include <array>
#include <functional>

namespace TomCat {
	class Project;

	class SceneHierarchyPanel
	{
	public:
		enum class ColliderEditMode
		{
			None = 0,
			Box,
			Circle
		};

		using SceneLoadCallback = std::function<void(AssetHandle)>;
		using SpriteCreateCallback = std::function<void(AssetHandle)>;
		using SceneModifiedCallback = std::function<void()>;

		SceneHierarchyPanel() = default;
		SceneHierarchyPanel(const Ref<Scene>& scene);

		void SetContext(const Ref<Scene>& scene, bool clearSelection = true, bool remapSelection = false);
		void SetProject(const Ref<Project>& project);
		void SetIcons(const Ref<EditorIconSet>& icons) { m_Icons = icons; }

		void OnImGuiRender(bool* hierarchyOpen = nullptr, bool* inspectorOpen = nullptr);

		Entity GetSelectedEntity() const { return m_SelectionContext; };
		ColliderEditMode GetColliderEditMode() const;
		bool IsEditingCollider() const { return GetColliderEditMode() != ColliderEditMode::None; }
		bool IsColliderEditingAllowed() const { return m_ColliderEditingAllowed; }
		void SetColliderEditingAllowed(bool allowed)
		{
			m_ColliderEditingAllowed = allowed;
			if (!allowed)
				ClearColliderEditMode();
		}
		void ClearColliderEditMode()
		{
			m_ColliderEditMode = ColliderEditMode::None;
			m_ColliderEditEntity = UUID(0);
		}

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
		Ref<Project> m_Project;
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
		Entity m_NameEditingEntity;
		char m_NameEditBuffer[256] = {};
		bool m_HierarchyFocused = false;
		bool m_ColliderEditingAllowed = true;
		ColliderEditMode m_ColliderEditMode = ColliderEditMode::None;
		UUID m_ColliderEditEntity = UUID(0);
		Ref<EditorIconSet> m_Icons;
		std::array<char, 256> m_SpriteSearch{};
		bool m_SpritePickerOpen = false;
		UUID m_SpritePickerEntity = UUID(0);
		SceneLoadCallback m_SceneLoadCallback;
		SpriteCreateCallback m_SpriteCreateCallback;
		SceneModifiedCallback m_SceneModifiedCallback;

	};

}
