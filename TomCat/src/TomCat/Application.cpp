#include "Application.h"

#include "Events/ApplicationEvent.h"
#include "Log.h"

namespace TomCat {

	Application::Application(){
	
	}

	Application :: ~Application() {
	
	}



	void Application::Run() {

		WindowResizeEvent e(1280,720);
		if(e.IsIncategory(EventCategoryApplication))
		{
			TC_Trace(e.ToString());
		}

		while (true);
	}
}
