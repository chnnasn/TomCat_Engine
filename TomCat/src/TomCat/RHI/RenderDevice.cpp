#include "tcpch.h"
#include "RenderDevice.h"
#include "TomCat/Renderer/RendererAPI.h"
#include "platform/OpenGL/OpenGLBuffer.h"
#include "platform/OpenGL/OpenGLVertexArray.h"
#include "platform/OpenGL/OpenGLUniformBuffer.h"
#include "platform/OpenGL/OpenGLTexture.h"
#include "platform/OpenGL/OpenGLShader.h"
#include "platform/OpenGL/OpenGLFramebuffer.h"
#include "platform/OpenGL/OpenGLRendererAPI.h"
#include "platform/OpenGL/OpenGLProfiler.h"
#include "platform/OpenGL/OpenGLApi.h"
#ifndef TC_PLATFORM_WEB
#include "platform/Vulkan/VulkanRendererAPI.h"
#endif
#include <stdexcept>

namespace TomCat::RHI {
namespace {
Scope<RenderDevice> s_Device;
class OpenGLDevice final : public RenderDevice {
public:
    void WaitIdle() override { glFinish(); }
    DeviceCapabilities Capabilities() const override {
#ifdef TC_PLATFORM_WEB
        return {"WebGL2", 16, false};
#else
        return {"OpenGL", 32, OpenGLProfiler::IsSupported()};
#endif
    }
    Scope<RendererAPI> CreateCommands() override { return CreateScope<OpenGLRendererAPI>(); }
    Ref<VertexBuffer> CreateVertexBuffer(uint32_t size, const void* data) override {
        auto buffer = CreateRef<OpenGLVertexBuffer>(size); if (data) buffer->SetData(data, size); return buffer;
    }
    Ref<IndexBuffer> CreateIndexBuffer(uint32_t* data, uint32_t count) override { return CreateRef<OpenGLIndexBuffer>(data, count); }
    Ref<VertexArray> CreateVertexArray() override { return CreateRef<OpenGLVertexArray>(); }
    Ref<UniformBuffer> CreateUniformBuffer(uint32_t size, uint32_t binding) override { return CreateRef<OpenGLUniformBuffer>(size, binding); }
    Ref<Texture2D> CreateTexture(uint32_t w, uint32_t h) override { return CreateRef<OpenGLTexture2D>(w, h); }
    Ref<Texture2D> CreateTexture(const std::filesystem::path& path) override { return CreateRef<OpenGLTexture2D>(path); }
    Ref<Texture2D> CreateTexture(const void* data, size_t size, const std::filesystem::path& path) override { return CreateRef<OpenGLTexture2D>(data, size, path); }
    Ref<Shader> CreateShader(const std::filesystem::path& path) override { return CreateRef<OpenGLShader>(path); }
    Ref<Shader> CreateShader(const std::string& name, const std::string& vertex, const std::string& fragment) override { return CreateRef<OpenGLShader>(name, vertex, fragment); }
    Ref<Shader> CreateShader(const std::string& name, std::span<const uint8_t> artifact) override { return CreateRef<OpenGLShader>(name, artifact); }
    Ref<Framebuffer> CreateFramebuffer(const FramebufferSpecification& spec) override { return CreateRef<OpenGLFramebuffer>(spec); }
    void BeginProfileFrame(uint64_t frame) override { OpenGLProfiler::BeginFrame(frame); }
    void EndProfileFrame() override { OpenGLProfiler::EndFrame(); }
    void ShutdownProfiling() override { OpenGLProfiler::Shutdown(); }
};
#ifndef TC_PLATFORM_WEB
class VulkanRenderDevice final : public RenderDevice {
public:
    void WaitIdle() override { auto d = VulkanDevice::Current(); d->SubmitAndWait(); VulkanCheck(vkDeviceWaitIdle(d->Device), "Wait for device"); }
    uint32_t ValidationErrors() const override { return VulkanDevice::Current()->ValidationErrors.load(); }
    DeviceCapabilities Capabilities() const override { auto d = VulkanDevice::Current(); return {d->Properties.deviceName, 32, d->SupportsProfiling()}; }
    Scope<RendererAPI> CreateCommands() override { return CreateScope<VulkanRendererAPI>(); }
    Ref<VertexBuffer> CreateVertexBuffer(uint32_t size, const void* data) override {
        auto buffer = CreateRef<VulkanVertexBuffer>(size); if (data) buffer->SetData(data, size); return buffer;
    }
    Ref<IndexBuffer> CreateIndexBuffer(uint32_t* data, uint32_t count) override { return CreateRef<VulkanIndexBuffer>(data, count); }
    Ref<VertexArray> CreateVertexArray() override { return CreateRef<VulkanVertexArray>(); }
    Ref<UniformBuffer> CreateUniformBuffer(uint32_t size, uint32_t binding) override { return CreateRef<VulkanUniformBuffer>(size, binding); }
    Ref<Texture2D> CreateTexture(uint32_t w, uint32_t h) override { return CreateRef<VulkanTexture2D>(w, h); }
    Ref<Texture2D> CreateTexture(const std::filesystem::path& path) override { return CreateRef<VulkanTexture2D>(path); }
    Ref<Texture2D> CreateTexture(const void* data, size_t size, const std::filesystem::path& path) override { return CreateRef<VulkanTexture2D>(data, size, path); }
    Ref<Shader> CreateShader(const std::filesystem::path& path) override { return CreateRef<VulkanShader>(path); }
    Ref<Shader> CreateShader(const std::string& name, const std::string& vertex, const std::string& fragment) override { return CreateRef<VulkanShader>(name, vertex, fragment); }
    Ref<Shader> CreateShader(const std::string& name, std::span<const uint8_t> artifact) override { return CreateRef<VulkanShader>(name, artifact); }
    Ref<Framebuffer> CreateFramebuffer(const FramebufferSpecification& spec) override { return CreateRef<VulkanFramebuffer>(spec); }
    void BeginProfileFrame(uint64_t frame) override { VulkanDevice::Current()->BeginProfile(frame); }
    void EndProfileFrame() override { VulkanDevice::Current()->EndProfile(); }
    void ShutdownProfiling() override { VulkanDevice::Current()->SubmitAndWait(); }
};
#endif
}
RenderDevice& RenderDevice::Get() {
    if (!s_Device) {
        switch (RendererAPI::GetAPI()) {
        case RendererAPI::API::OpenGL: s_Device = CreateScope<OpenGLDevice>(); break;
#ifndef TC_PLATFORM_WEB
        case RendererAPI::API::Vulkan: s_Device = CreateScope<VulkanRenderDevice>(); break;
#endif
        default: throw std::runtime_error("Requested graphics backend is not available in this build");
        }
    }
    return *s_Device;
}
void RenderDevice::Shutdown() { s_Device.reset(); }
}
