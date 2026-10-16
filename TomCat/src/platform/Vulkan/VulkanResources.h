#pragma once
#include "TomCat/Core/Base.h"
#include "TomCat/Renderer/Buffer.h"
#include "TomCat/Renderer/VertexArray.h"
#include "TomCat/Renderer/UniformBuffer.h"
#include "TomCat/Renderer/Texture.h"
#include "TomCat/Renderer/Framebuffer.h"
#include "VulkanDevice.h"

namespace TomCat {

class VulkanVertexBuffer final : public VertexBuffer {
public:
    explicit VulkanVertexBuffer(uint32_t size);
    VulkanVertexBuffer(float* data, uint32_t size);
    void Bind() const override {}
    void Unbind() const override {}
    void SetData(const void* data, uint32_t size) override;
    void SetLayout(const BufferLayout& layout) override { m_Layout = layout; }
    const BufferLayout& GetLayout() const override { return m_Layout; }
    std::span<const uint8_t> Data() const { return std::span(m_Data).first(m_Used); }
private:
    BufferLayout m_Layout;
    std::vector<uint8_t> m_Data;
    uint32_t m_Used = 0;
};
class VulkanIndexBuffer final : public IndexBuffer {
public:
    VulkanIndexBuffer(uint32_t* data, uint32_t count);
    void Bind() const override {}
    void Unbind() const override {}
    uint32_t GetCount() const override { return static_cast<uint32_t>(m_Data.size()); }
    std::span<const uint32_t> Data() const { return m_Data; }
private:
    std::vector<uint32_t> m_Data;
};
class VulkanVertexArray final : public VertexArray {
public:
    void Bind() const override {}
    void Unbind() const override {}
    void AddVertexBuffer(const Ref<VertexBuffer>& buffer) override;
    void SetIndexBuffer(const Ref<IndexBuffer>& buffer) override { m_Index = buffer; }
    const std::vector<Ref<VertexBuffer>>& GetVertexBuffers() const override { return m_Vertices; }
    const Ref<IndexBuffer>& GetIndexBuffer() const override { return m_Index; }
private:
    std::vector<Ref<VertexBuffer>> m_Vertices;
    Ref<IndexBuffer> m_Index;
};
class VulkanUniformBuffer final : public UniformBuffer {
public:
    VulkanUniformBuffer(uint32_t size, uint32_t binding);
    void SetData(const void* data, uint32_t size, uint32_t offset) override;
private:
    std::shared_ptr<VulkanDevice> m_Device;
    uint32_t m_Binding;
};

class VulkanTexture2D final : public Texture2D {
public:
    VulkanTexture2D(uint32_t width, uint32_t height);
    explicit VulkanTexture2D(const std::filesystem::path& path);
    VulkanTexture2D(const void* encoded, size_t size, const std::filesystem::path& path);
    ~VulkanTexture2D() override;
    uint32_t GetWidth() const override { return m_Image ? m_Image->Width : 0; }
    uint32_t GetHeight() const override { return m_Image ? m_Image->Height : 0; }
    uint32_t GetRendererID() const override;
    uintptr_t GetUITextureID() const override;
    void SetData(const void* data, uint32_t size) override;
    void Bind(uint32_t slot) const override;
    bool IsLoaded() const override { return m_Image != nullptr; }
    bool operator==(const Texture& other) const override { return this == &other; }
    const std::filesystem::path& GetPath() const override { return m_Path; }
    const std::shared_ptr<VulkanImageAllocation>& NativeImage() const { return m_Image; }
private:
    void Load(const void* encoded, size_t size);
    void Upload(uint32_t level, const void* data, size_t size);
    std::shared_ptr<VulkanDevice> m_Device;
    std::shared_ptr<VulkanImageAllocation> m_Image;
    std::filesystem::path m_Path;
    mutable VkDescriptorSet m_UI = VK_NULL_HANDLE;
    uint64_t m_ProfileBytes = 0;
};

class VulkanFramebuffer final : public Framebuffer {
public:
    explicit VulkanFramebuffer(const FramebufferSpecification& spec);
    ~VulkanFramebuffer() override;
    void Bind() override;
    void Unbind() override;
    bool Resize(uint32_t width, uint32_t height) override;
    int ReadPixel(uint32_t attachment, int x, int y) override;
    void ClearAttachment(uint32_t attachment, int value) override;
    uint32_t GetColorAttachmentRendererID(uint32_t index) const override;
    uintptr_t GetColorAttachmentUITextureID(uint32_t index) const override;
    const FramebufferSpecification& GetSpecification() const override { return m_Spec; }
    VkRenderPass RenderPass() const { return m_RenderPass; }
    VkFramebuffer NativeFramebuffer() const { return m_Framebuffer; }
    const std::vector<std::shared_ptr<VulkanImageAllocation>>& Colors() const { return m_Colors; }
    const std::shared_ptr<VulkanImageAllocation>& Depth() const { return m_Depth; }
    void BeginPass();
    void Clear(const std::array<float, 4>& color);
private:
    void Create();
    void Destroy();
    std::shared_ptr<VulkanDevice> m_Device;
    FramebufferSpecification m_Spec;
    std::vector<std::shared_ptr<VulkanImageAllocation>> m_Colors;
    std::shared_ptr<VulkanImageAllocation> m_Depth;
    mutable std::vector<VkDescriptorSet> m_UI;
    VkRenderPass m_RenderPass = VK_NULL_HANDLE;
    VkFramebuffer m_Framebuffer = VK_NULL_HANDLE;
    uint64_t m_ProfileBytes = 0;
};
}
