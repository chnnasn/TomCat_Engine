#include <TomCat.h>

#include "platform/OpenGL/OpenGLShader.h"

#include "ImGui/imgui.h"

#include <glm/gtc/matrix_transform.hpp>

#include <glm/gtc/type_ptr.hpp>

class ExampleLayer : public TomCat::Layer
{
public:
	ExampleLayer() 
		: Layer("Example"), m_CameraController(1280.0f/720.0f)
	{
		m_VertexArray.reset(TomCat::VertexArray::Create());


		float vertices[3 * 7] = {
			-0.5f,-0.5f,0.0f,1.0f,0.0f,1.0f,1.0f,
			0.5f,-0.5f,0.0f,0.0f,0.0f,1.0f,1.0f,
			0.0f,0.5f,0.0f,1.0f,1.0f,0.0f,1.0f,
		};

		TomCat::Ref<TomCat::VertexBuffer> vertexBuffer;
		vertexBuffer.reset(TomCat::VertexBuffer::Create(vertices, sizeof(vertices)));

		TomCat::BufferLayout layout = {
				{TomCat::ShaderDataType::Float3,"a_Posioton"},
				{TomCat::ShaderDataType::Float4,"a_Color"}
		};
		vertexBuffer->SetLayout(layout);
		m_VertexArray->AddVertexBuffer(vertexBuffer);

		uint32_t indices[3] = { 0,1,2 };
		TomCat::Ref<TomCat::IndexBuffer> indexBuffer;
		indexBuffer.reset(TomCat::IndexBuffer::Create(indices, sizeof(indices) / sizeof(uint32_t)));
		m_VertexArray->SetIndexBuffer(indexBuffer);


		m_SquareVA.reset(TomCat::VertexArray::Create());

		float squareVertices[5 * 4] = {
			-0.5f, -0.5f, 0.0f,0.0f,0.0f,
			 0.5f, -0.5f, 0.0f,1.0f,0.0f,
			 0.5f,  0.5f, 0.0f,1.0f,1.0f,
			-0.5f,  0.5f, 0.0f,0.0f,1.0f
		};


		TomCat::Ref<TomCat::VertexBuffer> squareVB;
		squareVB.reset(TomCat::VertexBuffer::Create(squareVertices, sizeof(squareVertices)));

		TomCat::BufferLayout squareVBLayout = {
			{TomCat::ShaderDataType::Float3,"a_Posioton"},
			{TomCat::ShaderDataType::Float2,"a_TexCoord"}
		};
		squareVB->SetLayout(squareVBLayout);
		m_SquareVA->AddVertexBuffer(squareVB);

		uint32_t squareIndices[6] = { 0,1,2,2,3,0 };
		TomCat::Ref<TomCat::IndexBuffer> squareIB;
		squareIB.reset(TomCat::IndexBuffer::Create(squareIndices, sizeof(squareIndices) / sizeof(uint32_t)));
		m_SquareVA->SetIndexBuffer(squareIB);

		///<summary>
		//先实例化m_RendererId，否则指针为空
		///</summary>
		std::string vertexSrc = R"(
			#version 330 core
			
			layout(location = 0) in vec3 a_Position;
			layout(location = 1) in vec4 a_Color;

			uniform mat4 u_ViewProjection;
			uniform mat4 u_Transform;

			out vec3 v_Position;
			out vec4 v_Color;

			void main()
			{
				v_Position = a_Position;
				v_Color = a_Color;
				gl_Position = u_ViewProjection * u_Transform * vec4(a_Position, 1.0);	
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

		m_Shader = TomCat::Shader::Create("VertexPosColor",vertexSrc, fragmentSrc);

		std::string flatColorShaderVertexSrc = R"(
			#version 330 core
			
			layout(location = 0) in vec3 a_Position;

			uniform mat4 u_ViewProjection;
			uniform mat4 u_Transform;

			out vec3 v_Position;

			void main()
			{
				v_Position = a_Position;
				gl_Position = u_ViewProjection * u_Transform * vec4(a_Position, 1.0);	
			}
		)";

		std::string flatColorShaderFragmentSrc = R"(
			#version 330 core
			
			layout(location = 0) out vec4 color;

			in vec3 v_Position;
			
			uniform vec3 u_Color;

			void main()
			{
				color = vec4(u_Color, 1.0);
			}
		)";

