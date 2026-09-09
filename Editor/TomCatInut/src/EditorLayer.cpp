#include "EditorLayer.h"
#include <imgui/imgui.h>

#include <algorithm>
#include <fstream>
#include <iomanip>
#include <sstream>

#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>
#include "TomCat/Scene/SceneSerializer.h"
#include "TomCat/Utils/PlatformUtils.h"
#include "TomCat/Project/ProjectManager.h"

#include "ImGuizmo.h"


namespace TomCat {

	extern const std::filesystem::path g_AssetPath;

	EditorLayer::EditorLayer(bool is2DMode)
		: Layer("EditorLayer"), m_CameraController(1280.0f / 720.0f), m_SquareColor({ 0.2f, 0.3f, 0.8f, 1.0f }), m_Is2DMode(is2DMode)
	{
		m_CurrentProject = ProjectManager::Get().GetActiveProject();
	}

	void EditorLayer::LoadSceneToolbarLayout()
	{
		// Project layouts are loaded after the editor-level layout, so prefer the
		// project imgui.ini and fall back to the current working directory for
		// projects created before toolbar persistence was added.
		auto loadFrom = [&](const std::filesystem::path& iniPath) -> bool
		{
			std::ifstream fin(iniPath);
			if (!fin)
				return false;

			bool inSection = false;
			bool foundSection = false;
			bool hasModeDocked = false;
			bool hasTransformDocked = false;
			bool hasModeFirst = false;
			bool hasModeX = false, hasModeY = false;
			bool hasTransformX = false, hasTransformY = false;
			std::string line;
			while (std::getline(fin, line))
			{
				if (!line.empty() && line.back() == '\r')
					line.pop_back();
				if (line == "[SceneToolbars]")
				{
					inSection = true;
					foundSection = true;
					continue;
				}
				if (!inSection)
					continue;
				if (!line.empty() && line.front() == '[')
					break;
				const std::string::size_type equals = line.find('=');
				if (equals == std::string::npos)
					continue;
				const std::string key = line.substr(0, equals);
				const std::string value = line.substr(equals + 1);
				auto readBool = [&value](bool& destination, bool& present)
				{
					if (value == "1" || value == "true" || value == "True")
					{
						destination = true;
						present = true;
					}
					else if (value == "0" || value == "false" || value == "False")
					{
						destination = false;
						present = true;
					}
				};
				auto readFloat = [&value](float& destination, bool& present)
				{
					try
					{
						destination = std::stof(value);
						present = true;
					}
					catch (const std::exception&)
					{
						// Ignore malformed values and retain the in-code default.
					}
				};

				if (key == "ModeToolbarDocked")
					readBool(m_GizmoModeToolbarDocked, hasModeDocked);
				else if (key == "TransformToolbarDocked")
					readBool(m_GizmoTransformToolbarDocked, hasTransformDocked);
				else if (key == "ModeToolbarFirst")
					readBool(m_GizmoModeToolbarFirst, hasModeFirst);
				else if (key == "ModeToolbarOffsetX")
					readFloat(m_GizmoModeToolbarOffset.x, hasModeX);
				else if (key == "ModeToolbarOffsetY")
					readFloat(m_GizmoModeToolbarOffset.y, hasModeY);
				else if (key == "TransformToolbarOffsetX")
					readFloat(m_GizmoToolbarOffset.x, hasTransformX);
				else if (key == "TransformToolbarOffsetY")
					readFloat(m_GizmoToolbarOffset.y, hasTransformY);
			}
			return foundSection;
		};

		bool loaded = false;
		if (m_CurrentProject)
			loaded = loadFrom(m_CurrentProject->GetProjectPath().parent_path() / "imgui.ini");
		if (!loaded)
			loadFrom(std::filesystem::current_path() / "imgui.ini");
	}

