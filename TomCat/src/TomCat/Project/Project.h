#pragma once

#include <string>
#include <filesystem>
#include <vector>
#include <ctime>

namespace TomCat {

	struct ProjectConfig
{
	std::string Name = "Untitled Project";
	std::string Version = "1.0.0";
	std::string Description = "";
	std::string EditorVersion = "";
	std::string Template = "3D"; // project template: "2D" or "3D"
	std::string LastOperationTime = "";
	std::filesystem::path AssetDirectory = "Assets";
	// Legacy fields used only to migrate personal Content Browser state from older
	// Project.tcproj files into UserSettings/imgui.ini. They are preserved while
	// migration is pending and cleared after the user INI has been written.
	std::string TwoColumnCurrentFolder = "";
	std::vector<std::string> ExpandedNodes;
	//std::string StartScene = "";
};

	class Project
	{
	public:
		Project() = default;
		Project(const std::filesystem::path& projectPath);

		const std::filesystem::path& GetProjectPath() const { return m_ProjectPath; }
		const ProjectConfig& GetConfig() const { return m_Config; }
		
		void SetConfig(const ProjectConfig& config) { m_Config = config; }
		
		const std::string& GetName() const { return m_Config.Name; }
		const std::string& GetEditorVersion() const { return m_Config.EditorVersion; }
		const std::string& GetLastOperationTime() const { return m_Config.LastOperationTime; }
		std::string GetLastOperationTimeAgo() const;
		void UpdateLastOperationTime();
		void Touch();

		std::filesystem::path GetAssetPath() const { return m_Directory / m_Config.AssetDirectory; };

		void SetStartScene(const std::string& sceneName);
		
		bool IsValid() const { return !m_ProjectPath.empty(); }
		
		static Ref<Project> CreateNew(const std::filesystem::path& projectPath, const ProjectConfig& config);
		static Ref<Project> Load(const std::filesystem::path& projectPath);
		bool Save();
		bool Reload();

	private:
		std::filesystem::path m_ProjectPath;
		std::filesystem::path m_Directory;
		ProjectConfig m_Config;
	};

}
