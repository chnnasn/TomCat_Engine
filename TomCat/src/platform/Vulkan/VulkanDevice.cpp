#include "tcpch.h"
#include "VulkanDevice.h"
#include <GLFW/glfw3.h>
#include <stdexcept>
#include <cstring>


namespace TomCat {
namespace {
std::weak_ptr<VulkanDevice> s_Device;
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
    if (!glfwVulkanSupported()) throw std::runtime_error("Vulkan loader/driver is unavailable");
    uint32_t count = 0;
    const char** required = glfwGetRequiredInstanceExtensions(&count);
    if (!required || !count) throw std::runtime_error("GLFW cannot provide a Vulkan surface");
    VkApplicationInfo app{VK_STRUCTURE_TYPE_APPLICATION_INFO};
    app.pApplicationName = "TomCat";
    app.apiVersion = VK_API_VERSION_1_2;
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
    VulkanCheck(vkCreateInstance(&info, nullptr, &Instance), "Create instance");
    if (info.enabledLayerCount) {
        debug.pNext = nullptr;
        auto createDebug = reinterpret_cast<PFN_vkCreateDebugUtilsMessengerEXT>(vkGetInstanceProcAddr(Instance, "vkCreateDebugUtilsMessengerEXT"));
        if (!createDebug) throw std::runtime_error("Vulkan debug utils unavailable");
        VulkanCheck(createDebug(Instance, &debug, nullptr, &m_DebugMessenger), "Create validation messenger");
    }
    VkSurfaceKHR surface = VK_NULL_HANDLE;
    VulkanCheck(glfwCreateWindowSurface(Instance, window, nullptr, &surface), "Create probe surface");
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
            if (requestedDevice && *requestedDevice && std::string_view(properties.deviceName).find(requestedDevice) == std::string_view::npos) continue;
            const int score = properties.deviceType == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU ? 2 : 1;
            if (score <= bestScore) continue;
            if (properties.apiVersion < VK_API_VERSION_1_2 || !features.independentBlend
                || properties.limits.maxPerStageDescriptorSamplers < 32) continue;
            uint32_t extensionCount = 0;
            VulkanCheck(vkEnumerateDeviceExtensionProperties(candidate, nullptr, &extensionCount, nullptr), "Enumerate device extensions");
            std::vector<VkExtensionProperties> extensions(extensionCount);
            VulkanCheck(vkEnumerateDeviceExtensionProperties(candidate, nullptr, &extensionCount, extensions.data()), "Enumerate device extensions");
            if (std::none_of(extensions.begin(), extensions.end(), [](auto& e) { return std::strcmp(e.extensionName, VK_KHR_SWAPCHAIN_EXTENSION_NAME) == 0; })) continue;
            uint32_t familyCount = 0;
            vkGetPhysicalDeviceQueueFamilyProperties(candidate, &familyCount, nullptr);
            std::vector<VkQueueFamilyProperties> families(familyCount);
            vkGetPhysicalDeviceQueueFamilyProperties(candidate, &familyCount, families.data());
            for (uint32_t i = 0; i < familyCount; ++i) {
                VkBool32 present = false;
                VulkanCheck(vkGetPhysicalDeviceSurfaceSupportKHR(candidate, i, surface, &present), "Query present support");
                if (!(families[i].queueFlags & VK_QUEUE_GRAPHICS_BIT) || !present) continue;
                PhysicalDevice = candidate; QueueFamily = i; Properties = properties; Features = features;
                m_TimestampBits = families[i].timestampValidBits;
                bestScore = score;
                break;
            }
        }
    } catch (...) { vkDestroySurfaceKHR(Instance, surface, nullptr); throw; }
    vkDestroySurfaceKHR(Instance, surface, nullptr);
    if (!PhysicalDevice) throw std::runtime_error("Vulkan requires a 1.2 graphics/present device with independent blend and 32 texture slots");
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
    VulkanCheck(vkCreateDevice(PhysicalDevice, &create, nullptr, &Device), "Create device");
    vkGetDeviceQueue(Device, QueueFamily, 0, &Queue);
    VkCommandPoolCreateInfo pool{VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO};
    pool.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT; pool.queueFamilyIndex = QueueFamily;
    VulkanCheck(vkCreateCommandPool(Device, &pool, nullptr, &m_CommandPool), "Create command pool");
    VkCommandBufferAllocateInfo allocate{VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};
    allocate.commandPool = m_CommandPool; allocate.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY; allocate.commandBufferCount = 1;
    VulkanCheck(vkAllocateCommandBuffers(Device, &allocate, &m_CommandBuffer), "Allocate command buffer");
    VkFenceCreateInfo fence{VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};
    VulkanCheck(vkCreateFence(Device, &fence, nullptr, &m_Fence), "Create submission fence");
    VkDescriptorPoolSize sizes[] = {{VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 4096}, {VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 32768}};
    VkDescriptorPoolCreateInfo descriptors{VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO};
    descriptors.maxSets = 4096; descriptors.poolSizeCount = 2; descriptors.pPoolSizes = sizes;
    VulkanCheck(vkCreateDescriptorPool(Device, &descriptors, nullptr, &DescriptorPool), "Create draw descriptor pool");
    descriptors.flags = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT;
    VulkanCheck(vkCreateDescriptorPool(Device, &descriptors, nullptr, &ImGuiPool), "Create UI descriptor pool");
    if (SupportsProfiling()) {
        VkQueryPoolCreateInfo query{VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO};
        query.queryType = VK_QUERY_TYPE_TIMESTAMP; query.queryCount = 2;
        VulkanCheck(vkCreateQueryPool(Device, &query, nullptr, &m_QueryPool), "Create timestamps");
    }
    TC_Core_Info("Vulkan device: {0}", Properties.deviceName);
}
VulkanDevice::~VulkanDevice() {
    if (Device) vkDeviceWaitIdle(Device);
    m_Retained.clear();
    if (m_QueryPool) vkDestroyQueryPool(Device, m_QueryPool, nullptr);
    if (DescriptorPool) vkDestroyDescriptorPool(Device, DescriptorPool, nullptr);
    if (ImGuiPool) vkDestroyDescriptorPool(Device, ImGuiPool, nullptr);
    if (m_Fence) vkDestroyFence(Device, m_Fence, nullptr);
    if (m_CommandPool) vkDestroyCommandPool(Device, m_CommandPool, nullptr);
    if (Device) vkDestroyDevice(Device, nullptr);
    if (m_DebugMessenger) {
        auto destroyDebug = reinterpret_cast<PFN_vkDestroyDebugUtilsMessengerEXT>(vkGetInstanceProcAddr(Instance, "vkDestroyDebugUtilsMessengerEXT"));
        destroyDebug(Instance, m_DebugMessenger, nullptr);
    }
    if (Instance) vkDestroyInstance(Instance, nullptr);
}
VkCommandBuffer VulkanDevice::Commands() {
    if (!m_Recording) {
        VkCommandBufferBeginInfo begin{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
        begin.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
        VulkanCheck(vkBeginCommandBuffer(m_CommandBuffer, &begin), "Begin commands");
        m_Recording = true;
    }
    return m_CommandBuffer;
}
void VulkanDevice::Barrier() {
    VkMemoryBarrier barrier{VK_STRUCTURE_TYPE_MEMORY_BARRIER};
    barrier.srcAccessMask = VK_ACCESS_MEMORY_WRITE_BIT;
    barrier.dstAccessMask = VK_ACCESS_MEMORY_READ_BIT | VK_ACCESS_MEMORY_WRITE_BIT;
    vkCmdPipelineBarrier(Commands(), VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,
        0, 1, &barrier, 0, nullptr, 0, nullptr);
}
void VulkanDevice::SubmitAndWait(VkSemaphore signal) {
    if (signal) Commands();
    if (!m_Recording) return;
    VulkanCheck(vkEndCommandBuffer(m_CommandBuffer), "End commands");
    VkSubmitInfo submit{VK_STRUCTURE_TYPE_SUBMIT_INFO};
    submit.commandBufferCount = 1; submit.pCommandBuffers = &m_CommandBuffer;
    if (signal) { submit.signalSemaphoreCount = 1; submit.pSignalSemaphores = &signal; }
    VulkanCheck(vkQueueSubmit(Queue, 1, &submit, m_Fence), "Submit commands");
    VulkanCheck(vkWaitForFences(Device, 1, &m_Fence, true, UINT64_MAX), "Wait for commands");
    VulkanCheck(vkResetFences(Device, 1, &m_Fence), "Reset fence");
    VulkanCheck(vkResetCommandPool(Device, m_CommandPool, 0), "Reset command pool");
    m_Recording = false;
    m_Retained.clear();
    VulkanCheck(vkResetDescriptorPool(Device, DescriptorPool, 0), "Reset descriptors");
    if (m_ProfileEnded) {
        uint64_t timestamps[2]{};
        VulkanCheck(vkGetQueryPoolResults(Device, m_QueryPool, 0, 2, sizeof(timestamps), timestamps,
            sizeof(uint64_t), VK_QUERY_RESULT_64_BIT | VK_QUERY_RESULT_WAIT_BIT), "Read timestamps");
        const uint64_t mask = m_TimestampBits == 64 ? UINT64_MAX : ((uint64_t(1) << m_TimestampBits) - 1);
        m_PendingProfileFrame = m_ProfileFrame;
        m_PendingProfileTime = double((timestamps[1] - timestamps[0]) & mask) * Properties.limits.timestampPeriod / 1000000.0;
        m_ProfileEnded = false; m_ProfileFrame = 0;
    }
}
void VulkanDevice::Keep(std::shared_ptr<void> object) { m_Retained.push_back(std::move(object)); }
void VulkanDevice::DrainForDestruction() noexcept {
    try { SubmitAndWait(); }
    catch (const std::exception& e) { std::fprintf(stderr, "Vulkan cleanup: %s\n", e.what()); }
    if (Device) vkDeviceWaitIdle(Device);
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
    VkImageUsageFlags usage, VkImageAspectFlags aspect, uint32_t mips) {
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
        sampler.magFilter = sampler.minFilter = format == VK_FORMAT_R32_SINT ? VK_FILTER_NEAREST : VK_FILTER_LINEAR;
        sampler.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
        sampler.addressModeU = sampler.addressModeV = sampler.addressModeW = VK_SAMPLER_ADDRESS_MODE_REPEAT;
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
VkDescriptorSet VulkanDevice::AllocateDescriptor(VkDescriptorSetLayout layout) {
    VkDescriptorSetAllocateInfo allocate{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};
    allocate.descriptorPool = DescriptorPool; allocate.descriptorSetCount = 1; allocate.pSetLayouts = &layout;
    VkDescriptorSet set{};
    VulkanCheck(vkAllocateDescriptorSets(Device, &allocate, &set), "Allocate draw descriptor");
    return set;
}
VkFormat VulkanDevice::DepthFormat() const {
    for (auto format : {VK_FORMAT_D24_UNORM_S8_UINT, VK_FORMAT_D32_SFLOAT_S8_UINT}) {
        VkFormatProperties properties{}; vkGetPhysicalDeviceFormatProperties(PhysicalDevice, format, &properties);
        if (properties.optimalTilingFeatures & VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT) return format;
    }
    throw std::runtime_error("No supported depth/stencil format");
}
void VulkanDevice::BeginProfile(uint64_t frame) {
    if (m_PendingProfileFrame) {
        FrameProfiler::Get().SetGpuTime(m_PendingProfileFrame, m_PendingProfileTime);
        m_PendingProfileFrame = 0;
    }
    if (!frame || !SupportsProfiling()) return;
    m_ProfileFrame = frame;
    vkCmdResetQueryPool(Commands(), m_QueryPool, 0, 2);
    vkCmdWriteTimestamp(Commands(), VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, m_QueryPool, 0);
}
void VulkanDevice::EndProfile() {
    if (!m_ProfileFrame) return;
    vkCmdWriteTimestamp(Commands(), VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, m_QueryPool, 1);
    m_ProfileEnded = true;
}
}
