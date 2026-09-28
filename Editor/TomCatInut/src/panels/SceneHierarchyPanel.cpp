#include "SceneHierarchyPanel.h"
#include "SceneHierarchyDetail.h"

#include <imgui/imgui.h>
#include <imgui/imgui_internal.h>

#include <algorithm>

#include "TomCat/Scene/Components.h"
#include "TomCat/Project/Project.h"

namespace TomCat {

	SceneHierarchyPanel::SceneHierarchyPanel()
		: m_Authors(m_Shared), m_Inspector(m_Shared, m_Authors),
		m_Tree(m_Shared, m_Inspector, m_Authors)
	{
	}

	SceneHierarchyPanel::SceneHierarchyPanel(const Ref<Scene>& context)
		: SceneHierarchyPanel()
	{
		SetContext(context);
	}

	SceneHierarchyPanel::ColliderEditMode SceneHierarchyPanel::GetColliderEditMode() const
	{
		return m_Shared.GetColliderEdit();
	}

	void SceneHierarchyPanel::SetProject(const Ref<Project>& project)
	{
		m_Shared.Project = project;
		if (m_Shared.Context && m_Shared.Project)
			m_Shared.Context->SetPhysics2DSettings(m_Shared.Project->GetSettings().Physics2D);
	}

	bool SceneHierarchyPanel::FlushPendingCommands()
	{
		if (static_cast<uint64_t>(m_Shared.PendingPrefabRoot) != 0)
		{
			const UUID root = m_Shared.PendingPrefabRoot;
			m_Shared.PendingPrefabRoot = UUID(0);
			if (m_Shared.Context && m_Shared.PrefabAction && m_Shared.PrefabCreationAllowed)
				m_Shared.PrefabAction(m_Shared.Context->FindEntityByUUID(root), m_Shared.PendingPrefabAction, m_Shared.PendingPrefabEntity, m_Shared.PendingPrefabComponent, m_Shared.PendingPrefabProperty);
            m_Shared.PrefabOverridesDirty = true;
		}
		return m_Tree.FlushPendingDeletion();
	}


	void SceneHierarchyPanel::SetContext(const Ref<Scene>& context, bool clearSelection, bool remapSelection)
	{
		if (m_Shared.ModificationGestureActive && m_Shared.SceneModified)
			m_Shared.SceneModified(SceneModificationPhase::Cancel);
		m_Shared.ModificationGestureActive = false;
		m_Shared.CommitAfterPendingDeletion = false;
		UUID selectedUUID{};
		const bool hadSelection = !clearSelection && remapSelection && (bool)m_Shared.SelectionContext;
		if (hadSelection)
			selectedUUID = m_Shared.SelectionContext.GetUUID();
		const bool contextChanged = m_Shared.Context != context;
		if (contextChanged || clearSelection)
		{
			m_Shared.MultiSelection.clear();
			m_Tree.OnSceneContextReset();
			m_Shared.SelectionAnchor = UUID(0);
		}
		if (contextChanged && m_Shared.Context)
			m_Authors.OnSceneContextChanging();
		m_Shared.Context = context;
		if (m_Shared.Context && m_Shared.Project)
			m_Shared.Context->SetPhysics2DSettings(m_Shared.Project->GetSettings().Physics2D);
		m_Tree.ResetExpansionState();
		m_Tree.CancelPendingDeletion();
		m_Tree.CancelRename();
		m_Shared.SpritePickerOpen = false;
		m_Shared.SpritePickerEntity = UUID(0);
		m_Shared.SpriteSearch.fill('\0');
		m_Authors.OnSceneContextChanged(contextChanged);
		if (contextChanged)
			m_Inspector.OnSceneContextChanged();
		m_Shared.ClearColliderEdit();
		if (contextChanged)
			m_Tree.ClearClipboard();
		if (clearSelection)
			m_Shared.SelectionContext = {};
		else if (hadSelection && m_Shared.Context)
		{
			Entity remappedEntity = m_Shared.Context->FindEntityByUUID(selectedUUID);
			if (remappedEntity)
				m_Shared.SelectionContext = remappedEntity;
			else
				m_Shared.SelectionContext = {};
		}
	}

	void SceneHierarchyPanel::ResetForSceneReplacement(const Ref<Scene>& scene, UUID selectedEntity)
	{
		// Clear stale handles before SetContext, which can otherwise attempt to
		// derive the selected UUID from an entity index reused by the new registry.
		m_Shared.SelectionContext = {};
		// The caller owns the replacement/history transaction. Any Inspector
		// gesture belongs to the discarded registry and must not cancel that new
		// transaction through SetContext's usual context-switch notification.
		m_Shared.ModificationGestureActive = false;
		m_Shared.FrameEntityRequest = UUID(0);
		m_Tree.ClearClipboard();
		m_Authors.ClearSceneState();
		m_Inspector.ClearSceneState();
		SetContext(scene, true, false);
		if (scene && static_cast<uint64_t>(selectedEntity) != 0)
			SetSelectedEntity(scene->FindEntityByUUID(selectedEntity));
	}

	void SceneHierarchyPanel::OnImGuiRender(bool* hierarchyOpen, bool* inspectorOpen,
		bool sceneDirty)
	{
		if (m_Shared.ColliderEdit != ColliderEditMode::None
			&& GetColliderEditMode() == ColliderEditMode::None)
			m_Shared.ClearColliderEdit();
		// Keyboard commands can originate from the Scene viewport while Hierarchy
		// is hidden or covered by another dock tab. Drain deletion before any tree
		// traversal so it never remains queued until the panel becomes visible.
		m_Tree.FlushPendingDeletion();
		if (!m_Shared.Context || !m_Shared.SelectionContext) m_Shared.MultiSelection.clear();
		else
		{
			std::erase_if(m_Shared.MultiSelection, [&](UUID id) { return !m_Shared.Context->FindEntityByUUID(id); });
			if (!m_Shared.MultiSelection.empty() && std::find(m_Shared.MultiSelection.begin(), m_Shared.MultiSelection.end(), m_Shared.SelectionContext.GetUUID()) == m_Shared.MultiSelection.end()) m_Shared.MultiSelection.clear();
		}
		m_Tree.DrawHierarchyWindow(hierarchyOpen, sceneDirty);
		m_Inspector.DrawInspectorWindow(inspectorOpen);
		m_Shared.FinishModificationGesture();
	}

	void SceneHierarchyPanel::SetSelectedEntity(Entity entity)
	{
		m_Inspector.ClearAssetSelection();
		m_Shared.MultiSelection.clear();
		if (m_Shared.SelectionContext != entity)
		{
			if (m_Shared.SelectionContext && m_Shared.SelectionContext.HasComponent<SpriteRenderer>())
			{
				auto& renderer = m_Shared.SelectionContext.GetComponent<SpriteRenderer>();
				renderer.RuntimeSpriteOverrideActive = false;
				renderer.RuntimeSpriteOverrideHandle = AssetHandle(0);
				m_Authors.StopTimelinePreview(m_Shared.SelectionContext.GetUUID());
			}
			m_Shared.ClearColliderEdit();
			m_Authors.ResetRenamePopupSelection();
			m_Inspector.ResetAddComponentPopup();
		}
		if (m_Shared.SpritePickerOpen && m_Shared.SelectionContext != entity)
		{
			m_Shared.SpritePickerOpen = false;
			m_Shared.SpritePickerEntity = UUID(0);
		}
		m_Shared.SelectionContext = entity;
	}

	bool SceneHierarchyPanel::DrawGameObjectMenu()
	{
		return m_Tree.DrawGameObjectMenu();
	}

}
