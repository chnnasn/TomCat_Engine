#include "ExampleLayer.h"
#include <imgui/imgui.h>

#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>
#include "TomCat/Scene/SceneSerializer.h"
#include "TomCat/Utils/PlatformUtils.h"
#include "TomCat/Project/ProjectManager.h"

#include "TomCat/Math/Math.h"
#include <fstream>
#include <cstdio>
#include <cctype>
#include <algorithm>

namespace {

	std::string ToLowerString(const std::string& s)
	{
		std::string out = s;
		for (auto& c : out)
			c = (char)std::tolower((unsigned char)c);
		return out;
	}

}

namespace TomCat {

	std::vector<std::string> m_Editers;

	ExampleLayer::ExampleLayer()
		: Layer("FileManager"), m_SelectedMenu(0)
	{
		ProjectManager::Get().SetProjectDirectory(std::filesystem::current_path() / "Projects");
		ProjectManager::Get().SetEditorDirectory(std::filesystem::current_path() / "Editors");
		ProjectManager::Get().ScanProjects();
		m_Projects = ProjectManager::Get().GetProjects();
		
		m_Editers = ProjectManager::Get().GetEditorDirectoryFiles();
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
			ImGuiWindowFlags_NoNavFocus | ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse;

		ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
		ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
		ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));

		ImGui::Begin("Hub Window", nullptr, window_flags);
		ImGui::PopStyleVar(3);

		ImVec2 work = ImGui::GetContentRegionAvail();
		const float sidebarW = 240.0f;

		// Unity-Hub style layout: dark narrow sidebar + main area
		ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0.11f, 0.11f, 0.11f, 1.0f));
		ImGui::BeginChild("Sidebar", ImVec2(sidebarW, work.y), false, ImGuiWindowFlags_NoScrollbar);
		RenderSidebar(ImVec2(sidebarW, work.y));
		ImGui::EndChild();
		ImGui::PopStyleColor();

		ImGui::SameLine(0.0f, 0.0f);
		ImGui::BeginChild("Main", ImVec2(work.x - sidebarW, work.y), false, ImGuiWindowFlags_NoScrollbar);
		RenderProjectList();
		ImGui::EndChild();

		ImGui::End();

		if (m_ShowNewProjectDialog)
			RenderNewProjectDialog();
		if (m_ShowSettingsDialog)
			RenderSettingsDialog();
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
			auto project = ProjectManager::Get().AddProject(projectPath);
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
		const auto& defaultDir = ProjectManager::Get().GetProjectDirectory();
		m_NewProjectPath = defaultDir.empty() ? (std::filesystem::current_path() / "Projects") : defaultDir;
	}

	void ExampleLayer::OpenProject(Ref<Project> project)
	{
		if (project)
		{
			ProjectManager::Get().SetActiveProject(project);
			ProjectManager::Get().OpenProjectInEditor(project);
			ProjectManager::Get().ScanProjects();
			m_Projects = ProjectManager::Get().GetProjects();
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
		void ExampleLayer::RenderSidebar(const ImVec2& size)
	{
		// Brand
		ImGui::SetCursorPos(ImVec2(20.0f, 26.0f));
		ImGui::Text("TomCat Hub");
		ImGui::SetCursorPos(ImVec2(20.0f, 72.0f));
		ImGui::TextDisabled("引擎管理器");

		ImGui::SetCursorPos(ImVec2(12.0f, 104.0f));
		ImGui::Separator();

		// Projects (the main page, always highlighted like Unity Hub)
		ImGui::SetCursorPos(ImVec2(12.0f, 120.0f));
		ImGui::PushStyleColor(ImGuiCol_Header, ImVec4(0.24f, 0.24f, 0.24f, 1.0f));
		ImGui::PushStyleColor(ImGuiCol_HeaderHovered, ImVec4(0.20f, 0.20f, 0.20f, 1.0f));
		ImGui::PushStyleColor(ImGuiCol_HeaderActive, ImVec4(0.22f, 0.22f, 0.22f, 1.0f));
		ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.95f, 0.95f, 0.95f, 1.0f));
		ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(16.0f, 10.0f));
		ImGui::Selectable("项目", true, 0, ImVec2(size.x - 24.0f, 52.0f));
		ImGui::PopStyleVar();
		ImGui::PopStyleColor(4);

		// Settings button -> opens a dialog (per user request)
		ImGui::SetCursorPos(ImVec2(12.0f, size.y - 130.0f));
		ImGui::PushStyleColor(ImGuiCol_HeaderHovered, ImVec4(0.20f, 0.20f, 0.20f, 1.0f));
		ImGui::PushStyleColor(ImGuiCol_HeaderActive, ImVec4(0.22f, 0.22f, 0.22f, 1.0f));
		ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.85f, 0.85f, 0.85f, 1.0f));
		ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(16.0f, 10.0f));
		if (ImGui::Selectable("设置", false, 0, ImVec2(size.x - 24.0f, 52.0f)))
			m_ShowSettingsDialog = true;
		ImGui::PopStyleVar();
		ImGui::PopStyleColor(3);

		// Footer
		ImGui::SetCursorPos(ImVec2(20.0f, size.y - 72.0f));
		ImGui::TextDisabled("TomCat 引擎");
		ImGui::SetCursorPos(ImVec2(20.0f, size.y - 44.0f));
		ImGui::TextDisabled("v1.0.0  |  Release");
	}

	bool ExampleLayer::SortProjects(const Ref<Project>& a, const Ref<Project>& b) const
	{
		int cmp = 0;
		switch (m_SortColumn)
		{
		case HubSortColumn::Name: cmp = a->GetName().compare(b->GetName()); break;
		case HubSortColumn::Modified: cmp = a->GetLastOperationTime().compare(b->GetLastOperationTime()); break;
		case HubSortColumn::EditorVersion: cmp = a->GetEditorVersion().compare(b->GetEditorVersion()); break;
		}
		if (cmp == 0)
			return false;
		return m_SortAscending ? (cmp < 0) : (cmp > 0);
	}

	void ExampleLayer::RenderProjectList()
	{
		auto* dl = ImGui::GetWindowDrawList();
		ImVec2 wmin = ImGui::GetWindowPos();
		ImVec2 size = ImGui::GetContentRegionAvail();
		const float pad = 24.0f;
		const float contentW = size.x - pad * 2.0f;

		// Build filtered + sorted list
		m_VisibleProjects.clear();
		std::string query = ToLowerString(m_SearchBuffer);
		for (const auto& project : m_Projects)
		{
			if (!query.empty())
			{
				std::string hay = ToLowerString(project->GetName() + " " + project->GetProjectPath().string());
				if (hay.find(query) == std::string::npos)
					continue;
			}
			m_VisibleProjects.push_back(project);
		}
		std::sort(m_VisibleProjects.begin(), m_VisibleProjects.end(),
			[this](const Ref<Project>& a, const Ref<Project>& b) { return SortProjects(a, b); });

		// Header: title + count
		ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.95f, 0.95f, 0.95f, 1.0f));
		ImGui::SetCursorPos(ImVec2(pad, pad));
		ImGui::Text("项目");
		ImGui::PopStyleColor();

		char sub[96];
		snprintf(sub, sizeof(sub), "%zu 个项目", m_VisibleProjects.size());
		ImGui::SetCursorPos(ImVec2(pad + 4.0f, pad + 46.0f));
		ImGui::TextDisabled(sub);

		// Top-right: search, Add dropdown, New project (blue CTA)
		const float btnH = 56.0f;
		const float searchW = 300.0f, addW = 110.0f, newW = 180.0f;
		float newX = size.x - pad - newW;
		float addX = newX - 10.0f - addW;
		float searchX = addX - 12.0f - searchW;
		float topY = pad;

		ImGui::SetCursorPos(ImVec2(searchX, topY));
		ImGui::SetNextItemWidth(searchW);
		ImGui::InputTextWithHint("##HubSearch", "搜索项目", m_SearchBuffer, sizeof(m_SearchBuffer),
			ImGuiInputTextFlags_AutoSelectAll);

		ImGui::SetCursorPos(ImVec2(addX, topY));
		if (ImGui::Button("添加", ImVec2(addW, btnH)))
			ImGui::OpenPopup("AddProjectMenu");
		if (ImGui::BeginPopup("AddProjectMenu"))
		{
			if (ImGui::Selectable("新建项目"))
				NewProject();
			if (ImGui::Selectable("从磁盘添加项目"))
				AddProject();
			ImGui::EndPopup();
		}

		ImGui::SetCursorPos(ImVec2(newX, topY));
		ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.27f, 0.55f, 0.92f, 1.0f));
		ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.20f, 0.45f, 0.85f, 1.0f));
		ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.15f, 0.38f, 0.78f, 1.0f));
		ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 1.0f, 1.0f, 1.0f));
		if (ImGui::Button("+ 新建项目", ImVec2(newW, btnH)))
			NewProject();
		ImGui::PopStyleColor(4);

		// Column header (Name | Modified | Editor version)
		const float headerH = 44.0f;
		const float modifiedW = 180.0f, versionW = 160.0f, actionsW = 230.0f;
		float listTop = pad + 46.0f + 20.0f + btnH + 6.0f;
		float versionX = size.x - pad - actionsW - versionW;
		float modifiedX = versionX - modifiedW;

		ImVec2 headerMin(wmin.x + pad, wmin.y + listTop);
		ImVec2 headerMax(wmin.x + size.x - pad, wmin.y + listTop + headerH);
		dl->AddRectFilled(headerMin, headerMax, IM_COL32(61, 61, 61, 255));
		dl->AddRectFilled(ImVec2(headerMin.x, headerMax.y - 1.0f), headerMax, IM_COL32(82, 82, 82, 255));

		auto headerCell = [this](const char* label, HubSortColumn col, ImVec2 pos, float w, float h)
		{
			ImGui::SetCursorScreenPos(pos);
			ImGui::InvisibleButton(("##hdr_" + std::string(label)).c_str(), ImVec2(w, h));
			if (ImGui::IsItemClicked(ImGuiMouseButton_Left))
			{
				if (m_SortColumn == col)
					m_SortAscending = !m_SortAscending;
				else
				{
					m_SortColumn = col;
					m_SortAscending = (col == HubSortColumn::Name);
				}
			}
			bool active = (m_SortColumn == col);
			ImGui::SetCursorScreenPos(ImVec2(pos.x + 10.0f, pos.y + 8.0f));
			ImGui::PushStyleColor(ImGuiCol_Text,
				active ? ImVec4(0.95f, 0.95f, 0.95f, 1.0f) : ImVec4(0.70f, 0.70f, 0.70f, 1.0f));
			ImGui::Text("%s", label);
			if (active)
			{
				ImGui::SameLine();
				ImGui::Text("%s", m_SortAscending ? "^" : "v");
			}
			ImGui::PopStyleColor();
		};

		headerCell("名称", HubSortColumn::Name, headerMin, contentW * 0.45f, headerH);
		headerCell("修改时间", HubSortColumn::Modified, ImVec2(wmin.x + modifiedX, wmin.y + listTop), modifiedW, headerH);
		headerCell("编辑器版本", HubSortColumn::EditorVersion, ImVec2(wmin.x + versionX, wmin.y + listTop), versionW, headerH);

		// Scrollable project list
		ImGui::SetCursorPos(ImVec2(pad, listTop + headerH));
		ImGui::BeginChild("ProjectListScroll", ImVec2(contentW, size.y - (listTop + headerH)), false, ImGuiWindowFlags_AlwaysVerticalScrollbar);
		{
			ImVec2 cmin = ImGui::GetWindowPos();
			float y = cmin.y;

			if (m_VisibleProjects.empty())
			{
				const char* emptyText = m_SearchBuffer[0]
					? "没有与搜索匹配的项目。"
					: "还没有项目。点击“添加”或“新建项目”开始。";
				ImGui::SetCursorScreenPos(ImVec2(cmin.x + 8.0f, cmin.y + 24.0f));
				ImGui::TextDisabled("%s", emptyText);
			}

			for (int i = 0; i < (int)m_VisibleProjects.size(); i++)
			{
				ImVec2 rowMin(cmin.x, y);
				ImVec2 rowMax(rowMin.x + contentW, y + 96.0f);
				ImGui::SetCursorScreenPos(rowMin);
				RenderProjectRow(m_VisibleProjects[i], i, rowMin, rowMax);
				y += 96.0f;
			}
		}
		ImGui::EndChild();
	}

	void ExampleLayer::RenderProjectRow(Ref<Project> project, int index, const ImVec2& rowMin, const ImVec2& rowMax)
	{
		auto* dl = ImGui::GetWindowDrawList();
		const float rowW = rowMax.x - rowMin.x;
		const float modifiedW = 180.0f, versionW = 160.0f, actionsW = 230.0f;

		ImGui::InvisibleButton(("##project_row_" + std::to_string(index)).c_str(), ImVec2(rowW, 96.0f));
		bool hovered = ImGui::IsItemHovered();
		bool selected = (m_SelectedProject == project);
		bool doubleClicked = hovered && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left);

		if (ImGui::IsItemClicked(ImGuiMouseButton_Left))
			m_SelectedProject = project;
		if (doubleClicked)
			OpenProject(project);

		// Row background (same gray family as the existing theme)
		ImU32 bg = selected ? IM_COL32(61, 61, 61, 255)
			: (hovered ? IM_COL32(52, 52, 52, 255) : IM_COL32(44, 44, 44, 255));
		dl->AddRectFilled(rowMin, rowMax, bg);
		dl->AddRectFilled(ImVec2(rowMin.x, rowMax.y - 1.0f), rowMax, IM_COL32(72, 72, 72, 255));
		if (selected)
			dl->AddRectFilled(ImVec2(rowMin.x, rowMin.y), ImVec2(rowMin.x + 3.0f, rowMax.y), IM_COL32(200, 200, 200, 255));

		// Project icon (gray square + first letter)
		ImVec2 iconMin(rowMin.x + 20.0f, rowMin.y + 24.0f);
		ImVec2 iconMax(iconMin.x + 48.0f, iconMin.y + 48.0f);
		dl->AddRectFilled(iconMin, iconMax, IM_COL32(112, 112, 112, 255), 6.0f);
		std::string initial = project->GetName().empty() ? "P" : project->GetName().substr(0, 1);
		ImGui::SetCursorScreenPos(ImVec2(iconMin.x + 6.0f, iconMin.y + 4.0f));
		ImGui::Text("%s", initial.c_str());

		// Name + path
		float tx = rowMin.x + 20.0f + 48.0f + 16.0f;
		ImGui::SetCursorScreenPos(ImVec2(tx, rowMin.y + 14.0f));
		ImGui::Text("%s", project->GetName().c_str());

		float nameMaxW = (rowMax.x - actionsW - versionW - modifiedW) - tx - 8.0f;
		std::string path = project->GetProjectPath().string();
		if (ImGui::CalcTextSize(path.c_str()).x > nameMaxW)
		{
			std::string out = path;
			while (!out.empty())
			{
				out.pop_back();
				if (ImGui::CalcTextSize((out + "...").c_str()).x <= nameMaxW)
					break;
			}
			path = out + "...";
		}
		ImGui::SetCursorScreenPos(ImVec2(tx, rowMin.y + 54.0f));
		ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.60f, 0.60f, 0.60f, 1.0f));
		ImGui::Text("%s", path.c_str());
		ImGui::PopStyleColor();

		// Modified / Editor version columns (aligned with the header)
		float versionX = rowMax.x - actionsW - versionW;
		float modifiedX = versionX - modifiedW;
		ImGui::SetCursorScreenPos(ImVec2(modifiedX + 8.0f, rowMin.y + 32.0f));
		ImGui::Text("%s", project->GetLastOperationTimeAgo().c_str());
		ImGui::SetCursorScreenPos(ImVec2(versionX + 8.0f, rowMin.y + 32.0f));
		ImGui::Text("%s", project->GetEditorVersion().c_str());

		// Actions
		const float btnH = 56.0f;
		float delX = rowMax.x - 20.0f - 110.0f;
		float openX = delX - 8.0f - 130.0f;
		float btnY = rowMin.y + (96.0f - btnH) * 0.5f;

		ImGui::SetCursorScreenPos(ImVec2(openX, btnY));
		if (ImGui::Button(("打开##" + std::to_string(index)).c_str(), ImVec2(130.0f, btnH)))
			OpenProject(project);

		ImGui::SetCursorScreenPos(ImVec2(delX, btnY));
		if (ImGui::Button(("删除##" + std::to_string(index)).c_str(), ImVec2(110.0f, btnH)))
			DeleteProject(project);
	}

