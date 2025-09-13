#include "tcpch.h"
#include "LayerStack.h"

namespace TomCat {

	LayerStack::LayerStack()
	{

	}

	LayerStack ::~LayerStack()
	{
		for (Layer* Layer : m_Layers)
			delete Layer;
	}

	void LayerStack::PushLayer(Layer* Layer) {
		m_Layers.emplace(m_Layers.begin()+ m_LayerInsertIndex, Layer);
		m_LayerInsertIndex++;
	
	}
	void LayerStack::PushOverLayer(Layer* OverLay) {
		m_Layers.emplace_back(OverLay);
	
	}
	void LayerStack::PopLayer(Layer* Layer) {
		auto it = std::find(m_Layers.begin(), m_Layers.end(),Layer);

		if (it != m_Layers.end())
		{
			m_Layers.erase(it);
			m_LayerInsertIndex--;
		}
	}
	void LayerStack::PopOverLayer(Layer* OverLay) {
		auto it = std::find(m_Layers.begin(), m_Layers.end(), OverLay);

		if (it != m_Layers.end())
		{
			m_Layers.erase(it);
		}
	}
}

