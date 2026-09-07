#include "tcpch.h"
#include "ProjectManager.h"

#include <filesystem>
#include <fstream>
#include <algorithm>
#include <unordered_set>
#include <sstream>
#include <yaml-cpp/yaml.h>

namespace TomCat {

	ProjectManager& ProjectManager::Get()
	{
		static ProjectManager instance;
		return instance;
	}

	ProjectManager::ProjectManager()
	{
		LoadHubSettings();
	}

	void ProjectManager::SetProjectDirectory(const std::filesystem::path& directory)
	{
		m_ProjectDirectory = directory;
		if (!std::filesystem::exists(directory))
		{
			std::filesystem::create_directories(directory);
		}

		SaveHubSettings();
		ScanProjects();
	}

	void ProjectManager::SetEditorDirectory(const std::filesystem::path& directory)
	{
		m_EditorDirectory = directory;
		if (!std::filesystem::exists(directory))
		{
			std::filesystem::create_directories(directory);
		}
		SaveHubSettings();
	}

	std::vector<std::string> ProjectManager::GetEditorDirectoryFiles() const
	{
		std::vector<std::string> fileNames;

		if (!std::filesystem::exists(m_EditorDirectory))
		{
			TC_Core_Error("Editor directory does not exist: {0}", m_EditorDirectory.string());
			return fileNames;
		}

		for (const auto& entry : std::filesystem::directory_iterator(m_EditorDirectory))
		{
			if (entry.is_directory())
			{
				fileNames.push_back(entry.path().filename().string());
			}
		}

		return fileNames;
	}

	void ProjectManager::ScanProjects()
	{
		m_Projects.clear();

		std::unordered_set<std::string> seen;

		auto addProjectFile = [&](const std::filesystem::path& file)
		{
			if (!std::filesystem::exists(file))
				return;

			std::string key = std::filesystem::absolute(file).lexically_normal().string();
			if (seen.find(key) != seen.end())
				return;

			auto project = Project::Load(file);
			if (project)
			{
				seen.insert(key);
				m_Projects.push_back(project);
			}
		};

		// 1. Scan the default project directory (one level deep).
		if (std::filesystem::exists(m_ProjectDirectory))
		{
			for (const auto& entry : std::filesystem::directory_iterator(m_ProjectDirectory))
			{
				if (!entry.is_directory())
					continue;

				for (const auto& file : std::filesystem::directory_iterator(entry.path()))
				{
					if (IsProjectFile(file.path()))
					{
						addProjectFile(file.path());
						break;
					}
				}
			}
		}

		// 2. Add projects serialized locally (added from any other path).
		for (const auto& path : m_KnownProjectPaths)
		{
			addProjectFile(path);
		}

		// 3. Sort by last opened time (descending).
		std::sort(m_Projects.begin(), m_Projects.end(),
			[](const Ref<Project>& a, const Ref<Project>& b) {
				return a->GetLastOperationTime() > b->GetLastOperationTime();
			});
	}

	Ref<Project> ProjectManager::CreateProject(const std::filesystem::path& projectPath, const ProjectConfig& config)
	{
		auto project = Project::CreateNew(projectPath, config);
		if (project)
		{
			m_Projects.push_back(project);

			// Remember the project even when it lives outside any mount, so it
			// stays in the list after a restart.
			auto normalized = std::filesystem::absolute(projectPath).lexically_normal();
			bool found = false;
			for (const auto& known : m_KnownProjectPaths)
			{
				if (std::filesystem::absolute(known).lexically_normal() == normalized)
				{
					found = true;
					break;
				}
			}
			if (!found)
			{
				m_KnownProjectPaths.push_back(projectPath);
				SaveHubSettings();
			}

			if (m_OnProjectCreated)
				m_OnProjectCreated(project);
		}
		return project;
	}

