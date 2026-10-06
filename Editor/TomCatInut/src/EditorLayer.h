#pragma once

#include "TomCat.h"
#include "EditorIcons.h"
#include "Panels/SceneHierarchyPanel.h"

#include "TomCat/Renderer/EditorCamera.h"
#include "TomCat/Editor/EditorRecoveryService.h"
#include "TomCat/Editor/SceneHistory.h"
#include "Panels/ContentBrowserPanel.h"
#include "Panels/ConsolePanel.h"
#include "Panels/ProfilerPanel.h"
#include "Scripting/ScriptProjectCompiler.h"
#include "Scripting/ScriptMetadataCache.h"
#include "EditorViewportHandles.h"
#include "EditorBuildController.h"
#include "Automation/AutomationServer.h"
#include "TomCat/Core/Input.h"
#include <map>
#include <unordered_set>
#include "EditorProjectSettingsController.h"
#include <functional>
#include "TomCat/Project/Project.h"
#include "TomCat/Project/ProjectManager.h"
#include "TomCat/Project/ProjectSettings.h"

#include <array>
#include <optional>
#include <string>
#include <string_view>

struct ImVec2;

namespace TomCat {

	class EditorLayer : public Layer
	{
	public:
		explicit EditorLayer(
			std::filesystem::path startupProjectPath = {});

		virtual ~EditorLayer() = default;

		virtual void OnAttach() override;
		virtual void OnDetach() override;

		void OnBeforeInputCapture() override;
		void OnUpdate(Timestep ts) override;
		virtual void OnImGuiRender() override;
		void OnEvent(Event& e) override;
	private:
        std::string ExecuteAutomation(const std::string& request);
        bool ApplyPrefabAction(Entity root, int action, UUID target, UUID component, UUID property, std::string& error);
        AutomationServer m_AutomationServer;
        bool m_AutomationStarting = false;
        bool m_AutomationCapturing = false;
        InputEventQueue m_AutomationInputQueue;
        Input::AutomationFrame m_AutomationInput;
        uint64_t m_AutomationRuntimeFrame = 0;
        uint32_t m_AutomationWidth = 640, m_AutomationHeight = 360;
        std::string m_AutomationSession = std::to_string(static_cast<uint64_t>(UUID()));
        std::map<std::string, std::pair<std::string, std::string>> m_AutomationResponses;
        std::deque<std::string> m_AutomationResponseOrder;
        std::unordered_set<std::string> m_AutomationSeenRequests;
        const Scene* m_AutomationObservedScene = nullptr;
        uint64_t m_AutomationSceneEpoch = 0;
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
		AssetHandle GetSavedEditorSceneHandle() const;
		bool CreatePrefabFromEntity(Entity entity,
			const std::filesystem::path& destinationDirectory);
		Entity InstantiatePrefab(AssetHandle handle, std::optional<UUID> parent,
			std::optional<glm::vec3> rootWorldPosition);
		void ReportPrefabOperation(bool succeeded, std::string message);
		void RefreshLinkedPrefabs();
		void ResizeSceneForGameView(const Ref<Scene>& scene);
		void CommitRuntimeSceneTransition();
		void ResetSceneInteractionState();
		void RequestDestructiveAction(std::function<bool()> action);
		void UI_UnsavedChangesModal();
		void UI_RecoveryModal();
		void RequestExit();

		bool CaptureSceneArchive(std::string& archive) const;
		void InitializeSceneHistory(bool isSaved);
		void BeginSceneTransaction(const char* label);
		void UpdateSceneTransaction();
		void CommitSceneTransaction();
		void CommitImmediateSceneTransaction(const char* label);
		void CancelSceneTransaction();
		void OnSceneModified(SceneHierarchyPanel::SceneModificationPhase phase);
		bool ApplyHistorySnapshot(const SceneHistory::Snapshot& snapshot);
		bool UndoScene();
		bool RedoScene();
		void MarkCurrentSceneSaved();
		bool IsSceneDirty() const { return m_SceneHistory.IsDirty(); }
		void ScheduleCurrentSceneAutosave();
		void CheckForRecovery();
		bool RestorePendingRecovery();

		void OnScenePlay();
		void OnScenePause();
		void OnSceneStep();
		void OnSceneStop();
		bool IsSceneRunning() const;

