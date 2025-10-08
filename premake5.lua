include "./vendor/premake/premake_customization/solution_items.lua"

workspace "TomCat"
	architecture "x64"
	startproject "TomCatInut"

	configurations 
	{
		"Debug",
		"Release",
		"Dist"
	}

		solution_items
	{
		".editorconfig"
	}

	flags
	{
		"MultiProcessorCompile"
	}


outputdir = "%{cfg.buildcfd}-%{cfg.system}-%{cfg.architecture}"

-- Include directories relative to root folder (solution directory)
IncludeDir = {}
IncludeDir["GLFW"] = "%{wks.location}/TomCat/vendor/GLFW/include"
IncludeDir["Glad"] = "%{wks.location}/TomCat/vendor/Glad/include"
IncludeDir["ImGui"] = "%{wks.location}/TomCat/vendor/ImGui"
IncludeDir["glm"] = "%{wks.location}/TomCat/vendor/glm"
IncludeDir["stb_image"] = "%{wks.location}/TomCat/vendor/stb_image"
IncludeDir["entt"] = "%{wks.location}/TomCat/vendor/entt/include"
IncludeDir["yaml_cpp"] = "%{wks.location}/TomCat/vendor/yaml-cpp/include"
IncludeDir["ImGuizmo"] = "%{wks.location}/TomCat/vendor/ImGuizmo"

group "Dependencies"
	include "vendor/premake"
	include "TomCat/vendor/GLFW"
	include "TomCat/vendor/Glad"
	include "TomCat/vendor/ImGui"
	include "TomCat/vendor/yaml-cpp"
group ""

include "TomCat"
include "Z_Examples"
include "TomCatInut"