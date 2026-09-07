#include "EditorLayer.h"
#include <imgui/imgui.h>

#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>
#include "TomCat/Scene/SceneSerializer.h"
#include "TomCat/Utils/PlatformUtils.h"
#include "TomCat/Project/ProjectManager.h"

#include "ImGuizmo.h"

#include "TomCat/Math/Math.h"


namespace TomCat {

	extern const std::filesystem::path g_AssetPath;

	EditorLayer::EditorLayer(bool is2DMode)
		: Layer("EditorLayer"), m_CameraController(1280.0f / 720.0f), m_SquareColor({ 0.2f, 0.3f, 0.8f, 1.0f }), m_Is2DMode(is2DMode)
	{
		m_CurrentProject = ProjectManager::Get().GetActiveProject();
	}

	void EditorLayer::OnAttach()
	{
		TC_PROFILE_FUNCTION();


		m_IconPlay = Texture2D::Create("Packages/Resources/Icons/PlayButton.png");
		m_IconStop = Texture2D::Create("Packages/Resources/Icons/StopButton.png");

		FramebufferSpecification fbSpec;
		fbSpec.Attachments = { FramebufferTextureFormat::RGBA8, FramebufferTextureFormat::RED_INTEGER, FramebufferTextureFormat::Depth };
		fbSpec.Width = 1280;
		fbSpec.Height = 720;
		m_Framebuffer = Framebuffer::Create(fbSpec);
		m_GameFramebuffer = Framebuffer::Create(fbSpec);

		m_ActiveScene = CreateRef<Scene>();

		// Restore editor window layout from <cwd>/imgui.ini (independent of any project)
		ImGui::LoadIniSettingsFromDisk((std::filesystem::current_path() / "imgui.ini").string().c_str());

		if (m_CurrentProject)
		{
			m_SceneDirty = true;
			
			// 读取Project.tcproj目录的imgui.ini文件
			std::filesystem::path projectDir = m_CurrentProject->GetProjectPath().parent_path();
			std::filesystem::path imguiIniPath = projectDir / "imgui.ini";
			if (std::filesystem::exists(imguiIniPath))
			{
				// 加载ImGui配置
				ImGui::LoadIniSettingsFromDisk(imguiIniPath.string().c_str());
			}
		}

		m_EditorCamera = EditorCamera(30.0f, 1.778f, 0.1f, 1000.0f);
		m_EditorCamera.Set2DMode(m_Is2DMode);

		m_SceneHierarchyPanel.SetSceneLoadCallback([this](const std::filesystem::path& path) {
			OpenScene(path);
		});

		m_SceneHierarchyPanel.SetSpriteCreateCallback([this](const std::filesystem::path& path) {
			std::string fileName = path.stem().string();
			auto Square = m_ActiveScene->CreateEntity(fileName);
			auto& SpriteR = Square.AddComponent<SpriteRenderer>(glm::vec4{ 1.0f, 1.0f, 1.0f, 1.0f });
			SpriteR.Texture = Texture2D::Create(path.string());
		});

		// Every project starts in a usable sample scene. Existing projects keep
		// their sample scene and simply reopen it on the next editor launch.
		if (m_CurrentProject)
			OpenOrCreateSampleScene();
	}

	void EditorLayer::OnDetach()
	{
		TC_PROFILE_FUNCTION();

		m_ContentBrowserPanel.Serialize();

		// Save window layout + [ContentBrowser] layout into the editor-level imgui.ini
		m_ContentBrowserPanel.SaveLayoutSetting();

		if (m_CurrentProject)
		{
			std::filesystem::path projectDir = m_CurrentProject->GetProjectPath().parent_path();
			std::filesystem::path imguiIniPath = projectDir / "imgui.ini";
			
			ImGui::SaveIniSettingsToDisk(imguiIniPath.string().c_str());
			
			m_CurrentProject->Save();
		}
	}

