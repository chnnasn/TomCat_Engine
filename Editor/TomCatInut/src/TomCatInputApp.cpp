#include <TomCat.h>
#include <TomCat/Core/EntryPoint.h>
#include <TomCat/Project/ProjectManager.h>
#include <TomCat/Utils/PathUtils.h>

#include "EditorLayer.h"

namespace TomCat {

	

	class TomCatInput : public Application
	{
	public:
		TomCatInput(ApplicationCommandLineArgs args)
			: Application("TomCatEditor",
				std::filesystem::path("Packages/Resources/Icons/Logo.ico"))
		{
			if (args.Count > 1)
			{
				const std::filesystem::path projectPath = UTF8ToPath(args[1]);
				ProjectManager::Get().LoadProject(projectPath);
			}
			
			PushLayer(new EditorLayer());
		}

	};

	Application* CreateApplication(ApplicationCommandLineArgs args)
	{
		return new TomCatInput(args);
	}

}
