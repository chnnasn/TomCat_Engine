#include "Z_Examples2D.h"

#include "platform/OpenGL/OpenGLShader.h"
#include "ImGui/imgui.h"

#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>

Z_Examples2D::Z_Examples2D():Layer("Z_Examples2D"),m_CameraController(1280.0f / 720.0f)
{

}


void Z_Examples2D::OnAttach()
{
	m_SquareVA = TomCat::VertexArray::Create();

	float squareVertices[5 * 4] = {
		-0.5f, -0.5f, 0.0f,
		 0.5f, -0.5f, 0.0f,
		 0.5f,  0.5f, 0.0f,
		-0.5f,  0.5f, 0.0f
	};


	TomCat::Ref<TomCat::VertexBuffer> squareVB;
	squareVB.reset(TomCat::VertexBuffer::Create(squareVertices, sizeof(squareVertices)));

	TomCat::BufferLayout squareVBLayout = {
		{TomCat::ShaderDataType::Float3,"a_Posioton"},
	};
	squareVB->SetLayout(squareVBLayout);
	m_SquareVA->AddVertexBuffer(squareVB);

	uint32_t squareIndices[6] = { 0,1,2,2,3,0 };
	TomCat::Ref<TomCat::IndexBuffer> squareIB;
	squareIB.reset(TomCat::IndexBuffer::Create(squareIndices, sizeof(squareIndices) / sizeof(uint32_t)));
	m_SquareVA->SetIndexBuffer(squareIB);

	m_FlatColorShader = TomCat::Shader::Create("assets/shaders/FlatColor.glsl");


}

void Z_Examples2D::OnDetach()
{
}

void Z_Examples2D::OnUpdate(TomCat::Timestep ts)
{
	//调用摄像机
	m_CameraController.OnUpdate(ts);

	//渲染
	TomCat::RenderCommand::SetClearColor({ 0.1f, 0.1f, 0.1f, 1 });
	TomCat::RenderCommand::Clear();

	TomCat::Renderer::BeginScene(m_CameraController.GetCamera());

	std::dynamic_pointer_cast<TomCat::OpenGLShader>(m_FlatColorShader)->Bind();
	std::dynamic_pointer_cast<TomCat::OpenGLShader>(m_FlatColorShader)->UploadUniformFloat4("u_Color", m_SquareColor);

	TomCat::Renderer::Submit(m_FlatColorShader, m_SquareVA, glm::scale(glm::mat4(1.0f), glm::vec3(1.5f)));

	TomCat::Renderer::EndScene();

}

void Z_Examples2D::OnImGuiRender()
{
	ImGui::Begin("Settings");
	ImGui::ColorEdit4("Square Color", glm::value_ptr(m_SquareColor));
	ImGui::End();
}

void Z_Examples2D::OnEvent(TomCat::Event& e)
{
	m_CameraController.OnEvent(e);
}
