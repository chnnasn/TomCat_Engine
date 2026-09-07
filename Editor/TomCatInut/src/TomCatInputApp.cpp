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
			bool is2DMode = false; // Editor launched directly defaults to 3D.
			
			if (args.Count > 1)
			{
				std::string projectPath = args[1];
				project = ProjectManager::Get().LoadProject(projectPath);
			}
			if (args.Count > 2)
			{
				std::string mode = args[2];
				is2DMode = mode == "2D" || mode == "2d";
			}
			
			if (project)
			{
				ProjectManager::Get().SetActiveProject(project);
			}
			
			PushLayer(new EditorLayer(is2DMode));
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
