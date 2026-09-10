#include "EditorLayer.h"
#include <imgui/imgui.h>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <string_view>
#include <vector>

#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>
#include "TomCat/Scene/SceneSerializer.h"
#include "TomCat/Utils/FileSystemUtils.h"
#include "TomCat/Utils/PlatformUtils.h"
#include "TomCat/Utils/PathUtils.h"
#include "TomCat/Project/ProjectManager.h"

#include "ImGuizmo.h"


namespace TomCat {

	extern const std::filesystem::path g_AssetPath;

	namespace {

		uint32_t ToFramebufferExtent(float value)
		{
			if (!std::isfinite(value) || value <= 0.0f)
				return 0;
			return static_cast<uint32_t>(std::round(std::clamp(value, 1.0f,
				static_cast<float>(Framebuffer::MaxFramebufferSize))));
		}

		std::filesystem::path AbsoluteLexicalPath(const std::filesystem::path& path)
		{
			if (path.empty())
				return {};
			std::error_code error;
			const std::filesystem::path absolute = std::filesystem::absolute(path, error);
			return (error ? path : absolute).lexically_normal();
		}

		bool TryGetRelativeWithin(const std::filesystem::path& root,
			const std::filesystem::path& candidate, std::filesystem::path& relative)
		{
			const std::filesystem::path normalizedRoot = AbsoluteLexicalPath(root);
			const std::filesystem::path normalizedCandidate = AbsoluteLexicalPath(candidate);
			if (normalizedRoot.empty() || normalizedCandidate.empty())
				return false;
			relative = normalizedCandidate.lexically_relative(normalizedRoot);
			if (relative.empty() || relative.is_absolute())
				return false;
			for (const auto& part : relative)
			{
				if (part == "..")
					return false;
			}
			if (relative == ".")
				relative.clear();
			return true;
		}

		bool IsImGuiManagedIniSection(const std::string& header)
		{
			return header.rfind("[Window][", 0) == 0 ||
				header.rfind("[Table][", 0) == 0 ||
				header.rfind("[Docking][", 0) == 0;
		}

		std::string::size_type FindIniSectionHeader(const std::string& ini,
			std::string_view sectionName)
		{
			std::string::size_type position = 0;
			while ((position = ini.find(sectionName, position)) != std::string::npos)
			{
				const bool lineStart = position == 0 || ini[position - 1] == '\n';
				const size_t end = position + sectionName.size();
				const bool lineEnd = end == ini.size() || ini[end] == '\n' || ini[end] == '\r';
				if (lineStart && lineEnd)
					return position;
				position = end;
			}
			return std::string::npos;
		}

		std::filesystem::path GetDefaultEditorLayoutPath()
		{
			std::error_code error;
			const std::filesystem::path currentDirectory = std::filesystem::current_path(error);
			return error ? std::filesystem::path("imgui.ini") : currentDirectory / "imgui.ini";
		}

		std::filesystem::path GetEditorLayoutPath(const Ref<Project>& project)
		{
			if (project)
			{
				if (project->GetProjectPath().empty())
				{
					TC_Core_Error("Cannot resolve an Editor layout for a project with an empty path");
					return {};
				}
				return project->GetProjectPath().parent_path() / "UserSettings" / "imgui.ini";
			}

			const std::optional<std::filesystem::path> settingsRoot = GetTomCatSettingsRoot();
			return settingsRoot ? *settingsRoot / "editor-layout.ini" : std::filesystem::path{};
		}

		bool EnsureSettingsDirectory(const std::filesystem::path& settingsPath)
		{
			if (settingsPath.empty())
				return false;
			const std::filesystem::path directory = settingsPath.parent_path();
			if (directory.empty())
				return true;

			std::error_code error;
			std::filesystem::create_directories(directory, error);
			if (!error)
				return true;

			TC_Core_Error("Failed to create editor settings directory '{0}': {1}",
				PathToUTF8(directory), error.message());
			return false;
		}

		bool LoadImGuiSettings(const std::filesystem::path& path)
		{
			std::ifstream input(path, std::ios::binary);
			if (!input)
				return false;
			std::ostringstream contents;
			contents << input.rdbuf();
			if (input.bad())
				return false;
			const std::string settings = contents.str();
			std::istringstream lines(settings);
			std::string line;
			bool hasManagedSection = false;
			while (std::getline(lines, line))
			{
				if (!line.empty() && line.back() == '\r')
					line.pop_back();
				if (IsImGuiManagedIniSection(line))
				{
					hasManagedSection = true;
					break;
				}
			}
			// A user file may initially contain only custom toolbar/browser sections.
			// Loading that through ImGui would clear the default Docking settings.
			if (!hasManagedSection)
				return false;
			ImGui::LoadIniSettingsFromMemory(settings.data(), settings.size());
			return true;
		}

