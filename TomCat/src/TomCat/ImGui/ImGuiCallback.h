#pragma once

#include <functional>
#include <unordered_map>
#include <string>

#include "../ImGui/imgui_internal.h"

namespace TomCat {

    class WindowCallbacksManager
    {
    public:
        static WindowCallbacksManager& Get();

        // 注册更多选项回调 - 使用字符串作为键
        void RegisterMoreOptionsCallback(const std::string& key, std::function<void(ImVec2)> callback);

        // 执行回调
        bool ExecuteMoreOptionsCallback(const std::string& key, ImVec2 position);

        // Control whether the "more options" (U+22EE) button is enabled
        void SetMoreOptionsEnabled(bool enabled);
        bool IsMoreOptionsEnabled() const;

        // 清理回调
        void RemoveMoreOptionsCallback(const std::string& key);
        void ClearAllCallbacks();

        // 检查回调是否存在
        bool HasCallback(const std::string& key) const;

    private:
        WindowCallbacksManager() = default;
        ~WindowCallbacksManager() = default;

        std::unordered_map<std::string, std::function<void(ImVec2)>> more_options_callbacks_;
        bool m_MoreOptionsEnabled = true;
    };

    // 便捷函数
    void RegisterWindowMoreOptionsCallback(const std::string& key, std::function<void(ImVec2)> callback);
    bool ExecuteWindowMoreOptionsCallback(const std::string& key, ImVec2 position);
    void RemoveWindowMoreOptionsCallback(const std::string& key);
    bool HasWindowMoreOptionsCallback(const std::string& key);

    // Free convenience wrappers for the feature flag
    void SetMoreOptionsEnabled(bool enabled);
    bool IsMoreOptionsEnabled();

    // Call after ImGui::Begin(windowName): if that window registered a "more options"
    // callback and the feature is enabled, draw a U+22EE (...) button on the title bar
    // right side and trigger the callback when clicked. Returns true when clicked.
    bool DrawWindowMoreOptionsButton(const char* windowName);

}