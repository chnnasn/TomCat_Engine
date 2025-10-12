#pragma once

#include "TomCat.h"
#include "TomCat/Renderer/EditorCamera.h"
#include <string>
#include <filesystem>


namespace TomCat {

	class ExampleLayer : public Layer
	{
	public:
		ExampleLayer();

		virtual ~ExampleLayer() = default;

		virtual void OnAttach() override;
		virtual void OnDetach() override;

		void OnUpdate(Timestep ts) override;
		virtual void OnImGuiRender() override;
		void OnEvent(Event& e) override;
	private:

		void AddProject();
		void NewProject();

	private:
		int m_SelectedMenu;
	};
}