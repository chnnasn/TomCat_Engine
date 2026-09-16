#include "tcpch.h"
#ifdef __EMSCRIPTEN__
#include "WebEditorUI.h"
#include "TomCat/Editor/WebEditorSession.h"
#include "TomCat/Asset/AssetManager.h"
#include "TomCat/Renderer/RenderCommand.h"
#include "TomCat/Utils/PlatformUtils.h"
#include "TomCat/Events/MouseEvent.h"
#include <GLES3/gl3.h>
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
  m_Icons = CreateRef<EditorIconSet>(); m_Icons->Load();
  m_Hierarchy.SetIcons(m_Icons); m_Content.SetIcons(m_Icons);
  m_Content.SetAssetMutationsEnabled(false);
  m_Hierarchy.SetPrefabCreationAllowed(false);
  m_Hierarchy.SetColliderEditingAllowed(false);
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
  m_Console.Push(ConsoleMessageSeverity::Info,"Desktop Hierarchy, Inspector, Project and Console panels loaded.","Web Editor");
}
void WebEditorUI::SyncContext() {
  if (m_Project != m_Session.GetProject()) {
    m_Project = m_Session.GetProject(); m_Hierarchy.SetProject(m_Project); m_Content.SetProject(m_Project);
  }
  if (m_Context != m_Session.GetScene()) {
    m_Context = m_Session.GetScene(); m_Hierarchy.SetContext(m_Context,false,true);
    m_GizmoActive = false;
  }
  if (m_Context) m_Hierarchy.SetSelectedEntity(m_Context->FindEntityByUUID(UUID(m_Session.GetSelection())));
}
void WebEditorUI::OnUpdate(Timestep delta) {
  SyncContext();
  if (!m_Context) return;
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
}
void WebEditorUI::History(bool redo) {
  const auto response = m_Session.HistoryFromUI(redo);
  if (response.find("\"ok\":false") != std::string::npos) m_Console.Push(ConsoleMessageSeverity::Warning,response,"History");
  SyncContext();
}
void WebEditorUI::DrawViewport() {
  ImGui::Begin("Scene");
  const char* tools[] = {"Move", "Rotate", "Scale"};
  for (int i=0;i<3;++i) {
    if (i) ImGui::SameLine();
    if (ImGui::RadioButton(tools[i],m_GizmoOperation==i)) m_GizmoOperation=i;
  }
  ImGui::SameLine(); bool is2d=m_Camera.Is2DMode();
  if (ImGui::Checkbox("2D",&is2d)) m_Camera.Set2DMode(is2d);
  ImGui::SameLine(); ImGui::TextDisabled("Edit mode");
  const auto available=ImGui::GetContentRegionAvail();
  const ImVec2 size(std::max(available.x,1.0f),std::max(available.y,1.0f));
  const auto origin=ImGui::GetCursorScreenPos();
  m_ViewportSize={std::max(size.x,1.0f),std::max(size.y,1.0f)};
  ImGui::Image(reinterpret_cast<ImTextureID>(uintptr_t(m_Framebuffer->GetColorAttachmentRendererID())),size,{0,1},{1,0});
  m_ViewportHovered=ImGui::IsItemHovered();
  if (m_ViewportHovered && ImGui::GetIO().MouseWheel != 0) {
    MouseScrolledEvent event(0,ImGui::GetIO().MouseWheel); m_Camera.OnEvent(event);
  }
  const auto selected=m_Hierarchy.GetSelectedEntity();
  if (selected && m_Context) {
    auto transform=selected.GetComponent<Transform>().GetTransform();
    ImGuizmo::SetOrthographic(m_Camera.Is2DMode()); ImGuizmo::SetDrawlist();
    ImGuizmo::SetRect(origin.x,origin.y,size.x,size.y);
    const auto operation=m_GizmoOperation==0 ? ImGuizmo::TRANSLATE : m_GizmoOperation==1 ? ImGuizmo::ROTATE : ImGuizmo::SCALE;
    ImGuizmo::Manipulate(glm::value_ptr(m_Camera.GetViewMatrix()),glm::value_ptr(m_Camera.GetProjection()),operation,ImGuizmo::LOCAL,glm::value_ptr(transform));
    const bool active=ImGuizmo::IsUsing();
    if (active) {
      if (!m_GizmoActive) m_Session.BeginUIEdit();
      m_Context->SetWorldTransform(selected,transform);
    }
    if (!active && m_GizmoActive) m_Session.EndUIEdit(selected.GetUUID());
    m_GizmoActive=active;
  }
  if (m_ViewportHovered && ImGui::IsMouseClicked(ImGuiMouseButton_Left) && !ImGuizmo::IsOver() && !m_GizmoActive && m_Context) {
    const auto mouse=ImGui::GetMousePos(); const auto spec=m_Framebuffer->GetSpecification();
    const int x=int((mouse.x-origin.x)/size.x*spec.Width);
    const int y=int((1-(mouse.y-origin.y)/size.y)*spec.Height);
    const int pixel=m_Framebuffer->ReadPixel(1,x,y);
    Entity entity = pixel < 0 ? Entity{} : Entity(entt::entity(pixel),m_Context.get());
    m_Hierarchy.SetSelectedEntity(entity); m_Session.SelectFromUI(entity ? uint64_t(entity.GetUUID()) : 0);
  }
  ImGui::End();
}
void WebEditorUI::OnImGuiRender() {
  SyncContext();
  if (ImGui::BeginMainMenuBar()) {
    if (ImGui::BeginMenu("File")) {
      if (ImGui::MenuItem("Save","Ctrl+S")) { m_Session.EndUIEdit(m_Session.GetSelection()); m_Actions|=1; }
      if (ImGui::MenuItem("Import image...")) m_Actions|=2;
      if (ImGui::MenuItem("Export project...")) m_Actions|=4;
      ImGui::EndMenu();
    }
    if (ImGui::BeginMenu("Edit")) {
      if (ImGui::MenuItem("Undo","Ctrl+Z")) History(false);
      if (ImGui::MenuItem("Redo","Ctrl+Shift+Z")) History(true);
      ImGui::EndMenu();
    }
    if (ImGui::BeginMenu("GameObject")) { m_Hierarchy.DrawGameObjectMenu(); ImGui::EndMenu(); }
    if (ImGui::BeginMenu("Window")) {
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
  const ImGuiID dock=ImGui::GetID("WebEditorDock");
  if (!ImGui::DockBuilderGetNode(dock)) {
    ImGui::DockBuilderAddNode(dock,ImGuiDockNodeFlags_DockSpace); ImGui::DockBuilderSetNodeSize(dock,viewport->WorkSize);
    ImGuiID center=dock,left,right,bottom;
    ImGui::DockBuilderSplitNode(center,ImGuiDir_Left,0.20f,&left,&center);
    ImGui::DockBuilderSplitNode(center,ImGuiDir_Right,0.40f,&right,&center);
    ImGui::DockBuilderSplitNode(center,ImGuiDir_Down,0.30f,&bottom,&center);
    ImGui::DockBuilderDockWindow("Hierarchy",left); ImGui::DockBuilderDockWindow("Inspector",right);
    ImGui::DockBuilderDockWindow("Scene",center); ImGui::DockBuilderDockWindow("Project",bottom); ImGui::DockBuilderDockWindow("Console",bottom);
    ImGui::DockBuilderFinish(dock);
  }
  ImGui::DockSpace(dock); ImGui::End();
  m_Hierarchy.OnImGuiRender(&m_ShowHierarchy,&m_ShowInspector);
  // Panel selection is authoritative until an RPC/history operation swaps it.
  const auto selected=m_Hierarchy.GetSelectedEntity(); m_Session.SelectFromUI(selected ? uint64_t(selected.GetUUID()) : 0);
  m_Content.OnImGuiRender(&m_ShowProject); m_Console.OnImGuiRender(&m_ShowConsole);
  DrawViewport();
  const auto& io=ImGui::GetIO();
  if (io.KeyCtrl && !io.WantTextInput) {
    if (ImGui::IsKeyPressed(ImGuiKey_Z,false)) History(io.KeyShift);
    if (ImGui::IsKeyPressed(ImGuiKey_Y,false)) History(true);
    if (ImGui::IsKeyPressed(ImGuiKey_S,false)) { m_Session.EndUIEdit(m_Session.GetSelection()); m_Actions|=1; }
  }
  if (!io.WantTextInput && (m_Hierarchy.IsHierarchyFocused() || m_ViewportHovered)) {
    if (ImGui::IsKeyPressed(ImGuiKey_Delete,false)) m_Hierarchy.HandleShortcut(261,false);
    if (io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_D,false)) m_Hierarchy.HandleShortcut(68,true);
  }
}
}
#endif
