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
// The display scale and the workspace reach the host through the existing RPC channel. A
// dedicated export per call would mean a new native import in the managed WebAssembly module,
// and those imports resolve when the module is instantiated: one wrong name takes the whole
// editor down before any diagnostic can run. Routing through the channel the host already uses
// keeps the native surface at one entry point.
bool ReadRequestField(const std::string& request, const char* key, std::string& value) {
  const std::string marker = std::string("\"") + key + "\"";
  const size_t keyAt = request.find(marker);
  if (keyAt == std::string::npos) return false;
  const size_t colon = request.find(':', keyAt + marker.size());
  if (colon == std::string::npos) return false;
  size_t at = colon + 1;
  while (at < request.size() && std::isspace(static_cast<unsigned char>(request[at]))) ++at;
  if (at >= request.size()) return false;
  // Numbers and booleans arrive bare; strings arrive quoted and may contain escapes (the
  // layout blob is a multi line string, so its escapes have to be resolved).
  if (request[at] != '"') {
    size_t end = at;
    while (end < request.size() && request[end] != ',' && request[end] != '}') ++end;
    std::string raw = request.substr(at, end - at);
    while (!raw.empty() && std::isspace(static_cast<unsigned char>(raw.back()))) raw.pop_back();
    if (raw.empty()) return false;
    value = std::move(raw);
    return true;
  }
  std::string result;
  for (size_t index = at + 1; index < request.size(); ++index) {
    const char current = request[index];
    if (current == '\\' && index + 1 < request.size()) {
      const char escape = request[++index];
      switch (escape) {
        case 'n': result.push_back('\n'); break;
        case 't': result.push_back('\t'); break;
        case 'r': result.push_back('\r'); break;
        case 'u': result.append("\\u"); break;   // 布局 blob 只有 ASCII，无需完整解转义
        default: result.push_back(escape); break;
      }
      continue;
    }
    if (current == '"') { value = std::move(result); return true; }
    result.push_back(current);
  }
  return false;
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
std::string LocalRpcReply(const std::string& request, const std::string& payload) {
  std::string requestId;
  ReadRequestField(request, "requestId", requestId);
  return "{\"protocol\":\"tomcat.web.v1\",\"requestId\":" + QuoteJson(requestId)
    + ",\"ok\":true,\"result\":" + payload + "}";
}
const char* tc_web_editor_rpc(const char* request) {
  EnsureSession();
  const std::string text = request ? request : "";
  // These need the ImGui layer and the panel state, which belong to this browser surface.
  if (text.find("\"editor.setDisplayScale\"") != std::string::npos) {
    std::string scale;
    const bool parsed = ReadRequestField(text, "scale", scale);
    const float value = parsed ? std::strtof(scale.c_str(), nullptr) : 1.0f;
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
    response = LocalRpcReply(text, numbers);
    return response.c_str();
  }
  if (text.find("\"editor.saveLayout\"") != std::string::npos) {
    response = LocalRpcReply(text, editorUI ? "{\"settings\":" + QuoteJson(editorUI->ComposeLayoutSettings()) + "}" : "{\"settings\":\"\"}");
    return response.c_str();
  }
  if (text.find("\"editor.loadLayout\"") != std::string::npos) {
    std::string settings;
    const bool parsed = ReadRequestField(text, "settings", settings);
    const bool applied = parsed && editorUI && editorUI->ApplyLayoutSettings(settings);
    response = LocalRpcReply(text, applied ? "{\"applied\":true}" : "{\"applied\":false}");
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
int tc_web_editor_set_ui_scale(float scale) {
  lastError.clear();
  if (!gui) { lastError = "Editor is not running"; return 0; }
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
