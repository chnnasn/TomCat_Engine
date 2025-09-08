#include <TomCat.h>

class ExampleLayer : public TomCat::Layer
{
public:
	ExampleLayer() : Layer("Example")
	{
		
	}

	void OnUpdate() override
	{
		TC_Info("ExampleLayer : UpDate");
	}

	void OnEvent(TomCat::Event& event) override
	{
		TC_Trace("{0}",event.ToString());
	}

};




class Examples :public TomCat::Application
{
public:
	Examples()
	{
		PushLayer(new ExampleLayer());
		PushOverLayer(new TomCat::ImGuiLayer());
	}
	~Examples() {}
};
TomCat::Application* TomCat::CreateApplication() {

	return new Examples();
}