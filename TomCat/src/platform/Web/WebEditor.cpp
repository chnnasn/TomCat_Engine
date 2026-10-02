#include "tcpch.h"
#ifdef __EMSCRIPTEN__
#include "TomCat/Core/Application.h"
#include "TomCat/Core/Layer.h"
#include "TomCat/Asset/AssetManager.h"
#include "TomCat/Editor/WebEditorSession.h"
#include "TomCat/Scene/Scene.h"
#include "TomCat/Renderer/EditorCamera.h"
#include "TomCat/Renderer/RenderCommand.h"
#include "WebWindow.h"
#include "WebEditorUI.h"
#include "TomCat/ImGui/ImGuiLayer.h"

namespace {
std::unique_ptr<TomCat::Application> application;
std::unique_ptr<TomCat::WebEditorSession> session;
std::unique_ptr<TomCat::ImGuiLayer> gui;
TomCat::WebEditorUI* editorUI = nullptr;
std::string response, lastError;
void EnsureSession() {
  if (!session) { TomCat::Log::Init(); session = std::make_unique<TomCat::WebEditorSession>(); }
}
}
extern "C" {
int tc_web_editor_set_managed_assembly(const uint8_t* assembly, size_t assemblySize,
  const uint8_t* pdb, size_t pdbSize) {
  EnsureSession(); lastError.clear();
  if (!assembly || assemblySize == 0 || (pdbSize != 0 && !pdb)) {
    lastError = "Invalid managed assembly buffer"; return 1;
  }
  return session->SetManagedAssembly({assembly, assemblySize}, {pdb, pdbSize},
    lastError) ? 0 : 2;
}
const char* tc_web_editor_rpc(const char* request) {
  EnsureSession(); response = session->Invoke(request ? request : ""); return response.c_str();
}
const char* tc_web_editor_error() { return lastError.c_str(); }
int tc_web_editor_boot(int width, int height) {
  lastError.clear();
  try {
    if (application || width < 1 || height < 1 || width > 8192 || height > 8192) throw std::runtime_error("Invalid editor viewport or already running");
    EnsureSession();
    application = std::make_unique<TomCat::Application>(TomCat::WindowProps("TomCat Web Editor", width, height), false);
    gui = std::make_unique<TomCat::ImGuiLayer>(); gui->OnAttach();
    editorUI = new TomCat::WebEditorUI(*session); application->PushLayer(editorUI); return 0;
  } catch (const std::exception& error) {
    lastError = error.what(); if (gui) { gui->OnDetach(); gui.reset(); }
    editorUI = nullptr; application.reset(); return 1;
  }
}
void tc_web_editor_frame(double delta) {
  if (!application) return;
  try {
    TomCat::RenderCommand::SetClearColor({0.12f, 0.14f, 0.18f, 1}); TomCat::RenderCommand::Clear();
    application->Tick(float(std::clamp(delta, 0.0, 0.1)));
    gui->Begin(); editorUI->OnImGuiRender(); gui->End();
  } catch (const std::exception& error) { lastError = error.what(); }
}
void tc_web_editor_resize(int width, int height) {
  if (application && width > 0 && height > 0 && width <= 8192 && height <= 8192)
    static_cast<TomCat::WebWindow&>(application->GetWindow()).Resize(width, height);
}
void tc_web_editor_shutdown() {
  // The workspace has to be captured while the ImGui context still owns the dock tree and
  // the panel visibility flags; destroying the context discards both.
  if (editorUI) { response = editorUI->ComposeLayoutSettings(); }
  if (gui) { gui->OnDetach(); gui.reset(); }
  editorUI = nullptr;
  application.reset(); session.reset(); TomCat::AssetManager::Get().Shutdown();
}
const char* tc_web_editor_state() { EnsureSession(); response = session->Status(); return response.c_str(); }
unsigned tc_web_editor_take_actions() { return editorUI ? editorUI->TakeActions() : 0; }
const char* tc_web_editor_save_layout() {
  lastError.clear();
  if (!application || !editorUI) { lastError = "Editor is not running"; return ""; }
  response = editorUI->ComposeLayoutSettings();
  return response.c_str();
}
int tc_web_editor_load_layout(const char* settings) {
  lastError.clear();
  if (!application || !editorUI) { lastError = "Editor is not running"; return 0; }
  if (!settings) { lastError = "Missing layout settings"; return 0; }
  if (!editorUI->ApplyLayoutSettings(settings)) {
    lastError = "Layout settings are not usable for this build";
    return 0;
  }
  return 1;
}
void tc_web_editor_set_ui_scale(float scale) {
  lastError.clear();
  if (!gui) { lastError = "Editor is not running"; return; }
  gui->SetUiScale(scale);
}
}
#endif
