#include "tcpch.h"
#include "Application.h"

#include "Log.h"

#include "TomCat/Renderer/Renderer.h"

#include "Input.h"

#include <glfw/glfw3.h>

namespace TomCat {

#define Bind_Event_Fn(x) std::bind(&Application::x,this,std::placeholders::_1)
	Application* Application::s_Instance = nullptr;

	Application::Application()
	{

		TC_Core_Assert(!s_Instance, "应用程序已经存在！");
		s_Instance = this;
		m_Window = std::unique_ptr<Window>(Window::Create());
		m_Window->SetEventCallback(Bind_Event_Fn(OnEvent));

		Renderer::Init();

		m_ImGuiLayer = new ImGuiLayer();

		PushOverLayer(m_ImGuiLayer);

	}


	Application :: ~Application() 
	{

	}

	void Application::PushLayer(Layer* Layer)
	{
		m_LayerStack.PushLayer(Layer);
		Layer->OnAttach();
	}
	void Application::PushOverLayer(Layer* Layer)
	{
		m_LayerStack.PushOverLayer(Layer);
		Layer->OnAttach();
	}



	void Application::OnEvent(Event& e) 
	{
		EventDispatcher dispatcher(e);
		dispatcher.Dispatch<WindowCloseEvent>(Bind_Event_Fn(OnWindowClose));
		dispatcher.Dispatch<WindowResizeEvent>(Bind_Event_Fn(OnWindowResize));

		//TC_Core_Trace("{0}",e.ToString());

		for (auto it = m_LayerStack.end(); it != m_LayerStack.begin(); )
		{
			(*--it)->OnEvent(e);
			if (e.m_Handled)
				break;
		}

	}


	void Application::Run() {

		while (m_Running) 
		{
			float time = (float)glfwGetTime();
			Timestep timestep = time - m_LastFrameTime;
			m_LastFrameTime = time;

			if (!m_Minized) 
			{
				for (Layer* layer : m_LayerStack)
					layer->OnUpdate(timestep);
			}

			m_ImGuiLayer->Begin();
			for (Layer* layer : m_LayerStack)
				layer->OnImGuiRender();
			m_ImGuiLayer->End();

			m_Window->OnUpdate();
		}
	}


	bool Application::OnWindowClose(WindowCloseEvent& e)
	{
		m_Running = false;
		return true;
	}


	bool Application::OnWindowResize(WindowResizeEvent& e)
	{
		if (e.GetWidth() == 0 || e.GetHeight() == 0)
		{
			m_Minized = true;
			return false;
		}

		m_Minized = false;
		Renderer::OnWindowResize(e.GetWidth(), e.GetHeight());

		return false;
	}
}
