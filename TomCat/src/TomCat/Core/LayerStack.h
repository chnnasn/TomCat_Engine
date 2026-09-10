#pragma once

#include "TomCat/Core/Base.h"

#include "Layer.h"

#include <vector>

namespace TomCat {

	class LayerStack
	{
	public:
		LayerStack() = default;
		~LayerStack();
		LayerStack(const LayerStack&) = delete;
		LayerStack& operator=(const LayerStack&) = delete;
		LayerStack(LayerStack&&) = delete;
		LayerStack& operator=(LayerStack&&) = delete;

		bool PushLayer(Layer* layer);
		bool PushOverlay(Layer* layer);
		void PopLayer(Layer* layer);
		void PopOverlay(Layer* layer);
		void Clear();

		std::vector<Layer*>::iterator begin() { return m_Layers.begin(); }
		std::vector<Layer*>::iterator end() { return m_Layers.end(); }
		std::vector<Layer*>::const_iterator begin() const { return m_Layers.begin(); }
		std::vector<Layer*>::const_iterator end() const { return m_Layers.end(); }

	private:
		std::vector<Layer*> m_Layers;
		unsigned int  m_LayerInsertIndex = 0;
	};

}
