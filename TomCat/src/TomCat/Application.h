#pragma once
#include "Core.h"

#include "Window.h"
#include "TomCat/LayerStack.h"
#include "TomCat/Events/Event.h"
#include "TomCat/Events/ApplicationEvent.h"

#include "TomCat/ImGui/ImGuiLayer.h"

#include"TomCat/Renderer/Shader.h"
#include"TomCat/Renderer/Buffer.h"
#include"TomCat/Renderer/VertexArray.h"

#include "TomCat/Renderer/OrthographicCamera.h"

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

		std::unique_ptr<Window>m_Window;
		ImGuiLayer* m_ImGuiLayer;
		bool m_Running = true;
		LayerStack m_LayerStack;

	private:
		static Application* s_Instance;
	};


	//客户端定义
	Application* CreateApplication();

}

