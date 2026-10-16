#pragma once

#include "TomCat/Core/Base.h"
#include <cstdint>
#include <filesystem>
#include <span>
#include <string>

namespace TomCat {
class VertexBuffer; class IndexBuffer; class VertexArray; class UniformBuffer;
class Texture2D; class Shader; class Framebuffer; class RendererAPI;
struct FramebufferSpecification;

namespace RHI {
struct DeviceCapabilities {
    std::string Name;
    uint32_t MaxTextureSlots = 0;
    bool GpuTimestamps = false;
};

// Backend-neutral resource/command factory. Renderer2D owns batching and scene
// policy; this device owns GPU implementation selection. Legacy resource Create
// methods forward here so backend switches cannot diverge between resource types.
class RenderDevice {
public:
    virtual ~RenderDevice() = default;
    static RenderDevice& Get();
    static void Shutdown();
    virtual DeviceCapabilities Capabilities() const = 0;
    virtual uint32_t ValidationErrors() const { return 0; }
    virtual void WaitIdle() = 0;
    virtual Scope<RendererAPI> CreateCommands() = 0;
    virtual Ref<VertexBuffer> CreateVertexBuffer(uint32_t size, const void* initialData) = 0;
    virtual Ref<IndexBuffer> CreateIndexBuffer(uint32_t* indices, uint32_t count) = 0;
    virtual Ref<VertexArray> CreateVertexArray() = 0;
    virtual Ref<UniformBuffer> CreateUniformBuffer(uint32_t size, uint32_t binding) = 0;
    virtual Ref<Texture2D> CreateTexture(uint32_t width, uint32_t height) = 0;
    virtual Ref<Texture2D> CreateTexture(const std::filesystem::path& path) = 0;
    virtual Ref<Texture2D> CreateTexture(const void* encoded, size_t size, const std::filesystem::path& path) = 0;
    virtual Ref<Shader> CreateShader(const std::filesystem::path& path) = 0;
    virtual Ref<Shader> CreateShader(const std::string& name, const std::string& vertex, const std::string& fragment) = 0;
    virtual Ref<Shader> CreateShader(const std::string& name, std::span<const uint8_t> artifact) = 0;
    virtual Ref<Framebuffer> CreateFramebuffer(const FramebufferSpecification& specification) = 0;
    virtual void BeginProfileFrame(uint64_t frame) = 0;
    virtual void EndProfileFrame() = 0;
    virtual void ShutdownProfiling() = 0;
};
}
}