	Ref<Project> ProjectManager::LoadProject(const std::filesystem::path& projectPath)
	{
		auto project = Project::Load(projectPath);
		if (project)
		{
			// Opening a project records the last opened time.
			project->Touch();
			m_ActiveProject = project;
			if (m_OnProjectLoaded)
				m_OnProjectLoaded(project);
		}
		return project;
	}

	Ref<Project> ProjectManager::AddProject(const std::filesystem::path& projectPath)
	{
		auto project = Project::Load(projectPath);
		if (project)
		{
			auto normalized = std::filesystem::absolute(projectPath).lexically_normal();
			bool found = false;
			for (const auto& known : m_KnownProjectPaths)
			{
				if (std::filesystem::absolute(known).lexically_normal() == normalized)
				{
					found = true;
					break;
				}
			}
			if (!found)
			{
				m_KnownProjectPaths.push_back(projectPath);
				SaveHubSettings();
			}
			ScanProjects();
		}
		return project;
	}

	bool ProjectManager::RemoveProject(const std::filesystem::path& projectPath)
	{
		auto it = std::find_if(m_Projects.begin(), m_Projects.end(),
			[&projectPath](const Ref<Project>& p) {
				return p->GetProjectPath() == projectPath;
			});

		if (it != m_Projects.end())
		{
			if (m_OnProjectRemoved)
				m_OnProjectRemoved(*it);

			if (m_ActiveProject && m_ActiveProject->GetProjectPath() == projectPath)
				m_ActiveProject = nullptr;

			m_Projects.erase(it);

			// Also forget the project in the persisted known list.
			auto normalized = std::filesystem::absolute(projectPath).lexically_normal();
			m_KnownProjectPaths.erase(
				std::remove_if(m_KnownProjectPaths.begin(), m_KnownProjectPaths.end(),
					[&normalized](const std::filesystem::path& known)
					{
						return std::filesystem::absolute(known).lexically_normal() == normalized;
					}),
				m_KnownProjectPaths.end());
			SaveHubSettings();

			return true;
		}
		return false;
	}

	void ProjectManager::SetActiveProject(Ref<Project> project)
	{
		m_ActiveProject = project;
	}

	void ProjectManager::OpenProjectInEditor(Ref<Project> project)
	{
		if (!project)
			return;

		// Record the last opened time before launching the editor.
		project->Touch();

		std::filesystem::path editorPath = ProjectManager::Get().GetEditorDirectory() / project->GetEditorVersion() / "TomCat.exe";

		TC_Core_Info("Looking for editor at: {0}", editorPath.string());

		if (!std::filesystem::exists(editorPath))
		{
			char buffer[MAX_PATH];
			GetModuleFileNameA(NULL, buffer, MAX_PATH);
			std::filesystem::path exeDir = std::filesystem::path(buffer).parent_path();
			editorPath = exeDir / "TomCat.exe";
			TC_Core_Info("Fallback to: {0}", editorPath.string());
		}

		TC_Core_Info("Editor exists: {0}", std::filesystem::exists(editorPath));

		// Pass the project template explicitly so the editor can choose the
		// appropriate viewport camera interaction. Unknown/empty values remain 3D.
		const std::string templateName = project->GetConfig().Template == "2D" ? "2D" : "3D";
		std::string command = "\"" + editorPath.string() + "\" \"" + project->GetProjectPath().string() + "\" " + templateName;
		TC_Core_Info("Command: {0}", command);

		STARTUPINFOA si = { sizeof(si) };
		PROCESS_INFORMATION pi = {};

		std::string workingDir = editorPath.parent_path().string();

		if (CreateProcessA(NULL, const_cast<LPSTR>(command.c_str()), NULL, NULL,
			FALSE, 0, NULL, workingDir.c_str(), &si, &pi))
		{
			TC_Core_Info("Editor launched successfully");
			CloseHandle(pi.hProcess);
			CloseHandle(pi.hThread);
		}
		else
		{
			TC_Core_Error("Failed to open editor. Error code: {0}", GetLastError());
		}
	}

