-- Include directories relative to root folder (solution directory)

VULKAN_SDK = os.getenv("VULKAN_SDK")

IncludeDir = {}
IncludeDir["GLFW"] = "%{wks.location}/../TomCat/vendor/GLFW/include"
IncludeDir["Glad"] = "%{wks.location}/../TomCat/vendor/Glad/include"
IncludeDir["ImGui"] = "%{wks.location}/../TomCat/vendor/ImGui"
IncludeDir["glm"] = "%{wks.location}/../TomCat/vendor/glm"
IncludeDir["stb_image"] = "%{wks.location}/../TomCat/vendor/stb_image"
IncludeDir["entt"] = "%{wks.location}/../TomCat/vendor/entt/include"
IncludeDir["yaml_cpp"] = "%{wks.location}/../TomCat/vendor/yaml-cpp/include"
IncludeDir["ImGuizmo"] = "%{wks.location}/../TomCat/vendor/ImGuizmo"
IncludeDir["Box2D"] = "%{wks.location}/../TomCat/vendor/Box2D/include"
IncludeDir["SPIRV_Cross"] = "%{wks.location}/../TomCat/vendor/SPIRV-Cross"
IncludeDir["VulkanSDK"] = "%{VULKAN_SDK}/Include"
IncludeDir["shaderc"] = "%{VULKAN_SDK}/Include"

LibraryDir = {}

LibraryDir["VulkanSDK"] = "%{VULKAN_SDK}/Lib"
LibraryDir["VulkanSDK_Debug"] = "%{VULKAN_SDK}/Lib"

Library = {}
Library["Vulkan"] = "%{LibraryDir.VulkanSDK}/vulkan-1.lib"
Library["VulkanUtils"] = "%{LibraryDir.VulkanSDK}/VkLayer_utils.lib"

Library["ShaderC_Debug"] = "%{LibraryDir.VulkanSDK_Debug}/shaderc_sharedd.lib"
Library["SPIRV_Cross_Debug"] = "%{LibraryDir.VulkanSDK_Debug}/spirv-cross-cored.lib"
Library["SPIRV_Cross_GLSL_Debug"] = "%{LibraryDir.VulkanSDK_Debug}/spirv-cross-glsld.lib"
Library["SPIRV_Tools_Debug"] = "%{LibraryDir.VulkanSDK_Debug}/SPIRV-Toolsd.lib"

Library["ShaderC_Release"] = "%{LibraryDir.VulkanSDK}/shaderc_shared.lib"
Library["SPIRV_Cross_Release"] = "%{LibraryDir.VulkanSDK}/spirv-cross-core.lib"
Library["SPIRV_Cross_GLSL_Release"] = "%{LibraryDir.VulkanSDK}/spirv-cross-glsl.lib"
