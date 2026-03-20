#include <TomCat.h>
#include <TomCat/Core/EntryPoint.h>
#include <TomCat/Project/ProjectManager.h>

#include "EditorLayer.h"

namespace TomCat {

	static const char* s_DefaultProjectPath = "Packages/TetxProject/Project.tcproj";

	class TomCatInput : public Application
	{
	public:
		TomCatInput(ApplicationCommandLineArgs args)
			: Application("TomCatInput", args)
		{
			Ref<Project> project = nullptr;
			
			if (args.Count > 1)
			{
				std::string projectPath = args[1];
				project = ProjectManager::Get().LoadProject(projectPath);
			}
			else
			{
				std::filesystem::path defaultPath = std::filesystem::current_path() / s_DefaultProjectPath;
				if (std::filesystem::exists(defaultPath))
				{
					project = ProjectManager::Get().LoadProject(defaultPath);
				}
			}
			
			if (project)
			{
				ProjectManager::Get().SetActiveProject(project);
			}
			
			PushLayer(new EditorLayer());
		}

		~TomCatInput()
		{
		}
	};

	Application* CreateApplication(ApplicationCommandLineArgs args)
	{
		return new TomCatInput(args);
	}

}