#include "tcpch.h"
#include "Application.h"

#include "Log.h"

#include "TomCat/Renderer/Renderer.h"

#include "Input.h"

#include <glfw/glfw3.h>

namespace TomCat {

	Application* Application::s_Instance = nullptr;

	Application::Application()
	{
		TC_PROFILE_FUNCTION();

		TC_Core_Assert(!s_Instance, "应用程序已经存在！");
		s_Instance = this;
		m_Window = std::unique_ptr<Window>(Window::Create());
		m_Window->SetEventCallback(TC_Bind_Event_Fn(Application::OnEvent));

		Renderer::Init();

		m_ImGuiLayer = new ImGuiLayer();

		PushOverLayer(m_ImGuiLayer);

	}


	Application :: ~Application() 
	{
		TC_PROFILE_FUNCTION();

		Renderer::Shutdown();
	}

	void Application::PushLayer(Layer* Layer)
	{
		TC_PROFILE_FUNCTION();
		m_LayerStack.PushLayer(Layer);
		Layer->OnAttach();
	}
	void Application::PushOverLayer(Layer* Layer)
	{
		TC_PROFILE_FUNCTION();
		m_LayerStack.PushOverLayer(Layer);
		Layer->OnAttach();
	}

	void Application::Close()
	{
		m_Running = false;

	}

	void Application::OnEvent(Event& e) 
	{
		TC_PROFILE_FUNCTION();

		EventDispatcher dispatcher(e);
		dispatcher.Dispatch<WindowCloseEvent>(TC_Bind_Event_Fn(Application::OnWindowClose));
		dispatcher.Dispatch<WindowResizeEvent>(TC_Bind_Event_Fn(Application::OnWindowResize));

		//TC_Core_Trace("{0}",e.ToString());

		for (auto it = m_LayerStack.end(); it != m_LayerStack.begin(); )
		{
			(*--it)->OnEvent(e);
			if (e.m_Handled)
				break;
		}

	}


	void Application::Run()
	{
		TC_PROFILE_FUNCTION();

		while (m_Running) 
		{
			TC_PROFILE_SCOPE("RunLoop");

			float time = (float)glfwGetTime();
			Timestep timestep = time - m_LastFrameTime;
			m_LastFrameTime = time;

			if (!m_Minized) 
			{
				{
					TC_PROFILE_SCOPE("LayerStack Onupdates");

					for (Layer* layer : m_LayerStack)
						layer->OnUpdate(timestep);
				}

				m_ImGuiLayer->Begin();
				{
					TC_PROFILE_SCOPE("LayerStack OnImGuiRender");
					for (Layer* layer : m_LayerStack)
						layer->OnImGuiRender();
				}
				m_ImGuiLayer->End();

			}

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
		TC_PROFILE_FUNCTION();

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
