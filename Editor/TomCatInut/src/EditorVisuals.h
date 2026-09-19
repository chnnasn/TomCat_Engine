#pragma once
#include "EditorIcons.h"
#ifdef __EMSCRIPTEN__
#include <imgui.h>
#else
#include <imgui/imgui.h>
#endif
#include <algorithm>
#include <cmath>

namespace TomCat {
inline std::weak_ptr<EditorIconSet> g_EditorVisualIcons;
// All editor panels share the same non-collapsible window contract. NoCollapse
// also expands windows restored from an older layout's collapsed state.
inline bool BeginEditorWindow(const char* name, bool* open = nullptr, ImGuiWindowFlags flags = 0)
{
    return ImGui::Begin(name, open, flags | ImGuiWindowFlags_NoCollapse);
}
// Use the project's packaged editor artwork. Only search fields without an icon
// set use the small procedural magnifier below.
inline void DrawEditorGlyph(ImDrawList* draw, const Ref<EditorIconSet>& icons,
    EditorIcon icon, ImVec2 minimum, ImVec2 maximum, ImU32 tint = IM_COL32_WHITE)
{
    if (!draw) return;
    const auto artwork=icons ? icons : g_EditorVisualIcons.lock();
    if (artwork && artwork->Get(icon))
    {
        draw->AddImage(reinterpret_cast<ImTextureID>(static_cast<uintptr_t>(artwork->Get(icon)->GetRendererID())),
            minimum,maximum,ImVec2(0,1),ImVec2(1,0),tint);
        return;
    }
    if (icon == EditorIcon::Search)
    {
        const float size=std::min(maximum.x-minimum.x,maximum.y-minimum.y);
        const float unit=size/16.0f;
        const ImVec2 center(minimum.x+6.5f*unit,minimum.y+6.5f*unit);
        draw->AddCircle(center,4.5f*unit,tint,16,std::max(1.0f,unit));
        draw->AddLine(ImVec2(minimum.x+10*unit,minimum.y+10*unit),
            ImVec2(minimum.x+14*unit,minimum.y+14*unit),tint,std::max(1.0f,unit));
    }
}

inline bool EditorGlyphButton(const char* id, const Ref<EditorIconSet>& icons, EditorIcon icon,
    const char* tooltip, bool selected=false, ImVec2 size=ImVec2(0,0))
{
    if(size.x<=0) size.x=ImGui::GetFrameHeight();
    if(size.y<=0) size.y=ImGui::GetFrameHeight();
    const bool pressed=ImGui::Button(id,size);
    const auto min=ImGui::GetItemRectMin(), max=ImGui::GetItemRectMax();
    if(selected) ImGui::GetWindowDrawList()->AddRectFilled(min,max,ImGui::GetColorU32(ImGuiCol_HeaderActive),ImGui::GetStyle().FrameRounding);
    const float extent=std::min(size.x,size.y)*0.62f;
    const ImVec2 start(std::round((min.x+max.x-extent)*0.5f),std::round((min.y+max.y-extent)*0.5f));
    DrawEditorGlyph(ImGui::GetWindowDrawList(),icons,icon,start,ImVec2(start.x+extent,start.y+extent),ImGui::GetColorU32(ImVec4(1,1,1,1)));
    if(ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) ImGui::SetTooltip("%s",tooltip);
    return pressed;
}

inline bool EditorIconMenuItem(const char* label, EditorIcon icon)
{
    if(icon==EditorIcon::Count) return ImGui::MenuItem(label);
    const std::string caption=std::string("    ")+label;
    const bool pressed=ImGui::MenuItem(caption.c_str());
    const auto a=ImGui::GetItemRectMin(),b=ImGui::GetItemRectMax();
    const float size=ImGui::GetFontSize()*0.85f;
    const ImVec2 start(a.x+ImGui::GetStyle().FramePadding.x,a.y+(b.y-a.y-size)*0.5f);
    DrawEditorGlyph(ImGui::GetWindowDrawList(),{},icon,start,ImVec2(start.x+size,start.y+size));
    return pressed;
}

// Floating tools open in a readable size, centered and bounded by the workspace.
// Saved dock positions/sizes still win; this only supplies first-use defaults.
inline void PrepareEditorToolWindow(ImVec2 preferred, ImVec2 minimum=ImVec2(320,240))
{
    const float scale=std::max(1.0f,ImGui::GetFontSize()/24.0f);
    preferred=ImVec2(preferred.x*scale,preferred.y*scale);
    const auto* viewport=ImGui::GetMainViewport();
    const ImVec2 limit(std::max(1.0f,viewport->WorkSize.x-32),std::max(1.0f,viewport->WorkSize.y-32));
    minimum=ImVec2(std::min(minimum.x*scale,limit.x),std::min(minimum.y*scale,limit.y));
    ImGui::SetNextWindowSize(ImVec2(std::clamp(preferred.x,minimum.x,limit.x),std::clamp(preferred.y,minimum.y,limit.y)),ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowPos(viewport->GetCenter(),ImGuiCond_FirstUseEver,ImVec2(0.5f,0.5f));
    ImGui::SetNextWindowSizeConstraints(minimum,limit);
}
inline bool EditorSearchField(const char* id,const char* hint,char* text,size_t capacity)
{
    const auto padding=ImGui::GetStyle().FramePadding;
    const float iconSize=ImGui::GetFontSize()*0.78f;
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding,ImVec2(padding.x+iconSize+5,padding.y));
    const bool changed=ImGui::InputTextWithHint(id,hint,text,capacity);
    ImGui::PopStyleVar();
    const auto min=ImGui::GetItemRectMin(),max=ImGui::GetItemRectMax();
    const ImVec2 start(min.x+padding.x,min.y+(max.y-min.y-iconSize)*0.5f);
    DrawEditorGlyph(ImGui::GetWindowDrawList(),{},EditorIcon::Search,start,ImVec2(start.x+iconSize,start.y+iconSize),ImGui::GetColorU32(ImGuiCol_TextDisabled));
    return changed;
}

inline void PrepareEditorPopup(const char* id,float width,bool centered=false)
{
    if(!ImGui::IsPopupOpen(id)) return;
    const auto* viewport=ImGui::GetMainViewport();
    const float maximumWidth=std::max(1.0f,viewport->WorkSize.x-32);
    const float maximumHeight=std::max(1.0f,viewport->WorkSize.y-48);
    width=std::min(width*std::max(1.0f,ImGui::GetFontSize()/24.0f),maximumWidth);
    ImGui::SetNextWindowSize(ImVec2(width,0),ImGuiCond_Appearing);
    ImGui::SetNextWindowSizeConstraints(ImVec2(width,0),ImVec2(maximumWidth,maximumHeight));
    if(centered) ImGui::SetNextWindowPos(viewport->GetCenter(),ImGuiCond_Appearing,ImVec2(0.5f,0.5f));
}

}
