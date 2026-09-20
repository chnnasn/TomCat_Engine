#include "tcpch.h"
#include "RendererAPI.h"
#include "TomCat/RHI/RenderDevice.h"
#include <cstdlib>
#include <string_view>
#include <stdexcept>
namespace TomCat {
RendererAPI::API RendererAPI::GetAPI() {
    static const API selected = [] {
#ifndef TC_PLATFORM_WEB
        if (const char* backend = std::getenv("TC_RENDERER"); backend && *backend) {
            if (std::string_view(backend) == "vulkan") return API::Vulkan;
            if (std::string_view(backend) != "opengl") throw std::invalid_argument("TC_RENDERER must be opengl or vulkan");
        }
#endif
        return API::OpenGL;
    }();
    return selected;
}
Scope<RendererAPI> RendererAPI::Create() { return RHI::RenderDevice::Get().CreateCommands(); }
}
