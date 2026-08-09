#include "tcpch.h"
#include "ImGuiLayer.h"

#include "imgui.h"
#include "backends/imgui_impl_glfw.h"
#include "backends/imgui_impl_opengl3.h"

#include "TomCat/Core/Application.h"

#include <GLFW/glfw3.h>
#include <Glad/glad.h>
#include <filesystem>

#include"ImGuizmo.h"

namespace TomCat {

	ImGuiLayer::ImGuiLayer()
		: Layer("ImGuiLayer")
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
		io.ConfigFlags |= ImGuiConfigFlags_ViewportsEnable;         // Enable Multi-Viewport / Platform Windows
		//io.ConfigFlags |= ImGuiConfigFlags_ViewportsNoTaskBarIcons;
		//io.ConfigFlags |= ImGuiConfigFlags_ViewportsNoMerge;

				io.Fonts->AddFontFromFileTTF("Packages/fonts/opensans/OpenSans-Bold.ttf", 32.0f);
		io.FontDefault = io.Fonts->AddFontFromFileTTF("Packages/fonts/opensans/OpenSans-Regular.ttf", 32.0f);

		// Make the UI support Chinese and common symbols:
		//  - simhei.ttf provides Chinese glyphs (2500 common chars + full-width punctuation
		//    + curly quotes + the rare UI char "?" U+64CE)
		//  - seguisym.ttf provides U+22EE (vertical ellipsis "?") and other symbols that
		//    simhei does not have.
		if (io.Fonts->Fonts.size() >= 2)
		{
			// Chinese + punctuation ranges
			// Note: SimplifiedCommon expands to ~5017 range items, so the buffer must be big
			// enough (a too-small buffer truncates glyphs and renders "?" for missing chars).
			static ImWchar cnRanges[16384];
			{
				const ImWchar* base = io.Fonts->GetGlyphRangesChineseSimplifiedCommon();
				int n = 0;
				for (; base[n] != 0 && n < 16300; ++n)
					cnRanges[n] = base[n];
				static const ImWchar extraRanges[] = {
					0x2014, 0x2015,   // em dash
					0x2018, 0x201E,   // curly quotes
					0x2026, 0x2026,   // ellipsis
					0x3000, 0x303F,   // CJK punctuation
					0xFF00, 0xFFEF,   // full-width forms
0x51FD, 0x51FD, 0x62DF, 0x62DF, 0x62FD, 0x62FD, 0x64CE, 0x64CE, 0x6D4F, 0x6D4F, 0x6E32, 0x6E32, 0x8F91, 0x8F91, 0x903B, 0x903B,
					0x94AE, 0x94AE,
					0
				};
				for (int i = 0; extraRanges[i] != 0 && n < 16350; ++i)
					cnRanges[n++] = extraRanges[i];
				cnRanges[n] = 0;
			}

			// Symbol ranges (U+22EE and friends) from Segoe UI Symbol
			static const ImWchar symRanges[] = {
				0x22EE, 0x22EE,   // vertical ellipsis
				0x25B6, 0x25B6,   // black right-pointing triangle
				0x25C0, 0x25C0,   // black left-pointing triangle
				0x25B2, 0x25B2,   // black up-pointing triangle
				0x25BC, 0x25BC,   // black down-pointing triangle
				0x25CF, 0x25CF,   // black circle
				0x25A0, 0x25A0,   // black square
				0
			};

			const char* cnFontPath = "C:/Windows/Fonts/simhei.ttf";
			const char* symFontPath = "C:/Windows/Fonts/seguisym.ttf";

			ImFontConfig mergeCfg;
			mergeCfg.MergeMode = true;

			if (std::filesystem::exists(cnFontPath))
			{
				mergeCfg.DstFont = io.Fonts->Fonts[0]; // OpenSans-Bold + Chinese
				io.Fonts->AddFontFromFileTTF(cnFontPath, 32.0f, &mergeCfg, cnRanges);
				mergeCfg.DstFont = io.Fonts->Fonts[1]; // OpenSans-Regular + Chinese (default)
				io.Fonts->AddFontFromFileTTF(cnFontPath, 32.0f, &mergeCfg, cnRanges);
			}
			if (std::filesystem::exists(symFontPath))
			{
				mergeCfg.DstFont = io.Fonts->Fonts[0];
				io.Fonts->AddFontFromFileTTF(symFontPath, 32.0f, &mergeCfg, symRanges);
				mergeCfg.DstFont = io.Fonts->Fonts[1];
				io.Fonts->AddFontFromFileTTF(symFontPath, 32.0f, &mergeCfg, symRanges);
			}
		}

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

