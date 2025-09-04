#pragma once

#ifdef TC_PLAYTFORM_WINDOWS

extern TomCat::Application* TomCat::CreateApplication();

int main(int argc,char** argv) {


	auto app = TomCat::CreateApplication();

	app->Run();

	delete app;
}

#endif

