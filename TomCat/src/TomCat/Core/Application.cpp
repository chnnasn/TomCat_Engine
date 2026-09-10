#include "tcpch.h"
#include "Application.h"

#include "Log.h"

#include "TomCat/Renderer/Renderer.h"

#include "Input.h"

#include <stdexcept>

namespace TomCat {

	Application* Application::s_Instance = nullptr;

	Application::Application(const std::string& name, std::filesystem::path iconPath)
	{
		TC_PROFILE_FUNCTION();

		if (s_Instance)
			throw std::logic_error("Only one TomCat application can exist at a time");

		s_Instance = this;
		try
		{
			m_Window = Window::Create(WindowProps(name, 1920, 1080, std::move(iconPath)));
			if (!m_Window)
				throw std::runtime_error("Failed to create the application window");

			m_Window->SetEventCallback(TC_Bind_Event_Fn(Application::OnEvent));
			Renderer::Init();

			m_ImGuiLayer = new ImGuiLayer();
			PushOverlay(m_ImGuiLayer);
		}
		catch (...)
		{
			m_LayerStack.Clear();
			m_ImGuiLayer = nullptr;
			Renderer::Shutdown();
			m_Window.reset();
			s_Instance = nullptr;
			throw;
		}

	}


	Application :: ~Application() 
	{
		TC_PROFILE_FUNCTION();

		m_LayerStack.Clear();
		m_ImGuiLayer = nullptr;
		Renderer::Shutdown();
		m_Window.reset();
		s_Instance = nullptr;
	}

	void Application::PushLayer(Layer* layer)
	{
		TC_PROFILE_FUNCTION();
		if (m_LayerStack.PushLayer(layer))
			layer->OnAttach();
	}
	void Application::PushOverlay(Layer* layer)
	{
		TC_PROFILE_FUNCTION();
		if (m_LayerStack.PushOverlay(layer))
			layer->OnAttach();
	}

	void Application::Close()
	{
		m_Running = false;

	}

	void Application::OnEvent(Event& e) 
	{
		TC_PROFILE_FUNCTION();

		// The editor must be allowed to veto the native close request while it
		// presents Save / Discard / Cancel. Other application events keep their
		// original core-first routing.
		if (e.GetEventType() == WindowCloseEvent::GetStaticType())
		{
			for (auto it = m_LayerStack.end(); it != m_LayerStack.begin(); )
			{
				(*--it)->OnEvent(e);
				if (e.m_Handled)
					break;
			}

			if (!e.m_Handled)
			{
				EventDispatcher closeDispatcher(e);
				closeDispatcher.Dispatch<WindowCloseEvent>(TC_Bind_Event_Fn(Application::OnWindowClose));
			}

			// A handled close may leave the run loop active while a layer presents a
			// confirmation. Reject the backend request and ensure a minimized window
			// becomes visible so that confirmation can actually be answered.
			if (m_Running && m_Window)
			{
				m_Window->CancelCloseRequest();
				m_Minimized = false;
			}
			return;
		}

		EventDispatcher dispatcher(e);
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
		m_LastFrameTime = static_cast<float>(m_Window->GetTimeSeconds());

		while (m_Running) 
		{
			TC_PROFILE_SCOPE("RunLoop");

			float time = static_cast<float>(m_Window->GetTimeSeconds());
			Timestep timestep = time - m_LastFrameTime;
			m_LastFrameTime = time;

			if (!m_Minimized)
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
			m_Minimized = true;
			return false;
		}

		m_Minimized = false;
		Renderer::OnWindowResize(e.GetWidth(), e.GetHeight());

		return false;
	}
}
