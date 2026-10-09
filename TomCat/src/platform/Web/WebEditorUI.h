#pragma once
#ifdef __EMSCRIPTEN__
#include "TomCat/Core/Layer.h"
#include "TomCat/Renderer/EditorCamera.h"
#include "TomCat/Renderer/Framebuffer.h"
#include "panels/SceneHierarchyPanel.h"
#include "panels/ContentBrowserPanel.h"
#include "panels/ConsolePanel.h"
#include "panels/ProfilerPanel.h"
#include "WebProjectSettingsPanel.h"
#include "Scripting/ScriptMetadataCache.h"
#include "EditorViewportHandles.h"
#include <imgui.h>
#include <cstdint>
#include <string>

namespace TomCat {
class WebEditorSession;
// Reuses the desktop panels and theme; only the browser host/dock viewport is new.
class WebEditorUI final : public Layer {
public:
  explicit WebEditorUI(WebEditorSession& session);
  void OnAttach() override;
  void OnDetach() override;
  void OnUpdate(Timestep delta) override;
  void OnImGuiRender() override;
  unsigned TakeActions() { unsigned result = m_Actions; m_Actions = 0; return result; }
  // Workspace persistence. The browser host owns the storage; the engine owns the format.
  // Managed sections ([Window]/[Table]/[Docking]) come from ImGui, and the panel
  // visibility mask is appended as an engine section in the same blob, mirroring how the
  // desktop editor keeps its custom ini sections next to the ones ImGui manages.
  std::string ComposeLayoutSettings() const;
  bool ApplyLayoutSettings(const std::string& settings);
  uint32_t GetPanelVisibilityMask() const;
private:
  void SyncContext();
  void History(bool redo);
  void DrawViewport();
  void DrawGame();
  void DrawToolbar();
  void Preview(const char* command);
  WebEditorSession& m_Session;
  ScriptMetadataCache m_ScriptMetadata;
  uint64_t m_ScriptMetadataGeneration = 0;
  SceneHierarchyPanel m_Hierarchy;
  ContentBrowserPanel m_Content;
  ConsolePanel m_Console;
  ProfilerPanel m_Profiler;
  Ref<Scene> m_Context;
  Ref<Project> m_Project;
  Ref<Framebuffer> m_Framebuffer;
  Ref<Framebuffer> m_GameFramebuffer;
  WebProjectSettingsPanel m_Settings;
  Ref<EditorIconSet> m_EditorIcons;
  EditorCamera m_Camera{45.0f,16.0f/9.0f,0.1f,1000.0f};
  glm::vec2 m_ViewportSize{960,540};
  glm::vec2 m_GameSize{960,540}, m_GameOrigin{0,0};
  bool m_ShowScene = true, m_ShowGame = true, m_GameVisible = false;
  enum class GizmoPivotMode { Pivot, Center };
  enum class GizmoSpaceMode { Local, World };
  GizmoPivotMode m_GizmoPivotMode=GizmoPivotMode::Pivot;
  GizmoSpaceMode m_GizmoSpaceMode=GizmoSpaceMode::Local;
  glm::vec2 m_ViewportBounds[2]{};
  bool m_GizmoModeToolbarDocked=true, m_GizmoTransformToolbarDocked=false;
  bool m_GizmoModeToolbarDragging=false, m_GizmoTransformToolbarDragging=false;
  bool m_GizmoModeToolbarFirst=true;
  float m_GizmoModeDockY=0, m_GizmoModeDockHeight=0;
  glm::vec2 m_GizmoModeToolbarOffset{16,10},m_GizmoToolbarOffset{16,48};
  void UI_SceneToolbarDragHandle(const char* id, glm::vec2& offset, bool& docked, bool& dragging,
    const ImVec2& handleMin, const ImVec2& handleMax, float tearX, bool canDock);
  void UI_SceneGizmoToolbar();
  void UI_SceneGizmoModeToolbarOverlay();
  void UI_SceneToolbarDockPreview();
  void SaveSceneToolbarLayout();
  void LoadSceneToolbarLayout();
  void UI_SceneOrientationGizmo();
  bool IsSceneOrientationGizmoPointerInside() const;
  void FinishViewportEdit();
  bool m_Is2DMode = true, m_SceneOrientationGizmoHovered = false, m_ToolbarBlocked = false;
  glm::vec2 m_SceneOrientationGizmoBounds[2]{};
  int m_SceneOrientationPressedTarget = -2;
  std::string m_PreviewError;
  bool m_ViewportHovered = false, m_GizmoActive = false;
  bool m_ShowHierarchy = true, m_ShowInspector = true, m_ShowProject = true, m_ShowConsole = true;
  bool m_ShowAnimation=false, m_ShowAnimator=false, m_ShowTilePalette=false, m_ShowProfiler=false, m_ShowAssetInspector=false;
  int m_GizmoType = -1;
  unsigned m_Actions = 0;
  uint64_t m_PendingSceneOpen = 0;
  bool m_ConfirmSceneOpen = false;
  // Last display scale the interface was built for. Browser zoom and monitor changes alter
  // it without a resize, so it is polled and applied through the ImGui layer.
  float m_DisplayScale = 1.0f;
  EditorViewportState m_ViewportState;
  EditorViewportHandles m_Viewport;
};
}
#endif
