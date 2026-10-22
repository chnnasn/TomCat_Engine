#pragma once
#include "EditorIcons.h"
#include "EditorVisuals.h"
#ifdef __EMSCRIPTEN__
#include <imgui.h>
#else
#include <imgui/imgui.h>
#endif
#include <algorithm>
namespace TomCat {
inline ImTextureID EditorTextureID(const Ref<Texture2D>& texture) {
  return texture ? reinterpret_cast<ImTextureID>(static_cast<uintptr_t>(texture->GetRendererID())) : nullptr;
}
// Shared desktop/Web Play controls. Keep dimensions, icons and behavior identical.
template<class Play, class Stop, class Pause, class Step>
void DrawEditorPlayToolbar(const Ref<EditorIconSet>& icons, bool canPlay, bool running, bool paused,
  Play onPlay, Stop onStop, Pause onPause, Step onStep) {

		const float buttonSize = std::round(ImGui::GetFontSize()*1.35f);
		const float spacing = ImGui::GetStyle().ItemSpacing.x;
		const float groupWidth = buttonSize * 3.0f + spacing * 2.0f;
		ImGui::SetCursorPosX(std::max(0.0f, (ImGui::GetWindowWidth() - groupWidth) * 0.5f));
		ImGui::SetCursorPosY(std::max(ImGui::GetCursorPosY(),
			(ImGui::GetWindowHeight() - buttonSize) * 0.5f));

		auto drawButton = [&](const char* id, EditorIcon icon, bool enabled,
			bool selected, const char* tooltip)
		{
			ImGui::PushID(id);
			const ImVec4 buttonColor = selected
				? ImGui::GetStyleColorVec4(ImGuiCol_HeaderActive)
				: ImGui::GetStyleColorVec4(ImGuiCol_Button);
			ImGui::PushStyleColor(ImGuiCol_Button, buttonColor);
			if (!enabled)
				ImGui::BeginDisabled();

            const bool pressed=EditorGlyphButton("##Control",icons,icon,tooltip,selected,ImVec2(buttonSize,buttonSize));

			if (!enabled)
				ImGui::EndDisabled();
			ImGui::PopStyleColor();
			ImGui::PopID();
			return pressed && enabled;
		};

		if (drawButton("PlayStop", running ? EditorIcon::Stop : EditorIcon::Play,
			canPlay,
			running, running ? "Stop" : "Play"))
		{
			if (running)
				onStop();
			else
				onPlay();
		}
		ImGui::SameLine();
		if (drawButton("Pause", EditorIcon::Pause, running,
			paused, paused ? "Resume" : "Pause"))
			onPause();
		ImGui::SameLine();
		if (drawButton("Step", EditorIcon::Step, paused,
			false, "Step one fixed physics frame (1/60 s)"))
			onStep();

}
}
