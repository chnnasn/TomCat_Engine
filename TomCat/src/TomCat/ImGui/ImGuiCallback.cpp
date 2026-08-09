#include "tcpch.h"
#include "ImGuiCallback.h"

namespace TomCat {

    WindowCallbacksManager& WindowCallbacksManager::Get()
    {
        static WindowCallbacksManager instance;
        return instance;
    }

    void WindowCallbacksManager::RegisterMoreOptionsCallback(const std::string& key, std::function<void(ImVec2)> callback)
    {
        more_options_callbacks_[key] = callback;
    }

    bool WindowCallbacksManager::ExecuteMoreOptionsCallback(const std::string& key, ImVec2 position)
    {
        
        auto it = more_options_callbacks_.find(key);
        if (it != more_options_callbacks_.end() && it->second)
        {

            it->second(position);
            return true;
        }
        return false;
    }

    void WindowCallbacksManager::RemoveMoreOptionsCallback(const std::string& key)
    {
        more_options_callbacks_.erase(key);
    }

    void WindowCallbacksManager::ClearAllCallbacks()
    {
        more_options_callbacks_.clear();
    }

    bool WindowCallbacksManager::HasCallback(const std::string& key) const
    {
        return more_options_callbacks_.find(key) != more_options_callbacks_.end();
    }

    void WindowCallbacksManager::SetMoreOptionsEnabled(bool enabled)
    {
        m_MoreOptionsEnabled = enabled;
    }

    bool WindowCallbacksManager::IsMoreOptionsEnabled() const
    {
        return m_MoreOptionsEnabled;
    }

    // 便捷函数实现
    void RegisterWindowMoreOptionsCallback(const std::string& key, std::function<void(ImVec2)> callback)
    {
        WindowCallbacksManager::Get().RegisterMoreOptionsCallback(key, callback);
    }

    bool ExecuteWindowMoreOptionsCallback(const std::string& key,ImVec2 posi)
    {
        return WindowCallbacksManager::Get().ExecuteMoreOptionsCallback(key,posi);
    }

    void RemoveWindowMoreOptionsCallback(const std::string& key)
    {
        WindowCallbacksManager::Get().RemoveMoreOptionsCallback(key);
    }

    bool HasWindowMoreOptionsCallback(const std::string& key)
    {
        return WindowCallbacksManager::Get().HasCallback(key);
    }

    void SetMoreOptionsEnabled(bool enabled)
    {
        WindowCallbacksManager::Get().SetMoreOptionsEnabled(enabled);
    }

    bool IsMoreOptionsEnabled()
    {
        return WindowCallbacksManager::Get().IsMoreOptionsEnabled();
    }

    bool DrawWindowMoreOptionsButton(const char* windowName)
    {
        if (!WindowCallbacksManager::Get().IsMoreOptionsEnabled())
            return false;
        if (!HasWindowMoreOptionsCallback(windowName))
            return false;

        ImGuiWindow* window = ImGui::GetCurrentWindow();
        if (!window)
            return false;

        const float buttonSize = ImGui::GetFrameHeight();
        ImVec2 pos = window->TitleBarRect().Min;
        // Place it on the right side of the title bar (left of the X close button)
        pos.x = window->TitleBarRect().Max.x - buttonSize * 2.0f - 8.0f;
        pos.y += 2.0f;

        // U+22EE (vertical ellipsis) button; the glyph is merged from Segoe UI Symbol
        ImGui::SetCursorScreenPos(pos);
        bool clicked = ImGui::Button(("\u22EE##more_options_" + std::string(windowName)).c_str(), ImVec2(buttonSize, buttonSize));

        ImVec2 cmin = ImGui::GetItemRectMin();
        ImVec2 cmax = ImGui::GetItemRectMax();
        if (clicked)
            ExecuteWindowMoreOptionsCallback(windowName, ImVec2((cmin.x + cmax.x) * 0.5f, cmax.y));
        return clicked;
    }

}