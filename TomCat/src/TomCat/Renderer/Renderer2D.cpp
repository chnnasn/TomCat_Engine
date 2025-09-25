#include"tcpch.h"
#include"Renderer2D.h"
#include"VertexArray.h"
#include"shader.h"
#include "RenderCommand.h"
#include <glm/gtc/matrix_transform.hpp>

namespace	TomCat {

	struct  Renderer2DStorage
	{
		Ref<VertexArray> QuadVertexArray;
		Ref<Shader> FlatColorShader;
		Ref<Shader> TextureShader;
	};

	static Renderer2DStorage* m_data;

	void Renderer2D::Init()
	{
		m_data = new Renderer2DStorage();

		m_data->QuadVertexArray = VertexArray::Create();

		float squareVertices[5 * 4] = {
			-0.5f, -0.5f, 0.0f, 0.0f, 0.0f,
			 0.5f, -0.5f, 0.0f, 1.0f, 0.0f,
			 0.5f,  0.5f, 0.0f, 1.0f, 1.0f,
			-0.5f,  0.5f, 0.0f, 0.0f, 1.0f
		};


		Ref<VertexBuffer> squareVB;
		squareVB.reset(VertexBuffer::Create(squareVertices, sizeof(squareVertices)));

		BufferLayout squareVBLayout = {
			{ ShaderDataType::Float3, "a_Position" },
			{ ShaderDataType::Float2, "a_TexCoord" }
		};
		squareVB->SetLayout(squareVBLayout);
		m_data->QuadVertexArray->AddVertexBuffer(squareVB);

		uint32_t squareIndices[6] = { 0,1,2,2,3,0 };
		Ref<IndexBuffer> squareIB;
		squareIB.reset(IndexBuffer::Create(squareIndices, sizeof(squareIndices) / sizeof(uint32_t)));
		m_data->QuadVertexArray->SetIndexBuffer(squareIB);

		m_data->FlatColorShader = Shader::Create("assets/shaders/FlatColor.glsl");
		m_data->TextureShader = Shader::Create("assets/shaders/Texture.glsl");
		m_data->TextureShader->Bind();
		m_data->TextureShader->SetInt("u_Texture",0);

	}
	void Renderer2D::Shutdown()
	{
		delete m_data;
	}
	void Renderer2D::BeginScene(const OrthographicCamera& carmera)
	{
		m_data->FlatColorShader->Bind();
		m_data->FlatColorShader->SetMat4("u_ViewProjection",carmera.GetViewProjectionMatrix());

		m_data->TextureShader->Bind();
		m_data->TextureShader->SetMat4("u_ViewProjection",carmera.GetViewProjectionMatrix());

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
		m_data->FlatColorShader->Bind();
		m_data->FlatColorShader->SetFloat4("u_Color", color);

		glm::mat4 transform = glm::translate(glm::mat4(1.0f), position) * glm::scale(glm::mat4(1.0f), { size.x, size.y, 1.0f });
		m_data->FlatColorShader->SetMat4("u_Transform", transform	);

		m_data->QuadVertexArray->Bind();
		RenderCommand::DrawIndexed(m_data->QuadVertexArray);
	}

	void Renderer2D::DrawQuad(const glm::vec2& position, const glm::vec2& size, const Ref<Texture2D>& texture)
	{
		DrawQuad({ position.x,position.y,0.0f }, size, texture);

	}

	void Renderer2D::DrawQuad(const glm::vec3& position, const glm::vec2& size, const Ref<Texture2D>& texture)
	{

		m_data->TextureShader->Bind();

		glm::mat4 transform = glm::translate(glm::mat4(1.0f), position) * glm::scale(glm::mat4(1.0f), { size.x, size.y, 1.0f });
		m_data->TextureShader->SetMat4("u_Transform", transform);

		texture->Bind();

		m_data->QuadVertexArray->Bind();
		RenderCommand::DrawIndexed(m_data->QuadVertexArray);
	}
}