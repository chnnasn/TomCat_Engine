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

	EditorLayer::EditorLayer()
		: Layer("EditorLayer"), m_CameraController(1280.0f / 720.0f), m_SquareColor({ 0.2f, 0.3f, 0.8f, 1.0f })
	{
		m_CurrentProject = ProjectManager::Get().GetActiveProject();
	}

	void EditorLayer::OnAttach()
	{
		TC_PROFILE_FUNCTION();

		FramebufferSpecification fbSpec;
		fbSpec.Attachments = { FramebufferTextureFormat::RGBA8, FramebufferTextureFormat::RED_INTEGER, FramebufferTextureFormat::Depth };
		fbSpec.Width = 1280;
		fbSpec.Height = 720;
		m_Framebuffer = Framebuffer::Create(fbSpec);

		m_ActiveScene = CreateRef<Scene>();

		if (m_CurrentProject)
		{
			auto startScenePath = m_CurrentProject->GetStartScenePath();
			if (!startScenePath.empty() && std::filesystem::exists(startScenePath))
			{
				SceneSerializer serializer(m_ActiveScene);
				if (serializer.Deserialize(startScenePath.string()))
				{
					m_CurrentScenePath = startScenePath;
				}
			}
			else
			{
				m_SceneDirty = true;
			}
		}

		m_EditorCamera = EditorCamera(30.0f, 1.778f, 0.1f, 1000.0f);

		m_SceneHierarchyPanel.SetContext(m_ActiveScene);
	}

	void EditorLayer::OnDetach()
	{
		TC_PROFILE_FUNCTION();
	}

	void EditorLayer::OnUpdate(Timestep ts)
	{
		TC_PROFILE_FUNCTION();

		// Resize
		if (FramebufferSpecification spec = m_Framebuffer->GetSpecification();
			m_ViewportSize.x > 0.0f && m_ViewportSize.y > 0.0f && // zero sized framebuffer is invalid
			(spec.Width != m_ViewportSize.x || spec.Height != m_ViewportSize.y))
		{
			m_Framebuffer->Resize((uint32_t)m_ViewportSize.x, (uint32_t)m_ViewportSize.y);
			m_CameraController.OnResize(m_ViewportSize.x, m_ViewportSize.y);

			m_EditorCamera.SetViewportSize(m_ViewportSize.x, m_ViewportSize.y);

			m_ActiveScene->OnViewportResize((uint32_t)m_ViewportSize.x, (uint32_t)m_ViewportSize.y);
		}

		// Update
		if (m_ViewportFocused)
			m_CameraController.OnUpdate(ts);

		m_EditorCamera.OnUpdate(ts);

		// Render
		Renderer2D::ResetStats();
		m_Framebuffer->Bind();
		RenderCommand::SetClearColor({ 0.1f, 0.1f, 0.1f, 1 });
		RenderCommand::Clear();


		// Clear our entity ID attachment to -1
		m_Framebuffer->ClearAttachment(1, -1);

		// Update scene
		m_ActiveScene->OnUpdateEditor(ts, m_EditorCamera);

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
					ImGui::Text("Project: %s", m_CurrentProject->GetName().c_str());
					ImGui::Text("Path: %s", m_CurrentProject->GetProjectPath().string().c_str());
					ImGui::Separator();
					ImGui::Text("Version: %s", m_CurrentProject->GetVersion().c_str());
					ImGui::Text("Author: %s", m_CurrentProject->GetConfig().Author.c_str());
				}
				else
				{
					ImGui::TextColored(ImVec4(0.6f, 0.6f, 0.6f, 1.0f), "No project loaded");
				}
				ImGui::EndMenu();
			}

			ImGui::EndMenuBar();
		}


		m_SceneHierarchyPanel.OnImGuiRender();

		m_ContentBrowserPanel.OnImGuiRender();

		ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2{ 0, 0 });

		static bool sceneWindowOpen = true;

		ImGui::Begin("Scene", &sceneWindowOpen, ImGuiWindowFlags_MenuBar);//菜单栏不算入内容

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

		// 然后渲染场景图像
		uint64_t textureID = m_Framebuffer->GetColorAttachmentRendererID();
		ImGui::Image(reinterpret_cast<void*>(textureID), ImVec2{ m_ViewportSize.x, m_ViewportSize.y },
			ImVec2{ 0, 1 }, ImVec2{ 1, 0 });


		if (ImGui::BeginDragDropTarget())
		{
			std::filesystem::path assetPath = m_CurrentProject ? m_CurrentProject->GetAssetPath() : g_AssetPath;
			
			if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("TOMCAT_SCENE"))
			{
				const wchar_t* path = (const wchar_t*)payload->Data;
				OpenScene(assetPath / path);
			}
			else if(const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("SPRITE")){
				const wchar_t* path = (const wchar_t*)payload->Data;
				std::filesystem::path texturePath = assetPath / path;
				std::string fileName = texturePath.stem().string();

				auto Square = m_ActiveScene->CreateEntity(fileName);
				auto& SpriteR = Square.AddComponent<SpriteRenderer>(glm::vec4{ 1.0f, 1.0f, 1.0f, 1.0f });

				SpriteR.Texture = Texture2D::Create(texturePath.string());
			}

			ImGui::EndDragDropTarget();
		}


		//Gizmos
		Entity selectedEntity = m_SceneHierarchyPanel.GetSelectedEntity();

		if (selectedEntity && m_GizmoType != -1)
		{
			ImGuizmo::SetOrthographic(false);
			ImGuizmo::SetDrawlist();

			ImGuizmo::SetRect(m_ViewportBounds[0].x, m_ViewportBounds[0].y, m_ViewportBounds[1].x - m_ViewportBounds[0].x,
				m_ViewportBounds[1].y - m_ViewportBounds[0].y);

			//Camera
			// Runtime camera from entity
			// auto cameraEntity = m_ActiveScene->GetPrimaryCameraEntity();
			// const auto& camera = cameraEntity.GetComponent<CameraComponent>().Camera;
			// const glm::mat4& cameraProjection = camera.GetProjection();
			// glm::mat4 cameraView = glm::inverse(cameraEntity.GetComponent<TransformComponent>().GetTransform());

			// Editor camera
			const glm::mat4& cameraProjection = m_EditorCamera.GetProjection();
			glm::mat4 cameraView = m_EditorCamera.GetViewMatrix();

			// Entity transform
			if (selectedEntity.HasComponent<Transform>())
			{
				auto& tc = selectedEntity.GetComponent<Transform>();
				glm::mat4 transform = tc.GetTransform();

				// Snapping
				bool snap = Input::IsKeyPressed(Key::LeftControl);
				float snapValue = 0.5f; // Snap to 0.5m for translation/scale
				// Snap to 45 degrees for rotation
				if (m_GizmoType == ImGuizmo::OPERATION::ROTATE)
					snapValue = 45.0f;

				float snapValues[3] = { snapValue, snapValue, snapValue };

				ImGuizmo::Manipulate(glm::value_ptr(cameraView), glm::value_ptr(cameraProjection),
					(ImGuizmo::OPERATION)m_GizmoType, ImGuizmo::LOCAL, glm::value_ptr(transform),
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

		ImGui::End();
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
			if (control && shift)
				SaveSceneAs();
			else if (control)
				SaveScene();

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
		m_ActiveScene = CreateRef<Scene>();
		m_ActiveScene->OnViewportResize((uint32_t)m_ViewportSize.x, (uint32_t)m_ViewportSize.y);
		m_SceneHierarchyPanel.SetContext(m_ActiveScene);
		m_SceneHierarchyPanel.SetSelectedEntity({});
		m_CurrentScenePath.clear();
		m_SceneDirty = true;
		m_ContentBrowserPanel.SetProject(m_CurrentProject);
	}

	void EditorLayer::OpenScene()
	{
		std::string defaultPath = GetProjectScenePath().string();
		if (defaultPath.empty())
		{
			defaultPath = m_CurrentProject ? m_CurrentProject->GetScenePath().string() : "";
		}

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
		m_ActiveScene = CreateRef<Scene>();
		m_ActiveScene->OnViewportResize((uint32_t)m_ViewportSize.x, (uint32_t)m_ViewportSize.y);
		m_SceneHierarchyPanel.SetContext(m_ActiveScene);
		m_SceneHierarchyPanel.SetSelectedEntity({});

		SceneSerializer serializer(m_ActiveScene);
		if (serializer.Deserialize(path.string()))
		{
			m_CurrentScenePath = path;
			m_SceneDirty = false;
		}
		m_ContentBrowserPanel.SetProject(m_CurrentProject);
	}

	void EditorLayer::SaveScene()
	{
		if (m_CurrentScenePath.empty())
		{
			SaveSceneAs();
		}
		else
		{
			SceneSerializer serializer(m_ActiveScene);
			serializer.Serialize(m_CurrentScenePath.string());
			m_SceneDirty = false;
		}
	}

	void EditorLayer::SaveSceneAs()
	{
		std::string defaultPath = GetProjectScenePath().string();
		if (defaultPath.empty() && m_CurrentProject)
		{
			defaultPath = m_CurrentProject->GetScenePath().string();
		}

		std::string filepath = FileDialogs::SaveFile("TomCat Scene (*.tomcat)\0*.tomcat\0");
		if (filepath.empty())
		{
			filepath = FileDialogs::SaveFile("TomCat Scene (*.tcproj)\0*.tcproj\0");
		}
		if (!filepath.empty())
		{
			SceneSerializer serializer(m_ActiveScene);
			serializer.Serialize(filepath);
			m_CurrentScenePath = filepath;
			m_SceneDirty = false;
		}
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
				NewScene();
				
				auto startScenePath = project->GetStartScenePath();
				if (!startScenePath.empty() && std::filesystem::exists(startScenePath))
				{
					OpenScene(startScenePath);
				}
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

	std::filesystem::path EditorLayer::GetProjectScenePath() const
	{
		if (!m_CurrentProject)
			return std::filesystem::path();
		
		if (!m_CurrentScenePath.empty())
		{
			if (m_CurrentScenePath.parent_path() == m_CurrentProject->GetScenePath())
				return m_CurrentScenePath;
		}
		
		return m_CurrentProject->GetScenePath();
	}
}