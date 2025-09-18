#include "tcpch.h"
#include "Renderer.h"

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
		shader->UploadUniformMat4("u_ViewProjection", m_SceneData->VertexProjectinMatrix);
		shader->UploadUniformMat4("u_Transform",transform);

		vertexArray->Bind();
		RenderCommand::DrawIndexed(vertexArray);
	}
}