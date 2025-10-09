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

}