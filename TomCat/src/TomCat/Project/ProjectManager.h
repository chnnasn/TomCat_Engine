#pragma once

#include "Project.h"
#include <vector>
#include <functional>
#include <memory>

namespace TomCat {

	class ProjectManager
	{
	public:
		using ProjectCallback = std::function<void(Ref<Project>)>;

		static ProjectManager& Get();

		void SetProjectDirectory(const std::filesystem::path& directory);
		const std::filesystem::path& GetProjectDirectory() const { return m_ProjectDirectory; }

		void ScanProjects();
		const std::vector<Ref<Project>>& GetProjects() const { return m_Projects; }

		Ref<Project> CreateProject(const std::filesystem::path& projectPath, const ProjectConfig& config);
		Ref<Project> LoadProject(const std::filesystem::path& projectPath);
		bool RemoveProject(const std::filesystem::path& projectPath);

		Ref<Project> GetActiveProject() const { return m_ActiveProject; }
		void SetActiveProject(Ref<Project> project);
		void OpenProjectInEditor(Ref<Project> project);

		void RegisterProjectCreatedCallback(ProjectCallback callback) { m_OnProjectCreated = callback; }
		void RegisterProjectLoadedCallback(ProjectCallback callback) { m_OnProjectLoaded = callback; }
		void RegisterProjectRemovedCallback(ProjectCallback callback) { m_OnProjectRemoved = callback; }

	private:
		ProjectManager() = default;
		~ProjectManager() = default;
		ProjectManager(const ProjectManager&) = delete;
		ProjectManager& operator=(const ProjectManager&) = delete;

		bool IsProjectFile(const std::filesystem::path& path) const;

	private:
		std::filesystem::path m_ProjectDirectory;
		std::vector<Ref<Project>> m_Projects;
		Ref<Project> m_ActiveProject;

		ProjectCallback m_OnProjectCreated;
		ProjectCallback m_OnProjectLoaded;
		ProjectCallback m_OnProjectRemoved;
	};

}
