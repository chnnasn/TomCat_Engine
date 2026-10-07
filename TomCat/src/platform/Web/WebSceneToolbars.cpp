#include "tcpch.h"
#ifdef __EMSCRIPTEN__
#include "WebEditorUI.h"
#include "SceneToolbarDrawing.h"
#include <ImGuizmo.h>
#include <emscripten.h>
#include <cstdint>
#include <cstdlib>
#include <charconv>
#include <string>
namespace TomCat {
void WebEditorUI::UI_SceneOrientationGizmo() {
  auto& m_EditorCamera = m_Camera;
#include "panels/UI_SceneOrientationGizmo.inl"
}

bool WebEditorUI::IsSceneOrientationGizmoPointerInside() const {
  if (m_Is2DMode) return false;
  const auto mouse = ImGui::GetMousePos();
  return mouse.x >= m_SceneOrientationGizmoBounds[0].x && mouse.x < m_SceneOrientationGizmoBounds[1].x
    && mouse.y >= m_SceneOrientationGizmoBounds[0].y && mouse.y < m_SceneOrientationGizmoBounds[1].y;
}
namespace {
	// Persisted workspace format. The managed sections carry the dock tree and window
	// geometry; the engine section carries state ImGui does not know about. A version
	// marker lets a future layout change discard data instead of misplacing panels.
	constexpr int kLayoutSchemaVersion = 1;
	constexpr const char* kPanelSection = "[TomCatWebPanelLayout][v1]";
	constexpr size_t kMaxLayoutBytes = 512 * 1024;
}