		void UI_Toolbar();
		void UI_SceneGizmoModeToolbarOverlay();
		void UI_SceneGizmoToolbar();
		void UI_SceneToolbarDockPreview();
		void UI_SceneOrientationGizmo();
		bool IsSceneOrientationGizmoPointerInside() const;
		// Screen-space UI uses RectTransform pixel coordinates, so it needs a
		// dedicated orthographic ImGuizmo projection. Translation is mapped back to
		// AnchoredPosition while rotation/scale reuse the entity Transform fields.
		bool UI_RectTransformHandles();
		void ResetRectTransformEditState();
		void UI_ColliderEditHandles();
		void RenderSceneColliderOverlays();
		void RenderSceneCameraOverlay();
		void RenderSceneCanvasOverlay();
		void FrameSceneEntity(Entity entity);
		bool ScreenToWorldOnPlane(const glm::vec2& screenPosition, float worldZ,
			glm::vec2& worldPosition) const;
		bool WorldToScreen(const glm::vec3& worldPosition, glm::vec2& screenPosition) const;
		void ResetColliderEditState();
		// Persist the Scene toolbar arrangement alongside ImGui's window layout.
		void LoadSceneToolbarLayout();
		void SaveSceneToolbarLayout();
		void LoadEditorPanelLayout();
		bool SaveEditorPanelLayout();
		void SaveEditorLayoutIfNeeded();
		uint32_t GetEditorPanelVisibilityMask() const;
		void ApplyPendingPanelMaximizeTransition(uint32_t dockspaceId,
			const ImVec2& dockspaceSize);
		void DetectPanelTabDoubleClick();
        void UI_PanelTabContextMenu();
        void ApplyPendingTabActions();
        bool* PanelVisibility(const std::string& name);
		void RestorePanelLayoutBeforePersistence();
		bool ShouldRenderDockPanel(std::string_view windowName) const;
		// Shared drag helper: submits the invisible handle and owns the only
		// drag/dock state transitions used by both Scene toolbars.
		void UI_SceneToolbarDragHandle(const char* id, glm::vec2& offset, bool& docked, bool& dragging,
			const ImVec2& handleMin, const ImVec2& handleMax, float tearX, bool canDock);
		void UI_GameNoCameraOverlay();
		void UI_MainMenuBar();
		void UI_BuildSettings();
		bool BuildPlayer(bool runAfterBuild);
		void UI_ProjectSettings();
		void OpenProjectSettingsPanel();
		void UpdateWindowTitle();
		void LoadProjectSettingsDraft();
		void SyncProjectSettingsLayerBuffers();
		void SyncPlayerSettingsBuffers();
		bool PersistProjectSettingsDraft();
		bool PersistPlayerSettingsDraft();
		void ClearProjectSettingsFeedback();
		void FocusEditorPanel(const char* panelName, bool& panelVisible);
		void CycleEditorPanel(int direction);
		bool PrepareManagedRuntime();
		bool ReconcileManagedScriptFields(const Ref<Scene>& scene);
		void UpdateScriptCompilation(Timestep ts);
		void ResetScriptCompileTracking();

		bool OpenProject();
		bool OpenProject(const std::filesystem::path& path);
		void SaveProject();
	private:
		Ref<Framebuffer> m_Framebuffer;
		Ref<Framebuffer> m_GameFramebuffer;

		Ref<Scene> m_ActiveScene;
		Ref<Scene> m_EditorScene;
		SceneManager m_RuntimeSceneManager;
		std::filesystem::path m_EditorScenePath;

		Entity m_HoveredEntity;

		EditorCamera m_EditorCamera;

		bool m_ViewportFocused = false;
		bool m_ViewportCanvasHovered = false;
		bool m_ViewportCameraDragOwned = false;
		glm::vec2 m_ViewportSize = { 0.0f, 0.0f };

		glm::vec2 m_ViewportBounds[2];
		glm::vec2 m_SceneOrientationGizmoBounds[2]{};
		bool m_SceneOrientationGizmoHovered = false;
		// -2 = no pending click, -1 = projection toggle, 0..5 = axis handle.
		int m_SceneOrientationPressedTarget = -2;

