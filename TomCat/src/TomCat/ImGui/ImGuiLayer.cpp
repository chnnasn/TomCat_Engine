#include "tcpch.h"
#include "ImGuiLayer.h"
#include "ImGuiFonts.h"

#include "imgui.h"
#include "backends/imgui_impl_glfw.h"
#include "backends/imgui_impl_opengl3.h"
#include "TomCat/Renderer/RendererAPI.h"
#ifndef TC_PLATFORM_WEB
#include "platform/Vulkan/VulkanContext.h"
#include "backends/imgui_impl_vulkan.h"
#endif

#include "TomCat/Core/Application.h"

#include <GLFW/glfw3.h>
#ifndef __EMSCRIPTEN__
#include <glad/glad.h>
#endif
#include <filesystem>
#include <algorithm>
#include <cmath>

#include"ImGuizmo.h"

namespace {
	// Nominal UI font size in interface units. The baked atlas size is these values
	// multiplied by the display scale, and ImGuiStyle is scaled the same way, so interface
	// units stay device independent.
	constexpr float kEditorBaseFontSize = 16.0f;
	constexpr float kRuntimeBaseFontSize = 32.0f;
	constexpr float kMinUiScale = 0.5f;
	constexpr float kMaxUiScale = 2.5f;
}

namespace TomCat {

	ImGuiLayer::ImGuiLayer(bool editorStyling)
        : Layer("ImGuiLayer"), m_EditorStyling(editorStyling)
	{
	}

	ImGuiLayer::~ImGuiLayer()
	{
	}

	void ImGuiLayer::OnAttach()
	{
		// Setup Dear ImGui context
		IMGUI_CHECKVERSION();
		ImGui::CreateContext();
		ImGuiIO& io = ImGui::GetIO(); (void)io;
		io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;       // Enable Keyboard Controls
		//io.ConfigFlags |= ImGuiConfigFlags_NavEnableGamepad;      // Enable Gamepad Controls
		io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;           // Enable Docking
		// Multi-viewport stays disabled until engine input is routed for every GLFW platform window.
#ifndef TC_PLATFORM_WEB
        // Opt-in renderer/platform integration testing. Engine scene input still
        // belongs to the main window, so this is not an editor default.
        if (const char* value = std::getenv("TC_IMGUI_VIEWPORTS"); value && std::string_view(value) == "1")
            io.ConfigFlags |= ImGuiConfigFlags_ViewportsEnable;
#endif
		// Disable ImGui's narrow-path automatic persistence. The Editor explicitly
		// stores its selected global/project layout; the Hub keeps its packaged default
		// layout read-only and stores non-layout state separately in LocalAppData JSON.
		io.IniFilename = NULL;
		//io.ConfigFlags |= ImGuiConfigFlags_ViewportsNoTaskBarIcons;
		//io.ConfigFlags |= ImGuiConfigFlags_ViewportsNoMerge;

		// Fonts are baked for the display scale, so the atlas and the style are built together
		// in one place instead of picking a font size here.
		m_DpiScale = Application::Get().GetWindow().GetDPIScale();
		RebuildFonts();

// Setup Dear ImGui style
		ImGui::StyleColorsDark();
		//ImGui::StyleColorsClassic();

		// When viewports are enabled we tweak WindowRounding/WindowBg so platform windows can look identical to regular ones.
		ImGuiStyle& style = ImGui::GetStyle();
		if (io.ConfigFlags & ImGuiConfigFlags_ViewportsEnable)
		{
			style.WindowRounding = 0.0f;
			style.Colors[ImGuiCol_WindowBg].w = 1.0f;
		}

		SetDarkThemeColors();

		// Remember the authored metrics and derive every later scale from them, so a rebuild
		// never accumulates style scaling.
		m_BaseStyle = std::make_unique<ImGuiStyle>(style);
		m_BaseStyleCaptured = true;
		PrepareImGuiStyle();

		Application& app = Application::Get();
		GLFWwindow* window = static_cast<GLFWwindow*>(app.GetWindow().GetNativeWindow());

		// Setup Platform/Renderer bindings
#ifndef TC_PLATFORM_WEB
        if (RendererAPI::GetAPI() == RendererAPI::API::Vulkan)
            ImGui_ImplGlfw_InitForVulkan(window, true);
        else
#endif
            ImGui_ImplGlfw_InitForOpenGL(window, true);
#ifndef __EMSCRIPTEN__
		// Windows can deliver WM_MOUSEWHEEL before a pending WM_MOUSEMOVE.
		// Queue the current cursor position before the wheel so ImGui routes the
		// first scroll to the panel under the pointer, not the previously hovered tab.
		// Both backend callbacks retain their original engine callback chains.
		glfwSetScrollCallback(window, [](GLFWwindow* source, double x, double y)
		{
			double cursorX = 0.0, cursorY = 0.0;
			glfwGetCursorPos(source, &cursorX, &cursorY);
			ImGui_ImplGlfw_CursorPosCallback(source, cursorX, cursorY);
			ImGui_ImplGlfw_ScrollCallback(source, x, y);
		});
#endif
#ifdef __EMSCRIPTEN__
		// Fonts use CSS pixel sizes so native panel metrics stay consistent.
		ImGui_ImplOpenGL3_Init("#version 300 es");
#else
        if (RendererAPI::GetAPI() == RendererAPI::API::Vulkan)
            VulkanContext::Current().InitImGui();
        else
            ImGui_ImplOpenGL3_Init("#version 410");
#endif
	}


