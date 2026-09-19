#pragma once
#include "EditorPlayToolbar.h"
#include "EditorVisuals.h"
#include <cmath>
namespace TomCat {
		constexpr float kSceneToolbarPadding = 5.0f;
		constexpr float kSceneToolbarHandleWidth = 24.0f;
		constexpr float kSceneToolbarItemGap = 4.0f;
		constexpr float kSceneToolbarDockGap = 4.0f;
		constexpr float kSceneModeButtonWidth = 92.0f;
		constexpr float kSceneTransformButtonWidth = 34.0f;
		constexpr float kSceneModeToolbarWidth = kSceneToolbarPadding * 2.0f +
			kSceneToolbarHandleWidth + kSceneToolbarItemGap +
			kSceneModeButtonWidth * 2.0f + kSceneToolbarItemGap;
		constexpr float kSceneTransformToolbarWidth = kSceneToolbarPadding * 2.0f +
			kSceneToolbarHandleWidth + kSceneToolbarItemGap +
			kSceneTransformButtonWidth * 4.0f + kSceneToolbarItemGap * 3.0f;
inline void DrawSceneToolbarIcon(ImDrawList* draw, const Ref<EditorIconSet>& icons,
  EditorIcon icon, const ImVec2& minimum, const ImVec2& maximum) {
  DrawEditorGlyph(draw,icons,icon,minimum,maximum);
}
}
