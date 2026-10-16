#include "tcpch.h"
#include "VulkanResources.h"
#include "TomCat/Asset/TextureArtifact.h"
#include "backends/imgui_impl_vulkan.h"
#include <stb_image.h>
#include <fstream>
#include <stdexcept>
#include <limits>

namespace TomCat {
VulkanVertexBuffer::VulkanVertexBuffer(uint32_t size) : m_Data(size), m_Used(size) {
    if (!size) throw std::invalid_argument("Empty vertex buffer");
}
VulkanVertexBuffer::VulkanVertexBuffer(float* data, uint32_t size) : VulkanVertexBuffer(size) { SetData(data, size); }
void VulkanVertexBuffer::SetData(const void* data, uint32_t size) {
    if (!data || size > m_Data.size()) throw std::out_of_range("Vertex buffer upload exceeds capacity");
    std::memcpy(m_Data.data(), data, size);
    m_Used = size;
}
VulkanIndexBuffer::VulkanIndexBuffer(uint32_t* data, uint32_t count) {
    if (!data || !count) throw std::invalid_argument("Empty index buffer");
    m_Data.assign(data, data + count);
}
void VulkanVertexArray::AddVertexBuffer(const Ref<VertexBuffer>& buffer) {
    if (!buffer || buffer->GetLayout().GetStride() == 0) throw std::invalid_argument("Vertex buffer needs a layout");
    m_Vertices.push_back(buffer);
}
VulkanUniformBuffer::VulkanUniformBuffer(uint32_t size, uint32_t binding)
    : m_Device(VulkanDevice::Current()), m_Binding(binding) {
    if (binding >= m_Device->Uniforms.size() || !size) throw std::invalid_argument("Invalid uniform buffer binding/size");
    m_Device->Uniforms[binding].resize(size);
}
void VulkanUniformBuffer::SetData(const void* data, uint32_t size, uint32_t offset) {
    auto& bytes = m_Device->Uniforms.at(m_Binding);
    if (!data || offset > bytes.size() || size > bytes.size() - offset) throw std::out_of_range("Uniform upload exceeds capacity");
    std::memcpy(bytes.data() + offset, data, size);
}

VulkanTexture2D::VulkanTexture2D(uint32_t width, uint32_t height) : m_Device(VulkanDevice::Current()) {
    m_Image = m_Device->Image(width, height, VK_FORMAT_R8G8B8A8_UNORM,
        VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT);
    m_ProfileBytes = uint64_t(width) * height * 4;
    ProfileResourceTracker::Get().Texture(1, static_cast<int64_t>(m_ProfileBytes));
}
VulkanTexture2D::VulkanTexture2D(const std::filesystem::path& path) : m_Device(VulkanDevice::Current()), m_Path(path) {
    std::ifstream input(path, std::ios::binary | std::ios::ate);
    auto size = input ? static_cast<std::streamoff>(input.tellg()) : -1;
    if (size <= 0 || size > INT_MAX) return;
    std::vector<uint8_t> bytes(static_cast<size_t>(size));
    input.seekg(0);
    if (input.read(reinterpret_cast<char*>(bytes.data()), size)) Load(bytes.data(), bytes.size());
}
VulkanTexture2D::VulkanTexture2D(const void* encoded, size_t size, const std::filesystem::path& path)
    : m_Device(VulkanDevice::Current()), m_Path(path) { Load(encoded, size); }
void VulkanTexture2D::Load(const void* encoded, size_t size) {
    if (!encoded || !size) return;
    auto bytes = std::span(static_cast<const uint8_t*>(encoded), size);
    if (IsTextureArtifact(bytes)) {
        TextureArtifactView artifact; std::string error;
        if (!ParseTextureArtifact(bytes, artifact, error)) { TC_Core_Error("Vulkan texture: {0}", error); return; }
        m_Image = m_Device->Image(artifact.Width, artifact.Height,
            artifact.SRGB ? VK_FORMAT_R8G8B8A8_SRGB : VK_FORMAT_R8G8B8A8_UNORM,
            VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT, VK_IMAGE_ASPECT_COLOR_BIT,
            static_cast<uint32_t>(artifact.Mips.size()));
        for (uint32_t i = 0; i < artifact.Mips.size(); ++i) {
            const auto& mip = artifact.Mips[i];
            std::vector<uint8_t> decoded;
            if (artifact.Format == TextureArtifactFormat::BC3) {
                if (!DecompressTextureMip(mip, artifact.Format, decoded, error)) throw std::runtime_error(error);
                Upload(i, decoded.data(), decoded.size());
            } else Upload(i, mip.Bytes.data(), mip.Bytes.size());
            m_ProfileBytes += uint64_t(mip.Width) * mip.Height * 4;
        }
    } else {
        if (size > INT_MAX) return;
        int width = 0, height = 0;
        stbi_set_flip_vertically_on_load(1);
        auto pixels = std::unique_ptr<stbi_uc, decltype(&stbi_image_free)>(
            stbi_load_from_memory(bytes.data(), static_cast<int>(size), &width, &height, nullptr, STBI_rgb_alpha), stbi_image_free);
        if (!pixels || width <= 0 || height <= 0) return;
        m_Image = m_Device->Image(width, height, VK_FORMAT_R8G8B8A8_UNORM, VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT);
        m_ProfileBytes = uint64_t(width) * height * 4;
        Upload(0, pixels.get(), static_cast<size_t>(m_ProfileBytes));
    }
    ProfileResourceTracker::Get().Texture(1, static_cast<int64_t>(m_ProfileBytes));
}
VulkanTexture2D::~VulkanTexture2D() {
    // A texture may still occur in an ImGui draw list or in recorded descriptors.
    // UI destruction happens between frames; recorded engine draws retain images.
    if (m_UI) {
        m_Device->DrainForDestruction();
        vkFreeDescriptorSets(m_Device->Device, m_Device->ImGuiPool, 1, &m_UI);
    }
    for (auto& texture : m_Device->Textures) if (texture == this) texture = nullptr;
    if (m_ProfileBytes) ProfileResourceTracker::Get().Texture(-1, -static_cast<int64_t>(m_ProfileBytes));
}
uint32_t VulkanTexture2D::GetRendererID() const { throw std::logic_error("Vulkan textures have no OpenGL renderer ID; use GetUITextureID"); }
uintptr_t VulkanTexture2D::GetUITextureID() const {
    if (!m_Image) return 0;
    if (!m_UI) m_UI = ImGui_ImplVulkan_AddTexture(m_Image->Sampler, m_Image->View, VK_IMAGE_LAYOUT_GENERAL);
    return reinterpret_cast<uintptr_t>(m_UI);
}
void VulkanTexture2D::Upload(uint32_t level, const void* data, size_t size) {
    const uint32_t width = std::max(1u, m_Image->Width >> level), height = std::max(1u, m_Image->Height >> level);
    if (!data || size != uint64_t(width) * height * 4) throw std::invalid_argument("Invalid RGBA texture payload");
    auto staging = m_Device->Buffer(size, VK_BUFFER_USAGE_TRANSFER_SRC_BIT, data);
    m_Device->Barrier();
    VkBufferImageCopy copy{};
    copy.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, level, 0, 1}; copy.imageExtent = {width, height, 1};
    vkCmdCopyBufferToImage(m_Device->Commands(), staging->Buffer, m_Image->Image, VK_IMAGE_LAYOUT_GENERAL, 1, &copy);
    m_Device->Keep(staging); m_Device->Keep(m_Image);
    m_Device->Barrier();
}
void VulkanTexture2D::SetData(const void* data, uint32_t size) {
    if (!m_Image) throw std::logic_error("Texture has no storage");
    Upload(0, data, size);
}
void VulkanTexture2D::Bind(uint32_t slot) const {
    if (slot >= m_Device->Textures.size()) throw std::out_of_range("Texture slot out of range");
    m_Device->Textures[slot] = this;
}

