#include "tcpch.h"
#ifdef __EMSCRIPTEN__
#include "WebEditorUI.h"
#include "TomCat/Editor/WebEditorSession.h"
#include "TomCat/Asset/AssetManager.h"
#include "TomCat/Renderer/RenderCommand.h"
#include "TomCat/Utils/PlatformUtils.h"
#include "TomCat/Events/MouseEvent.h"
#include <GLES3/gl3.h>
#include "EditorPlayToolbar.h"
#include "SceneToolbarDrawing.h"
#include <imgui.h>
#include <imgui_internal.h>
#include <ImGuizmo.h>
#include <glm/gtc/type_ptr.hpp>

namespace TomCat {
// Native file dialogs/external editors have no browser equivalent. Import and
// download are explicit host actions instead of blocking filesystem dialogs.
std::filesystem::path FileDialogs::OpenFile(const char*) { return {}; }
std::filesystem::path FileDialogs::SaveFile(const char*) { return {}; }
std::filesystem::path FileDialogs::OpenFolder() { return {}; }

WebEditorUI::WebEditorUI(WebEditorSession& session) : m_Session(session) {}
void WebEditorUI::OnAttach() {
  m_Camera.Set2DMode(true);
  LoadSceneToolbarLayout();
  m_EditorIcons = CreateRef<EditorIconSet>(); m_EditorIcons->Load();
  m_Hierarchy.SetIcons(m_EditorIcons); m_Content.SetIcons(m_EditorIcons);
  m_Content.SetAssetMutationsEnabled(false);
  m_Hierarchy.SetPrefabCreationAllowed(false);
  m_Hierarchy.SetColliderGizmosEnabled(false);
  m_Hierarchy.SetScriptEditingEnabled(false);
  m_Hierarchy.SetSceneModifiedCallback([this](SceneHierarchyPanel::SceneModificationPhase phase) {
    using Phase = SceneHierarchyPanel::SceneModificationPhase;
    const auto selected = m_Hierarchy.GetSelectedEntity();
    const uint64_t id = selected ? uint64_t(selected.GetUUID()) : 0;
    if (phase == Phase::Begin) m_Session.BeginUIEdit();
    if (phase == Phase::Commit || phase == Phase::Instant) {
      if (phase == Phase::Instant) m_Session.BeginUIEdit();
      m_Session.EndUIEdit(id);
    }
    if (phase == Phase::Cancel) m_Session.EndUIEdit(id,true);
  });
  const auto openScene = [this](AssetHandle handle) {
    try {
      const auto result = m_Session.OpenSceneAsset(handle);
      if (result.find("\"ok\":false") != std::string::npos) m_Console.Push(ConsoleMessageSeverity::Error,result,"Scene");
    } catch (const std::exception& error) {
      m_Console.Push(ConsoleMessageSeverity::Error,error.what(),"Scene");
    }
  };
  m_Hierarchy.SetSceneLoadCallback(openScene); m_Content.SetSceneOpenCallback(openScene);
  m_Hierarchy.SetSpriteCreateCallback([this](AssetHandle handle) {
    if (!m_Context) return;
    m_Session.BeginUIEdit();
    auto entity = m_Context->CreateEntity("Sprite"); entity.AddComponent<SpriteRenderer>().SpriteHandle = handle;
    m_Hierarchy.SetSelectedEntity(entity); m_Session.EndUIEdit(entity.GetUUID());
  });
  FramebufferSpecification spec; spec.Width=960; spec.Height=540;
  spec.Attachments = {FramebufferTextureFormat::RGBA8,FramebufferTextureFormat::RED_INTEGER,FramebufferTextureFormat::DEPTH24STENCIL8};
  m_Framebuffer = Framebuffer::Create(spec);
  m_GameFramebuffer = Framebuffer::Create(spec);
  m_Settings.m_OnChanged = [this] { m_Hierarchy.SetProject(m_Project); };
  m_Console.Push(ConsoleMessageSeverity::Info,"Desktop Hierarchy, Inspector, Project and Console panels loaded.","Web Editor");
}
void WebEditorUI::OnDetach() { m_Session.StopPreview(); }
void WebEditorUI::SyncContext() {
  if (m_Project != m_Session.GetProject()) {
    m_Project = m_Session.GetProject(); m_Hierarchy.SetProject(m_Project); m_Content.SetProject(m_Project);
  }
  const auto active = m_Session.GetPreviewScene() ? m_Session.GetPreviewScene() : m_Session.GetScene();
  if (m_Context != active) {
    m_Context = active; m_Hierarchy.SetContext(m_Context,false,true);
    m_GizmoActive = false;
  }
  if (m_Context) m_Hierarchy.SetSelectedEntity(m_Context->FindEntityByUUID(UUID(m_Session.GetSelection())));
}
void WebEditorUI::OnUpdate(Timestep delta) {
  SyncContext();
  if (!m_Context) return;
  m_Session.AdvancePreview(float(delta));
  const uint32_t width = uint32_t(std::clamp(m_ViewportSize.x,1.0f,4096.0f));
  const uint32_t height = uint32_t(std::clamp(m_ViewportSize.y,1.0f,4096.0f));
  m_Framebuffer->Resize(width,height); m_Framebuffer->Bind();
  // WebGL requires typed clears for mixed float/integer MRT attachments.
  const GLfloat background[] = {0.12f,0.14f,0.18f,1.0f};
  glClearBufferfv(GL_COLOR,0,background);
  glClear(GL_DEPTH_BUFFER_BIT);
  m_Framebuffer->ClearAttachment(1,-1);
  m_Context->OnViewportResize(width,height); m_Camera.SetViewportSize(float(width),float(height));
  m_Camera.OnUpdate(delta,m_ViewportHovered && !m_GizmoActive);
  m_Context->OnUpdateEditor(delta,m_Camera); m_Framebuffer->Unbind();
  const auto game = m_Context;
  if (game) {
    const uint32_t gameWidth=uint32_t(std::clamp(m_GameSize.x,1.0f,4096.0f));
    const uint32_t gameHeight=uint32_t(std::clamp(m_GameSize.y,1.0f,4096.0f));
    m_GameFramebuffer->Resize(gameWidth,gameHeight); m_GameFramebuffer->Bind();
    glClearBufferfv(GL_COLOR,0,background); glClear(GL_DEPTH_BUFFER_BIT);
    m_GameFramebuffer->ClearAttachment(1,-1);
    game->OnViewportResize(gameWidth,gameHeight);
    game->SetRuntimeUIViewportMetrics(m_GameVisible && m_Session.GetPreviewMode()!=WebEditorSession::PreviewMode::Edit ? m_GameOrigin : glm::vec2(-1000000.0f),1.0f);
    game->OnRenderRuntime(); m_GameFramebuffer->Unbind();
  }
}
void WebEditorUI::History(bool redo) {
  const auto response = m_Session.HistoryFromUI(redo);
  if (response.find("\"ok\":false") != std::string::npos) m_Console.Push(ConsoleMessageSeverity::Warning,response,"History");
  SyncContext();
}
void WebEditorUI::DrawViewport() {
  if (!m_ShowScene) { m_ViewportHovered=false; return; }
  // Reserve the shared toolbar's 28px controls and padding in the menu row only.
  // Inflating FramePadding also enlarges the Scene title/tab and popup items.
  const float toolbarHeight=28.0f+2.0f*kSceneToolbarPadding;
  GImGui->NextWindowData.MenuBarOffsetMinVal.y=std::max(0.0f,toolbarHeight-ImGui::GetFrameHeight());
  ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding,ImVec2(0,0));
  const bool visible=ImGui::Begin("Scene", &m_ShowScene,ImGuiWindowFlags_MenuBar);
  if (!visible && !m_GizmoModeToolbarDragging && !m_GizmoTransformToolbarDragging) {
    m_ViewportHovered=false; ImGui::End(); ImGui::PopStyleVar(); return;
  }
  const bool editing=m_Session.GetPreviewMode()==WebEditorSession::PreviewMode::Edit;
  const auto available=ImGui::GetContentRegionAvail();
  const ImVec2 size(std::max(available.x,1.0f),std::max(available.y,1.0f));
  const auto origin=ImGui::GetCursorScreenPos();
  m_ViewportSize={std::max(size.x,1.0f),std::max(size.y,1.0f)};
  ImGui::Image(reinterpret_cast<ImTextureID>(uintptr_t(m_Framebuffer->GetColorAttachmentRendererID())),size,{0,1},{1,0});
  m_ViewportHovered=visible && ImGui::IsItemHovered();
  m_ViewportBounds[0]={origin.x,origin.y}; m_ViewportBounds[1]={origin.x+size.x,origin.y+size.y};
  // Use ImGui's actual menu rectangle, not an image origin shifted by padding.
  const ImRect dockRow=ImGui::GetCurrentWindow()->MenuBarRect();
  m_GizmoModeDockHeight=dockRow.GetHeight(); m_GizmoModeDockY=dockRow.Min.y;
  const bool wasDragging=m_GizmoModeToolbarDragging || m_GizmoTransformToolbarDragging;
  const bool menu=visible && ImGui::BeginMenuBar();
  if(menu) ImGui::GetWindowDrawList()->AddRectFilled(dockRow.Min,dockRow.Max,ImGui::GetColorU32(ImGuiCol_Tab));
  ImGui::BeginDisabled(!editing);
  UI_SceneGizmoModeToolbarOverlay(); UI_SceneGizmoToolbar(); UI_SceneToolbarDockPreview();
  const bool toolbarBlocked=ImGui::IsAnyItemHovered() || ImGui::IsAnyItemActive() ||
    m_GizmoModeToolbarDragging || m_GizmoTransformToolbarDragging || ImGui::IsPopupOpen("",ImGuiPopupFlags_AnyPopupId);
  ImGui::EndDisabled();
  if(menu) ImGui::EndMenuBar();
  if(wasDragging && !m_GizmoModeToolbarDragging && !m_GizmoTransformToolbarDragging) SaveSceneToolbarLayout();
  m_ViewportHovered=m_ViewportHovered && !toolbarBlocked;
  if (m_ViewportHovered && ImGui::GetIO().MouseWheel != 0) {
    MouseScrolledEvent event(0,ImGui::GetIO().MouseWheel); m_Camera.OnEvent(event);
  }
  const auto selected=m_Hierarchy.GetSelectedEntity();
  if (editing && visible && m_GizmoType>=0 && selected && m_Context) {
    auto transform=selected.GetComponent<Transform>().GetTransform();
    ImGuizmo::Enable(!toolbarBlocked || m_GizmoActive);
    ImGuizmo::SetOrthographic(m_Camera.IsOrthographic()); ImGuizmo::SetDrawlist();
    ImGuizmo::SetRect(origin.x,origin.y,size.x,size.y);
    const auto operation=static_cast<ImGuizmo::OPERATION>(m_GizmoType);
    glm::mat4 gizmoView(1.0f),gizmoProjection(1.0f);
    m_Camera.GetRightHandedToolMatrices(gizmoView,gizmoProjection);
    ImGuizmo::Manipulate(glm::value_ptr(gizmoView),glm::value_ptr(gizmoProjection),operation,m_GizmoSpaceMode==GizmoSpaceMode::Local ? ImGuizmo::LOCAL : ImGuizmo::WORLD,glm::value_ptr(transform));
    const bool active=ImGuizmo::IsUsing();
    if (active) {
      if (!m_GizmoActive) m_Session.BeginUIEdit();
      m_Context->SetWorldTransform(selected,transform);
    }
    if (!active && m_GizmoActive) m_Session.EndUIEdit(selected.GetUUID());
    m_GizmoActive=active;
  }
  if (editing && m_ViewportHovered && ImGui::IsMouseClicked(ImGuiMouseButton_Left) && (m_GizmoType<0 || !ImGuizmo::IsOver()) && !m_GizmoActive && m_Context) {
    const auto mouse=ImGui::GetMousePos(); const auto spec=m_Framebuffer->GetSpecification();
    const int x=int((mouse.x-origin.x)/size.x*spec.Width);
    const int y=int((1-(mouse.y-origin.y)/size.y)*spec.Height);
    const int pixel=m_Framebuffer->ReadPixel(1,x,y);
    Entity entity = pixel < 0 ? Entity{} : m_Context->FindEntityByPickingID(pixel);
    m_Hierarchy.SetSelectedEntity(entity); m_Session.SelectFromUI(entity ? uint64_t(entity.GetUUID()) : 0);
  }
  ImGui::End(); ImGui::PopStyleVar();
}
void WebEditorUI::Preview(const char* command) {
  try {
    m_Session.ControlPreview(command); m_PreviewError.clear(); SyncContext();
    if (std::string(command)=="play") { m_Console.OnPlayStarted(); m_ShowGame=true; ImGui::SetWindowFocus("Game"); }
    if (std::string(command)=="stop") { m_ShowScene=true; ImGui::SetWindowFocus("Scene"); }
  } catch(const std::exception& error) {
    m_PreviewError=error.what(); m_Console.Push(ConsoleMessageSeverity::Error,m_PreviewError,"Play");
  }
}
void WebEditorUI::DrawToolbar() {
  const bool running=m_Session.GetPreviewMode()!=WebEditorSession::PreviewMode::Edit;
  const bool paused=m_Session.GetPreviewMode()==WebEditorSession::PreviewMode::Pause;
  DrawEditorPlayToolbar(m_EditorIcons,bool(m_Session.GetScene()),running,paused,
    [this]{Preview("play");},[this]{Preview("stop");},
    [this,paused]{Preview(paused ? "resume" : "pause");},[this]{Preview("step");});
}
void WebEditorUI::DrawGame() {
  m_GameVisible=false;
  if(!m_ShowGame) return;
  if(ImGui::Begin("Game",&m_ShowGame)) {
    const bool running=m_Session.GetPreviewMode()!=WebEditorSession::PreviewMode::Edit;
    if(running) ImGui::TextDisabled(m_Session.GetPreviewMode()==WebEditorSession::PreviewMode::Pause ? "Paused" : "Playing");
    if(!m_PreviewError.empty()) ImGui::TextWrapped("%s",m_PreviewError.c_str());
    const auto available=ImGui::GetContentRegionAvail();
    m_GameSize={std::max(1.0f,available.x),std::max(1.0f,available.y)};
    const auto origin=ImGui::GetCursorScreenPos(); m_GameOrigin={origin.x,origin.y};
    if(m_Context) { ImGui::Image(reinterpret_cast<ImTextureID>(uintptr_t(m_GameFramebuffer->GetColorAttachmentRendererID())),{m_GameSize.x,m_GameSize.y},{0,1},{1,0}); }
    if(m_Context && !m_Context->GetPrimaryCameraEntity()) {
      const char* text="No cameras rendering";
      const auto label=ImGui::CalcTextSize(text);
      const ImVec2 pos(origin.x+(m_GameSize.x-label.x)*0.5f,origin.y+(m_GameSize.y-label.y)*0.5f);
      ImGui::GetWindowDrawList()->AddText(pos,IM_COL32(243,243,243,255),text);
    }
    m_GameVisible=bool(m_Context);
  }
  ImGui::End();
}
void WebEditorUI::OnImGuiRender() {
  SyncContext();
  const bool editing=m_Session.GetPreviewMode()==WebEditorSession::PreviewMode::Edit;
  if (ImGui::BeginMainMenuBar()) {
    if (ImGui::BeginMenu("File")) {
      if (ImGui::MenuItem("Save","Ctrl+S")) { m_Session.EndUIEdit(m_Session.GetSelection()); m_Actions|=1; }
      if (ImGui::MenuItem("Open Scene...", "Ctrl+O",false,editing)) m_Actions|=8;
      if (ImGui::MenuItem("Import image...",nullptr,false,editing)) m_Actions|=2;
      if (ImGui::MenuItem("Export project...")) m_Actions|=4;
      ImGui::EndMenu();
    }
    if (ImGui::BeginMenu("Edit")) {
      ImGui::BeginDisabled(!editing);
      if (ImGui::MenuItem("Undo","Ctrl+Z")) History(false);
      if (ImGui::MenuItem("Redo","Ctrl+Shift+Z")) History(true);
      ImGui::EndDisabled();
      ImGui::Separator();
      if(ImGui::MenuItem("Project Settings...")) { m_Settings.m_ShowProjectSettingsPanel=true; m_Settings.m_FocusProjectSettingsPanel=true; }
      ImGui::EndMenu();
    }
    if (ImGui::BeginMenu("GameObject",editing)) { m_Hierarchy.DrawGameObjectMenu(); ImGui::EndMenu(); }
    if (ImGui::BeginMenu("Window")) {
      ImGui::MenuItem("Scene",nullptr,&m_ShowScene); ImGui::MenuItem("Game",nullptr,&m_ShowGame);
      ImGui::MenuItem("Hierarchy",nullptr,&m_ShowHierarchy); ImGui::MenuItem("Inspector",nullptr,&m_ShowInspector);
      ImGui::MenuItem("Project",nullptr,&m_ShowProject); ImGui::MenuItem("Console",nullptr,&m_ShowConsole); ImGui::EndMenu();
    }
    ImGui::EndMainMenuBar();
  }
  const auto* viewport=ImGui::GetMainViewport();
  ImGui::SetNextWindowPos(viewport->WorkPos); ImGui::SetNextWindowSize(viewport->WorkSize); ImGui::SetNextWindowViewport(viewport->ID);
  ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding,ImVec2(0,0)); ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize,0);
  ImGui::Begin("TomCat Editor",nullptr,ImGuiWindowFlags_NoDocking|ImGuiWindowFlags_NoTitleBar|ImGuiWindowFlags_NoResize|ImGuiWindowFlags_NoMove|ImGuiWindowFlags_NoCollapse|ImGuiWindowFlags_NoBringToFrontOnFocus|ImGuiWindowFlags_NoNavFocus);
  ImGui::PopStyleVar(2);
  ImGui::BeginChild("ToolbarRegion",{0,42},false,ImGuiWindowFlags_NoScrollbar);
  DrawToolbar(); ImGui::EndChild();
  const ImGuiID dock=ImGui::GetID("WebEditorDock");
  if (!ImGui::DockBuilderGetNode(dock)) {
    ImGui::DockBuilderAddNode(dock,ImGuiDockNodeFlags_DockSpace); ImGui::DockBuilderSetNodeSize(dock,viewport->WorkSize);
    ImGuiID center=dock,left,right,bottom;
    ImGui::DockBuilderSplitNode(center,ImGuiDir_Left,0.20f,&left,&center);
    ImGui::DockBuilderSplitNode(center,ImGuiDir_Right,0.40f,&right,&center);
    ImGui::DockBuilderSplitNode(center,ImGuiDir_Down,0.30f,&bottom,&center);
    ImGui::DockBuilderDockWindow("Hierarchy",left); ImGui::DockBuilderDockWindow("Inspector",right);
    ImGui::DockBuilderDockWindow("Scene",center); ImGui::DockBuilderDockWindow("Game",center); ImGui::DockBuilderDockWindow("Project",bottom); ImGui::DockBuilderDockWindow("Console",bottom);
    ImGui::DockBuilderFinish(dock);
  }
  ImGui::DockSpace(dock); ImGui::End();
  const bool editingPanels=m_Session.GetPreviewMode()==WebEditorSession::PreviewMode::Edit;
  ImGui::BeginDisabled(!editingPanels);
  m_Hierarchy.OnImGuiRender(&m_ShowHierarchy,&m_ShowInspector);
  // Panel selection is authoritative until an RPC/history operation swaps it.
  const auto selected=m_Hierarchy.GetSelectedEntity(); m_Session.SelectFromUI(selected ? uint64_t(selected.GetUUID()) : 0);
  m_Content.OnImGuiRender(&m_ShowProject);
  ImGui::EndDisabled();
  m_Console.OnImGuiRender(&m_ShowConsole);
  DrawGame(); DrawViewport();
  m_Settings.m_CurrentProject=m_Project; m_Settings.m_Running=!editingPanels; m_Settings.Draw();
  const auto& io=ImGui::GetIO();
  if (editingPanels && io.KeyCtrl && !io.WantTextInput) {
    if (ImGui::IsKeyPressed(ImGuiKey_O,false)) m_Actions|=8;
    if (ImGui::IsKeyPressed(ImGuiKey_Z,false)) History(io.KeyShift);
    if (ImGui::IsKeyPressed(ImGuiKey_Y,false)) History(true);
    if (ImGui::IsKeyPressed(ImGuiKey_S,false)) { m_Session.EndUIEdit(m_Session.GetSelection()); m_Actions|=1; }
  }
  if (editingPanels && !io.WantTextInput && (m_Hierarchy.IsHierarchyFocused() || m_ViewportHovered)) {
    if(!io.KeyCtrl && !io.KeyAlt) {
      if(ImGui::IsKeyPressed(ImGuiKey_Q,false)) m_GizmoType=-1;
      if(ImGui::IsKeyPressed(ImGuiKey_W,false)) m_GizmoType=ImGuizmo::TRANSLATE;
      if(ImGui::IsKeyPressed(ImGuiKey_E,false)) m_GizmoType=ImGuizmo::ROTATE;
      if(ImGui::IsKeyPressed(ImGuiKey_R,false)) m_GizmoType=ImGuizmo::SCALE;
    }
    if (ImGui::IsKeyPressed(ImGuiKey_Delete,false)) m_Hierarchy.HandleShortcut(261,false);
    if (io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_D,false)) m_Hierarchy.HandleShortcut(68,true);
  }
}
}
#endif
