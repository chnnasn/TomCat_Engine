#include "tcpch.h"
#include "ProjectManager.h"

#include <filesystem>
#include <algorithm>

namespace TomCat {

	ProjectManager& ProjectManager::Get()
	{
		static ProjectManager instance;
		return instance;
	}

	void ProjectManager::SetProjectDirectory(const std::filesystem::path& directory)
	{
		m_ProjectDirectory = directory;
		if (!std::filesystem::exists(directory))
		{
			std::filesystem::create_directories(directory);
		}
		ScanProjects();
	}

	void ProjectManager::ScanProjects()
	{
		m_Projects.clear();
		
		if (!std::filesystem::exists(m_ProjectDirectory))
			return;

		for (const auto& entry : std::filesystem::directory_iterator(m_ProjectDirectory))
		{
			if (entry.is_directory())
			{
				for (const auto& file : std::filesystem::directory_iterator(entry.path()))
				{
					if (IsProjectFile(file.path()))
					{
						auto project = Project::Load(file.path());
						if (project)
						{
							m_Projects.push_back(project);
						}
						break;
					}
				}
			}
		}

		std::sort(m_Projects.begin(), m_Projects.end(), 
			[](const Ref<Project>& a, const Ref<Project>& b) {
				return a->GetLastModified() > b->GetLastModified();
			});
	}

	Ref<Project> ProjectManager::CreateProject(const std::filesystem::path& projectPath, const ProjectConfig& config)
	{
		auto project = Project::CreateNew(projectPath, config);
		if (project)
		{
			m_Projects.push_back(project);
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
			m_ActiveProject = project;
			if (m_OnProjectLoaded)
				m_OnProjectLoaded(project);
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

		std::filesystem::path editorPath = "TomCatInut.exe";
		std::string command = "\"" + editorPath.string() + "\" \"" + project->GetProjectPath().string() + "\"";
		
		STARTUPINFOA si;
		PROCESS_INFORMATION pi;
		ZeroMemory(&si, sizeof(si));
		si.cb = sizeof(si);
		ZeroMemory(&pi, sizeof(pi));

		CreateProcessA(
			NULL,
			const_cast<LPSTR>(command.c_str()),
			NULL,
			NULL,
			FALSE,
			0,
			NULL,
			NULL,
			&si,
			&pi
		);

		CloseHandle(pi.hProcess);
		CloseHandle(pi.hThread);
	}

	bool ProjectManager::IsProjectFile(const std::filesystem::path& path) const
	{
		return path.extension() == ".tcproj" && path.filename() == "Project.tcproj";
	}

}
