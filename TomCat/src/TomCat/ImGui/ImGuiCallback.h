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

        // 清理回调
        void RemoveMoreOptionsCallback(const std::string& key);
        void ClearAllCallbacks();

        // 检查回调是否存在
        bool HasCallback(const std::string& key) const;

    private:
        WindowCallbacksManager() = default;
        ~WindowCallbacksManager() = default;

        std::unordered_map<std::string, std::function<void(ImVec2)>> more_options_callbacks_;
    };

    // 便捷函数
    void RegisterWindowMoreOptionsCallback(const std::string& key, std::function<void(ImVec2)> callback);
    bool ExecuteWindowMoreOptionsCallback(const std::string& key, ImVec2 position);
    void RemoveWindowMoreOptionsCallback(const std::string& key);
    bool HasWindowMoreOptionsCallback(const std::string& key);

}