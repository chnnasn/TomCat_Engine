#include "tcpch.h"
#include "Project.h"

#include <fstream>
#include <yaml-cpp/yaml.h>
#include <filesystem>
#include <chrono>
#include <iomanip>
#include <sstream>

namespace TomCat {

	Project::Project(const std::filesystem::path& projectPath)
		: m_ProjectPath(projectPath)
	{
		if (std::filesystem::exists(projectPath))
		{
			m_Directory = projectPath.parent_path();
		}
	}

	void Project::SetStartScene(const std::string& sceneName)
	{
		//m_Config.StartScene = sceneName;
	}




	void Project::UpdateLastOperationTime()
	{
		auto now = std::chrono::system_clock::now();
		auto now_time_t = std::chrono::system_clock::to_time_t(now);
		std::tm* localTime = std::localtime(&now_time_t);
		
		std::stringstream ss;
		ss << std::put_time(localTime, "%Y-%m-%d %H:%M:%S");
		m_Config.LastOperationTime = ss.str();
	}

	void Project::Touch()
	{
		UpdateLastOperationTime();
		Save();
	}

	std::string Project::GetLastOperationTimeAgo() const
	{
		if (m_Config.LastOperationTime.empty())
			return "Unknown";

		std::tm opTm = {};
		std::istringstream iss(m_Config.LastOperationTime);
		iss >> std::get_time(&opTm, "%Y-%m-%d %H:%M:%S");
		
		if (iss.fail())
			return "Unknown";

		std::time_t opTime = std::mktime(&opTm);
		std::time_t now = std::time(nullptr);
		
		double diff = std::difftime(now, opTime);
		
		if (diff < 0)
			return "Unknown";

		const double secondsPerMinute = 60.0;
		const double secondsPerHour = 3600.0;
		const double secondsPerDay = 86400.0;
		const double secondsPerMonth = 30.0 * secondsPerDay;
		const double secondsPerYear = 365.0 * secondsPerDay;

		if (diff < secondsPerMinute)
		{
			// Within a minute: show "Just now" (minute precision only)
			return "Just now";
		}
		else if (diff < secondsPerHour)
		{
			int minutes = static_cast<int>(diff / secondsPerMinute);
			return std::to_string(minutes) + (minutes == 1 ? " minute ago" : " minutes ago");
		}
		else if (diff < secondsPerDay)
		{
			int hours = static_cast<int>(diff / secondsPerHour);
			return std::to_string(hours) + (hours == 1 ? " hour ago" : " hours ago");
		}
		else if (diff < secondsPerMonth)
		{
			int days = static_cast<int>(diff / secondsPerDay);
			return std::to_string(days) + (days == 1 ? " day ago" : " days ago");
		}
		else if (diff < secondsPerYear)
		{
			int months = static_cast<int>(diff / secondsPerMonth);
			return std::to_string(months) + (months == 1 ? " month ago" : " months ago");
		}
		else
		{
			int years = static_cast<int>(diff / secondsPerYear);
			return std::to_string(years) + (years == 1 ? " year ago" : " years ago");
		}
	}

	Ref<Project> Project::CreateNew(const std::filesystem::path& projectPath, const ProjectConfig& config)
	{
		auto project = CreateRef<Project>();
		project->m_ProjectPath = projectPath;
		project-> m_Directory = projectPath.parent_path();
		project->m_Config = config;
		
		project->UpdateLastOperationTime();
		
		std::filesystem::create_directories(project->m_Directory / project->m_Config.AssetDirectory);
		
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
				project->m_Config.EditorVersion = configNode["EditorVersion"] ? configNode["EditorVersion"].as<std::string>() : "";
				project->m_Config.Template = configNode["Template"] ? configNode["Template"].as<std::string>() : "3D";
				project->m_Config.AssetDirectory = configNode["AssetDirectory"] ? configNode["AssetDirectory"].as<std::string>() : "Assets";
				project->m_Config.TwoColumnCurrentFolder = configNode["TwoColumnCurrentFolder"] ? configNode["TwoColumnCurrentFolder"].as<std::string>() : "";
				// 加载展开的节点
				if (configNode["ExpandedNodes"]) {
					for (const auto& node : configNode["ExpandedNodes"]) {
						project->m_Config.ExpandedNodes.push_back(node.as<std::string>());
					}
				}
				
				//project->m_Config.StartScene = configNode["StartScene"] ? configNode["StartScene"].as<std::string>() : "";
				project->m_Config.LastOperationTime = configNode["LastOperationTime"] ? configNode["LastOperationTime"].as<std::string>() : "";
			}
			
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
			out << YAML::Key << "EditorVersion" << YAML::Value << m_Config.EditorVersion;
			out << YAML::Key << "Template" << YAML::Value << m_Config.Template;
			out << YAML::Key << "AssetDirectory" << YAML::Value << m_Config.AssetDirectory.string();
			out << YAML::Key << "TwoColumnCurrentFolder" << YAML::Value << m_Config.TwoColumnCurrentFolder;
			// 保存展开的节点
			out << YAML::Key << "ExpandedNodes" << YAML::Value;
			out << YAML::BeginSeq;
			for (const auto& node : m_Config.ExpandedNodes) {
				out << YAML::Value << node;
			}
			out << YAML::EndSeq;
			
			//out << YAML::Key << "StartScene" << YAML::Value << m_Config.StartScene;
			out << YAML::Key << "LastOperationTime" << YAML::Value << m_Config.LastOperationTime;
			out << YAML::EndMap;
			out << YAML::EndMap;

			std::ofstream fout(m_ProjectPath.string());
			fout << out.c_str();
			
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
