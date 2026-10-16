#include "tcpch.h"
#include "RendererAPI.h"
#include "TomCat/RHI/RenderDevice.h"
#ifndef TC_PLATFORM_WEB
#include "platform/Vulkan/VulkanSupport.h"
#endif
#include <cstdlib>
#include <string_view>
#include <stdexcept>
namespace TomCat {
namespace {
struct BackendSelection {
    RendererAPI::API Backend = RendererAPI::API::OpenGL;
    bool Automatic = false;
};
BackendSelection& SelectedBackend() {
    static BackendSelection selected = [] {
#ifndef TC_PLATFORM_WEB
        const char* value = std::getenv("TC_RENDERER");
        const std::string_view requested = value ? value : "";
        if (requested == "opengl") {
            if (Log::GetCoreLogger()) TC_Core_Info("Renderer: OpenGL (TC_RENDERER=opengl)");
            return BackendSelection{RendererAPI::API::OpenGL, false};
        }
        if (!requested.empty() && requested != "auto" && requested != "vulkan")
            throw std::invalid_argument("TC_RENDERER must be auto, opengl or vulkan");
        const bool automatic = requested != "vulkan";
        const auto support = ProbeVulkanSupport();
        if (support.Supported) {
            if (Log::GetCoreLogger()) TC_Core_Info("Renderer: Vulkan ({0})", automatic ? "automatic" : "TC_RENDERER=vulkan");
            return BackendSelection{RendererAPI::API::Vulkan, automatic};
        }
        if (!automatic) throw VulkanUnsupportedError(support.Reason);
        if (Log::GetCoreLogger()) TC_Core_Warn("Renderer: OpenGL fallback; Vulkan unavailable: {0}", support.Reason);
        return BackendSelection{RendererAPI::API::OpenGL, true};
#else
        return BackendSelection{};
#endif
    }();
    return selected;
}
}
RendererAPI::API RendererAPI::GetAPI() { return SelectedBackend().Backend; }
bool RendererAPI::TryUseOpenGLFallback(const char* reason) {
    auto& selected = SelectedBackend();
    if (!selected.Automatic || selected.Backend != API::Vulkan) return false;
    selected.Backend = API::OpenGL;
    if (Log::GetCoreLogger()) TC_Core_Warn("Renderer: OpenGL fallback; Vulkan unavailable: {0}", reason);
    return true;
}
Scope<RendererAPI> RendererAPI::Create() { return RHI::RenderDevice::Get().CreateCommands(); }
}