		m_FlatColorShader = TomCat::Shader::Create("FlatColor",flatColorShaderVertexSrc, flatColorShaderFragmentSrc);

		auto textureShader = m_ShaderLibrary.Load("assets/shaders/Texture.glsl");

		m_Texture = TomCat::Texture2D::Create("assets/textures/Checkerboard.png");
		m_LogoTexture = TomCat::Texture2D::Create("assets/textures/ChernoLogo.png");
		 


		std::dynamic_pointer_cast<TomCat::OpenGLShader>(textureShader)->Bind();
		std::dynamic_pointer_cast<TomCat::OpenGLShader>(textureShader)->UploadUniformInt("u_Texture", 0);

	}

	void OnUpdate(TomCat::Timestep ts) override
	{
		//调用摄像机
		m_CameraController.OnUpdate(ts);

		//渲染
		TomCat::RenderCommand::SetClearColor({ 0.1f, 0.1f, 0.1f, 1 });
		TomCat::RenderCommand::Clear();

		TomCat::Renderer::BeginScene(m_CameraController.GetCamera());

		 glm::mat4 scale = glm::scale(glm::mat4(1.0f), glm::vec3(0.1f));

		 std::dynamic_pointer_cast<TomCat::OpenGLShader>(m_FlatColorShader)->Bind();
		 std::dynamic_pointer_cast<TomCat::OpenGLShader>(m_FlatColorShader)->UploadUniformFloat3("u_Color", m_SquareColor);

		for (int x = 0; x < 20; x++)
		{
			for (int y = 0; y < 20; y++)
			{

				glm::vec3 pos(x*0.11f,y*0.11f,0.0f);
				glm::mat4 transform = glm::translate(glm::mat4(1.0f), pos) * scale;

				TomCat::Renderer::Submit(m_FlatColorShader, m_SquareVA, transform);

			}
		}

		auto textureShader = m_ShaderLibrary.Get("Texture");

		m_Texture->Bind();

		TomCat::Renderer::Submit(textureShader, m_SquareVA, glm::scale(glm::mat4(1.0f), glm::vec3(1.5f)));


		m_LogoTexture->Bind();

		TomCat::Renderer::Submit(textureShader, m_SquareVA,glm::scale(glm::mat4(1.0f), glm::vec3(1.5f)));

		//TomCat::Renderer::Submit(m_Shader, m_VertexArray);

		TomCat::Renderer::EndScene();


	}

	virtual void OnImGuiRender()override
	{
		ImGui::Begin("Settings");
		ImGui::ColorEdit3("Square Color", glm::value_ptr(m_SquareColor));
		ImGui::End();

	}


	void OnEvent(TomCat::Event& e) override
	{
		m_CameraController.OnEvent(e);

	}

private:
	TomCat::ShaderLibrary m_ShaderLibrary;

	TomCat::Ref<TomCat::Shader> m_Shader;
	TomCat::Ref<TomCat::VertexArray> m_VertexArray;

	TomCat::Ref<TomCat::Shader> m_FlatColorShader;
	TomCat::Ref<TomCat::VertexArray> m_SquareVA;

	TomCat::Ref<TomCat::Texture2D> m_Texture, m_LogoTexture;

	TomCat::OrthographicCameraController m_CameraController;

	glm::vec3 m_SquareColor = { 0.2f, 0.3f, 0.8f };

};


class Examples :public TomCat::Application
{
public:
	Examples()
	{
		PushLayer(new ExampleLayer());
	}
	~Examples()
	{
	
	}
};


TomCat::Application* TomCat::CreateApplication() {

	return new Examples();
}