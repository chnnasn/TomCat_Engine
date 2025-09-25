#include"tcpch.h"
#include"Renderer2D.h"
#include"VertexArray.h"
#include"shader.h"
#include "platform/OpenGL/OpenGLShader.h"
#include "RenderCommand.h"
namespace	TomCat {


	struct  Renderer2DStorage
	{
		Ref<VertexArray> QuadVertexArray;
		Ref<Shader> FlatColorShader;
	};

	static Renderer2DStorage* m_data;

	void Renderer2D::Init()
	{
		m_data = new Renderer2DStorage();

		m_data->QuadVertexArray = VertexArray::Create();

		float squareVertices[5 * 4] = {
			-0.5f, -0.5f, 0.0f,
			 0.5f, -0.5f, 0.0f,
			 0.5f,  0.5f, 0.0f,
			-0.5f,  0.5f, 0.0f
		};


		Ref<VertexBuffer> squareVB;
		squareVB.reset(VertexBuffer::Create(squareVertices, sizeof(squareVertices)));

		BufferLayout squareVBLayout = {
			{ShaderDataType::Float3,"a_Posioton"},
		};
		squareVB->SetLayout(squareVBLayout);
		m_data->QuadVertexArray->AddVertexBuffer(squareVB);

		uint32_t squareIndices[6] = { 0,1,2,2,3,0 };
		Ref<IndexBuffer> squareIB;
		squareIB.reset(IndexBuffer::Create(squareIndices, sizeof(squareIndices) / sizeof(uint32_t)));
		m_data->QuadVertexArray->SetIndexBuffer(squareIB);

		m_data->FlatColorShader = Shader::Create("assets/shaders/FlatColor.glsl");


	}
	void Renderer2D::Shutdown()
	{
		delete m_data;
	}
	void Renderer2D::BeginScene(const OrthographicCamera& carmera)
	{
		std::dynamic_pointer_cast<TomCat::OpenGLShader>(m_data->FlatColorShader)->Bind();
		std::dynamic_pointer_cast<TomCat::OpenGLShader>(m_data->FlatColorShader)->UploadUniformMat4("u_ViewProjection",carmera.GetViewProjectionMatrix());
		std::dynamic_pointer_cast<TomCat::OpenGLShader>(m_data->FlatColorShader)->UploadUniformMat4("u_Transform",glm::mat4(1.0f));
	}
	void Renderer2D::EndScene()
	{

	}
	void Renderer2D::DrawQuad(const glm::vec2& position, const glm::vec2& size, const glm::vec4& color)
	{
		DrawQuad({ position.x,position.y,0.0f },size,color);
	}
	void Renderer2D::DrawQuad(const glm::vec3& position, const glm::vec2& size, const glm::vec4& color)
	{
		std::dynamic_pointer_cast<TomCat::OpenGLShader>(m_data->FlatColorShader)->Bind();
		std::dynamic_pointer_cast<TomCat::OpenGLShader>(m_data->FlatColorShader)->UploadUniformFloat4("u_Color", color);

		m_data->QuadVertexArray->Bind();
		RenderCommand::DrawIndexed(m_data->QuadVertexArray);
	}
}