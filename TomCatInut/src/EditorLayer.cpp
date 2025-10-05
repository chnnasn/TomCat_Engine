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
		auto square = m_ActiveScene->CreateEntity("Square");
		square.AddComponent<SpriteRenderer>(glm::vec4{0.0f,1.0f,0.0f,1.0f});

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

		// 如果视口大小发生变化，更新帧缓冲区大小和摄像机的宽高比
		if (m_ViewportSize.x > 0.0f && m_ViewportSize.y > 0.0f) {
			// 更新帧缓冲区大小
			TomCat::FramebufferSpecification spec = m_Framebuffer->GetSpecification();
			if (spec.Width != (uint32_t)m_ViewportSize.x || spec.Height != (uint32_t)m_ViewportSize.y) {
				spec.Width = (uint32_t)m_ViewportSize.x;
				spec.Height = (uint32_t)m_ViewportSize.y;
				m_Framebuffer->Resize(spec.Width, spec.Height);
				
				// 更新摄像机的宽高比
				float aspectRatio = m_ViewportSize.x / m_ViewportSize.y;
				m_CameraController.OnResize(m_ViewportSize.x, m_ViewportSize.y);
			}
		}

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
		// 先保存当前的视口大小
		ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
		ImGui::Begin("Scene");
		ImVec2 ViewportPanelSize = ImGui::GetContentRegionAvail();
		m_ViewportSize = { ViewportPanelSize.x, ViewportPanelSize.y };
		ImGui::End();
		ImGui::PopStyleVar();
		// 然后绘制视口面板
		m_UIManager->DrawViewportPanel(textureID, m_ViewportSize);
	}

	void EditorLayer::OnEvent(TomCat::Event& e)
	{
		m_CameraController.OnEvent(e);
	}
}