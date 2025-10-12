#include "ExampleLayer.h"
#include <imgui/imgui.h>

#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>
#include "TomCat/Scene/SceneSerializer.h"
#include "TomCat/Utils/PlatformUtils.h"

#include "TomCat/Math/Math.h"
#include <fstream>

namespace TomCat {

	extern const std::filesystem::path g_AssetPath;

	ExampleLayer::ExampleLayer()
		: Layer("FileManager"), m_SelectedMenu(0) // 初始化 selected_menu
	{
	}

	void ExampleLayer::OnAttach()
	{
		TC_PROFILE_FUNCTION();

		// Set up ImGui style to mimic UnityHub
		ImGuiStyle& style = ImGui::GetStyle();
		style.WindowPadding = ImVec2(15, 15);
		style.FramePadding = ImVec2(5, 5);
		style.CellPadding = ImVec2(6, 6);
		style.ItemSpacing = ImVec2(12, 8);
		style.ItemInnerSpacing = ImVec2(8, 6);
		style.IndentSpacing = 25;
		style.ScrollbarSize = 15;
		style.WindowRounding = 4.0f;
		style.FrameRounding = 4.0f;
		style.ScrollbarRounding = 9.0f;
		style.GrabRounding = 3.0f;

		// Set colors similar to UnityHub
		ImVec4* colors = style.Colors;
		colors[ImGuiCol_WindowBg] = ImVec4(0.16f, 0.16f, 0.16f, 1.00f);
		colors[ImGuiCol_MenuBarBg] = ImVec4(0.16f, 0.16f, 0.16f, 1.00f);
		colors[ImGuiCol_TitleBg] = ImVec4(0.16f, 0.16f, 0.16f, 1.00f);
		colors[ImGuiCol_TitleBgActive] = ImVec4(0.16f, 0.16f, 0.16f, 1.00f);
		colors[ImGuiCol_FrameBg] = ImVec4(0.24f, 0.24f, 0.24f, 1.00f);
		colors[ImGuiCol_FrameBgHovered] = ImVec4(0.30f, 0.30f, 0.30f, 1.00f);
		colors[ImGuiCol_FrameBgActive] = ImVec4(0.40f, 0.40f, 0.40f, 1.00f);
		colors[ImGuiCol_Button] = ImVec4(0.44f, 0.44f, 0.44f, 1.00f);
		colors[ImGuiCol_ButtonHovered] = ImVec4(0.54f, 0.54f, 0.54f, 1.00f);
		colors[ImGuiCol_ButtonActive] = ImVec4(0.60f, 0.60f, 0.60f, 1.00f);
		colors[ImGuiCol_Text] = ImVec4(0.95f, 0.95f, 0.95f, 1.00f);
		colors[ImGuiCol_TextDisabled] = ImVec4(0.60f, 0.60f, 0.60f, 1.00f);
	}

	void ExampleLayer::OnDetach()
	{
		TC_PROFILE_FUNCTION();
	}

	void ExampleLayer::OnUpdate(Timestep ts)
	{
		TC_PROFILE_FUNCTION();

	}

