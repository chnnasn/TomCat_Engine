#pragma once
#include "Core.h"

#include "Window.h"
#include "TomCat/LayerStack.h"
#include "TomCat/Events/Event.h"
#include "TomCat/Events/ApplicationEvent.h"

namespace TomCat {
	class TomCat_API Application
	{ 
	public :
		Application();

		virtual ~Application();

		void Run();

		void OnEvent(Event& e);

		void PushLayer(Layer* Layer);
		void PushOverLayer(Layer* Layer);

	private:
		bool OnWindowClose(WindowCloseEvent& e);

		std::unique_ptr<Window>m_Window;
		bool m_Running = true;
		LayerStack m_LayerStack;
	};


	//客户端定义
	Application* CreateApplication();

}

