#pragma once

#include <vulkan/vulkan.h>
#include <span>
#include <stdexcept>
#include <string>

namespace TomCat {

inline constexpr uint32_t RequiredVulkanVersion = VK_API_VERSION_1_2;
inline constexpr uint32_t RequiredVulkanTextureSlots = 32;

struct VulkanSupportResult {
    bool Supported = false;
    std::string Reason;
};

class VulkanUnsupportedError : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};

// Only capability/driver incompatibilities permit automatic OpenGL fallback.
// Resource exhaustion, lost devices, validation and other failures stay errors.
void VulkanCheckSupport(VkResult result, const char* operation);

// Checks the loader and device capabilities without GLFW, a window, or a logical
// device. A supported result still requires checking the actual window surface
// during initialization. All probe resources are released before returning.
VulkanSupportResult ProbeVulkanSupport();

// Keep automatic backend selection and actual device selection in agreement.
const char* VulkanDeviceRequirementFailure(const VkPhysicalDeviceProperties& properties,
    const VkPhysicalDeviceFeatures& features);
bool VulkanDeviceMatchesRequest(const VkPhysicalDeviceProperties& properties, const char* requestedDevice);
bool VulkanHasSwapchainExtension(std::span<const VkExtensionProperties> extensions);
const char* VulkanSurfaceRequirementFailure(const VkSurfaceCapabilitiesKHR& capabilities,
    std::span<const VkSurfaceFormatKHR> formats, VkFormat requiredFormat = VK_FORMAT_UNDEFINED);

}
