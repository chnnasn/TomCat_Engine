#include <TomCat.h>

#include <TomCat/Core/EntryPoint.h>

#include "Z_Examples2D.h"

class Examples :public TomCat::Application
{
public:
	Examples()
	{
		//PushLayer(new ExampleLayer());
		PushLayer(new Z_Examples2D());
	}
	~Examples()
	{
	
	}
};


TomCat::Application* TomCat::CreateApplication() {

	return new Examples();
}