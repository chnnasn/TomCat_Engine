#include "EditorLayer.h"
#include <imgui/imgui.h>

#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>

namespace TomCat {

	EditorLayer::EditorLayer()
		: Layer("EditorLayer"), m_CameraController(1920.0f / 1080.0f), m_SquareColor({ 0.2f, 0.3f, 0.8f, 1.0f })
	{
		// Initialize scene objects
		AddSceneObject("Main Camera");
		AddSceneObject("Directional Light");
		AddSceneObject("Ground");
		AddSceneObject("Cube");
		AddSceneObject("Sphere");
		
		// Initialize project files
		m_ProjectFiles = {
			"Assets/Scenes/Main.unity",
			"Assets/Scripts/Player.cs",
			"Assets/Materials/Default.mat",
			"Assets/Textures/Checkerboard.png",
			"Assets/Textures/ChernoLogo.png",
			"Assets/Shaders/FlatColor.glsl",
			"Assets/Shaders/Texture.glsl"
		};
		
		// Initialize console logs
		m_ConsoleLogs = {
			"[INFO] TomCat Engine initialized successfully",
			"[INFO] OpenGL context created",
			"[INFO] ImGui initialized",
			"[WARNING] No scene file loaded",
			"[INFO] Ready to start editing"
		};
	}

	void EditorLayer::OnAttach()
	{
		TC_PROFILE_FUNCTION();

		m_CheckerboardTexture = TomCat::Texture2D::Create("assets/textures/Checkerboard.png");

		TomCat::FramebufferSpecification fbSpec;
		fbSpec.Width = 1920;
		fbSpec.Height = 1080;
		m_Framebuffer = TomCat::Framebuffer::Create(fbSpec);
		
		SetupImGuiStyle();
	}

	void EditorLayer::OnDetach()
	{
		TC_PROFILE_FUNCTION();
	}

	void EditorLayer::OnUpdate(TomCat::Timestep ts)
	{
		TC_PROFILE_FUNCTION();

		// Update
		m_CameraController.OnUpdate(ts);

		// Render
		TomCat::Renderer2D::ResetStats();
		{
			TC_PROFILE_SCOPE("Renderer Prep");
			m_Framebuffer->Bind();
			TomCat::RenderCommand::SetClearColor(m_SceneBackgroundColor);
			TomCat::RenderCommand::Clear();
		}

		{
			static float rotation = 0.0f;
			rotation += ts * 50.0f;

			TC_PROFILE_SCOPE("Renderer Draw");
			TomCat::Renderer2D::BeginScene(m_CameraController.GetCamera());
			TomCat::Renderer2D::DrawRotatedQuad({ 1.0f, 0.0f }, { 0.8f, 0.8f }, -45.0f, { 0.8f, 0.2f, 0.3f, 1.0f });
			TomCat::Renderer2D::DrawQuad({ -1.0f, 0.0f }, { 0.8f, 0.8f }, { 0.8f, 0.2f, 0.3f, 1.0f });
			TomCat::Renderer2D::DrawQuad({ 0.5f, -0.5f }, { 0.5f, 0.75f }, m_SquareColor);
			TomCat::Renderer2D::DrawQuad({ 0.0f, 0.0f, -0.1f }, { 20.0f, 20.0f }, m_CheckerboardTexture, 10.0f);
			TomCat::Renderer2D::DrawRotatedQuad({ -2.0f, 0.0f, 0.0f }, { 1.0f, 1.0f }, rotation, m_CheckerboardTexture, 20.0f);
			TomCat::Renderer2D::EndScene();

			TomCat::Renderer2D::BeginScene(m_CameraController.GetCamera());
			for (float y = -5.0f; y < 5.0f; y += 0.5f)
			{
				for (float x = -5.0f; x < 5.0f; x += 0.5f)
				{
					glm::vec4 color = { (x + 5.0f) / 10.0f, 0.4f, (y + 5.0f) / 10.0f, 0.7f };
					TomCat::Renderer2D::DrawQuad({ x, y }, { 0.45f, 0.45f }, color);
				}
			}
			TomCat::Renderer2D::EndScene();
			m_Framebuffer->Unbind();
		}
	}

