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
#include <yaml-cpp/yaml.h>
#include <set>

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
// The display scale and the workspace reach the host through the existing RPC channel. A
// dedicated export per call would mean a new native import in the managed WebAssembly module,
// and those imports resolve when the module is instantiated: one wrong name takes the whole
// editor down before any diagnostic can run. Routing through the channel the host already uses
// keeps the native surface at one entry point.
void ValidateLocalRequest(const YAML::Node& node, int depth, size_t& count) {
  if (depth > 32 || ++count > 50000) throw std::runtime_error("Request nesting or size limit exceeded");
  if (node.IsMap()) {
    std::set<std::string> keys;
    for (auto entry : node) {
      if (!entry.first.IsScalar() || !keys.insert(entry.first.Scalar()).second)
        throw std::runtime_error("Duplicate or invalid request key");
      ValidateLocalRequest(entry.second, depth + 1, count);
    }
  } else if (node.IsSequence()) for (auto child : node) ValidateLocalRequest(child, depth + 1, count);
}
std::string RequestString(const YAML::Node& node) {
  if (!node.IsScalar() || node.Tag() != "!") throw std::runtime_error("Expected a JSON string");
  return node.Scalar();
}
std::string QuoteJson(const std::string& value) {
  std::string quoted = "\"";
  for (const char current : value) {
    switch (current) {
      case '"': quoted += "\\\""; break;
      case '\\': quoted += "\\\\"; break;
      case '\n': quoted += "\\n"; break;
      case '\r': quoted += "\\r"; break;
      case '\t': quoted += "\\t"; break;
      default:
        if (static_cast<unsigned char>(current) < 0x20) { char buffer[8]; std::snprintf(buffer, sizeof(buffer), "\\u%04x", current); quoted += buffer; }
        else quoted.push_back(current);
    }
  }
  quoted += "\"";
  return quoted;
}
std::string LocalRpcReply(const std::string& requestId, const std::string& payload) {
  return "{\"protocol\":\"tomcat.web.v1\",\"requestId\":" + QuoteJson(requestId)
    + ",\"ok\":true,\"result\":" + payload + "}";
}
const char* tc_web_editor_rpc(const char* request) {
  EnsureSession();
  const std::string text = request ? request : "";
  std::string requestId;
  try {
    if (text.size() > 8 * 1024 * 1024) throw std::runtime_error("Request exceeds 8 MiB");
    const auto root = YAML::Load(text);
    size_t count = 0; ValidateLocalRequest(root, 0, count);
    if (!root.IsMap()) throw std::runtime_error("Expected request object");
    requestId = RequestString(root["requestId"]);
    if (RequestString(root["protocol"]) != "tomcat.web.v1") throw std::runtime_error("Unsupported protocol");
    const auto type = RequestString(root["type"]);
    const auto payload = root["payload"];
    if (!payload.IsMap()) throw std::runtime_error("Expected payload object");
  if (type == "editor.setDisplayScale") {
    const auto scale = payload["scale"];
    if (!scale.IsScalar() || scale.Tag() == "!") throw std::runtime_error("Expected numeric display scale");
    const float value = scale.as<float>();
    if (!std::isfinite(value) || value <= 0) throw std::runtime_error("Display scale must be finite and positive");
    // Record it on the window as well: the interface polls the window scale every frame, and
    // without this the poll would immediately overwrite the host value with the (unreliable)
    // GLFW content scale.
    if (application) static_cast<TomCat::WebWindow&>(application->GetWindow()).SetDisplayScale(value);
    const bool applied = gui && gui->SetUiScale(value);
    // Report back the numbers the interface was actually built for: the host cannot see the
    // font atlas from the outside, and a check that only asserted the call happened would
    // pass even if the scale never reached the style.
    char numbers[192];
    std::snprintf(numbers, sizeof(numbers),
      "{\"applied\":%s,\"displayScale\":%.3f,\"effectiveScale\":%.3f,\"baseFontSize\":%.2f,\"bakedFontSize\":%.2f}",
      applied ? "true" : "false", static_cast<double>(gui ? gui->GetUiScale() : 0.0f),
      static_cast<double>(gui ? gui->GetEffectiveScale() : 0.0f),
      static_cast<double>(gui ? gui->GetBaseFontSize() : 0.0f),
      static_cast<double>(gui ? gui->GetBakedFontSize() : 0.0f));
    response = LocalRpcReply(requestId, numbers);
    return response.c_str();
  }
  if (type == "editor.saveLayout") {
    response = LocalRpcReply(requestId, editorUI ? "{\"settings\":" + QuoteJson(editorUI->ComposeLayoutSettings()) + "}" : "{\"settings\":\"\"}");
    return response.c_str();
  }
  if (type == "editor.loadLayout") {
    const std::string settings = RequestString(payload["settings"]);
    const bool applied = editorUI && editorUI->ApplyLayoutSettings(settings);
    response = LocalRpcReply(requestId, applied ? "{\"applied\":true}" : "{\"applied\":false}");
    return response.c_str();
  }
  } catch (const std::exception& error) {
    response = "{\"protocol\":\"tomcat.web.v1\",\"requestId\":" + QuoteJson(requestId)
      + ",\"ok\":false,\"error\":{\"code\":\"INVALID_REQUEST\",\"message\":" + QuoteJson(error.what()) + "}}";
    return response.c_str();
  }
  response = session->Invoke(text);
  return response.c_str();
}
const char* tc_web_editor_error() { return lastError.c_str(); }
int tc_web_editor_boot(int width, int height) {
  lastError.clear();
  try {
    if (application || width < 1 || height < 1 || width > 8192 || height > 8192) throw std::runtime_error("Invalid editor viewport or already running");
    EnsureSession();
    application = std::make_unique<TomCat::Application>(TomCat::WindowProps("TomCat Web Editor", width, height), false);
    gui = std::make_unique<TomCat::ImGuiLayer>(true); gui->OnAttach();
    editorUI = new TomCat::WebEditorUI(*session); application->PushLayer(editorUI); return 0;
  } catch (const std::exception& error) {
    lastError = error.what(); if (gui) { gui->OnDetach(); gui.reset(); }
    editorUI = nullptr; application.reset(); return 1;
  }
}
void tc_web_editor_frame(double delta) {
  if (!application) return;
  try {
    // This surface owns gui separately; Application::GetImGuiLayer() is null.
    gui->SetUiScale(application->GetWindow().GetDPIScale());
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
int tc_web_editor_set_ui_scale(float scale) {
  lastError.clear();
  if (!gui) { lastError = "Editor is not running"; return 0; }
  if (application) static_cast<TomCat::WebWindow&>(application->GetWindow()).SetDisplayScale(scale);
  gui->SetUiScale(scale);
  return 1;
}
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
}
#endif
