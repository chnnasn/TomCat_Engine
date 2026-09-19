#include "ExampleLayer.h"
#include <imgui/imgui.h>

#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>
#include "TomCat/Core/Version.h"
#include "TomCat/Scene/SceneSerializer.h"
#include "TomCat/Utils/PlatformUtils.h"
#include "TomCat/Utils/PathUtils.h"
#include "TomCat/Project/ProjectManager.h"

#include "TomCat/Math/Math.h"
#include <shellapi.h>
#include <cstdio>
#include <cctype>
#include <algorithm>
#include <array>
#include <system_error>

namespace {

	std::string ToLowerString(const std::string& s)
	{
		std::string out = s;
		for (auto& c : out)
			c = (char)std::tolower((unsigned char)c);
		return out;
	}

	bool HasNonEmptyFile(const std::filesystem::path& path)
	{
		std::error_code error;
		if (!std::filesystem::is_regular_file(path, error) || error)
			return false;

		const auto size = std::filesystem::file_size(path, error);
		return !error && size > 0;
	}

	bool IsValidProjectDirectoryName(const std::string& name)
	{
		if (name.empty() || name == "." || name == "..")
			return false;

		static constexpr const char* invalidCharacters = "<>:\"/\\|?*";
		if (name.find_first_of(invalidCharacters) != std::string::npos ||
			static_cast<unsigned char>(name.back()) <= ' ' || name.back() == '.')
			return false;

		for (const unsigned char character : name)
		{
			if (character < 32)
				return false;
		}

		std::string baseName = name.substr(0, name.find('.'));
		std::transform(baseName.begin(), baseName.end(), baseName.begin(),
			[](unsigned char character) { return static_cast<char>(std::toupper(character)); });
		static constexpr std::array<const char*, 22> reservedNames = {
			"CON", "PRN", "AUX", "NUL",
			"COM1", "COM2", "COM3", "COM4", "COM5", "COM6", "COM7", "COM8", "COM9",
			"LPT1", "LPT2", "LPT3", "LPT4", "LPT5", "LPT6", "LPT7", "LPT8", "LPT9"
		};
		return std::none_of(reservedNames.begin(), reservedNames.end(),
			[&baseName](const char* reserved) { return baseName == reserved; });
	}

	bool WriteSampleScene(const std::filesystem::path& destination, const std::string& templateName)
	{
		// SceneSerializer owns the current schema version and field set. Generating
		// starter scenes through it prevents packaged templates from drifting.
		auto scene = TomCat::CreateRef<TomCat::Scene>();
		scene->SetSceneName("sample");
		TomCat::Entity mainCamera = scene->CreateEntityWithUUID(
			TomCat::UUID(1000000000000000001ULL), "MainCamera");
		auto& camera = mainCamera.AddComponent<TomCat::C_Camera>();
		if (templateName == "2D")
			camera._Camera.SetOrthographic(10.0f, 0.0f, 1000.0f);
		else
			camera._Camera.SetPerspective(glm::radians(45.0f), 0.01f, 1000.0f);

		TomCat::SceneSerializer serializer(scene);
		return serializer.Serialize(destination) && HasNonEmptyFile(destination);
	}

	bool EnsureSampleSceneAsset(const TomCat::Ref<TomCat::Project>& project, const std::string& templateName)
	{
		if (!project)
			return false;

		const std::filesystem::path destination = project->GetAssetPath() / "sample.tomcat";
		auto registerSample = [&]()
		{
			TomCat::AssetRegistry registry;
			if (!registry.Initialize(project->GetAssetPath(), project->GetLibraryPath()))
				return false;
			const TomCat::AssetHandle handle = registry.ImportAsset(destination);
			registry.Shutdown();
			if (static_cast<uint64_t>(handle) == 0)
				return false;
			project->SetStartSceneHandle(handle);
			return project->SetStartScene("sample.tomcat") && project->Save();
		};
		if (HasNonEmptyFile(destination))
		{
			if (!TomCat::SceneSerializer::ValidateCurrentFormat(destination))
			{
				TC_Core_Error("Existing starter scene is not in the current format: {0}",
					TomCat::PathToUTF8(destination));
				return false;
			}
			return registerSample();
		}

		std::error_code error;
		std::filesystem::create_directories(destination.parent_path(), error);
		if (error)
		{
			TC_Core_Error("Could not create the project's asset directory '{0}': {1}",
				TomCat::PathToUTF8(destination.parent_path()), error.message());
			return false;
		}

		return WriteSampleScene(destination, templateName) && registerSample();
	}


}

