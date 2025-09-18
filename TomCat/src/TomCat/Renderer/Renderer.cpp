#include "tcpch.h"
#include "Renderer.h"
#include "platform/OpenGL/OpenGLShader.h"

namespace TomCat {

	Renderer:: SceneData* Renderer::m_SceneData = new Renderer::SceneData;

	void Renderer::BeginScene(OrthographicCamera& Camera)
	{
		m_SceneData->VertexProjectinMatrix = Camera.GetViewProjectionMatrix();
	}
	void Renderer::EndScene()
	{
	}
	void Renderer::Submit(const std::shared_ptr<Shader>& shader , const std::shared_ptr<VertexArray>& vertexArray, const glm::mat4& transform)
	{
		shader->Bind();
		std::dynamic_pointer_cast<OpenGLShader>(shader)->UploadUniformMat4("u_ViewProjection", m_SceneData->VertexProjectinMatrix);
		std::dynamic_pointer_cast<OpenGLShader>(shader)->UploadUniformMat4("u_Transform",transform);

		vertexArray->Bind();
		RenderCommand::DrawIndexed(vertexArray);
	}
}