VulkanFramebuffer::VulkanFramebuffer(const FramebufferSpecification& spec)
    : m_Device(VulkanDevice::Current()), m_Spec(spec) {
    try { Create(); } catch (...) { m_Device->DrainForDestruction(); Destroy(); throw; }
}
VulkanFramebuffer::~VulkanFramebuffer() {
    m_Device->DrainForDestruction();
    if (m_Device->Target == this) m_Device->Target = nullptr;
    if (m_Device->DefaultTarget == this) m_Device->DefaultTarget = nullptr;
    Destroy();
}
void VulkanFramebuffer::Create() {
    if (!m_Spec.Width || !m_Spec.Height || m_Spec.Width > MaxFramebufferSize || m_Spec.Height > MaxFramebufferSize)
        throw std::invalid_argument("Invalid framebuffer extent");
    if (m_Spec.Samples != 1) throw std::invalid_argument("Vulkan framebuffer currently requires one sample");
    for (auto attachment : m_Spec.Attachments.Attachments) {
        if (attachment.TextureFormat == FramebufferTextureFormat::DEPTH24STENCIL8) {
            if (m_Depth) throw std::invalid_argument("Duplicate depth attachment");
            m_Depth = m_Device->Image(m_Spec.Width, m_Spec.Height, m_Device->DepthFormat(),
                VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT,
                VK_IMAGE_ASPECT_DEPTH_BIT | VK_IMAGE_ASPECT_STENCIL_BIT);
        } else {
            if (attachment.TextureFormat != FramebufferTextureFormat::RGBA8 && attachment.TextureFormat != FramebufferTextureFormat::RED_INTEGER)
                throw std::invalid_argument("Unsupported framebuffer format");
            auto format = attachment.TextureFormat == FramebufferTextureFormat::RGBA8 ? VK_FORMAT_R8G8B8A8_UNORM : VK_FORMAT_R32_SINT;
            m_Colors.push_back(m_Device->Image(m_Spec.Width, m_Spec.Height, format,
                VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT));
        }
    }
    if (m_Colors.size() > m_Device->Properties.limits.maxColorAttachments) throw std::invalid_argument("Too many framebuffer attachments");
    std::vector<VkAttachmentDescription> attachments;
    std::vector<VkAttachmentReference> references;
    std::vector<VkImageView> views;
    for (auto& color : m_Colors) {
        VkAttachmentDescription desc{}; desc.format = color->Format; desc.samples = VK_SAMPLE_COUNT_1_BIT;
        desc.loadOp = VK_ATTACHMENT_LOAD_OP_LOAD; desc.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
        desc.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE; desc.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
        desc.initialLayout = desc.finalLayout = VK_IMAGE_LAYOUT_GENERAL;
        references.push_back({static_cast<uint32_t>(attachments.size()), VK_IMAGE_LAYOUT_GENERAL});
        attachments.push_back(desc); views.push_back(color->View);
    }
    VkAttachmentReference depth{};
    if (m_Depth) {
        depth = {static_cast<uint32_t>(attachments.size()), VK_IMAGE_LAYOUT_GENERAL};
        VkAttachmentDescription desc{}; desc.format = m_Depth->Format; desc.samples = VK_SAMPLE_COUNT_1_BIT;
        desc.loadOp = desc.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_LOAD;
        desc.storeOp = desc.stencilStoreOp = VK_ATTACHMENT_STORE_OP_STORE;
        desc.initialLayout = desc.finalLayout = VK_IMAGE_LAYOUT_GENERAL;
        attachments.push_back(desc); views.push_back(m_Depth->View);
    }
    VkSubpassDescription subpass{}; subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
    subpass.colorAttachmentCount = static_cast<uint32_t>(references.size()); subpass.pColorAttachments = references.data();
    subpass.pDepthStencilAttachment = m_Depth ? &depth : nullptr;
    VkRenderPassCreateInfo create{VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO};
    create.attachmentCount = static_cast<uint32_t>(attachments.size()); create.pAttachments = attachments.data();
    create.subpassCount = 1; create.pSubpasses = &subpass;
    VulkanCheck(vkCreateRenderPass(m_Device->Device, &create, nullptr, &m_RenderPass), "Create render pass");
    VkFramebufferCreateInfo framebuffer{VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO};
    framebuffer.renderPass = m_RenderPass; framebuffer.attachmentCount = static_cast<uint32_t>(views.size());
    framebuffer.pAttachments = views.data(); framebuffer.width = m_Spec.Width; framebuffer.height = m_Spec.Height; framebuffer.layers = 1;
    VulkanCheck(vkCreateFramebuffer(m_Device->Device, &framebuffer, nullptr, &m_Framebuffer), "Create framebuffer");
    m_UI.resize(m_Colors.size());
    m_ProfileBytes = uint64_t(m_Spec.Width) * m_Spec.Height * (m_Colors.size() * 4 + (m_Depth ? 8 : 0));
    ProfileResourceTracker::Get().Framebuffer(1, static_cast<int64_t>(m_ProfileBytes));
    Clear({0, 0, 0, 0});
}
void VulkanFramebuffer::Destroy() {
    for (auto descriptor : m_UI) if (descriptor) vkFreeDescriptorSets(m_Device->Device, m_Device->ImGuiPool, 1, &descriptor);
    m_UI.clear();
    if (m_Framebuffer) vkDestroyFramebuffer(m_Device->Device, m_Framebuffer, nullptr);
    if (m_RenderPass) vkDestroyRenderPass(m_Device->Device, m_RenderPass, nullptr);
    m_Framebuffer = VK_NULL_HANDLE; m_RenderPass = VK_NULL_HANDLE;
    m_Colors.clear(); m_Depth.reset();
    if (m_ProfileBytes) ProfileResourceTracker::Get().Framebuffer(-1, -static_cast<int64_t>(m_ProfileBytes));
    m_ProfileBytes = 0;
}
void VulkanFramebuffer::Bind() {
    m_Device->Target = this;
    m_Device->Viewport = {0, 0, float(m_Spec.Width), float(m_Spec.Height), 0, 1};
}
void VulkanFramebuffer::Unbind() { m_Device->Target = m_Device->DefaultTarget; }
bool VulkanFramebuffer::Resize(uint32_t width, uint32_t height) {
    if (!width || !height || width > MaxFramebufferSize || height > MaxFramebufferSize) return false;
    if (width == m_Spec.Width && height == m_Spec.Height) return true;
    try {
        auto spec = m_Spec; spec.Width = width; spec.Height = height;
        VulkanFramebuffer replacement(spec);
        m_Device->SubmitAndWait();
        std::swap(m_Spec, replacement.m_Spec); std::swap(m_Colors, replacement.m_Colors);
        std::swap(m_Depth, replacement.m_Depth); std::swap(m_UI, replacement.m_UI);
        std::swap(m_RenderPass, replacement.m_RenderPass); std::swap(m_Framebuffer, replacement.m_Framebuffer);
        std::swap(m_ProfileBytes, replacement.m_ProfileBytes);
        return true;
    } catch (const std::exception& e) { TC_Core_Error("Vulkan resize: {0}", e.what()); return false; }
}
void VulkanFramebuffer::BeginPass() {
    m_Device->Barrier();
    VkRenderPassBeginInfo begin{VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO};
    begin.renderPass = m_RenderPass; begin.framebuffer = m_Framebuffer;
    begin.renderArea.extent = {m_Spec.Width, m_Spec.Height};
    vkCmdBeginRenderPass(m_Device->Commands(), &begin, VK_SUBPASS_CONTENTS_INLINE);
}
void VulkanFramebuffer::Clear(const std::array<float, 4>& color) {
    m_Device->Barrier();
    VkImageSubresourceRange range{VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
    for (auto& image : m_Colors) {
        VkClearColorValue value{};
        if (image->Format == VK_FORMAT_R32_SINT) value.int32[0] = -1;
        else std::copy(color.begin(), color.end(), value.float32);
        vkCmdClearColorImage(m_Device->Commands(), image->Image, VK_IMAGE_LAYOUT_GENERAL, &value, 1, &range);
    }
    if (m_Depth) {
        range.aspectMask = m_Depth->Aspect;
        VkClearDepthStencilValue value{1.0f, 0};
        vkCmdClearDepthStencilImage(m_Device->Commands(), m_Depth->Image, VK_IMAGE_LAYOUT_GENERAL, &value, 1, &range);
    }
}
void VulkanFramebuffer::ClearAttachment(uint32_t attachment, int value) {
    auto& image = m_Colors.at(attachment);
    m_Device->Barrier();
    VkClearColorValue clear{};
    if (image->Format == VK_FORMAT_R32_SINT) clear.int32[0] = value;
    else for (auto& component : clear.float32) component = float(std::clamp(value, 0, 255)) / 255.0f;
    VkImageSubresourceRange range{VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
    vkCmdClearColorImage(m_Device->Commands(), image->Image, VK_IMAGE_LAYOUT_GENERAL, &clear, 1, &range);
}
int VulkanFramebuffer::ReadPixel(uint32_t attachment, int x, int y) {
    if (attachment >= m_Colors.size() || x < 0 || y < 0 || uint32_t(x) >= m_Spec.Width || uint32_t(y) >= m_Spec.Height) return -1;
    auto& image = m_Colors[attachment];
    if (image->Format != VK_FORMAT_R32_SINT) return -1;
    auto staging = m_Device->Buffer(4, VK_BUFFER_USAGE_TRANSFER_DST_BIT);
    m_Device->Barrier();
    VkBufferImageCopy copy{}; copy.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
    copy.imageOffset = {x, y, 0}; copy.imageExtent = {1, 1, 1};
    vkCmdCopyImageToBuffer(m_Device->Commands(), image->Image, VK_IMAGE_LAYOUT_GENERAL, staging->Buffer, 1, &copy);
    m_Device->SubmitAndWait();
    int value = -1; std::memcpy(&value, staging->Mapped, sizeof(value)); return value;
}
uint32_t VulkanFramebuffer::GetColorAttachmentRendererID(uint32_t) const { throw std::logic_error("Use GetColorAttachmentUITextureID for Vulkan"); }
uintptr_t VulkanFramebuffer::GetColorAttachmentUITextureID(uint32_t index) const {
    const auto& image = m_Colors.at(index);
    if (image->Format == VK_FORMAT_R32_SINT) throw std::invalid_argument("Integer attachment cannot be sampled by ImGui");
    if (!m_UI.at(index)) m_UI[index] = ImGui_ImplVulkan_AddTexture(image->Sampler, image->View, VK_IMAGE_LAYOUT_GENERAL);
    return reinterpret_cast<uintptr_t>(m_UI[index]);
}
}
