#include "Z_Examples2D.h"
#include "imgui/imgui.h"

#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <chrono>


Z_Examples2D::Z_Examples2D() : Layer("Z_Examples2D"), m_CameraController(1280.0f / 720.0f), m_SquareColor({ 0.2f, 0.3f, 0.8f, 1.0f })
{

}


void Z_Examples2D::OnAttach()
{
    TC_PROFILE_FUNCTION();
    m_CheckerboardTexture = TomCat::Texture2D::Create("assets/textures/Checkerboard.png");
}
    

void Z_Examples2D::OnDetach()
{
   
    TC_PROFILE_FUNCTION();
}

void Z_Examples2D::OnUpdate(TomCat::Timestep ts)
{
    TC_PROFILE_FUNCTION();

    // Update
    m_CameraController.OnUpdate(ts);

    // Render
    {
        TC_PROFILE_SCOPE("Renderer Prep");

        TomCat::RenderCommand::SetClearColor({ 0.1f, 0.1f, 0.1f, 1 });
        TomCat::RenderCommand::Clear();
    }

    {
        TC_PROFILE_SCOPE("Renderer Draw");
        TomCat::Renderer2D::BeginScene(m_CameraController.GetCamera());
        // TomCat::Renderer2D::DrawRotatedQuad({ -1.0f, 0.0f }, { 0.8f, 0.8f }, glm::radians(-45.0f), { 0.8f, 0.2f, 0.3f, 1.0f });
        TomCat::Renderer2D::DrawQuad({ -1.0f, 0.0f }, { 0.8f, 0.8f }, { 0.8f, 0.2f, 0.3f, 1.0f });
        TomCat::Renderer2D::DrawQuad({ 0.5f, -0.5f }, { 0.5f, 0.75f }, { 0.2f, 0.3f, 0.8f, 1.0f });
        TomCat::Renderer2D::DrawQuad({ -5.0f, -5.0f, -0.1f }, { 10.0f, 10.0f }, m_CheckerboardTexture, 10.0f);
        TomCat::Renderer2D::DrawQuad({ -0.5f, -0.5f, 0.0f }, { 1.0f, 1.0f }, m_CheckerboardTexture, 20.0f);
        TomCat::Renderer2D::EndScene();

    }  
}

void Z_Examples2D::OnImGuiRender()
{
    TC_PROFILE_FUNCTION();

        ImGui::Begin("Settings");
        //auto stats = TomCat::Renderer2D::GetStats();
        ImGui::Text("Renderer2D Stats:");
        //ImGui::Text("Draw Calls: %d", stats.DrawCalls);
        //ImGui::Text("Quads: %d", stats.QuadCount);
        //ImGui::Text("Vertices: %d", stats.GetTotalVertexCount());
        //ImGui::Text("Indices: %d", stats.GetTotalIndexCount());
        ImGui::ColorEdit4("Square Color", glm::value_ptr(m_SquareColor));
        ImGui::End();
}

void Z_Examples2D::OnEvent(TomCat::Event& e)
{
	m_CameraController.OnEvent(e);
}
