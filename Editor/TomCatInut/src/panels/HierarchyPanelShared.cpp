#include "HierarchyPanelShared.h"

#include <imgui/imgui.h>

#include "TomCat/Scene/Components.h"

namespace TomCat {

	void HierarchyPanelShared::MarkModified(bool instant)
	{
        PrefabOverridesDirty = true;
		if (!SceneModified)
			return;
		if (instant)
		{
			if (ModificationGestureActive)
				SceneModified(SceneModificationPhase::Commit);
			ModificationGestureActive = false;
			SceneModified(SceneModificationPhase::Instant);
			return;
		}

		const bool hasImGui = ImGui::GetCurrentContext() != nullptr;
		const bool itemActivated = hasImGui && ImGui::IsItemActivated();
		if (ModificationGestureActive && itemActivated)
		{
			SceneModified(SceneModificationPhase::Commit);
			ModificationGestureActive = false;
		}
		if (!hasImGui || !ImGui::IsAnyItemActive())
		{
			SceneModified(SceneModificationPhase::Instant);
			return;
		}
		if (!ModificationGestureActive)
		{
			SceneModified(SceneModificationPhase::Begin);
			ModificationGestureActive = true;
		}
		SceneModified(SceneModificationPhase::Update);
		if (ImGui::IsItemDeactivatedAfterEdit())
		{
			SceneModified(SceneModificationPhase::Commit);
			ModificationGestureActive = false;
		}
	}

	void HierarchyPanelShared::FinishModificationGesture()
	{
		if (!ModificationGestureActive || !SceneModified)
			return;
		if (!ImGui::GetCurrentContext() || !ImGui::IsAnyItemActive())
		{
			SceneModified(SceneModificationPhase::Commit);
			ModificationGestureActive = false;
		}
	}
	ColliderEditMode HierarchyPanelShared::GetColliderEdit() const
	{
		if (!ColliderEditingAllowed || !ColliderGizmosEnabled || ColliderEdit == ColliderEditMode::None || !SelectionContext
			|| SelectionContext.GetUUID() != ColliderEditEntity)
			return ColliderEditMode::None;

		if (ColliderEdit == ColliderEditMode::Box
			&& !SelectionContext.HasComponent<BoxCollider2D>())
			return ColliderEditMode::None;
		if (ColliderEdit == ColliderEditMode::Circle
			&& !SelectionContext.HasComponent<CircleCollider2D>())
			return ColliderEditMode::None;
		return ColliderEdit;
	}

}
