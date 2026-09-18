#pragma once
#ifdef __EMSCRIPTEN__
#include "TomCat/Core/Layer.h"
#include "TomCat/Renderer/EditorCamera.h"
#include "TomCat/Renderer/Framebuffer.h"
#include "panels/SceneHierarchyPanel.h"
#include "panels/ContentBrowserPanel.h"
#include "panels/ConsolePanel.h"

namespace TomCat {
class WebEditorSession;
// Reuses the desktop panels and theme; only the browser host/dock viewport is new.
class WebEditorUI final : public Layer {
public:
  explicit WebEditorUI(WebEditorSession& session);
  void OnAttach() override;
  void OnUpdate(Timestep delta) override;
  void OnImGuiRender() override;
  unsigned TakeActions() { unsigned result = m_Actions; m_Actions = 0; return result; }
private:
  void SyncContext();
  void History(bool redo);
  void DrawViewport();
  WebEditorSession& m_Session;
  SceneHierarchyPanel m_Hierarchy;
  ContentBrowserPanel m_Content;
  ConsolePanel m_Console;
  Ref<Scene> m_Context;
  Ref<Project> m_Project;
  Ref<Framebuffer> m_Framebuffer;
  Ref<EditorIconSet> m_Icons;
  EditorCamera m_Camera{45.0f,16.0f/9.0f,0.1f,1000.0f};
  glm::vec2 m_ViewportSize{960,540};
  bool m_ViewportHovered = false, m_GizmoActive = false;
  bool m_ShowHierarchy = true, m_ShowInspector = true, m_ShowProject = true, m_ShowConsole = true;
  int m_GizmoOperation = 0;
  unsigned m_Actions = 0;
};
}
#endif