	void EditorLayer::OnUpdate(Timestep ts)
	{
		TC_PROFILE_FUNCTION();

		// Resize Scene Framebuffer
		if (FramebufferSpecification spec = m_Framebuffer->GetSpecification();
			m_ViewportSize.x > 0.0f && m_ViewportSize.y > 0.0f &&
			(spec.Width != m_ViewportSize.x || spec.Height != m_ViewportSize.y))
		{
			m_Framebuffer->Resize((uint32_t)m_ViewportSize.x, (uint32_t)m_ViewportSize.y);
			m_CameraController.OnResize(m_ViewportSize.x, m_ViewportSize.y);
			m_EditorCamera.SetViewportSize(m_ViewportSize.x, m_ViewportSize.y);
			m_ActiveScene->OnViewportResize((uint32_t)m_ViewportSize.x, (uint32_t)m_ViewportSize.y);
		}

		// Resize Game Framebuffer
		if (FramebufferSpecification gameSpec = m_GameFramebuffer->GetSpecification();
			m_GameViewportSize.x > 0.0f && m_GameViewportSize.y > 0.0f &&
			(gameSpec.Width != m_GameViewportSize.x || gameSpec.Height != m_GameViewportSize.y))
		{
			m_GameFramebuffer->Resize((uint32_t)m_GameViewportSize.x, (uint32_t)m_GameViewportSize.y);
			m_ActiveScene->OnViewportResize((uint32_t)m_GameViewportSize.x, (uint32_t)m_GameViewportSize.y);
		}

		// Render Scene View (Editor Camera) - Always use EditorCamera with dark gray background
		Renderer2D::ResetStats();
		m_Framebuffer->Bind();
		RenderCommand::SetClearColor({ 0.1f, 0.1f, 0.1f, 1 });
		RenderCommand::Clear();
		m_Framebuffer->ClearAttachment(1, -1);

		// Update
		if (m_ViewportFocused)
			m_CameraController.OnUpdate(ts);
		m_EditorCamera.OnUpdate(ts);

		// Scene窗口始终使用EditorCamera渲染
		m_ActiveScene->OnUpdateEditor(ts, m_EditorCamera);

		// Mouse picking for Scene viewport
		auto [mx, my] = ImGui::GetMousePos();
		mx -= m_ViewportBounds[0].x;
		my -= m_ViewportBounds[0].y;
		glm::vec2 viewportSize = m_ViewportBounds[1] - m_ViewportBounds[0];
		my = viewportSize.y - my;
		int mouseX = (int)mx;
		int mouseY = (int)my;

		if (mouseX >= 0 && mouseY >= 0 && mouseX < (int)viewportSize.x && mouseY < (int)viewportSize.y)
		{
			int pixelData = m_Framebuffer->ReadPixel(1, mouseX, mouseY);
			m_HoveredEntity = pixelData == -1 ? Entity() : Entity((entt::entity)pixelData, m_ActiveScene.get());
		}

		m_Framebuffer->Unbind();

		// Render Game View (Runtime Camera) - Always render runtime camera
		m_GameFramebuffer->Bind();

		RenderCommand::SetClearColor({ 0.1f, 0.1f, 0.1f, 1 });
		RenderCommand::Clear();
		m_GameFramebuffer->ClearAttachment(1, -1);

		// Game窗口使用Runtime渲染，背景色由摄像机的BackgroundColor设置
		if (m_SceneState == SceneState::Play)
			m_ActiveScene->OnUpdateRuntime(ts);
		else
			m_ActiveScene->OnRenderRuntime();
		m_GameFramebuffer->Unbind();
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
		ImGui::Begin("DockSpace Demo", &dockspaceOpen, window_flags);
		ImGui::PopStyleVar();

		if (opt_fullscreen)
			ImGui::PopStyleVar(2);

		// 菜单栏
		if (ImGui::BeginMenuBar())
		{
			if (ImGui::BeginMenu("File"))
			{
				if (ImGui::MenuItem("Open Project"))
				{
					OpenProject();
				}

				if (ImGui::MenuItem("Save Project"))
				{
					SaveProject();
				}

				ImGui::Separator();

				if (ImGui::MenuItem("New Scene", "Ctrl + N"))
				{
					NewScene();
				}

				if (ImGui::MenuItem("Open Scene...", "Ctrl + O"))
				{
					OpenScene();
				}

				if (ImGui::MenuItem("Save Scene", "Ctrl + S"))
				{
					SaveScene();
				}

				if (ImGui::MenuItem("Save Scene As...", "Ctrl + Shift + S"))
				{
					SaveSceneAs();
				}

				ImGui::Separator();

				if (ImGui::MenuItem("Exit")) Application::Get().Close();
				ImGui::EndMenu();
			}

			if (ImGui::BeginMenu("Project"))
			{
				if (m_CurrentProject)
				{
					ImGui::Text("ProjectName: %s", m_CurrentProject->GetName().c_str());
					ImGui::Text("Path: %s", m_CurrentProject->GetProjectPath().string().c_str());
					ImGui::Separator();
					ImGui::Text("EditorVersion: %s", m_CurrentProject->GetEditorVersion().c_str());
				}
				else
				{
					ImGui::TextColored(ImVec4(0.6f, 0.6f, 0.6f, 1.0f), "No project loaded");
				}
				ImGui::EndMenu();
			}

			ImGui::EndMenuBar();
		}

		float toolbarHeight = 48.0f;
		ImGui::BeginChild("ToolbarRegion", ImVec2(0, toolbarHeight), false,
			ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
		UI_Toolbar();
		ImGui::EndChild();

		ImGui::Separator();

		ImGui::BeginChild("DockSpaceRegion", ImVec2(0, 0), false,
			ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);

		ImGuiIO& io = ImGui::GetIO();
		ImGuiStyle& style = ImGui::GetStyle();
		float minWinSizeX = style.WindowMinSize.x;
		style.WindowMinSize.x = 370.0f;

		if (io.ConfigFlags & ImGuiConfigFlags_DockingEnable)
		{
			ImGuiID dockspace_id = ImGui::GetID("MyDockSpace");
			ImGui::DockSpace(dockspace_id, ImVec2(0.0f, 0.0f), dockspace_flags);
		}

		style.WindowMinSize.x = minWinSizeX;

		ImGui::EndChild(); 

		m_SceneHierarchyPanel.OnImGuiRender();
		m_ContentBrowserPanel.OnImGuiRender();

		ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2{ 0, 0 });
	static bool sceneWindowOpen = true;

	ImGui::Begin("Scene", &sceneWindowOpen);

		auto viewportMinRegion = ImGui::GetWindowContentRegionMin();
		auto viewportMaxRegion = ImGui::GetWindowContentRegionMax();
		auto viewportOffset = ImGui::GetWindowPos();
		m_ViewportBounds[0] = { viewportMinRegion.x + viewportOffset.x, viewportMinRegion.y + viewportOffset.y };
		m_ViewportBounds[1] = { viewportMaxRegion.x + viewportOffset.x, viewportMaxRegion.y + viewportOffset.y };

		m_ViewportFocused = ImGui::IsWindowFocused();
		m_ViewportHovered = ImGui::IsWindowHovered();
		Application::Get().GetImGuiLayer()->BlockEvents(!m_ViewportFocused && !m_ViewportHovered);

		ImVec2 viewportPanelSize = ImGui::GetContentRegionAvail();
		m_ViewportSize = { viewportPanelSize.x, viewportPanelSize.y };

		uint64_t sceneTextureID = m_Framebuffer->GetColorAttachmentRendererID();
		ImGui::Image(reinterpret_cast<void*>(sceneTextureID), ImVec2{ m_ViewportSize.x, m_ViewportSize.y },
			ImVec2{ 0, 1 }, ImVec2{ 1, 0 });

		UI_SceneGizmoToolbar();

		if (ImGui::BeginDragDropTarget())
		{
			std::filesystem::path assetPath = m_CurrentProject ? m_CurrentProject->GetAssetPath() : g_AssetPath;

			ImGuiDragDropFlags flags = ImGuiDragDropFlags_AcceptNoDrawDefaultRect;
			if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("SPRITE", flags)) {
				const wchar_t* path = (const wchar_t*)payload->Data;
				std::filesystem::path texturePath = assetPath / path;
				std::string fileName = texturePath.stem().string();

				auto Square = m_ActiveScene->CreateEntity(fileName);
				auto& SpriteR = Square.AddComponent<SpriteRenderer>(glm::vec4{ 1.0f, 1.0f, 1.0f, 1.0f });
				SpriteR.Texture = Texture2D::Create(texturePath.string());
			}
			ImGui::EndDragDropTarget();
		}

