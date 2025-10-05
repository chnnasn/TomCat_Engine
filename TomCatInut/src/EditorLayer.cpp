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

		m_CheckerboardTexture = Texture2D::Create("assets/textures/Checkerboard.png");

		FramebufferSpecification fbSpec;
		fbSpec.Width = 1920;
		fbSpec.Height = 1080;
		m_Framebuffer = Framebuffer::Create(fbSpec);
		
		// Setup UI style
		m_UIManager->SetupImGuiStyle();

		m_ActiveScene = CreateRef<Scene>();
		auto square = m_ActiveScene->CreateEntity("Square");
		square.AddComponent<SpriteRenderer>(glm::vec4{0.0f,1.0f,0.0f,1.0f});

		m_CameraEntity = m_ActiveScene->CreateEntity("Camera");
		m_CameraEntity.AddComponent<C_Camera>();
	}

	void EditorLayer::OnDetach()
	{
		TC_PROFILE_FUNCTION();
	}

	void EditorLayer::OnUpdate(Timestep ts)
	{
		TC_PROFILE_FUNCTION();

		m_SceneFocuse = m_UIManager->m_SceneFocuse;

		if (FramebufferSpecification spec = m_Framebuffer->GetSpecification();
			m_ViewportSize.x > 0.0f && m_ViewportSize.y > 0.0f && // zero sized framebuffer is invalid
			(spec.Width != m_ViewportSize.x || spec.Height != m_ViewportSize.y))
		{
			m_Framebuffer->Resize((uint32_t)m_ViewportSize.x, (uint32_t)m_ViewportSize.y);
			m_CameraController.OnResize(m_ViewportSize.x, m_ViewportSize.y);
		}

		// Update
		if(m_SceneFocuse)
			m_CameraController.OnUpdate(ts);

		// Render
		Renderer2D::ResetStats();
		m_Framebuffer->Bind();
		RenderCommand::SetClearColor({0.1f,0.1f,0.1f,1.0f});
		RenderCommand::Clear();

		m_ActiveScene->OnUpdate(ts);

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
		
		// 更新视口大小
		// 先记录当前窗口大小，在绘制视口前更新m_ViewportSize
		ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
		ImGui::Begin("Scene");
		ImVec2 viewportPanelSize = ImGui::GetContentRegionAvail();
		if (m_ViewportSize != *((glm::vec2*)&viewportPanelSize) && viewportPanelSize.x > 0 && viewportPanelSize.y > 0)
		{
		    m_Framebuffer->Resize( (uint32_t)viewportPanelSize.x, (uint32_t)viewportPanelSize.y );
		    m_ViewportSize = { viewportPanelSize.x, viewportPanelSize.y };
		    m_ActiveScene->OnViewportResize((uint32_t)m_ViewportSize.x, (uint32_t)m_ViewportSize.y);
		    m_CameraController.OnResize( viewportPanelSize.x, viewportPanelSize.y );
		}
		ImGui::End();
		ImGui::PopStyleVar();

		// Viewport panel with framebuffer texture
		uint32_t textureID = m_Framebuffer->GetColorAttachmentRendererID();
		// 然后绘制视口面板
		m_UIManager->DrawViewportPanel(textureID, m_ViewportSize);
	}

	void EditorLayer::OnEvent(Event& e)
	{
		m_CameraController.OnEvent(e);
	}
}