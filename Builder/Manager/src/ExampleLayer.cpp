#include "ExampleLayer.h"
#include <imgui/imgui.h>

#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>
#include "TomCat/Scene/SceneSerializer.h"
#include "TomCat/Utils/PlatformUtils.h"
#include "TomCat/Project/ProjectManager.h"

#include "TomCat/Math/Math.h"
#include <fstream>
#include <shellapi.h>
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
		if (m_ShowNewProjectDialog)
			RenderNewProjectDialog(); // full page in the main area (Unity-Hub style)
		else if (m_SelectedMenu == 2)
			RenderInstallsPage(ImVec2(work.x - sidebarW, work.y));
		else
			RenderProjectList();
		ImGui::EndChild();

		ImGui::End();

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
		ImGui::TextDisabled("%s", T("引擎管理器", "ENGINE MANAGER")); // ?????

		ImGui::SetCursorPos(ImVec2(12.0f, 104.0f));
		ImGui::Separator();

		// Navigation
		auto navItem = [this, &size](const char* label, int index, float y)
		{
			bool selected = (m_SelectedMenu == index);
			ImGui::SetCursorPos(ImVec2(12.0f, y));
			ImGui::PushStyleColor(ImGuiCol_Header,
				selected ? ImVec4(0.24f, 0.24f, 0.24f, 1.0f) : ImVec4(0.0f, 0.0f, 0.0f, 0.0f));
			ImGui::PushStyleColor(ImGuiCol_HeaderHovered, ImVec4(0.20f, 0.20f, 0.20f, 1.0f));
			ImGui::PushStyleColor(ImGuiCol_HeaderActive, ImVec4(0.22f, 0.22f, 0.22f, 1.0f));
			ImGui::PushStyleColor(ImGuiCol_Text,
				selected ? ImVec4(0.95f, 0.95f, 0.95f, 1.0f) : ImVec4(0.72f, 0.72f, 0.72f, 1.0f));
			ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(16.0f, 10.0f));
			// Button centers its label automatically
			if (ImGui::Button(label, ImVec2(size.x - 24.0f, 52.0f)))
				m_SelectedMenu = index;
			ImGui::PopStyleVar();
			ImGui::PopStyleColor(4);
		};

		navItem(T("项目", "Projects"), 1, 120.0f);   // ??
		navItem(T("安装", "Installs"), 2, 120.0f + 58.0f); // ??

		// Settings button -> opens a dialog
		ImGui::SetCursorPos(ImVec2(12.0f, size.y - 130.0f));
		ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.0f, 0.0f, 0.0f, 0.0f));
		ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.20f, 0.20f, 0.20f, 1.0f));
		ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.22f, 0.22f, 0.22f, 1.0f));
		ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.85f, 0.85f, 0.85f, 1.0f));
		ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(16.0f, 10.0f));
		if (ImGui::Button(T("设置", "Settings"), ImVec2(size.x - 24.0f, 52.0f)))
			m_ShowSettingsDialog = true;
		ImGui::PopStyleVar();
		ImGui::PopStyleColor(4);

		// Footer (centered to match the Settings button)
		const char* foot1 = T("TomCat 引擎", "TomCat Engine"); // TomCat ??
		const char* foot2 = "v1.0.0  |  Release";
		ImVec2 f1 = ImGui::CalcTextSize(foot1);
		ImVec2 f2 = ImGui::CalcTextSize(foot2);
		ImGui::SetCursorPos(ImVec2((size.x - f1.x) * 0.5f, size.y - 72.0f));
		ImGui::TextDisabled("%s", foot1);
		ImGui::SetCursorPos(ImVec2((size.x - f2.x) * 0.5f, size.y - 44.0f));
		ImGui::TextDisabled("%s", foot2);
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
		ImGui::Text("%s", T("项目", "Projects"));
		ImGui::PopStyleColor();

		char sub[96];
		if (m_Chinese)
			snprintf(sub, sizeof(sub), "%zu 个项目", m_VisibleProjects.size()); // ???
		else
			snprintf(sub, sizeof(sub), "%zu project(s)", m_VisibleProjects.size());
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
		ImGui::InputTextWithHint("##HubSearch", T("搜索项目", "Search projects"), m_SearchBuffer, sizeof(m_SearchBuffer),
			ImGuiInputTextFlags_AutoSelectAll);

		ImGui::SetCursorPos(ImVec2(addX, topY));
		if (ImGui::Button(T("添加", "Add"), ImVec2(addW, btnH)))
			ImGui::OpenPopup("AddProjectMenu");
		if (ImGui::BeginPopup("AddProjectMenu"))
		{
			if (ImGui::Selectable(T("新建项目", "New project")))
				NewProject();
			if (ImGui::Selectable(T("从磁盘添加项目", "Add project from disk")))
				AddProject();
			ImGui::EndPopup();
		}

		ImGui::SetCursorPos(ImVec2(newX, topY));
		ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.27f, 0.55f, 0.92f, 1.0f));
		ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.20f, 0.45f, 0.85f, 1.0f));
		ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.15f, 0.38f, 0.78f, 1.0f));
		ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 1.0f, 1.0f, 1.0f));
		if (ImGui::Button(T("+ 新建项目", "+ New project"), ImVec2(newW, btnH)))
			NewProject();
		ImGui::PopStyleColor(4);

		// Column header (Name | Modified | Editor version)
		const float headerH = 44.0f;
		const float modifiedW = 180.0f, versionW = 160.0f, actionsW = 200.0f;
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
			// Center the Modified / Editor version headers (and their sort arrow) like the row data
			bool center = (col != HubSortColumn::Name);
			const char* arrow = active ? (m_SortAscending ? "\u2191" : "\u2193") : ""; // up / down arrow
			ImVec2 labelSz = ImGui::CalcTextSize(label);
			ImVec2 arrowSz = ImGui::CalcTextSize(arrow);
			float totalW = labelSz.x + (active ? arrowSz.x + 4.0f : 0.0f);
			float sx = center ? (pos.x + (w - totalW) * 0.5f) : (pos.x + 10.0f);
			ImGui::SetCursorScreenPos(ImVec2(sx, pos.y + 8.0f));
			ImGui::PushStyleColor(ImGuiCol_Text,
				active ? ImVec4(0.95f, 0.95f, 0.95f, 1.0f) : ImVec4(0.70f, 0.70f, 0.70f, 1.0f));
			ImGui::Text("%s", label);
			if (active)
			{
				ImGui::SameLine(0.0f, 4.0f);
				ImGui::Text("%s", arrow);
			}
			ImGui::PopStyleColor();
		};

		headerCell(T("名称", "Name"), HubSortColumn::Name, headerMin, contentW * 0.45f, headerH);
		headerCell(T("修改时间", "Modified"), HubSortColumn::Modified, ImVec2(wmin.x + modifiedX, wmin.y + listTop), modifiedW, headerH);
		headerCell(T("编辑器版本", "Editor version"), HubSortColumn::EditorVersion, ImVec2(wmin.x + versionX, wmin.y + listTop), versionW, headerH);

		// Scrollable project list
		ImGui::SetCursorPos(ImVec2(pad, listTop + headerH));
		ImGui::BeginChild("ProjectListScroll", ImVec2(contentW, size.y - (listTop + headerH)), false, ImGuiWindowFlags_AlwaysVerticalScrollbar);
		{
			ImVec2 cmin = ImGui::GetWindowPos();
			float y = cmin.y;

			if (m_VisibleProjects.empty())
			{
				const char* emptyText = m_SearchBuffer[0]
					? T("没有与搜索匹配的项目。", "No projects match your search.")
					: T("还没有项目。点击“添加”或“新建项目”开始。", "No projects yet. Use \"Add\" or \"New project\" to get started.");
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
		const float modifiedW = 180.0f, versionW = 160.0f, actionsW = 200.0f;
		const float menuZoneW = 64.0f; // keep the right zone free for the U+22EE button

		// Row click area excludes the menu-button zone so the button is never overlapped
		ImGui::InvisibleButton(("##project_row_" + std::to_string(index)).c_str(), ImVec2(rowW - menuZoneW, 96.0f));
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

		// Modified / Editor version columns: center text within the column width
		float versionX = rowMax.x - actionsW - versionW;
		float modifiedX = versionX - modifiedW;
		std::string rel = LocalizedRelativeTime(project);
		ImVec2 relSz = ImGui::CalcTextSize(rel.c_str());
		float relX = modifiedX + (modifiedW - relSz.x) * 0.5f;
		if (relX < modifiedX) relX = modifiedX;
		ImGui::SetCursorScreenPos(ImVec2(relX, rowMin.y + 32.0f));
		ImGui::Text("%s", rel.c_str());

		const std::string& ver = project->GetEditorVersion();
		ImVec2 verSz = ImGui::CalcTextSize(ver.c_str());
		float verX = versionX + (versionW - verSz.x) * 0.5f;
		if (verX < versionX) verX = versionX;
		ImGui::SetCursorScreenPos(ImVec2(verX, rowMin.y + 32.0f));
		ImGui::Text("%s", ver.c_str());

		// U+22EE (vertical ellipsis) menu button on the right
		const float btnH = 40.0f;
		float menuX = rowMax.x - 16.0f - btnH;
		float btnY = rowMin.y + (96.0f - btnH) * 0.5f;

		ImGui::SetCursorScreenPos(ImVec2(menuX, btnY));
		ImGui::PushStyleColor(ImGuiCol_Button, IM_COL32(0, 0, 0, 0));
		ImGui::PushStyleColor(ImGuiCol_ButtonHovered, IM_COL32(70, 70, 70, 255));
		ImGui::PushStyleColor(ImGuiCol_ButtonActive, IM_COL32(80, 80, 80, 255));
		ImGui::PushStyleColor(ImGuiCol_Text, IM_COL32(200, 200, 200, 255));
		if (ImGui::Button(("\u22EE##menu_" + std::to_string(index)).c_str(), ImVec2(btnH, btnH)))
			m_MenuOpenRow = (m_MenuOpenRow == index) ? -1 : index; // toggle
		ImGui::PopStyleColor(4);

		// Custom context menu window (Open / Show in Explorer / Delete)
		if (m_MenuOpenRow == index)
		{
			ImGui::SetNextWindowPos(ImVec2(menuX + btnH, btnY + btnH), ImGuiCond_Appearing, ImVec2(1.0f, 0.0f));
			ImGui::SetNextWindowSize(ImVec2(200.0f, 0.0f), ImGuiCond_Appearing);
			ImGui::PushStyleColor(ImGuiCol_WindowBg, IM_COL32(32, 32, 32, 255));
			ImGui::PushStyleColor(ImGuiCol_Border, IM_COL32(85, 85, 85, 255));
			ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(10.0f, 8.0f));
			ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 1.0f);
			ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 6.0f);
			ImGuiWindowFlags mflags = ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove |
				ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoScrollbar;
			if (ImGui::Begin(("##project_menu_" + std::to_string(index)).c_str(), nullptr, mflags))
			{
				if (ImGui::IsMouseClicked(ImGuiMouseButton_Left) && !ImGui::IsWindowHovered(ImGuiHoveredFlags_AllowWhenBlockedByPopup))
					m_MenuOpenRow = -1;
				if (ImGui::MenuItem(T("\u6253\u5f00\u9879\u76ee", "Open project"))) { OpenProject(project); m_MenuOpenRow = -1; } // ????
				if (ImGui::MenuItem(T("\u5728\u8d44\u6e90\u7ba1\u7406\u5668\u4e2d\u6253\u5f00", "Show in Explorer"))) { OpenInExplorer(project); m_MenuOpenRow = -1; } // ?????????
				ImGui::Separator();
				if (ImGui::MenuItem(T("\u5220\u9664", "Delete"))) { DeleteProject(project); m_MenuOpenRow = -1; } // ??
			}
			ImGui::End();
			ImGui::PopStyleVar(3);
			ImGui::PopStyleColor(2);
		}
	}

		void ExampleLayer::RenderNewProjectDialog()
	{
		// ??????
		static int selectedVersion = 0;
		static bool needsRefresh = true;
		if (needsRefresh)
		{
			m_Editers = ProjectManager::Get().GetEditorDirectoryFiles();
			needsRefresh = false;
			selectedVersion = 0;
		}

		auto* dl = ImGui::GetWindowDrawList();
		ImVec2 wmin = ImGui::GetWindowPos();
		ImVec2 size = ImGui::GetContentRegionAvail();
		const float pad = 24.0f;

		// Top bar: back button + title
		ImGui::SetCursorPos(ImVec2(pad, pad));
		ImGui::PushStyleColor(ImGuiCol_Button, IM_COL32(0, 0, 0, 0));
		ImGui::PushStyleColor(ImGuiCol_ButtonHovered, IM_COL32(60, 60, 60, 255));
		ImGui::PushStyleColor(ImGuiCol_ButtonActive, IM_COL32(70, 70, 70, 255));
		if (ImGui::Button(T("\u2190 \u8fd4\u56de", "\u2190 Back"), ImVec2(120.0f, 44.0f))) // ? ??
		{
			m_ShowNewProjectDialog = false;
			needsRefresh = true;
		}
		ImGui::PopStyleColor(3);

		ImGui::SetCursorPos(ImVec2(pad + 132.0f, pad + 6.0f));
		ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.95f, 0.95f, 0.95f, 1.0f));
		ImGui::Text("%s", T("\u65b0\u5efa\u9879\u76ee", "New Project")); // ????
		ImGui::PopStyleColor();

		// Unity-Hub style two-column layout: left = version + template, right = settings
		const float gap = 28.0f;
		float midW = (size.x - pad * 2.0f - gap) * 0.52f;
		float rightW = (size.x - pad * 2.0f - gap) * 0.48f;
		float midX = wmin.x + pad;
		float rightX = midX + midW + gap;
		float topY = wmin.y + 100.0f;

		// ============ Middle column: editor version + template ============
		ImGui::SetCursorScreenPos(ImVec2(midX, topY));
		ImGui::Text("%s", T("\u7f16\u8f91\u5668\u7248\u672c *", "Editor Version *")); // ????? *
		ImGui::SetCursorScreenPos(ImVec2(midX, topY + 42.0f));
		ImGui::SetNextItemWidth(midW);
		if (m_Editers.empty())
		{
			ImGui::TextColored(ImVec4(1.0f, 0.5f, 0.5f, 1.0f), "%s",
				T("\u672a\u627e\u5230\u7f16\u8f91\u5668\u7248\u672c\uff0c\u8bf7\u5728\u8bbe\u7f6e\u4e2d\u68c0\u67e5\u7f16\u8f91\u5668\u76ee\u5f55\u3002", "No editor versions found. Check Editor Directory in Settings."));
		}
		else
		{
			if (selectedVersion >= (int)m_Editers.size())
				selectedVersion = 0;
			if (ImGui::BeginCombo("##EditorVersion", m_Editers[selectedVersion].c_str()))
			{
				for (int i = 0; i < (int)m_Editers.size(); i++)
				{
					bool is_sel = (selectedVersion == i);
					if (ImGui::Selectable(m_Editers[i].c_str(), is_sel))
						selectedVersion = i;
					if (is_sel)
						ImGui::SetItemDefaultFocus();
				}
				ImGui::EndCombo();
			}
		}

		// Template (2D / 3D cards)
		float ty = topY + 42.0f + 62.0f;
		ImGui::SetCursorScreenPos(ImVec2(midX, ty));
		ImGui::Text("%s", T("\u6a21\u677f", "Template")); // ??
		float cardY = ty + 40.0f;
		float cardW = (midW - 12.0f) * 0.5f;
		float cardH = 110.0f;

		auto tmplCard = [&](int id, const char* label, const char* sub, float x, float y)
		{
			bool sel = (m_NewProjectTemplate == id);
			ImVec2 c0(x, y);
			ImVec2 c1(x + cardW, y + cardH);
			ImGui::SetCursorScreenPos(c0);
			ImGui::InvisibleButton(("##tmpl_" + std::to_string(id)).c_str(), ImVec2(cardW, cardH));
			bool hovered = ImGui::IsItemHovered();
			if (ImGui::IsItemClicked(ImGuiMouseButton_Left))
				m_NewProjectTemplate = id;
			ImU32 bg = hovered ? IM_COL32(56, 56, 56, 255) : IM_COL32(44, 44, 44, 255);
			dl->AddRectFilled(c0, c1, bg, 8.0f);
			dl->AddRect(c0, c1, sel ? IM_COL32(90, 170, 240, 255) : IM_COL32(72, 72, 72, 255), 8.0f, 0, sel ? 2.0f : 1.0f);
			// colored icon square
			dl->AddRectFilled(ImVec2(c0.x + 18.0f, c0.y + 18.0f), ImVec2(c0.x + 66.0f, c0.y + 66.0f),
				id == 0 ? IM_COL32(40, 120, 170, 255) : IM_COL32(60, 90, 200, 255), 6.0f);
			ImVec2 ls = ImGui::CalcTextSize(label);
			ImGui::PushStyleColor(ImGuiCol_Text, IM_COL32(240, 240, 240, 255));
			ImGui::SetCursorScreenPos(ImVec2(c0.x + 82.0f, c0.y + 24.0f));
			ImGui::Text("%s", label);
			ImGui::PopStyleColor();
			ImVec2 ss = ImGui::CalcTextSize(sub);
			ImGui::PushStyleColor(ImGuiCol_Text, IM_COL32(150, 150, 150, 255));
			ImGui::SetCursorScreenPos(ImVec2(c0.x + 82.0f, c0.y + 62.0f));
			ImGui::Text("%s", sub);
			ImGui::PopStyleColor();
		};
		tmplCard(0, "2D", T("2D \u5de5\u7a0b", "2D project"), midX, cardY); // 2D ??
		tmplCard(1, "3D", T("3D \u5de5\u7a0b", "3D project"), midX + cardW + 12.0f, cardY); // 3D ??

		// ============ Right column: name / location / create ============
		ImGui::SetCursorScreenPos(ImVec2(rightX, topY));
		ImGui::Text("%s", T("\u9879\u76ee\u540d\u79f0 *", "Project Name *")); // ???? *
		ImGui::SetCursorScreenPos(ImVec2(rightX, topY + 42.0f));
		ImGui::SetNextItemWidth(rightW);
		ImGui::InputText("##Name", m_NewProjectName, sizeof(m_NewProjectName));

		float y2 = topY + 42.0f + 64.0f;
		ImGui::SetCursorScreenPos(ImVec2(rightX, y2));
		ImGui::Text("%s", T("\u4f4d\u7f6e *", "Location *")); // ?? *
		y2 += 42.0f;
		float browseW = 100.0f;
		std::string loc = m_NewProjectPath.string();
		float locMax = rightW - browseW - 10.0f;
		if (ImGui::CalcTextSize(loc.c_str()).x > locMax)
		{
			std::string o = loc;
			while (!o.empty())
			{
				o.pop_back();
				if (ImGui::CalcTextSize((o + "...").c_str()).x <= locMax)
					break;
			}
			loc = o + "...";
		}
		ImGui::SetCursorScreenPos(ImVec2(rightX, y2 + 6.0f));
		ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.70f, 0.70f, 0.70f, 1.0f));
		ImGui::Text("%s", loc.c_str());
		ImGui::PopStyleColor();
		ImGui::SetCursorScreenPos(ImVec2(rightX + rightW - browseW, y2));
		ImGui::PushStyleColor(ImGuiCol_Button, IM_COL32(70, 70, 70, 255));
		ImGui::PushStyleColor(ImGuiCol_ButtonHovered, IM_COL32(85, 85, 85, 255));
		if (ImGui::Button(T("\u6d4f\u89c8", "Browse"), ImVec2(browseW, 44.0f))) // ??
		{
			std::string path = FileDialogs::OpenFolder();
			if (!path.empty())
				m_NewProjectPath = path;
		}
		ImGui::PopStyleColor(2);

		// Description
		y2 += 64.0f;
		ImGui::SetCursorScreenPos(ImVec2(rightX, y2));
		ImGui::Text("%s", T("\u63cf\u8ff0", "Description")); // ??
		y2 += 42.0f;
		ImGui::SetCursorScreenPos(ImVec2(rightX, y2));
		ImGui::SetNextItemWidth(rightW);
		ImGui::InputTextMultiline("##Description", m_NewProjectDescription, sizeof(m_NewProjectDescription), ImVec2(rightW, 90.0f));

		// Create / Cancel buttons (bottom right)
		const float btnW = 150.0f, btnH = 48.0f;
		float btnY = wmin.y + size.y - pad - btnH;
		float createX = wmin.x + size.x - pad - btnW;
		float cancelX = createX - 12.0f - btnW;

		ImGui::SetCursorScreenPos(ImVec2(cancelX, btnY));
		ImGui::PushStyleColor(ImGuiCol_Button, IM_COL32(70, 70, 70, 255));
		ImGui::PushStyleColor(ImGuiCol_ButtonHovered, IM_COL32(85, 85, 85, 255));
		if (ImGui::Button(T("\u53d6\u6d88", "Cancel"), ImVec2(btnW, btnH))) // ??
		{
			m_ShowNewProjectDialog = false;
			needsRefresh = true;
		}
		ImGui::PopStyleColor(2);

		bool canCreate = (strlen(m_NewProjectName) > 0) && !m_Editers.empty();
		ImGui::SetCursorScreenPos(ImVec2(createX, btnY));
		ImGui::PushStyleColor(ImGuiCol_Button, canCreate ? IM_COL32(70, 130, 220, 255) : IM_COL32(60, 60, 60, 255));
		ImGui::PushStyleColor(ImGuiCol_ButtonHovered, canCreate ? IM_COL32(60, 115, 200, 255) : IM_COL32(60, 60, 60, 255));
		if (ImGui::Button(T("\u521b\u5efa\u9879\u76ee", "Create Project"), ImVec2(btnW, btnH)) && canCreate) // ????
		{
			ProjectConfig config;
			config.Name = m_NewProjectName;
			config.Description = m_NewProjectDescription;
			config.Version = "1.0.0";
			config.EditorVersion = m_Editers[selectedVersion];
			config.Template = (m_NewProjectTemplate == 0) ? "2D" : "3D";
			std::filesystem::path projectPath = m_NewProjectPath / m_NewProjectName / "Project.tcproj";
			auto project = ProjectManager::Get().CreateProject(projectPath, config);
			if (project)
			{
				m_Projects = ProjectManager::Get().GetProjects();
				m_ShowNewProjectDialog = false;
				needsRefresh = true;
			}
		}
		ImGui::PopStyleColor(2);

		if (m_Editers.empty())
		{
			ImGui::SetCursorScreenPos(ImVec2(rightX, btnY - 34.0f));
			ImGui::TextColored(ImVec4(1.0f, 0.5f, 0.5f, 1.0f), "%s",
				T("\u65e0\u6cd5\u521b\u5efa\u9879\u76ee\uff1a\u6ca1\u6709\u53ef\u7528\u7684\u7f16\u8f91\u5668\u7248\u672c", "Cannot create project: no editor version available"));
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
		if (ImGui::Begin(T("\u8bbe\u7f6e", "Settings"), &m_ShowSettingsDialog, flags)) // ??
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

			// Language switch (Chinese / English)
			ImGui::Text("%s", T("\u8bed\u8a00\uff1a", "Language:")); // ???
			ImGui::SameLine();
			ImGui::PushStyleColor(ImGuiCol_Button, m_Chinese ? ImVec4(0.27f, 0.55f, 0.92f, 1.0f) : ImVec4(0.44f, 0.44f, 0.44f, 1.0f));
			ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.30f, 0.55f, 0.90f, 1.0f));
			ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.25f, 0.50f, 0.85f, 1.0f));
			if (ImGui::Button("\u4e2d\u6587", ImVec2(90.0f, 48.0f))) // ??
				m_Chinese = true;
			ImGui::PopStyleColor(3);
			ImGui::SameLine();
			ImGui::PushStyleColor(ImGuiCol_Button, !m_Chinese ? ImVec4(0.27f, 0.55f, 0.92f, 1.0f) : ImVec4(0.44f, 0.44f, 0.44f, 1.0f));
			ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.30f, 0.55f, 0.90f, 1.0f));
			ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.25f, 0.50f, 0.85f, 1.0f));
			if (ImGui::Button("English", ImVec2(110.0f, 48.0f)))
				m_Chinese = false;
			ImGui::PopStyleColor(3);

			ImGui::Spacing();
			ImGui::Separator();
			ImGui::TextColored(ImVec4(0.60f, 0.60f, 0.60f, 1.0f),
				T("\u914d\u7f6e TomCat \u5f15\u64ce\u504f\u597d\u4e0e\u76ee\u5f55\u3002", "Configure TomCat Engine preferences and directories.")); // ?? TomCat ????????
			ImGui::Spacing();
			ImGui::Spacing();

			// Clickable path rows (no Change button; click the row to change)
			auto dirRow = [this, &trunc, &winSize](const char* zhLabel, const char* enLabel, const std::filesystem::path& dir,
				const char* id, bool isProjectDir)
			{
				ImVec2 rowStart = ImGui::GetCursorScreenPos();
				ImVec2 rowSize(winSize.x - 56.0f, 84.0f);
				ImGui::InvisibleButton(id, rowSize);
				bool hov = ImGui::IsItemHovered();
				if (ImGui::IsItemClicked(ImGuiMouseButton_Left))
				{
					std::string path = FileDialogs::OpenFolder();
					if (!path.empty())
					{
						if (isProjectDir)
						{
							ProjectManager::Get().SetProjectDirectory(path);
							m_Projects = ProjectManager::Get().GetProjects();
						}
						else
						{
							ProjectManager::Get().SetEditorDirectory(path);
						}
					}
				}
				if (hov)
					ImGui::GetWindowDrawList()->AddRectFilled(rowStart, ImVec2(rowStart.x + rowSize.x, rowStart.y + rowSize.y), IM_COL32(52, 52, 52, 255), 6.0f);
				ImGui::SetCursorScreenPos(ImVec2(rowStart.x + 12.0f, rowStart.y + 10.0f));
				ImGui::Text("%s", T(zhLabel, enLabel));
				ImGui::SetCursorScreenPos(ImVec2(rowStart.x + 12.0f, rowStart.y + 46.0f));
				ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.70f, 0.70f, 0.70f, 1.0f));
				ImGui::Text("%s", trunc(dir.string(), rowSize.x - 24.0f).c_str());
				ImGui::PopStyleColor();
				ImGui::Spacing();
				ImGui::Spacing();
			};

			dirRow("\u9879\u76ee\u76ee\u5f55\uff1a", "Project Directory:", // ?????
				ProjectManager::Get().GetProjectDirectory(), "##proj_dir_row", true);
			dirRow("\u7f16\u8f91\u5668\u76ee\u5f55\uff1a", "Editor Directory:", // ??????
				ProjectManager::Get().GetEditorDirectory(), "##editor_dir_row", false);

			ImGui::Spacing();
			ImGui::Separator();
			ImGui::Text("%s", T("\u5f15\u64ce\u7248\u672c\uff1a1.0.0", "Engine Version: 1.0.0")); // ?????1.0.0
			ImGui::Text("%s", T("\u6784\u5efa\uff1aRelease", "Build: Release")); // ???Release
		}
		ImGui::End();

		ImGui::PopStyleColor();
		ImGui::PopStyleVar(3);
	}


	const char* ExampleLayer::T(const char* zh, const char* en) const
	{
		return m_Chinese ? zh : en;
	}

	void ExampleLayer::OpenInExplorer(Ref<Project> project)
	{
		if (!project)
			return;
		std::filesystem::path dir = project->GetProjectPath().parent_path();
		if (std::filesystem::exists(dir))
		{
			ShellExecuteA(NULL, "open", dir.string().c_str(), NULL, NULL, SW_SHOWNORMAL);
		}
	}

	std::string ExampleLayer::LocalizedRelativeTime(Ref<Project> project) const
	{
		if (!project)
			return "";
		std::string en = project->GetLastOperationTimeAgo();
		if (!m_Chinese)
			return en;
		if (en == "Just now")
			return "近期"; // ??
		if (en == "Unknown")
			return "未知"; // ??

		auto replaceUnit = [](const std::string& in, const char* enUnit, const char* zhUnit) -> std::string
		{
			size_t p = in.find(enUnit);
			if (p == std::string::npos)
				return "";
			std::string n = in.substr(0, p);
			while (!n.empty() && n.back() == ' ')
				n.pop_back();
			return n + " " + zhUnit;
		};

		const char* units[][2] = {
			{ "minute", "分钟前" }, // ???
			{ "hour", "小时前" },   // ???
			{ "day", "天前" },          // ??
			{ "month", "个月前" },  // ???
			{ "year", "年前" },         // ??
		};
		for (auto& u : units)
		{
			std::string r = replaceUnit(en, u[0], u[1]);
			if (!r.empty())
				return r;
		}
		return en;
	}

		void ExampleLayer::RenderInstallsPage(const ImVec2& size)
	{
		auto* dl = ImGui::GetWindowDrawList();
		ImVec2 wmin = ImGui::GetWindowPos();
		const float pad = 24.0f;

		ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.95f, 0.95f, 0.95f, 1.0f));
		ImGui::SetCursorPos(ImVec2(pad, pad));
		ImGui::Text("%s", T("\u5b89\u88c5", "Installs")); // ??
		ImGui::PopStyleColor();

		char sub[96];
		snprintf(sub, sizeof(sub), "%s", T("\u5df2\u5b89\u88c5\u7684\u5f15\u64ce\u7248\u672c", "Installed engine versions")); // ????????
		ImGui::SetCursorPos(ImVec2(pad + 4.0f, pad + 46.0f));
		ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.78f, 0.78f, 0.78f, 1.0f));
		ImGui::Text("%s", sub);
		ImGui::PopStyleColor();

		ImGui::SetCursorPos(ImVec2(pad, pad + 90.0f));
		ImGui::BeginChild("InstallsList", ImVec2(size.x - pad * 2.0f, size.y - (pad + 90.0f)), false, ImGuiWindowFlags_AlwaysVerticalScrollbar);
		{
			ImVec2 cmin = ImGui::GetWindowPos();
			float rowW = size.x - pad * 2.0f;
			float y = cmin.y;

			if (m_Editers.empty())
			{
				ImGui::SetCursorScreenPos(ImVec2(cmin.x + 8.0f, cmin.y + 12.0f));
				ImGui::TextDisabled("%s", T("\u5c1a\u672a\u5b89\u88c5\u4efb\u4f55\u7248\u672c\u3002", "No versions installed yet.")); // ?????????
			}

			for (const auto& ver : m_Editers)
			{
				ImVec2 rowMin(cmin.x, y);
				ImVec2 rowMax(cmin.x + rowW, y + 72.0f);
				ImGui::SetCursorScreenPos(rowMin);
				ImGui::InvisibleButton(("##inst_" + ver).c_str(), ImVec2(rowW, 72.0f));
				bool hovered = ImGui::IsItemHovered();
				ImU32 bg = hovered ? IM_COL32(54, 54, 54, 255) : IM_COL32(44, 44, 44, 255);
				dl->AddRectFilled(rowMin, rowMax, bg, 6.0f);
				dl->AddRect(rowMin, rowMax, IM_COL32(72, 72, 72, 255), 6.0f);

				// version (bright) + "Installed" tag on the right
				ImGui::SetCursorScreenPos(ImVec2(rowMin.x + 18.0f, rowMin.y + 12.0f));
				ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.95f, 0.95f, 0.95f, 1.0f));
				ImGui::Text("%s", ver.c_str());
				ImGui::PopStyleColor();

				// editor path (truncated)
				std::filesystem::path ep = ProjectManager::Get().GetEditorDirectory() / ver;
				std::string eps = ep.string();
				float epMax = rowW - 150.0f;
				if (ImGui::CalcTextSize(eps.c_str()).x > epMax)
				{
					std::string o = eps;
					while (!o.empty())
					{
						o.pop_back();
						if (ImGui::CalcTextSize((o + "...").c_str()).x <= epMax)
							break;
					}
					eps = o + "...";
				}
				ImGui::SetCursorScreenPos(ImVec2(rowMin.x + 18.0f, rowMin.y + 40.0f));
				ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.62f, 0.62f, 0.62f, 1.0f));
				ImGui::Text("%s", eps.c_str());
				ImGui::PopStyleColor();

				// "Installed" green badge (right side), sized for the 32px font
				const char* tag = T("\u5df2\u5b89\u88c5", "Installed"); // ???
				ImVec2 tagSz = ImGui::CalcTextSize(tag);
				const float tagH = 44.0f;
				ImVec2 tagMin(rowMax.x - tagSz.x - 36.0f, rowMin.y + (72.0f - tagH) * 0.5f);
				ImVec2 tagMax(tagMin.x + tagSz.x + 20.0f, tagMin.y + tagH);
				dl->AddRectFilled(tagMin, tagMax, IM_COL32(30, 90, 55, 255), 6.0f);
				dl->AddRect(tagMin, tagMax, IM_COL32(70, 180, 120, 255), 6.0f);
				ImGui::PushStyleColor(ImGuiCol_Text, IM_COL32(120, 220, 160, 255));
				ImGui::SetCursorScreenPos(ImVec2(tagMin.x + 10.0f, tagMin.y + (tagH - tagSz.y) * 0.5f));
				ImGui::Text("%s", tag);
				ImGui::PopStyleColor();

				y += 72.0f + 8.0f;
			}
		}
		ImGui::EndChild();
	}

}