		bool ReadCustomIniSections(const std::filesystem::path& path,
			std::vector<std::string>& sections)
		{
			sections.clear();
			std::error_code existsError;
			const bool exists = std::filesystem::exists(path, existsError);
			if (existsError)
			{
				TC_Core_Error("Could not inspect ImGui settings '{0}': {1}",
					PathToUTF8(path), existsError.message());
				return false;
			}

			std::ifstream input(path, std::ios::binary);
			if (exists && !input)
			{
				TC_Core_Error("Could not read ImGui settings '{0}'", PathToUTF8(path));
				return false;
			}
			if (!input)
				return true;
			std::ostringstream contents;
			contents << input.rdbuf();
			if (input.bad())
			{
				TC_Core_Error("Failed while reading ImGui settings '{0}'", PathToUTF8(path));
				return false;
			}
			const std::string ini = contents.str();

			std::string::size_type sectionStart = 0;
			while (sectionStart < ini.size())
			{
				if (ini[sectionStart] != '[' || (sectionStart > 0 && ini[sectionStart - 1] != '\n'))
				{
					sectionStart = ini.find("\n[", sectionStart);
					if (sectionStart == std::string::npos)
						break;
					++sectionStart;
				}
				const std::string::size_type headerEnd = ini.find('\n', sectionStart);
				const std::string header = ini.substr(sectionStart,
					headerEnd == std::string::npos ? std::string::npos : headerEnd - sectionStart);
				const std::string::size_type nextMarker = headerEnd == std::string::npos
					? std::string::npos : ini.find("\n[", headerEnd);
				const std::string::size_type sectionEnd = nextMarker == std::string::npos
					? ini.size() : nextMarker + 1;
				if (!IsImGuiManagedIniSection(header))
					sections.push_back(ini.substr(sectionStart, sectionEnd - sectionStart));
				if (nextMarker == std::string::npos)
					break;
				sectionStart = nextMarker + 1;
			}
			return true;
		}

		bool SaveImGuiSettingsPreservingCustomSections(const std::filesystem::path& path)
		{
			if (!EnsureSettingsDirectory(path))
				return false;

			std::vector<std::string> customSections;
			if (!ReadCustomIniSections(path, customSections))
				return false;
			size_t size = 0;
			const char* settings = ImGui::SaveIniSettingsToMemory(&size);
			if (!settings)
				return false;

			std::string ini(settings, size);
			for (const std::string& section : customSections)
			{
				if (!ini.empty() && ini.back() != '\n')
					ini.push_back('\n');
				ini += section;
			}
			std::string writeError;
			if (FileSystem::WriteFileAtomically(path, ini, writeError))
				return true;
			TC_Core_Error("Failed to save ImGui settings '{0}': {1}", PathToUTF8(path), writeError);
			return false;
		}

	}

	EditorLayer::EditorLayer()
		: Layer("EditorLayer")
	{
		m_CurrentProject = ProjectManager::Get().GetActiveProject();
		if (m_CurrentProject)
			m_Is2DMode = m_CurrentProject->GetConfig().Template == "2D";
	}

	void EditorLayer::LoadSceneToolbarLayout()
	{
		// Restore the in-code fallback before applying the packaged baseline and
		// the selected global/project override. This prevents one layout's values
		// from leaking into another layout that has no settings file yet.
		m_GizmoModeToolbarDocked = true;
		m_GizmoTransformToolbarDocked = false;
		m_GizmoModeToolbarFirst = true;
		m_GizmoModeToolbarOffset = { 16.0f, 10.0f };
		m_GizmoToolbarOffset = { 16.0f, 48.0f };

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

		// Custom sections are not consumed by ImGui itself. Apply the packaged
		// defaults first, then let the selected global/project section override them.
		loadFrom(GetDefaultEditorLayoutPath());
		loadFrom(GetEditorLayoutPath(m_CurrentProject));
	}