	void EditorLayer::OnImGuiRender()
	{
		TC_PROFILE_FUNCTION();

		static bool dockspaceOpen = true;
		static bool opt_fullscreen_persistant = true;
		bool opt_fullscreen = opt_fullscreen_persistant;
		static ImGuiDockNodeFlags dockspace_flags = ImGuiDockNodeFlags_None;

		ImGuiWindowFlags window_flags = ImGuiWindowFlags_MenuBar | ImGuiWindowFlags_NoDocking;
		if (opt_fullscreen)
		{
			ImGuiViewport* viewport = ImGui::GetMainViewport();
			ImGui::SetNextWindowPos(viewport->Pos);
			ImGui::SetNextWindowSize(viewport->Size);
			ImGui::SetNextWindowViewport(viewport->ID);
			ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
			ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
			window_flags |= ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove;
			window_flags |= ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoNavFocus;
		}

		if (dockspace_flags & ImGuiDockNodeFlags_PassthruCentralNode)
			window_flags |= ImGuiWindowFlags_NoBackground;

		ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
		ImGui::Begin("TomCat Editor", &dockspaceOpen, window_flags);
		ImGui::PopStyleVar();

		if (opt_fullscreen)
			ImGui::PopStyleVar(2);

		// DockSpace
		ImGuiIO& io = ImGui::GetIO();
		if (io.ConfigFlags & ImGuiConfigFlags_DockingEnable)
		{
			ImGuiID dockspace_id = ImGui::GetID("TomCatDockSpace");
			ImGui::DockSpace(dockspace_id, ImVec2(0.0f, 0.0f), dockspace_flags);
		}

		// Draw UI Panels
		DrawMenuBar();
		DrawToolbar();
		
		if (m_ShowHierarchy) DrawHierarchyPanel();
		if (m_ShowInspector) DrawInspectorPanel();
		if (m_ShowProject) DrawProjectPanel();
		if (m_ShowConsole) DrawConsolePanel();
		if (m_ShowSceneSettings) DrawSceneSettingsPanel();
		
		DrawViewportPanel();
		DrawStatusBar();

		ImGui::End();
	}

	void EditorLayer::OnEvent(TomCat::Event& e)
	{
		m_CameraController.OnEvent(e);
	}

