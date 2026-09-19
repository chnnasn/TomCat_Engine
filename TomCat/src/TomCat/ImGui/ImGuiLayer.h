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

		void BlockEvents(bool block)
		{
			m_BlockMouseEvents = block;
			m_BlockKeyboardEvents = block;
		}
		void BlockMouseEvents(bool block) { m_BlockMouseEvents = block; }
		void BlockKeyboardEvents(bool block) { m_BlockKeyboardEvents = block; }

		 void SetDarkThemeColors();
	private:
		bool m_BlockMouseEvents = true;
		bool m_BlockKeyboardEvents = true;
	};


}
