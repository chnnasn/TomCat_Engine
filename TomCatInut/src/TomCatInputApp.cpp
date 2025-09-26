#include <TomCat.h>
#include <TomCat/Core/EntryPoint.h>

#include "EditorLayer.h"

namespace TomCat {

	class TomCatInput : public Application
	{
	public:
		TomCatInput()
			: Application("TomCat")
		{
			PushLayer(new EditorLayer());
		}

		~TomCatInput()
		{
		}
	};

	Application* CreateApplication()
	{
		return new TomCatInput();
	}

}