	void EditorLayer::SetupImGuiStyle()
	{
		ImGuiStyle& style = ImGui::GetStyle();
		
		// Increase font size
		ImGuiIO& io = ImGui::GetIO();
		// Try to load a larger font, fallback to default if not available
		ImFont* font = io.Fonts->AddFontFromFileTTF("C:/Windows/Fonts/arial.ttf", 28.0f);
		if (!font) {
			font = io.Fonts->AddFontDefault();
		}
		io.FontDefault = font;
		
		// Dark theme similar to Unity
		ImVec4* colors = style.Colors;
		colors[ImGuiCol_Text] = ImVec4(0.95f, 0.96f, 0.98f, 1.00f);
		colors[ImGuiCol_TextDisabled] = ImVec4(0.36f, 0.42f, 0.47f, 1.00f);
		colors[ImGuiCol_WindowBg] = ImVec4(0.11f, 0.15f, 0.17f, 1.00f);
		colors[ImGuiCol_ChildBg] = ImVec4(0.15f, 0.18f, 0.22f, 1.00f);
		colors[ImGuiCol_PopupBg] = ImVec4(0.08f, 0.08f, 0.08f, 0.94f);
		colors[ImGuiCol_Border] = ImVec4(0.08f, 0.10f, 0.12f, 1.00f);
		colors[ImGuiCol_BorderShadow] = ImVec4(0.00f, 0.00f, 0.00f, 0.00f);
		colors[ImGuiCol_FrameBg] = ImVec4(0.20f, 0.25f, 0.29f, 1.00f);
		colors[ImGuiCol_FrameBgHovered] = ImVec4(0.12f, 0.20f, 0.28f, 1.00f);
		colors[ImGuiCol_FrameBgActive] = ImVec4(0.09f, 0.12f, 0.14f, 1.00f);
		colors[ImGuiCol_TitleBg] = ImVec4(0.09f, 0.12f, 0.14f, 0.65f);
		colors[ImGuiCol_TitleBgActive] = ImVec4(0.08f, 0.10f, 0.12f, 1.00f);
		colors[ImGuiCol_TitleBgCollapsed] = ImVec4(0.00f, 0.00f, 0.00f, 0.51f);
		colors[ImGuiCol_MenuBarBg] = ImVec4(0.15f, 0.18f, 0.22f, 1.00f);
		colors[ImGuiCol_ScrollbarBg] = ImVec4(0.02f, 0.02f, 0.02f, 0.39f);
		colors[ImGuiCol_ScrollbarGrab] = ImVec4(0.20f, 0.25f, 0.29f, 1.00f);
		colors[ImGuiCol_ScrollbarGrabHovered] = ImVec4(0.18f, 0.22f, 0.25f, 1.00f);
		colors[ImGuiCol_ScrollbarGrabActive] = ImVec4(0.09f, 0.21f, 0.31f, 1.00f);
		colors[ImGuiCol_CheckMark] = ImVec4(0.28f, 0.56f, 1.00f, 1.00f);
		colors[ImGuiCol_SliderGrab] = ImVec4(0.28f, 0.56f, 1.00f, 1.00f);
		colors[ImGuiCol_SliderGrabActive] = ImVec4(0.37f, 0.61f, 1.00f, 1.00f);
		colors[ImGuiCol_Button] = ImVec4(0.20f, 0.25f, 0.29f, 1.00f);
		colors[ImGuiCol_ButtonHovered] = ImVec4(0.28f, 0.56f, 1.00f, 1.00f);
		colors[ImGuiCol_ButtonActive] = ImVec4(0.06f, 0.53f, 0.98f, 1.00f);
		colors[ImGuiCol_Header] = ImVec4(0.20f, 0.25f, 0.29f, 0.55f);
		colors[ImGuiCol_HeaderHovered] = ImVec4(0.26f, 0.59f, 0.98f, 0.80f);
		colors[ImGuiCol_HeaderActive] = ImVec4(0.26f, 0.59f, 0.98f, 1.00f);
		colors[ImGuiCol_Separator] = ImVec4(0.20f, 0.25f, 0.29f, 1.00f);
		colors[ImGuiCol_SeparatorHovered] = ImVec4(0.10f, 0.40f, 0.75f, 0.78f);
		colors[ImGuiCol_SeparatorActive] = ImVec4(0.10f, 0.40f, 0.75f, 1.00f);
		colors[ImGuiCol_ResizeGrip] = ImVec4(0.26f, 0.59f, 0.98f, 0.25f);
		colors[ImGuiCol_ResizeGripHovered] = ImVec4(0.26f, 0.59f, 0.98f, 0.67f);
		colors[ImGuiCol_ResizeGripActive] = ImVec4(0.26f, 0.59f, 0.98f, 0.95f);
		colors[ImGuiCol_Tab] = ImVec4(0.11f, 0.15f, 0.17f, 1.00f);
		colors[ImGuiCol_TabHovered] = ImVec4(0.26f, 0.59f, 0.98f, 0.80f);
		colors[ImGuiCol_TabActive] = ImVec4(0.20f, 0.25f, 0.29f, 1.00f);
		colors[ImGuiCol_TabUnfocused] = ImVec4(0.11f, 0.15f, 0.17f, 1.00f);
		colors[ImGuiCol_TabUnfocusedActive] = ImVec4(0.11f, 0.15f, 0.17f, 1.00f);
		colors[ImGuiCol_DockingPreview] = ImVec4(0.26f, 0.59f, 0.98f, 0.70f);
		colors[ImGuiCol_DockingEmptyBg] = ImVec4(0.20f, 0.20f, 0.20f, 1.00f);
		colors[ImGuiCol_PlotLines] = ImVec4(0.61f, 0.61f, 0.61f, 1.00f);
		colors[ImGuiCol_PlotLinesHovered] = ImVec4(1.00f, 0.43f, 0.35f, 1.00f);
		colors[ImGuiCol_PlotHistogram] = ImVec4(0.90f, 0.70f, 0.00f, 1.00f);
		colors[ImGuiCol_PlotHistogramHovered] = ImVec4(1.00f, 0.60f, 0.00f, 1.00f);
		colors[ImGuiCol_TextSelectedBg] = ImVec4(0.26f, 0.59f, 0.98f, 0.35f);
		colors[ImGuiCol_DragDropTarget] = ImVec4(1.00f, 1.00f, 0.00f, 0.90f);
		colors[ImGuiCol_NavHighlight] = ImVec4(0.26f, 0.59f, 0.98f, 1.00f);
		colors[ImGuiCol_NavWindowingHighlight] = ImVec4(1.00f, 1.00f, 1.00f, 0.70f);
		colors[ImGuiCol_NavWindowingDimBg] = ImVec4(0.80f, 0.80f, 0.80f, 0.20f);
		colors[ImGuiCol_ModalWindowDimBg] = ImVec4(0.80f, 0.80f, 0.80f, 0.35f);

		style.WindowPadding = ImVec2(12, 12);
		style.FramePadding = ImVec2(8, 6);
		style.CellPadding = ImVec2(6, 4);
		style.ItemSpacing = ImVec2(12, 6);
		style.ItemInnerSpacing = ImVec2(6, 6);
		style.IndentSpacing = 25;
		style.ScrollbarSize = 18;
		style.GrabMinSize = 16;

		style.WindowBorderSize = 1;
		style.ChildBorderSize = 1;
		style.PopupBorderSize = 1;
		style.FrameBorderSize = 0;
		style.TabBorderSize = 0;

		style.WindowRounding = 0;
		style.ChildRounding = 0;
		style.FrameRounding = 0;
		style.PopupRounding = 0;
		style.ScrollbarRounding = 9;
		style.GrabRounding = 3;
		style.LogSliderDeadzone = 4;
		style.TabRounding = 4;
	}

