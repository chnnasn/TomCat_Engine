#include "Z_Examples2D.h"
#include "imgui/imgui.h"

#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>


Z_Examples2D::Z_Examples2D():Layer("Z_Examples2D"),m_CameraController(1280.0f / 720.0f)
{

}


void Z_Examples2D::OnAttach()
{
    m_CheckerboardTexture = TomCat::Texture2D::Create("assets/textures/Checkerboard.png");
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

	TomCat::Renderer2D::BeginScene(m_CameraController.GetCamera());
    TomCat::Renderer2D::DrawQuad({-1.0f,0.0f}, {0.8f,0.8f}, {0.8f,0.2f,0.3f,1.0f});
    TomCat::Renderer2D::DrawQuad({0.5f,-0.5f}, {0.5f,0.75f},{0.2f,0.3f,0.8f,1.0f});
    TomCat::Renderer2D::DrawQuad({ 0.0f, 0.0f, -0.1f }, { 10.0f, 10.0f }, m_CheckerboardTexture);
    TomCat::Renderer2D::EndScene();


}

void Z_Examples2D::OnImGuiRender()
{

    //static bool DockSpace = true;
    //static bool opt_fullscreen = true;
    //static bool opt_padding = false;
    //static ImGuiDockNodeFlags dockspace_flags = ImGuiDockNodeFlags_None;

    //// We are using the ImGuiWindowFlags_NoDocking flag to make the parent window not dockable into,
    //// because it would be confusing to have two docking targets within each others.
    //ImGuiWindowFlags window_flags = ImGuiWindowFlags_MenuBar | ImGuiWindowFlags_NoDocking;
    //if (opt_fullscreen)
    //{
    //    const ImGuiViewport* viewport = ImGui::GetMainViewport();
    //    ImGui::SetNextWindowPos(viewport->WorkPos);
    //    ImGui::SetNextWindowSize(viewport->WorkSize);
    //    ImGui::SetNextWindowViewport(viewport->ID);
    //    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
    //    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
    //    window_flags |= ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove;
    //    window_flags |= ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoNavFocus;
    //}
    //else
    //{
    //    dockspace_flags &= ~ImGuiDockNodeFlags_PassthruCentralNode;
    //}

    //// When using ImGuiDockNodeFlags_PassthruCentralNode, DockSpace() will render our background
    //// and handle the pass-thru hole, so we ask Begin() to not render a background.
    //if (dockspace_flags & ImGuiDockNodeFlags_PassthruCentralNode)
    //    window_flags |= ImGuiWindowFlags_NoBackground;

    //// Important: note that we proceed even if Begin() returns false (aka window is collapsed).
    //// This is because we want to keep our DockSpace() active. If a DockSpace() is inactive,
    //// all active windows docked into it will lose their parent and become undocked.
    //// We cannot preserve the docking relationship between an active window and an inactive docking, otherwise
    //// any change of dockspace/settings would lead to windows being stuck in limbo and never being visible.
    //if (!opt_padding)
    //    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
    //ImGui::Begin("DockSpace Demo", &DockSpace, window_flags);
    //if (!opt_padding)
    //    ImGui::PopStyleVar();

    //if (opt_fullscreen)
    //    ImGui::PopStyleVar(2);

    //// Submit the DockSpace
    //ImGuiIO& io = ImGui::GetIO();
    //if (io.ConfigFlags & ImGuiConfigFlags_DockingEnable)
    //{
    //    ImGuiID dockspace_id = ImGui::GetID("MyDockSpace");
    //    ImGui::DockSpace(dockspace_id, ImVec2(0.0f, 0.0f), dockspace_flags);
    //}

    //if (ImGui::BeginMenuBar())
    //{
    //    if (ImGui::BeginMenu("File"))
    //    {
    //        // Disabling fullscreen would allow the window to be moved to the front of other windows,
    //        // which we can't undo at the moment without finer window depth/z control.
    //        
    //        if (ImGui::MenuItem("Exit"))TomCat::Application::Get().Close();
    //        ImGui::EndMenu();
    //    }
    //    ImGui::EndMenuBar();
    //}


    ImGui::Begin("Settings");
    ImGui::ColorEdit4("Square Color", glm::value_ptr(m_SquareColor));
    //uint32_t textureID = m_checkerboardTexture->GetRendererID();
    //ImGui::Image((void*)textureID->GetRendererID(), ImVec2{64.0f,64.0f});

    ImGui::End();

    //ImGui::End();
}

void Z_Examples2D::OnEvent(TomCat::Event& e)
{
	m_CameraController.OnEvent(e);
}
