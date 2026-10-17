#include "tcpch.h"
#include "VulkanDevice.h"
#include "VulkanSupport.h"
#include <GLFW/glfw3.h>
#include <stdexcept>
#include <cstring>
#include <limits>


namespace TomCat {
namespace {
std::weak_ptr<VulkanDevice> s_Device;
constexpr VkBufferUsageFlags UploadUsage = VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_INDEX_BUFFER_BIT
    | VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
constexpr VkDeviceSize UploadPageSize = 1024 * 1024;
constexpr VkDeviceSize SubmissionUploadBudget = 8 * UploadPageSize;
VKAPI_ATTR VkBool32 VKAPI_CALL ValidationMessage(VkDebugUtilsMessageSeverityFlagBitsEXT severity,
    VkDebugUtilsMessageTypeFlagsEXT, const VkDebugUtilsMessengerCallbackDataEXT* message, void* user) {
    if (severity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT)
        static_cast<VulkanDevice*>(user)->ValidationErrors.fetch_add(1);
    std::fprintf(stderr, "Vulkan validation %s: %s\n",
        severity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT ? "ERROR" : "warning", message->pMessage);
    return VK_FALSE;
}
}

void VulkanCheck(VkResult result, const char* operation) {
    if (result != VK_SUCCESS)
        throw std::runtime_error(std::string(operation) + " failed (VkResult " + std::to_string(result) + ")");
}

VulkanBufferAllocation::~VulkanBufferAllocation() {
    if (Mapped) vkUnmapMemory(Device, Memory);
    if (Buffer) vkDestroyBuffer(Device, Buffer, nullptr);
    if (Memory) vkFreeMemory(Device, Memory, nullptr);
    if (Size) ProfileResourceTracker::Get().Buffer(-1, -static_cast<int64_t>(Size));
}
VulkanImageAllocation::~VulkanImageAllocation() {
    if (Sampler) vkDestroySampler(Device, Sampler, nullptr);
    if (View) vkDestroyImageView(Device, View, nullptr);
    if (Image) vkDestroyImage(Device, Image, nullptr);
    if (Memory) vkFreeMemory(Device, Memory, nullptr);
}
std::shared_ptr<VulkanDevice> VulkanDevice::Current() {
    auto device = s_Device.lock();
    if (!device) throw std::logic_error("Vulkan device has not been initialized");
    return device;
}
std::shared_ptr<VulkanDevice> VulkanDevice::Create(GLFWwindow* window) {
    if (!s_Device.expired()) throw std::logic_error("Only one Vulkan device may be active");
    auto device = std::shared_ptr<VulkanDevice>(new VulkanDevice);
    device->Initialize(window);
    s_Device = device;
    return device;
}
void VulkanDevice::Initialize(GLFWwindow* window) {
    if (!glfwVulkanSupported()) throw VulkanUnsupportedError("Vulkan loader/driver is unavailable");
    auto enumerateVersion = reinterpret_cast<PFN_vkEnumerateInstanceVersion>(glfwGetInstanceProcAddress(VK_NULL_HANDLE, "vkEnumerateInstanceVersion"));
    if (!enumerateVersion) throw VulkanUnsupportedError("Vulkan loader does not support Vulkan 1.2");
    uint32_t version = VK_API_VERSION_1_0;
    VulkanCheck(enumerateVersion(&version), "Query Vulkan loader version");
    if (version < RequiredVulkanVersion) throw VulkanUnsupportedError("Vulkan loader does not support Vulkan 1.2");
    uint32_t count = 0;
    const char** required = glfwGetRequiredInstanceExtensions(&count);
    if (!required || !count) throw VulkanUnsupportedError("GLFW cannot provide a Vulkan surface");
    VkApplicationInfo app{VK_STRUCTURE_TYPE_APPLICATION_INFO};
    app.pApplicationName = "TomCat";
    app.apiVersion = RequiredVulkanVersion;
    VkInstanceCreateInfo info{VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO};
    info.pApplicationInfo = &app;
    info.enabledExtensionCount = count;
    info.ppEnabledExtensionNames = required;
    std::vector<const char*> extensions(required, required + count);
    VkDebugUtilsMessengerCreateInfoEXT debug{VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT};
    debug.messageSeverity = VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT;
    debug.messageType = VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT;
    debug.pfnUserCallback = ValidationMessage; debug.pUserData = this;
    VkValidationFeatureEnableEXT synchronization = VK_VALIDATION_FEATURE_ENABLE_SYNCHRONIZATION_VALIDATION_EXT;
    VkValidationFeaturesEXT validationFeatures{VK_STRUCTURE_TYPE_VALIDATION_FEATURES_EXT};
    validationFeatures.enabledValidationFeatureCount = 1; validationFeatures.pEnabledValidationFeatures = &synchronization;
    // Validation is opt-in so a shipping installation does not require the SDK.
    const char* validation = "VK_LAYER_KHRONOS_validation";
    if (std::getenv("TC_VULKAN_VALIDATION")) {
        uint32_t layerCount = 0;
        VulkanCheck(vkEnumerateInstanceLayerProperties(&layerCount, nullptr), "Enumerate layers");
        std::vector<VkLayerProperties> layers(layerCount);
        VulkanCheck(vkEnumerateInstanceLayerProperties(&layerCount, layers.data()), "Enumerate layers");
        if (std::none_of(layers.begin(), layers.end(), [&](auto& l) { return std::strcmp(l.layerName, validation) == 0; }))
            throw std::runtime_error("TC_VULKAN_VALIDATION requested, but validation layer is not installed");
        info.enabledLayerCount = 1;
        info.ppEnabledLayerNames = &validation;
        extensions.push_back(VK_EXT_DEBUG_UTILS_EXTENSION_NAME);
        info.enabledExtensionCount = static_cast<uint32_t>(extensions.size());
        info.ppEnabledExtensionNames = extensions.data();
        debug.pNext = &validationFeatures; info.pNext = &debug;
    }
    VkInstance instance = VK_NULL_HANDLE;
    const auto instanceResult = vkCreateInstance(&info, nullptr, &instance);
    // An explicitly requested validation/debug extension must not silently turn
    // validation off by selecting another backend.
    if (info.enabledLayerCount && instanceResult == VK_ERROR_EXTENSION_NOT_PRESENT)
        VulkanCheck(instanceResult, "Create validation instance");
    VulkanCheckSupport(instanceResult, "Create instance");
    Instance = instance;
    if (info.enabledLayerCount) {
        debug.pNext = nullptr;
        auto createDebug = reinterpret_cast<PFN_vkCreateDebugUtilsMessengerEXT>(vkGetInstanceProcAddr(Instance, "vkCreateDebugUtilsMessengerEXT"));
        if (!createDebug) throw std::runtime_error("Vulkan debug utils unavailable");
        VulkanCheck(createDebug(Instance, &debug, nullptr, &m_DebugMessenger), "Create validation messenger");
    }
    VkSurfaceKHR surface = VK_NULL_HANDLE;
    VulkanCheckSupport(glfwCreateWindowSurface(Instance, window, nullptr, &surface), "Create probe surface");
    try {
        VulkanCheck(vkEnumeratePhysicalDevices(Instance, &count, nullptr), "Enumerate physical devices");
        std::vector<VkPhysicalDevice> devices(count);
        VulkanCheck(vkEnumeratePhysicalDevices(Instance, &count, devices.data()), "Enumerate physical devices");
        int bestScore = -1;
        const char* requestedDevice = std::getenv("TC_VULKAN_DEVICE");
        for (auto candidate : devices) {
            VkPhysicalDeviceProperties properties{};
            VkPhysicalDeviceFeatures features{};
            vkGetPhysicalDeviceProperties(candidate, &properties);
            vkGetPhysicalDeviceFeatures(candidate, &features);
            if (!VulkanDeviceMatchesRequest(properties, requestedDevice)) continue;
            const int score = properties.deviceType == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU ? 2 : 1;
            if (score <= bestScore) continue;
            if (VulkanDeviceRequirementFailure(properties, features)) continue;
            uint32_t extensionCount = 0;
            VulkanCheck(vkEnumerateDeviceExtensionProperties(candidate, nullptr, &extensionCount, nullptr), "Enumerate device extensions");
            std::vector<VkExtensionProperties> extensions(extensionCount);
            VulkanCheck(vkEnumerateDeviceExtensionProperties(candidate, nullptr, &extensionCount, extensions.data()), "Enumerate device extensions");
            if (!VulkanHasSwapchainExtension(extensions)) continue;
            uint32_t familyCount = 0;
            vkGetPhysicalDeviceQueueFamilyProperties(candidate, &familyCount, nullptr);
            std::vector<VkQueueFamilyProperties> families(familyCount);
            vkGetPhysicalDeviceQueueFamilyProperties(candidate, &familyCount, families.data());
            uint32_t presentFamily = UINT32_MAX;
            for (uint32_t i = 0; i < familyCount; ++i) {
                VkBool32 present = false;
                VulkanCheck(vkGetPhysicalDeviceSurfaceSupportKHR(candidate, i, surface, &present), "Query present support");
                if (!families[i].queueCount || !(families[i].queueFlags & VK_QUEUE_GRAPHICS_BIT) || !present) continue;
                presentFamily = i;
                break;
            }
            if (presentFamily == UINT32_MAX) continue;
            VkSurfaceCapabilitiesKHR capabilities{};
            VulkanCheck(vkGetPhysicalDeviceSurfaceCapabilitiesKHR(candidate, surface, &capabilities), "Query device surface capabilities");
            uint32_t formatCount = 0;
            VulkanCheck(vkGetPhysicalDeviceSurfaceFormatsKHR(candidate, surface, &formatCount, nullptr), "Query device surface formats");
            std::vector<VkSurfaceFormatKHR> formats(formatCount);
            VulkanCheck(vkGetPhysicalDeviceSurfaceFormatsKHR(candidate, surface, &formatCount, formats.data()), "Query device surface formats");
            if (VulkanSurfaceRequirementFailure(capabilities, formats)) continue;
            PhysicalDevice = candidate; QueueFamily = presentFamily; Properties = properties; Features = features;
            m_TimestampBits = families[presentFamily].timestampValidBits;
            bestScore = score;
        }
    } catch (...) { vkDestroySurfaceKHR(Instance, surface, nullptr); throw; }
    vkDestroySurfaceKHR(Instance, surface, nullptr);
    if (!PhysicalDevice) throw VulkanUnsupportedError("Vulkan requires a 1.2 graphics/present device with independent blend, 32 texture slots and a compatible window surface");
    float priority = 1;
    VkDeviceQueueCreateInfo queue{VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO};
    queue.queueFamilyIndex = QueueFamily; queue.queueCount = 1; queue.pQueuePriorities = &priority;
    VkPhysicalDeviceFeatures enabled{};
    enabled.independentBlend = true;
    enabled.wideLines = Features.wideLines;
    const char* extension = VK_KHR_SWAPCHAIN_EXTENSION_NAME;
    VkDeviceCreateInfo create{VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO};
    create.queueCreateInfoCount = 1; create.pQueueCreateInfos = &queue;
    create.enabledExtensionCount = 1; create.ppEnabledExtensionNames = &extension;
    create.pEnabledFeatures = &enabled;
    VkDevice device = VK_NULL_HANDLE;
    VulkanCheckSupport(vkCreateDevice(PhysicalDevice, &create, nullptr, &device), "Create device");
    Device = device;
    vkGetDeviceQueue(Device, QueueFamily, 0, &Queue);
    VkCommandPoolCreateInfo pool{VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO};
    pool.flags = VK_COMMAND_POOL_CREATE_TRANSIENT_BIT; pool.queueFamilyIndex = QueueFamily;
    VkCommandBufferAllocateInfo allocate{VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};
    allocate.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY; allocate.commandBufferCount = 1;
    VkFenceCreateInfo fence{VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};
    VkDescriptorPoolSize sizes[] = {{VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 4096}, {VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 32768}};
    VkDescriptorPoolCreateInfo descriptors{VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO};
    descriptors.maxSets = 4096; descriptors.poolSizeCount = 2; descriptors.pPoolSizes = sizes;
    for (auto& slot : m_Slots) {
        VulkanCheck(vkCreateCommandPool(Device, &pool, nullptr, &slot.CommandPool), "Create command pool");
        allocate.commandPool = slot.CommandPool;
        VulkanCheck(vkAllocateCommandBuffers(Device, &allocate, &slot.CommandBuffer), "Allocate command buffer");
        VulkanCheck(vkCreateFence(Device, &fence, nullptr, &slot.Fence), "Create submission fence");
        slot.DescriptorPools.push_back(CreateDrawDescriptorPool());
        if (SupportsProfiling()) {
            VkQueryPoolCreateInfo query{VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO};
            query.queryType = VK_QUERY_TYPE_TIMESTAMP; query.queryCount = 2;
            VulkanCheck(vkCreateQueryPool(Device, &query, nullptr, &slot.QueryPool), "Create timestamps");
        }
    }
    descriptors.flags = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT;
    VulkanCheck(vkCreateDescriptorPool(Device, &descriptors, nullptr, &ImGuiPool), "Create UI descriptor pool");
    TC_Core_Info("Vulkan device: {0}", Properties.deviceName);
}
VulkanDevice::~VulkanDevice() {
    if (Device) vkDeviceWaitIdle(Device);
    for (auto& slot : m_Slots) {
        slot.Retained.clear();
        slot.UploadPages.clear();
        if (slot.QueryPool) vkDestroyQueryPool(Device, slot.QueryPool, nullptr);
        for (auto descriptors : slot.DescriptorPools) vkDestroyDescriptorPool(Device, descriptors, nullptr);
        if (slot.Fence) vkDestroyFence(Device, slot.Fence, nullptr);
        if (slot.CommandPool) vkDestroyCommandPool(Device, slot.CommandPool, nullptr);
    }
    if (ImGuiPool) vkDestroyDescriptorPool(Device, ImGuiPool, nullptr);
    if (Device) vkDestroyDevice(Device, nullptr);
    if (m_DebugMessenger) {
        auto destroyDebug = reinterpret_cast<PFN_vkDestroyDebugUtilsMessengerEXT>(vkGetInstanceProcAddr(Instance, "vkDestroyDebugUtilsMessengerEXT"));
        destroyDebug(Instance, m_DebugMessenger, nullptr);
    }
    if (Instance) vkDestroyInstance(Instance, nullptr);
}
VkCommandBuffer VulkanDevice::Commands() {
    auto& slot = AcquireSlot();
    if (!slot.Recording) {
        VkCommandBufferBeginInfo begin{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
        begin.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
        VulkanCheck(vkBeginCommandBuffer(slot.CommandBuffer, &begin), "Begin commands");
        slot.Recording = true;
        StartProfileSegment(slot);
    }
    return slot.CommandBuffer;
}
VulkanDevice::SubmissionSlot& VulkanDevice::AcquireSlot() {
    auto& slot = m_Slots[m_CurrentSlot];
    if (slot.InFlight) CompleteSlot(slot, true);
    return slot;
}
bool VulkanDevice::CompleteSlot(SubmissionSlot& slot, bool wait) {
    if (!slot.InFlight) return true;
    const auto status = vkGetFenceStatus(Device, slot.Fence);
    if (status == VK_NOT_READY) {
        if (!wait) return false;
        ++m_Stats.SlotWaitCount;
        VulkanCheck(vkWaitForFences(Device, 1, &slot.Fence, true, UINT64_MAX), "Wait for submission slot");
    } else VulkanCheck(status, "Query submission fence");
    if (slot.ProfileFrame) {
        uint64_t timestamps[2]{};
        // Fence completion makes both timestamp results available; never wait on
        // query retrieval while the CPU could be preparing another submission.
        VulkanCheck(vkGetQueryPoolResults(Device, slot.QueryPool, 0, 2, sizeof(timestamps), timestamps,
            sizeof(uint64_t), VK_QUERY_RESULT_64_BIT), "Read timestamps");
        const uint64_t mask = m_TimestampBits == 64 ? UINT64_MAX : ((uint64_t(1) << m_TimestampBits) - 1);
        auto& result = m_ProfileResults.at(slot.ProfileFrame);
        result.Milliseconds += double((timestamps[1] - timestamps[0]) & mask) * Properties.limits.timestampPeriod / 1000000.0;
        --result.PendingSegments;
    }
    VulkanCheck(vkResetFences(Device, 1, &slot.Fence), "Reset submission fence");
    VulkanCheck(vkResetCommandPool(Device, slot.CommandPool, 0), "Reset command pool");
    for (auto descriptors : slot.DescriptorPools)
        VulkanCheck(vkResetDescriptorPool(Device, descriptors, 0), "Reset draw descriptors");
    while (slot.DescriptorPools.size() > 2) {
        vkDestroyDescriptorPool(Device, slot.DescriptorPools.back(), nullptr);
        slot.DescriptorPools.pop_back();
    }
    slot.DescriptorPoolIndex = 0;
    slot.InFlight = false;
    m_CompletedSerial = (std::max)(m_CompletedSerial, slot.Serial);
    slot.ProfileFrame = 0; slot.ProfileEnded = false; slot.DescriptorSets = 0;
    slot.UploadUsed = 0;
    // Keep a bounded warm cache. One-off texture uploads must not pin their
    // oversized staging buffers, while full Renderer2D batches reuse geometry
    // pages larger than the standard page. Prefer large reusable allocations.
    std::stable_sort(slot.UploadPages.begin(), slot.UploadPages.end(), [](const auto& a, const auto& b) {
        return a.Allocation->Size > b.Allocation->Size;
    });
    VkDeviceSize cachedBytes = 0;
    std::erase_if(slot.UploadPages, [&](const auto& page) {
        if (!page.Cacheable || page.Allocation->Size > SubmissionUploadBudget - cachedBytes) return true;
        cachedBytes += page.Allocation->Size;
        return false;
    });
    for (auto& page : slot.UploadPages) page.Used = 0;
    slot.Retained.clear();
    PublishProfiles();
    return true;
}
void VulkanDevice::CollectCompleted() {
    for (auto& slot : m_Slots) CompleteSlot(slot, false);
    PublishProfiles();
}
void VulkanDevice::PrepareDraw() {
    auto& slot = AcquireSlot();
    // Roll over only before a draw: all slices and descriptor sets assembled by
    // that draw must belong to the same slot until its commands are submitted.
    if (slot.Retained.size() > 512 || slot.DescriptorSets >= 1024 || slot.UploadUsed >= SubmissionUploadBudget)
        Submit();
}
void VulkanDevice::Barrier() {
    VkMemoryBarrier barrier{VK_STRUCTURE_TYPE_MEMORY_BARRIER};
    barrier.srcAccessMask = VK_ACCESS_MEMORY_WRITE_BIT;
    barrier.dstAccessMask = VK_ACCESS_MEMORY_READ_BIT | VK_ACCESS_MEMORY_WRITE_BIT;
    vkCmdPipelineBarrier(Commands(), VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,
        0, 1, &barrier, 0, nullptr, 0, nullptr);
}
uint64_t VulkanDevice::Submit(VkSemaphore signal, VkSemaphore wait, VkPipelineStageFlags waitStage) {
    if (signal || wait) Commands();
    auto& slot = m_Slots[m_CurrentSlot];
    if (!slot.Recording) return 0;
    EndProfileSegment(slot);
    VulkanCheck(vkEndCommandBuffer(slot.CommandBuffer), "End commands");
    VkSubmitInfo submit{VK_STRUCTURE_TYPE_SUBMIT_INFO};
    submit.commandBufferCount = 1; submit.pCommandBuffers = &slot.CommandBuffer;
    if (signal) { submit.signalSemaphoreCount = 1; submit.pSignalSemaphores = &signal; }
    if (wait) {
        submit.waitSemaphoreCount = 1; submit.pWaitSemaphores = &wait; submit.pWaitDstStageMask = &waitStage;
    }
    VulkanCheck(vkQueueSubmit(Queue, 1, &submit, slot.Fence), "Submit commands");
    slot.Recording = false; slot.InFlight = true; slot.Serial = ++m_NextSerial;
    ++m_Stats.SubmissionCount;
    const auto inFlight = static_cast<uint64_t>(std::count_if(m_Slots.begin(), m_Slots.end(), [](const auto& s) { return s.InFlight; }));
    m_Stats.PeakInFlight = (std::max)(m_Stats.PeakInFlight, inFlight);
    m_CurrentSlot = (m_CurrentSlot + 1) % FramesInFlight;
    // Acquiring the next slot is deliberately deferred until recording resumes.
    return slot.Serial;
}
void VulkanDevice::WaitForSubmission(uint64_t serial) {
    if (!serial || serial <= m_CompletedSerial) return;
    for (auto& slot : m_Slots) {
        if (slot.InFlight && slot.Serial == serial) {
            CompleteSlot(slot, true);
            CollectCompleted();
            return;
        }
    }
    throw std::invalid_argument("Unknown Vulkan submission serial");
}
void VulkanDevice::SubmitAndWait(VkSemaphore signal) {
    Submit(signal);
    ++m_Stats.ExplicitWaitCount;
    // ImGui platform windows submit directly to this queue. A queue drain also
    // covers them when there is no engine command buffer currently recording.
    VulkanCheck(vkQueueWaitIdle(Queue), "Wait for graphics queue");
    CollectCompleted();
}
void VulkanDevice::Keep(std::shared_ptr<void> object) {
    Commands();
    m_Slots[m_CurrentSlot].Retained.push_back(std::move(object));
}
void VulkanDevice::DrainForDestruction() noexcept {
    if (m_Draining) return;
    m_Draining = true;
    try { SubmitAndWait(); }
    catch (const std::exception& e) { std::fprintf(stderr, "Vulkan cleanup: %s\n", e.what()); }
    if (Device) vkDeviceWaitIdle(Device);
    m_Draining = false;
}
VulkanBufferSlice VulkanDevice::Upload(VkDeviceSize size, VkBufferUsageFlags usage, const void* data, VkDeviceSize alignment) {
    if (!size || !data) throw std::invalid_argument("Empty Vulkan upload");
    if (size > (std::numeric_limits<size_t>::max)() || size > VkDeviceSize((std::numeric_limits<int64_t>::max)()))
        throw std::length_error("Vulkan upload is too large");
    if (!usage || (usage & ~UploadUsage)) throw std::invalid_argument("Unsupported Vulkan upload usage");
    if ((usage & VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT) && size > Properties.limits.maxUniformBufferRange)
        throw std::length_error("Vulkan upload exceeds the uniform buffer range");
    if (!alignment || (alignment & (alignment - 1))) throw std::invalid_argument("Upload alignment must be a power of two");
    alignment = (std::max)(alignment, VkDeviceSize(4));
    if (usage & VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT)
        alignment = (std::max)(alignment, Properties.limits.minUniformBufferOffsetAlignment);
    Commands();
    auto& slot = m_Slots[m_CurrentSlot];
    if (size > (std::numeric_limits<VkDeviceSize>::max)() - slot.UploadUsed)
        throw std::length_error("Vulkan submission upload size overflow");
    const bool standardPage = size <= UploadPageSize;
    const bool geometry = (usage & (VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_INDEX_BUFFER_BIT | VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT)) != 0;
    const bool cacheable = standardPage || (geometry && size <= SubmissionUploadBudget);
    UploadPage* selected = nullptr;
    VkDeviceSize selectedOffset = 0;
    for (auto& page : slot.UploadPages) {
        // Small allocations must not consume the beginning of a cached large
        // geometry page before the frame's full batch arrives. Temporary large
        // texture staging also stays separate from the warm geometry cache.
        if ((page.Allocation->Size <= UploadPageSize) != standardPage || page.Cacheable != cacheable) continue;
        const auto remainder = page.Used % alignment;
        const auto padding = remainder ? alignment - remainder : 0;
        if (padding <= page.Allocation->Size - page.Used && size <= page.Allocation->Size - page.Used - padding) {
            if (!selected || page.Allocation->Size - page.Used < selected->Allocation->Size - selected->Used) {
                selected = &page;
                selectedOffset = page.Used + padding;
            }
        }
    }
    if (selected) {
        auto* destination = static_cast<uint8_t*>(selected->Allocation->Mapped) + selectedOffset;
        std::memcpy(destination, data, static_cast<size_t>(size));
        selected->Used = selectedOffset + size;
        slot.UploadUsed += size; m_Stats.UploadBytes += size;
        return {selected->Allocation->Buffer, selectedOffset, size, destination};
    }
    const auto capacity = (std::max)(UploadPageSize, size);
    slot.UploadPages.push_back({Buffer(capacity, UploadUsage), 0, cacheable});
    ++m_Stats.UploadPageAllocations;
    auto& page = slot.UploadPages.back();
    std::memcpy(page.Allocation->Mapped, data, static_cast<size_t>(size));
    page.Used = size; slot.UploadUsed += size; m_Stats.UploadBytes += size;
    return {page.Allocation->Buffer, 0, size, page.Allocation->Mapped};
}
uint32_t VulkanDevice::MemoryType(uint32_t bits, VkMemoryPropertyFlags flags) const {
    VkPhysicalDeviceMemoryProperties memory{};
    vkGetPhysicalDeviceMemoryProperties(PhysicalDevice, &memory);
    for (uint32_t i = 0; i < memory.memoryTypeCount; ++i)
        if ((bits & (1u << i)) && (memory.memoryTypes[i].propertyFlags & flags) == flags) return i;
    throw std::runtime_error("No compatible Vulkan memory type");
}
std::shared_ptr<VulkanBufferAllocation> VulkanDevice::Buffer(VkDeviceSize size, VkBufferUsageFlags usage, const void* data) {
    if (!size) throw std::invalid_argument("Zero-sized GPU buffer");
    auto buffer = std::make_shared<VulkanBufferAllocation>(); buffer->Device = Device;
    VkBufferCreateInfo create{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
    create.size = size; create.usage = usage; create.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    VulkanCheck(vkCreateBuffer(Device, &create, nullptr, &buffer->Buffer), "Create buffer");
    VkMemoryRequirements requirements{}; vkGetBufferMemoryRequirements(Device, buffer->Buffer, &requirements);
    VkMemoryAllocateInfo memory{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
    memory.allocationSize = requirements.size;
    memory.memoryTypeIndex = MemoryType(requirements.memoryTypeBits, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
    VulkanCheck(vkAllocateMemory(Device, &memory, nullptr, &buffer->Memory), "Allocate buffer memory");
    VulkanCheck(vkBindBufferMemory(Device, buffer->Buffer, buffer->Memory, 0), "Bind buffer memory");
    VulkanCheck(vkMapMemory(Device, buffer->Memory, 0, size, 0, &buffer->Mapped), "Map buffer");
    if (data) std::memcpy(buffer->Mapped, data, static_cast<size_t>(size));
    buffer->Size = size;
    ProfileResourceTracker::Get().Buffer(1, static_cast<int64_t>(size));
    return buffer;
}
std::shared_ptr<VulkanImageAllocation> VulkanDevice::Image(uint32_t width, uint32_t height, VkFormat format,
    VkImageUsageFlags usage, VkImageAspectFlags aspect, uint32_t mips, const TextureSampling& sampling) {
    if (!width || !height || width > Properties.limits.maxImageDimension2D || height > Properties.limits.maxImageDimension2D)
        throw std::invalid_argument("Invalid Vulkan image dimensions");
    auto image = std::make_shared<VulkanImageAllocation>();
    image->Device = Device; image->Width = width; image->Height = height; image->Format = format; image->Aspect = aspect; image->Mips = mips;
    VkImageCreateInfo create{VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};
    create.imageType = VK_IMAGE_TYPE_2D; create.format = format; create.extent = {width, height, 1};
    create.mipLevels = mips; create.arrayLayers = 1; create.samples = VK_SAMPLE_COUNT_1_BIT;
    create.tiling = VK_IMAGE_TILING_OPTIMAL; create.usage = usage; create.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    VulkanCheck(vkCreateImage(Device, &create, nullptr, &image->Image), "Create image");
    VkMemoryRequirements requirements{}; vkGetImageMemoryRequirements(Device, image->Image, &requirements);
    VkMemoryAllocateInfo memory{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
    memory.allocationSize = requirements.size; memory.memoryTypeIndex = MemoryType(requirements.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    VulkanCheck(vkAllocateMemory(Device, &memory, nullptr, &image->Memory), "Allocate image memory");
    VulkanCheck(vkBindImageMemory(Device, image->Image, image->Memory, 0), "Bind image memory");
    VkImageViewCreateInfo view{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
    view.image = image->Image; view.viewType = VK_IMAGE_VIEW_TYPE_2D; view.format = format;
    view.subresourceRange = {aspect, 0, mips, 0, 1};
    VulkanCheck(vkCreateImageView(Device, &view, nullptr, &image->View), "Create image view");
    if (usage & VK_IMAGE_USAGE_SAMPLED_BIT) {
        VkSamplerCreateInfo sampler{VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO};
        const bool nearest = sampling.Filter == TextureFilterMode::Nearest;
        sampler.magFilter = sampler.minFilter = format == VK_FORMAT_R32_SINT || nearest ? VK_FILTER_NEAREST : VK_FILTER_LINEAR;
        sampler.mipmapMode = nearest ? VK_SAMPLER_MIPMAP_MODE_NEAREST : VK_SAMPLER_MIPMAP_MODE_LINEAR;
        sampler.addressModeU = sampling.WrapU == TextureWrapMode::Clamp ? VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE : VK_SAMPLER_ADDRESS_MODE_REPEAT;
        sampler.addressModeV = sampling.WrapV == TextureWrapMode::Clamp ? VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE : VK_SAMPLER_ADDRESS_MODE_REPEAT;
        sampler.addressModeW = VK_SAMPLER_ADDRESS_MODE_REPEAT;
        sampler.maxLod = float(mips - 1);
        VulkanCheck(vkCreateSampler(Device, &sampler, nullptr, &image->Sampler), "Create sampler");
    }
    VkImageMemoryBarrier barrier{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};
    barrier.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED; barrier.newLayout = VK_IMAGE_LAYOUT_GENERAL;
    barrier.srcQueueFamilyIndex = barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.image = image->Image; barrier.subresourceRange = {aspect, 0, mips, 0, 1};
    barrier.dstAccessMask = VK_ACCESS_MEMORY_READ_BIT | VK_ACCESS_MEMORY_WRITE_BIT;
    vkCmdPipelineBarrier(Commands(), VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,
        0, 0, nullptr, 0, nullptr, 1, &barrier);
    Keep(image);
    return image;
}
VkDescriptorPool VulkanDevice::CreateDrawDescriptorPool() {
    VkDescriptorPoolSize sizes[] = {{VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 4096}, {VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 32768}};
    VkDescriptorPoolCreateInfo create{VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO};
    create.maxSets = 4096; create.poolSizeCount = 2; create.pPoolSizes = sizes;
    VkDescriptorPool pool = VK_NULL_HANDLE;
    VulkanCheck(vkCreateDescriptorPool(Device, &create, nullptr, &pool), "Create draw descriptor pool");
    return pool;
}
VkDescriptorSet VulkanDevice::AllocateDescriptor(VkDescriptorSetLayout layout) {
    Commands();
    auto& slot = m_Slots[m_CurrentSlot];
    VkDescriptorSetAllocateInfo allocate{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};
    allocate.descriptorSetCount = 1; allocate.pSetLayouts = &layout;
    VkDescriptorSet set{};
    while (slot.DescriptorPoolIndex < slot.DescriptorPools.size()) {
        allocate.descriptorPool = slot.DescriptorPools[slot.DescriptorPoolIndex];
        const auto result = vkAllocateDescriptorSets(Device, &allocate, &set);
        if (result == VK_SUCCESS) { ++slot.DescriptorSets; return set; }
        if (result != VK_ERROR_OUT_OF_POOL_MEMORY && result != VK_ERROR_FRAGMENTED_POOL)
            VulkanCheck(result, "Allocate draw descriptor");
        ++slot.DescriptorPoolIndex;
    }
    // Descriptor-heavy shaders may exhaust one descriptor type before maxSets.
    // Grow the current slot without submitting slices from an unfinished draw.
    slot.DescriptorPools.push_back(CreateDrawDescriptorPool());
    allocate.descriptorPool = slot.DescriptorPools.back();
    VulkanCheck(vkAllocateDescriptorSets(Device, &allocate, &set), "Allocate draw descriptor in new pool");
    ++slot.DescriptorSets;
    return set;
}
VkFormat VulkanDevice::DepthFormat() const {
    for (auto format : {VK_FORMAT_D24_UNORM_S8_UINT, VK_FORMAT_D32_SFLOAT_S8_UINT}) {
        VkFormatProperties properties{}; vkGetPhysicalDeviceFormatProperties(PhysicalDevice, format, &properties);
        if (properties.optimalTilingFeatures & VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT) return format;
    }
    throw std::runtime_error("No supported depth/stencil format");
}
void VulkanDevice::StartProfileSegment(SubmissionSlot& slot) {
    if (!m_ProfileFrame || slot.ProfileFrame) return;
    slot.ProfileFrame = m_ProfileFrame;
    ++m_ProfileResults.at(m_ProfileFrame).PendingSegments;
    vkCmdResetQueryPool(slot.CommandBuffer, slot.QueryPool, 0, 2);
    vkCmdWriteTimestamp(slot.CommandBuffer, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, slot.QueryPool, 0);
}
void VulkanDevice::EndProfileSegment(SubmissionSlot& slot) {
    if (!slot.ProfileFrame || slot.ProfileEnded) return;
    vkCmdWriteTimestamp(slot.CommandBuffer, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, slot.QueryPool, 1);
    slot.ProfileEnded = true;
}
void VulkanDevice::PublishProfiles() {
    for (auto it = m_ProfileResults.begin(); it != m_ProfileResults.end();) {
        // FrameProfiler accepts results only after EndFrame has stored the frame.
        // A readback may finish the GPU while that CPU frame is still active.
        if (it->second.Ended && !it->second.PendingSegments && FrameProfiler::Get().ActiveFrame() != it->first) {
            FrameProfiler::Get().SetGpuTime(it->first, it->second.Milliseconds);
            it = m_ProfileResults.erase(it);
        } else ++it;
    }
}
void VulkanDevice::BeginProfile(uint64_t frame) {
    CollectCompleted();
    if (m_ProfileFrame) EndProfile();
    if (!frame || !SupportsProfiling()) return;
    // A slot has one query pair. Preserve an ended frame's queries when callers
    // start another profile before submitting its commands.
    if (m_Slots[m_CurrentSlot].ProfileFrame) Submit();
    m_ProfileFrame = frame;
    m_ProfileResults.try_emplace(frame);
    Commands();
    StartProfileSegment(m_Slots[m_CurrentSlot]);
}
void VulkanDevice::EndProfile() {
    if (!m_ProfileFrame) return;
    Commands();
    EndProfileSegment(m_Slots[m_CurrentSlot]);
    m_ProfileResults.at(m_ProfileFrame).Ended = true;
    m_ProfileFrame = 0;
}
}
