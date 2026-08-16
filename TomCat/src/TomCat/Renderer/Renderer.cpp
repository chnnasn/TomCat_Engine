#include "tcpch.h"
#include "Renderer.h"
#include "platform/OpenGL/OpenGLShader.h"
#include "Renderer2D.h"
#include "Renderer3D.h"

namespace TomCat {

Scope<Renderer::SceneData> Renderer::m_SceneData = CreateScope<Renderer::SceneData>();

	void Renderer::Init()
	{
		TC_PROFILE_FUNCTION();

		RenderCommand::Init();
		Renderer2D::Init();
		Renderer3D::Init();
	}

	void Renderer::Shutdown()
	{
		Renderer2D::Shutdown();
		Renderer3D::Shutdown();
	}


	void Renderer::OnWindowResize(uint32_t width, uint32_t height)
	{
		RenderCommand::SetViewport(0, 0, width, height);
	}

	void Renderer::BeginScene(OrthographicCamera& Camera)
	{
		m_SceneData->ViewProjectionMatrix = Camera.GetViewProjectionMatrix();
	}
	void Renderer::EndScene()
	{
	}
	void Renderer::Submit(const Ref<Shader>& shader , const Ref<VertexArray>& vertexArray, const glm::mat4& transform)
	{
		shader->Bind();
		shader->SetMat4("u_ViewProjection", m_SceneData->ViewProjectionMatrix);
		shader->SetMat4("u_Transform", transform);

		vertexArray->Bind();
		RenderCommand::DrawIndexed(vertexArray);
	}
}