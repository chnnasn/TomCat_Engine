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
		std::string Author = "";
		std::filesystem::path AssetDirectory = "Assets";
		std::filesystem::path SceneDirectory = "Assets/Scenes";
		std::filesystem::path ScriptDirectory = "Assets/Scripts";
		std::string StartScene = "";
	};

	class Project
	{
	public:
		Project() = default;
		Project(const std::filesystem::path& projectPath);

		const std::filesystem::path& GetProjectPath() const { return m_ProjectPath; }
		const std::filesystem::path& GetProjectDirectory() const { return m_ProjectDirectory; }
		const ProjectConfig& GetConfig() const { return m_Config; }
		
		void SetConfig(const ProjectConfig& config) { m_Config = config; }
		
		const std::string& GetName() const { return m_Config.Name; }
		const std::string& GetVersion() const { return m_Config.Version; }
		
		std::filesystem::path GetAssetPath() const;
		std::filesystem::path GetScenePath() const;
		std::filesystem::path GetScriptPath() const;
		
		std::filesystem::path GetStartScenePath() const;
		void SetStartScene(const std::string& sceneName);
		
		time_t GetLastModified() const { return m_LastModified; }
		void UpdateLastModified();
		
		bool IsValid() const { return !m_ProjectPath.empty(); }
		
		static Ref<Project> CreateNew(const std::filesystem::path& projectPath, const ProjectConfig& config);
		static Ref<Project> Load(const std::filesystem::path& projectPath);
		bool Save();
		bool Reload();

	private:
		std::filesystem::path m_ProjectPath;
		std::filesystem::path m_ProjectDirectory;
		ProjectConfig m_Config;
		time_t m_LastModified = 0;
	};

}
