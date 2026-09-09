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
		void SetEditorDirectory(const std::filesystem::path& directory);
		const std::filesystem::path& GetProjectDirectory() const { return m_ProjectDirectory; }
		const std::filesystem::path& GetEditorDirectory() const { return m_EditorDirectory; }

		std::vector<std::string> GetEditorDirectoryFiles() const;

		void ScanProjects();
		const std::vector<Ref<Project>>& GetProjects() const { return m_Projects; }

		Ref<Project> CreateProject(const std::filesystem::path& projectPath, const ProjectConfig& config);
		Ref<Project> LoadProject(const std::filesystem::path& projectPath);
		Ref<Project> AddProject(const std::filesystem::path& projectPath);
		bool RemoveProject(const std::filesystem::path& projectPath);

		Ref<Project> GetActiveProject() const { return m_ActiveProject; }
		void SetActiveProject(Ref<Project> project);
		void OpenProjectInEditor(Ref<Project> project);

		// Hub settings persistence in %LOCALAPPDATA%/TomCat/Hub/imgui.ini.
		// Projects added from other paths are serialized here (local serialization),
		// so no separate virtual-mount system is needed.
		void LoadHubSettings();
		bool SaveHubSettings();
		// Atomically persist the current HubConfig together with a serialized
		// ImGui layout. The Hub uses this overload to avoid a layout-only window
		// if the second write is interrupted.
		bool SaveHubSettings(const std::string& layoutIni);
		const std::vector<std::filesystem::path>& GetKnownProjectPaths() const { return m_KnownProjectPaths; }

		void RegisterProjectCreatedCallback(ProjectCallback callback) { m_OnProjectCreated = callback; }
		void RegisterProjectLoadedCallback(ProjectCallback callback) { m_OnProjectLoaded = callback; }
		void RegisterProjectRemovedCallback(ProjectCallback callback) { m_OnProjectRemoved = callback; }

	private:
		ProjectManager();
		~ProjectManager() = default;
		ProjectManager(const ProjectManager&) = delete;
		ProjectManager& operator=(const ProjectManager&) = delete;

		bool IsProjectFile(const std::filesystem::path& path) const;
		std::filesystem::path GetHubSettingsPath() const;

	private:
		std::filesystem::path m_ProjectDirectory;
		std::filesystem::path m_EditorDirectory;
		std::vector<std::filesystem::path> m_KnownProjectPaths;
		std::vector<Ref<Project>> m_Projects;
		Ref<Project> m_ActiveProject;

		ProjectCallback m_OnProjectCreated;
		ProjectCallback m_OnProjectLoaded;
		ProjectCallback m_OnProjectRemoved;
	};

}