	void EditorLayer::DrawMenuBar()
	{
		if (ImGui::BeginMenuBar())
		{
			if (ImGui::BeginMenu("File"))
			{
				if (ImGui::MenuItem("New Scene", "Ctrl+N")) {
					// TODO: Implement new scene
				}
				if (ImGui::MenuItem("Open Scene", "Ctrl+O")) {
					// TODO: Implement open scene
				}
				if (ImGui::MenuItem("Save Scene", "Ctrl+S")) {
					// TODO: Implement save scene
				}
				ImGui::Separator();
				if (ImGui::MenuItem("Build Settings...")) {
					// TODO: Implement build settings
				}
				ImGui::Separator();
				if (ImGui::MenuItem("Exit")) TomCat::Application::Get().Close();
				ImGui::EndMenu();
			}

			if (ImGui::BeginMenu("Edit"))
			{
				if (ImGui::MenuItem("Undo", "Ctrl+Z")) {
					// TODO: Implement undo
				}
				if (ImGui::MenuItem("Redo", "Ctrl+Y")) {
					// TODO: Implement redo
				}
				ImGui::Separator();
				if (ImGui::MenuItem("Cut", "Ctrl+X")) {
					// TODO: Implement cut
				}
				if (ImGui::MenuItem("Copy", "Ctrl+C")) {
					// TODO: Implement copy
				}
				if (ImGui::MenuItem("Paste", "Ctrl+V")) {
					// TODO: Implement paste
				}
				ImGui::Separator();
				if (ImGui::MenuItem("Preferences...")) {
					// TODO: Implement preferences
				}
				ImGui::EndMenu();
			}

			if (ImGui::BeginMenu("GameObject"))
			{
				if (ImGui::MenuItem("Create Empty")) {
					AddSceneObject("GameObject");
				}
				if (ImGui::MenuItem("3D Object", nullptr, false, false)) {
					// TODO: Implement 3D object creation
				}
				if (ImGui::MenuItem("2D Object", nullptr, false, false)) {
					// TODO: Implement 2D object creation
				}
				ImGui::Separator();
				if (ImGui::MenuItem("Light", nullptr, false, false)) {
					// TODO: Implement light creation
				}
				if (ImGui::MenuItem("Audio", nullptr, false, false)) {
					// TODO: Implement audio creation
				}
				ImGui::EndMenu();
			}

			if (ImGui::BeginMenu("Window"))
			{
				if (ImGui::MenuItem("Hierarchy", nullptr, &m_ShowHierarchy)) {}
				if (ImGui::MenuItem("Inspector", nullptr, &m_ShowInspector)) {}
				if (ImGui::MenuItem("Project", nullptr, &m_ShowProject)) {}
				if (ImGui::MenuItem("Console", nullptr, &m_ShowConsole)) {}
				if (ImGui::MenuItem("Scene Settings", nullptr, &m_ShowSceneSettings)) {}
				ImGui::Separator();
				if (ImGui::MenuItem("Layouts", nullptr, false, false)) {
					// TODO: Implement layout management
				}
				ImGui::EndMenu();
			}

			ImGui::EndMenuBar();
		}
	}

