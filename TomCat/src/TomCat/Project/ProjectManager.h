#pragma once

#include "Project.h"
#include <vector>
#include <memory>
#include <optional>
#include <unordered_map>
#include <unordered_set>

namespace TomCat {

	class ProjectManager
	{
	public:
		static ProjectManager& Get();

		[[nodiscard]] bool SetProjectDirectory(const std::filesystem::path& directory);
		[[nodiscard]] bool SetEditorDirectory(const std::filesystem::path& directory);
		const std::filesystem::path& GetProjectDirectory() const { return m_ProjectDirectory; }
		const std::filesystem::path& GetEditorDirectory() const { return m_EditorDirectory; }

		[[nodiscard]] std::optional<std::vector<std::string>> GetEditorDirectoryFiles() const;

		[[nodiscard]] bool ScanProjects();
		const std::vector<Ref<Project>>& GetProjects() const { return m_Projects; }

		Ref<Project> CreateProject(const std::filesystem::path& projectPath, const ProjectConfig& config);
		Ref<Project> LoadProject(const std::filesystem::path& projectPath);
		Ref<Project> AddProject(const std::filesystem::path& projectPath);
		[[nodiscard]] bool RemoveProject(const std::filesystem::path& projectPath);

		Ref<Project> GetActiveProject() const { return m_ActiveProject; }
		void OpenProjectInEditor(Ref<Project> project);

		// Non-layout Hub state lives in
		// %LOCALAPPDATA%/TomCat/TomCatSettings/hub.json.
		void LoadHubSettings();
		[[nodiscard]] bool SaveHubSettings();
	private:
		ProjectManager();
		~ProjectManager() = default;
		ProjectManager(const ProjectManager&) = delete;
		ProjectManager& operator=(const ProjectManager&) = delete;

		bool IsProjectFile(const std::filesystem::path& path) const;
		std::optional<std::filesystem::path> GetHubSettingsPath() const;
		bool ScanProjectsInternal();
		[[nodiscard]] bool RecordProjectOpened(const Ref<Project>& project);
		void ApplyStoredLastOpenedTime(const Ref<Project>& project,
			std::unordered_map<std::string, std::string>& lastOpenedTimes) const;

	private:
		std::filesystem::path m_ProjectDirectory;
		std::filesystem::path m_EditorDirectory;
		std::vector<std::filesystem::path> m_KnownProjectPaths;
		std::unordered_map<std::string, std::string> m_ProjectLastOpenedTimes;
		std::unordered_set<std::string> m_IgnoredProjectPaths;
		std::vector<Ref<Project>> m_Projects;
		Ref<Project> m_ActiveProject;
		bool m_HubSettingsWriteBlocked = false;
	};

}