namespace TomCat {

	ExampleLayer::ExampleLayer()
		: Layer("FileManager"), m_SelectedMenu(0)
	{
		auto& projectManager = ProjectManager::Get();
		std::error_code currentPathError;
		const std::filesystem::path workingDirectory = std::filesystem::current_path(currentPathError);
		if (currentPathError)
			TC_Core_Warn("The current working directory could not be resolved: {0}", currentPathError.message());
		const std::filesystem::path projectDirectory = projectManager.GetProjectDirectory().empty()
			? workingDirectory / "Projects" : projectManager.GetProjectDirectory();
		if (!projectManager.SetProjectDirectory(projectDirectory))
			TC_Core_Warn("The configured Project directory could not be opened; keeping the previous Hub list");
		const std::filesystem::path editorDirectory = projectManager.GetEditorDirectory().empty()
			? workingDirectory / "Editors" : projectManager.GetEditorDirectory();
		if (!projectManager.SetEditorDirectory(editorDirectory))
			TC_Core_Warn("The configured Editor directory could not be opened");
		m_Projects = projectManager.GetProjects();
		
		if (auto editors = projectManager.GetEditorVersions())
			m_Editors = std::move(*editors);
		else
			m_Editors.clear();
	}

	void ExampleLayer::OnAttach()
	{
		TC_PROFILE_FUNCTION();

		// Use the same Unity editor palette as the editor executable.  The Hub has
		// a different layout, but sharing the palette keeps the two applications
		// visually consistent when switching between them.
		if (Application::Get().GetImGuiLayer())
			Application::Get().GetImGuiLayer()->SetDarkThemeColors();

		ImGuiStyle& style = ImGui::GetStyle();
		// Keep Hub content comfortably spaced while retaining Unity's compact,
		// square-cornered controls and dock chrome.
		style.WindowPadding = ImVec2(12.0f, 10.0f);
		style.FramePadding = ImVec2(5.0f, 4.0f);
		style.CellPadding = ImVec2(5.0f, 4.0f);
		style.ItemSpacing = ImVec2(8.0f, 6.0f);
		style.ItemInnerSpacing = ImVec2(6.0f, 4.0f);
		style.IndentSpacing = 20.0f;
		style.ScrollbarSize = 14.0f;
		style.WindowRounding = 0.0f;
		style.FrameRounding = 2.0f;
		style.PopupRounding = 2.0f;
		style.TabRounding = 2.0f;
		style.ScrollbarRounding = 0.0f;
		style.GrabRounding = 2.0f;
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

		// Unity-Hub style layout: dark narrow sidebar + main area.  The sidebar
		// intentionally uses the reference menu-bar tone rather than a separate
		// near-black color so the palette remains coherent.
		ImGui::PushStyleColor(ImGuiCol_ChildBg, ImGui::GetStyle().Colors[ImGuiCol_MenuBarBg]);
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
		const std::filesystem::path projectPath = FileDialogs::OpenFile("TomCat Project (*.tcproj)\0*.tcproj\0");
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
		memset(m_NewProjectDescription, 0, sizeof(m_NewProjectDescription));
		const auto& defaultDir = ProjectManager::Get().GetProjectDirectory();
		if (!defaultDir.empty())
		{
			m_NewProjectPath = defaultDir;
		}
		else
		{
			std::error_code currentPathError;
			const std::filesystem::path workingDirectory = std::filesystem::current_path(currentPathError);
			m_NewProjectPath = currentPathError ? std::filesystem::path{} : workingDirectory / "Projects";
			if (currentPathError)
				TC_Core_Warn("The default Project location could not be resolved: {0}", currentPathError.message());
		}
	}

	void ExampleLayer::OpenProject(Ref<Project> project)
	{
		if (project)
		{
			ProjectManager::Get().OpenProjectInEditor(project);
			if (ProjectManager::Get().ScanProjects())
				m_Projects = ProjectManager::Get().GetProjects();
		}
	}

