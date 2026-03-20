#include <TomCat.h>
#include <TomCat/Core/EntryPoint.h>
#include <TomCat/Project/ProjectManager.h>

#include "EditorLayer.h"

namespace TomCat {

	

	class TomCatInput : public Application
	{
	public:
		TomCatInput(ApplicationCommandLineArgs args)
			: Application("TomCatEditor","Packages/Resources/Icons/Logo.ico", args)
		{
			Ref<Project> project = nullptr;
			
			if (args.Count > 1)
			{
				std::string projectPath = args[1];
				project = ProjectManager::Get().LoadProject(projectPath);
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