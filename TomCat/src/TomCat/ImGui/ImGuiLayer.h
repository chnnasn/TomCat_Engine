#pragma once

#include "TomCat/Layer.h"
#include "TomCat/Events/KeyEvent.h"
#include "TomCat/Events/MouseEvent.h"
#include "TomCat/Events/ApplicationEvent.h"

namespace TomCat {

	class TomCat_API ImGuiLayer : public Layer
	{
	public:
		ImGuiLayer();
		~ImGuiLayer();

		virtual void OnAttach() override;
		virtual void OnDetach() override;


		virtual void OnImGuiRender() override;


		 void Begin() ;
		 void End();
	private:
		float m_Time = 0.0f;
	};


}