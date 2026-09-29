#include "tcpch.h"
#ifdef __EMSCRIPTEN__
#include "TomCat/Core/Application.h"
#include "TomCat/Core/Input.h"
#include "TomCat/Asset/AssetManager.h"
#include "TomCat/Asset/AssetJobSystem.h"
#include "TomCat/Project/Project.h"
#include "TomCat/Renderer/RenderCommand.h"
#include "TomCat/Renderer/Renderer2D.h"
#include "TomCat/Scene/SceneManager.h"
#include "TomCat/Scene/Entity.h"
#include "TomCat/Scene/Components.h"
#include "PlayerRuntimeLayer.h"
#include "WebWindow.h"
#include <fstream>
#include <cmath>

namespace {
std::unique_ptr<TomCat::Application> application;
std::string lastError, stats;
uint64_t frames = 0;
void Shutdown() {
  application.reset();
  TomCat::AssetManager::Get().UnmountCookedPackage();
  TomCat::Input::ClearState();
  frames = 0;
}
}
extern "C" {
const char* tc_web_player_error() { return lastError.c_str(); }
void tc_web_player_shutdown() { Shutdown(); }
int tc_web_player_boot(int width, int height, const uint8_t* bytes, size_t size) {
  lastError.clear();
  if (application) { lastError = "Player already running"; return 1; }
  if (!bytes || !size || size > 256 * 1024 * 1024 || width <= 0 || height <= 0 || width > 8192 || height > 8192) {
    lastError = "Invalid package buffer or viewport"; return 2;
  }
  try {
    TomCat::Log::Init();
    std::ofstream file("/Game.tcpak", std::ios::binary | std::ios::trunc);
    file.write(reinterpret_cast<const char*>(bytes), size); file.close();
    if (!file) throw std::runtime_error("Could not stage Game.tcpak in MEMFS");
    auto& assets = TomCat::AssetManager::Get();
    if (!assets.MountCookedPackage("/Game.tcpak")) throw std::runtime_error("TCPAK validation failed");
    assets.UnmountCookedPackage();
    application = std::make_unique<TomCat::Application>(TomCat::WindowProps("TomCat Web Player", width, height), false);
    application->PushLayer(new TomCat::PlayerRuntimeLayer("/Game.tcpak"));
    if (application->GetExitCode() != 0) throw std::runtime_error("PlayerRuntimeLayer could not activate the package entry scene");
    return 0;
  } catch (const std::exception& error) { lastError = error.what(); Shutdown(); return 3; }
}
void tc_web_player_frame(double delta) {
  if (!application) return;
  try {
    TomCat::Renderer2D::ResetStats();
    TomCat::RenderCommand::SetClearColor({0.08f, 0.09f, 0.11f, 1.0f});
    TomCat::RenderCommand::Clear();
    if (!application->Tick(std::clamp(float(delta), 0.0f, 0.1f))) {
      lastError = "Player requested shutdown"; Shutdown(); return;
    }
    ++frames;
  } catch (const std::exception& error) { lastError = error.what(); Shutdown(); }
}
void tc_web_player_resize(int width, int height) {
  if (application && width > 0 && height > 0 && width <= 8192 && height <= 8192)
    static_cast<TomCat::WebWindow&>(application->GetWindow()).Resize(width, height);
}
const char* tc_web_player_stats() {
  const auto render = TomCat::Renderer2D::GetStats();
  stats = "{\"running\":" + std::string(application ? "true" : "false") +
    ",\"frames\":" + std::to_string(frames) + ",\"drawCalls\":" + std::to_string(render.DrawCalls);
  if (auto* manager = TomCat::SceneManager::GetRuntime()) {
    if (auto scene = manager->GetActiveScene()) {
      const auto& physics = scene->GetRuntimePhysicsSyncStatistics();
      stats += ",\"bodies\":" + std::to_string(physics.BodiesCreated - physics.BodiesDestroyed);
      for (auto uuid : scene->GetRootEntityUUIDs()) {
        auto entity = scene->FindEntityByUUID(uuid);
        if (entity && entity.GetName() == "Square")
          stats += ",\"squareY\":" + std::to_string(entity.GetComponent<TomCat::Transform>()._Translation.y);
      }
    }
  }
  stats += "}";
  return stats.c_str();
}
// Generalized content cook: loads an authoring project staged in MEMFS and
// writes a TCPAK package without a GL context. The runtime cook entry for
// server-side workers; the sample below is the same flow with fixed paths.
int tc_web_player_cook(const char* projectPath, const char* outputPath) {
  lastError.clear();
  if (application) { lastError = "Stop Player before cooking"; return 1; }
  if (!projectPath || !*projectPath || !outputPath || !*outputPath) {
    lastError = "Project and output paths are required"; return 3;
  }
  try {
    TomCat::Log::Init();
    auto project = TomCat::Project::Load(projectPath);
    if (!project) throw std::runtime_error("Project could not be loaded");
    if (!TomCat::AssetManager::Get().SetProject(project)) throw std::runtime_error("Assets could not be initialized");
    if (!TomCat::AssetManager::Get().CookToPackage(outputPath)) throw std::runtime_error("Cook failed");
    return 0;
  } catch (const std::exception& error) { lastError = error.what(); return 2; }
}
// Compatibility shim: upstream tooling still cooks the checked-in sample.
int tc_web_player_cook_sample() {
  return tc_web_player_cook("/Samples/PhysicsPlayground/Project.tcproj", "/PhysicsPlayground.tcpak");
}
// C# project support: inject an already-validated managed payload (assembly +
// script manifest + optional portable PDB) before cooking. The cook honors
// this override with priority over desktop last-good discovery; the payload
// must pass the same validation as any desktop build.
int tc_web_player_set_cook_payload(const uint8_t* assembly, size_t assemblySize,
    const char* manifestJson, const char* buildId, const uint8_t* pdb, size_t pdbSize) {
  lastError.clear();
  if (application) { lastError = "Stop Player before cooking"; return 1; }
  if (!assembly || !assemblySize || !manifestJson || !*manifestJson || !buildId || !*buildId) {
    lastError = "Assembly, script manifest and build id are required"; return 3;
  }
  if (assemblySize > 256 * 1024 * 1024 || pdbSize > 256 * 1024 * 1024) {
    lastError = "Managed payload exceeds the size limit"; return 3;
  }
  try {
    TomCat::Log::Init();
    std::vector<uint8_t> assemblyBytes(assembly, assembly + assemblySize);
    std::vector<uint8_t> pdbBytes;
    if (pdb && pdbSize) pdbBytes.assign(pdb, pdb + pdbSize);
    if (!TomCat::AssetManager::Get().SetManagedCookPayload(std::move(assemblyBytes),
        manifestJson, buildId, std::move(pdbBytes)))
      throw std::runtime_error("Managed cook payload was rejected");
    return 0;
  } catch (const std::exception& error) { lastError = error.what(); return 2; }
}
// Drops a previously injected payload so later cooks fall back to discovery.
int tc_web_player_clear_cook_payload() {
  TomCat::AssetManager::Get().ClearManagedCookPayload();
  return 0;
}
}
#endif // __EMSCRIPTEN__