	void EditorLayer::SaveSceneToolbarLayout()
	{
		const std::filesystem::path iniPath = GetEditorLayoutPath(m_CurrentProject);
		if (!EnsureSettingsDirectory(iniPath))
			return;

		std::string ini;
		{
			std::error_code existsError;
			const bool exists = std::filesystem::exists(iniPath, existsError);
			if (existsError)
			{
				TC_Core_Error("Could not inspect Scene toolbar settings '{0}': {1}",
					PathToUTF8(iniPath), existsError.message());
				return;
			}

			std::ifstream fin(iniPath, std::ios::binary);
			if (exists && !fin)
			{
				TC_Core_Error("Could not read Scene toolbar settings '{0}'", PathToUTF8(iniPath));
				return;
			}
			if (fin)
			{
				std::stringstream contents;
				contents << fin.rdbuf();
				if (fin.bad())
				{
					TC_Core_Error("Failed while reading Scene toolbar settings '{0}'", PathToUTF8(iniPath));
					return;
				}
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
		const std::string::size_type sectionPos = FindIniSectionHeader(ini, sectionName);
		if (sectionPos != std::string::npos)
		{
			const std::string::size_type nextSection = ini.find("\n[", sectionPos + sectionName.size());
			ini.erase(sectionPos, nextSection == std::string::npos ? std::string::npos : nextSection - sectionPos);
		}
		if (!ini.empty() && ini.back() != '\n')
			ini.push_back('\n');
		ini += section.str();

		std::string writeError;
		if (!FileSystem::WriteFileAtomically(iniPath, ini, writeError))
			TC_Core_Error("Failed to save Scene toolbar layout '{0}': {1}", PathToUTF8(iniPath), writeError);
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

		// The packaged root file is a read-only baseline. The selected writable
		// layout is global in no-project mode and project-local otherwise.
		LoadImGuiSettings(GetDefaultEditorLayoutPath());
		LoadImGuiSettings(GetEditorLayoutPath(m_CurrentProject));

		LoadSceneToolbarLayout();

		m_EditorCamera = EditorCamera(30.0f, 1.778f, 0.1f, 1000.0f);
		m_EditorCamera.Set2DMode(m_Is2DMode);

		m_SceneHierarchyPanel.SetSceneLoadCallback([this](const std::filesystem::path& path) {
			OpenScene(path);
		});
		m_ContentBrowserPanel.SetSceneOpenCallback([this](const std::filesystem::path& path) {
			OpenScene(path);
		});
		m_ContentBrowserPanel.SetAssetRenamedCallback([this](const std::filesystem::path& oldPath,
			const std::filesystem::path& newPath) {
			std::filesystem::path relative;
			if (!m_EditorScenePath.empty() && TryGetRelativeWithin(oldPath, m_EditorScenePath, relative))
				m_EditorScenePath = newPath / relative;

			const bool editorChanged = m_SceneHierarchyPanel.RemapSpriteTextureReferences(
				m_EditorScene, oldPath, newPath);
			if (m_ActiveScene && m_ActiveScene != m_EditorScene)
				m_SceneHierarchyPanel.RemapSpriteTextureReferences(m_ActiveScene, oldPath, newPath);
			if (editorChanged)
				m_SceneDirty = true;
		});
		m_ContentBrowserPanel.SetAssetDeletedCallback([this](const std::filesystem::path& deletedPath) {
			std::filesystem::path relative;
			if (!m_EditorScenePath.empty() && TryGetRelativeWithin(deletedPath, m_EditorScenePath, relative))
			{
				m_EditorScenePath.clear();
				m_SceneDirty = true;
			}

			const bool editorChanged = m_SceneHierarchyPanel.ClearSpriteTextureReferences(
				m_EditorScene, deletedPath);
			if (m_ActiveScene && m_ActiveScene != m_EditorScene)
				m_SceneHierarchyPanel.ClearSpriteTextureReferences(m_ActiveScene, deletedPath);
			if (editorChanged)
				m_SceneDirty = true;
		});

		m_SceneHierarchyPanel.SetSpriteCreateCallback([this](const std::filesystem::path& path) {
			if (!m_ActiveScene)
				return;
			std::string fileName = PathToUTF8(path.stem());
			auto Square = m_ActiveScene->CreateEntity(fileName);
			auto& SpriteR = Square.AddComponent<SpriteRenderer>(glm::vec4{ 1.0f, 1.0f, 1.0f, 1.0f });
			SpriteR.Texture = Texture2D::Create(path);
			if (m_SceneState == SceneState::Edit)
				m_SceneDirty = true;
		});
		m_SceneHierarchyPanel.SetSceneModifiedCallback([this]() {
			if (m_SceneState == SceneState::Edit)
				m_SceneDirty = true;
		});

		if (m_CurrentProject)
			OpenProjectStartScene();
		else
		{
			NewScene();
			m_SceneDirty = false;
		}
	}

	void EditorLayer::OnDetach()
	{
		TC_PROFILE_FUNCTION();
		if (m_SceneState == SceneState::Play)
			OnSceneStop();

		if (m_CurrentProject)
			m_ContentBrowserPanel.Serialize();

		// ImGui-managed state and custom panel sections share either the global
		// no-project layout or the active project's UserSettings/imgui.ini.
		SaveImGuiSettingsPreservingCustomSections(GetEditorLayoutPath(m_CurrentProject));
		m_ContentBrowserPanel.SaveLayoutSetting();
		SaveSceneToolbarLayout();
	}

	void EditorLayer::OnUpdate(Timestep ts)
	{
		TC_PROFILE_FUNCTION();

		// Resize Scene Framebuffer
		const uint32_t sceneWidth = ToFramebufferExtent(m_ViewportSize.x);
		const uint32_t sceneHeight = ToFramebufferExtent(m_ViewportSize.y);
		if (FramebufferSpecification spec = m_Framebuffer->GetSpecification();
			sceneWidth > 0 && sceneHeight > 0 &&
			(spec.Width != sceneWidth || spec.Height != sceneHeight))
		{
			if (m_Framebuffer->Resize(sceneWidth, sceneHeight))
				m_EditorCamera.SetViewportSize(static_cast<float>(sceneWidth), static_cast<float>(sceneHeight));
		}

		// Resize Game Framebuffer
		const uint32_t gameWidth = ToFramebufferExtent(m_GameViewportSize.x);
		const uint32_t gameHeight = ToFramebufferExtent(m_GameViewportSize.y);
		if (FramebufferSpecification gameSpec = m_GameFramebuffer->GetSpecification();
			gameWidth > 0 && gameHeight > 0 &&
			(gameSpec.Width != gameWidth || gameSpec.Height != gameHeight))
		{
			if (m_GameFramebuffer->Resize(gameWidth, gameHeight))
				m_ActiveScene->OnViewportResize(gameWidth, gameHeight);
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
		m_EditorCamera.OnUpdate(ts, m_ViewportCameraDragOwned);

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
		else
		{
			m_HoveredEntity = {};
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

				if (ImGui::MenuItem("Exit")) RequestExit();
				ImGui::EndMenu();
			}

			if (ImGui::BeginMenu("Project"))
			{
				if (m_CurrentProject)
				{
					ImGui::Text("ProjectName: %s", m_CurrentProject->GetName().c_str());
					const std::string projectPath = PathToUTF8(m_CurrentProject->GetProjectPath());
					ImGui::Text("Path: %s", projectPath.c_str());
					ImGui::Separator();
					ImGui::Text("EditorVersion: %s", m_CurrentProject->GetEditorVersion().c_str());
				}
				else
				{
					ImGui::TextColored(ImVec4(0.6f, 0.6f, 0.6f, 1.0f), "No project loaded");
				}
				ImGui::EndMenu();
			}

			if (ImGui::BeginMenu("Window"))
			{
				ImGui::MenuItem("Scene", nullptr, &m_ShowScenePanel);
				ImGui::MenuItem("Game", nullptr, &m_ShowGamePanel);
				ImGui::Separator();
				ImGui::MenuItem("Hierarchy", nullptr, &m_ShowHierarchyPanel);
				ImGui::MenuItem("Inspector", nullptr, &m_ShowInspectorPanel);
				ImGui::MenuItem("Project", nullptr, &m_ShowProjectPanel);
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

		m_SceneHierarchyPanel.OnImGuiRender(&m_ShowHierarchyPanel, &m_ShowInspectorPanel);
		m_ContentBrowserPanel.OnImGuiRender(&m_ShowProjectPanel);

		if (m_ShowScenePanel)
		{
		ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2{ 0, 0 });

		// Use the same native menu-bar slot as Hierarchy.  ImGui's dock tab and
		// menu-bar layout then share one geometry source, eliminating the hand-
		// positioned gap that appeared with the custom Scene strip.
		const char* sceneTitle = m_SceneDirty ? "Scene *###Scene" : "Scene###Scene";
		const bool sceneVisible = ImGui::Begin(sceneTitle, &m_ShowScenePanel, ImGuiWindowFlags_MenuBar);
		if (!sceneVisible)
		{
			m_ViewportFocused = false;
			m_HoveredEntity = {};
		}

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

		ImVec2 viewportPanelSize = ImGui::GetContentRegionAvail();
		m_ViewportSize = { viewportPanelSize.x, viewportPanelSize.y };

		uint64_t sceneTextureID = m_Framebuffer->GetColorAttachmentRendererID();
		ImGui::Image(reinterpret_cast<void*>(sceneTextureID), ImVec2{ m_ViewportSize.x, m_ViewportSize.y },
			ImVec2{ 0, 1 }, ImVec2{ 1, 0 });
		m_ViewportCanvasHovered = sceneVisible && ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenBlockedByActiveItem);
		Application::Get().GetImGuiLayer()->BlockEvents(!m_ViewportCanvasHovered && !m_ViewportCameraDragOwned);

		// The framebuffer image must remain the current ImGui item while registering
		// its drop target. Toolbar items submitted later must never steal the target.
		if (ImGui::BeginDragDropTarget())
		{
			const std::filesystem::path assetPath = m_CurrentProject ? m_CurrentProject->GetAssetPath() : g_AssetPath;
			const ImGuiDragDropFlags flags = ImGuiDragDropFlags_AcceptNoDrawDefaultRect;
			if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("SPRITE", flags))
			{
				const wchar_t* relativePath = static_cast<const wchar_t*>(payload->Data);
				const std::filesystem::path texturePath = assetPath / relativePath;
				if (m_ActiveScene)
				{
					Entity sprite = m_ActiveScene->CreateEntity(PathToUTF8(texturePath.stem()));
					auto& renderer = sprite.AddComponent<SpriteRenderer>(glm::vec4{ 1.0f });
					renderer.Texture = Texture2D::Create(texturePath);
					if (m_SceneState == SceneState::Edit)
						m_SceneDirty = true;
				}
			}
			ImGui::EndDragDropTarget();
		}

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
					if (m_ActiveScene->SetWorldTransform(selectedEntity, transform)
						&& m_SceneState == SceneState::Edit)
						m_SceneDirty = true;
				}
			}
		}

