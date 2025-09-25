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
		Ref<Shader> TextureShader;
		Ref<Texture2D> WhiteTexture;
	};

	static Renderer2DStorage* m_data;

	void Renderer2D::Init()
	{
		TC_PROFILE_FUNCTION();

		m_data = new Renderer2DStorage();

		m_data->QuadVertexArray = VertexArray::Create();

		float squareVertices[5 * 4] = {
			-0.5f, -0.5f, 0.0f, 0.0f, 0.0f,
			 0.5f, -0.5f, 0.0f, 1.0f, 0.0f,
			 0.5f,  0.5f, 0.0f, 1.0f, 1.0f,
			-0.5f,  0.5f, 0.0f, 0.0f, 1.0f
		};



        Ref<VertexBuffer> squareVB = VertexBuffer::Create(squareVertices, sizeof(squareVertices));

        BufferLayout squareVBLayout = {
            { ShaderDataType::Float3, "a_Position" },
            { ShaderDataType::Float2, "a_TexCoord" }
        };
        squareVB->SetLayout(squareVBLayout);
        m_data->QuadVertexArray->AddVertexBuffer(squareVB);

		uint32_t squareIndices[6] = { 0,1,2,2,3,0 };
		Ref<IndexBuffer> squareIB = IndexBuffer::Create(squareIndices, sizeof(squareIndices) / sizeof(uint32_t));
		m_data->QuadVertexArray->SetIndexBuffer(squareIB);

		m_data->WhiteTexture = Texture2D::Create(1, 1);
		uint32_t whiteTextureData = 0xffffffff;
		m_data->WhiteTexture->SetData(&whiteTextureData, sizeof(uint32_t));

		m_data->TextureShader = Shader::Create("assets/shaders/Texture.glsl");
		m_data->TextureShader->Bind();
		m_data->TextureShader->SetInt("u_Texture",0);

	}
	void Renderer2D::Shutdown()
	{
		TC_PROFILE_FUNCTION();

		delete m_data;
	}
	void Renderer2D::BeginScene(const OrthographicCamera& carmera)
	{
		TC_PROFILE_FUNCTION();

		m_data->TextureShader->Bind();
		m_data->TextureShader->SetMat4("u_ViewProjection",carmera.GetViewProjectionMatrix());

	}
	void Renderer2D::EndScene()
	{
		TC_PROFILE_FUNCTION();
	}
	void Renderer2D::DrawQuad(const glm::vec2& position, const glm::vec2& size, const glm::vec4& color)
	{
		DrawQuad({ position.x,position.y,0.0f },size,color);
	}
	void Renderer2D::DrawQuad(const glm::vec3& position, const glm::vec2& size, const glm::vec4& color)
	{
		TC_PROFILE_FUNCTION();

		m_data->TextureShader->SetFloat4("u_Color", color);
		m_data->WhiteTexture->Bind();

		glm::mat4 transform = glm::translate(glm::mat4(1.0f), position) * glm::scale(glm::mat4(1.0f), { size.x, size.y, 1.0f });
		m_data->TextureShader->SetMat4("u_Transform", transform	);

		m_data->QuadVertexArray->Bind();
		RenderCommand::DrawIndexed(m_data->QuadVertexArray);
	}

	void Renderer2D::DrawQuad(const glm::vec2& position, const glm::vec2& size, const Ref<Texture2D>& texture)
	{
		DrawQuad({ position.x,position.y,0.0f }, size, texture);

	}

	void Renderer2D::DrawQuad(const glm::vec3& position, const glm::vec2& size, const Ref<Texture2D>& texture)
	{
		TC_PROFILE_FUNCTION();

		m_data->TextureShader->SetFloat4("u_Color", glm::vec4{1.0f});
		texture->Bind();

		glm::mat4 transform = glm::translate(glm::mat4(1.0f), position) * glm::scale(glm::mat4(1.0f), { size.x, size.y, 1.0f });
		m_data->TextureShader->SetMat4("u_Transform", transform);

		m_data->QuadVertexArray->Bind();
		RenderCommand::DrawIndexed(m_data->QuadVertexArray);
	}
}