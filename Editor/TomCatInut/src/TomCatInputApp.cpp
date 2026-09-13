#include <TomCat.h>
#define TC_APPLICATION_PRODUCT TomCat::ApplicationProduct::Editor
#include <TomCat/Core/EntryPoint.h>
#include <TomCat/Utils/PathUtils.h>

#include "EditorLayer.h"

#include <utility>

namespace TomCat {

	

	class TomCatInput : public Application
	{
	public:
		TomCatInput(ApplicationCommandLineArgs args)
			: Application("TomCatEditor",
				std::filesystem::path("Packages/Resources/Icons/Logo.ico"))
		{
			std::filesystem::path startupProjectPath;
			if (args.Count > 1)
				startupProjectPath = UTF8ToPath(args[1]);
			
			// EditorLayer acquires the LocalAppData project lock, previews the exact
			// migration, and passes that approved plan to the low-level load API.
			PushLayer(new EditorLayer(std::move(startupProjectPath)));
		}

	};

	Application* CreateApplication(ApplicationCommandLineArgs args)
	{
		return new TomCatInput(args);
	}

}
