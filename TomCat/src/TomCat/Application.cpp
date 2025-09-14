#include "tcpch.h"
#include "Application.h"

#include "Log.h"

#include <Glad/glad.h>

#include "Input.h"

namespace TomCat {

#define Bind_Event_Fn(x) std::bind(&Application::x,this,std::placeholders::_1)
	Application* Application::s_Instance = nullptr;
	
	static GLenum ShaderDataTypeToOpenGLBaseType(ShaderDataType type)
	{
		switch (type)
		{
			case TomCat::ShaderDataType::Float:    return GL_FLOAT;
			case TomCat::ShaderDataType::Float2:   return GL_FLOAT;
			case TomCat::ShaderDataType::Float3:   return GL_FLOAT;
			case TomCat::ShaderDataType::Float4:   return GL_FLOAT;
			case TomCat::ShaderDataType::Mat3:     return GL_FLOAT;
			case TomCat::ShaderDataType::Mat4:     return GL_FLOAT;
			case TomCat::ShaderDataType::Int:      return GL_INT;
			case TomCat::ShaderDataType::Int2:     return GL_INT;
			case TomCat::ShaderDataType::Int3:     return GL_INT;
			case TomCat::ShaderDataType::Int4:     return GL_INT;
			case TomCat::ShaderDataType::Bool:     return GL_BOOL;
		}

		TC_Core_Assert(false, "Unknown ShaderDataType!");
		return 0;
	}


	Application::Application()
	{

		TC_Core_Assert(!s_Instance, "应用程序已经存在！");
		s_Instance = this;
		m_Window = std::unique_ptr<Window>(Window::Create());
		m_Window->SetEventCallback(Bind_Event_Fn(OnEvent));

		m_ImGuiLayer = new ImGuiLayer();

		PushOverLayer(m_ImGuiLayer);

///<summary>
///构建顶点数组、构建顶点缓冲、构建索引缓冲
///</summary>


		glGenVertexArrays(1,&m_VertexArray);
		glBindVertexArray(m_VertexArray);

		float vertices[3 * 7] = {
			-0.5f,-0.5f,0.0f,1.0f,0.0f,1.0f,1.0f,
			0.5f,-0.5f,0.0f,0.0f,0.0f,1.0f,1.0f,
			0.0f,0.5f,0.0f,1.0f,1.0f,0.0f,1.0f,
		};

		m_VertexBuffer.reset(VertexBuffer::Create(vertices, sizeof(vertices)));

		{
			BufferLayout layout = {
				{ShaderDataType::Float3,"a_Posioton"},
				{ShaderDataType::Float4,"a_Color"}
			};
			m_VertexBuffer->SetLayout(layout);
		}


		uint32_t index = 0;
		const auto& layout =  m_VertexBuffer->GetLayout();
		for (const auto& element : layout)
		{
			glEnableVertexAttribArray(index);
			glVertexAttribPointer(index,
				element.GetComponentCount(), 
				ShaderDataTypeToOpenGLBaseType(element.Type),
				element.Normalized ? GL_TRUE:GL_FALSE, 
				layout.GetStride(),
				(const void*)element.Offset);
			index++;
		}

		
		uint32_t indices[3] = { 0,1,2 };

		m_IndexBuffer.reset(IndexBuffer :: Create(indices,sizeof(indices)/sizeof(uint32_t)));


		///<summary>
		//先实例化m_RendererId，否则指针为空
		///</summary>
		std::string vertexSrc = R"(
			#version 330 core
			
			layout(location = 0) in vec3 a_Position;
			layout(location = 1) in vec4 a_Color;

			out vec3 v_Position;
			out vec4 v_Color;

			void main()
			{
				v_Position = a_Position;
				v_Color = a_Color;
				gl_Position = vec4(a_Position, 1.0);	
			}
		)";

		std::string fragmentSrc = R"(
			#version 330 core
			
			layout(location = 0) out vec4 color;

			in vec3 v_Position;
			in vec4 v_Color;

			void main()
			{
				color = vec4(v_Position * 0.5 + 0.5, 1.0);
				color = v_Color;
			}
		)";

		m_Shader.reset(new Shader(vertexSrc, fragmentSrc));

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

			m_Shader->Bind();
			glBindVertexArray(m_VertexArray);
			glDrawElements(GL_TRIANGLES,m_IndexBuffer->GetCount(), GL_UNSIGNED_INT, nullptr);


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
