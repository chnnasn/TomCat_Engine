#include "tcpch.h"
#include "Texture.h"
#include "TomCat/RHI/RenderDevice.h"
namespace TomCat {
Ref<Texture2D> Texture2D::Create(uint32_t w, uint32_t h) { return RHI::RenderDevice::Get().CreateTexture(w, h); }
Ref<Texture2D> Texture2D::Create(const std::filesystem::path& path) { return RHI::RenderDevice::Get().CreateTexture(path); }
Ref<Texture2D> Texture2D::Create(const void* data, size_t size, const std::filesystem::path& path) { return RHI::RenderDevice::Get().CreateTexture(data, size, path); }
}
