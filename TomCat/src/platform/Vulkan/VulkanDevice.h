#pragma once

#include <vulkan/vulkan.h>
#include <array>
#include <atomic>
#include <functional>
#include <memory>
#include <span>
#include <string>
#include <vector>

struct GLFWwindow;
struct ImDrawData;

namespace TomCat {

class VulkanFramebuffer;
class VulkanShader;
class VulkanTexture2D;

void VulkanCheck(VkResult result, const char* operation);

// The native objects never cross the renderer/backend boundary. Command-local
// allocations are retained until the submission fence signals.
struct VulkanBufferAllocation {
    VkDevice Device = VK_NULL_HANDLE;
    VkBuffer Buffer = VK_NULL_HANDLE;
    VkDeviceMemory Memory = VK_NULL_HANDLE;
    void* Mapped = nullptr;
    VkDeviceSize Size = 0;
    ~VulkanBufferAllocation();
};

struct VulkanImageAllocation {
    VkDevice Device = VK_NULL_HANDLE;
    VkImage Image = VK_NULL_HANDLE;
    VkImageView View = VK_NULL_HANDLE;
    VkDeviceMemory Memory = VK_NULL_HANDLE;
    VkSampler Sampler = VK_NULL_HANDLE;
    VkFormat Format = VK_FORMAT_UNDEFINED;
    VkImageAspectFlags Aspect = VK_IMAGE_ASPECT_COLOR_BIT;
    uint32_t Width = 0, Height = 0, Mips = 1;
    ~VulkanImageAllocation();
};

class VulkanDevice : public std::enable_shared_from_this<VulkanDevice> {
public:
    static std::shared_ptr<VulkanDevice> Current();
    static std::shared_ptr<VulkanDevice> Create(GLFWwindow* window);
    ~VulkanDevice();
    VulkanDevice(const VulkanDevice&) = delete;
    VulkanDevice& operator=(const VulkanDevice&) = delete;

    VkInstance Instance = VK_NULL_HANDLE;
    VkPhysicalDevice PhysicalDevice = VK_NULL_HANDLE;
    VkDevice Device = VK_NULL_HANDLE;
    VkQueue Queue = VK_NULL_HANDLE;
    uint32_t QueueFamily = 0;
    VkPhysicalDeviceProperties Properties{};
    VkPhysicalDeviceFeatures Features{};
    VkDescriptorPool DescriptorPool = VK_NULL_HANDLE;
    VkDescriptorPool ImGuiPool = VK_NULL_HANDLE;
    VulkanFramebuffer* Target = nullptr;
    VulkanFramebuffer* DefaultTarget = nullptr;
    const VulkanShader* Shader = nullptr;
    std::array<const VulkanTexture2D*, 32> Textures{};
    std::array<std::vector<uint8_t>, 16> Uniforms;
    bool DepthTest = true;
    float LineWidth = 1.0f;
    std::array<float, 4> ClearColor{0, 0, 0, 1};
    VkViewport Viewport{0, 0, 1, 1, 0, 1};

    VkCommandBuffer Commands();
    void PrepareDraw() { if (m_Retained.size() > 512) SubmitAndWait(); }
    void Barrier();
    void SubmitAndWait(VkSemaphore signal = VK_NULL_HANDLE);
    void DrainForDestruction() noexcept;
    void Keep(std::shared_ptr<void> object);
    std::shared_ptr<VulkanBufferAllocation> Buffer(VkDeviceSize size, VkBufferUsageFlags usage, const void* data = nullptr);
    std::shared_ptr<VulkanImageAllocation> Image(uint32_t width, uint32_t height, VkFormat format,
        VkImageUsageFlags usage, VkImageAspectFlags aspect = VK_IMAGE_ASPECT_COLOR_BIT, uint32_t mips = 1);
    VkDescriptorSet AllocateDescriptor(VkDescriptorSetLayout layout);
    uint32_t MemoryType(uint32_t bits, VkMemoryPropertyFlags flags) const;
    VkFormat DepthFormat() const;
    void BeginProfile(uint64_t frame);
    void EndProfile();
    bool SupportsProfiling() const { return m_TimestampBits != 0; }
    std::atomic<uint32_t> ValidationErrors{0};

private:
    VulkanDevice() = default;
    void Initialize(GLFWwindow* window);
    VkCommandPool m_CommandPool = VK_NULL_HANDLE;
    VkCommandBuffer m_CommandBuffer = VK_NULL_HANDLE;
    VkFence m_Fence = VK_NULL_HANDLE;
    bool m_Recording = false;
    std::vector<std::shared_ptr<void>> m_Retained;
    VkQueryPool m_QueryPool = VK_NULL_HANDLE;
    uint32_t m_TimestampBits = 0;
    uint64_t m_ProfileFrame = 0, m_PendingProfileFrame = 0;
    double m_PendingProfileTime = 0;
    bool m_ProfileEnded = false;
    VkDebugUtilsMessengerEXT m_DebugMessenger = VK_NULL_HANDLE;
};

}
