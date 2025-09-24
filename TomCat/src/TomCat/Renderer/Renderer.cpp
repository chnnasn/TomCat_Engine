#include "tcpch.h"
#include "Renderer.h"
#include "platform/OpenGL/OpenGLShader.h"

namespace TomCat {

	Renderer:: SceneData* Renderer::m_SceneData = new Renderer::SceneData;

	void Renderer::Init() 
	{
		RenderCommand::Init();
	}

	void Renderer::OnWindowResize(uint32_t width, uint32_t height)
	{
		RenderCommand::SetViewport(0, 0, width, height);
	}

	void Renderer::BeginScene(OrthographicCamera& Camera)
	{
		m_SceneData->VertexProjectinMatrix = Camera.GetViewProjectionMatrix();
	}
	void Renderer::EndScene()
	{
	}
	void Renderer::Submit(const Ref<Shader>& shader , const Ref<VertexArray>& vertexArray, const glm::mat4& transform)
	{
		shader->Bind();
		std::dynamic_pointer_cast<OpenGLShader>(shader)->UploadUniformMat4("u_ViewProjection", m_SceneData->VertexProjectinMatrix);
		std::dynamic_pointer_cast<OpenGLShader>(shader)->UploadUniformMat4("u_Transform",transform);

		vertexArray->Bind();
		RenderCommand::DrawIndexed(vertexArray);
	}
}