	void EditorLayer::DrawToolbar()
	{
		ImGui::Begin("Toolbar");
		
		// Get available width for centering
		float availableWidth = ImGui::GetContentRegionAvail().x;
		float buttonWidth = 100.0f;
		float spacing = 10.0f;
		float totalButtonWidth = 3 * buttonWidth + 2 * spacing;
		float startX = (availableWidth - totalButtonWidth) * 0.5f;
		
		// Center the buttons
		if (startX > 0) {
			ImGui::SetCursorPosX(ImGui::GetCursorPosX() + startX);
		}
		
		// Play/Pause/Stop buttons
		if (ImGui::Button("Play", ImVec2(buttonWidth, 0))) {
			// TODO: Implement play mode
		}
		ImGui::SameLine();
		if (ImGui::Button("Pause", ImVec2(buttonWidth, 0))) {
			// TODO: Implement pause mode
		}
		ImGui::SameLine();
		if (ImGui::Button("Stop", ImVec2(buttonWidth, 0))) {
			// TODO: Implement stop mode
		}
		
		ImGui::SameLine();
		ImGui::Separator();
		ImGui::SameLine();
		
		// Transform tools - also centered
		float transformButtonWidth = 70.0f;
		float transformSpacing = 8.0f;
		float totalTransformWidth = 3 * transformButtonWidth + 2 * transformSpacing;
		float transformStartX = (availableWidth - totalTransformWidth) * 0.5f;
		
		if (transformStartX > 0) {
			ImGui::SetCursorPosX(ImGui::GetCursorPosX() + transformStartX);
		}
		
		if (ImGui::Button("Move", ImVec2(transformButtonWidth, 0))) {
			// TODO: Implement move tool
		}
		ImGui::SameLine();
		if (ImGui::Button("Rotate", ImVec2(transformButtonWidth, 0))) {
			// TODO: Implement rotate tool
		}
		ImGui::SameLine();
		if (ImGui::Button("Scale", ImVec2(transformButtonWidth, 0))) {
			// TODO: Implement scale tool
		}
		
		ImGui::SameLine();
		ImGui::Separator();
		ImGui::SameLine();
		
		// Layer dropdown - right aligned
		static int selectedLayer = 0;
		const char* layers[] = { "Default", "UI", "Background", "Foreground" };
		ImGui::SetNextItemWidth(120);
		ImGui::Combo("Layer", &selectedLayer, layers, IM_ARRAYSIZE(layers));
		
		ImGui::End();
	}

	void EditorLayer::DrawHierarchyPanel()
	{
		ImGui::Begin("Hierarchy");
		
		// Search bar
		static char searchBuffer[256] = "";
		ImGui::InputTextWithHint("##Search", "Search...", searchBuffer, sizeof(searchBuffer));
		
		ImGui::Separator();
		
		// Scene objects tree
		for (auto& obj : m_SceneObjects)
		{
			DrawSceneObjectTree(obj);
		}
		
		ImGui::End();
	}

	void EditorLayer::DrawInspectorPanel()
	{
		ImGui::Begin("Inspector");

		if (m_SelectedObject)
		{
			ImGui::Text("Selected: %s", m_SelectedObject->name.c_str());
			ImGui::Separator();

			if (ImGui::CollapsingHeader("Transform", ImGuiTreeNodeFlags_DefaultOpen))
			{
				// 设置固定的标签宽度
				const float labelWidth = 120.0f;

				// Position
				ImGui::Text("Position");
				ImGui::SameLine(labelWidth);
				ImGui::DragFloat3("##Position", glm::value_ptr(m_SelectedTransform.position), 0.1f);

				// Rotation
				ImGui::Text("Rotation");
				ImGui::SameLine(labelWidth);
				ImGui::DragFloat3("##Rotation", glm::value_ptr(m_SelectedTransform.rotation), 1.0f);

				// Scale
				ImGui::Text("Scale");
				ImGui::SameLine(labelWidth);
				ImGui::DragFloat3("##Scale", glm::value_ptr(m_SelectedTransform.scale), 0.1f);
			}
			// Add component button - 居中显示
			ImGui::Separator();

			// 计算按钮居中位置
			float buttonWidth = 240.0f; // 按钮预估宽度
			float windowWidth = ImGui::GetWindowSize().x;
			float cursorPosX = (windowWidth - buttonWidth) * 0.5f;

			ImGui::SetCursorPosX(cursorPosX);

			if (ImGui::Button("Add Component", ImVec2(buttonWidth, 0)))
			{
				ImGui::OpenPopup("AddComponentPopup");
			}

			if (ImGui::BeginPopup("AddComponentPopup"))
			{
				if (ImGui::MenuItem("Mesh Renderer")) {
					// TODO: Add mesh renderer component
				}
				if (ImGui::MenuItem("Light")) {
					// TODO: Add light component
				}
				if (ImGui::MenuItem("Camera")) {
					// TODO: Add camera component
				}
				if (ImGui::MenuItem("Rigidbody")) {
					// TODO: Add rigidbody component
				}
				ImGui::EndPopup();
			}
		}
		else
		{
			ImGui::Text("No object selected");
		}

		ImGui::End();
	}

