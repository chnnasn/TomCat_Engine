#include <TomCat.h>

#include "ImGui/imgui.h"

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

	virtual void OnImGuiRender()override
	{
	
		ImGui::Begin("Test");
		ImGui::Text("Hello World");
		ImGui::End();

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
	}
	~Examples() {}
};
TomCat::Application* TomCat::CreateApplication() {

	return new Examples();
}