	void ExampleLayer::OnImGuiRender()
	{
		TC_PROFILE_FUNCTION();

		// Set main window to cover entire viewport
		ImGuiViewport* viewport = ImGui::GetMainViewport();
		ImGui::SetNextWindowPos(viewport->Pos);
		ImGui::SetNextWindowSize(viewport->Size);
		ImGui::SetNextWindowViewport(viewport->ID);

		ImGuiWindowFlags window_flags = ImGuiWindowFlags_NoDocking | ImGuiWindowFlags_NoTitleBar |
			ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoResize |
			ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoBringToFrontOnFocus |
			ImGuiWindowFlags_NoNavFocus;

		ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
		ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
		ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));

		ImGui::Begin("Hub Window", nullptr, window_flags);
		ImGui::PopStyleVar(3); // Pop all style variables

		// Get available work area size
		ImVec2 work_size = ImGui::GetContentRegionAvail();

		// Left sidebar - fixed width
		float sidebar_width = work_size.x * 0.38f;
		ImGui::BeginChild("Sidebar", ImVec2(sidebar_width, work_size.y), true);
		{
			ImGui::SetCursorPosY(ImGui::GetCursorPosY());

			// Hub title
			ImGui::SetWindowFontScale(2.0f);
			ImGui::SetCursorPosX((sidebar_width - ImGui::CalcTextSize("TomCat Hub").x) * 0.5f);
			ImGui::Text("TomCat Hub");
			ImGui::SetWindowFontScale(1.5f);
			ImGui::Spacing();

			// Vertical menu styling
			ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(0, 1));
			ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(20, 12));

			// Start Setting menu item
			if (ImGui::Selectable("Start", m_SelectedMenu == 0, 0, ImVec2(0, 40)))
			{
				m_SelectedMenu = 0;
			}

			if (ImGui::Selectable("Projects", m_SelectedMenu == 1, 0, ImVec2(0, 40)))
			{
				m_SelectedMenu = 1;
			}

			ImGui::PopStyleVar(2);
		}
		ImGui::EndChild();

		if (m_SelectedMenu == 1)
		{
			// Main content area - takes remaining space
			ImGui::SameLine();
			ImGui::BeginChild("MainContent", ImVec2(work_size.x - sidebar_width, work_size.y), false);
			{
				// Project list area
				ImGui::BeginChild("ProjectList", ImVec2(0, work_size.y), true);
				{
					// 项目标题和操作按钮，右对齐
					float button_width = 100.0f;
					float spacing = 10.0f;
					float total_buttons_width = button_width * 2 + spacing;

					// 先计算文本宽度
					float text_width = ImGui::CalcTextSize("Projects").x;

					// 计算剩余空间，并留出按钮宽度
					float available_width = ImGui::GetWindowContentRegionWidth();
					float offset = available_width - total_buttons_width;

					// 绘制标题文本
					ImGui::Text("Projects");

					// 移动光标到右侧，放置按钮
					ImGui::SameLine(offset);
					if (ImGui::Button("AddProject", ImVec2(button_width, 0)))
					{
						AddProject();
					}

					ImGui::SameLine(0, spacing);
					if (ImGui::Button("NewProject", ImVec2(button_width, 0)))
					{
						NewProject();
					}

					ImGui::Separator();

					ImGui::Text("Name");
					ImGui::Separator();
					// 这里可以添加项目列表
				}
				ImGui::EndChild();
			}
			ImGui::EndChild();
		}
		// 可以在这里添加其他菜单项的内容
		else if (m_SelectedMenu == 0)
		{
			// Start Setting 的内容
			ImGui::SameLine();
			ImGui::BeginChild("StartSettingContent", ImVec2(work_size.x - sidebar_width, work_size.y), false);
			{
				ImGui::BeginChild("StartSettingPanel", ImVec2(0, work_size.y), true);
				{
					ImGui::Text("Start");
					ImGui::Separator();
					ImGui::Text(" Start Setting ...");
					// 添加 Start Setting 的具体内容
				}
				ImGui::EndChild();
			}
			ImGui::EndChild();
		}

		// End main window
		ImGui::End();
	}

	void ExampleLayer::OnEvent(Event& e)
	{
		// 事件处理
	}

	void ExampleLayer::AddProject()
	{
		// 打开文件对话框让用户选择项目文件或目录
		std::string projectPath = FileDialogs::OpenFile("TomCat Project (*.tcproj);;All Files (*.*)");
		if (!projectPath.empty())
		{
			//TC_CORE_INFO("添加项目: {0}", projectPath);
			// 这里添加项目到列表的逻辑
		}
	}

	void ExampleLayer::NewProject()
	{
		std::string projectPath = FileDialogs::SaveFile("TomCat Project (*.tcproj);;All Files (*.*)");
		if (!projectPath.empty())
		{
			//TC_CORE_INFO("创建新项目: {0}", projectPath);
			// 这里创建新项目的逻辑
		}
	}
}