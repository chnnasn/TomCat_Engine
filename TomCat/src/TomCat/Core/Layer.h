#pragma once

#include "TomCat/Core/Base.h"

#include "TomCat/Events/Event.h"

#include "TomCat/Core/TimeStep.h"

namespace TomCat{
	
	class Layer
	{
	public:
		Layer(const std::string& name = "Layer");
		virtual ~Layer();

		virtual void OnAttach() {}
		virtual void OnDetach() {}
		// Main-thread work that must also run while the window is minimized.
		// No rendering or ImGui frame is active at this point.
		virtual void OnFrameBegin() {}
		virtual void OnUpdate(Timestep ts) {}
		virtual void OnImGuiRender() {}
		virtual void OnEvent(Event& event){}

		inline const std::string& GetName() const { return m_DebugName; }

	protected:
		std::string m_DebugName;

	};


}
