#pragma once

#include "TomCat.h"
#include "EditorUIManager.h"

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

		// Rendering components
		Ref<Framebuffer> m_Framebuffer;
		Ref<Texture2D> m_CheckerboardTexture;

		Ref<Scene> m_ActiveScene;
		Entity m_CameraEntity;

		glm::vec2 m_ViewportSize = { 0.0f, 0.0f };
		glm::vec4 m_SquareColor = { 0.2f, 0.3f, 0.8f, 1.0f };

		// UI Manager
		std::unique_ptr<EditorUIManager> m_UIManager;
		bool m_SceneFocuse = false;
	};

}