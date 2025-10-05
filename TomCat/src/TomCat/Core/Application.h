#pragma once
#include "Base.h"

#include "Window.h"
#include "TomCat/Core/LayerStack.h"
#include "TomCat/Events/Event.h"
#include "TomCat/Events/ApplicationEvent.h"

#include "TomCat/ImGui/ImGuiLayer.h"

#include "TomCat/Core/TimeStep.h"

namespace TomCat {
	class Application
	{ 
	public :
		Application(const std::string& name = "TomCat App");

		virtual ~Application();

		void Run();

		void OnEvent(Event& e);

		void PushLayer(Layer* Layer);
		void PushOverLayer(Layer* Layer);


		Window& GetWindow() { return *m_Window; }

		void Close();

		ImGuiLayer* GetImGuiLayer() { return m_ImGuiLayer; };

		static Application& Get() { return *s_Instance; }

	private:
		bool OnWindowClose(WindowCloseEvent& e);
		bool OnWindowResize(WindowResizeEvent& e);

	private:
		std::unique_ptr<Window>m_Window;
		ImGuiLayer* m_ImGuiLayer;
		bool m_Running = true;
		bool m_Minized = false;
		LayerStack m_LayerStack;
		float m_LastFrameTime = 0.0f;

	private:
		static Application* s_Instance;
	};


	//客户端定义
	Application* CreateApplication();

}

