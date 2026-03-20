#include "ExampleLayer.h"
#include <imgui/imgui.h>

#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>
#include "TomCat/Scene/SceneSerializer.h"
#include "TomCat/Utils/PlatformUtils.h"
#include "TomCat/Project/ProjectManager.h"

#include "TomCat/Math/Math.h"
#include <fstream>
#include <ctime>

namespace TomCat {

	extern const std::filesystem::path g_AssetPath;

	ExampleLayer::ExampleLayer()
		: Layer("FileManager"), m_SelectedMenu(0)
	{
		ProjectManager::Get().SetProjectDirectory(std::filesystem::current_path() / "Projects");
		ProjectManager::Get().ScanProjects();
		m_Projects = ProjectManager::Get().GetProjects();
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
		ImGui::PopStyleVar(3);

		ImVec2 work_size = ImGui::GetContentRegionAvail();
		float sidebar_width = work_size.x * 0.38f;

		ImGui::BeginChild("Sidebar", ImVec2(sidebar_width, work_size.y), true);
		{
			ImGui::SetCursorPosY(ImGui::GetCursorPosY());

			ImGui::SetWindowFontScale(2.0f);
			ImGui::SetCursorPosX((sidebar_width - ImGui::CalcTextSize("TomCat Hub").x) * 0.5f);
			ImGui::Text("TomCat Hub");
			ImGui::SetWindowFontScale(1.5f);
			ImGui::Spacing();

			ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(0, 1));
			ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(20, 12));

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
			ImGui::SameLine();
			ImGui::BeginChild("MainContent", ImVec2(work_size.x - sidebar_width, work_size.y), false);
			{
				RenderProjectList();
			}
			ImGui::EndChild();
		}
		else if (m_SelectedMenu == 0)
		{
			ImGui::SameLine();
			ImGui::BeginChild("StartSettingContent", ImVec2(work_size.x - sidebar_width, work_size.y), false);
			{
				RenderStartSettings();
			}
			ImGui::EndChild();
		}

		ImGui::End();

		if (m_ShowNewProjectDialog)
		{
			RenderNewProjectDialog();
		}
	}

	void ExampleLayer::OnEvent(Event& e)
	{
		// 事件处理
	}

	void ExampleLayer::AddProject()
	{
		std::string projectPath = FileDialogs::OpenFile("TomCat Project (*.tcproj)");
		if (!projectPath.empty())
		{
			auto project = ProjectManager::Get().LoadProject(projectPath);
			if (project)
			{
				m_Projects = ProjectManager::Get().GetProjects();
			}
		}
	}

	void ExampleLayer::NewProject()
	{
		m_ShowNewProjectDialog = true;
		memset(m_NewProjectName, 0, sizeof(m_NewProjectName));
		memset(m_NewProjectAuthor, 0, sizeof(m_NewProjectAuthor));
		memset(m_NewProjectDescription, 0, sizeof(m_NewProjectDescription));
		m_NewProjectPath = std::filesystem::current_path() / "Projects";
	}

	void ExampleLayer::OpenProject(Ref<Project> project)
	{
		if (project)
		{
			ProjectManager::Get().SetActiveProject(project);
			ProjectManager::Get().OpenProjectInEditor(project);
		}
	}

	void ExampleLayer::DeleteProject(Ref<Project> project)
	{
		if (project)
		{
			ProjectManager::Get().RemoveProject(project->GetProjectPath());
			m_Projects = ProjectManager::Get().GetProjects();
		}
	}

	void ExampleLayer::RenderProjectList()
	{
		ImGui::BeginChild("ProjectList", ImVec2(0, 0), true);
		{
			float button_width = 100.0f;
			float spacing = 10.0f;
			float total_buttons_width = button_width * 2 + spacing;
			float available_width = ImGui::GetWindowContentRegionWidth();
			float offset = available_width - total_buttons_width;

			ImGui::Text("Projects");
			ImGui::SameLine(offset);
			if (ImGui::Button("Add", ImVec2(button_width, 0)))
			{
				AddProject();
			}

			ImGui::SameLine(0, spacing);
			if (ImGui::Button("New", ImVec2(button_width, 0)))
			{
				NewProject();
			}

			ImGui::Separator();

			if (m_Projects.empty())
			{
				ImGui::TextColored(ImVec4(0.6f, 0.6f, 0.6f, 1.0f), "No projects found. Create a new project to get started.");
			}
			else
			{
				ImGui::Text("Name");
				ImGui::SameLine(200);
				ImGui::Text("Version");
				ImGui::SameLine(300);
				ImGui::Text("Author");
				ImGui::SameLine(450);
				ImGui::Text("Last Modified");
				ImGui::SameLine(600);
				ImGui::Text("Actions");
				ImGui::Separator();

				for (const auto& project : m_Projects)
				{
					ImGui::Selectable(project->GetName().c_str(), false, ImGuiSelectableFlags_SpanAllColumns);
					ImGui::SameLine(200);
					ImGui::Text(project->GetVersion().c_str());
					ImGui::SameLine(300);
					ImGui::Text(project->GetConfig().Author.c_str());
					ImGui::SameLine(450);
					
					time_t modTime = project->GetLastModified();
					if (modTime > 0)
					{
						struct tm timeInfo;
						if (localtime_s(&timeInfo, &modTime) == 0)
						{
							char timeBuffer[80];
							if (strftime(timeBuffer, sizeof(timeBuffer), "%Y-%m-%d %H:%M", &timeInfo) > 0)
							{
								ImGui::Text(timeBuffer);
							}
							else
							{
								ImGui::Text("Unknown");
							}
						}
						else
						{
							ImGui::Text("Unknown");
						}
					}
					else
					{
						ImGui::Text("Unknown");
					}

					ImGui::SameLine(600);
					ImGui::PushID(project->GetName().c_str());
					if (ImGui::Button("Open", ImVec2(60, 0)))
					{
						OpenProject(project);
					}
					ImGui::SameLine();
					if (ImGui::Button("Delete", ImVec2(60, 0)))
					{
						DeleteProject(project);
					}
					ImGui::PopID();
				}
			}
		}
		ImGui::EndChild();
	}

	void ExampleLayer::RenderNewProjectDialog()
	{
		ImGui::SetNextWindowSize(ImVec2(500, 400), ImGuiCond_Always);
		ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(), ImGuiCond_Always, ImVec2(0.5f, 0.5f));

		if (ImGui::Begin("New Project", &m_ShowNewProjectDialog, ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoResize))
		{
			ImGui::Text("Project Name:");
			ImGui::InputText("##Name", m_NewProjectName, sizeof(m_NewProjectName));

			ImGui::Text("Author:");
			ImGui::InputText("##Author", m_NewProjectAuthor, sizeof(m_NewProjectAuthor));

			ImGui::Text("Description:");
			ImGui::InputTextMultiline("##Description", m_NewProjectDescription, sizeof(m_NewProjectDescription), ImVec2(0, 80));

			ImGui::Text("Location:");
			ImGui::Text(m_NewProjectPath.string().c_str());
			ImGui::SameLine();
			if (ImGui::Button("Browse..."))
			{
				std::string path = FileDialogs::OpenFolder();
				if (!path.empty())
				{
					m_NewProjectPath = path;
				}
			}

			ImGui::Spacing();
			ImGui::Spacing();

			float buttonWidth = 120.0f;
			float availableWidth = ImGui::GetContentRegionAvail().x;
			ImGui::SetCursorPosX(availableWidth - buttonWidth * 2 - 10);

			if (ImGui::Button("Cancel", ImVec2(buttonWidth, 0)))
			{
				m_ShowNewProjectDialog = false;
			}

			ImGui::SameLine();
			if (ImGui::Button("Create", ImVec2(buttonWidth, 0)))
			{
				if (strlen(m_NewProjectName) > 0)
				{
					ProjectConfig config;
					config.Name = m_NewProjectName;
					config.Author = m_NewProjectAuthor;
					config.Description = m_NewProjectDescription;
					config.Version = "1.0.0";

					std::filesystem::path projectPath = m_NewProjectPath / m_NewProjectName / "Project.tcproj";
					auto project = ProjectManager::Get().CreateProject(projectPath, config);
					if (project)
					{
						m_Projects = ProjectManager::Get().GetProjects();
						m_ShowNewProjectDialog = false;
					}
				}
			}
		}
		ImGui::End();
	}

	void ExampleLayer::RenderStartSettings()
	{
		ImGui::BeginChild("StartSettingPanel", ImVec2(0, 0), true);
		{
			ImGui::Text("Start Settings");
			ImGui::Separator();
			ImGui::Text("Configure your TomCat Engine preferences here.");
			ImGui::Spacing();
			
			ImGui::Text("Project Directory:");
			ImGui::Text(ProjectManager::Get().GetProjectDirectory().string().c_str());
			ImGui::SameLine();
			if (ImGui::Button("Change"))
			{
				std::string path = FileDialogs::OpenFolder();
				if (!path.empty())
				{
					ProjectManager::Get().SetProjectDirectory(path);
					m_Projects = ProjectManager::Get().GetProjects();
				}
			}

			ImGui::Spacing();
			ImGui::Separator();
			ImGui::Text("Engine Version: 1.0.0");
			ImGui::Text("Build: Debug");
		}
		ImGui::EndChild();
	}
}