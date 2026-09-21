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
  if (gui) { gui->OnDetach(); gui.reset(); }
  editorUI = nullptr;
  application.reset(); session.reset(); TomCat::AssetManager::Get().Shutdown();
}
const char* tc_web_editor_state() { EnsureSession(); response = session->Status(); return response.c_str(); }
unsigned tc_web_editor_take_actions() { return editorUI ? editorUI->TakeActions() : 0; }
}
#endif