	void EditorLayer::DrawProjectPanel()
	{
		ImGui::Begin("Project");
		
		// Project folder structure
		if (ImGui::TreeNode("Assets"))
		{
			if (ImGui::TreeNode("Scenes"))
			{
				if (ImGui::Selectable("Main.unity", m_SelectedAsset == "Main.unity"))
				{
					m_SelectedAsset = "Main.unity";
				}
				ImGui::TreePop();
			}
			
			if (ImGui::TreeNode("Scripts"))
			{
				if (ImGui::Selectable("Player.cs", m_SelectedAsset == "Player.cs"))
				{
					m_SelectedAsset = "Player.cs";
				}
				ImGui::TreePop();
			}
			
			if (ImGui::TreeNode("Materials"))
			{
				if (ImGui::Selectable("Default.mat", m_SelectedAsset == "Default.mat"))
				{
					m_SelectedAsset = "Default.mat";
				}
				ImGui::TreePop();
			}
			
			if (ImGui::TreeNode("Textures"))
			{
				if (ImGui::Selectable("Checkerboard.png", m_SelectedAsset == "Checkerboard.png"))
				{
					m_SelectedAsset = "Checkerboard.png";
				}
				if (ImGui::Selectable("ChernoLogo.png", m_SelectedAsset == "ChernoLogo.png"))
				{
					m_SelectedAsset = "ChernoLogo.png";
				}
				ImGui::TreePop();
			}
			
			if (ImGui::TreeNode("Shaders"))
			{
				if (ImGui::Selectable("FlatColor.glsl", m_SelectedAsset == "FlatColor.glsl"))
				{
					m_SelectedAsset = "FlatColor.glsl";
				}
				if (ImGui::Selectable("Texture.glsl", m_SelectedAsset == "Texture.glsl"))
				{
					m_SelectedAsset = "Texture.glsl";
				}
				ImGui::TreePop();
			}
			
			ImGui::TreePop();
		}
		
		ImGui::End();
	}

	void EditorLayer::DrawConsolePanel()
	{
		ImGui::Begin("Console");
		
		// Filter buttons
		ImGui::Checkbox("Errors", &m_ShowErrors);
		ImGui::SameLine();
		ImGui::Checkbox("Warnings", &m_ShowWarnings);
		ImGui::SameLine();
		ImGui::Checkbox("Info", &m_ShowInfo);
		ImGui::SameLine();
		if (ImGui::Button("Clear"))
		{
			m_ConsoleLogs.clear();
		}
		
		ImGui::Separator();
		
		// Console logs
		ImGui::BeginChild("ConsoleLogs");
		for (const auto& log : m_ConsoleLogs)
		{
			ImVec4 color = { 1.0f, 1.0f, 1.0f, 1.0f };
			if (log.find("[ERROR]") != std::string::npos)
				color = { 1.0f, 0.4f, 0.4f, 1.0f };
			else if (log.find("[WARNING]") != std::string::npos)
				color = { 1.0f, 0.8f, 0.4f, 1.0f };
			else if (log.find("[INFO]") != std::string::npos)
				color = { 0.4f, 0.8f, 1.0f, 1.0f };
			
			ImGui::TextColored(color, "%s", log.c_str());
		}
		ImGui::EndChild();
		
		ImGui::End();
	}

