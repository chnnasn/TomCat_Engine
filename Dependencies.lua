-- Include directories relative to root folder (solution directory)

IncludeDir = {}
IncludeDir["GLFW"] = "%{wks.location}/../TomCat/vendor/GLFW/include"
IncludeDir["Glad"] = "%{wks.location}/../TomCat/vendor/Glad/include"
IncludeDir["ImGui"] = "%{wks.location}/../TomCat/vendor/ImGui"
IncludeDir["glm"] = "%{wks.location}/../TomCat/vendor/glm"
IncludeDir["stb_image"] = "%{wks.location}/../TomCat/vendor/stb_image"
IncludeDir["entt"] = "%{wks.location}/../TomCat/vendor/entt/include"
IncludeDir["yaml_cpp"] = "%{wks.location}/../TomCat/vendor/yaml-cpp/include"
IncludeDir["ImGuizmo"] = "%{wks.location}/../TomCat/vendor/ImGuizmo"
IncludeDir["Butter"] = "%{wks.location}/../TomCat/vendor/Butter/include"
-- The OpenGL shader pipeline uses ShaderC and SPIRV-Cross from this submodule.
-- These are shader compilation tools; no Vulkan renderer/loader is linked.
IncludeDir["VulkanSDK"] = "%{wks.location}/../vendor/VulkanSDK/Include"

LibraryDir = {}

LibraryDir["VulkanSDK"] = "%{wks.location}/../vendor/VulkanSDK/Lib"

Library = {}

Library["ShaderC_Debug"] = "%{LibraryDir.VulkanSDK}/shaderc_sharedd.lib"
Library["SPIRV_Cross_Debug"] = "%{LibraryDir.VulkanSDK}/spirv-cross-cored.lib"
Library["SPIRV_Cross_GLSL_Debug"] = "%{LibraryDir.VulkanSDK}/spirv-cross-glsld.lib"

Library["ShaderC_Release"] = "%{LibraryDir.VulkanSDK}/shaderc_shared.lib"
Library["SPIRV_Cross_Release"] = "%{LibraryDir.VulkanSDK}/spirv-cross-core.lib"
Library["SPIRV_Cross_GLSL_Release"] = "%{LibraryDir.VulkanSDK}/spirv-cross-glsl.lib"

-- Libraries that consumers of the TomCat static library must also link.
-- Kept here so Editor / Hub only reference one shared list.
TomCatConsumerLinks = {
	"GLFW.lib",
	"Glad.lib",
	"ImGui.lib",
	"yaml-cpp.lib",
	"opengl32.lib"
}

TomCatConsumerLinksRelease = {
	"%{Library.ShaderC_Release}",
	"%{Library.SPIRV_Cross_Release}",
	"%{Library.SPIRV_Cross_GLSL_Release}"
}

TomCatConsumerLinksDebug = {
	"%{Library.ShaderC_Debug}",
	"%{Library.SPIRV_Cross_Debug}",
	"%{Library.SPIRV_Cross_GLSL_Debug}"
}