	void ImGuiLayer::RebuildFonts()
	{
		ImGuiIO& io = ImGui::GetIO();
		// Glyphs are rasterised at the size they occupy in device pixels: the interface keeps
		// its authored metrics at any scale, and text stays crisp on high DPI displays instead
		// of magnifying a low resolution atlas.
		m_BaseFontSize = m_EditorStyling ? kEditorBaseFontSize : kRuntimeBaseFontSize;
		m_BakedFontSize = std::round(m_BaseFontSize * EffectiveScale());
		const float fontSize = m_BakedFontSize;
		// Dropping the atlas invalidates glyph pointers that visible windows still hold, so
		// SetUiScale only schedules this call for a frame boundary.
		io.FontDefault = BuildImGuiFonts(*io.Fonts, fontSize, m_EditorStyling);
	}

	void ImGuiLayer::OnDetach()
	{
		TC_PROFILE_FUNCTION();

#ifndef TC_PLATFORM_WEB
        if (RendererAPI::GetAPI() == RendererAPI::API::Vulkan)
            VulkanContext::Current().ShutdownImGui();
        else
#endif
            ImGui_ImplOpenGL3_Shutdown();
		ImGui_ImplGlfw_Shutdown();
		ImGui::DestroyContext();
	}

	void ImGuiLayer::OnEvent(Event& e)
	{
		ImGuiIO& io = ImGui::GetIO();
		e.m_Handled |= m_BlockMouseEvents
			&& e.IsInCategory(EventCategoryMouse) && io.WantCaptureMouse;
		e.m_Handled |= m_BlockKeyboardEvents
			&& e.IsInCategory(EventCategoryKeyboard) && io.WantCaptureKeyboard;

	}


	void ImGuiLayer::Begin()
	{
		TC_PROFILE_FUNCTION();

#ifndef TC_PLATFORM_WEB
        if (RendererAPI::GetAPI() == RendererAPI::API::Vulkan)
            ImGui_ImplVulkan_NewFrame();
        else
#endif
            ImGui_ImplOpenGL3_NewFrame();
		// A scale requested while the interface was on screen waits for this boundary: the
		// atlas can only be replaced once no frame holds pointers into the old glyphs.
		if (m_FontRebuildPending)
		{
			m_FontRebuildPending = false;
			PrepareImGuiStyle();
			RebuildFonts();
#ifndef TC_PLATFORM_WEB
            if (RendererAPI::GetAPI() == RendererAPI::API::Vulkan)
                VulkanContext::Current().RebuildImGuiFonts();
            else
#endif
            {
				ImGui_ImplOpenGL3_DestroyFontsTexture();
				ImGui_ImplOpenGL3_CreateFontsTexture();
            }
		}
		ImGui_ImplGlfw_NewFrame();
		ImGui::NewFrame();
		ImGuizmo::BeginFrame();
	}


