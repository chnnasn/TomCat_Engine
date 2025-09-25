#pragma once

#ifdef TC_PLAYTFORM_WINDOWS

extern TomCat::Application* TomCat::CreateApplication();

int main(int argc,char** argv) {

	TomCat::Log::Init();

	TC_PROFILE_BEGIN_SESSION("Startup", "TomCatProfile-Startup.json");
	auto app = TomCat::CreateApplication();
	TC_PROFILE_END_SESSION();			 
										 
	TC_PROFILE_BEGIN_SESSION("Runtime", "TomCatProfile-Runtime.json");
	app->Run();							
	TC_PROFILE_END_SESSION();			
										
	TC_PROFILE_BEGIN_SESSION("Startup", "TomCatProfile-Shutdown.json");
	delete app;							
	TC_PROFILE_END_SESSION();
}

#endif

