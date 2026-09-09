#include "tcpch.h"
#include "ProjectManager.h"

#include <filesystem>
#include <fstream>
#include <algorithm>
#include <unordered_set>
#include <sstream>
#include <yaml-cpp/yaml.h>
#include "TomCat/ImGui/ImGuiSettings.h"

namespace {

	struct IniSectionRange
	{
		std::string::size_type Begin = std::string::npos;
		std::string::size_type End = std::string::npos;

		explicit operator bool() const { return Begin != std::string::npos; }
	};

	std::string ReadTextFile(const std::filesystem::path& path)
	{
		std::ifstream input(path, std::ios::binary);
		if (!input)
			return {};

		std::ostringstream contents;
		contents << input.rdbuf();
		return input.bad() ? std::string{} : contents.str();
	}

	bool WriteTextFileAtomically(const std::filesystem::path& path, const std::string& contents)
	{
		if (!TomCat::ImGuiSettings::EnsureParentDirectory(path))
			return false;

		auto temporary = path;
		temporary += ".tmp";
		{
			std::ofstream output(temporary, std::ios::binary | std::ios::trunc);
			if (!output)
				return false;

			output.write(contents.data(), static_cast<std::streamsize>(contents.size()));
			output.flush();
			if (!output.good())
			{
				output.close();
				std::error_code removeError;
				std::filesystem::remove(temporary, removeError);
				return false;
			}
		}

#ifdef _WIN32
		if (!MoveFileExW(temporary.c_str(), path.c_str(),
			MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
		{
			std::error_code removeError;
			std::filesystem::remove(temporary, removeError);
			return false;
		}
#else
		std::error_code renameError;
		std::filesystem::rename(temporary, path, renameError);
		if (renameError)
		{
			std::error_code removeError;
			std::filesystem::remove(temporary, removeError);
			return false;
		}
#endif
		return true;
	}

	IniSectionRange FindIniSection(const std::string& ini, const std::string& header)
	{
		std::string::size_type cursor = 0;
		while (cursor < ini.size())
		{
			const auto newline = ini.find('\n', cursor);
			const auto rawEnd = newline == std::string::npos ? ini.size() : newline;
			auto lineEnd = rawEnd;
			if (lineEnd > cursor && ini[lineEnd - 1] == '\r')
				--lineEnd;

			if (ini.compare(cursor, lineEnd - cursor, header) == 0)
			{
				const auto begin = cursor;
				cursor = newline == std::string::npos ? ini.size() : newline + 1;
				while (cursor < ini.size())
				{
					const auto nextNewline = ini.find('\n', cursor);
					const auto nextRawEnd = nextNewline == std::string::npos ? ini.size() : nextNewline;
					auto nextLineEnd = nextRawEnd;
					if (nextLineEnd > cursor && ini[nextLineEnd - 1] == '\r')
						--nextLineEnd;
					if (nextLineEnd > cursor && ini[cursor] == '[')
						return { begin, cursor };
					cursor = nextNewline == std::string::npos ? ini.size() : nextNewline + 1;
				}
				return { begin, ini.size() };
			}

			cursor = newline == std::string::npos ? ini.size() : newline + 1;
		}
		return {};
	}

	std::string ExtractIniSection(const std::string& ini, const std::string& header)
	{
		const auto range = FindIniSection(ini, header);
		return range ? ini.substr(range.Begin, range.End - range.Begin) : std::string{};
	}

	std::string ReplaceIniSection(std::string ini, const std::string& header, const std::string& replacement)
	{
		const auto range = FindIniSection(ini, header);
		if (range)
		{
			ini.replace(range.Begin, range.End - range.Begin, replacement);
			return ini;
		}

		if (!ini.empty() && ini.back() != '\n')
			ini.push_back('\n');
		ini += replacement;
		return ini;
	}

	bool HasImGuiLayoutSections(const std::string& ini)
	{
		std::istringstream input(ini);
		std::string line;
		while (std::getline(input, line))
		{
			if (!line.empty() && line.back() == '\r')
				line.pop_back();
			if (line.rfind("[Window]", 0) == 0 || line.rfind("[Docking]", 0) == 0)
				return true;
		}
		return false;
	}

}

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
				m_KnownProjectPaths.push_back(project->GetProjectPath());
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
				m_KnownProjectPaths.push_back(project->GetProjectPath());
				SaveHubSettings();
			}
			ScanProjects();
		}
		return project;
	}

	bool ProjectManager::RemoveProject(const std::filesystem::path& projectPath)
	{
		std::error_code pathError;
		const auto absoluteProjectPath = std::filesystem::absolute(projectPath, pathError);
		const auto normalizedProjectPath = pathError
			? projectPath.lexically_normal() : absoluteProjectPath.lexically_normal();
		auto it = std::find_if(m_Projects.begin(), m_Projects.end(),
			[&normalizedProjectPath](const Ref<Project>& p) {
				return p->GetProjectPath().lexically_normal() == normalizedProjectPath;
			});

		if (it != m_Projects.end())
		{
			if (m_OnProjectRemoved)
				m_OnProjectRemoved(*it);

			if (m_ActiveProject && m_ActiveProject->GetProjectPath().lexically_normal() == normalizedProjectPath)
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
		// Hub settings are per-user state, not project/repository state. Keep them
		// beside the Hub layout under LOCALAPPDATA so launching from another cwd
		// does not lose the project list.
		return ImGuiSettings::GetHubUserIniPath();
	}

	void ProjectManager::LoadHubSettings()
	{
		m_ProjectDirectory.clear();
		m_EditorDirectory.clear();
		m_KnownProjectPaths.clear();

		const std::filesystem::path iniPath = GetHubSettingsPath();
		std::string ini = ReadTextFile(iniPath);

		// The old Hub stored ImGui's complete layout and [HubConfig] beside the
		// executable.  On the first run with the new per-user location, copy the
		// whole file before parsing it; copying only [HubConfig] would lose docks.
		std::error_code currentError;
		const auto current = std::filesystem::current_path(currentError);
		if (!currentError)
		{
			const auto oldIni = current / "imgui.ini";
			const auto oldIniText = (oldIni.lexically_normal() == iniPath.lexically_normal())
				? std::string{} : ReadTextFile(oldIni);
			const bool oldHasHubConfig = static_cast<bool>(FindIniSection(oldIniText, "[HubConfig]"));
			const bool hasHubConfig = static_cast<bool>(FindIniSection(ini, "[HubConfig]"));

			if (ini.empty() && !oldIniText.empty())
			{
				if (WriteTextFileAtomically(iniPath, oldIniText))
					ini = oldIniText;
				else
					TC_Core_Warn("Could not migrate the legacy Hub imgui.ini to '{0}'", iniPath.string());
			}
			else if (!oldIniText.empty())
			{
				// Repair an installation created by the earlier migration code: if the
				// new file contains only HubConfig, restore its old Window/Docking data;
				// if it has layout but no HubConfig, import the old application section.
				// The old file may contain only Window/Docking, so do not require an
				// old HubConfig section for the first case.
				const bool hasLayout = HasImGuiLayoutSections(ini);
				if ((!hasLayout && hasHubConfig) || (!hasHubConfig && hasLayout))
				{
					std::string merged = hasLayout ? ini : oldIniText;
					if (hasLayout && !hasHubConfig)
						merged = ReplaceIniSection(merged, "[HubConfig]", ExtractIniSection(oldIniText, "[HubConfig]"));
					else if (!hasLayout && hasHubConfig)
						merged = ReplaceIniSection(oldIniText, "[HubConfig]", ExtractIniSection(ini, "[HubConfig]"));

					if (WriteTextFileAtomically(iniPath, merged))
						ini = std::move(merged);
				}
			}
		}

		bool sawSection = false;
		{
			std::istringstream input(ini);
			std::string line;
			bool inSection = false;
			while (std::getline(input, line))
			{
				if (!line.empty() && line.back() == '\r')
					line.pop_back();
				if (line == "[HubConfig]")
				{
					inSection = true;
					sawSection = true;
					continue;
				}
				if (inSection && !line.empty() && line[0] == '[')
					break;
				if (!inSection)
					continue;
				if (line.rfind("ProjectDirectory=", 0) == 0)
					m_ProjectDirectory = line.substr(17);
				else if (line.rfind("EditorDirectory=", 0) == 0)
					m_EditorDirectory = line.substr(16);
				else if (line.rfind("KnownProjects=", 0) == 0)
					m_KnownProjectPaths.emplace_back(line.substr(14));
			}
		}

		// Legacy migration: if there is no [HubConfig] in the per-user INI yet,
		// import the old HubConfig.tomcat once and persist it to the new location.
		if (!sawSection)
		{
			std::filesystem::path legacyPath = currentError ? std::filesystem::path{} : current / "HubConfig.tomcat";
			if (!legacyPath.empty() && std::filesystem::exists(legacyPath))
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
								m_KnownProjectPaths.emplace_back(node.as<std::string>());
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

	bool ProjectManager::SaveHubSettings()
	{
		return SaveHubSettings({});
	}

	bool ProjectManager::SaveHubSettings(const std::string& layoutIni)
	{
		try
		{
			const std::filesystem::path iniPath = GetHubSettingsPath();

			// Keep application-owned data in the same file as ImGui's layout, but
			// replace only this section so Window/Docking/other handlers survive.
			std::string section = "[HubConfig]\n";
			section += "ProjectDirectory=" + m_ProjectDirectory.string() + "\n";
			section += "EditorDirectory=" + m_EditorDirectory.string() + "\n";
			for (const auto& path : m_KnownProjectPaths)
				section += "KnownProjects=" + path.string() + "\n";

			const std::string baseIni = layoutIni.empty() ? ReadTextFile(iniPath) : layoutIni;
			const std::string ini = ReplaceIniSection(baseIni, "[HubConfig]", section);
			if (!WriteTextFileAtomically(iniPath, ini))
			{
				TC_Core_Error("Failed to write Hub settings '{0}'", iniPath.string());
				return false;
			}
			return true;
		}
		catch (const std::exception& e)
		{
			TC_Core_Error("Failed to save Hub settings: {0}", e.what());
			return false;
		}
	}

}