	void ExampleLayer::RemoveProjectFromHub(Ref<Project> project)
	{
		if (project)
		{
			if (ProjectManager::Get().RemoveProject(project->GetProjectPath()))
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
			ImGui::PushStyleColor(ImGuiCol_Button,
				selected ? ImGui::GetStyle().Colors[ImGuiCol_HeaderActive] : ImVec4(0.0f, 0.0f, 0.0f, 0.0f));
			ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImGui::GetStyle().Colors[ImGuiCol_HeaderHovered]);
			ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImGui::GetStyle().Colors[ImGuiCol_HeaderActive]);
			ImGui::PushStyleColor(ImGuiCol_Text,
				selected ? ImGui::GetStyle().Colors[ImGuiCol_Text] : ImGui::GetStyle().Colors[ImGuiCol_TextDisabled]);
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
		ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImGui::GetStyle().Colors[ImGuiCol_FrameBgHovered]);
		ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImGui::GetStyle().Colors[ImGuiCol_FrameBg]);
		ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyle().Colors[ImGuiCol_Text]);
		ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(16.0f, 10.0f));
		if (ImGui::Button(T("设置", "Settings"), ImVec2(size.x - 24.0f, 52.0f)))
			m_ShowSettingsDialog = true;
		ImGui::PopStyleVar();
		ImGui::PopStyleColor(4);

		// Footer (centered to match the Settings button)
		const char* foot1 = T("TomCat 引擎", "TomCat Engine"); // TomCat ??
		const std::string foot2 = "v" + std::string(Version::ProductVersion) + "  |  Release";
		ImVec2 f1 = ImGui::CalcTextSize(foot1);
		ImVec2 f2 = ImGui::CalcTextSize(foot2.c_str());
		ImGui::SetCursorPos(ImVec2((size.x - f1.x) * 0.5f, size.y - 72.0f));
		ImGui::TextDisabled("%s", foot1);
		ImGui::SetCursorPos(ImVec2((size.x - f2.x) * 0.5f, size.y - 44.0f));
		ImGui::TextDisabled("%s", foot2.c_str());
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
				std::string hay = ToLowerString(project->GetName() + " " + PathToUTF8(project->GetProjectPath()));
				if (hay.find(query) == std::string::npos)
					continue;
			}
			m_VisibleProjects.push_back(project);
		}
		std::sort(m_VisibleProjects.begin(), m_VisibleProjects.end(),
			[this](const Ref<Project>& a, const Ref<Project>& b) { return SortProjects(a, b); });

		// Header: title + count
		ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyle().Colors[ImGuiCol_Text]);
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
		float searchX = std::max(pad,addX - 12.0f - searchW);
		float topY = size.x < 1150.0f ? pad+70.0f : pad;

		ImGui::SetCursorPos(ImVec2(searchX, topY));
		ImGui::SetNextItemWidth(std::max(80.0f,addX-12.0f-searchX));
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
		ImGui::PushStyleColor(ImGuiCol_Button, ImGui::GetStyle().Colors[ImGuiCol_HeaderActive]);
		ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImGui::GetStyle().Colors[ImGuiCol_NavHighlight]);
		ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImGui::GetStyle().Colors[ImGuiCol_HeaderActive]);
		ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyle().Colors[ImGuiCol_Text]);
		if (ImGui::Button(T("+ 新建项目", "+ New project"), ImVec2(newW, btnH)))
			NewProject();
		ImGui::PopStyleColor(4);

		// Column header (Name | Modified | Editor version)
		const float headerH = 44.0f;
		const float modifiedW = std::clamp(contentW*0.19f,90.0f,180.0f), versionW = std::clamp(contentW*0.17f,90.0f,160.0f), actionsW = 64.0f;
		float listTop = pad + 46.0f + 20.0f + btnH + 6.0f;
		float versionX = size.x - pad - actionsW - versionW;
		float modifiedX = versionX - modifiedW;

		ImVec2 headerMin(wmin.x + pad, wmin.y + listTop);
		ImVec2 headerMax(wmin.x + size.x - pad, wmin.y + listTop + headerH);
		dl->AddRectFilled(headerMin, headerMax, IM_COL32(40, 40, 40, 255));
		dl->AddRectFilled(ImVec2(headerMin.x, headerMax.y - 1.0f), headerMax, IM_COL32(85, 85, 85, 255));

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
				active ? ImGui::GetStyle().Colors[ImGuiCol_Text] : ImGui::GetStyle().Colors[ImGuiCol_TextDisabled]);
			ImGui::Text("%s", label);
			if (active)
			{
				ImGui::SameLine(0.0f, 4.0f);
				ImGui::Text("%s", arrow);
			}
			ImGui::PopStyleColor();
		};

		headerCell(T("名称", "Name"), HubSortColumn::Name, headerMin, contentW-modifiedW-versionW-actionsW, headerH);
		headerCell(T("修改时间", "Modified"), HubSortColumn::Modified, ImVec2(wmin.x + modifiedX, wmin.y + listTop), modifiedW, headerH);
		headerCell(T("编辑器版本", "Editor version"), HubSortColumn::EditorVersion, ImVec2(wmin.x + versionX, wmin.y + listTop), versionW, headerH);

		// Scrollable project list
		ImGui::SetCursorPos(ImVec2(pad, listTop + headerH));
		ImGui::BeginChild("ProjectListScroll", ImVec2(contentW, size.y - (listTop + headerH)), false, ImGuiWindowFlags_AlwaysVerticalScrollbar);
		{
			ImVec2 cmin = ImGui::GetCursorScreenPos();
            const float rowWidth = ImGui::GetContentRegionAvail().x;
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
				ImVec2 rowMax(rowMin.x + rowWidth, y + 96.0f);
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
		const float modifiedW = std::clamp(rowW*0.19f,90.0f,180.0f), versionW = std::clamp(rowW*0.17f,90.0f,160.0f), actionsW = 64.0f;
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

		// Keep custom-drawn rows on the shared Unity gray ramp.  Using the active
		// ImGui colors here avoids a second, subtly different palette for Hub rows.
		const ImU32 panelColor = ImGui::GetColorU32(ImGuiCol_WindowBg);
		const ImU32 hoverColor = ImGui::GetColorU32(ImGuiCol_FrameBgHovered);
		const ImU32 selectedColor = ImGui::GetColorU32(ImGuiCol_HeaderActive);
		const ImU32 borderColor = ImGui::GetColorU32(ImGuiCol_Border);
		ImU32 bg = selected ? selectedColor : (hovered ? hoverColor : panelColor);
		dl->AddRectFilled(rowMin, rowMax, bg);
		dl->AddRectFilled(ImVec2(rowMin.x, rowMax.y - 1.0f), rowMax, borderColor);
		if (selected)
			dl->AddRectFilled(ImVec2(rowMin.x, rowMin.y), ImVec2(rowMin.x + 3.0f, rowMax.y),
				ImGui::GetColorU32(ImGuiCol_HeaderHovered));

		// Project icon (gray square + first letter)
		ImVec2 iconMin(rowMin.x + 20.0f, rowMin.y + 24.0f);
		ImVec2 iconMax(iconMin.x + 48.0f, iconMin.y + 48.0f);
		dl->AddRectFilled(iconMin, iconMax, ImGui::GetColorU32(ImGuiCol_FrameBgActive), 2.0f);
		const std::string& projectName=project->GetName();
        size_t initialLength=projectName.empty()?0:1;
        while(initialLength<projectName.size() && (static_cast<unsigned char>(projectName[initialLength])&0xc0)==0x80) ++initialLength;
        std::string initial=projectName.empty()?"P":projectName.substr(0,initialLength);
		ImGui::SetCursorScreenPos(ImVec2(iconMin.x + 6.0f, iconMin.y + 4.0f));
		ImGui::Text("%s", initial.c_str());

		// Name + path
		float tx = rowMin.x + 20.0f + 48.0f + 16.0f;
		ImGui::SetCursorScreenPos(ImVec2(tx, rowMin.y + 14.0f));
		ImGui::Text("%s", project->GetName().c_str());

		float nameMaxW = (rowMax.x - actionsW - versionW - modifiedW) - tx - 8.0f;
		std::string path = PathToUTF8(project->GetProjectPath());
		if (ImGui::CalcTextSize(path.c_str()).x > nameMaxW)
		{
			std::string out = path;
			while (!out.empty())
			{
				size_t end=out.size()-1;
                    while(end>0 && (static_cast<unsigned char>(out[end])&0xc0)==0x80) --end;
                    out.resize(end);
				if (ImGui::CalcTextSize((out + "...").c_str()).x <= nameMaxW)
					break;
			}
			path = out + "...";
		}
		ImGui::SetCursorScreenPos(ImVec2(tx, rowMin.y + 54.0f));
		ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyle().Colors[ImGuiCol_TextDisabled]);
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
		ImGui::PushStyleColor(ImGuiCol_ButtonHovered, IM_COL32(98, 98, 98, 255));
		ImGui::PushStyleColor(ImGuiCol_ButtonActive, IM_COL32(112, 112, 112, 255));
		ImGui::PushStyleColor(ImGuiCol_Text, IM_COL32(196, 196, 196, 255));
		if (ImGui::Button(("\u22EE##menu_" + std::to_string(index)).c_str(), ImVec2(btnH, btnH)))
			m_MenuOpenRow = (m_MenuOpenRow == index) ? -1 : index; // toggle
		ImGui::PopStyleColor(4);

		// Custom context menu window (Open / Show in Explorer / Remove from Hub)
		if (m_MenuOpenRow == index)
		{
			ImGui::SetNextWindowPos(ImVec2(menuX + btnH, btnY + btnH), ImGuiCond_Appearing, ImVec2(1.0f, 0.0f));
			ImGui::SetNextWindowSize(ImVec2(200.0f, 0.0f), ImGuiCond_Appearing);
			ImGui::PushStyleColor(ImGuiCol_WindowBg, IM_COL32(60, 60, 60, 255));
			ImGui::PushStyleColor(ImGuiCol_Border, IM_COL32(85, 85, 85, 255));
			ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(10.0f, 8.0f));
			ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 1.0f);
			ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 2.0f);
			ImGuiWindowFlags mflags = ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove |
				ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoScrollbar;
			if (ImGui::Begin(("##project_menu_" + std::to_string(index)).c_str(), nullptr, mflags))
			{
				if (ImGui::IsMouseClicked(ImGuiMouseButton_Left) && !ImGui::IsWindowHovered(ImGuiHoveredFlags_AllowWhenBlockedByPopup))
					m_MenuOpenRow = -1;
				if (ImGui::MenuItem(T("\u6253\u5f00\u9879\u76ee", "Open project"))) { OpenProject(project); m_MenuOpenRow = -1; } // ????
				if (ImGui::MenuItem(T("\u5728\u8d44\u6e90\u7ba1\u7406\u5668\u4e2d\u6253\u5f00", "Show in Explorer"))) { OpenInExplorer(project); m_MenuOpenRow = -1; } // ?????????
				ImGui::Separator();
				if (ImGui::MenuItem(T("\u4ece Hub \u4e2d\u79fb\u9664", "Remove from Hub"))) { RemoveProjectFromHub(project); m_MenuOpenRow = -1; }
			}
			ImGui::End();
			ImGui::PopStyleVar(3);
			ImGui::PopStyleColor(2);
		}
	}

    void ExampleLayer::RenderNewProjectDialog()
    {
        static int selectedVersion = 0;
        static bool needsRefresh = true;
        static std::string creationError;
        if (needsRefresh)
        {
            if (auto editors = ProjectManager::Get().GetEditorVersions()) m_Editors = std::move(*editors);
            else m_Editors.clear();
            selectedVersion = 0;
            creationError.clear();
            needsRefresh = false;
        }
        ImGui::SetCursorPos(ImVec2(24,24));
        if (ImGui::Button(T("返回", "Back"))) { m_ShowNewProjectDialog=false; needsRefresh=true; }
        ImGui::SameLine(); ImGui::TextUnformatted(T("新建项目", "New Project"));
        ImGui::Separator();
        ImGui::BeginChild("NewProjectForm", ImVec2(0,-150), false);
        const int columns = ImGui::GetContentRegionAvail().x > 800 ? 2 : 1;
        if (ImGui::BeginTable("ProjectForm", columns, ImGuiTableFlags_SizingStretchSame | ImGuiTableFlags_PadOuterX))
        {
            ImGui::TableNextColumn();
            ImGui::TextUnformatted(T("编辑器版本 *", "Editor version *"));
            if (m_Editors.empty())
                ImGui::TextWrapped("%s", T("未找到编辑器版本，请在设置中检查编辑器目录。", "No editor versions found. Check Editor Directory in Settings."));
            else
            {
                selectedVersion=std::clamp(selectedVersion,0,static_cast<int>(m_Editors.size())-1);
                ImGui::SetNextItemWidth(-1);
                if (ImGui::BeginCombo("##EditorVersion",m_Editors[selectedVersion].c_str()))
                {
                    for (int i=0;i<static_cast<int>(m_Editors.size());++i)
                        if (ImGui::Selectable(m_Editors[i].c_str(),selectedVersion==i)) selectedVersion=i;
                    ImGui::EndCombo();
                }
            }
            ImGui::Spacing(); ImGui::TextUnformatted(T("模板", "Template"));
            if (ImGui::Selectable(T("2D 项目##Template2D", "2D project##Template2D"),m_NewProjectTemplate==0,0,ImVec2(0,48))) m_NewProjectTemplate=0;
            if (ImGui::Selectable(T("3D 项目##Template3D", "3D project##Template3D"),m_NewProjectTemplate==1,0,ImVec2(0,48))) m_NewProjectTemplate=1;
            ImGui::Spacing();
            ImGui::TableNextColumn();
            ImGui::TextUnformatted(T("项目名称 *", "Project name *"));
            ImGui::SetNextItemWidth(-1);
            ImGui::InputText("##Name",m_NewProjectName,sizeof(m_NewProjectName));
            ImGui::Spacing(); ImGui::TextUnformatted(T("位置 *", "Location *"));
            ImGui::TextWrapped("%s",PathToUTF8(m_NewProjectPath).c_str());
            if (ImGui::Button(T("浏览文件夹", "Browse folder")))
            {
                const auto path=FileDialogs::OpenFolder();
                if(!path.empty()) m_NewProjectPath=path;
            }
            ImGui::Spacing(); ImGui::TextUnformatted(T("描述", "Description"));
            ImGui::InputTextMultiline("##Description",m_NewProjectDescription,sizeof(m_NewProjectDescription),ImVec2(-1,100));
            ImGui::EndTable();
        }
        ImGui::EndChild();
        ImGui::Separator();
        const std::string name=m_NewProjectName;
        const bool validName=IsValidProjectDirectoryName(name);
        std::error_code targetError;
        const auto target=m_NewProjectPath/UTF8ToPath(name);
        const bool exists=validName && std::filesystem::exists(target,targetError);
        const bool canCreate=validName && !exists && !targetError && !m_Editors.empty();
        ImGui::BeginChild("ProjectValidation",ImVec2(0,76),false);
        if (!creationError.empty()) ImGui::TextWrapped("%s",creationError.c_str());
        else if(m_Editors.empty()) ImGui::TextWrapped("%s",T("无法创建：没有可用的编辑器版本。", "Cannot create: no editor version available."));
        else if(!validName) ImGui::TextWrapped("%s",T("请输入有效项目名，不能包含 Windows 保留字符或设备名。", "Enter a valid project name without reserved Windows characters or device names."));
        else if(exists) ImGui::TextWrapped("%s",T("目标项目目录已存在。", "The target project directory already exists."));
        else if(targetError) ImGui::TextWrapped("%s",targetError.message().c_str());
        else ImGui::TextWrapped("%s",PathToUTF8(target).c_str());
        ImGui::EndChild();
        if (ImGui::Button(T("取消", "Cancel"),ImVec2(130,44))) {m_ShowNewProjectDialog=false; needsRefresh=true;}
        ImGui::SameLine();
        ImGui::BeginDisabled(!canCreate);
        if (ImGui::Button(T("创建项目", "Create project"),ImVec2(180,44)))
        {
            ProjectConfig config;
            config.Name=name; config.Description=m_NewProjectDescription;
            config.Version=std::string(Version::ProductVersion);
            config.EditorVersion=m_Editors[selectedVersion];
            config.Template=m_NewProjectTemplate==0?"2D":"3D";
            auto project=ProjectManager::Get().CreateProject(target/"Project.tcproj",config);
            if(project && EnsureSampleSceneAsset(project,config.Template))
            {m_Projects=ProjectManager::Get().GetProjects();m_ShowNewProjectDialog=false;needsRefresh=true;}
            else creationError=T("项目创建未完成，请检查路径权限和日志。", "Project creation did not complete. Check folder permissions and logs.");
        }
        ImGui::EndDisabled();
    }

