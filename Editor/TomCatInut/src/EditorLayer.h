#pragma once

#include "TomCat.h"
#include "EditorIcons.h"
#include "Panels/SceneHierarchyPanel.h"

#include "TomCat/Renderer/EditorCamera.h"
#include "TomCat/Editor/EditorRecoveryService.h"
#include "TomCat/Editor/SceneHistory.h"
#include "Panels/ContentBrowserPanel.h"
#include "Panels/ConsolePanel.h"
#include "Scripting/ScriptProjectCompiler.h"
#include "Scripting/ScriptMetadataCache.h"
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
		AssetHandle GetSavedEditorSceneHandle() const;
		bool CreatePrefabFromEntity(Entity entity,
			const std::filesystem::path& destinationDirectory);
		Entity InstantiatePrefab(AssetHandle handle, std::optional<UUID> parent,
			std::optional<glm::vec3> rootWorldPosition);
		void ReportPrefabOperation(bool succeeded, std::string message);
		void ResizeSceneForGameView(const Ref<Scene>& scene);
		void CommitRuntimeSceneTransition();
		void ResetSceneInteractionState();
		void RequestDestructiveAction(std::function<bool()> action);
		void UI_UnsavedChangesModal();
		void UI_ProjectMigrationRecoveryModal();
		void UI_ProjectMigrationModal();
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
		void UI_ColliderEditHandles();
		void RenderSceneColliderOverlays();
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

		// Game Viewport
		glm::vec2 m_GameViewportSize = { 0.0f, 0.0f };
		glm::vec2 m_GameViewportBounds[2]{};

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
		std::filesystem::path m_StartupProjectPath;
		SceneHistory m_SceneHistory;
		EditorRecoveryService m_RecoveryService;
		EditorProjectLock m_ProjectLock;
		std::optional<EditorRecoveryService::RecoveryCandidate> m_PendingRecovery;
		bool m_OpenRecoveryModal = false;
		bool m_SceneTransactionChanged = false;
		bool m_GizmoTransactionActive = false;
		bool m_ColliderTransactionActive = false;
		bool m_BypassUnsavedCheck = false;
		struct PendingProjectMigration
		{
			std::filesystem::path ProjectPath;
			ProjectMigrationPreview Preview;
		};
		struct PendingProjectMigrationRecovery
		{
			std::filesystem::path ProjectPath;
			ProjectMigrationRecoveryPreview Preview;
		};
		std::optional<PendingProjectMigrationRecovery>
			m_PendingProjectMigrationRecovery;
		std::optional<PendingProjectMigrationRecovery>
			m_ApprovedProjectMigrationRecovery;
		EditorProjectLock m_PendingProjectMigrationRecoveryLock;
		bool m_OpenProjectMigrationRecoveryModal = false;
		std::string m_ProjectMigrationRecoveryStatus;
		bool m_ProjectMigrationRecoveryStatusSucceeded = false;
		std::optional<PendingProjectMigration> m_PendingProjectMigration;
		std::optional<PendingProjectMigration> m_ApprovedProjectMigration;
		EditorProjectLock m_PendingProjectMigrationLock;
		bool m_OpenProjectMigrationModal = false;

		bool m_ShowScenePanel = true;
		bool m_ShowGamePanel = true;
		bool m_ScenePanelDocked = true;
		bool m_GamePanelDocked = true;
		bool m_ShowHierarchyPanel = true;
		bool m_ShowInspectorPanel = true;
		bool m_ShowProjectPanel = true;
		bool m_ShowConsolePanel = false;
		bool m_ShowBuildSettingsPanel = false;
		bool m_FocusBuildSettingsPanel = false;
		uint32_t m_LastSavedPanelVisibilityMask = 0;
		bool m_PanelVisibilitySnapshotInitialized = false;
		std::string m_BuildSettingsStatus;
		bool m_BuildSettingsSucceeded = false;
		std::string m_PlayerBuildStatus;
		bool m_PlayerBuildSucceeded = false;
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
		std::array<int, 6> m_DockTabOrdersBeforeMaximize = {
			-1, -1, -1, -1, -1, -1
		};
		bool m_PanelMaximized = false;
		uint32_t m_EditorDockspaceId = 0;
		std::string m_LastWindowTitle;
		std::string m_PendingPanelFocus;
		int m_EditorPanelCycleIndex = 5;
		int m_ProjectSettingsPage = 0;
		Ref<Project> m_ProjectSettingsDraftProject;
		ProjectSettings m_ProjectSettingsDraft;
		PlayerSettings m_PlayerSettingsDraft;
		std::array<std::array<char, 128>, Physics2DLayerCount> m_ProjectLayerNameBuffers{};
		std::array<char, 128> m_NewProjectTagBuffer{};
		std::array<char, 129> m_PlayerProductNameBuffer{};
		std::array<char, 129> m_PlayerCompanyNameBuffer{};
		std::array<char, 65> m_PlayerVersionBuffer{};
		std::array<char, 513> m_PlayerSaveDirectoryBuffer{};
		std::array<char, 513> m_PlayerLogDirectoryBuffer{};
		std::array<char, 513> m_PlayerCrashDirectoryBuffer{};
		std::string m_ProjectSettingsError;
		std::string m_ProjectSettingsStatus;
		bool m_OpenUnsavedChangesModal = false;
		std::function<bool()> m_PendingUnsavedAction;
	};

}