void ExampleLayer::RenderNewProjectDialog()
	{
		// 使用局部变量，不要用成员变量（或者确保成员变量被正确初始化）
		static int selectedVersion = 0;

		// 在显示对话框前刷新编辑器列表
		static bool needsRefresh = true;
		if (needsRefresh)
		{
			m_Editers = ProjectManager::Get().GetEditorDirectoryFiles();
			needsRefresh = false;
			selectedVersion = 0;
		}

		ImGui::SetNextWindowSize(ImVec2(800, 600), ImGuiCond_Always);
		ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(), ImGuiCond_Always, ImVec2(0.5f, 0.5f));

		if (ImGui::Begin("新建项目", &m_ShowNewProjectDialog, ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoResize))
		{
			ImGui::Text("项目名称：");
			ImGui::InputText("##Name", m_NewProjectName, sizeof(m_NewProjectName));

			// 检查编辑器列表是否为空
			if (m_Editers.empty())
			{
				ImGui::TextColored(ImVec4(1.0f, 0.5f, 0.5f, 1.0f),
					"未找到编辑器版本，请在设置中检查编辑器目录。");
			}
			else
			{
				// 确保 selectedVersion 在有效范围内
				if (selectedVersion >= (int)m_Editers.size())
				{
					selectedVersion = 0;
				}

				// 修复1: 检查索引有效性
				const char* previewValue = m_Editers[selectedVersion].c_str();

				if (ImGui::BeginCombo("##EditorVersion", previewValue))
				{
					for (int i = 0; i < (int)m_Editers.size(); i++)
					{
						bool is_selected = (selectedVersion == i);

						// 修复2: 使用 m_Editers[i] 而不是 m_Editers[m_SelectedVersion]
						if (ImGui::Selectable(m_Editers[i].c_str(), is_selected))
						{
							selectedVersion = i;
						}

						if (is_selected)
						{
							ImGui::SetItemDefaultFocus();
						}
					}
					ImGui::EndCombo();
				}
			}

			ImGui::Text("描述：");
			ImGui::InputTextMultiline("##Description", m_NewProjectDescription, sizeof(m_NewProjectDescription), ImVec2(0, 80));

			ImGui::Text("位置：");
			ImGui::Text(m_NewProjectPath.string().c_str());
			ImGui::SameLine();
			if (ImGui::Button("浏览..."))
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

			if (ImGui::Button("取消", ImVec2(buttonWidth, 0)))
			{
				m_ShowNewProjectDialog = false;
				needsRefresh = true; // 下次打开时刷新
			}

			ImGui::SameLine();

			// 如果没有编辑器版本，禁用创建按钮
			if (m_Editers.empty())
			{
				ImGui::BeginDisabled();
			}

			if (ImGui::Button("创建", ImVec2(buttonWidth, 0)))
			{
				if (strlen(m_NewProjectName) > 0 && !m_Editers.empty())
				{
					ProjectConfig config;
					config.Name = m_NewProjectName;
					config.Description = m_NewProjectDescription;
					config.Version = "1.0.0";
					config.EditorVersion = m_Editers[selectedVersion];

					std::filesystem::path projectPath = m_NewProjectPath / m_NewProjectName / "Project.tcproj";
					auto project = ProjectManager::Get().CreateProject(projectPath, config);
					if (project)
					{
						m_Projects = ProjectManager::Get().GetProjects();
						m_ShowNewProjectDialog = false;
						needsRefresh = true;
					}
				}
			}

			if (m_Editers.empty())
			{
				ImGui::EndDisabled();
				ImGui::TextColored(ImVec4(1.0f, 0.5f, 0.5f, 1.0f),
					"无法创建项目：没有可用的编辑器版本");
			}

			ImGui::End();
		}
	}

			void ExampleLayer::RenderSettingsDialog()
	{
		const ImVec2 winSize(880.0f, 600.0f);
		ImGui::SetNextWindowSize(winSize, ImGuiCond_Always);
		ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(), ImGuiCond_Always, ImVec2(0.5f, 0.5f));
		ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 8.0f);
		ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(28.0f, 24.0f));
		ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 1.0f);
		ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0.16f, 0.16f, 0.16f, 1.0f));

		ImGuiWindowFlags flags = ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoMove;
		if (ImGui::Begin("设置", &m_ShowSettingsDialog, flags))
		{
			auto trunc = [](const std::string& text, float maxW) -> std::string
			{
				if (maxW <= 0.0f || ImGui::CalcTextSize(text.c_str()).x <= maxW)
					return text;
				std::string out = text;
				while (!out.empty())
				{
					out.pop_back();
					if (ImGui::CalcTextSize((out + "...").c_str()).x <= maxW)
						break;
				}
				return out + "...";
			};

			ImGui::Text("Settings");
			ImGui::Separator();
			ImGui::TextColored(ImVec4(0.60f, 0.60f, 0.60f, 1.0f),
				"配置 TomCat 引擎偏好与目录。");
			ImGui::Spacing();
			ImGui::Spacing();

			// Project Directory
			ImGui::Text("项目目录：");
			ImGui::SameLine(ImGui::GetContentRegionAvail().x - 116.0f);
			if (ImGui::Button("更改##ProjectDir", ImVec2(110.0f, 40.0f)))
			{
				std::string path = FileDialogs::OpenFolder();
				if (!path.empty())
				{
					ProjectManager::Get().SetProjectDirectory(path);
					m_Projects = ProjectManager::Get().GetProjects();
				}
			}
			ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.60f, 0.60f, 0.60f, 1.0f));
			ImGui::Text("%s", trunc(ProjectManager::Get().GetProjectDirectory().string(),
				ImGui::GetContentRegionAvail().x).c_str());
			ImGui::PopStyleColor();
			ImGui::Spacing();
			ImGui::Spacing();

			// Editor Directory
			ImGui::Text("编辑器目录：");
			ImGui::SameLine(ImGui::GetContentRegionAvail().x - 116.0f);
			if (ImGui::Button("更改##EditorDir", ImVec2(110.0f, 40.0f)))
			{
				std::string path = FileDialogs::OpenFolder();
				if (!path.empty())
					ProjectManager::Get().SetEditorDirectory(path);
			}
			ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.60f, 0.60f, 0.60f, 1.0f));
			ImGui::Text("%s", trunc(ProjectManager::Get().GetEditorDirectory().string(),
				ImGui::GetContentRegionAvail().x).c_str());
			ImGui::PopStyleColor();

			ImGui::Spacing();
			ImGui::Separator();
			ImGui::Text("引擎版本：1.0.0");
			ImGui::Text("构建：Release");
		}
		ImGui::End();

		ImGui::PopStyleColor();
		ImGui::PopStyleVar(3);
	}

}