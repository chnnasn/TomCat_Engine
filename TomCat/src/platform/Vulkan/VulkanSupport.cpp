#include "tcpch.h"
#include "VulkanSupport.h"
#include <cstdlib>
#include <cstring>
#include <string_view>

#ifdef TC_PLATFORM_WINDOWS
#include <vulkan/vulkan_win32.h>
#endif

namespace TomCat {

void VulkanCheckSupport(VkResult result, const char* operation) {
    if (result == VK_SUCCESS) return;
    const std::string message = std::string(operation) + " failed (VkResult " + std::to_string(result) + ")";
    switch (result) {
    case VK_ERROR_INCOMPATIBLE_DRIVER:
    case VK_ERROR_FEATURE_NOT_PRESENT:
    case VK_ERROR_EXTENSION_NOT_PRESENT:
    case VK_ERROR_FORMAT_NOT_SUPPORTED:
        throw VulkanUnsupportedError(message);
    default:
        throw std::runtime_error(message);
    }
}

const char* VulkanDeviceRequirementFailure(const VkPhysicalDeviceProperties& properties,
    const VkPhysicalDeviceFeatures& features) {
    if (properties.apiVersion < RequiredVulkanVersion) return "Vulkan 1.2 is required";
    if (!features.independentBlend) return "independent blend is unavailable";
    const auto& limits = properties.limits;
    if (limits.maxPerStageDescriptorSamplers < RequiredVulkanTextureSlots
        || limits.maxPerStageDescriptorSampledImages < RequiredVulkanTextureSlots
        || limits.maxDescriptorSetSamplers < RequiredVulkanTextureSlots
        || limits.maxDescriptorSetSampledImages < RequiredVulkanTextureSlots)
        return "32 combined image sampler texture slots are required";
    return nullptr;
}

bool VulkanDeviceMatchesRequest(const VkPhysicalDeviceProperties& properties, const char* requestedDevice) {
    return !requestedDevice || !*requestedDevice
        || std::string_view(properties.deviceName).find(requestedDevice) != std::string_view::npos;
}

bool VulkanHasSwapchainExtension(std::span<const VkExtensionProperties> extensions) {
    return std::any_of(extensions.begin(), extensions.end(), [](const auto& extension) {
        return std::strcmp(extension.extensionName, VK_KHR_SWAPCHAIN_EXTENSION_NAME) == 0;
    });
}

const char* VulkanSurfaceRequirementFailure(const VkSurfaceCapabilitiesKHR& capabilities,
    std::span<const VkSurfaceFormatKHR> formats, VkFormat requiredFormat) {
    constexpr VkImageUsageFlags requiredUsage = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
    if ((capabilities.supportedUsageFlags & requiredUsage) != requiredUsage)
        return "Surface does not support color attachment and transfer destination usage";
    if (capabilities.maxImageCount && capabilities.maxImageCount < 2)
        return "Surface cannot provide two images for ImGui";
    if (std::none_of(formats.begin(), formats.end(), [&](const auto& format) {
        return (requiredFormat != VK_FORMAT_UNDEFINED ? format.format == requiredFormat
            : (format.format == VK_FORMAT_B8G8R8A8_UNORM || format.format == VK_FORMAT_R8G8B8A8_UNORM))
            && format.colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR;
    })) return "Surface requires an unsupported color format (RGBA/BGRA UNORM required)";
    return nullptr;
}

namespace {
#ifdef TC_PLATFORM_WINDOWS
struct ProbeResources {
    HMODULE Loader = nullptr;
    VkInstance Instance = VK_NULL_HANDLE;
    PFN_vkDestroyInstance DestroyInstance = nullptr;

