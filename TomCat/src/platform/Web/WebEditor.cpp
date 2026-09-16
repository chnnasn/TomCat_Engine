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

namespace {
std::unique_ptr<TomCat::Application> application;
std::unique_ptr<TomCat::WebEditorSession> session;
std::string response, lastError;
void EnsureSession() {
  if (!session) { TomCat::Log::Init(); session = std::make_unique<TomCat::WebEditorSession>(); }
}
class EditorLayer final : public TomCat::Layer {
  TomCat::EditorCamera camera{45.0f, 16.0f / 9.0f, 0.1f, 1000.0f};
public:
  EditorLayer() { camera.Set2DMode(true); }
  void OnUpdate(TomCat::Timestep delta) override {
    if (auto scene = session->GetScene()) {
      const auto& window = application->GetWindow();
      scene->OnViewportResize(window.GetWidth(), window.GetHeight());
      camera.SetViewportSize(float(window.GetWidth()), float(window.GetHeight()));
      camera.OnUpdate(delta, false);
      scene->OnUpdateEditor(delta, camera);
    }
  }
};
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
    application->PushLayer(new EditorLayer()); return 0;
  } catch (const std::exception& error) { lastError = error.what(); application.reset(); return 1; }
}
void tc_web_editor_frame(double delta) {
  if (!application) return;
  try {
    TomCat::RenderCommand::SetClearColor({0.12f, 0.14f, 0.18f, 1}); TomCat::RenderCommand::Clear();
    application->Tick(float(std::clamp(delta, 0.0, 0.1)));
  } catch (const std::exception& error) { lastError = error.what(); }
}
void tc_web_editor_resize(int width, int height) {
  if (application && width > 0 && height > 0 && width <= 8192 && height <= 8192)
    static_cast<TomCat::WebWindow&>(application->GetWindow()).Resize(width, height);
}
void tc_web_editor_shutdown() {
  application.reset(); session.reset(); TomCat::AssetManager::Get().Shutdown();
}
}
#endif
