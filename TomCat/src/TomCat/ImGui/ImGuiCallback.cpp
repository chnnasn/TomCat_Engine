#include "tcpch.h"
#include "ImGuiCallback.h"

namespace TomCat {

	WindowCallbacksManager& WindowCallbacksManager::Get()
	{
		static WindowCallbacksManager manager;
		return manager;
	}

	void WindowCallbacksManager::RegisterMoreOptionsCallback(
		const std::string& key, MoreOptionsCallback callback)
	{
		if (callback)
			m_MoreOptionsCallbacks[key] = std::move(callback);
		else
			m_MoreOptionsCallbacks.erase(key);
	}

	bool WindowCallbacksManager::ExecuteMoreOptionsCallback(
		const std::string& key, ImVec2 popupAnchor)
	{
		const auto callback = m_MoreOptionsCallbacks.find(key);
		if (callback == m_MoreOptionsCallbacks.end() || !callback->second)
			return false;
		callback->second(popupAnchor);
		return true;
	}

	void WindowCallbacksManager::RemoveMoreOptionsCallback(const std::string& key)
	{
		m_MoreOptionsCallbacks.erase(key);
	}

	bool WindowCallbacksManager::HasMoreOptionsCallback(const std::string& key) const
	{
		return m_MoreOptionsCallbacks.find(key) != m_MoreOptionsCallbacks.end();
	}

	void RegisterWindowMoreOptionsCallback(const std::string& key,
		WindowCallbacksManager::MoreOptionsCallback callback)
	{
		WindowCallbacksManager::Get().RegisterMoreOptionsCallback(key, std::move(callback));
	}

	bool ExecuteWindowMoreOptionsCallback(const std::string& key, ImVec2 popupAnchor)
	{
		return WindowCallbacksManager::Get().ExecuteMoreOptionsCallback(key, popupAnchor);
	}

	void RemoveWindowMoreOptionsCallback(const std::string& key)
	{
		WindowCallbacksManager::Get().RemoveMoreOptionsCallback(key);
	}

	bool HasWindowMoreOptionsCallback(const std::string& key)
	{
		return WindowCallbacksManager::Get().HasMoreOptionsCallback(key);
	}

	void SetMoreOptionsEnabled(bool enabled)
	{
		WindowCallbacksManager::Get().SetMoreOptionsEnabled(enabled);
	}

	bool IsMoreOptionsEnabled()
	{
		return WindowCallbacksManager::Get().IsMoreOptionsEnabled();
	}

}
