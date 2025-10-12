#include <TomCat.h>
#include <TomCat/Core/EntryPoint.h>

#include "EditorLayer.h"

namespace TomCat {

	class TomCatInput : public Application
	{
	public:
		TomCatInput(ApplicationCommandLineArgs args)
			: Application("TomCatInput", args)
		{
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