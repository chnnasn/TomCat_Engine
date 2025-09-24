#pragma once

#ifdef TC_PLAYTFORM_WINDOWS

extern TomCat::Application* TomCat::CreateApplication();

int main(int argc,char** argv) {

	TomCat::Log::Init();

	TC_Core_Warn("Init LOG");

	TC_Info("Hello");


	auto app = TomCat::CreateApplication();

	app->Run();

	delete app;
}

#endif

