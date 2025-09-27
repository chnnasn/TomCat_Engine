#pragma once

#include "TomCat.h"

namespace TomCat {

	class EditorLayer : public Layer
	{
	public:
		EditorLayer();
		virtual ~EditorLayer() = default;

		virtual void OnAttach() override;
		virtual void OnDetach() override;

		void OnUpdate(Timestep ts) override;
		virtual void OnImGuiRender() override;
		void OnEvent(Event& e) override;
	private:
		TomCat::OrthographicCameraController m_CameraController;

		// Temp
		Ref<VertexArray> m_SquareVA;
		Ref<Shader> m_FlatColorShader;
		Ref<Framebuffer> m_Framebuffer;

		Ref<Texture2D> m_CheckerboardTexture;

		glm::vec2 m_ViewportSize = { 0.0f, 0.0f };

		glm::vec4 m_SquareColor = { 0.2f, 0.3f, 0.8f, 1.0f };

		// UI State
		bool m_ShowHierarchy = true;
		bool m_ShowInspector = true;
		bool m_ShowProject = true;
		bool m_ShowConsole = true;
		bool m_ShowSceneSettings = true;
		
		// Scene Objects
		struct SceneObject {
			std::string name;
			bool selected = false;
			bool expanded = false;
			std::vector<std::shared_ptr<SceneObject>> children;
		};
		std::vector<std::shared_ptr<SceneObject>> m_SceneObjects;
		std::shared_ptr<SceneObject> m_SelectedObject = nullptr;

		// Inspector Properties
		struct Transform {
			glm::vec3 position = { 0.0f, 0.0f, 0.0f };
			glm::vec3 rotation = { 0.0f, 0.0f, 0.0f };
			glm::vec3 scale = { 1.0f, 1.0f, 1.0f };
		};
		Transform m_SelectedTransform;

		// Project Panel
		std::vector<std::string> m_ProjectFiles;
		std::string m_SelectedAsset = "";

		// Console
		std::vector<std::string> m_ConsoleLogs;
		bool m_ShowErrors = true;
		bool m_ShowWarnings = true;
		bool m_ShowInfo = true;

		// Scene Settings
		glm::vec3 m_SceneLightDirection = { 0.5f, -1.0f, 0.3f };
		float m_SceneAmbientIntensity = 0.3f;
		glm::vec4 m_SceneBackgroundColor = { 0.1f, 0.1f, 0.1f,1.0f};

	private:
		void DrawMenuBar();
		void DrawToolbar();
		void DrawHierarchyPanel();
		void DrawInspectorPanel();
		void DrawProjectPanel();
		void DrawConsolePanel();
		void DrawSceneSettingsPanel();
		void DrawViewportPanel();
		void DrawStatusBar();
		
		void SetupImGuiStyle();
		void AddSceneObject(const std::string& name);
		void DrawSceneObjectTree(std::shared_ptr<SceneObject> obj, int depth = 0);
	};

}