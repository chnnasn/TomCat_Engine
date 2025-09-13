#include <TomCat.h>

class ExampleLayer : public TomCat::Layer
{
public:
	ExampleLayer() : Layer("Example")
	{
		
	}

	void OnUpdate() override
	{
		//TC_Info("ExampleLayer : UpDate");

		if (TomCat::Input::IsKeyPressed(KeyCode::Tab)) {
			
			TC_Info("Tab is down");
		
		}
	}

	void OnEvent(TomCat::Event& event) override
	{
		//TC_Trace("{0}",event.ToString());

		if (event.GetEventType()==TomCat::EventType::KeyPressed)
		{
			TomCat::KeyPressedEvent& e = (TomCat::KeyPressedEvent&)event;

			TC_Trace("{0}",(char)e.GetKeyCode());

		}
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