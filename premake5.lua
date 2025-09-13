workspace "TomCat"
	architecture "x64"

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

include "TomCat/vendor/GLFW"
include "TomCat/vendor/Glad"
include "TomCat/vendor/ImGui"

project "TomCat"
	location "TomCat"
	kind"sharedLib"
	language "C++"

	targetdir ("bin/" .. outputdir .."/%{prj.name}")
	objdir ("bin - int/" .. outputdir .."/%{prj.name}")

	pchheader "tcpch.h"
	pchsource "TomCat/src/tcpch.cpp"

	files
	{
		"%{prj.name}/src/**.h",
		"%{prj.name}/src/**.cpp",
		"%{prj.name}/vendor/glm/glm/**.hpp",
		"%{prj.name}/vendor/glm/glm/**.inl"
	}

	includedirs
	{
		"%{prj.name}/src",
		"%{prj.name}/vendor/spdlog/include",
		"%{IncludeDir.GLFW}",
		"%{IncludeDir.ImGui}",
		"%{IncludeDir.Glad}",
		"%{IncludeDir.glm}"
	}

	
	links 
	{ 
		"GLFW",
		"Glad",
		"ImGui",
		"opengl32.lib"
	}
	buildoptions "/utf-8"



	filter "system:windows"
		cppdialect"C++20"
		staticruntime "On"
		systemversion "latest"

	defines
	{
		"TC_PLAYTFORM_WINDOWS",
		"TC_BUILD_DLL",
		"GLFW_INCLUDE_NONE"
	}

	postbuildcommands
	{	("{MKDIR} ../bin/" .. outputdir .. "/Z_Examples"),
		("{COPYFILE} %{cfg.buildtarget.relpath} \"../bin/".. outputdir .."/Z_Examples/\"")
	}

	filter "configurations:Debug"
		defines "TC_DEBUG"
		buildoptions "/MDd"
		symbols "On"

	filter "configurations:Release"
		defines "TC_RELEASE"
		buildoptions "/MD"
		optimize "On"

	filter "configurations:Dist"
		defines "TC_DIST"
		buildoptions "/MD"
		optimize "On"

project "Z_Examples"
	location "Z_Examples"

	kind "ConsoleAPP"

	language "C++"

	targetdir ("bin/" .. outputdir .."/%{prj.name}")
	objdir ("bin - int/" .. outputdir .."/%{prj.name}")

	files
	{
		"%{prj.name}/src/**.h",
		"%{prj.name}/src/**.cpp"
	}

	includedirs
	{
	
		"TomCat/vendor/spdlog/include",
		"TomCat/src",
		"%{IncludeDir.glm}"
	}

	links
	{
		"TomCat"

	}

	buildoptions "/utf-8"


	filter "system:windows"
		cppdialect"C++20"
		staticruntime "On"
		systemversion "latest"

	defines
	{
		"TC_PLAYTFORM_WINDOWS"
	}

	filter "configurations:Debug"
		defines "TC_DEBUG"
		buildoptions "/MDd"
		symbols "On"

	filter "configurations:Release"
		defines "TC_RELEASE"
		buildoptions "/MD"
		optimize "On"

	filter "configurations:Dist"
		defines "TC_DIST"
		buildoptions "/MD"
		optimize "On"  