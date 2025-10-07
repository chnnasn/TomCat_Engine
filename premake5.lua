workspace "TomCat"
	architecture "x64"
	startproject "TomCatInut"

	configurations 
	{
		"Debug",
		"Release",
		"Dist"
	}


outputdir = "%{cfg.buildcfd}-%{cfg.system}-%{cfg.architecture}"

-- Include directories relative to root folder (solution directory)
IncludeDir = {}
IncludeDir["GLFW"] = "TomCat/vendor/GLFW/include"
IncludeDir["Glad"] = "TomCat/vendor/Glad/include"
IncludeDir["ImGui"] = "TomCat/vendor/ImGui"
IncludeDir["glm"] = "TomCat/vendor/glm"
IncludeDir["stb_image"] = "TomCat/vendor/stb_image"
IncludeDir["entt"] = "TomCat/vendor/entt/include"
IncludeDir["yaml_cpp"] = "TomCat/vendor/yaml-cpp/include"

group "Dependencies"
	include "TomCat/vendor/GLFW"
	include "TomCat/vendor/Glad"
	include "TomCat/vendor/ImGui"
	include "TomCat/vendor/yaml-cpp"
group ""
project "TomCat"
	location "TomCat"
	kind"StaticLib"
	language "C++"
	cppdialect"C++20"
	staticruntime "on"

	targetdir ("bin/" .. outputdir .."/%{prj.name}")
	objdir ("bin-int/" .. outputdir .."/%{prj.name}")

	pchheader "tcpch.h"
	pchsource "TomCat/src/tcpch.cpp"

	files
	{
		"%{prj.name}/src/**.h",
		"%{prj.name}/src/**.cpp",
		"%{prj.name}/vendor/glm/glm/**.hpp",
		"%{prj.name}/vendor/glm/glm/**.inl",
		"%{prj.name}/vendor/stb_image/**.h",
		"%{prj.name}/vendor/stb_image/**.cpp"
	}


	defines
	{
		"_CRT_SECURE_NO_WARNINGS",
		"GLFW_INCLUDE_NONE"
	}

	includedirs
	{
		"%{prj.name}/src",
		"%{prj.name}/vendor/spdlog/include",
		"%{IncludeDir.GLFW}",
		"%{IncludeDir.ImGui}",
		"%{IncludeDir.Glad}",
		"%{IncludeDir.glm}",
		"%{IncludeDir.stb_image}",
		"%{IncludeDir.entt}",
		"%{IncludeDir.yaml_cpp}"
	}

	
	links 
	{ 
		"GLFW",
		"Glad",
		"ImGui",
		"yaml-cpp",
		"opengl32.lib"
	}
	buildoptions "/utf-8"



	filter "system:windows"
		systemversion "latest"

	defines
	{
		"TC_PLAYTFORM_WINDOWS",
		"TC_BUILD_DLL",
		"GLFW_INCLUDE_NONE",
		"IMGUI_API=_declspec(dllexport);"
	}

	filter "configurations:Debug"
		defines "TC_DEBUG"
		runtime "Debug"
		symbols "on"

	filter "configurations:Release"
		defines "TC_RELEASE"
		runtime "Release"
		optimize "on"

	filter "configurations:Dist"
		defines "TC_DIST"
		runtime "Release"
		optimize "on"


project "Z_Examples"

	location "Z_Examples"
	kind "ConsoleAPP"
	language "C++"
	cppdialect"C++20"
	staticruntime "on"

	targetdir ("bin/" .. outputdir .."/%{prj.name}")
	objdir ("bin-int/" .. outputdir .."/%{prj.name}")

	files
	{
		"%{prj.name}/src/**.h",
		"%{prj.name}/src/**.cpp"
	}

	includedirs
	{
	
		"TomCat/vendor/spdlog/include",
		"TomCat/src",
		"TomCat/vendor",
		"%{IncludeDir.glm}",
		"%{IncludeDir.entt}"
	}

	links
	{
		"TomCat"

	}

	buildoptions "/utf-8"


	filter "system:windows"
		systemversion "latest"

	defines
	{
		"TC_PLAYTFORM_WINDOWS",
		"IMGUI_API=_declspec(dllimport);"
	}

	filter "configurations:Debug"
		defines "TC_DEBUG"
		runtime "Debug"
		symbols "on"

	filter "configurations:Release"
		defines "TC_RELEASE"
		runtime "Release"
		optimize "on"

	filter "configurations:Dist"
		defines "TC_DIST"
		runtime "Release"
		optimize "on" 

project "TomCatInut"
	location "TomCatInut"
	kind "ConsoleApp"
	language "C++"
	cppdialect "C++20"
	staticruntime "on"

	targetdir ("bin/" .. outputdir .. "/%{prj.name}")
	objdir ("bin-int/" .. outputdir .. "/%{prj.name}")

	files
	{
		"%{prj.name}/src/**.h",
		"%{prj.name}/src/**.cpp"
	}

	includedirs
	{
		"TomCat/vendor/spdlog/include",
		"TomCat/src",
		"TomCat/vendor",
		"%{IncludeDir.glm}",
		"%{IncludeDir.entt}"
	}

	links
	{
		"TomCat"
	}

	buildoptions "/utf-8"

	filter "system:windows"
		systemversion "latest"
		
	defines
	{
		"TC_PLAYTFORM_WINDOWS",
		"IMGUI_API=_declspec(dllimport);"
	}

	filter "configurations:Debug"
		defines "TC_DEBUG"
		runtime "Debug"
		symbols "on"

	filter "configurations:Release"
		defines "TC_RELEASE"
		runtime "Release"
		optimize "on"

	filter "configurations:Dist"
		defines "TC_DIST"
		runtime "Release"
		optimize "on"