#pragma once

#include "TomCat.h"
#include "Panels/SceneHierarchyPanel.h"

#include "TomCat/Renderer/EditorCamera.h"
#include "Panels/ContentBrowserPanel.h"
#include <functional>
#include "TomCat/Project/Project.h"
#include "TomCat/Project/ProjectManager.h"

struct ImVec2;

namespace TomCat {

	class EditorLayer : public Layer
	{
	public:
		explicit EditorLayer(bool is2DMode = false);

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

		void NewScene();
		void OpenOrCreateSampleScene();
		void AddDefaultMainCamera();
		void OpenScene();
		void OpenScene(const std::filesystem::path& path);
		void SaveScene();
		void SaveSceneAs();
		
		void SerializeScene(Ref<Scene> scene, const std::filesystem::path& path);

		void OnScenePlay();
		void OnSceneStop();

		void OnDuplicateEntity();

		void UI_Toolbar();
		void UI_SceneGizmoModeToolbarRow();
		void UI_SceneGizmoModeToolbarOverlay();
		void UI_SceneGizmoToolbar();
		// Persist the Scene toolbar arrangement alongside ImGui's window layout.
		void LoadSceneToolbarLayout();
		bool SaveSceneToolbarLayout();
		// Load the read-only packaged defaults, then the active project's
		// per-user override. All writes go through this user-settings path.
		void LoadEditorLayout();
		bool SaveEditorLayout();
		std::filesystem::path GetEditorUserSettingsPath() const;
		// Shared drag helper: submits the invisible handle and owns the only
		// drag/dock state transitions used by both Scene toolbars.
		void UI_SceneToolbarDragHandle(const char* id, glm::vec2& offset, bool& docked, bool& dragging,
			const ImVec2& handleMin, const ImVec2& handleMax, float tearX, bool canDock);
		void UI_GameNoCameraOverlay();

		void OpenProject();
		void SaveProject();
	private:
		TomCat::OrthographicCameraController m_CameraController;

		Ref<VertexArray> m_SquareVA;
		Ref<Shader> m_FlatColorShader;
		Ref<Framebuffer> m_Framebuffer;
		Ref<Framebuffer> m_GameFramebuffer;

		Ref<Scene> m_ActiveScene;
		Ref<Scene> m_EditorScene;
		std::filesystem::path m_EditorScenePath;
		Entity m_CameraEntity;
		Entity m_SecondCamera;

		Entity m_HoveredEntity;

		bool m_PrimaryCamera = true;

		EditorCamera m_EditorCamera;

		bool m_ViewportFocused = false, m_ViewportHovered = false;
		glm::vec2 m_ViewportSize = { 0.0f, 0.0f };

		glm::vec2 m_ViewportBounds[2];

		// Game Viewport
		glm::vec2 m_GameViewportSize = { 0.0f, 0.0f };

		glm::vec4 m_SquareColor = { 0.2f, 0.3f, 0.8f, 1.0f };

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
		
		Ref<Texture2D> m_IconPlay, m_IconStop;

		enum  SceneState
		{
			Edit = 0,
			Play = 1
		};

		SceneState m_SceneState = SceneState::Edit;
		bool m_Is2DMode = false;

		Ref<Project> m_CurrentProject;
		std::filesystem::path m_CurrentScenePath;
		bool m_SceneDirty = false;
		bool m_PendingEditorLayoutLoad = false;
	};

}
