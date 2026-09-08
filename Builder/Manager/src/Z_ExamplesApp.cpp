#include <TomCat.h>
#include <TomCat/Core/EntryPoint.h>
#include "ExampleLayer.h"

namespace TomCat {

	class Manager : public Application
	{
	public:
		Manager(ApplicationCommandLineArgs args)
			: Application("TomCatHub", "Packages/Resources/Icons/HubLogo.ico", args)
		{

			PushLayer(new ExampleLayer());
		}

		~Manager()
		{
		}
	};

	Application* CreateApplication(ApplicationCommandLineArgs args)
	{
		return new Manager(args);
	}

}