void ExampleLayer::RenderSettingsDialog()
	{
		const ImVec2 winSize(std::min(880.0f,ImGui::GetMainViewport()->WorkSize.x-24),std::min(600.0f,ImGui::GetMainViewport()->WorkSize.y-24));
		ImGui::SetNextWindowSize(winSize, ImGuiCond_Appearing);
        ImGui::SetNextWindowSizeConstraints(ImVec2(500,360),ImGui::GetMainViewport()->WorkSize);
		ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(), ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
		ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 2.0f);
		ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(28.0f, 24.0f));
		ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 1.0f);
		ImGui::PushStyleColor(ImGuiCol_WindowBg, ImGui::GetStyle().Colors[ImGuiCol_WindowBg]);

		ImGuiWindowFlags flags = ImGuiWindowFlags_NoCollapse;
		if (ImGui::Begin(T("\u8bbe\u7f6e", "Settings"), &m_ShowSettingsDialog, flags)) // ??
		{
			auto trunc = [](const std::string& text, float maxW) -> std::string
			{
				if (maxW <= 0.0f || ImGui::CalcTextSize(text.c_str()).x <= maxW)
					return text;
				std::string out = text;
				while (!out.empty())
				{
					size_t end=out.size()-1;
                    while(end>0 && (static_cast<unsigned char>(out[end])&0xc0)==0x80) --end;
                    out.resize(end);
					if (ImGui::CalcTextSize((out + "...").c_str()).x <= maxW)
						break;
				}
				return out + "...";
			};

			// Language switch (Chinese / English)
			ImGui::Text("%s", T("\u8bed\u8a00\uff1a", "Language:")); // ???
			ImGui::SameLine();
			ImGui::PushStyleColor(ImGuiCol_Button, m_Chinese ? ImGui::GetStyle().Colors[ImGuiCol_HeaderActive] : ImGui::GetStyle().Colors[ImGuiCol_Button]);
			ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImGui::GetStyle().Colors[ImGuiCol_HeaderHovered]);
			ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImGui::GetStyle().Colors[ImGuiCol_HeaderActive]);
			if (ImGui::Button("\u4e2d\u6587", ImVec2(90.0f, 48.0f))) // ??
				m_Chinese = true;
			ImGui::PopStyleColor(3);
			ImGui::SameLine();
			ImGui::PushStyleColor(ImGuiCol_Button, !m_Chinese ? ImGui::GetStyle().Colors[ImGuiCol_HeaderActive] : ImGui::GetStyle().Colors[ImGuiCol_Button]);
			ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImGui::GetStyle().Colors[ImGuiCol_HeaderHovered]);
			ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImGui::GetStyle().Colors[ImGuiCol_HeaderActive]);
			if (ImGui::Button("English", ImVec2(110.0f, 48.0f)))
				m_Chinese = false;
			ImGui::PopStyleColor(3);

			ImGui::Spacing();
			ImGui::Separator();
			ImGui::TextColored(ImGui::GetStyle().Colors[ImGuiCol_TextDisabled],
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
					const std::filesystem::path path = FileDialogs::OpenFolder();
					if (!path.empty())
					{
						const std::filesystem::path selectedPath = path;
						if (isProjectDir)
						{
							if (ProjectManager::Get().SetProjectDirectory(selectedPath))
								m_Projects = ProjectManager::Get().GetProjects();
						}
						else
						{
							if (ProjectManager::Get().SetEditorDirectory(selectedPath))
							{
								if (auto editors = ProjectManager::Get().GetEditorVersions())
									m_Editors = std::move(*editors);
								else
									m_Editors.clear();
							}
						}
					}
				}
				if (hov)
					ImGui::GetWindowDrawList()->AddRectFilled(rowStart, ImVec2(rowStart.x + rowSize.x, rowStart.y + rowSize.y), IM_COL32(71, 71, 71, 255), 2.0f);
				ImGui::SetCursorScreenPos(ImVec2(rowStart.x + 12.0f, rowStart.y + 10.0f));
				ImGui::Text("%s", T(zhLabel, enLabel));
				ImGui::SetCursorScreenPos(ImVec2(rowStart.x + 12.0f, rowStart.y + 46.0f));
				ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyle().Colors[ImGuiCol_TextDisabled]);
				ImGui::Text("%s", trunc(PathToUTF8(dir), rowSize.x - 24.0f).c_str());
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
			ImGui::Text("%s %.*s", T("\u5f15\u64ce\u7248\u672c\uff1a", "Engine Version:"),
				static_cast<int>(Version::ProductVersion.size()), Version::ProductVersion.data());
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
		std::error_code error;
		if (std::filesystem::is_directory(dir, error) && !error)
		{
			ShellExecuteW(NULL, L"open", dir.c_str(), NULL, NULL, SW_SHOWNORMAL);
		}
		else if (error)
		{
			TC_Core_Error("Could not inspect project directory '{0}': {1}",
				PathToUTF8(dir), error.message());
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

		ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyle().Colors[ImGuiCol_Text]);
		ImGui::SetCursorPos(ImVec2(pad, pad));
		ImGui::Text("%s", T("\u5b89\u88c5", "Installs")); // ??
		ImGui::PopStyleColor();

		char sub[96];
		snprintf(sub, sizeof(sub), "%s", T("\u5df2\u5b89\u88c5\u7684\u5f15\u64ce\u7248\u672c", "Installed engine versions")); // ????????
		ImGui::SetCursorPos(ImVec2(pad + 4.0f, pad + 46.0f));
		ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyle().Colors[ImGuiCol_TextDisabled]);
		ImGui::Text("%s", sub);
		ImGui::PopStyleColor();

		ImGui::SetCursorPos(ImVec2(pad, pad + 90.0f));
		ImGui::BeginChild("InstallsList", ImVec2(size.x - pad * 2.0f, size.y - (pad + 90.0f)), false, ImGuiWindowFlags_AlwaysVerticalScrollbar);
		{
			ImVec2 cmin = ImGui::GetCursorScreenPos();
			auto* dl = ImGui::GetWindowDrawList();
            float rowW = ImGui::GetContentRegionAvail().x;
			float y = cmin.y;

			if (m_Editors.empty())
			{
				ImGui::SetCursorScreenPos(ImVec2(cmin.x + 8.0f, cmin.y + 12.0f));
				ImGui::TextDisabled("%s", T("\u5c1a\u672a\u5b89\u88c5\u4efb\u4f55\u7248\u672c\u3002", "No versions installed yet.")); // ?????????
			}

			for (const auto& ver : m_Editors)
			{
				ImVec2 rowMin(cmin.x, y);
				ImVec2 rowMax(cmin.x + rowW, y + 72.0f);
				ImGui::SetCursorScreenPos(rowMin);
				ImGui::InvisibleButton(("##inst_" + ver).c_str(), ImVec2(rowW, 72.0f));
				bool hovered = ImGui::IsItemHovered();
				const ImU32 panelColor = ImGui::GetColorU32(ImGuiCol_WindowBg);
				const ImU32 hoverColor = ImGui::GetColorU32(ImGuiCol_FrameBgHovered);
				const ImU32 borderColor = ImGui::GetColorU32(ImGuiCol_Border);
				ImU32 bg = hovered ? hoverColor : panelColor;
				dl->AddRectFilled(rowMin, rowMax, bg, 2.0f);
				dl->AddRect(rowMin, rowMax, borderColor, 2.0f);

				// version (bright) + "Installed" tag on the right
				ImGui::SetCursorScreenPos(ImVec2(rowMin.x + 18.0f, rowMin.y + 12.0f));
				ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyle().Colors[ImGuiCol_Text]);
				ImGui::Text("%s", ver.c_str());
				ImGui::PopStyleColor();

				// editor path (truncated)
				std::filesystem::path ep = ProjectManager::Get().GetEditorDirectory() / ver;
				std::string eps = PathToUTF8(ep);
				float epMax = rowW - 150.0f;
				if (ImGui::CalcTextSize(eps.c_str()).x > epMax)
				{
					std::string o = eps;
					while (!o.empty())
					{
						size_t end=o.size()-1;
                        while(end>0 && (static_cast<unsigned char>(o[end])&0xc0)==0x80) --end;
                        o.resize(end);
						if (ImGui::CalcTextSize((o + "...").c_str()).x <= epMax)
							break;
					}
					eps = o + "...";
				}
				ImGui::SetCursorScreenPos(ImVec2(rowMin.x + 18.0f, rowMin.y + 40.0f));
				ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyle().Colors[ImGuiCol_TextDisabled]);
				ImGui::Text("%s", eps.c_str());
				ImGui::PopStyleColor();

				// "Installed" green badge (right side), sized for the 32px font
				const char* tag = T("\u5df2\u5b89\u88c5", "Installed"); // ???
				ImVec2 tagSz = ImGui::CalcTextSize(tag);
				const float tagH = 44.0f;
				ImVec2 tagMin(rowMax.x - tagSz.x - 36.0f, rowMin.y + (72.0f - tagH) * 0.5f);
				ImVec2 tagMax(tagMin.x + tagSz.x + 20.0f, tagMin.y + tagH);
				dl->AddRectFilled(tagMin, tagMax, IM_COL32(30, 90, 55, 255), 2.0f);
				dl->AddRect(tagMin, tagMax, IM_COL32(70, 180, 120, 255), 2.0f);
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
