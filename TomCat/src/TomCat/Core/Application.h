#pragma once
#include "Core.h"

#include "Window.h"
#include "TomCat/Core/LayerStack.h"
#include "TomCat/Events/Event.h"
#include "TomCat/Events/ApplicationEvent.h"

#include "TomCat/ImGui/ImGuiLayer.h"

#include "TomCat/Core/TimeStep.h"

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


		inline Window& GetWindow() { return *m_Window; }

		inline static Application& Get() { return *s_Instance; }

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

