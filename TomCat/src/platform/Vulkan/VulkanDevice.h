#pragma once

#include <vulkan/vulkan.h>
#include <array>
#include <atomic>
#include <functional>
#include <memory>
#include <span>
#include <string>
#include <unordered_map>
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

// A snapshot in the current submission's upload arena. The slot owns its memory;
// callers may use the slice until Submit(), without retaining an allocation.
struct VulkanBufferSlice {
    VkBuffer Buffer = VK_NULL_HANDLE;
    VkDeviceSize Offset = 0, Size = 0;
    void* Mapped = nullptr;
};

struct VulkanSubmissionStats {
    uint64_t SubmissionCount = 0, ExplicitWaitCount = 0, SlotWaitCount = 0;
    uint64_t UploadPageAllocations = 0, UploadBytes = 0, PeakInFlight = 0;
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
    static constexpr uint32_t FramesInFlight = 3;
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
    void PrepareDraw();
    void Barrier();
    uint64_t Submit(VkSemaphore signal = VK_NULL_HANDLE, VkSemaphore wait = VK_NULL_HANDLE,
        VkPipelineStageFlags waitStage = VK_PIPELINE_STAGE_ALL_COMMANDS_BIT);
    void WaitForSubmission(uint64_t serial);
    void SubmitAndWait(VkSemaphore signal = VK_NULL_HANDLE);
    void DrainForDestruction() noexcept;
    void Keep(std::shared_ptr<void> object);
    VulkanBufferSlice Upload(VkDeviceSize size, VkBufferUsageFlags usage, const void* data, VkDeviceSize alignment = 1);
    std::shared_ptr<VulkanBufferAllocation> Buffer(VkDeviceSize size, VkBufferUsageFlags usage, const void* data = nullptr);
    std::shared_ptr<VulkanImageAllocation> Image(uint32_t width, uint32_t height, VkFormat format,
        VkImageUsageFlags usage, VkImageAspectFlags aspect = VK_IMAGE_ASPECT_COLOR_BIT, uint32_t mips = 1);
    VkDescriptorSet AllocateDescriptor(VkDescriptorSetLayout layout);
    uint32_t MemoryType(uint32_t bits, VkMemoryPropertyFlags flags) const;
    VkFormat DepthFormat() const;
    void BeginProfile(uint64_t frame);
    void EndProfile();
    bool SupportsProfiling() const { return m_TimestampBits != 0; }
    const VulkanSubmissionStats& SubmissionStats() const { return m_Stats; }
    std::atomic<uint32_t> ValidationErrors{0};

private:
    VulkanDevice() = default;
    void Initialize(GLFWwindow* window);
    struct UploadPage {
        std::shared_ptr<VulkanBufferAllocation> Allocation;
        VkDeviceSize Used = 0;
        bool Cacheable = true;
    };
    struct SubmissionSlot {
        VkCommandPool CommandPool = VK_NULL_HANDLE;
        VkCommandBuffer CommandBuffer = VK_NULL_HANDLE;
        VkFence Fence = VK_NULL_HANDLE;
        std::vector<VkDescriptorPool> DescriptorPools;
        size_t DescriptorPoolIndex = 0;
        VkQueryPool QueryPool = VK_NULL_HANDLE;
        bool Recording = false, InFlight = false, ProfileEnded = false;
        uint64_t Serial = 0, ProfileFrame = 0;
        uint32_t DescriptorSets = 0;
        std::vector<std::shared_ptr<void>> Retained;
        std::vector<UploadPage> UploadPages;
        VkDeviceSize UploadUsed = 0;
    };
    struct ProfileResult {
        double Milliseconds = 0;
        uint32_t PendingSegments = 0;
        bool Ended = false;
    };
    SubmissionSlot& AcquireSlot();
    VkDescriptorPool CreateDrawDescriptorPool();
    bool CompleteSlot(SubmissionSlot& slot, bool wait);
    void CollectCompleted();
    void StartProfileSegment(SubmissionSlot& slot);
    void EndProfileSegment(SubmissionSlot& slot);
    void PublishProfiles();
    std::array<SubmissionSlot, FramesInFlight> m_Slots;
    uint32_t m_CurrentSlot = 0;
    uint64_t m_NextSerial = 0, m_CompletedSerial = 0;
    VulkanSubmissionStats m_Stats;
    uint32_t m_TimestampBits = 0;
    uint64_t m_ProfileFrame = 0;
    std::unordered_map<uint64_t, ProfileResult> m_ProfileResults;
    bool m_Draining = false;
    VkDebugUtilsMessengerEXT m_DebugMessenger = VK_NULL_HANDLE;
};

}
