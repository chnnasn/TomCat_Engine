#pragma once

#include "TomCat.h"
#include "EditorIcons.h"
#include "Panels/SceneHierarchyPanel.h"

#include "TomCat/Renderer/EditorCamera.h"
#include "Panels/ContentBrowserPanel.h"
#include <functional>
#include "TomCat/Project/Project.h"
#include "TomCat/Project/ProjectManager.h"
#include "TomCat/Project/ProjectSettings.h"

#include <array>
#include <string>

struct ImVec2;

namespace TomCat {

	class EditorLayer : public Layer
	{
	public:
		EditorLayer();

		virtual ~EditorLayer() = default;

		virtual void OnAttach() override;
		virtual void OnDetach() override;

		void OnUpdate(Timestep ts) override;
		virtual void OnImGuiRender() override;
		void OnEvent(Event& e) override;
	private:
		enum class GizmoPivotMode
		{
			Pivot = 0,
			Center = 1
		};

		enum class GizmoSpaceMode
		{
			Local = 0,
			World = 1
		};

		bool OnKeyPressed(KeyPressedEvent& e);
		bool OnMouseButtonPressed(MouseButtonPressedEvent& e);
		bool OnMouseButtonReleased(MouseButtonReleasedEvent& e);
		bool OnWindowClose(WindowCloseEvent& e);

		void NewScene();
		bool OpenProjectStartScene();
		void AddDefaultMainCamera();
		bool OpenScene();
		bool OpenScene(const std::filesystem::path& path);
		void SaveScene();
		void SaveSceneAs();
		
		bool SerializeScene(const Ref<Scene>& scene, const std::filesystem::path& path);
		void ResizeSceneForGameView(const Ref<Scene>& scene);
		void ResetSceneInteractionState();
		void RequestDestructiveAction(std::function<bool()> action);
		void UI_UnsavedChangesModal();
		void RequestExit();

		void OnScenePlay();
		void OnScenePause();
		void OnSceneStep();
		void OnSceneStop();
		bool IsSceneRunning() const;

		void UI_Toolbar();
		void UI_SceneGizmoModeToolbarOverlay();
		void UI_SceneGizmoToolbar();
		void UI_SceneToolbarDockPreview();
		void UI_SceneColliderVisibilityToggle();
		void UI_ColliderEditHandles();
		void RenderSceneColliderOverlays();
		bool ScreenToWorldOnPlane(const glm::vec2& screenPosition, float worldZ,
			glm::vec2& worldPosition) const;
		bool WorldToScreen(const glm::vec3& worldPosition, glm::vec2& screenPosition) const;
		void ResetColliderEditState();
		// Persist the Scene toolbar arrangement alongside ImGui's window layout.
		void LoadSceneToolbarLayout();
		void SaveSceneToolbarLayout();
		// Shared drag helper: submits the invisible handle and owns the only
		// drag/dock state transitions used by both Scene toolbars.
		void UI_SceneToolbarDragHandle(const char* id, glm::vec2& offset, bool& docked, bool& dragging,
			const ImVec2& handleMin, const ImVec2& handleMax, float tearX, bool canDock);
		void UI_GameNoCameraOverlay();
		void UI_BuildSettings();
		void UI_ProjectSettings();
		void LoadProjectSettingsDraft();
		bool ApplyProjectSettingsDraft();
		void ClearProjectSettingsFeedback();

		bool OpenProject();
		bool OpenProject(const std::filesystem::path& path);
		void SaveProject();
	private:
		Ref<Framebuffer> m_Framebuffer;
		Ref<Framebuffer> m_GameFramebuffer;

		Ref<Scene> m_ActiveScene;
		Ref<Scene> m_EditorScene;
		std::filesystem::path m_EditorScenePath;

		Entity m_HoveredEntity;

		EditorCamera m_EditorCamera;

		bool m_ViewportFocused = false;
		bool m_ViewportCanvasHovered = false;
		bool m_ViewportCameraDragOwned = false;
		glm::vec2 m_ViewportSize = { 0.0f, 0.0f };

		glm::vec2 m_ViewportBounds[2];

		// Game Viewport
		glm::vec2 m_GameViewportSize = { 0.0f, 0.0f };

		int m_GizmoType = -1;
		GizmoPivotMode m_GizmoPivotMode = GizmoPivotMode::Pivot;
		GizmoSpaceMode m_GizmoSpaceMode = GizmoSpaceMode::Local;
		bool m_GizmoModeToolbarDocked = true;
		bool m_GizmoTransformToolbarDocked = false;
		bool m_GizmoModeToolbarDragging = false;
		bool m_GizmoTransformToolbarDragging = false;
		// Order of the two toolbars in the Scene top dock strip.  When false,
		// the transform (Q/W/E/R) toolbar is placed before the mode toolbar.
		bool m_GizmoModeToolbarFirst = true;
		float m_GizmoModeDockY = 0.0f;
		float m_GizmoModeDockHeight = 0.0f;
		glm::vec2 m_GizmoModeToolbarOffset = { 16.0f, 10.0f };
		glm::vec2 m_GizmoToolbarOffset = { 16.0f, 48.0f };

		SceneHierarchyPanel m_SceneHierarchyPanel;
		ContentBrowserPanel m_ContentBrowserPanel;
		
		Ref<EditorIconSet> m_EditorIcons;

		enum  SceneState
		{
			Edit = 0,
			Play = 1,
			Pause = 2
		};

		SceneState m_SceneState = SceneState::Edit;
		bool m_StepRequested = false;
		bool m_Is2DMode = false;
		bool m_ShowColliders = true;

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

		ColliderEditHandle m_ActiveColliderHandle = ColliderEditHandle::None;
		UUID m_ColliderEditEntity = UUID(0);
		glm::vec2 m_ColliderDragStartMouseWorld{ 0.0f };
		glm::vec2 m_ColliderDragStartCenter{ 0.0f };
		glm::vec2 m_ColliderDragStartHalfSize{ 0.0f };
		float m_ColliderDragStartRadius = 0.0f;
		float m_ColliderDragPlaneZ = 0.0f;
		bool m_ColliderHandleHovered = false;

		Ref<Project> m_CurrentProject;
		bool m_SceneDirty = false;

		bool m_ShowScenePanel = true;
		bool m_ShowGamePanel = true;
		bool m_ShowHierarchyPanel = true;
		bool m_ShowInspectorPanel = true;
		bool m_ShowProjectPanel = true;
		bool m_ShowBuildSettingsPanel = false;
		bool m_ShowProjectSettingsPanel = false;
		int m_ProjectSettingsPage = 0;
		Ref<Project> m_ProjectSettingsDraftProject;
		ProjectSettings m_ProjectSettingsDraft;
		std::array<std::array<char, 128>, Physics2DLayerCount> m_ProjectLayerNameBuffers{};
		std::array<char, 128> m_NewProjectTagBuffer{};
		std::string m_ProjectSettingsError;
		std::string m_ProjectSettingsStatus;
		bool m_OpenUnsavedChangesModal = false;
		std::function<bool()> m_PendingUnsavedAction;
	};

}