	void EditorLayer::SaveSceneToolbarLayout()
	{
		const std::filesystem::path iniPath = m_CurrentProject
			? m_CurrentProject->GetProjectPath().parent_path() / "imgui.ini"
			: std::filesystem::current_path() / "imgui.ini";

		std::string ini;
		{
			std::ifstream fin(iniPath);
			if (fin)
			{
				std::stringstream contents;
				contents << fin.rdbuf();
				ini = contents.str();
			}
		}

		std::ostringstream section;
		section << "\n[SceneToolbars]\n"
			<< "ModeToolbarDocked=" << (m_GizmoModeToolbarDocked ? 1 : 0) << "\n"
			<< "TransformToolbarDocked=" << (m_GizmoTransformToolbarDocked ? 1 : 0) << "\n"
			<< "ModeToolbarFirst=" << (m_GizmoModeToolbarFirst ? 1 : 0) << "\n"
			<< std::fixed << std::setprecision(3)
			<< "ModeToolbarOffsetX=" << m_GizmoModeToolbarOffset.x << "\n"
			<< "ModeToolbarOffsetY=" << m_GizmoModeToolbarOffset.y << "\n"
			<< "TransformToolbarOffsetX=" << m_GizmoToolbarOffset.x << "\n"
			<< "TransformToolbarOffsetY=" << m_GizmoToolbarOffset.y << "\n";

		const std::string sectionName = "[SceneToolbars]";
		const std::string::size_type sectionPos = ini.find(sectionName);
		if (sectionPos != std::string::npos)
		{
			const std::string::size_type nextSection = ini.find("\n[", sectionPos + sectionName.size());
			ini.erase(sectionPos, nextSection == std::string::npos ? std::string::npos : nextSection - sectionPos);
		}
		if (!ini.empty() && ini.back() != '\n')
			ini.push_back('\n');
		ini += section.str();

		std::ofstream fout(iniPath, std::ios::trunc);
		if (fout)
			fout << ini;
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

		// Restore the custom Scene toolbar arrangement after all ImGui window
		// settings have been loaded, so the project layout wins over the fallback
		// editor-level layout.
		LoadSceneToolbarLayout();

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

		// Append the custom section after ImGui writes its own settings; otherwise
		// SaveIniSettingsToDisk would overwrite the toolbar section.
		SaveSceneToolbarLayout();
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

		// Render Scene View (Editor Camera).  Unity's Scene canvas is one step
		// lighter than the surrounding #383838 panels (#474747); the grid and
		// selection overlays then provide the additional contrast seen in the
		// reference.
		Renderer2D::ResetStats();
		m_Framebuffer->Bind();
		RenderCommand::SetClearColor({ 71.0f / 255.0f, 71.0f / 255.0f, 71.0f / 255.0f, 1 });
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
		// Unity keeps the global playbar one step darker than docked panels.
		ImGui::PushStyleColor(ImGuiCol_ChildBg, ImGui::GetStyle().Colors[ImGuiCol_TitleBg]);
		ImGui::BeginChild("ToolbarRegion", ImVec2(0, toolbarHeight), false,
			ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
		UI_Toolbar();
		ImGui::EndChild();
		ImGui::PopStyleColor();

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

		// Use the same native menu-bar slot as Hierarchy.  ImGui's dock tab and
		// menu-bar layout then share one geometry source, eliminating the hand-
		// positioned gap that appeared with the custom Scene strip.
		const bool sceneVisible = ImGui::Begin("Scene", &sceneWindowOpen, ImGuiWindowFlags_MenuBar);

		auto viewportMinRegion = ImGui::GetWindowContentRegionMin();
		auto viewportMaxRegion = ImGui::GetWindowContentRegionMax();
		auto viewportOffset = ImGui::GetWindowPos();
		m_ViewportBounds[0] = { viewportMinRegion.x + viewportOffset.x, viewportMinRegion.y + viewportOffset.y };
		m_ViewportBounds[1] = { viewportMaxRegion.x + viewportOffset.x, viewportMaxRegion.y + viewportOffset.y };
		// The content origin is the bottom edge of the native MenuBar.  Derive the
		// dock row from that edge so the custom Scene toolbar occupies the actual
		// menu-bar slot (and remains vertically centered at every font/DPI scale).
		m_GizmoModeDockHeight = ImGui::GetFrameHeight();
		m_GizmoModeDockY = m_ViewportBounds[0].y - m_GizmoModeDockHeight;

		m_ViewportFocused = ImGui::IsWindowFocused();
		m_ViewportHovered = ImGui::IsWindowHovered();
		Application::Get().GetImGuiLayer()->BlockEvents(!m_ViewportFocused && !m_ViewportHovered);

		ImVec2 viewportPanelSize = ImGui::GetContentRegionAvail();
		m_ViewportSize = { viewportPanelSize.x, viewportPanelSize.y };

		uint64_t sceneTextureID = m_Framebuffer->GetColorAttachmentRendererID();
		ImGui::Image(reinterpret_cast<void*>(sceneTextureID), ImVec2{ m_ViewportSize.x, m_ViewportSize.y },
			ImVec2{ 0, 1 }, ImVec2{ 1, 0 });

		// Draw the mode bar in the foreground layer for both docked and floating
		// states.  The docked position is still computed from the Scene row, while
		// the foreground draw list keeps it above that row during drag operations.
		// Foreground primitives are global.  Do not submit ordinary overlays while
		// the Scene tab is hidden; an active drag is the one exception below so the
		// toolbar remains visible while crossing the tab bar.
		// Keep rendering an active drag even when docking temporarily marks the
		// Scene tab hidden (for example while the cursor crosses the Scene/Game
		// tab bar).  Otherwise the drag state has no frame in which to paint and
		// the anchor toolbar appears to disappear.
		if (sceneVisible || m_GizmoModeToolbarDragging || m_GizmoTransformToolbarDragging)
		{
			// BeginMenuBar switches ImGui to the same full-width clip/layout region
			// used by Hierarchy.  Submit the custom controls after the framebuffer
			// image so their foreground draw order remains above the Scene content.
			if (sceneVisible && ImGui::BeginMenuBar())
			{
				// MenuBarBg is intentionally the darker foundation.  Scene's toolbar
				// row is an explicit lighter overlay, painted before the controls so
				// an empty dock still shows the complete top-bar surface.
				ImGui::GetWindowDrawList()->AddRectFilled(
					ImVec2(m_ViewportBounds[0].x, m_GizmoModeDockY),
					ImVec2(m_ViewportBounds[1].x, m_GizmoModeDockY + m_GizmoModeDockHeight),
					ImGui::GetColorU32(ImGuiCol_Tab));
				UI_SceneGizmoModeToolbarOverlay();
				UI_SceneGizmoToolbar();
				ImGui::EndMenuBar();
			}
			else
			{
				// Keep the overlay alive if the native menu-bar slot is temporarily
				// unavailable (for example while the tab is being hidden during a drag).
				UI_SceneGizmoModeToolbarOverlay();
				UI_SceneGizmoToolbar();
			}
		}

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
			ImGuizmo::AllowAxisFlip(false);
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
					(ImGuizmo::OPERATION)m_GizmoType,
					m_GizmoSpaceMode == GizmoSpaceMode::Local ? ImGuizmo::LOCAL : ImGuizmo::WORLD,
					glm::value_ptr(transform),
					nullptr, snap ? snapValues : nullptr);

				if (ImGuizmo::IsUsing())
				{
					m_ActiveScene->SetWorldTransform(selectedEntity, transform);
					m_SceneDirty = true;
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
			// Stats is a toolbar control in the Game view, not a text-only menu
			// entry.  Give it the same full frame height as the neighboring Unity
			// controls while keeping enough width for the current font/DPI scale.
			const ImGuiStyle& gameStyle = ImGui::GetStyle();
			const ImVec2 statsLabelSize = ImGui::CalcTextSize("Stats");
			const float statsButtonHeight = ImGui::GetFrameHeight();
			const float statsButtonWidth = std::max(69.0f,
				statsLabelSize.x + gameStyle.FramePadding.x * 2.0f);
			// MenuBarBg is the dark foundation now; toolbar controls keep the
			// lighter neutral surface used by the existing editor chrome.
			const ImVec4 statsSurface = gameStyle.Colors[ImGuiCol_Tab];
			const ImVec4 statsHovered = gameStyle.Colors[ImGuiCol_ButtonHovered];
			const ImVec4 statsActive = gameStyle.Colors[ImGuiCol_ButtonActive];
			ImGui::PushStyleColor(ImGuiCol_Button, statsSurface);
			ImGui::PushStyleColor(ImGuiCol_ButtonHovered, statsHovered);
			ImGui::PushStyleColor(ImGuiCol_ButtonActive, statsActive);
			const bool statsPressed = ImGui::Button("Stats##GameStatsButton",
				ImVec2(statsButtonWidth, statsButtonHeight));
			ImGui::PopStyleColor(3);
			const ImVec2 statsPopupPos(ImGui::GetItemRectMin().x,
				ImGui::GetItemRectMax().y);

			if (statsPressed)
				ImGui::OpenPopup("##GameStatsPopup");

			ImGui::SetNextWindowPos(statsPopupPos, ImGuiCond_Appearing);
			if (ImGui::BeginPopup("##GameStatsPopup"))
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

				ImGui::EndPopup();
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
		// Unity's empty Game view uses a soft rounded notification card; keep its
		// larger radius while moving the fill into the reference gray ramp.
		draw->AddRectFilled(panelMin, panelMax, IM_COL32(98, 98, 98, 235), 18.0f);
		draw->AddRect(panelMin, panelMax, IM_COL32(140, 140, 140, 255), 18.0f, 0, 1.0f);

		const char* messageText = "No cameras rendering";
		ImVec2 messageSize = ImGui::CalcTextSize(messageText);
		ImVec2 messagePos(imageCenter.x - messageSize.x * 0.5f, imageCenter.y - messageSize.y * 0.5f);
		draw->AddText(messagePos, IM_COL32(243, 243, 243, 255), messageText);
	}

	void EditorLayer::UI_SceneToolbarDragHandle(const char* id, glm::vec2& offset, bool& docked, bool& dragging,
		const ImVec2& handleMin, const ImVec2& handleMax, float tearX, bool canDock)
	{
		ImGui::PushID(id);
		ImGui::SetCursorScreenPos(handleMin);
		ImGui::InvisibleButton("##drag", ImVec2(handleMax.x - handleMin.x, handleMax.y - handleMin.y));

		const bool pressing = ImGui::IsMouseDown(ImGuiMouseButton_Left);
		if (!dragging && ImGui::IsItemActive() && ImGui::IsMouseDragging(ImGuiMouseButton_Left))
		{
			dragging = true;
			if (docked)
			{
				docked = false;
				const ImVec2 mouse = ImGui::GetMousePos();
				offset = { mouse.x - m_ViewportBounds[0].x - tearX,
					m_GizmoModeDockY - m_ViewportBounds[0].y };
			}
		}

		if (dragging && pressing)
		{
			const ImVec2 delta = ImGui::GetIO().MouseDelta;
			offset.x += delta.x;
			offset.y += delta.y;
		}
		else if (dragging)
		{
			const ImVec2 mouse = ImGui::GetMousePos();
			if (canDock &&
				mouse.y >= m_GizmoModeDockY - 6.0f &&
				mouse.y <= m_GizmoModeDockY + m_GizmoModeDockHeight + 6.0f)
			{
				docked = true;
				offset = { 16.0f, 10.0f };
			}
			dragging = false;
		}

		ImGui::PopID();
	}

	void EditorLayer::UI_SceneGizmoModeToolbarRow()
	{
		const ImGuiStyle& style = ImGui::GetStyle();
		const float buttonHeight = ImGui::GetFrameHeight();
		m_GizmoModeDockY = ImGui::GetCursorScreenPos().y;
		m_GizmoModeDockHeight = buttonHeight;

		auto DrawModeButton = [&](const char* label, ImVec4 selectedColor, bool selected, const char* popupId)
		{
			ImVec4 normalColor = style.Colors[ImGuiCol_Button];
			ImVec4 hoverColor = style.Colors[ImGuiCol_ButtonHovered];
			ImVec4 activeColor = style.Colors[ImGuiCol_ButtonActive];
			if (selected)
			{
				normalColor = selectedColor;
				hoverColor = ImVec4(
					std::min(1.0f, selectedColor.x + 0.08f),
					std::min(1.0f, selectedColor.y + 0.08f),
					std::min(1.0f, selectedColor.z + 0.08f),
					selectedColor.w);
				activeColor = ImVec4(
					std::max(0.0f, selectedColor.x - 0.08f),
					std::max(0.0f, selectedColor.y - 0.08f),
					std::max(0.0f, selectedColor.z - 0.08f),
					selectedColor.w);
			}

			ImGui::PushStyleColor(ImGuiCol_Button, normalColor);
			ImGui::PushStyleColor(ImGuiCol_ButtonHovered, hoverColor);
			ImGui::PushStyleColor(ImGuiCol_ButtonActive, activeColor);
			bool clicked = ImGui::Button(label, ImVec2(72.0f, buttonHeight));
			ImGui::PopStyleColor(3);
			if (clicked)
				ImGui::OpenPopup(popupId);
		};

		if (!m_GizmoModeToolbarDocked)
		{
			const float dockButtonWidth = ImGui::CalcTextSize("Dock").x + style.FramePadding.x * 2.0f;
			ImGui::SetCursorPosX(ImGui::GetCursorPosX() + ImGui::GetContentRegionAvail().x - dockButtonWidth);
			if (ImGui::SmallButton("Dock"))
				m_GizmoModeToolbarDocked = true;
			return;
		}

		// Small grip on the left of the docked strip. Drag it to tear the bar
		// off into the floating overlay; drag that overlay back here to dock.
		ImDrawList* rowDraw = ImGui::GetWindowDrawList();
		const ImVec2 gripMin = ImGui::GetCursorScreenPos();
		const ImVec2 gripMax(gripMin.x + 22.0f, gripMin.y + buttonHeight);
		const ImVec2 gripCenter((gripMin.x + gripMax.x) * 0.5f, (gripMin.y + gripMax.y) * 0.5f);
		rowDraw->AddLine(ImVec2(gripCenter.x - 5.0f, gripCenter.y - 4.0f),
			ImVec2(gripCenter.x + 5.0f, gripCenter.y - 4.0f), IM_COL32(137, 137, 137, 255), 1.5f);
		rowDraw->AddLine(ImVec2(gripCenter.x - 5.0f, gripCenter.y),
			ImVec2(gripCenter.x + 5.0f, gripCenter.y), IM_COL32(137, 137, 137, 255), 1.5f);
		rowDraw->AddLine(ImVec2(gripCenter.x - 5.0f, gripCenter.y + 4.0f),
			ImVec2(gripCenter.x + 5.0f, gripCenter.y + 4.0f), IM_COL32(137, 137, 137, 255), 1.5f);
		UI_SceneToolbarDragHandle("##scene_mode_row", m_GizmoModeToolbarOffset,
			m_GizmoModeToolbarDocked, m_GizmoModeToolbarDragging,
			gripMin, gripMax, 17.0f, true);
		ImGui::SameLine(0.0f, 4.0f);

		DrawModeButton(m_GizmoPivotMode == GizmoPivotMode::Pivot ? "Pivot" : "Center",
			ImVec4(44.0f / 255.0f, 93.0f / 255.0f, 135.0f / 255.0f, 1.0f),
			m_GizmoPivotMode == GizmoPivotMode::Pivot, "##scene_gizmo_pivot_popup");
		ImGui::SameLine(0.0f, style.ItemInnerSpacing.x);
		DrawModeButton(m_GizmoSpaceMode == GizmoSpaceMode::Local ? "Local" : "World",
			ImVec4(44.0f / 255.0f, 93.0f / 255.0f, 135.0f / 255.0f, 1.0f),
			m_GizmoSpaceMode == GizmoSpaceMode::Local, "##scene_gizmo_space_popup");

		ImGui::SameLine(0.0f, 10.0f);
		if (ImGui::SmallButton("Float"))
			m_GizmoModeToolbarDocked = false;

		if (ImGui::BeginPopup("##scene_gizmo_pivot_popup"))
		{
			if (ImGui::MenuItem("Pivot", nullptr, m_GizmoPivotMode == GizmoPivotMode::Pivot))
				m_GizmoPivotMode = GizmoPivotMode::Pivot;
			if (ImGui::MenuItem("Center", nullptr, m_GizmoPivotMode == GizmoPivotMode::Center))
				m_GizmoPivotMode = GizmoPivotMode::Center;
			ImGui::EndPopup();
		}

		if (ImGui::BeginPopup("##scene_gizmo_space_popup"))
		{
			if (ImGui::MenuItem("Local", nullptr, m_GizmoSpaceMode == GizmoSpaceMode::Local))
				m_GizmoSpaceMode = GizmoSpaceMode::Local;
			if (ImGui::MenuItem("World", nullptr, m_GizmoSpaceMode == GizmoSpaceMode::World))
				m_GizmoSpaceMode = GizmoSpaceMode::World;
			ImGui::EndPopup();
		}
	}

	void EditorLayer::UI_SceneGizmoToolbar()
	{
		// Draw directly over the Scene image. This keeps the palette clipped and
		// owned by the Scene view instead of creating another dockable ImGui window.
		// The native Scene menu bar clips to its own row while it is active; widen
		// this toolbar pass to the Scene bounds so a floating palette below the row
		// remains visible and interactive.
		const float toolbarClipTop = m_GizmoTransformToolbarDragging
			? ImGui::GetWindowPos().y : m_GizmoModeDockY;
		ImGui::PushClipRect(ImVec2(ImGui::GetWindowPos().x, toolbarClipTop),
			ImVec2(m_ViewportBounds[1].x, m_ViewportBounds[1].y), false);
		const float dockPadding = 5.0f;
		const float dockHandleWidth = 24.0f;
		const float dockButtonWidth = 34.0f;
		const float dockButtonHeight = 28.0f;
		const float dockGap = 4.0f;
		const float dockWidth = dockPadding * 2.0f + dockHandleWidth + dockGap +
			dockButtonWidth * 4.0f + dockGap * 3.0f;
		const float modeWidth = 5.0f * 2.0f + 24.0f + dockGap + 62.0f * 2.0f + dockGap;

		if (m_GizmoTransformToolbarDocked)
		{
			const float dockStartX = m_ViewportBounds[0].x + 8.0f;
			const float transformDockX = (m_GizmoModeToolbarDocked && m_GizmoModeToolbarFirst)
				? dockStartX + modeWidth + dockGap : dockStartX;
			const float transformHeight = dockButtonHeight + dockPadding * 2.0f;
			const float transformDockOffsetY = (m_GizmoModeDockHeight - transformHeight) * 0.5f;
			ImVec2 topLeft(transformDockX, m_GizmoModeDockY + transformDockOffsetY);
			ImVec2 bottomRight(topLeft.x + dockWidth, topLeft.y + dockButtonHeight + dockPadding * 2.0f);
			// A docked toolbar may be torn off while the cursor leaves the Scene
			// window.  Use the viewport foreground list during the drag so the
			// active bar is not clipped away by the Scene window bounds.
			ImDrawList* draw = m_GizmoTransformToolbarDragging
				? ImGui::GetForegroundDrawList() : ImGui::GetWindowDrawList();
			const ImU32 outer = IM_COL32(40, 40, 40, 245);
			const ImU32 normal = IM_COL32(71, 71, 71, 245);
			const ImU32 active = IM_COL32(44, 93, 135, 255);
			const ImU32 line = IM_COL32(196, 196, 196, 255);
			const ImU32 handleLine = IM_COL32(137, 137, 137, 255);
			draw->AddRectFilled(topLeft, bottomRight, outer, 2.0f);

			ImVec2 handleMin(topLeft.x + dockPadding, topLeft.y + dockPadding);
			ImVec2 handleMax(handleMin.x + dockHandleWidth, topLeft.y + dockButtonHeight + dockPadding);
			ImVec2 handleCenter((handleMin.x + handleMax.x) * 0.5f, (handleMin.y + handleMax.y) * 0.5f);
			for (int i = -1; i <= 1; ++i)
				draw->AddLine(ImVec2(handleCenter.x - 7.0f, handleCenter.y + i * 4.0f),
					ImVec2(handleCenter.x + 7.0f, handleCenter.y + i * 4.0f), handleLine, 2.0f);

			const bool transformWasDragging = m_GizmoTransformToolbarDragging;
			UI_SceneToolbarDragHandle("##scene_transform_toolbar_docked", m_GizmoToolbarOffset,
				m_GizmoTransformToolbarDocked, m_GizmoTransformToolbarDragging,
				handleMin, handleMax, 0.0f, true);
			if (transformWasDragging && !m_GizmoTransformToolbarDragging && m_GizmoTransformToolbarDocked)
			{
				const float x = ImGui::GetMousePos().x;
				m_GizmoModeToolbarFirst = !m_GizmoModeToolbarDocked ||
					x >= dockStartX + modeWidth * 0.5f;
				SaveSceneToolbarLayout();
			}

			const int tools[] = { -1, ImGuizmo::OPERATION::TRANSLATE,
				ImGuizmo::OPERATION::ROTATE, ImGuizmo::OPERATION::SCALE };
			for (int i = 0; i < 4; ++i)
			{
				ImVec2 min(topLeft.x + dockPadding + dockHandleWidth + dockGap + i * (dockButtonWidth + dockGap),
					topLeft.y + dockPadding);
				ImVec2 max(min.x + dockButtonWidth, min.y + dockButtonHeight);
				const bool selected = m_GizmoType == tools[i];
				draw->AddRectFilled(min, max, selected ? active : normal, 2.0f);
				draw->AddRect(min, max, selected ? active : IM_COL32(25, 25, 25, 255), 2.0f, 0, 1.0f);
				ImVec2 c((min.x + max.x) * 0.5f, (min.y + max.y) * 0.5f);
				if (i == 0)
				{
					draw->AddTriangleFilled(ImVec2(c.x - 6, c.y - 10), ImVec2(c.x + 7, c.y + 8),
						ImVec2(c.x, c.y + 6), line);
					draw->AddLine(ImVec2(c.x, c.y + 6), ImVec2(c.x - 4, c.y + 11), line, 2.0f);
				}
				else if (i == 1)
				{
					draw->AddLine(ImVec2(c.x - 9, c.y), ImVec2(c.x + 9, c.y), line, 2.0f);
					draw->AddLine(ImVec2(c.x, c.y - 9), ImVec2(c.x, c.y + 9), line, 2.0f);
				}
				else if (i == 2)
				{
					draw->AddCircle(c, 8.0f, line, 20, 2.0f);
					draw->AddTriangleFilled(ImVec2(c.x + 7, c.y - 8), ImVec2(c.x + 2, c.y - 9), ImVec2(c.x + 7, c.y - 3), line);
				}
				else
				{
					draw->AddLine(ImVec2(c.x - 8, c.y - 7), ImVec2(c.x - 2, c.y - 7), line, 2.0f);
					draw->AddLine(ImVec2(c.x - 8, c.y - 7), ImVec2(c.x - 8, c.y - 1), line, 2.0f);
					draw->AddLine(ImVec2(c.x + 8, c.y + 7), ImVec2(c.x + 2, c.y + 7), line, 2.0f);
					draw->AddLine(ImVec2(c.x + 8, c.y + 7), ImVec2(c.x + 8, c.y + 1), line, 2.0f);
				}
				ImGui::SetCursorScreenPos(min);
				ImGui::InvisibleButton((std::string("##scene_docked_tool_") + std::to_string(i)).c_str(),
					ImVec2(max.x - min.x, max.y - min.y));
				if (ImGui::IsItemClicked())
					m_GizmoType = tools[i];
			}
			ImGui::PopClipRect();
			return;
		}

		const float width = 52.0f;
		const float handleHeight = 28.0f;
		const float buttonHeight = 50.0f;
		const float gap = 2.0f;
		const float height = handleHeight + gap + buttonHeight * 4.0f + gap * 3.0f + 5.0f;

		// Never allow the palette to become stranded outside the Scene view.
		// While dragging, leave the offset free so the bar follows the mouse.
		if (!m_GizmoTransformToolbarDragging)
		{
			const float maxOffsetX = m_ViewportSize.x > width + 8.0f ? m_ViewportSize.x - width - 4.0f : 4.0f;
			const float maxOffsetY = m_ViewportSize.y > height + 8.0f ? m_ViewportSize.y - height - 4.0f : 4.0f;
			if (m_GizmoToolbarOffset.x < 4.0f) m_GizmoToolbarOffset.x = 4.0f;
			if (m_GizmoToolbarOffset.y < 4.0f) m_GizmoToolbarOffset.y = 4.0f;
			if (m_GizmoToolbarOffset.x > maxOffsetX) m_GizmoToolbarOffset.x = maxOffsetX;
			if (m_GizmoToolbarOffset.y > maxOffsetY) m_GizmoToolbarOffset.y = maxOffsetY;
		}

		ImVec2 topLeft(m_ViewportBounds[0].x + m_GizmoToolbarOffset.x,
			m_ViewportBounds[0].y + m_GizmoToolbarOffset.y);
		ImVec2 bottomRight(topLeft.x + width, topLeft.y + height);
		ImDrawList* draw = m_GizmoTransformToolbarDragging
			? ImGui::GetForegroundDrawList() : ImGui::GetWindowDrawList();
		const ImU32 outer = IM_COL32(40, 40, 40, 245);
		const ImU32 normal = IM_COL32(71, 71, 71, 245);
		const ImU32 active = IM_COL32(44, 93, 135, 255);
		const ImU32 line = IM_COL32(196, 196, 196, 255);

		draw->AddRectFilled(topLeft, bottomRight, outer, 2.0f);

		// The top handle is the only draggable area, matching the reference UI.
		ImVec2 handleMin(topLeft.x + 5.0f, topLeft.y + 4.0f);
		ImVec2 handleMax(topLeft.x + width - 5.0f, topLeft.y + handleHeight);
		ImVec2 handleCenter((handleMin.x + handleMax.x) * 0.5f, (handleMin.y + handleMax.y) * 0.5f);
		const ImU32 handleLine = IM_COL32(137, 137, 137, 255);
		for (int i = -1; i <= 1; ++i)
			draw->AddLine(ImVec2(handleCenter.x - 12.0f, handleCenter.y + i * 5.0f),
				ImVec2(handleCenter.x + 12.0f, handleCenter.y + i * 5.0f), handleLine, 2.0f);
		const bool transformWasDragging = m_GizmoTransformToolbarDragging;
		UI_SceneToolbarDragHandle("##scene_transform_toolbar", m_GizmoToolbarOffset,
			m_GizmoTransformToolbarDocked, m_GizmoTransformToolbarDragging,
			handleMin, handleMax, 0.0f, true);
		if (transformWasDragging && !m_GizmoTransformToolbarDragging && m_GizmoTransformToolbarDocked)
		{
			const float modeWidth = 5.0f * 2.0f + 24.0f + 4.0f + 62.0f * 2.0f + 4.0f;
			const float dockStartX = m_ViewportBounds[0].x + 8.0f;
			m_GizmoModeToolbarFirst = !m_GizmoModeToolbarDocked ||
				ImGui::GetMousePos().x >= dockStartX + modeWidth * 0.5f;
			SaveSceneToolbarLayout();
		}

		const int tools[] = { -1, ImGuizmo::OPERATION::TRANSLATE,
			ImGuizmo::OPERATION::ROTATE, ImGuizmo::OPERATION::SCALE };
		for (int i = 0; i < 4; ++i)
		{
			ImVec2 min(topLeft.x + 5.0f, topLeft.y + handleHeight + gap + i * (buttonHeight + gap));
			ImVec2 max(min.x + width - 10.0f, min.y + buttonHeight);
			bool selected = m_GizmoType == tools[i];
			draw->AddRectFilled(min, max, selected ? active : normal, 2.0f);

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
		ImGui::PopClipRect();
	}

	void EditorLayer::UI_SceneGizmoModeToolbarOverlay()
	{
		const float padding = 5.0f;
		const float handleWidth = 24.0f;
		const float buttonWidth = 62.0f;
		const float buttonHeight = 28.0f;
		const float gap = 4.0f;
		const float height = buttonHeight + padding * 2.0f;
		const float width = padding * 2.0f + handleWidth + gap + buttonWidth * 2.0f + gap;
		// The Q/W/E/R toolbar uses the same strip when docked.  Keep these
		// dimensions here (and in its renderer below) so insertion previews and
		// the two bars always agree about their occupied widths.
		const float transformHandleWidth = 24.0f;
		const float transformButtonWidth = 34.0f;
		const float transformWidth = 5.0f * 2.0f + transformHandleWidth + gap +
			transformButtonWidth * 4.0f + gap * 3.0f;
		const float dockGap = 4.0f;

		// While dragging, let the bar follow the mouse freely so it can cover the
		// Scene top strip.  When idle, keep it inside the viewport while still
		// allowing it to sit over that strip if the user parked it there.
		if (!m_GizmoModeToolbarDragging)
		{
			const float maxOffsetX = m_ViewportSize.x > width + 8.0f ? m_ViewportSize.x - width - 4.0f : 4.0f;
			const float minOffsetY = m_GizmoModeDockY - m_ViewportBounds[0].y + 1.0f;
			const float maxOffsetY = m_ViewportSize.y > height + 8.0f ? m_ViewportSize.y - height - 4.0f : 4.0f;
			if (m_GizmoModeToolbarOffset.x < 4.0f) m_GizmoModeToolbarOffset.x = 4.0f;
			if (m_GizmoModeToolbarOffset.y < minOffsetY) m_GizmoModeToolbarOffset.y = minOffsetY;
			if (m_GizmoModeToolbarOffset.x > maxOffsetX) m_GizmoModeToolbarOffset.x = maxOffsetX;
			if (m_GizmoModeToolbarOffset.y > maxOffsetY) m_GizmoModeToolbarOffset.y = maxOffsetY;
		}

		// Dock to the same strip used by the drop preview. Extend the ImGui item
		// clip rectangle as well as the drawing clip so the handle stays interactive.
		const float dockStartX = m_ViewportBounds[0].x + 8.0f;
		const float modeDockX = (m_GizmoModeToolbarDocked && m_GizmoTransformToolbarDocked && !m_GizmoModeToolbarFirst)
			? dockStartX + transformWidth + dockGap : dockStartX;
		const float modeDockOffsetY = (m_GizmoModeDockHeight - height) * 0.5f;
		ImVec2 topLeft = m_GizmoModeToolbarDocked
			? ImVec2(modeDockX, m_GizmoModeDockY + modeDockOffsetY)
			: ImVec2(m_ViewportBounds[0].x + m_GizmoModeToolbarOffset.x,
				m_ViewportBounds[0].y + m_GizmoModeToolbarOffset.y);
		ImVec2 bottomRight(topLeft.x + width, topLeft.y + height);

		const ImVec2 savedCursor = ImGui::GetCursorScreenPos();
		const float toolbarClipTop = (m_GizmoModeToolbarDragging || m_GizmoTransformToolbarDragging)
			? ImGui::GetWindowPos().y : m_GizmoModeDockY;
		ImGui::PushClipRect(ImVec2(ImGui::GetWindowPos().x, toolbarClipTop),
			ImVec2(m_ViewportBounds[1].x, m_ViewportBounds[1].y), false);
		// Keep the shared strip and insertion preview in the Scene window layer;
		// otherwise their full-width background would cover the Q/W/E/R bar when
		// the anchor/mode bar is the one being dragged.  Only the active toolbar
		// body is promoted to the viewport foreground.
		ImDrawList* dockDraw = ImGui::GetWindowDrawList();
		ImDrawList* draw = m_GizmoModeToolbarDragging
			? ImGui::GetForegroundDrawList() : dockDraw;
		const ImU32 outer = IM_COL32(40, 40, 40, 245);
		const ImU32 normal = IM_COL32(71, 71, 71, 245);
		const ImU32 hover = IM_COL32(98, 98, 98, 245);
		const ImU32 line = IM_COL32(196, 196, 196, 255);
		const ImU32 arrow = IM_COL32(137, 137, 137, 255);
		const ImU32 accent = IM_COL32(212, 127, 42, 255);
		const bool anyToolbarDragging = m_GizmoModeToolbarDragging || m_GizmoTransformToolbarDragging;

		// The caller paints the Scene menu-bar overlay before entering this helper.
		// Keep this pass focused on the toolbar body and insertion preview so the
		// overlay remains visible in the empty-dock state as well.

		draw->AddRectFilled(topLeft, bottomRight, outer, 2.0f);

		// Blue drop preview while dragging near the docked strip.  The preview is
		// an insertion slot, not a full-width highlight: with one toolbar already
		// docked it appears immediately before or after that toolbar, matching the
		// small left-side preview in the reference UI.
		const ImVec2 mouse = ImGui::GetMousePos();
		const bool dockHover = anyToolbarDragging &&
			mouse.y >= m_GizmoModeDockY - 5.0f &&
			mouse.y <= m_GizmoModeDockY + m_GizmoModeDockHeight + 5.0f;
		if (dockHover)
		{
			const bool draggingMode = m_GizmoModeToolbarDragging;
			const float previewWidth = draggingMode ? width : transformWidth;
			const bool otherDocked = draggingMode ? m_GizmoTransformToolbarDocked : m_GizmoModeToolbarDocked;
			const float otherWidth = draggingMode ? transformWidth : width;
			float previewX = dockStartX;
			if (otherDocked)
			{
				const float otherX = dockStartX;
				const bool insertBefore = mouse.x < otherX + otherWidth * 0.5f;
				previewX = insertBefore ? dockStartX : otherX + otherWidth + dockGap;
			}
			previewX = std::max(dockStartX, std::min(previewX,
				m_ViewportBounds[1].x - previewWidth - 4.0f));
			dockDraw->AddRectFilled(ImVec2(previewX, m_GizmoModeDockY),
				ImVec2(previewX + previewWidth, m_GizmoModeDockY + m_GizmoModeDockHeight),
				IM_COL32(44, 93, 135, 85), 1.0f);
			dockDraw->AddRect(ImVec2(previewX + 1.0f, m_GizmoModeDockY + 1.0f),
				ImVec2(previewX + previewWidth - 1.0f, m_GizmoModeDockY + m_GizmoModeDockHeight - 1.0f),
				IM_COL32(80, 165, 235, 230), 1.0f, 0, 1.0f);
		}

		ImVec2 handleMin(topLeft.x + padding, topLeft.y + padding);
		ImVec2 handleMax(handleMin.x + handleWidth, topLeft.y + height - padding);
		ImVec2 handleCenter((handleMin.x + handleMax.x) * 0.5f, (handleMin.y + handleMax.y) * 0.5f);
		const ImU32 handleLine = IM_COL32(137, 137, 137, 255);
		for (int i = -1; i <= 1; ++i)
			draw->AddLine(ImVec2(handleCenter.x - 7.0f, handleCenter.y + i * 4.0f),
				ImVec2(handleCenter.x + 7.0f, handleCenter.y + i * 4.0f), handleLine, 2.0f);

		const bool modeWasDragging = m_GizmoModeToolbarDragging;
		UI_SceneToolbarDragHandle("##scene_gizmo_mode", m_GizmoModeToolbarOffset,
			m_GizmoModeToolbarDocked, m_GizmoModeToolbarDragging,
			handleMin, handleMax, 17.0f, true);
		if (modeWasDragging && !m_GizmoModeToolbarDragging && m_GizmoModeToolbarDocked)
		{
			m_GizmoModeToolbarFirst = !m_GizmoTransformToolbarDocked ||
				ImGui::GetMousePos().x < dockStartX + transformWidth * 0.5f;
			SaveSceneToolbarLayout();
		}

		auto DrawFrame = [&](const ImVec2& min, const ImVec2& max, bool hovered)
		{
			// Pivot/space choices are represented by their icon.  Keep the button
			// surface neutral after a click; only pointer hover may tint it.
			draw->AddRectFilled(min, max, hovered ? hover : normal, 2.0f);
			draw->AddRect(min, max, IM_COL32(25, 25, 25, 255), 2.0f, 0, 1.0f);
		};

		auto DrawDropArrow = [&](const ImVec2& min, const ImVec2& max)
		{
			const ImVec2 c(max.x - 8.0f, (min.y + max.y) * 0.5f + 1.0f);
			draw->AddTriangleFilled(ImVec2(c.x - 3.0f, c.y - 2.0f), ImVec2(c.x + 3.0f, c.y - 2.0f), ImVec2(c.x, c.y + 2.5f), arrow);
		};

		auto DrawPivotIcon = [&](const ImVec2& min, const ImVec2& max)
		{
			const ImVec2 c((min.x + max.x) * 0.5f - 3.5f, (min.y + max.y) * 0.5f);
			draw->AddRect(ImVec2(c.x - 9.0f, c.y - 9.0f), ImVec2(c.x + 9.0f, c.y + 9.0f), line, 0.0f, 0, 1.8f);
			draw->AddCircleFilled(ImVec2(c.x - 5.0f, c.y + 5.0f), 3.0f, accent);
		};

		auto DrawCenterIcon = [&](const ImVec2& min, const ImVec2& max)
		{
			const ImVec2 c((min.x + max.x) * 0.5f - 3.5f, (min.y + max.y) * 0.5f);
			draw->AddRect(ImVec2(c.x - 9.0f, c.y - 9.0f), ImVec2(c.x + 9.0f, c.y + 9.0f), line, 0.0f, 0, 1.8f);
			draw->AddCircleFilled(c, 3.0f, accent);
		};

		auto DrawLocalIcon = [&](const ImVec2& min, const ImVec2& max)
		{
			const ImVec2 c((min.x + max.x) * 0.5f - 3.5f, (min.y + max.y) * 0.5f);
			draw->AddRect(ImVec2(c.x - 9.0f, c.y - 8.0f), ImVec2(c.x + 9.0f, c.y + 9.0f), line, 0.0f, 0, 1.8f);
			draw->AddLine(ImVec2(c.x - 8.0f, c.y - 3.0f), ImVec2(c.x + 1.5f, c.y - 9.0f), line, 2.0f);
			draw->AddLine(ImVec2(c.x + 1.5f, c.y - 9.0f), ImVec2(c.x + 8.0f, c.y - 1.5f), line, 2.0f);
			draw->AddLine(ImVec2(c.x + 8.0f, c.y - 1.5f), ImVec2(c.x + 8.0f, c.y + 8.0f), line, 2.0f);
			draw->AddCircleFilled(ImVec2(c.x - 3.5f, c.y + 4.0f), 2.8f, accent);
		};

		auto DrawWorldIcon = [&](const ImVec2& min, const ImVec2& max)
		{
			const ImVec2 c((min.x + max.x) * 0.5f - 3.5f, (min.y + max.y) * 0.5f);
			draw->AddCircle(c, 9.0f, line, 24, 1.8f);
			draw->AddLine(ImVec2(c.x - 9.0f, c.y), ImVec2(c.x + 9.0f, c.y), line, 1.8f);
			draw->AddLine(ImVec2(c.x, c.y - 9.0f), ImVec2(c.x, c.y + 9.0f), line, 1.8f);
			draw->AddCircleFilled(ImVec2(c.x + 3.0f, c.y - 3.0f), 2.8f, accent);
		};

		const float buttonMinY = topLeft.y + padding;
		const ImVec2 pivotMin(topLeft.x + padding + handleWidth + gap, buttonMinY);
		const ImVec2 pivotMax(pivotMin.x + buttonWidth, pivotMin.y + buttonHeight);
		const ImVec2 spaceMin(pivotMax.x + gap, buttonMinY);
		const ImVec2 spaceMax(spaceMin.x + buttonWidth, spaceMin.y + buttonHeight);

		ImGui::SetCursorScreenPos(pivotMin);
		ImGui::InvisibleButton("##scene_gizmo_pivot_mode", ImVec2(pivotMax.x - pivotMin.x, pivotMax.y - pivotMin.y));
		const bool pivotHovered = ImGui::IsItemHovered();
		if (ImGui::IsItemClicked())
			ImGui::OpenPopup("##scene_gizmo_pivot_popup");
		DrawFrame(pivotMin, pivotMax, pivotHovered);
		if (m_GizmoPivotMode == GizmoPivotMode::Pivot)
			DrawPivotIcon(pivotMin, pivotMax);
		else
			DrawCenterIcon(pivotMin, pivotMax);
		DrawDropArrow(pivotMin, pivotMax);
		if (ImGui::BeginPopup("##scene_gizmo_pivot_popup"))
		{
			if (ImGui::MenuItem("Pivot", nullptr, m_GizmoPivotMode == GizmoPivotMode::Pivot))
				m_GizmoPivotMode = GizmoPivotMode::Pivot;
			if (ImGui::MenuItem("Center", nullptr, m_GizmoPivotMode == GizmoPivotMode::Center))
				m_GizmoPivotMode = GizmoPivotMode::Center;
			ImGui::EndPopup();
		}

		ImGui::SetCursorScreenPos(spaceMin);
		ImGui::InvisibleButton("##scene_gizmo_space_mode", ImVec2(spaceMax.x - spaceMin.x, spaceMax.y - spaceMin.y));
		const bool spaceHovered = ImGui::IsItemHovered();
		if (ImGui::IsItemClicked())
			ImGui::OpenPopup("##scene_gizmo_space_popup");
		DrawFrame(spaceMin, spaceMax, spaceHovered);
		if (m_GizmoSpaceMode == GizmoSpaceMode::Local)
			DrawLocalIcon(spaceMin, spaceMax);
		else
			DrawWorldIcon(spaceMin, spaceMax);
		DrawDropArrow(spaceMin, spaceMax);
		if (ImGui::BeginPopup("##scene_gizmo_space_popup"))
		{
			if (ImGui::MenuItem("Local", nullptr, m_GizmoSpaceMode == GizmoSpaceMode::Local))
				m_GizmoSpaceMode = GizmoSpaceMode::Local;
			if (ImGui::MenuItem("World", nullptr, m_GizmoSpaceMode == GizmoSpaceMode::World))
				m_GizmoSpaceMode = GizmoSpaceMode::World;
			ImGui::EndPopup();
		}
		ImGui::PopClipRect();
		ImGui::SetCursorScreenPos(savedCursor);
	}

	void EditorLayer::UI_Toolbar()
	{
		float size = 40;
		float padding = 4;

		ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(0, padding));
		auto& colors = ImGui::GetStyle().Colors;
		// Use a framed Unity-style play control instead of a bare floating icon.
		// When running, the stop control inherits the same blue selected state as
		// the hierarchy and Scene toolbars.
		const ImVec4 buttonColor = m_SceneState == SceneState::Play
			? colors[ImGuiCol_HeaderActive] : colors[ImGuiCol_Button];
		ImGui::PushStyleColor(ImGuiCol_Button, buttonColor);
		const auto& buttonHovered = colors[ImGuiCol_ButtonHovered];
		ImGui::PushStyleColor(ImGuiCol_ButtonHovered, buttonHovered);
		const auto& buttonActive = colors[ImGuiCol_ButtonActive];
		ImGui::PushStyleColor(ImGuiCol_ButtonActive, buttonActive);

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
			// Preserve the current project's toolbar arrangement before switching
			// the active project and loading its independent imgui.ini.
			SaveSceneToolbarLayout();
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
				LoadSceneToolbarLayout();
				
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
			const std::filesystem::path imguiIniPath = m_CurrentProject->GetProjectPath().parent_path() / "imgui.ini";
			ImGui::SaveIniSettingsToDisk(imguiIniPath.string().c_str());
		}
		SaveSceneToolbarLayout();
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
