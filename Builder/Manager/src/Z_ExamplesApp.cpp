#include <TomCat.h>
#include <TomCat/Core/EntryPoint.h>
#include "ExampleLayer.h"

namespace TomCat {

	class Manager : public Application
	{
	public:
		Manager()
			: Application("TomCatHub", "Packages/Resources/Icons/HubLogo.ico")
		{

			PushLayer(new ExampleLayer());
		}
	};

	Application* CreateApplication(ApplicationCommandLineArgs)
	{
		return new Manager();
	}

}
