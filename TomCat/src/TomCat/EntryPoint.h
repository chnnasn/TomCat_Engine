#pragma once

#ifdef TC_PLAYTFORM_WINDOWS

extern TomCat::Application* TomCat::CreateApplication();

int main(int argc,char** argv) {

	TomCat::Log::Init();

	TomCat_Core_Warn("Init LOG");

	TomCat_Info("Hello");

	auto app = TomCat::CreateApplication();

	app->Run();

	delete app;
}

#endif

