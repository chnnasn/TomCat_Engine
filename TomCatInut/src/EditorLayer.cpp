#include "EditorLayer.h"
#include <imgui/imgui.h>

#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <ImGui/imgui_internal.h>

namespace TomCat {

	EditorLayer::EditorLayer()
		: Layer("EditorLayer"), m_CameraController(1920.0f / 1080.0f), m_SquareColor({ 0.2f, 0.3f, 0.8f, 1.0f })
	{
		// Create UI manager
		m_UIManager = std::make_unique<EditorUIManager>();
	}

	void EditorLayer::OnAttach()
	{
		TC_PROFILE_FUNCTION();

		m_CheckerboardTexture = TomCat::Texture2D::Create("assets/textures/Checkerboard.png");

		TomCat::FramebufferSpecification fbSpec;
		fbSpec.Width = 1920;
		fbSpec.Height = 1080;
		m_Framebuffer = TomCat::Framebuffer::Create(fbSpec);
		
		// Setup UI style
		m_UIManager->SetupImGuiStyle();

		m_ActiveScene = CreateRef<Scene>();
		auto square = m_ActiveScene->CreateEntity();
		m_ActiveScene->Reg().emplace<Transform>(square);
		m_ActiveScene->Reg().emplace<SpriteRenderer>(square, glm::vec4{0.0f,1.0f,0.0f,1.0f});

	}

	void EditorLayer::OnDetach()
	{
		TC_PROFILE_FUNCTION();
	}

	void EditorLayer::OnUpdate(TomCat::Timestep ts)
	{
		TC_PROFILE_FUNCTION();

		m_SceneFocuse = m_UIManager->m_SceneFocuse;

		// Update
		if(m_SceneFocuse)
			m_CameraController.OnUpdate(ts);

		// 同步场景背景颜色
		m_SceneBackgroundColor = m_UIManager->m_SceneBackgroundColor;

		// Render
		TomCat::Renderer2D::ResetStats();
		m_Framebuffer->Bind();
		TomCat::RenderCommand::SetClearColor(m_SceneBackgroundColor);
		TomCat::RenderCommand::Clear();

		Renderer2D::BeginScene(m_CameraController.GetCamera());

		m_ActiveScene->OnUpdate(ts);
		Renderer2D::EndScene();

		m_Framebuffer->Unbind();

	}

	void EditorLayer::OnImGuiRender()
	{
		// 设置DockSpace
		m_UIManager->SetupDockSpace();

		// 独立窗口：Toolbar（始终在最上方）
		m_UIManager->DrawToolbar();

		// 独立窗口：StatusBar（始终在最下方）
		m_UIManager->DrawStatusBar();

		// 其他面板
		if (m_UIManager->m_ShowHierarchy) m_UIManager->DrawHierarchyPanel();
		if (m_UIManager->m_ShowInspector) m_UIManager->DrawInspectorPanel();
		if (m_UIManager->m_ShowProject) m_UIManager->DrawProjectPanel();
		if (m_UIManager->m_ShowConsole) m_UIManager->DrawConsolePanel();
		if (m_UIManager->m_ShowSceneSettings) m_UIManager->DrawSceneSettingsPanel();
		
		// Viewport panel with framebuffer texture
		uint32_t textureID = m_Framebuffer->GetColorAttachmentRendererID();
		m_UIManager->DrawViewportPanel(textureID, m_ViewportSize);
	}

	void EditorLayer::OnEvent(TomCat::Event& e)
	{
		m_CameraController.OnEvent(e);
	}
}