#pragma once
#include "TomCat/Renderer/GraphicsContext.h"
#include "VulkanResources.h"

namespace TomCat {
class VulkanContext final : public GraphicsContext {
public:
    explicit VulkanContext(GLFWwindow* window) : m_Window(window) {}
    ~VulkanContext() override;
    void Init() override;
    void SwapBuffers() override;
    void SetVSync(bool enabled) override { if (m_VSync != enabled) { m_VSync = enabled; m_Rebuild = true; } }
    void InitImGui();
    void RebuildImGuiFonts();
    void ShutdownImGui();
    void RenderImGui(ImDrawData* data);
    static VulkanContext& Current();
private:
    bool Acquire();
    void Recreate(uint32_t width, uint32_t height);
    void DestroySwapchain();
    void BlitDefault();
    uint64_t SubmitFrameCommands(VkSemaphore signal = VK_NULL_HANDLE);
    GLFWwindow* m_Window;
    std::shared_ptr<VulkanDevice> m_Device;
    std::unique_ptr<VulkanFramebuffer> m_Default;
    VkSurfaceKHR m_Surface = VK_NULL_HANDLE;
    VkSwapchainKHR m_Swapchain = VK_NULL_HANDLE;
    VkFormat m_Format = VK_FORMAT_UNDEFINED;
    VkRenderPass m_RenderPass = VK_NULL_HANDLE;
    // Bound CPU lead before ImGui reuses its main-window vertex/index ring.
    // Presentation semaphores below belong to swapchain images, not frames.
    struct FrameSync {
        VkSemaphore Acquired = VK_NULL_HANDLE;
        uint64_t Submission = 0;
    };
    std::array<FrameSync, 2> m_Frames{};
    uint32_t m_Frame = 0;
    bool m_AcquireWaitPending = false, m_FrameStarted = false;
    std::vector<VkImage> m_Images;
    std::vector<VkImageView> m_Views;
    std::vector<VkFramebuffer> m_Framebuffers;
    std::vector<VkSemaphore> m_Ready;
    uint32_t m_Width = 0, m_Height = 0, m_Image = 0, m_MinImages = 2;
    bool m_Acquired = false, m_Rendered = false, m_Rebuild = true, m_VSync = true, m_ImGui = false;
};
}
