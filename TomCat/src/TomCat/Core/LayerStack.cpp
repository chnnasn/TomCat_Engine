#include "tcpch.h"
#include "LayerStack.h"

namespace TomCat {

	LayerStack ::~LayerStack()
	{
		Clear();
	}

	bool LayerStack::PushLayer(Layer* layer) {
		if (!layer)
		{
			TC_Core_Error("Cannot push a null layer");
			return false;
		}
		if (std::find(m_Layers.begin(), m_Layers.end(), layer) != m_Layers.end())
			return false;

		m_Layers.emplace(m_Layers.begin()+ m_LayerInsertIndex, layer);
		m_LayerInsertIndex++;
		return true;
	}
	bool LayerStack::PushOverlay(Layer* overlay) {
		if (!overlay)
		{
			TC_Core_Error("Cannot push a null overlay");
			return false;
		}
		if (std::find(m_Layers.begin(), m_Layers.end(), overlay) != m_Layers.end())
			return false;

		m_Layers.emplace_back(overlay);
		return true;
	}
	void LayerStack::PopLayer(Layer* layer) {
		auto layerEnd = m_Layers.begin() + m_LayerInsertIndex;
		auto it = std::find(m_Layers.begin(), layerEnd, layer);

		if (it != layerEnd)
		{
			layer->OnDetach();
			m_Layers.erase(it);
			m_LayerInsertIndex--;
			delete layer;
		}
	}
	void LayerStack::PopOverlay(Layer* overlay) {
		auto overlayBegin = m_Layers.begin() + m_LayerInsertIndex;
		auto it = std::find(overlayBegin, m_Layers.end(), overlay);

		if (it != m_Layers.end())
		{
			overlay->OnDetach();
			m_Layers.erase(it);
			delete overlay;
		}
	}
	void LayerStack::Clear()
	{
		for (Layer* layer : m_Layers)
		{
			layer->OnDetach();
			delete layer;
		}

		m_Layers.clear();
		m_LayerInsertIndex = 0;
	}
}

