#include "tcpch.h"
#include "VulkanContext.h"
#include <GLFW/glfw3.h>
#include "backends/imgui_impl_vulkan.h"
#include <stdexcept>

namespace TomCat {
namespace { VulkanContext* s_Context = nullptr; }
VulkanContext& VulkanContext::Current() {
    if (!s_Context) throw std::logic_error("No active Vulkan context");
    return *s_Context;
}
void VulkanContext::Init() {
    m_Device = VulkanDevice::Create(m_Window);
    VulkanCheck(glfwCreateWindowSurface(m_Device->Instance, m_Window, nullptr, &m_Surface), "Create window surface");
    VkFenceCreateInfo fence{VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};
    VulkanCheck(vkCreateFence(m_Device->Device, &fence, nullptr, &m_AcquireFence), "Create acquire fence");
    int width, height; glfwGetFramebufferSize(m_Window, &width, &height);
    FramebufferSpecification spec;
    spec.Width = std::max(width, 1); spec.Height = std::max(height, 1);
    spec.Attachments = {FramebufferTextureFormat::RGBA8, FramebufferTextureFormat::RED_INTEGER, FramebufferTextureFormat::DEPTH24STENCIL8};
    m_Default = std::make_unique<VulkanFramebuffer>(spec);
    m_Device->DefaultTarget = m_Default.get(); m_Default->Bind();
    if (width > 0 && height > 0) Recreate(width, height);
    s_Context = this;
}
VulkanContext::~VulkanContext() {
    if (!m_Device) return;
    try { m_Device->SubmitAndWait(); } catch (...) {}
    vkDeviceWaitIdle(m_Device->Device);
    if (m_ImGui) ShutdownImGui();
    m_Default.reset();
    DestroySwapchain();
    if (m_RenderPass) vkDestroyRenderPass(m_Device->Device, m_RenderPass, nullptr);
    if (m_AcquireFence) vkDestroyFence(m_Device->Device, m_AcquireFence, nullptr);
    if (m_Surface) vkDestroySurfaceKHR(m_Device->Instance, m_Surface, nullptr);
    if (s_Context == this) s_Context = nullptr;
}
void VulkanContext::DestroySwapchain() {
    for (auto framebuffer : m_Framebuffers) vkDestroyFramebuffer(m_Device->Device, framebuffer, nullptr);
    for (auto view : m_Views) vkDestroyImageView(m_Device->Device, view, nullptr);
    for (auto ready : m_Ready) vkDestroySemaphore(m_Device->Device, ready, nullptr);
    m_Framebuffers.clear(); m_Views.clear(); m_Ready.clear(); m_Images.clear();
    if (m_Swapchain) vkDestroySwapchainKHR(m_Device->Device, m_Swapchain, nullptr);
    m_Swapchain = VK_NULL_HANDLE;
}
void VulkanContext::Recreate(uint32_t width, uint32_t height) {
    m_Device->SubmitAndWait();
    VulkanCheck(vkDeviceWaitIdle(m_Device->Device), "Wait before swapchain recreation");
    VkSurfaceCapabilitiesKHR caps{};
    VulkanCheck(vkGetPhysicalDeviceSurfaceCapabilitiesKHR(m_Device->PhysicalDevice, m_Surface, &caps), "Query surface capabilities");
    if (!(caps.supportedUsageFlags & VK_IMAGE_USAGE_TRANSFER_DST_BIT)) throw std::runtime_error("Surface does not support transfer destination");
    VkExtent2D extent = caps.currentExtent;
    if (extent.width == UINT32_MAX) extent = {std::clamp(width, caps.minImageExtent.width, caps.maxImageExtent.width), std::clamp(height, caps.minImageExtent.height, caps.maxImageExtent.height)};
    if (!extent.width || !extent.height) return;
    uint32_t count = 0;
    VulkanCheck(vkGetPhysicalDeviceSurfaceFormatsKHR(m_Device->PhysicalDevice, m_Surface, &count, nullptr), "Query surface formats");
    std::vector<VkSurfaceFormatKHR> formats(count);
    VulkanCheck(vkGetPhysicalDeviceSurfaceFormatsKHR(m_Device->PhysicalDevice, m_Surface, &count, formats.data()), "Query surface formats");
    auto chosen = std::find_if(formats.begin(), formats.end(), [&](auto f) {
        return (m_Format != VK_FORMAT_UNDEFINED ? f.format == m_Format : (f.format == VK_FORMAT_B8G8R8A8_UNORM || f.format == VK_FORMAT_R8G8B8A8_UNORM))
            && f.colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR;
    });
    if (chosen == formats.end()) throw std::runtime_error("Surface requires an unsupported color format (RGBA/BGRA UNORM required)");
    m_Format = chosen->format;
    VkPresentModeKHR mode = VK_PRESENT_MODE_FIFO_KHR;
    if (!m_VSync) {
        VulkanCheck(vkGetPhysicalDeviceSurfacePresentModesKHR(m_Device->PhysicalDevice, m_Surface, &count, nullptr), "Query present modes");
        std::vector<VkPresentModeKHR> modes(count);
        VulkanCheck(vkGetPhysicalDeviceSurfacePresentModesKHR(m_Device->PhysicalDevice, m_Surface, &count, modes.data()), "Query present modes");
        for (auto preferred : {VK_PRESENT_MODE_MAILBOX_KHR, VK_PRESENT_MODE_IMMEDIATE_KHR})
            if (std::find(modes.begin(), modes.end(), preferred) != modes.end()) { mode = preferred; break; }
    }
    m_MinImages = std::max(2u, caps.minImageCount);
    if (caps.maxImageCount && m_MinImages > caps.maxImageCount) throw std::runtime_error("Surface cannot provide two images for ImGui");
    VkSwapchainCreateInfoKHR create{VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR};
    create.surface = m_Surface; create.minImageCount = m_MinImages; create.imageFormat = m_Format; create.imageColorSpace = chosen->colorSpace;
    create.imageExtent = extent; create.imageArrayLayers = 1;
    create.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
    create.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE; create.preTransform = caps.currentTransform;
    create.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
    if (!(caps.supportedCompositeAlpha & create.compositeAlpha)) {
        for (auto alpha : {VK_COMPOSITE_ALPHA_PRE_MULTIPLIED_BIT_KHR, VK_COMPOSITE_ALPHA_POST_MULTIPLIED_BIT_KHR, VK_COMPOSITE_ALPHA_INHERIT_BIT_KHR})
            if (caps.supportedCompositeAlpha & alpha) { create.compositeAlpha = alpha; break; }
    }
    create.presentMode = mode; create.clipped = true; create.oldSwapchain = m_Swapchain;
    VkSwapchainKHR replacement{};
    VulkanCheck(vkCreateSwapchainKHR(m_Device->Device, &create, nullptr, &replacement), "Create swapchain");
    DestroySwapchain(); m_Swapchain = replacement;
    m_Width = extent.width; m_Height = extent.height;
    VulkanCheck(vkGetSwapchainImagesKHR(m_Device->Device, m_Swapchain, &count, nullptr), "Get swapchain images");
    m_Images.resize(count);
    VulkanCheck(vkGetSwapchainImagesKHR(m_Device->Device, m_Swapchain, &count, m_Images.data()), "Get swapchain images");
    if (!m_RenderPass) {
        VkAttachmentDescription color{};
        color.format = m_Format; color.samples = VK_SAMPLE_COUNT_1_BIT; color.loadOp = VK_ATTACHMENT_LOAD_OP_LOAD; color.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
        color.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE; color.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
        color.initialLayout = color.finalLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
        VkAttachmentReference reference{0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL};
        VkSubpassDescription subpass{}; subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS; subpass.colorAttachmentCount = 1; subpass.pColorAttachments = &reference;
        VkRenderPassCreateInfo pass{VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO};
        pass.attachmentCount = 1; pass.pAttachments = &color; pass.subpassCount = 1; pass.pSubpasses = &subpass;
        // Keep the ImGui pipeline compatible with the bundled backend's
        // secondary-window render passes, including their dependency contract.
        VkSubpassDependency dependency{};
        dependency.srcSubpass = VK_SUBPASS_EXTERNAL; dependency.dstSubpass = 0;
        dependency.srcStageMask = dependency.dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
        dependency.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
        pass.dependencyCount = 1; pass.pDependencies = &dependency;
        VulkanCheck(vkCreateRenderPass(m_Device->Device, &pass, nullptr, &m_RenderPass), "Create UI render pass");
    }
    m_Views.resize(count); m_Framebuffers.resize(count); m_Ready.resize(count);
    for (uint32_t i = 0; i < count; ++i) {
        VkImageViewCreateInfo view{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
        view.image = m_Images[i]; view.viewType = VK_IMAGE_VIEW_TYPE_2D; view.format = m_Format;
        view.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
        VulkanCheck(vkCreateImageView(m_Device->Device, &view, nullptr, &m_Views[i]), "Create swapchain view");
        VkFramebufferCreateInfo framebuffer{VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO};
        framebuffer.renderPass = m_RenderPass; framebuffer.attachmentCount = 1; framebuffer.pAttachments = &m_Views[i];
        framebuffer.width = m_Width; framebuffer.height = m_Height; framebuffer.layers = 1;
        VulkanCheck(vkCreateFramebuffer(m_Device->Device, &framebuffer, nullptr, &m_Framebuffers[i]), "Create swapchain framebuffer");
        VkSemaphoreCreateInfo semaphore{VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO};
        VulkanCheck(vkCreateSemaphore(m_Device->Device, &semaphore, nullptr, &m_Ready[i]), "Create image present semaphore");
    }
    if (m_ImGui) ImGui_ImplVulkan_SetMinImageCount(m_MinImages);
    m_Rebuild = false;
}
bool VulkanContext::Acquire() {
    if (m_Acquired) return true;
    int width, height; glfwGetFramebufferSize(m_Window, &width, &height);
    if (width <= 0 || height <= 0) return false;
    if (m_Rebuild || uint32_t(width) != m_Width || uint32_t(height) != m_Height) Recreate(width, height);
    if (!m_Swapchain) return false;
    auto result = vkAcquireNextImageKHR(m_Device->Device, m_Swapchain, UINT64_MAX, VK_NULL_HANDLE, m_AcquireFence, &m_Image);
    if (result == VK_ERROR_OUT_OF_DATE_KHR) { m_Rebuild = true; return false; }
    if (result == VK_SUBOPTIMAL_KHR) m_Rebuild = true;
    else VulkanCheck(result, "Acquire image");
    VulkanCheck(vkWaitForFences(m_Device->Device, 1, &m_AcquireFence, true, UINT64_MAX), "Wait for image acquisition");
    VulkanCheck(vkResetFences(m_Device->Device, 1, &m_AcquireFence), "Reset acquire fence");
    m_Acquired = true; return true;
}
void VulkanContext::BlitDefault() {
    m_Device->Barrier();
    VkImageMemoryBarrier barrier{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};
    barrier.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED; barrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    barrier.srcQueueFamilyIndex = barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.image = m_Images[m_Image]; barrier.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
    barrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    auto commands = m_Device->Commands();
    vkCmdPipelineBarrier(commands, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, 0, nullptr, 1, &barrier);
    const auto& source = m_Default->Colors().at(0);
    VkImageBlit blit{};
    blit.srcSubresource = blit.dstSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
    blit.srcOffsets[0] = {0, static_cast<int32_t>(source->Height), 0};
    blit.srcOffsets[1] = {static_cast<int32_t>(source->Width), 0, 1};
    blit.dstOffsets[1] = {static_cast<int32_t>(m_Width), static_cast<int32_t>(m_Height), 1};
    vkCmdBlitImage(commands, source->Image, VK_IMAGE_LAYOUT_GENERAL, m_Images[m_Image], VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &blit, VK_FILTER_NEAREST);
    barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL; barrier.newLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT; barrier.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
    vkCmdPipelineBarrier(commands, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, 0, 0, nullptr, 0, nullptr, 1, &barrier);
}
void VulkanContext::InitImGui() {
    if (!m_RenderPass) { int w, h; glfwGetFramebufferSize(m_Window, &w, &h); Recreate(std::max(w, 1), std::max(h, 1)); }
    ImGui_ImplVulkan_InitInfo info{};
    info.Instance = m_Device->Instance; info.PhysicalDevice = m_Device->PhysicalDevice; info.Device = m_Device->Device;
    info.QueueFamily = m_Device->QueueFamily; info.Queue = m_Device->Queue; info.DescriptorPool = m_Device->ImGuiPool;
    info.MinImageCount = m_MinImages; info.ImageCount = static_cast<uint32_t>(m_Images.size()); info.MSAASamples = VK_SAMPLE_COUNT_1_BIT;
    info.CheckVkResultFn = [](VkResult result) { VulkanCheck(result, "ImGui Vulkan"); };
    if (!ImGui_ImplVulkan_Init(&info, m_RenderPass)) throw std::runtime_error("Cannot initialize ImGui Vulkan renderer");
    m_ImGui = true;
    if (!ImGui_ImplVulkan_CreateFontsTexture(m_Device->Commands())) throw std::runtime_error("Cannot upload ImGui font atlas");
    m_Device->SubmitAndWait();
    ImGui_ImplVulkan_DestroyFontUploadObjects();
}
void VulkanContext::ShutdownImGui() {
    if (!m_ImGui) return;
    m_Device->SubmitAndWait();
    VulkanCheck(vkDeviceWaitIdle(m_Device->Device), "Wait for UI shutdown");
    ImGui_ImplVulkan_Shutdown(); m_ImGui = false;
}
void VulkanContext::RenderImGui(ImDrawData* data) {
    if (!Acquire()) { m_Device->SubmitAndWait(); return; }
    BlitDefault();
    m_Device->Barrier();
    VkRenderPassBeginInfo pass{VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO};
    pass.renderPass = m_RenderPass; pass.framebuffer = m_Framebuffers.at(m_Image); pass.renderArea.extent = {m_Width, m_Height};
    vkCmdBeginRenderPass(m_Device->Commands(), &pass, VK_SUBPASS_CONTENTS_INLINE);
    ImGui_ImplVulkan_RenderDrawData(data, m_Device->Commands());
    vkCmdEndRenderPass(m_Device->Commands());
    m_Rendered = true;
    // Platform windows submit directly to this same queue after this call.
    // Finish the offscreen producers and the main UI before they sample them.
    m_Device->SubmitAndWait();
}
void VulkanContext::SwapBuffers() {
    if (!Acquire()) { m_Device->SubmitAndWait(); return; }
    if (!m_Rendered) BlitDefault();
    VkImageMemoryBarrier barrier{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};
    barrier.oldLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL; barrier.newLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
    barrier.srcQueueFamilyIndex = barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
    barrier.image = m_Images[m_Image]; barrier.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
    vkCmdPipelineBarrier(m_Device->Commands(), VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT,
        0, 0, nullptr, 0, nullptr, 1, &barrier);
    m_Device->SubmitAndWait(m_Ready[m_Image]);
    VkPresentInfoKHR present{VK_STRUCTURE_TYPE_PRESENT_INFO_KHR};
    present.waitSemaphoreCount = 1; present.pWaitSemaphores = &m_Ready[m_Image];
    present.swapchainCount = 1; present.pSwapchains = &m_Swapchain; present.pImageIndices = &m_Image;
    auto result = vkQueuePresentKHR(m_Device->Queue, &present);
    if (result == VK_ERROR_OUT_OF_DATE_KHR || result == VK_SUBOPTIMAL_KHR) m_Rebuild = true;
    else VulkanCheck(result, "Present image");
    m_Acquired = m_Rendered = false;
}
}