		// Game Viewport
		glm::vec2 m_GameViewportSize = { 0.0f, 0.0f };
		glm::vec2 m_GameViewportBounds[2]{};
		int m_GameViewResolutionIndex = 2;
		float m_GameViewScale = 1.0f;
		float m_GameViewEffectiveScale = 1.0f;
		bool m_GameViewStatsVisible = false;

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
		ConsolePanel m_ConsolePanel;
		ProfilerPanel m_ProfilerPanel;
		ScriptProjectCompiler m_ScriptCompiler;
		ScriptMetadataCache m_ScriptMetadata;
		float m_ScriptSourcePollCountdown = 0.0f;
		float m_ScriptCompileDebounceRemaining = 0.65f;
		std::string m_ObservedScriptSourceHash;
		bool m_PlayScriptDirtyNoticeShown = false;
		
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
        bool m_SceneViewVisible = true, m_GameViewVisible = true;
        bool m_SceneViewDirty = true, m_GameViewDirty = true;
        const Scene* m_LastRenderedScene = nullptr;
        uint64_t m_LastRenderHistory = 0, m_LastRenderImport = 0, m_LastRenderSelection = 0;
        glm::mat4 m_LastEditorViewProjection{0.0f};
        glm::vec2 m_LastSceneRenderSize{0.0f}, m_LastGameRenderSize{0.0f};

		using ColliderEditHandle = EditorViewportState::ColliderEditHandle;


		friend class EditorViewportHandles;
		friend class EditorBuildController;
		friend class EditorProjectSettingsController;

		EditorViewportState m_ViewportState;
		EditorBuildState m_BuildState;
		EditorViewportHandles m_Viewport;
		EditorBuildController m_Build;
		EditorProjectSettingsController m_ProjectSettings;

		Ref<Project> m_CurrentProject;
		std::filesystem::path m_StartupProjectPath;
		SceneHistory m_SceneHistory;
		uint64_t m_PrefabImportRevision = 0;
		struct PrefabFileEdit
		{
			SceneHistory::StateId BeforeState = 0, AfterState = 0;
			AssetHandle Asset{ 0 };
			std::string Before, After;
		};
		std::vector<PrefabFileEdit> m_PrefabFileEdits;
		EditorRecoveryService m_RecoveryService;
		EditorProjectLock m_ProjectLock;
		std::optional<EditorRecoveryService::RecoveryCandidate> m_PendingRecovery;
		bool m_OpenRecoveryModal = false;
		bool m_SceneTransactionChanged = false;
		bool m_GizmoTransactionActive = false;
		bool m_GizmoDragActive = false;
		bool m_GizmoHandleHovered = false;
		bool m_BypassUnsavedCheck = false;
		bool m_ShowScenePanel = true;
		bool m_ShowGamePanel = true;
		bool m_ShowAnimationPanel = false;
		bool m_ShowAnimatorPanel = false;
		bool m_ShowTilePalettePanel = false;
		bool m_ScenePanelDocked = true;
		bool m_GamePanelDocked = true;
		bool m_ShowHierarchyPanel = true;
		bool m_ShowInspectorPanel = true;
		bool m_ShowProjectPanel = true;
		bool m_ShowConsolePanel = false;
		bool m_ShowProfilerPanel = false;
		bool m_ShowBuildSettingsPanel = false;
		bool m_FocusBuildSettingsPanel = false;
		uint32_t m_LastSavedPanelVisibilityMask = 0;
		bool m_PanelVisibilitySnapshotInitialized = false;
		bool m_ShowProjectSettingsPanel = false;
		bool m_FocusProjectSettingsPanel = false;
		enum class PanelMaximizeAction
		{
			None = 0,
			Maximize,
			Restore
		};
		PanelMaximizeAction m_PendingPanelMaximizeAction =
			PanelMaximizeAction::None;
		std::string m_PendingMaximizedPanelWindow;
		std::string m_MaximizedPanelWindow;
		std::string m_DockLayoutBeforeMaximize;
		std::array<int, 10> m_DockTabOrdersBeforeMaximize = {
			-1, -1, -1, -1, -1, -1, -1, -1, -1, -1
		};
		bool m_PanelMaximized = false;
        std::string m_TabContextPanel, m_PendingTabClose, m_PendingTabAdd;
        uint32_t m_TabContextDockID = 0;
        bool m_ShowAssetInspector = false;
        bool m_ShowRuntimeScenes = false;
        bool m_ShowEditorPreferences = false;
        float m_EditorUIScale = 1.0f;
        int m_LayoutRequest = 0;
		uint32_t m_EditorDockspaceId = 0;
		std::string m_LastWindowTitle;
		std::string m_PendingPanelFocus;
		std::string m_PendingPanelFocusAfterRestore;
		std::string m_PendingRestoredTabWindow;
		int m_PendingRestoredTabOrder = -1;
		int m_EditorPanelCycleIndex = 5;
		bool m_OpenUnsavedChangesModal = false;
		std::function<bool()> m_PendingUnsavedAction;
		std::function<void()> m_PendingProjectOpen;
	};

}
