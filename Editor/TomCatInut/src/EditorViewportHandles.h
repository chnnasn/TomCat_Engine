#pragma once

// Scene-viewport editing handles and overlays (collider outlines/handles,
// RectTransform handles, camera/canvas overlays and screen/world helpers),
// extracted from EditorLayer. Drag state lives in EditorViewportState, which
// the EditorLayer facade shares with this class so event handlers can cancel
// drags without going through the service.

#include "TomCat/Core/Base.h"
#include "TomCat/Scene/Entity.h"

#include <glm/glm.hpp>
#include <functional>

namespace TomCat {

	class EditorLayer;
	class EditorCamera;
	class EditorIconSet;
	class SceneHierarchyPanel;
	// Both hosts supply state and history callbacks; all geometry, picking and
	// manipulation below remain the same desktop implementation.
	struct EditorViewportContext
	{
		Ref<Scene>& Scene;
		EditorCamera& Camera;
		SceneHierarchyPanel& Hierarchy;
		Ref<EditorIconSet>& Icons;
		glm::vec2 (&Bounds)[2];
		int& GizmoType;
		std::function<bool()> IsEditing, UsePivot, UseLocal, CanvasHovered, PointerBlocked, HasTransaction;
		std::function<void(const char*)> BeginTransaction;
		std::function<void()> UpdateTransaction, CommitTransaction, FocusScene;
	};

	struct EditorViewportState
	{
		enum class ColliderEditHandle
		{
			None = 0,
			Offset,
			BoxLeft,
			BoxRight,
			BoxBottom,
			BoxTop,
			BoxBottomLeft,
			BoxBottomRight,
			BoxTopLeft,
			BoxTopRight,
			CircleLeft,
			CircleRight,
			CircleBottom,
			CircleTop
		};

		ColliderEditHandle ActiveColliderHandle = ColliderEditHandle::None;
		UUID ColliderEditEntity{ 0 };
		glm::vec2 ColliderDragStartMouseWorld{ 0.0f };
		glm::vec2 ColliderDragStartCenter{ 0.0f };
		glm::vec2 ColliderDragStartHalfSize{ 0.0f };
		float ColliderDragStartRadius = 0.0f;
		float ColliderDragPlaneZ = 0.0f;
		bool ColliderHandleHovered = false;
		bool ColliderTransactionActive = false;

		bool UIRectTransactionActive = false;
		bool UIRectDragActive = false;
		// Cached across the native-event/ImGui frame boundary. Mouse button events
		// arrive before the Scene overlay is rebuilt, so the previous frame's hit
		// result must protect transparent UI rectangles from world picking.
		bool UIRectHandleHovered = false;
		UUID UIRectEditEntity{ 0 };
	};

	class EditorViewportHandles
	{
		friend class EditorLayer;
	public:
		using ColliderEditHandle = EditorViewportState::ColliderEditHandle;

		EditorViewportHandles(EditorViewportState& state, EditorLayer& layer);
		EditorViewportHandles(EditorViewportState& state, EditorViewportContext context)
			: m_ViewportState(state), m_Context(std::move(context)) {}

		void FrameSceneEntity(Entity root);
		bool GetEntityBounds(Entity root, glm::vec3& minimum, glm::vec3& maximum, bool includeChildren = true);
		void RenderSceneColliderOverlays();
		void RenderSceneCameraOverlay();
		void RenderSceneCanvasOverlay();
		bool WorldToScreen(const glm::vec3& worldPosition, glm::vec2& screenPosition) const;
		bool ScreenToWorldOnPlane(const glm::vec2& screenPosition, float worldZ,
			glm::vec2& worldPosition) const;
		void ResetRectTransformEditState();
		// Screen-space UI uses RectTransform pixel coordinates, so it needs a
		// dedicated orthographic ImGuizmo projection. Translation is mapped back to
		// AnchoredPosition while rotation/scale reuse the entity Transform fields.
		bool UI_RectTransformHandles();
		void ResetColliderEditState();
		void UI_ColliderEditHandles();

	private:
		EditorViewportState& m_ViewportState;
		EditorViewportContext m_Context;
	};
}
