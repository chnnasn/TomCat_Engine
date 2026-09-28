#pragma once

// Hierarchy tree window: entity nodes, drag/drop reparenting, inline rename,
// clipboard operations and the shared GameObject context menu, extracted from
// SceneHierarchyPanel.

#include "HierarchyPanelShared.h"
#include "SceneInspectorPanel.h"

#include <array>
#include <unordered_set>
#include <vector>

namespace TomCat {

	class SceneHierarchyTreePanel
	{
	public:
		SceneHierarchyTreePanel(HierarchyPanelShared& shared,
			SceneInspectorPanel& inspector, SceneAuthoringEditorsPanel& authors)
			: m_Shared(shared), m_Inspector(inspector), m_Authors(authors) {}

		void DrawHierarchyWindow(bool* open, bool sceneDirty);
		bool DrawGameObjectMenu();
		bool HandleShortcut(int keyCode, bool control);
		bool FlushPendingDeletion();
		bool IsFocused() const { return m_HierarchyFocused; }
		bool IsDocked() const { return m_HierarchyDocked; }
		bool HasPendingRenameFocus() const { return m_RenameFocus; }
		void OnSceneContextReset()
		{
			m_HierarchyVisibleOrder.clear();
			m_PreviousHierarchyOrder.clear();
		}
		void ResetExpansionState()
		{
			m_ForceExpandParent = {};
			m_ForceOpenEntityNodes.clear();
			m_ForceOpenSceneRoot = false;
		}
		void CancelPendingDeletion() { m_EntityToDelete = {}; }
		void CancelRename()
		{
			m_RenameEntity = {};
			m_RenameFocus = false;
			m_Shared.NameEditEntity = {};
		}
		void ClearClipboard()
		{
			m_ClipboardEntity = {};
			m_ClipboardScene = nullptr;
			m_ClipboardIsCut = false;
		}

	private:
		bool AcceptPrefabDrop(Entity parent);
		void DrawEntityNode(Entity entity);
		void DrawEntityOperationsMenu();
		void BeginRename(Entity entity);
		void CutSelectedEntity();
		void CopySelectedEntity();
		void PasteEntity();
		void DuplicateSelectedEntity();
		void DeleteSelectedEntity();
		bool CanPaste() const;

	private:
		HierarchyPanelShared& m_Shared;
		SceneInspectorPanel& m_Inspector;
		SceneAuthoringEditorsPanel& m_Authors;
		std::vector<UUID> m_HierarchyVisibleOrder, m_PreviousHierarchyOrder;
		// 创建子对象后用于强制展开父节点的一次性标记。
		Entity m_ForceExpandParent;
		std::unordered_set<uint64_t> m_ForceOpenEntityNodes;
		bool m_ForceOpenSceneRoot = false;
		Entity m_EntityToDelete;
		Entity m_ClipboardEntity;
		Ref<Scene> m_ClipboardScene;
		bool m_ClipboardIsCut = false;
		Entity m_RenameEntity;
		char m_RenameBuffer[256] = {};
		bool m_RenameFocus = false;
		bool m_HierarchyFocused = false;
		bool m_HierarchyDocked = true;
		std::array<char, 128> m_HierarchySearch{};
	};
}