    ~ProbeResources() {
        if (Instance && DestroyInstance) DestroyInstance(Instance, nullptr);
        if (Loader) FreeLibrary(Loader);
    }
};

template<typename T, typename Enumerate>
VkResult EnumerateProperties(std::vector<T>& values, Enumerate enumerate) {
    // The device/extension set can change between the count and data queries.
    for (int attempt = 0; attempt < 4; ++attempt) {
        uint32_t count = 0;
        auto result = enumerate(&count, nullptr);
        if (result != VK_SUCCESS) return result;
        values.resize(count);
        if (!count) return VK_SUCCESS;
        result = enumerate(&count, values.data());
        if (result == VK_SUCCESS) {
            values.resize(count);
            return result;
        }
        if (result != VK_INCOMPLETE) return result;
    }
    return VK_INCOMPLETE;
}

VulkanSupportResult ProbeFailure(const char* operation, VkResult result) {
    return {false, std::string(operation) + " failed (VkResult " + std::to_string(result) + ")"};
}

VulkanSupportResult ProbeWindowsVulkanSupport() {
    ProbeResources resources;
    // Never touch imported Vulkan entry points here: vulkan-1.dll is delay
    // loaded by the engine, and missing drivers must be a normal fallback.
    resources.Loader = LoadLibraryExW(L"vulkan-1.dll", nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32);
    if (!resources.Loader) return {false, "Vulkan loader vulkan-1.dll is unavailable"};
    const auto getProc = reinterpret_cast<PFN_vkGetInstanceProcAddr>(GetProcAddress(resources.Loader, "vkGetInstanceProcAddr"));
    if (!getProc) return {false, "Vulkan loader entry point is unavailable"};
    const auto enumerateVersion = reinterpret_cast<PFN_vkEnumerateInstanceVersion>(getProc(VK_NULL_HANDLE, "vkEnumerateInstanceVersion"));
    if (!enumerateVersion) return {false, "Vulkan loader does not support Vulkan 1.2"};
    uint32_t version = VK_API_VERSION_1_0;
    auto result = enumerateVersion(&version);
    if (result != VK_SUCCESS) return ProbeFailure("Query Vulkan loader version", result);
    if (version < RequiredVulkanVersion) return {false, "Vulkan loader does not support Vulkan 1.2"};

    const auto enumerateInstanceExtensions = reinterpret_cast<PFN_vkEnumerateInstanceExtensionProperties>(getProc(VK_NULL_HANDLE, "vkEnumerateInstanceExtensionProperties"));
    const auto createInstance = reinterpret_cast<PFN_vkCreateInstance>(getProc(VK_NULL_HANDLE, "vkCreateInstance"));
    if (!enumerateInstanceExtensions || !createInstance) return {false, "Vulkan loader instance entry points are unavailable"};
    std::vector<VkExtensionProperties> instanceExtensions;
    result = EnumerateProperties(instanceExtensions, [&](uint32_t* count, VkExtensionProperties* extensions) {
        return enumerateInstanceExtensions(nullptr, count, extensions);
    });
    if (result != VK_SUCCESS) return ProbeFailure("Enumerate Vulkan instance extensions", result);
    const char* requiredExtensions[] = {VK_KHR_SURFACE_EXTENSION_NAME, VK_KHR_WIN32_SURFACE_EXTENSION_NAME};
    for (const auto* required : requiredExtensions) {
        if (std::none_of(instanceExtensions.begin(), instanceExtensions.end(), [&](const auto& extension) {
            return std::strcmp(extension.extensionName, required) == 0;
        })) return {false, std::string("Vulkan instance extension is unavailable: ") + required};
    }
    VkApplicationInfo app{VK_STRUCTURE_TYPE_APPLICATION_INFO};
    app.pApplicationName = "TomCat capability probe";
    app.apiVersion = RequiredVulkanVersion;
    VkInstanceCreateInfo create{VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO};
    create.pApplicationInfo = &app;
    create.enabledExtensionCount = 2;
    create.ppEnabledExtensionNames = requiredExtensions;
    result = createInstance(&create, nullptr, &resources.Instance);
    if (result != VK_SUCCESS) return ProbeFailure("Create Vulkan probe instance", result);
    resources.DestroyInstance = reinterpret_cast<PFN_vkDestroyInstance>(getProc(resources.Instance, "vkDestroyInstance"));
    const auto enumerateDevices = reinterpret_cast<PFN_vkEnumeratePhysicalDevices>(getProc(resources.Instance, "vkEnumeratePhysicalDevices"));
    const auto getProperties = reinterpret_cast<PFN_vkGetPhysicalDeviceProperties>(getProc(resources.Instance, "vkGetPhysicalDeviceProperties"));
    const auto getFeatures = reinterpret_cast<PFN_vkGetPhysicalDeviceFeatures>(getProc(resources.Instance, "vkGetPhysicalDeviceFeatures"));
    const auto enumerateDeviceExtensions = reinterpret_cast<PFN_vkEnumerateDeviceExtensionProperties>(getProc(resources.Instance, "vkEnumerateDeviceExtensionProperties"));
    const auto getQueueFamilies = reinterpret_cast<PFN_vkGetPhysicalDeviceQueueFamilyProperties>(getProc(resources.Instance, "vkGetPhysicalDeviceQueueFamilyProperties"));
    const auto supportsPresentation = reinterpret_cast<PFN_vkGetPhysicalDeviceWin32PresentationSupportKHR>(getProc(resources.Instance, "vkGetPhysicalDeviceWin32PresentationSupportKHR"));
    if (!resources.DestroyInstance || !enumerateDevices || !getProperties || !getFeatures
        || !enumerateDeviceExtensions || !getQueueFamilies || !supportsPresentation)
        return {false, "Vulkan device/presentation query entry points are unavailable"};

    std::vector<VkPhysicalDevice> devices;
    result = EnumerateProperties(devices, [&](uint32_t* count, VkPhysicalDevice* values) {
        return enumerateDevices(resources.Instance, count, values);
    });
    if (result != VK_SUCCESS) return ProbeFailure("Enumerate Vulkan physical devices", result);
    if (devices.empty()) return {false, "No Vulkan physical devices are available"};
    const char* requestedDevice = std::getenv("TC_VULKAN_DEVICE");
    bool matchedDevice = false;
    std::string lastFailure;
    for (auto device : devices) {
        VkPhysicalDeviceProperties properties{};
        VkPhysicalDeviceFeatures features{};
        getProperties(device, &properties);
        if (!VulkanDeviceMatchesRequest(properties, requestedDevice)) continue;
        matchedDevice = true;
        getFeatures(device, &features);
        const std::string devicePrefix = std::string(properties.deviceName) + ": ";
        if (const auto* reason = VulkanDeviceRequirementFailure(properties, features)) {
            lastFailure = devicePrefix + reason;
            continue;
        }
        std::vector<VkExtensionProperties> extensions;
        result = EnumerateProperties(extensions, [&](uint32_t* count, VkExtensionProperties* values) {
            return enumerateDeviceExtensions(device, nullptr, count, values);
        });
        if (result != VK_SUCCESS) {
            lastFailure = devicePrefix + "device extension query failed (VkResult " + std::to_string(result) + ")";
            continue;
        }
        if (!VulkanHasSwapchainExtension(extensions)) {
            lastFailure = devicePrefix + "VK_KHR_swapchain is unavailable";
            continue;
        }
        uint32_t count = 0;
        getQueueFamilies(device, &count, nullptr);
        std::vector<VkQueueFamilyProperties> families(count);
        getQueueFamilies(device, &count, families.data());
        for (uint32_t i = 0; i < count; ++i) {
            // Win32 presentation support can be queried before a surface exists.
            if (families[i].queueCount && (families[i].queueFlags & VK_QUEUE_GRAPHICS_BIT)
                && supportsPresentation(device, i)) return {true, {}};
        }
        lastFailure = devicePrefix + "no queue supports both graphics and Win32 presentation";
    }
    if (!matchedDevice) return {false, std::string("No Vulkan physical device matches TC_VULKAN_DEVICE=\"") + requestedDevice + "\""};
    return {false, "No suitable Vulkan device: " + lastFailure};
}
#endif
}

VulkanSupportResult ProbeVulkanSupport() {
#ifdef TC_PLATFORM_WINDOWS
    try {
        return ProbeWindowsVulkanSupport();
    } catch (const std::exception& error) {
        return {false, std::string("Vulkan capability probe failed: ") + error.what()};
    }
#else
    return {false, "Automatic Vulkan capability probing is unavailable on this platform"};
#endif
}

}