	void ImGuiLayer::End()
	{
		TC_PROFILE_FUNCTION();

		ImGuiIO& io = ImGui::GetIO();
		Application& app = Application::Get();
		io.DisplaySize = ImVec2((float)app.GetWindow().GetWidth(), (float)app.GetWindow().GetHeight());

		// Rendering
		ImGui::Render();
#ifndef TC_PLATFORM_WEB
        if (RendererAPI::GetAPI() == RendererAPI::API::Vulkan)
            VulkanContext::Current().RenderImGui(ImGui::GetDrawData());
        else
#endif
            ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());

		if (io.ConfigFlags & ImGuiConfigFlags_ViewportsEnable)
		{
			GLFWwindow* backup_current_context = glfwGetCurrentContext();
			ImGui::UpdatePlatformWindows();
			ImGui::RenderPlatformWindowsDefault();
			if (RendererAPI::GetAPI() == RendererAPI::API::OpenGL)
				glfwMakeContextCurrent(backup_current_context);
		}
	}

	void ImGuiLayer::SetDarkThemeColors()
	{
		ImGuiStyle& style = ImGui::GetStyle();
		ImVec4* colors = style.Colors;

		// Unity's dark editor uses a small, neutral grayscale ramp.  Keeping the
		// ramp in this shared implementation keeps the editor and Hub consistent.
		// These values are sampled/rounded from the Unity
		// reference (panel #383838, toolbar #282828, menu #191919, selection
		// #2C5D87) instead of relying on ImGui's much darker default theme.
		const auto Rgb = [](int r, int g, int b, int a = 255)
		{
			return ImVec4(r / 255.0f, g / 255.0f, b / 255.0f, a / 255.0f);
		};

		const ImVec4 text = Rgb(196, 196, 196);
		const ImVec4 textBright = Rgb(243, 243, 243);
		const ImVec4 textDisabled = Rgb(137, 137, 137);
		const ImVec4 panel = Rgb(56, 56, 56);       // #383838
		const ImVec4 panelAlt = Rgb(60, 60, 60);    // #3C3C3C
		const ImVec4 toolbar = Rgb(40, 40, 40);     // #282828
		const ImVec4 menu = Rgb(25, 25, 25);        // #191919
		const ImVec4 frame = Rgb(71, 71, 71);       // #474747
		const ImVec4 frameHover = Rgb(98, 98, 98);  // #626262
		const ImVec4 frameActive = Rgb(112, 112, 112);
		const ImVec4 selection = Rgb(44, 93, 135);  // #2C5D87
		const ImVec4 selectionHover = Rgb(58, 112, 157);
		const ImVec4 border = Rgb(25, 25, 25);
		const ImVec4 borderLight = Rgb(85, 85, 85);

		colors[ImGuiCol_Text] = text;
		colors[ImGuiCol_TextDisabled] = textDisabled;
		colors[ImGuiCol_WindowBg] = panel;
		colors[ImGuiCol_ChildBg] = panel;
		colors[ImGuiCol_PopupBg] = panelAlt;
		colors[ImGuiCol_Border] = border;
		colors[ImGuiCol_BorderShadow] = Rgb(0, 0, 0, 80);

		// Framed controls (fields, combo boxes, checkboxes and sliders).
		colors[ImGuiCol_FrameBg] = frame;
		colors[ImGuiCol_FrameBgHovered] = frameHover;
		colors[ImGuiCol_FrameBgActive] = frameActive;

		// Window title bars keep the Unity toolbar tone.  MenuBars use the darker
		// #282828 foundation from the reference strip; individual editor toolbars
		// can paint their lighter #3C3C3C surface over this base.
		colors[ImGuiCol_TitleBg] = toolbar;
		colors[ImGuiCol_TitleBgActive] = toolbar;
		colors[ImGuiCol_TitleBgCollapsed] = menu;
		colors[ImGuiCol_MenuBarBg] = toolbar;

		colors[ImGuiCol_ScrollbarBg] = menu;
		colors[ImGuiCol_ScrollbarGrab] = frame;
		colors[ImGuiCol_ScrollbarGrabHovered] = frameHover;
		colors[ImGuiCol_ScrollbarGrabActive] = frameActive;
		colors[ImGuiCol_CheckMark] = selectionHover;
		colors[ImGuiCol_SliderGrab] = borderLight;
		colors[ImGuiCol_SliderGrabActive] = textBright;

		colors[ImGuiCol_Button] = frame;
		colors[ImGuiCol_ButtonHovered] = frameHover;
		colors[ImGuiCol_ButtonActive] = frameActive;

		// Headers drive tree rows, selectable items and menu items.  The blue
		// active state is the same blue used by Unity's hierarchy selection.
		colors[ImGuiCol_Header] = toolbar;
		colors[ImGuiCol_HeaderHovered] = frameHover;
		colors[ImGuiCol_HeaderActive] = selection;

		colors[ImGuiCol_Separator] = borderLight;
		colors[ImGuiCol_SeparatorHovered] = selectionHover;
		colors[ImGuiCol_SeparatorActive] = selection;
		colors[ImGuiCol_ResizeGrip] = borderLight;
		colors[ImGuiCol_ResizeGripHovered] = selectionHover;
		colors[ImGuiCol_ResizeGripActive] = selection;

		// All tab surfaces share the #3C3C3C reference color.  Focus is shown by
		// the blue indicator rendered in TabItemEx rather than by recoloring the
		// entire selected tab.
		colors[ImGuiCol_Tab] = panelAlt;
		// Hovering a tab keeps the same base surface; focus is communicated by
		// the blue top indicator, so the tab never flashes to a different grey.
		colors[ImGuiCol_TabHovered] = panelAlt;
		colors[ImGuiCol_TabActive] = panelAlt;
		colors[ImGuiCol_TabUnfocused] = panelAlt;
		colors[ImGuiCol_TabUnfocusedActive] = panelAlt;
		colors[ImGuiCol_DockingPreview] = Rgb(44, 93, 135, 150);
		colors[ImGuiCol_DockingEmptyBg] = Rgb(48, 48, 48);

		colors[ImGuiCol_PlotLines] = selectionHover;
		colors[ImGuiCol_PlotLinesHovered] = textBright;
		colors[ImGuiCol_PlotHistogram] = selection;
		colors[ImGuiCol_PlotHistogramHovered] = selectionHover;
		colors[ImGuiCol_TableHeaderBg] = toolbar;
		colors[ImGuiCol_TableBorderStrong] = borderLight;
		colors[ImGuiCol_TableBorderLight] = border;
		colors[ImGuiCol_TableRowBg] = panel;
		colors[ImGuiCol_TableRowBgAlt] = panelAlt;
		colors[ImGuiCol_TextSelectedBg] = Rgb(44, 93, 135, 115);
		colors[ImGuiCol_DragDropTarget] = Rgb(80, 165, 235, 220);
		colors[ImGuiCol_NavHighlight] = selectionHover;
		colors[ImGuiCol_NavWindowingHighlight] = textBright;
		colors[ImGuiCol_NavWindowingDimBg] = Rgb(0, 0, 0, 80);
		colors[ImGuiCol_ModalWindowDimBg] = Rgb(0, 0, 0, 110);

		// Unity controls are compact and nearly rectangular.  In particular,
		// removing ImGui's default 7px rounding keeps dock tabs, fields and the
		// Scene toolbar visually aligned with the reference editor.
		style.DisabledAlpha = 0.55f;
		style.WindowPadding = ImVec2(6.0f, 6.0f);
		style.WindowRounding = 0.0f;
		style.WindowBorderSize = 1.0f;
		style.ChildRounding = 0.0f;
		style.ChildBorderSize = 1.0f;
		style.PopupRounding = 2.0f;
		style.PopupBorderSize = 1.0f;
		style.FramePadding = ImVec2(5.0f, 3.0f);
		style.FrameRounding = 2.0f;
		style.FrameBorderSize = 0.0f;
		style.ItemSpacing = ImVec2(4.0f, 3.0f);
		style.ItemInnerSpacing = ImVec2(4.0f, 3.0f);
		style.CellPadding = ImVec2(4.0f, 3.0f);
		style.IndentSpacing = 16.0f;
		style.ScrollbarSize = 14.0f;
		style.ScrollbarRounding = 0.0f;
		style.GrabMinSize = 10.0f;
		style.GrabRounding = 2.0f;
		style.TabRounding = 2.0f;
		style.TabBorderSize = 1.0f;
        if(m_EditorStyling)
        {
            colors[ImGuiCol_FrameBg]=Rgb(43,43,43);
            colors[ImGuiCol_FrameBgHovered]=Rgb(68,68,68);
            colors[ImGuiCol_FrameBgActive]=Rgb(77,77,77);
            colors[ImGuiCol_Button]=Rgb(80,80,80);
            colors[ImGuiCol_ButtonHovered]=Rgb(95,95,95);
            colors[ImGuiCol_ButtonActive]=selection;
            colors[ImGuiCol_ScrollbarBg]=panel;
            colors[ImGuiCol_ScrollbarGrab]=Rgb(91,91,91);
            colors[ImGuiCol_Tab]=toolbar;
            style.WindowMenuButtonPosition=ImGuiDir_None;
            style.WindowPadding=ImVec2(8,6);
            style.WindowRounding=3;
            style.PopupRounding=4;
            style.FramePadding=ImVec2(6,2);
            style.FrameRounding=2;
            style.FrameBorderSize=1;
            style.ItemSpacing=ImVec2(6,4);
            style.ScrollbarSize=12;
            style.ScrollbarRounding=5;
            style.TabBorderSize=0;
#ifndef __EMSCRIPTEN__
            style.ScaleAllSizes(std::clamp(Application::Get().GetWindow().GetDPIScale(),1.0f,2.5f));
#endif
        }

	}

	float ImGuiLayer::EffectiveScale() const
	{
		return std::clamp(m_DpiScale * m_UserScale, kMinUiScale, kMaxUiScale);
	}

	void ImGuiLayer::PrepareImGuiStyle()
	{
		if (!m_BaseStyleCaptured)
			return;

		ImGuiStyle& style = ImGui::GetStyle();
		style = *m_BaseStyle;
		float coordinateScale = EffectiveScale();
#ifdef __EMSCRIPTEN__
		// GLFW/ImGui coordinates are CSS pixels on Web. The framebuffer already
		// applies DPR, so only the atlas density (not widget sizes) includes it.
		coordinateScale /= m_DpiScale;
#endif
		style.ScaleAllSizes(coordinateScale);

		// Fonts are baked for this scale, so no global glyph stretch is applied:
		// FontGlobalScale would resample the atlas and blur it.
		ImGui::GetIO().FontGlobalScale = 1.0f;
#ifdef __EMSCRIPTEN__
		ImGui::GetIO().FontGlobalScale = 1.0f / m_DpiScale;
#endif
	}

	void ImGuiLayer::RequestFontRebuild()
	{
		// Rebuilding between two frames of the same stack would leave the frame that already
		// captured glyph pointers reading freed memory.
		m_FontRebuildPending = true;
	}

	bool ImGuiLayer::SetUiScale(float scale)
	{
		const float next = std::isfinite(scale) && scale > 0.0f ? scale : 1.0f;
		if (std::abs(next - m_DpiScale) < 0.001f)
			return m_BaseStyleCaptured;

		m_DpiScale = next;
		RequestFontRebuild();
		return m_BaseStyleCaptured;
	}

	void ImGuiLayer::SetUserScale(float scale)
	{
		const float next = std::isfinite(scale) && scale > 0.0f ? scale : 1.0f;
		if (std::abs(next - m_UserScale) < 0.001f)
			return;

		m_UserScale = next;
		RequestFontRebuild();
	}

}
