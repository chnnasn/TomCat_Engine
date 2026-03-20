#pragma once

#include "TomCat.h"
#include "TomCat/Renderer/EditorCamera.h"
#include "TomCat/Project/Project.h"
#include "TomCat/Project/ProjectManager.h"
#include <string>
#include <filesystem>
#include <vector>


namespace TomCat {

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
		void RenderProjectList();
		void RenderNewProjectDialog();
		void RenderStartSettings();

	private:
		int m_SelectedMenu;
		std::vector<Ref<Project>> m_Projects;
		Ref<Project> m_SelectedProject;
		
		bool m_ShowNewProjectDialog = false;
		char m_NewProjectName[256] = "";
		char m_NewProjectAuthor[256] = "";
		char m_NewProjectDescription[512] = "";
		std::filesystem::path m_NewProjectPath;
	};
}