	void EditorLayer::DrawSceneSettingsPanel()
	{
		ImGui::Begin("Scene Settings");
		
		// Lighting settings
		if (ImGui::CollapsingHeader("Lighting", ImGuiTreeNodeFlags_DefaultOpen))
		{
			ImGui::DragFloat3("Light Direction", glm::value_ptr(m_SceneLightDirection), 0.01f);
			ImGui::DragFloat("Ambient Intensity", &m_SceneAmbientIntensity, 0.01f, 0.0f, 1.0f);
			ImGui::ColorEdit3("Background Color", glm::value_ptr(m_SceneBackgroundColor));
		}
		
		// Renderer settings
		if (ImGui::CollapsingHeader("Renderer"))
		{
		auto stats = TomCat::Renderer2D::GetStats();
		ImGui::Text("Draw Calls: %d", stats.DrawCalls);
		ImGui::Text("Quads: %d", stats.QuadCount);
		ImGui::Text("Vertices: %d", stats.GetTotalVertexCount());
		ImGui::Text("Indices: %d", stats.GetTotalIndexCount());
		}

		ImGui::End();
	}

	void EditorLayer::DrawViewportPanel()
	{
		ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
		ImGui::Begin("Scene");
		
		ImVec2 ViewportPanelSize = ImGui::GetContentRegionAvail();
		if (m_ViewportSize != *((glm::vec2*)&ViewportPanelSize))
		{
			m_Framebuffer->Resize((uint32_t)ViewportPanelSize.x, (uint32_t)ViewportPanelSize.y);
			m_ViewportSize = { ViewportPanelSize.x, ViewportPanelSize.y };
			m_CameraController.OnResize(ViewportPanelSize.x, ViewportPanelSize.y);
		}

		uint32_t textureID = m_Framebuffer->GetColorAttachmentRendererID();
		ImGui::Image((void*)textureID, ImVec2{ ViewportPanelSize.x, ViewportPanelSize.y }, ImVec2{ 0, 1 }, ImVec2{ 1, 0 });

		ImGui::End();
		ImGui::PopStyleVar();
	}

	void EditorLayer::DrawStatusBar()
	{
		ImGui::Begin("Status");

		// 计算总文本宽度
		float totalWidth = 0;
		totalWidth += ImGui::CalcTextSize("Ready").x;
		totalWidth += ImGui::CalcTextSize("FPS: 000.0").x; // 估算FPS文本宽度
		totalWidth += ImGui::CalcTextSize("Objects: 0000").x; // 估算Objects文本宽度
		totalWidth += ImGui::GetStyle().ItemSpacing.x * 4; // SameLine的间距
		totalWidth += ImGui::GetFrameHeight() * 2; // 两个分隔符的宽度

		// 计算起始位置使其居中
		float windowWidth = ImGui::GetWindowSize().x;
		float startPos = (windowWidth - totalWidth) * 0.5f;

		ImGui::SetCursorPosX(startPos);

		ImGui::Text("Ready");
		ImGui::SameLine();
		ImGui::Separator();
		ImGui::SameLine();
		ImGui::Text("FPS: %.1f", ImGui::GetIO().Framerate);
		ImGui::SameLine();
		ImGui::Separator();
		ImGui::SameLine();
		ImGui::Text("Objects: %d", (int)m_SceneObjects.size());

		ImGui::End();;
	}

	void EditorLayer::AddSceneObject(const std::string& name)
	{
		auto obj = std::make_shared<SceneObject>();
		obj->name = name;
		m_SceneObjects.push_back(obj);
	}

	void EditorLayer::DrawSceneObjectTree(std::shared_ptr<SceneObject> obj, int depth)
	{
		ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_SpanAvailWidth;
		if (obj->selected) flags |= ImGuiTreeNodeFlags_Selected;
		if (obj->children.empty()) flags |= ImGuiTreeNodeFlags_Leaf | ImGuiTreeNodeFlags_NoTreePushOnOpen;
		
		bool opened = ImGui::TreeNodeEx(obj->name.c_str(), flags);
		
		if (ImGui::IsItemClicked())
		{
			// Deselect all other objects
			for (auto& other : m_SceneObjects)
			{
				other->selected = false;
			}
			obj->selected = true;
			m_SelectedObject = obj;
		}
		
		if (opened && !obj->children.empty())
		{
			for (auto& child : obj->children)
			{
				DrawSceneObjectTree(child, depth + 1);
			}
			ImGui::TreePop();
		}
	}

}