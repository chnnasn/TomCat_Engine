#pragma once

#include "TomCat.h"
#include "TomCat/Renderer/EditorCamera.h"
#include "TomCat/Project/Project.h"
#include "TomCat/Project/ProjectManager.h"
#include <string>
#include <filesystem>
#include <vector>
#include <imgui/imgui.h>


namespace TomCat {

	enum class HubSortColumn
	{
		Name,
		Modified,
		EditorVersion
	};

	class ExampleLayer : public Layer
	{
	public:
		ExampleLayer();

		virtual ~ExampleLayer() = default;

		virtual void OnAttach() override;
		virtual void OnDetach() override;

		void OnUpdate(Timestep ts) override;
		virtual void OnImGuiRender() override;
		void OnEvent(Event& e) override;
	private:
		void AddProject();
		void NewProject();
		void OpenProject(Ref<Project> project);
		void DeleteProject(Ref<Project> project);
		void RenderSidebar(const ImVec2& size);
		void RenderProjectList();
		void RenderProjectRow(Ref<Project> project, int index, const ImVec2& rowMin, const ImVec2& rowMax);
		void RenderInstallsPage(const ImVec2& size);
		void RenderNewProjectDialog();
		void RenderSettingsDialog();

		void OpenInExplorer(Ref<Project> project);
		const char* T(const char* zh, const char* en) const;
		std::string LocalizedRelativeTime(Ref<Project> project) const;

		bool SortProjects(const Ref<Project>& a, const Ref<Project>& b) const;

	private:
		int m_SelectedMenu = 1; // 1 = Projects, 2 = Installs
		int m_MenuOpenRow = -1; // which project row has its U+22EE menu open (-1 = none)
		int m_NewProjectTemplate = 1; // 0 = 2D, 1 = 3D
		bool m_Chinese = true;
		std::vector<Ref<Project>> m_Projects;
		std::vector<Ref<Project>> m_VisibleProjects;
		Ref<Project> m_SelectedProject;

		HubSortColumn m_SortColumn = HubSortColumn::Modified;
		bool m_SortAscending = false;
		char m_SearchBuffer[128] = "";

		bool m_ShowNewProjectDialog = false;
		bool m_ShowSettingsDialog = false;
		char m_NewProjectName[256] = "";
		char m_NewProjectAuthor[256] = "";
		char m_NewProjectDescription[512] = "";
		std::filesystem::path m_NewProjectPath;
	};
}