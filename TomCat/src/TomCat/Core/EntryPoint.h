#pragma once
#include "TomCat/Core/Base.h"
#include "TomCat/Core/Application.h"

#ifdef TC_PLAYTFORM_WINDOWS

extern TomCat::Application* TomCat::CreateApplication(ApplicationCommandLineArgs args);

int main(int argc,char** argv) {

	TomCat::Log::Init();

	TC_PROFILE_BEGIN_SESSION("Startup", "TomCatProfile-Startup.json");
	auto app = TomCat::CreateApplication({ argc, argv });
	TC_PROFILE_END_SESSION();			 
										 
	TC_PROFILE_BEGIN_SESSION("Runtime", "TomCatProfile-Runtime.json");
	app->Run();							
	TC_PROFILE_END_SESSION();			
										
	TC_PROFILE_BEGIN_SESSION("Startup", "TomCatProfile-Shutdown.json");
	delete app;							
	TC_PROFILE_END_SESSION();
}

#endif