		Application& app = Application::Get();
		GLFWwindow* window = static_cast<GLFWwindow*>(app.GetWindow().GetNativeWindow());

		// Setup Platform/Renderer bindings
		ImGui_ImplGlfw_InitForOpenGL(window, true);
		ImGui_ImplOpenGL3_Init("#version 410");
	}

	void ImGuiLayer::OnDetach()
	{
		TC_PROFILE_FUNCTION();

		ImGui_ImplOpenGL3_Shutdown();
		ImGui_ImplGlfw_Shutdown();
		ImGui::DestroyContext();
	}

	void ImGuiLayer::OnEvent(Event& e)
	{
		if (m_BlockEvents) 
		{
			ImGuiIO& io = ImGui::GetIO();
			e.m_Handled |= e.IsIncategory(EventCategoryMouse) & io.WantCaptureMouse;
			e.m_Handled |= e.IsIncategory(EventCategoryKeyboard) & io.WantCaptureKeyboard;
		}

	}


	void ImGuiLayer::Begin()
	{
		TC_PROFILE_FUNCTION();

		ImGui_ImplOpenGL3_NewFrame();
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
		ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());

		if (io.ConfigFlags & ImGuiConfigFlags_ViewportsEnable)
		{
			GLFWwindow* backup_current_context = glfwGetCurrentContext();
			ImGui::UpdatePlatformWindows();
			ImGui::RenderPlatformWindowsDefault();
			glfwMakeContextCurrent(backup_current_context);
		}
	}

	void ImGuiLayer::SetDarkThemeColors()
	{
		auto& colors = ImGui::GetStyle().Colors;
		colors[ImGuiCol_WindowBg] = ImVec4{ 0.1f, 0.105f, 0.11f, 1.0f };

		// Headers
		colors[ImGuiCol_Header] = ImVec4{ 0.2f, 0.205f, 0.21f, 1.0f };
		colors[ImGuiCol_HeaderHovered] = ImVec4{ 0.3f, 0.305f, 0.31f, 1.0f };
		colors[ImGuiCol_HeaderActive] = ImVec4{ 0.15f, 0.1505f, 0.151f, 1.0f };

		// Buttons
		colors[ImGuiCol_Button] = ImVec4{ 0.2f, 0.205f, 0.21f, 1.0f };
		colors[ImGuiCol_ButtonHovered] = ImVec4{ 0.3f, 0.305f, 0.31f, 1.0f };
		colors[ImGuiCol_ButtonActive] = ImVec4{ 0.15f, 0.1505f, 0.151f, 1.0f };

		// Frame BG
		colors[ImGuiCol_FrameBg] = ImVec4{ 0.2f, 0.205f, 0.21f, 1.0f };
		colors[ImGuiCol_FrameBgHovered] = ImVec4{ 0.3f, 0.305f, 0.31f, 1.0f };
		colors[ImGuiCol_FrameBgActive] = ImVec4{ 0.15f, 0.1505f, 0.151f, 1.0f };

		// Tabs
		colors[ImGuiCol_Tab] = ImVec4{ 0.15f, 0.1505f, 0.151f, 1.0f };
		colors[ImGuiCol_TabHovered] = ImVec4{ 0.38f, 0.3805f, 0.381f, 1.0f };
		colors[ImGuiCol_TabActive] = ImVec4{ 0.28f, 0.2805f, 0.281f, 1.0f };
		colors[ImGuiCol_TabUnfocused] = ImVec4{ 0.15f, 0.1505f, 0.151f, 1.0f };
		colors[ImGuiCol_TabUnfocusedActive] = ImVec4{ 0.2f, 0.205f, 0.21f, 1.0f };

		// Title
		colors[ImGuiCol_TitleBg] = ImVec4{ 0.15f, 0.1505f, 0.151f, 1.0f };
		colors[ImGuiCol_TitleBgActive] = ImVec4{ 0.15f, 0.1505f, 0.151f, 1.0f };
		colors[ImGuiCol_TitleBgCollapsed] = ImVec4{ 0.15f, 0.1505f, 0.151f, 1.0f };
	}

}