		// Gizmos
		Entity selectedEntity = m_SceneHierarchyPanel.GetSelectedEntity();

		if (selectedEntity && m_GizmoType != -1)
		{
			ImGuizmo::SetOrthographic(false);
			ImGuizmo::SetDrawlist();

			ImGuizmo::SetRect(m_ViewportBounds[0].x, m_ViewportBounds[0].y,
				m_ViewportBounds[1].x - m_ViewportBounds[0].x,
				m_ViewportBounds[1].y - m_ViewportBounds[0].y);

			const glm::mat4& cameraProjection = m_EditorCamera.GetProjection();
			glm::mat4 cameraView = m_EditorCamera.GetViewMatrix();

			if (selectedEntity.HasComponent<Transform>())
			{
				auto& tc = selectedEntity.GetComponent<Transform>();
				glm::mat4 transform = tc.GetTransform();

				bool snap = Input::IsKeyPressed(Key::LeftControl);
				float snapValue = 0.5f;
				if (m_GizmoType == ImGuizmo::OPERATION::ROTATE)
					snapValue = 45.0f;

				float snapValues[3] = { snapValue, snapValue, snapValue };

				ImGuizmo::Manipulate(glm::value_ptr(cameraView), glm::value_ptr(cameraProjection),
					(ImGuizmo::OPERATION)m_GizmoType, ImGuizmo::LOCAL,
					glm::value_ptr(transform),
					nullptr, snap ? snapValues : nullptr);

				if (ImGuizmo::IsUsing())
				{
					glm::vec3 translation, rotation, scale;
					Math::DecomposeTransform(transform, translation, rotation, scale);

					glm::vec3 deltaRotation = rotation - tc._Rotation;
					tc._Translation = translation;
					tc._Rotation += deltaRotation;
					tc._Scale = scale;
				}
			}
		}