	void WebEditorUI::UI_SceneToolbarDragHandle(const char* id, glm::vec2& offset, bool& docked, bool& dragging,
		const ImVec2& handleMin, const ImVec2& handleMax, float tearX, bool canDock)
	{
#include "panels/UI_SceneToolbarDragHandle.inl"
	}
	void WebEditorUI::UI_SceneGizmoToolbar()
	{
#include "panels/UI_SceneGizmoToolbar.inl"
	}
	void WebEditorUI::UI_SceneGizmoModeToolbarOverlay()
	{
#include "panels/UI_SceneGizmoModeToolbarOverlay.inl"
	}
	void WebEditorUI::UI_SceneToolbarDockPreview()
	{
#include "panels/UI_SceneToolbarDockPreview.inl"
	}


uint32_t WebEditorUI::GetPanelVisibilityMask() const {
  return (m_ShowScene ? 1u : 0u) | (m_ShowGame ? 2u : 0u) | (m_ShowHierarchy ? 4u : 0u)
    | (m_ShowInspector ? 8u : 0u) | (m_ShowProject ? 16u : 0u) | (m_ShowConsole ? 32u : 0u)
    | (m_ShowAnimation ? 64u : 0u) | (m_ShowAnimator ? 128u : 0u) | (m_ShowTilePalette ? 256u : 0u)
    | (m_ShowProfiler ? 512u : 0u) | (m_ShowAssetInspector ? 1024u : 0u);
}

std::string WebEditorUI::ComposeLayoutSettings() const {
  std::string blob = std::to_string(kLayoutSchemaVersion);
  blob += '\n';
  blob += kPanelSection;
  blob += '\n';
  blob += std::to_string(GetPanelVisibilityMask());
  blob += '\n';
  size_t size = 0;
  if (const char* imgui = ImGui::SaveIniSettingsToMemory(&size))
    blob.append(imgui, size);
  blob += "\n[ContentBrowser]\nLayout=";
  blob += m_Content.GetLayoutMode() == ContentBrowserPanel::OneColumn ? "OneColumn\n" : "TwoColumn\n";
  return blob;
}

bool WebEditorUI::ApplyLayoutSettings(const std::string& settings) {
  if (settings.empty() || settings.size() > kMaxLayoutBytes)
    return false;
  const size_t versionEnd = settings.find('\n');
  if (versionEnd == std::string::npos || settings.substr(0, versionEnd) != std::to_string(kLayoutSchemaVersion))
    return false;
  const size_t sectionEnd = settings.find('\n', versionEnd + 1);
  if (sectionEnd == std::string::npos || settings.compare(versionEnd + 1, sectionEnd - versionEnd - 1, kPanelSection) != 0)
    return false;
  const size_t maskEnd = settings.find('\n', sectionEnd + 1);
  if (maskEnd == std::string::npos)
    return false;
  uint32_t mask = 0;
  const char* maskStart = settings.data() + sectionEnd + 1;
  const char* maskLimit = settings.data() + maskEnd;
  const auto parsedMask = std::from_chars(maskStart, maskLimit, mask);
  if (parsedMask.ec != std::errc{} || parsedMask.ptr != maskLimit || (mask & ~2047u))
    return false;

  // A blob that carries only the engine section would make ImGui drop the default dock
  // tree, so the managed sections are required before anything is applied.
  const std::string managed = settings.substr(maskEnd + 1);
  if (managed.find("[Window][") == std::string::npos
    && managed.find("[Table][") == std::string::npos
    && managed.find("[Docking][") == std::string::npos)
    return false;
  auto contentLayout = ContentBrowserPanel::TwoColumn;
  const auto contentSection = managed.find("[ContentBrowser]\nLayout=");
  if (contentSection != std::string::npos) {
    const auto start = contentSection + std::string("[ContentBrowser]\nLayout=").size();
    const auto value = managed.substr(start, managed.find('\n', start) - start);
    if (value == "OneColumn") contentLayout = ContentBrowserPanel::OneColumn;
    else if (value != "TwoColumn") return false;
  }
  // SetProject loads desktop defaults; do it before applying browser preferences.
  SyncContext();
  ImGui::LoadIniSettingsFromMemory(managed.data(), managed.size());
  m_Content.SetLayoutMode(contentLayout);

  m_ShowScene = (mask & 1u) != 0;
  m_ShowGame = (mask & 2u) != 0;
  m_ShowHierarchy = (mask & 4u) != 0;
  m_ShowInspector = (mask & 8u) != 0;
  m_ShowProject = (mask & 16u) != 0;
  m_ShowConsole = (mask & 32u) != 0;
  m_ShowAnimation = (mask & 64u) != 0;
  m_ShowAnimator = (mask & 128u) != 0;
  m_ShowTilePalette = (mask & 256u) != 0;
  m_ShowProfiler = (mask & 512u) != 0;
  m_ShowAssetInspector = (mask & 1024u) != 0;
  return true;
}


void WebEditorUI::SaveSceneToolbarLayout() {
  // Editor UI preference only; independent of project contents and scene history.
  const std::array<float,7> values{float(m_GizmoModeToolbarDocked),float(m_GizmoTransformToolbarDocked),float(m_GizmoModeToolbarFirst),m_GizmoModeToolbarOffset.x,m_GizmoModeToolbarOffset.y,m_GizmoToolbarOffset.x,m_GizmoToolbarOffset.y};
  EM_ASM({ try { localStorage.setItem('tomcat.scene-toolbars.v1',JSON.stringify(Array.from(HEAPF32.subarray($0>>2,($0>>2)+7)))); } catch (_) {} },values.data());
}
void WebEditorUI::LoadSceneToolbarLayout() {
  std::array<float,7> values{};
  const bool loaded=EM_ASM_INT({ try {
    const v=JSON.parse(localStorage.getItem('tomcat.scene-toolbars.v1'));
    if(!Array.isArray(v)||v.length!==7||!v.every(Number.isFinite)||!v.slice(0,3).every(n=>n===0||n===1)) return 0;
    HEAPF32.set(v,$0>>2); return 1;
  } catch (_) { return 0; } },values.data());
  if(loaded) {
    m_GizmoModeToolbarDocked=values[0]!=0; m_GizmoTransformToolbarDocked=values[1]!=0; m_GizmoModeToolbarFirst=values[2]!=0;
    m_GizmoModeToolbarOffset={values[3],values[4]}; m_GizmoToolbarOffset={values[5],values[6]};
  }
}
}
#endif
