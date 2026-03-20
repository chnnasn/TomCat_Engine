#include "tcpch.h"
#include "Project.h"

#include <fstream>
#include <yaml-cpp/yaml.h>
#include <filesystem>

namespace TomCat {

	Project::Project(const std::filesystem::path& projectPath)
		: m_ProjectPath(projectPath)
	{
		if (std::filesystem::exists(projectPath))
		{
			m_ProjectDirectory = projectPath.parent_path();
			m_LastModified = std::filesystem::last_write_time(projectPath).time_since_epoch().count();
		}
	}

	std::filesystem::path Project::GetAssetPath() const
	{
		return m_ProjectDirectory / m_Config.AssetDirectory;
	}

	std::filesystem::path Project::GetScenePath() const
	{
		return m_ProjectDirectory / m_Config.SceneDirectory;
	}

	std::filesystem::path Project::GetScriptPath() const
	{
		return m_ProjectDirectory / m_Config.ScriptDirectory;
	}

	std::filesystem::path Project::GetStartScenePath() const
	{
		if (m_Config.StartScene.empty())
			return std::filesystem::path();
		return GetScenePath() / m_Config.StartScene;
	}

	void Project::SetStartScene(const std::string& sceneName)
	{
		m_Config.StartScene = sceneName;
	}

	void Project::UpdateLastModified()
	{
		m_LastModified = std::filesystem::last_write_time(m_ProjectPath).time_since_epoch().count();
	}

	Ref<Project> Project::CreateNew(const std::filesystem::path& projectPath, const ProjectConfig& config)
	{
		auto project = CreateRef<Project>();
		project->m_ProjectPath = projectPath;
		project->m_ProjectDirectory = projectPath.parent_path();
		project->m_Config = config;
		
		std::filesystem::create_directories(project->GetAssetPath());
		std::filesystem::create_directories(project->GetScenePath());
		std::filesystem::create_directories(project->GetScriptPath());
		
		project->Save();
		return project;
	}

	Ref<Project> Project::Load(const std::filesystem::path& projectPath)
	{
		if (!std::filesystem::exists(projectPath))
			return nullptr;

		try
		{
			YAML::Node data = YAML::LoadFile(projectPath.string());
			
			auto project = CreateRef<Project>(projectPath);
			
			auto configNode = data["Project"];
			if (configNode)
			{
				project->m_Config.Name = configNode["Name"] ? configNode["Name"].as<std::string>() : "Untitled Project";
				project->m_Config.Version = configNode["Version"] ? configNode["Version"].as<std::string>() : "1.0.0";
				project->m_Config.Description = configNode["Description"] ? configNode["Description"].as<std::string>() : "";
				project->m_Config.Author = configNode["Author"] ? configNode["Author"].as<std::string>() : "";
				project->m_Config.AssetDirectory = configNode["AssetDirectory"] ? configNode["AssetDirectory"].as<std::string>() : "Assets";
				project->m_Config.SceneDirectory = configNode["SceneDirectory"] ? configNode["SceneDirectory"].as<std::string>() : "Assets/Scenes";
				project->m_Config.ScriptDirectory = configNode["ScriptDirectory"] ? configNode["ScriptDirectory"].as<std::string>() : "Assets/Scripts";
				project->m_Config.StartScene = configNode["StartScene"] ? configNode["StartScene"].as<std::string>() : "";
			}
			
			project->UpdateLastModified();
			return project;
		}
		catch (const std::exception& e)
		{
			TC_Core_Error("Failed to load project: {0}", e.what());
			return nullptr;
		}
	}

	bool Project::Save()
	{
		try
		{
			YAML::Emitter out;
			out << YAML::BeginMap;
			out << YAML::Key << "Project" << YAML::Value;
			out << YAML::BeginMap;
			out << YAML::Key << "Name" << YAML::Value << m_Config.Name;
			out << YAML::Key << "Version" << YAML::Value << m_Config.Version;
			out << YAML::Key << "Description" << YAML::Value << m_Config.Description;
			out << YAML::Key << "Author" << YAML::Value << m_Config.Author;
			out << YAML::Key << "AssetDirectory" << YAML::Value << m_Config.AssetDirectory.string();
			out << YAML::Key << "SceneDirectory" << YAML::Value << m_Config.SceneDirectory.string();
			out << YAML::Key << "ScriptDirectory" << YAML::Value << m_Config.ScriptDirectory.string();
			out << YAML::Key << "StartScene" << YAML::Value << m_Config.StartScene;
			out << YAML::EndMap;
			out << YAML::EndMap;

			std::ofstream fout(m_ProjectPath.string());
			fout << out.c_str();
			
			m_LastModified = std::filesystem::last_write_time(m_ProjectPath).time_since_epoch().count();
			return true;
		}
		catch (const std::exception& e)
		{
			TC_Core_Error("Failed to save project: {0}", e.what());
			return false;
		}
	}

	bool Project::Reload()
	{
		return Load(m_ProjectPath) != nullptr;
	}

}
