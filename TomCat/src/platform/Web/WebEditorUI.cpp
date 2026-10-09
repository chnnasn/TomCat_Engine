#include "tcpch.h"
#ifdef __EMSCRIPTEN__
#include "WebEditorUI.h"
#include "TomCat/Editor/WebEditorSession.h"
#include "TomCat/Asset/AssetManager.h"
#include "TomCat/Asset/SpriteAsset.h"
#include "TomCat/Utils/PathUtils.h"
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
#include "TomCat/Core/Application.h"
#include "TomCat/ImGui/ImGuiLayer.h"

namespace TomCat {
// Native file dialogs/external editors have no browser equivalent. Import and
// download are explicit host actions instead of blocking filesystem dialogs.
std::filesystem::path FileDialogs::OpenFile(const char*) { return {}; }
std::filesystem::path FileDialogs::SaveFile(const char*) { return {}; }
std::filesystem::path FileDialogs::OpenFolder() { return {}; }

WebEditorUI::WebEditorUI(WebEditorSession& session) : m_Session(session),
  m_Viewport(m_ViewportState, EditorViewportContext{
    m_Context,m_Camera,m_Hierarchy,m_EditorIcons,m_ViewportBounds,m_GizmoType,
    [this]{return m_Session.GetPreviewMode()==WebEditorSession::PreviewMode::Edit;},
    [this]{return m_GizmoPivotMode==GizmoPivotMode::Pivot;},
    [this]{return m_GizmoSpaceMode==GizmoSpaceMode::Local;},
    [this]{return m_ViewportHovered;},
    [this]{return m_ToolbarBlocked || IsSceneOrientationGizmoPointerInside();},
    [this]{return m_Session.HasUIEdit();},
    [this](const char*){m_Session.BeginUIEdit();},
    []{}, // EndUIEdit snapshots the completed transaction.
    [this]{m_Session.EndUIEdit(m_Session.GetSelection());},
    [this]{m_ShowScene=true; ImGui::SetWindowFocus("Scene");}
  }) {}

void WebEditorUI::FinishViewportEdit() {
  m_Viewport.ResetRectTransformEditState();
  if (m_ViewportState.ColliderTransactionActive || m_GizmoActive)
    m_Session.EndUIEdit(m_Session.GetSelection());
  m_Viewport.ResetColliderEditState();
  m_GizmoActive=false;
}
void WebEditorUI::OnAttach() {
  m_Camera.Set2DMode(true);
  LoadSceneToolbarLayout();
  m_EditorIcons = CreateRef<EditorIconSet>(); m_EditorIcons->Load();
  m_Hierarchy.SetIcons(m_EditorIcons); m_Content.SetIcons(m_EditorIcons);
  m_Content.SetAssetMutationsEnabled(true);
  m_Content.SetAssetsChangedCallback([this] { m_Actions |= 8u; });
  m_Content.SetAssetRenamedCallback([this](const auto&, const auto&) { m_Actions |= 8u; return true; });
  m_Content.SetAssetDeletionEnabled(true);
  m_Content.SetAssetDeletedCallback([this](const std::filesystem::path&) { m_Actions |= 8u; });
  AssetManager::Get().SetLiveReferenceProvider([this](AssetHandle handle) {
    auto scene = m_Session.GetScene();
    auto references = scene ? scene->FindAssetReferences(handle) : std::vector<AssetReference>{};
    for (auto& reference : references) reference.FilePath = "<Current Web Scene>";
    return references;
  });
  m_Hierarchy.SetPrefabCreationAllowed(false);
  m_Hierarchy.SetColliderGizmosEnabled(true);
  // Script components belong to the native Inspector, including removal and
  // asset drag/drop before managed field metadata has been loaded.
  m_Hierarchy.SetScriptEditingEnabled(true);
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
    if (m_Session.ActiveScenePath().empty()) {
      m_PendingSceneOpen = uint64_t(handle); m_ConfirmSceneOpen = true; return;
    }
    try {
      const auto result = m_Session.OpenSceneAsset(handle);
      if (result.find("\"ok\":false") != std::string::npos) m_Console.Push(ConsoleMessageSeverity::Error,result,"Scene");
    } catch (const std::exception& error) {
      m_Console.Push(ConsoleMessageSeverity::Error,error.what(),"Scene");
    }
  };
  m_Hierarchy.SetSceneLoadCallback(openScene); m_Content.SetSceneOpenCallback(openScene);
  m_Content.SetAuthoringAssetOpenCallback([this](AssetHandle handle,AssetType type){m_Hierarchy.OpenAuthoringAsset(handle,type);});
  m_Content.SetAssetSelectionCallback([this](const std::filesystem::path& path){m_Hierarchy.SetAssetSelection(path);if(!path.empty()) m_ShowInspector=true;});
  m_Hierarchy.SetAssetInspectorRenderer([this](const std::filesystem::path& path){m_Content.DrawAssetInspector(path);});
  m_Console.SetErrorPauseCallback([this]{if(m_Session.GetPreviewMode()==WebEditorSession::PreviewMode::Play) Preview("pause");});
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
void WebEditorUI::OnDetach() { AssetManager::Get().SetLiveReferenceProvider({}); m_Session.StopPreview(); }
void WebEditorUI::SyncContext() {
  m_Session.SyncSceneAssetName();
  if (m_Project != m_Session.GetProject()) {
    m_Project = m_Session.GetProject(); m_Hierarchy.SetProject(m_Project); m_Content.SetProject(m_Project);
    m_Is2DMode=!m_Project || m_Project->GetConfig().Template=="2D";
    m_Camera.Set2DMode(m_Is2DMode);
  }
  m_Content.SetActiveScenePath(m_Session.ActiveScenePath());
  const auto active = m_Session.GetPreviewScene() ? m_Session.GetPreviewScene() : m_Session.GetScene();
  if (m_Context != active) {
    FinishViewportEdit();
    m_Context = active; m_Hierarchy.SetContext(m_Context,false,true);
    m_GizmoActive = false;
  }
  if (m_Context) {
    const auto selected=m_Context->FindEntityByUUID(UUID(m_Session.GetSelection()));
    // Reapplying an unchanged selection clears desktop multi-selection and
    // the asset Inspector. Only synchronize when an RPC/history changed it.
    if(m_Hierarchy.GetSelectedEntity()!=selected) m_Hierarchy.SetSelectedEntity(selected);
  }
}
void WebEditorUI::OnUpdate(Timestep delta) {
  SyncContext();
  // The browser reports zoom and monitor changes as a content scale change, which can arrive
  // without a canvas resize. Rebaking fonts and style here keeps interface units tied to CSS
  // pixels instead of letting the UI shrink or grow with the display.
  const float displayScale = Application::Get().GetWindow().GetDPIScale();
  if (std::abs(displayScale - m_DisplayScale) > 0.001f) {
    m_DisplayScale = displayScale;
    if (ImGuiLayer* layer = Application::Get().GetImGuiLayer()) layer->SetUiScale(displayScale);
  }
  if (!m_Context) return;
  m_Session.AdvancePreview(float(delta));
  const uint32_t width = uint32_t(std::clamp(m_ViewportSize.x,1.0f,4096.0f));
  const uint32_t height = uint32_t(std::clamp(m_ViewportSize.y,1.0f,4096.0f));
  m_Framebuffer->Resize(width,height); m_Framebuffer->Bind();
  // WebGL requires typed clears for mixed float/integer MRT attachments.
  const GLfloat background[] = {71.0f/255.0f,71.0f/255.0f,71.0f/255.0f,1.0f};
  glClearBufferfv(GL_COLOR,0,background);
  glClear(GL_DEPTH_BUFFER_BIT);
  m_Framebuffer->ClearAttachment(1,-1);
  m_Context->OnViewportResize(width,height); m_Camera.SetViewportSize(float(width),float(height));
  m_Camera.OnUpdate(delta,m_ViewportHovered && !m_GizmoActive && !m_ViewportState.UIRectDragActive
    && m_ViewportState.ActiveColliderHandle==EditorViewportState::ColliderEditHandle::None
    && !IsSceneOrientationGizmoPointerInside());
  m_Context->OnUpdateEditor(delta,m_Camera);
  // Camera aspect belongs to the Game view, not the editor's Scene view.
  m_Context->OnViewportResize(uint32_t(std::max(m_GameSize.x,1.0f)),uint32_t(std::max(m_GameSize.y,1.0f)));
  m_Viewport.RenderSceneCameraOverlay();
  m_Viewport.RenderSceneCanvasOverlay();
  m_Framebuffer->Unbind();
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
  if (!m_ShowScene) { m_ViewportHovered=false; FinishViewportEdit(); return; }
  // Reserve the shared toolbar's 28px controls and padding in the menu row only.
  // Inflating FramePadding also enlarges the Scene title/tab and popup items.
  const float toolbarHeight=28.0f+2.0f*kSceneToolbarPadding;
  GImGui->NextWindowData.MenuBarOffsetMinVal.y=std::max(0.0f,toolbarHeight-ImGui::GetFrameHeight());
  ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding,ImVec2(0,0));
  ImGui::SetNextWindowScroll(ImVec2(0,0));
  const bool visible=ImGui::Begin("Scene", &m_ShowScene,ImGuiWindowFlags_MenuBar|ImGuiWindowFlags_NoScrollbar|ImGuiWindowFlags_NoScrollWithMouse);
  if (!visible && !m_GizmoModeToolbarDragging && !m_GizmoTransformToolbarDragging) {
    m_ViewportHovered=false; FinishViewportEdit(); ImGui::End(); ImGui::PopStyleVar(); return;
  }
  const bool editing=m_Session.GetPreviewMode()==WebEditorSession::PreviewMode::Edit;
  const auto available=ImGui::GetContentRegionAvail();
  const ImVec2 size(std::max(available.x,1.0f),std::max(available.y,1.0f));
  const auto origin=ImGui::GetCursorScreenPos();
  m_ViewportSize={std::max(size.x,1.0f),std::max(size.y,1.0f)};
  ImGui::Image(reinterpret_cast<ImTextureID>(uintptr_t(m_Framebuffer->GetColorAttachmentRendererID())),size,{0,1},{1,0});
  m_ViewportHovered=visible && ImGui::IsItemHovered();
  m_ViewportBounds[0]={origin.x,origin.y}; m_ViewportBounds[1]={origin.x+size.x,origin.y+size.y};
  if(editing && ImGui::BeginDragDropTarget()) {
    if(const auto* payload=ImGui::AcceptDragDropPayload(AssetDragDropPayloadID,ImGuiDragDropFlags_AcceptNoDrawDefaultRect);
      payload && payload->DataSize==sizeof(uint64_t) && m_Context) {
      const AssetHandle handle(*static_cast<const uint64_t*>(payload->Data));
      const auto* builtIn=FindBuiltInSpriteAsset(handle);
      const auto* metadata=AssetManager::Get().GetRegistry().GetMetadata(handle);
      if(builtIn || (metadata && !metadata->IsMissing && metadata->Type==AssetType::Texture2D)) {
        m_Session.BeginUIEdit();
        auto sprite=m_Context->CreateEntity(builtIn ? std::string(builtIn->Name) : PathToUTF8(metadata->FilePath.stem()));
        auto& renderer=sprite.AddComponent<SpriteRenderer>(glm::vec4(1));
        renderer.SpriteHandle=handle; renderer.Sprite=AssetManager::Get().LoadTexture(handle);
        m_Session.EndUIEdit(sprite.GetUUID()); m_Hierarchy.SetSelectedEntity(sprite);
      }
    }
    ImGui::EndDragDropTarget();
  }
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
  m_ToolbarBlocked=toolbarBlocked;
  m_ViewportHovered=m_ViewportHovered && !toolbarBlocked;
  if (m_ViewportHovered && !IsSceneOrientationGizmoPointerInside() && ImGui::GetIO().MouseWheel != 0) {
    MouseScrolledEvent event(0,ImGui::GetIO().MouseWheel); m_Camera.OnEvent(event);
  }
  const auto selected=m_Hierarchy.GetSelectedEntity();
  ImGuizmo::Enable(true);
  const bool rectHandles=m_Viewport.UI_RectTransformHandles();
  bool worldActive=false, worldHovered=false;
  if (editing && visible && m_GizmoType>=0 && selected && m_Context && !rectHandles
    && !m_Hierarchy.IsEditingCollider() && m_Context->IsVisibleInEditorHierarchy(selected)) {
    const auto originalTransform=selected.GetComponent<Transform>().GetTransform();
    auto transform=originalTransform;
    glm::vec3 minimum,maximum;
    const bool standardSingle=m_Context->GetChildrenUUIDs(selected).empty()
      && !selected.HasComponent<Tilemap2D>() && !selected.HasComponent<ParticleSystem2D>();
    if((standardSingle || m_GizmoPivotMode==GizmoPivotMode::Center) && m_Viewport.GetEntityBounds(selected,minimum,maximum))
      transform[3]=glm::vec4((minimum+maximum)*0.5f,1.0f);
    const auto initialGizmoTransform=transform;
    ImGuizmo::Enable((!toolbarBlocked && !IsSceneOrientationGizmoPointerInside()) || m_GizmoActive);
    ImGuizmo::AllowAxisFlip(false);
    ImGuizmo::SetID(0);
    ImGuizmo::SetOrthographic(m_Camera.IsOrthographic()); ImGuizmo::SetDrawlist();
    ImGuizmo::SetRect(origin.x,origin.y,size.x,size.y);
    const auto operation=static_cast<ImGuizmo::OPERATION>(m_GizmoType);
    glm::mat4 gizmoView(1.0f),gizmoProjection(1.0f);
    m_Camera.GetRightHandedToolMatrices(gizmoView,gizmoProjection);
    const float snapValue=operation==ImGuizmo::ROTATE ? 45.0f : 0.5f;
    float snap[3]={snapValue,snapValue,snapValue};
    ImGuizmo::Manipulate(glm::value_ptr(gizmoView),glm::value_ptr(gizmoProjection),operation,m_GizmoSpaceMode==GizmoSpaceMode::Local ? ImGuizmo::LOCAL : ImGuizmo::WORLD,glm::value_ptr(transform),nullptr,ImGui::GetIO().KeyCtrl ? snap : nullptr);
    const bool active=ImGuizmo::IsUsing();
    if (active) {
      if (!m_GizmoActive) m_Session.BeginUIEdit();
      m_Context->SetWorldTransform(selected,transform*glm::inverse(initialGizmoTransform)*originalTransform);
    }
    worldActive=active; worldHovered=ImGuizmo::IsOver(operation);
  }
  if(!worldActive && m_GizmoActive) m_Session.EndUIEdit(m_Session.GetSelection());
  m_GizmoActive=worldActive;
  m_Viewport.UI_ColliderEditHandles();
  UI_SceneOrientationGizmo();
  if (editing && m_ViewportHovered && ImGui::IsMouseClicked(ImGuiMouseButton_Left)
    && !ImGui::GetIO().KeyAlt && !worldHovered && !m_GizmoActive && m_Context
    && !m_ViewportState.UIRectHandleHovered && !m_ViewportState.UIRectDragActive
    && !m_ViewportState.ColliderHandleHovered
    && m_ViewportState.ActiveColliderHandle==EditorViewportState::ColliderEditHandle::None
    && !IsSceneOrientationGizmoPointerInside()) {
    // ImGui updates the pointer in NewFrame, after OnUpdate. Pick with this
    // frame's pointer before collider outlines write non-selectable IDs.
    const auto mouse=ImGui::GetMousePos();
    const auto spec=m_Framebuffer->GetSpecification();
    const int x=int((mouse.x-origin.x)/size.x*spec.Width);
    const int y=int((origin.y+size.y-mouse.y)/size.y*spec.Height);
    const int pixel=m_Framebuffer->ReadPixel(1,x,y);
    Entity entity = pixel < 0 ? Entity{} : Entity(entt::entity(pixel),m_Context.get());
    m_Hierarchy.SetSelectedEntity(entity); m_Session.SelectFromUI(entity ? uint64_t(entity.GetUUID()) : 0);
  }
  m_Framebuffer->Bind(); m_Viewport.RenderSceneColliderOverlays(); m_Framebuffer->Unbind();
  ImGui::End(); ImGui::PopStyleVar();
}
void WebEditorUI::Preview(const char* command) {
  try {
    m_Session.ControlPreview(command); m_PreviewError.clear(); SyncContext();
    if (std::string(command)=="play") { m_Console.OnPlayStarted(); m_Profiler.OnPlayStarted(); m_ShowGame=true; ImGui::SetWindowFocus("Game"); }
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
  if (m_ConfirmSceneOpen) { ImGui::OpenPopup("Open another scene?"); m_ConfirmSceneOpen = false; }
  if (ImGui::BeginPopupModal("Open another scene?",nullptr,ImGuiWindowFlags_AlwaysAutoResize)) {
    ImGui::TextUnformatted("The current scene has no file in Assets.");
    ImGui::TextUnformatted("Opening another scene will discard its in-memory content.");
    if (ImGui::Button("Cancel")) ImGui::CloseCurrentPopup();
    ImGui::SameLine();
    if (ImGui::Button("Discard and open")) {
      try { m_Session.OpenSceneAsset(m_PendingSceneOpen,true); m_Actions |= 8u; }
      catch (const std::exception& error) { m_Console.Push(ConsoleMessageSeverity::Error,error.what(),"Scene"); }
      ImGui::CloseCurrentPopup();
    }
    ImGui::EndPopup();
  }
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
    if (ImGui::BeginMenu("GameObject",editing)) { if(m_Hierarchy.DrawGameObjectMenu()){m_ShowHierarchy=true;ImGui::SetWindowFocus("Hierarchy");} ImGui::EndMenu(); }
    if (ImGui::BeginMenu("Window")) {
      if (ImGui::MenuItem("2D Scene",nullptr,m_Is2DMode)) { m_Is2DMode=!m_Is2DMode; m_Camera.Set2DMode(m_Is2DMode); }
      ImGui::MenuItem("Scene",nullptr,&m_ShowScene); ImGui::MenuItem("Game",nullptr,&m_ShowGame);
      ImGui::MenuItem("Hierarchy",nullptr,&m_ShowHierarchy); ImGui::MenuItem("Inspector",nullptr,&m_ShowInspector);
      ImGui::MenuItem("Project",nullptr,&m_ShowProject); ImGui::MenuItem("Console",nullptr,&m_ShowConsole);
      ImGui::MenuItem("Animation",nullptr,&m_ShowAnimation); ImGui::MenuItem("Animator",nullptr,&m_ShowAnimator);
      ImGui::MenuItem("Tile Palette",nullptr,&m_ShowTilePalette); ImGui::MenuItem("Profiler",nullptr,&m_ShowProfiler);
      ImGui::MenuItem("Asset Inspector",nullptr,&m_ShowAssetInspector); ImGui::EndMenu();
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
  if (const UUID frame=m_Hierarchy.ConsumeFrameEntityRequest(); uint64_t(frame)!=0 && m_Context)
    m_Viewport.FrameSceneEntity(m_Context->FindEntityByUUID(frame));
  // Panel selection is authoritative until an RPC/history operation swaps it.
  const auto selected=m_Hierarchy.GetSelectedEntity(); m_Session.SelectFromUI(selected ? uint64_t(selected.GetUUID()) : 0);
  m_Content.OnImGuiRender(&m_ShowProject);
  const auto focus=[](bool requested,bool& show,const char* name){if(requested){show=true;ImGui::SetWindowFocus(name);}};
  focus(m_Hierarchy.ConsumeAnimationOpenRequest(),m_ShowAnimation,"Animation");
  focus(m_Hierarchy.ConsumeAnimatorGraphOpenRequest(),m_ShowAnimator,"Animator");
  focus(m_Hierarchy.ConsumeTilePaletteOpenRequest(),m_ShowTilePalette,"Tile Palette");
  if(m_ShowAnimation) m_Hierarchy.OnAnimationImGuiRender(&m_ShowAnimation);
  if(m_ShowAnimator) m_Hierarchy.OnAnimatorGraphImGuiRender(&m_ShowAnimator);
  if(m_ShowTilePalette) m_Hierarchy.OnTilePaletteImGuiRender(&m_ShowTilePalette);
  m_Content.OnAssetInspectorRender(&m_ShowAssetInspector);
  ImGui::EndDisabled();
  m_Console.OnImGuiRender(&m_ShowConsole);
  m_Profiler.OnImGuiRender(&m_ShowProfiler);
  DrawGame(); DrawViewport();
  m_Settings.m_CurrentProject=m_Project; m_Settings.m_Running=!editingPanels; m_Settings.Draw();
  const auto& io=ImGui::GetIO();
  if (editingPanels && io.KeyCtrl && !io.WantTextInput) {
    if (ImGui::IsKeyPressed(ImGuiKey_O,false)) m_Actions|=8;
    if (ImGui::IsKeyPressed(ImGuiKey_Z,false)) History(io.KeyShift);
    if (ImGui::IsKeyPressed(ImGuiKey_Y,false)) History(true);
    if (ImGui::IsKeyPressed(ImGuiKey_S,false)) { m_Session.EndUIEdit(m_Session.GetSelection()); m_Actions|=1; }
  }
  if (editingPanels && !io.WantTextInput && !ImGui::IsPopupOpen(nullptr,ImGuiPopupFlags_AnyPopupId)
    && !m_GizmoActive && !m_ViewportState.UIRectDragActive && !m_ViewportState.ColliderTransactionActive
    && (m_Hierarchy.IsHierarchyFocused() || m_Hierarchy.IsInspectorFocused() || m_ViewportHovered)) {
    if(!io.KeyCtrl && !io.KeyAlt) {
      if(m_ViewportHovered && !m_GizmoActive && ImGui::IsKeyPressed(ImGuiKey_2,false)) { m_Is2DMode=!m_Is2DMode; m_Camera.Set2DMode(m_Is2DMode); }
      if(ImGui::IsKeyPressed(ImGuiKey_F,false)) m_Viewport.FrameSceneEntity(m_Hierarchy.GetSelectedEntity());
      if(ImGui::IsKeyPressed(ImGuiKey_Q,false)) m_GizmoType=-1;
      if(ImGui::IsKeyPressed(ImGuiKey_W,false)) m_GizmoType=ImGuizmo::TRANSLATE;
      if(ImGui::IsKeyPressed(ImGuiKey_E,false)) m_GizmoType=ImGuizmo::ROTATE;
      if(ImGui::IsKeyPressed(ImGuiKey_R,false)) m_GizmoType=ImGuizmo::SCALE;
    }
    if (ImGui::IsKeyPressed(ImGuiKey_Delete,false)) m_Hierarchy.HandleShortcut(261,false);
    if (io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_D,false)) m_Hierarchy.HandleShortcut(68,true);
    if (io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_X,false)) m_Hierarchy.HandleShortcut(88,true);
    if (io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_C,false)) m_Hierarchy.HandleShortcut(67,true);
    if (io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_V,false)) m_Hierarchy.HandleShortcut(86,true);
    if (!io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_F2,false)) m_Hierarchy.HandleShortcut(291,false);
  }
}
}
#endif
