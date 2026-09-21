#include "tcpch.h"
#ifdef __EMSCRIPTEN__
#include "WebEditorUI.h"
#include "SceneToolbarDrawing.h"
#include <ImGuizmo.h>
#include <emscripten.h>
namespace TomCat {
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
