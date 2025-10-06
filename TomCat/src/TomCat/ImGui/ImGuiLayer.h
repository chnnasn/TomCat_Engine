#pragma once

#include "TomCat/Core/Layer.h"
#include "TomCat/Events/KeyEvent.h"
#include "TomCat/Events/MouseEvent.h"
#include "TomCat/Events/ApplicationEvent.h"

namespace TomCat {

	class ImGuiLayer : public Layer
	{
	public:
		ImGuiLayer();
		~ImGuiLayer();

		virtual void OnAttach() override;
		virtual void OnDetach() override;
		virtual void OnEvent(Event& e) override;

		 void Begin() ;
		 void End();

		 void BlockEvents(bool block) { m_BlockEvents = block; };

		 void SetDarkThemeColors();
	private:
		bool m_BlockEvents = true;
		float m_Time = 0.0f;
	};


}