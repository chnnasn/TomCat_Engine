#pragma once

#include <functional>
#include <string>
#include <unordered_map>

#include "../../../vendor/ImGui/imgui.h"

namespace TomCat {

	class WindowCallbacksManager
	{
	public:
		using MoreOptionsCallback = std::function<void(ImVec2)>;

		static WindowCallbacksManager& Get();

		void RegisterMoreOptionsCallback(const std::string& key, MoreOptionsCallback callback);
		bool ExecuteMoreOptionsCallback(const std::string& key, ImVec2 popupAnchor);
		void RemoveMoreOptionsCallback(const std::string& key);
		bool HasMoreOptionsCallback(const std::string& key) const;

		void SetMoreOptionsEnabled(bool enabled) { m_MoreOptionsEnabled = enabled; }
		bool IsMoreOptionsEnabled() const { return m_MoreOptionsEnabled; }

	private:
		std::unordered_map<std::string, MoreOptionsCallback> m_MoreOptionsCallbacks;
		bool m_MoreOptionsEnabled = true;
	};

	void RegisterWindowMoreOptionsCallback(const std::string& key,
		WindowCallbacksManager::MoreOptionsCallback callback);
	bool ExecuteWindowMoreOptionsCallback(const std::string& key, ImVec2 popupAnchor);
	void RemoveWindowMoreOptionsCallback(const std::string& key);
	bool HasWindowMoreOptionsCallback(const std::string& key);
	void SetMoreOptionsEnabled(bool enabled);
	bool IsMoreOptionsEnabled();

}