	bool ProjectManager::IsProjectFile(const std::filesystem::path& path) const
	{
		return path.extension() == ".tcproj" && path.filename() == "Project.tcproj";
	}


	std::filesystem::path ProjectManager::GetHubSettingsPath() const
	{
		// Hub settings live in <cwd>/imgui.ini under a custom [HubConfig] section,
		// kept alongside ImGui's window layout settings (no separate .tomcat file).
		return std::filesystem::current_path() / "imgui.ini";
	}

	void ProjectManager::LoadHubSettings()
	{
		m_ProjectDirectory.clear();
		m_EditorDirectory.clear();
		m_KnownProjectPaths.clear();

		std::filesystem::path iniPath = GetHubSettingsPath();
		bool sawSection = false;
		if (std::filesystem::exists(iniPath))
		{
			std::ifstream fin(iniPath);
			std::string line;
			bool inSection = false;
			while (std::getline(fin, line))
			{
				if (line == "[HubConfig]") { inSection = true; sawSection = true; continue; }
				if (inSection)
				{
					if (line.empty() || line[0] == '[') break;
					if (line.rfind("ProjectDirectory=", 0) == 0) m_ProjectDirectory = line.substr(17);
					else if (line.rfind("EditorDirectory=", 0) == 0) m_EditorDirectory = line.substr(16);
					else if (line.rfind("KnownProjects=", 0) == 0) m_KnownProjectPaths.emplace_back(line.substr(14));
				}
			}
		}

		// Legacy migration: if there is no [HubConfig] in imgui.ini yet, import the
		// old HubConfig.tomcat once and persist it to the new location.
		if (!sawSection)
		{
			std::filesystem::path legacyPath = std::filesystem::current_path() / "HubConfig.tomcat";
			if (std::filesystem::exists(legacyPath))
			{
				try
				{
					YAML::Node data = YAML::LoadFile(legacyPath.string());
					auto config = data["HubConfig"];
					if (config)
					{
						m_ProjectDirectory = config["ProjectDirectory"] ? config["ProjectDirectory"].as<std::string>() : "";
						m_EditorDirectory = config["EditorDirectory"] ? config["EditorDirectory"].as<std::string>() : "";
						if (config["KnownProjects"])
						{
							for (const auto& node : config["KnownProjects"])
							{
								m_KnownProjectPaths.emplace_back(node.as<std::string>());
							}
						}
						SaveHubSettings();
					}
				}
				catch (const std::exception& e)
				{
					TC_Core_Error("Failed to load legacy Hub settings: {0}", e.what());
				}
			}
		}
	}

	void ProjectManager::SaveHubSettings()
	{
		try
		{
			std::filesystem::path iniPath = GetHubSettingsPath();

			// Build the [HubConfig] section text.
			std::string section = "\n[HubConfig]\n";
			section += "ProjectDirectory=" + m_ProjectDirectory.string() + "\n";
			section += "EditorDirectory=" + m_EditorDirectory.string() + "\n";
			for (const auto& path : m_KnownProjectPaths)
			{
				section += "KnownProjects=" + path.string() + "\n";
			}

			// Read the current imgui.ini so other sections (window layout etc.)
			// are preserved, then replace only the [HubConfig] block.
			std::string ini;
			{
				std::ifstream fin(iniPath);
				if (fin)
				{
					std::stringstream ss;
					ss << fin.rdbuf();
					ini = ss.str();
				}
			}

			std::string::size_type pos = ini.find("[HubConfig]");
			if (pos != std::string::npos)
			{
				std::string::size_type next = ini.find("\n[", pos + 1);
				ini.erase(pos, (next == std::string::npos) ? std::string::npos : next - pos);
			}
			ini += section;

			std::ofstream fout(iniPath, std::ios::trunc);
			fout << ini;
		}
		catch (const std::exception& e)
		{
			TC_Core_Error("Failed to save Hub settings: {0}", e.what());
		}
	}

}