		ImGui::End();
		ImGui::PopStyleVar();

		// Game Window - Always visible
		ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2{ 0, 0 });
		static bool gameWindowOpen = true;

		ImGui::Begin("Game", &gameWindowOpen, ImGuiWindowFlags_MenuBar);

		if (ImGui::BeginMenuBar())
		{
			if (ImGui::BeginMenu("Stats"))
			{
				std::string name = "None";
				if (m_HoveredEntity)
					name = m_HoveredEntity.GetComponent<Tag>()._Tag;
				ImGui::Text("Hovered Entity : %s", name.c_str());

				auto stats = Renderer2D::GetStats();
				ImGui::Text("Stats:");
				ImGui::Text("Draw Calls: %d", stats.DrawCalls);
				ImGui::Text("Quads: %d", stats.QuadCount);
				ImGui::Text("Vertices: %d", stats.GetTotalVertexCount());
				ImGui::Text("Indices: %d", stats.GetTotalIndexCount());

				ImGui::EndMenu();
			}
			ImGui::EndMenuBar();
		}

		ImVec2 gameViewportPanelSize = ImGui::GetContentRegionAvail();
		m_GameViewportSize = { gameViewportPanelSize.x, gameViewportPanelSize.y };

		// 始终显示GameFramebuffer（Runtime摄像机渲染内容）
		uint64_t gameTextureID = m_GameFramebuffer->GetColorAttachmentRendererID();
		ImGui::Image(reinterpret_cast<void*>(gameTextureID), ImVec2{ m_GameViewportSize.x, m_GameViewportSize.y },
			ImVec2{ 0, 1 }, ImVec2{ 1, 0 });
		UI_GameNoCameraOverlay();

		ImGui::End();
		ImGui::PopStyleVar();

		ImGui::End();
	}

	void EditorLayer::UI_GameNoCameraOverlay()
	{
		if (!m_ActiveScene || m_ActiveScene->GetPrimaryCameraEntity())
			return;

		ImVec2 imageMin = ImGui::GetItemRectMin();
		ImVec2 imageMax = ImGui::GetItemRectMax();
		ImVec2 imageCenter((imageMin.x + imageMax.x) * 0.5f, (imageMin.y + imageMax.y) * 0.5f);
		const float panelWidth = std::min(520.0f, std::max(300.0f, imageMax.x - imageMin.x - 40.0f));
		const float panelHeight = 112.0f;
		ImVec2 panelMin(imageCenter.x - panelWidth * 0.5f, imageCenter.y - panelHeight * 0.5f);
		ImVec2 panelMax(imageCenter.x + panelWidth * 0.5f, imageCenter.y + panelHeight * 0.5f);

		ImDrawList* draw = ImGui::GetWindowDrawList();
		draw->AddRectFilled(panelMin, panelMax, IM_COL32(82, 82, 82, 235), 18.0f);
		draw->AddRect(panelMin, panelMax, IM_COL32(112, 112, 112, 255), 18.0f, 0, 1.0f);

		const char* messageText = "No cameras rendering";
		ImVec2 messageSize = ImGui::CalcTextSize(messageText);
		ImVec2 messagePos(imageCenter.x - messageSize.x * 0.5f, imageCenter.y - messageSize.y * 0.5f);
		draw->AddText(messagePos, IM_COL32(245, 245, 245, 255), messageText);
	}

	void EditorLayer::UI_SceneGizmoToolbar()
	{
		// Draw directly over the Scene image. This keeps the palette clipped and
		// owned by the Scene view instead of creating another dockable ImGui window.
		const float width = 52.0f;
		const float handleHeight = 28.0f;
		const float buttonHeight = 50.0f;
		const float gap = 2.0f;
		const float height = handleHeight + gap + buttonHeight * 4.0f + gap * 3.0f + 5.0f;

		// Never allow the palette to become stranded outside the Scene view.
		const float maxOffsetX = m_ViewportSize.x > width + 8.0f ? m_ViewportSize.x - width - 4.0f : 4.0f;
		const float maxOffsetY = m_ViewportSize.y > height + 8.0f ? m_ViewportSize.y - height - 4.0f : 4.0f;
		if (m_GizmoToolbarOffset.x < 4.0f) m_GizmoToolbarOffset.x = 4.0f;
		if (m_GizmoToolbarOffset.y < 4.0f) m_GizmoToolbarOffset.y = 4.0f;
		if (m_GizmoToolbarOffset.x > maxOffsetX) m_GizmoToolbarOffset.x = maxOffsetX;
		if (m_GizmoToolbarOffset.y > maxOffsetY) m_GizmoToolbarOffset.y = maxOffsetY;

		ImVec2 topLeft(m_ViewportBounds[0].x + m_GizmoToolbarOffset.x,
			m_ViewportBounds[0].y + m_GizmoToolbarOffset.y);
		ImVec2 bottomRight(topLeft.x + width, topLeft.y + height);
		ImDrawList* draw = ImGui::GetWindowDrawList();
		const ImU32 outer = IM_COL32(38, 38, 40, 245);
		const ImU32 normal = IM_COL32(82, 82, 84, 245);
		const ImU32 active = IM_COL32(54, 103, 151, 255);
		const ImU32 line = IM_COL32(225, 225, 225, 255);

		draw->AddRectFilled(topLeft, bottomRight, outer, 7.0f);

		// The top handle is the only draggable area, matching the reference UI.
		ImVec2 handleMin(topLeft.x + 5.0f, topLeft.y + 4.0f);
		ImVec2 handleMax(topLeft.x + width - 5.0f, topLeft.y + handleHeight);
		ImVec2 handleCenter((handleMin.x + handleMax.x) * 0.5f, (handleMin.y + handleMax.y) * 0.5f);
		const ImU32 handleLine = IM_COL32(105, 105, 108, 255);
		for (int i = -1; i <= 1; ++i)
			draw->AddLine(ImVec2(handleCenter.x - 12.0f, handleCenter.y + i * 5.0f),
				ImVec2(handleCenter.x + 12.0f, handleCenter.y + i * 5.0f), handleLine, 2.0f);
		ImGui::SetCursorScreenPos(handleMin);
		ImGui::InvisibleButton("##scene_tool_drag_handle",
			ImVec2(handleMax.x - handleMin.x, handleMax.y - handleMin.y));
		if (ImGui::IsItemActive() && ImGui::IsMouseDragging(ImGuiMouseButton_Left))
		{
			ImVec2 delta = ImGui::GetMouseDragDelta(ImGuiMouseButton_Left);
			m_GizmoToolbarOffset.x += delta.x;
			m_GizmoToolbarOffset.y += delta.y;
			ImGui::ResetMouseDragDelta(ImGuiMouseButton_Left);
		}

		const int tools[] = { -1, ImGuizmo::OPERATION::TRANSLATE,
			ImGuizmo::OPERATION::ROTATE, ImGuizmo::OPERATION::SCALE };
		for (int i = 0; i < 4; ++i)
		{
			ImVec2 min(topLeft.x + 5.0f, topLeft.y + handleHeight + gap + i * (buttonHeight + gap));
			ImVec2 max(min.x + width - 10.0f, min.y + buttonHeight);
			bool selected = m_GizmoType == tools[i];
			draw->AddRectFilled(min, max, selected ? active : normal, 5.0f);

			// Four compact symbols: cursor, move, rotate and scale.
			ImVec2 center((min.x + max.x) * 0.5f, (min.y + max.y) * 0.5f);
			if (i == 0)
			{
				draw->AddTriangleFilled(ImVec2(center.x - 8, center.y - 15),
					ImVec2(center.x + 9, center.y + 11), ImVec2(center.x - 1, center.y + 8), line);
				draw->AddLine(ImVec2(center.x - 1, center.y + 8), ImVec2(center.x - 6, center.y + 15), line, 3.0f);
			}
			else if (i == 1)
			{
				draw->AddLine(ImVec2(center.x - 15, center.y), ImVec2(center.x + 15, center.y), line, 3.0f);
				draw->AddLine(ImVec2(center.x, center.y - 15), ImVec2(center.x, center.y + 15), line, 3.0f);
				draw->AddTriangleFilled(ImVec2(center.x - 15, center.y), ImVec2(center.x - 7, center.y - 5), ImVec2(center.x - 7, center.y + 5), line);
				draw->AddTriangleFilled(ImVec2(center.x + 15, center.y), ImVec2(center.x + 7, center.y - 5), ImVec2(center.x + 7, center.y + 5), line);
				draw->AddTriangleFilled(ImVec2(center.x, center.y - 15), ImVec2(center.x - 5, center.y - 7), ImVec2(center.x + 5, center.y - 7), line);
				draw->AddTriangleFilled(ImVec2(center.x, center.y + 15), ImVec2(center.x - 5, center.y + 7), ImVec2(center.x + 5, center.y + 7), line);
			}
			else if (i == 2)
			{
				draw->AddCircle(center, 14.0f, line, 24, 3.0f);
				draw->AddTriangleFilled(ImVec2(center.x + 13, center.y - 14), ImVec2(center.x + 4, center.y - 15), ImVec2(center.x + 12, center.y - 5), line);
			}
			else
			{
				draw->AddLine(ImVec2(min.x + 12, min.y + 16), ImVec2(min.x + 22, min.y + 16), line, 3.0f);
				draw->AddLine(ImVec2(min.x + 12, min.y + 16), ImVec2(min.x + 12, min.y + 26), line, 3.0f);
				draw->AddLine(ImVec2(max.x - 12, max.y - 16), ImVec2(max.x - 22, max.y - 16), line, 3.0f);
				draw->AddLine(ImVec2(max.x - 12, max.y - 16), ImVec2(max.x - 12, max.y - 26), line, 3.0f);
			}

			ImGui::SetCursorScreenPos(min);
			ImGui::InvisibleButton((std::string("##scene_tool_") + std::to_string(i)).c_str(),
				ImVec2(max.x - min.x, max.y - min.y));
			if (ImGui::IsItemClicked())
				m_GizmoType = tools[i];
		}
	}

	void EditorLayer::UI_Toolbar()
	{
		float size = 40;
		float padding = 4;

		ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(0, padding));
		ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0, 0, 0, 0));
		auto& colors = ImGui::GetStyle().Colors;
		const auto& buttonHovered = colors[ImGuiCol_ButtonHovered];
		ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(buttonHovered.x, buttonHovered.y, buttonHovered.z, 0.5f));
		const auto& buttonActive = colors[ImGuiCol_ButtonActive];
		ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(buttonActive.x, buttonActive.y, buttonActive.z, 0.5f));

		float windowWidth = ImGui::GetWindowWidth();
		ImGui::SetCursorPosX((windowWidth - size) * 0.5f);

		Ref<Texture2D> icon = m_SceneState == SceneState::Edit ? m_IconPlay : m_IconStop;

		// 只有当当前有场景时，才允许按下播放按钮
		if (m_SceneState == SceneState::Edit && !m_EditorScene)
		{
			// 如果当前没有场景，禁用播放按钮
			ImGui::BeginDisabled();
		}

		if (ImGui::ImageButton((ImTextureID)icon->GetRendererID(), ImVec2(size, size),
			ImVec2(0, 0), ImVec2(1, 1), 0))
		{
			if (m_SceneState == SceneState::Edit && m_EditorScene)
			{
				OnScenePlay();
				if (m_ActiveScene)
					m_ActiveScene->OnRuntimeStart();
			}
			else if (m_SceneState == SceneState::Play)
			{
				OnSceneStop();
				if (m_ActiveScene)
					m_ActiveScene->OnRuntimeStop();
			}
		}

		if (m_SceneState == SceneState::Edit && !m_EditorScene)
		{
			ImGui::EndDisabled();
		}

		ImGui::PopStyleVar();
		ImGui::PopStyleColor(3);
	}


	void EditorLayer::OnScenePlay()
	{

		m_SceneState = SceneState::Play;

		m_ActiveScene = Scene::Copy(m_EditorScene);

		m_ActiveScene->OnRuntimeStart();
		m_SceneHierarchyPanel.SetContext(m_ActiveScene, false, true);

		// 切换到Game窗口焦点
		ImGui::SetWindowFocus("Game");

	}

	void EditorLayer::OnSceneStop()
	{
		m_SceneState = SceneState::Edit;

		if (m_ActiveScene)
			m_ActiveScene->OnRuntimeStop();
		m_ActiveScene = m_EditorScene;

		m_SceneHierarchyPanel.SetContext(m_ActiveScene);

		// 切换到Scene窗口焦点
		ImGui::SetWindowFocus("Scene");
	}

	void EditorLayer::OnEvent(Event& e)
	{
		m_CameraController.OnEvent(e);

		m_EditorCamera.OnEvent(e);

		EventDispatcher dispatcher(e);
		dispatcher.Dispatch<KeyPressedEvent>(TC_Bind_Event_Fn(EditorLayer::OnKeyPressed));
		dispatcher.Dispatch<MouseButtonPressedEvent>(TC_Bind_Event_Fn(EditorLayer::OnMouseButtonPressed));
	}

	bool EditorLayer::OnKeyPressed(KeyPressedEvent& e)
	{
		// Shortcuts
		if (e.GetRepeatCount() > 0)
			return false;

		bool control = Input::IsKeyPressed(Key::LeftControl) || Input::IsKeyPressed(Key::RightControl);
		bool shift = Input::IsKeyPressed(Key::LeftShift) || Input::IsKeyPressed(Key::RightShift);
		switch (e.GetKeyCode())
		{
		case Key::N:
		{
			if (control)
				NewScene();

			break;
		}
		case Key::O:
		{
			if (control)
				OpenScene();

			break;
		}
		case Key::S:
		{
			SaveSceneAs();
			if (control)
			{
				if (shift)
					SaveSceneAs();
				else
					SaveScene();
			}

			break;
		}

		// Scene Commands
		case Key::D:
		{
			if (control)
				m_SceneHierarchyPanel.HandleShortcut(e.GetKeyCode(), control);

			break;
		}
		case Key::X:
		case Key::C:
		case Key::V:
		case Key::F2:
		case Key::Delete:
		{
			m_SceneHierarchyPanel.HandleShortcut(e.GetKeyCode(), control);
			break;
		}

		// Gizmos
		case Key::Q:
		{
			if (!ImGuizmo::IsUsing())
				m_GizmoType = -1;
			break;
		}
		case Key::W:
		{
			if (!ImGuizmo::IsUsing())
				m_GizmoType = ImGuizmo::OPERATION::TRANSLATE;
			break;
		}
		case Key::E:
		{
			if (!ImGuizmo::IsUsing())
				m_GizmoType = ImGuizmo::OPERATION::ROTATE;
			break;
		}
		case Key::R:
		{
			if (!ImGuizmo::IsUsing())
				m_GizmoType = ImGuizmo::OPERATION::SCALE;
			break;
		}
		}
	}

	bool EditorLayer::OnMouseButtonPressed(MouseButtonPressedEvent& e)
	{
		if (e.GetMouseButton() == Mouse::ButtonLeft)
		{
			if (m_ViewportHovered && !ImGuizmo::IsOver() && !Input::IsKeyPressed(Key::LeftAlt) && m_HoveredEntity)
		{
			// Only select the entity if it's valid
			m_SceneHierarchyPanel.SetSelectedEntity(m_HoveredEntity);
		}
		}
		return false;
	}


	void EditorLayer::NewScene()
	{
		m_EditorScene = CreateRef<Scene>();
		m_EditorScene->SetSceneName("Untitled");
		m_ActiveScene = m_EditorScene;
		AddDefaultMainCamera();
		m_ActiveScene->OnViewportResize((uint32_t)m_ViewportSize.x, (uint32_t)m_ViewportSize.y);
		m_SceneHierarchyPanel.SetContext(m_ActiveScene);
		m_SceneHierarchyPanel.SetSelectedEntity({});
		m_CurrentScenePath.clear();
		m_SceneDirty = true;
		m_ContentBrowserPanel.SetProject(m_CurrentProject);

		m_EditorScenePath = std::filesystem::path();
	}

	void EditorLayer::AddDefaultMainCamera()
	{
		if (!m_ActiveScene)
			return;

		Entity mainCamera = m_ActiveScene->CreateEntity("MainCamera");
		auto& camera = mainCamera.AddComponent<C_Camera>();
		if (m_Is2DMode)
			camera._Camera.SetOrthographic(10.0f, -1.0f, 1.0f);
		else
			camera._Camera.SetPerspective(glm::radians(45.0f), 0.01f, 1000.0f);
	}

	void EditorLayer::OpenOrCreateSampleScene()
	{
		std::filesystem::path samplePath = m_CurrentProject->GetAssetPath() / "sample.tomcat";
		std::error_code error;
		std::filesystem::create_directories(samplePath.parent_path(), error);

		if (std::filesystem::exists(samplePath))
		{
			OpenScene(samplePath);
			return;
		}

		NewScene();
		m_EditorScene->SetSceneName("sample");
		SerializeScene(m_ActiveScene, samplePath);
		m_EditorScenePath = samplePath;
		m_CurrentScenePath = samplePath;
		m_SceneDirty = false;
	}

	void EditorLayer::OpenScene()
	{
		std::string filepath = FileDialogs::OpenFile("TomCat Scene (*.tomcat)\0*.tomcat\0");
		if (filepath.empty())
		{
			filepath = FileDialogs::OpenFile("TomCat Scene (*.tcproj)\0*.tcproj\0");
		}
		if (!filepath.empty())
		{
			OpenScene(filepath);
		}
	}

	void EditorLayer::OpenScene(const std::filesystem::path& path)
	{

		if (m_SceneState != SceneState::Edit)
			OnSceneStop();


		if (path.extension().string() != ".tomcat")
		{
			TC_Warn("Could not load {0} - not a scene file", path.filename().string());
			return;
		}

		Ref<Scene> newScene = CreateRef<Scene>();
		SceneSerializer serializer(newScene);
		if (serializer.Deserialize(path.string()))
		{
			newScene->SetSceneName(path.stem().string());
			m_EditorScene = newScene;
			m_EditorScene->OnViewportResize((uint32_t)m_ViewportSize.x, (uint32_t)m_ViewportSize.y);
			m_SceneHierarchyPanel.SetContext(m_EditorScene);

			m_ActiveScene = m_EditorScene;
			m_EditorScenePath = path;
		}
		m_ContentBrowserPanel.SetProject(m_CurrentProject);
	}

	void EditorLayer::SaveScene()
	{
		if (!m_EditorScenePath.empty())
			SerializeScene(m_ActiveScene, m_EditorScenePath);
		else
			SaveSceneAs();
	}	

	void EditorLayer::SaveSceneAs()
	{
		std::string filepath = FileDialogs::SaveFile("TomCat Scene (*.tomcat)\0*.tomcat\0");
		if (filepath.empty())
		{
			filepath = FileDialogs::SaveFile("TomCat Scene (*.tcproj)\0*.tcproj\0");
		}
		if (!filepath.empty())
		{
			SerializeScene(m_ActiveScene, filepath);
			m_EditorScenePath = filepath;
			m_CurrentScenePath = filepath;
			m_SceneDirty = false;
		}
	}

	void EditorLayer::SerializeScene(Ref<Scene> scene, const std::filesystem::path& path)
	{
		if (scene)
			scene->SetSceneName(path.stem().string());
		SceneSerializer serializer(scene);
		serializer.Serialize(path.string());
	}

	void EditorLayer::OpenProject()
	{
		std::string filepath = FileDialogs::OpenFile("TomCat Project (*.tcproj)\0*.tcproj\0");
		if (!filepath.empty())
		{
			auto project = ProjectManager::Get().LoadProject(filepath);
			if (project)
			{
				m_CurrentProject = project;
				
				// 读取Project.tcproj目录的imgui.ini文件
				std::filesystem::path projectDir = project->GetProjectPath().parent_path();
				std::filesystem::path imguiIniPath = projectDir / "imgui.ini";
				if (std::filesystem::exists(imguiIniPath))
				{
					// 加载ImGui配置
					ImGui::LoadIniSettingsFromDisk(imguiIniPath.string().c_str());
				}
				
				NewScene();
				m_ContentBrowserPanel.SetProject(m_CurrentProject);
			}
		}
	}

	void EditorLayer::SaveProject()
	{
		if (m_CurrentProject)
		{
			m_CurrentProject->Save();
		}
	}

	void EditorLayer::OnDuplicateEntity()
	{
		if (m_SceneState != SceneState::Edit)
			return;

		Entity selectedEntity = m_SceneHierarchyPanel.GetSelectedEntity();
		if (selectedEntity)
			m_EditorScene->DuplicateEntity(selectedEntity);
	}
}