		ImGui::End();
		ImGui::PopStyleVar();
		}
		else
		{
			m_ViewportFocused = false;
			m_ViewportCanvasHovered = false;
			m_ViewportCameraDragOwned = false;
			m_HoveredEntity = {};
		}

		if (m_ShowGamePanel)
		{
		ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2{ 0, 0 });

		ImGui::Begin("Game", &m_ShowGamePanel, ImGuiWindowFlags_MenuBar);

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
		}

		UI_UnsavedChangesModal();
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
		const float modeWidth = 5.0f * 2.0f + 24.0f + dockGap + 62.0f;

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
			const float modeWidth = 5.0f * 2.0f + 24.0f + 4.0f + 62.0f;
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
		const float width = padding * 2.0f + handleWidth + gap + buttonWidth;
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
			// The space choice is represented by its icon. Keep the button
			// surface neutral after a click; only pointer hover may tint it.
			draw->AddRectFilled(min, max, hovered ? hover : normal, 2.0f);
			draw->AddRect(min, max, IM_COL32(25, 25, 25, 255), 2.0f, 0, 1.0f);
		};

		auto DrawDropArrow = [&](const ImVec2& min, const ImVec2& max)
		{
			const ImVec2 c(max.x - 8.0f, (min.y + max.y) * 0.5f + 1.0f);
			draw->AddTriangleFilled(ImVec2(c.x - 3.0f, c.y - 2.0f), ImVec2(c.x + 3.0f, c.y - 2.0f), ImVec2(c.x, c.y + 2.5f), arrow);
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
		const ImVec2 spaceMin(topLeft.x + padding + handleWidth + gap, buttonMinY);
		const ImVec2 spaceMax(spaceMin.x + buttonWidth, spaceMin.y + buttonHeight);

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

		if (ImGui::ImageButton(reinterpret_cast<ImTextureID>(static_cast<uintptr_t>(icon->GetRendererID())), ImVec2(size, size),
			ImVec2(0, 0), ImVec2(1, 1), 0))
		{
			if (m_SceneState == SceneState::Edit && m_EditorScene)
				OnScenePlay();
			else if (m_SceneState == SceneState::Play)
				OnSceneStop();
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
		if (m_SceneState != SceneState::Edit || !m_EditorScene)
			return;
		m_ActiveScene = Scene::Copy(m_EditorScene);
		if (!m_ActiveScene)
			return;
		ResizeSceneForGameView(m_ActiveScene);
		m_ActiveScene->OnRuntimeStart();
		m_SceneState = SceneState::Play;
		m_SceneHierarchyPanel.SetContext(m_ActiveScene, false, true);
		ResetSceneInteractionState();

		// 切换到Game窗口焦点
		ImGui::SetWindowFocus("Game");

	}

	void EditorLayer::OnSceneStop()
	{
		if (m_SceneState != SceneState::Play)
			return;

		Ref<Scene> runtimeScene = m_ActiveScene;
		if (runtimeScene)
			runtimeScene->OnRuntimeStop();
		m_ActiveScene = m_EditorScene;
		m_SceneState = SceneState::Edit;
		ResizeSceneForGameView(m_ActiveScene);
		m_SceneHierarchyPanel.SetContext(m_ActiveScene, false, true);
		ResetSceneInteractionState();

		// 切换到Scene窗口焦点
		ImGui::SetWindowFocus("Scene");
	}

	void EditorLayer::OnEvent(Event& e)
	{
		if (m_ViewportCanvasHovered)
			m_EditorCamera.OnEvent(e);

		EventDispatcher dispatcher(e);
		dispatcher.Dispatch<WindowCloseEvent>(TC_Bind_Event_Fn(EditorLayer::OnWindowClose));
		dispatcher.Dispatch<KeyPressedEvent>(TC_Bind_Event_Fn(EditorLayer::OnKeyPressed));
		dispatcher.Dispatch<MouseButtonPressedEvent>(TC_Bind_Event_Fn(EditorLayer::OnMouseButtonPressed));
		dispatcher.Dispatch<MouseButtonReleasedEvent>(TC_Bind_Event_Fn(EditorLayer::OnMouseButtonReleased));
	}

	bool EditorLayer::OnKeyPressed(KeyPressedEvent& e)
	{
		// Shortcuts
		if (e.GetRepeatCount() > 0)
			return false;

		const bool control = e.IsControlDown();
		const bool shift = e.IsShiftDown();
		const bool alt = e.IsAltDown();
		const bool super = e.IsSuperDown();
		bool handled = false;
		switch (e.GetKeyCode())
		{
		case Key::N:
		{
			if (control && !shift && !alt && !super)
			{
				NewScene();
				handled = true;
			}

			break;
		}
		case Key::O:
		{
			if (control && !shift && !alt && !super)
			{
				OpenScene();
				handled = true;
			}

			break;
		}
		case Key::S:
		{
			if (control && !alt && !super)
			{
				if (shift)
					SaveSceneAs();
				else
					SaveScene();
				handled = true;
			}

			break;
		}

		// Scene commands only belong to the Scene canvas or Hierarchy. This keeps
		// Delete/Cut/Copy/Paste from leaking out of text fields and Project assets.
		case Key::D:
		{
			if (!ImGui::GetIO().WantTextInput &&
				(m_ViewportFocused || m_SceneHierarchyPanel.IsHierarchyFocused()) &&
				control && !shift && !alt && !super)
			{
				handled = m_SceneHierarchyPanel.HandleShortcut(e.GetKeyCode(), control);
			}

			break;
		}
		case Key::X:
		case Key::C:
		case Key::V:
		{
			if (!ImGui::GetIO().WantTextInput &&
				(m_ViewportFocused || m_SceneHierarchyPanel.IsHierarchyFocused()) &&
				control && !shift && !alt && !super)
			{
				handled = m_SceneHierarchyPanel.HandleShortcut(e.GetKeyCode(), control);
			}
			break;
		}
		case Key::F2:
		case Key::Delete:
		{
			if (!ImGui::GetIO().WantTextInput &&
				(m_ViewportFocused || m_SceneHierarchyPanel.IsHierarchyFocused()) &&
				!control && !shift && !alt && !super)
				handled = m_SceneHierarchyPanel.HandleShortcut(e.GetKeyCode(), control);
			break;
		}

		// Gizmos
		case Key::Q:
		{
			if (m_ViewportFocused && !control && !shift && !alt && !super && !ImGuizmo::IsUsing())
			{
				m_GizmoType = -1;
				handled = true;
			}
			break;
		}
		case Key::W:
		{
			if (m_ViewportFocused && !control && !shift && !alt && !super && !ImGuizmo::IsUsing())
			{
				m_GizmoType = ImGuizmo::OPERATION::TRANSLATE;
				handled = true;
			}
			break;
		}
		case Key::E:
		{
			if (m_ViewportFocused && !control && !shift && !alt && !super && !ImGuizmo::IsUsing())
			{
				m_GizmoType = ImGuizmo::OPERATION::ROTATE;
				handled = true;
			}
			break;
		}
		case Key::R:
		{
			if (m_ViewportFocused && !control && !shift && !alt && !super && !ImGuizmo::IsUsing())
			{
				m_GizmoType = ImGuizmo::OPERATION::SCALE;
				handled = true;
			}
			break;
		}
		}

		return handled;
	}

	bool EditorLayer::OnMouseButtonPressed(MouseButtonPressedEvent& e)
	{
		const int button = e.GetMouseButton();
		const bool altDown = e.IsAltDown();
		const bool cameraButton = button == Mouse::ButtonMiddle || button == Mouse::ButtonRight ||
			(button == Mouse::ButtonLeft && altDown);
		if (cameraButton && m_ViewportFocused && m_ViewportCanvasHovered)
		{
			m_ViewportCameraDragOwned = true;
			return true;
		}

		if (e.GetMouseButton() == Mouse::ButtonLeft)
		{
			if (m_ViewportCanvasHovered && !ImGuizmo::IsOver() && !altDown)
			{
				m_SceneHierarchyPanel.SetSelectedEntity(m_HoveredEntity);
				return true;
			}
		}
		return false;
	}

	bool EditorLayer::OnMouseButtonReleased(MouseButtonReleasedEvent& e)
	{
		const int button = e.GetMouseButton();
		if (m_ViewportCameraDragOwned &&
			(button == Mouse::ButtonLeft || button == Mouse::ButtonMiddle || button == Mouse::ButtonRight))
		{
			m_ViewportCameraDragOwned = false;
			return true;
		}
		return false;
	}

	bool EditorLayer::OnWindowClose(WindowCloseEvent&)
	{
		RequestExit();
		return true;
	}


	void EditorLayer::NewScene()
	{
		if (m_SceneDirty)
		{
			RequestDestructiveAction([this]() {
				NewScene();
				return true;
			});
			return;
		}
		if (m_SceneState == SceneState::Play)
			OnSceneStop();
		m_EditorScene = CreateRef<Scene>();
		m_EditorScene->SetSceneName("Untitled");
		m_ActiveScene = m_EditorScene;
		AddDefaultMainCamera();
		ResizeSceneForGameView(m_ActiveScene);
		m_SceneHierarchyPanel.SetContext(m_ActiveScene);
		m_SceneHierarchyPanel.SetSelectedEntity({});
		m_SceneDirty = true;

		m_EditorScenePath = std::filesystem::path();
		ResetSceneInteractionState();
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

	bool EditorLayer::OpenProjectStartScene()
	{
		if (!m_CurrentProject)
			return false;
		const std::filesystem::path startScene =
			m_CurrentProject->GetAssetPath() / m_CurrentProject->GetConfig().StartScene;
		std::error_code error;
		if (std::filesystem::is_regular_file(startScene, error))
		{
			if (OpenScene(startScene))
				return true;
			TC_Core_Error("Failed to load project start scene: {0}", PathToUTF8(startScene));
		}
		else
		{
			TC_Warn("Project start scene is missing: {0}", PathToUTF8(startScene));
		}

		// A project switch must never leave the previous project's scene or path
		// active when the new start scene is missing or malformed.
		NewScene();
		return false;
	}

	bool EditorLayer::OpenScene()
	{
		const std::filesystem::path filepath = FileDialogs::OpenFile("TomCat Scene (*.tomcat)\0*.tomcat\0");
		return !filepath.empty() && OpenScene(filepath);
	}

	bool EditorLayer::OpenScene(const std::filesystem::path& path)
	{
		std::string extension = PathToUTF8(path.extension());
		std::transform(extension.begin(), extension.end(), extension.begin(),
			[](unsigned char c) { return static_cast<char>(std::tolower(c)); });
		if (extension != ".tomcat")
		{
			TC_Warn("Could not load {0} - not a scene file", PathToUTF8(path.filename()));
			return false;
		}
		if (m_SceneDirty)
		{
			RequestDestructiveAction([this, path]() { return OpenScene(path); });
			return false;
		}

		Ref<Scene> newScene = CreateRef<Scene>();
		SceneSerializer serializer(newScene);
		if (!serializer.Deserialize(path))
			return false;

		if (m_SceneState == SceneState::Play)
			OnSceneStop();
		newScene->SetSceneName(PathToUTF8(path.stem()));
		m_EditorScene = newScene;
		ResizeSceneForGameView(m_EditorScene);
		m_SceneHierarchyPanel.SetContext(m_EditorScene);

		m_ActiveScene = m_EditorScene;
		m_EditorScenePath = AbsoluteLexicalPath(path);
		m_SceneDirty = false;
		ResetSceneInteractionState();
		return true;
	}

	void EditorLayer::SaveScene()
	{
		if (!m_EditorScene)
			return;
		if (!m_EditorScenePath.empty())
		{
			if (SerializeScene(m_EditorScene, m_EditorScenePath))
				m_SceneDirty = false;
		}
		else
			SaveSceneAs();
	}	

	void EditorLayer::SaveSceneAs()
	{
		if (!m_EditorScene)
			return;
		std::filesystem::path filepath = FileDialogs::SaveFile("TomCat Scene (*.tomcat)\0*.tomcat\0");
		if (!filepath.empty())
		{
			std::filesystem::path path = std::move(filepath);
			std::string extension = PathToUTF8(path.extension());
			std::transform(extension.begin(), extension.end(), extension.begin(),
				[](unsigned char c) { return static_cast<char>(std::tolower(c)); });
			if (extension != ".tomcat")
				path.replace_extension(".tomcat");
			if (SerializeScene(m_EditorScene, path))
			{
				m_EditorScenePath = path;
				m_SceneDirty = false;
			}
		}
	}

	bool EditorLayer::SerializeScene(const Ref<Scene>& scene, const std::filesystem::path& path)
	{
		if (!scene || path.empty())
			return false;
		const std::string previousName = scene->GetSceneName();
		scene->SetSceneName(PathToUTF8(path.stem()));
		SceneSerializer serializer(scene);
		if (!serializer.Serialize(path))
		{
			scene->SetSceneName(previousName);
			return false;
		}
		return true;
	}

	void EditorLayer::ResizeSceneForGameView(const Ref<Scene>& scene)
	{
		const uint32_t width = ToFramebufferExtent(m_GameViewportSize.x);
		const uint32_t height = ToFramebufferExtent(m_GameViewportSize.y);
		if (scene && width > 0 && height > 0)
			scene->OnViewportResize(width, height);
	}

	void EditorLayer::ResetSceneInteractionState()
	{
		m_HoveredEntity = {};
		m_ViewportCameraDragOwned = false;
	}

	void EditorLayer::RequestDestructiveAction(std::function<bool()> action)
	{
		if (!m_SceneDirty)
		{
			action();
			return;
		}
		m_PendingUnsavedAction = std::move(action);
		m_OpenUnsavedChangesModal = true;
	}

	void EditorLayer::UI_UnsavedChangesModal()
	{
		if (m_OpenUnsavedChangesModal)
		{
			ImGui::OpenPopup("Unsaved Scene Changes");
			m_OpenUnsavedChangesModal = false;
		}

		std::function<bool()> actionToRun;
		bool restoreDirtyOnFailure = false;
		if (ImGui::BeginPopupModal("Unsaved Scene Changes", nullptr, ImGuiWindowFlags_AlwaysAutoResize))
		{
			ImGui::TextUnformatted("The current scene has unsaved changes.");
			ImGui::TextUnformatted("Save before continuing?");
			ImGui::Separator();
			if (ImGui::Button("Save"))
			{
				SaveScene();
				if (!m_SceneDirty)
				{
					actionToRun = std::move(m_PendingUnsavedAction);
					ImGui::CloseCurrentPopup();
				}
			}
			ImGui::SameLine();
			if (ImGui::Button("Discard"))
			{
				m_SceneDirty = false;
				actionToRun = std::move(m_PendingUnsavedAction);
				restoreDirtyOnFailure = true;
				ImGui::CloseCurrentPopup();
			}
			ImGui::SameLine();
			if (ImGui::Button("Cancel"))
			{
				m_PendingUnsavedAction = {};
				ImGui::CloseCurrentPopup();
			}
			ImGui::EndPopup();
		}
		if (actionToRun && !actionToRun() && restoreDirtyOnFailure)
			m_SceneDirty = true;
	}

	void EditorLayer::RequestExit()
	{
		RequestDestructiveAction([]() {
			Application::Get().Close();
			return true;
		});
	}

	bool EditorLayer::OpenProject()
	{
		const std::filesystem::path filepath = FileDialogs::OpenFile("TomCat Project (*.tcproj)\0*.tcproj\0");
		if (filepath.empty())
			return false;

		const std::filesystem::path projectPath = filepath;
		if (m_SceneDirty)
		{
			RequestDestructiveAction([this, projectPath]() { return OpenProject(projectPath); });
			return false;
		}
		return OpenProject(projectPath);
	}

	bool EditorLayer::OpenProject(const std::filesystem::path& path)
	{
		if (m_SceneDirty)
		{
			RequestDestructiveAction([this, path]() { return OpenProject(path); });
			return false;
		}

		// Persist the current layout before ProjectManager changes the active
		// project. No-project mode writes to the global LocalAppData layout.
		SaveImGuiSettingsPreservingCustomSections(GetEditorLayoutPath(m_CurrentProject));
		m_ContentBrowserPanel.SaveLayoutSetting();
		SaveSceneToolbarLayout();
		if (m_CurrentProject)
			m_ContentBrowserPanel.Serialize();
		auto project = ProjectManager::Get().LoadProject(path);
		if (!project)
			return false;

		if (m_SceneState == SceneState::Play)
			OnSceneStop();
		m_CurrentProject = project;
		m_Is2DMode = project->GetConfig().Template == "2D";
		m_EditorCamera.Set2DMode(m_Is2DMode);

		// Reapply the packaged baseline before the new project's override so UI
		// state never carries over from the project that was just closed.
		LoadImGuiSettings(GetDefaultEditorLayoutPath());
		LoadImGuiSettings(GetEditorLayoutPath(m_CurrentProject));
		LoadSceneToolbarLayout();

		OpenProjectStartScene();
		m_ContentBrowserPanel.SetProject(m_CurrentProject);
		return true;
	}

	void EditorLayer::SaveProject()
	{
		if (m_CurrentProject)
			m_ContentBrowserPanel.Serialize();
		SaveImGuiSettingsPreservingCustomSections(GetEditorLayoutPath(m_CurrentProject));
		m_ContentBrowserPanel.SaveLayoutSetting();
		SaveSceneToolbarLayout();
	}

}
