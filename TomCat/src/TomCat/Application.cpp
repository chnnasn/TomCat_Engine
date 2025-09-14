#include "tcpch.h"
#include "Application.h"

#include "Log.h"

#include <Glad/glad.h>

#include "Input.h"

namespace TomCat {

#define Bind_Event_Fn(x) std::bind(&Application::x,this,std::placeholders::_1)
	Application* Application::s_Instance = nullptr;

	Application::Application()
	{

		TC_Core_Assert(!s_Instance, "应用程序已经存在！");
		s_Instance = this;
		m_Window = std::unique_ptr<Window>(Window::Create());
		m_Window->SetEventCallback(Bind_Event_Fn(OnEvent));

		m_ImGuiLayer = new ImGuiLayer();

		PushOverLayer(m_ImGuiLayer);

		//顶点数组
		//顶点缓冲
		//索引缓冲


		///<summary>
		///先实例化m_RendererId，否则指针为空
		///</summary>
		std::string vertexSrc = R"(
			#version 330 core
			
			layout(location = 0) in vec3 a_Position;

			out vec3 v_Position;

			void main()
			{
				v_Position = a_Position;
				gl_Position = vec4(a_Position, 1.0);	
			}
		)";

		std::string fragmentSrc = R"(
			#version 330 core
			
			layout(location = 0) out vec4 color;

			in vec3 v_Position;

			void main()
			{
				color = vec4(v_Position*0.5+0.5, 1.0);
			}
		)";

		m_Shader.reset(new Shader(vertexSrc, fragmentSrc));

		glGenVertexArrays(1,&m_VertexArray);
		glBindVertexArray(m_VertexArray);

		m_Shader->Bind();

		glGenBuffers(1,&m_VertexBuffer);
		glBindBuffer(GL_ARRAY_BUFFER,m_VertexBuffer);

		float vertices[3 * 3] = {
			-0.5f,-0.5f,0.0f,
			0.5f,-0.5f,0.0f,
			0.0f,0.5f,0.0f
		};

		glBufferData(GL_ARRAY_BUFFER,sizeof(vertices), vertices,GL_STATIC_DRAW);

		glEnableVertexAttribArray(0);
		glVertexAttribPointer(0,3,GL_FLOAT,GL_FALSE,3*sizeof(float),nullptr);

		glGenBuffers(1 ,&m_IndexBuffer);
		glBindBuffer(GL_ELEMENT_ARRAY_BUFFER,m_IndexBuffer);

		unsigned int indices[3] = { 0,1,2 };
		glBufferData(GL_ELEMENT_ARRAY_BUFFER,sizeof(indices), indices, GL_STATIC_DRAW);


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
			glClearColor(0.1f,0.1f,0.1f,1);
			glClear(GL_COLOR_BUFFER_BIT);

			glBindVertexArray(m_VertexArray);
			glDrawElements(GL_TRIANGLES,3,GL_UNSIGNED_INT,nullptr);


			for (Layer* layer : m_LayerStack)
				layer->OnUpdate();

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
}
