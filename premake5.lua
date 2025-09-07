workspace "TomCat"
	architecture "x64"

	configurations 
	{
		"Debug",
		"Release",
		"Dist"
	}


outputdir = "%{cfg.buildcfd}-%{cfg.system}-%{cfg.architecture}"
project "TomCat"
	location "TomCat"
	kind"sharedLib"
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
		"%{prj.name}/src",
		"%{prj.name}/vendor/spdlog/include"
	}

	filter "system:windows"
		cppdialect"C++20"
		staticruntime "On"
		systemversion "latest"

	defines
	{
		"TC_PLAYTFORM_WINDOWS",
		"TC_BUILD_DLL",
	}

	postbuildcommands
	{
		("{COPYFILE} %{cfg.buildtarget.relpath} ../bin/" .. outputdir .."/Z_Examples")
	}

	filter "configurations:Debug"
		defines "TC_DEBUG"
		symbols "On"

	filter "configurations:Release"
		defines "TC_RELEASE"
		optimize "On"

	filter "configurations:Dist"
		defines "TC_DIST"
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
		"TomCat/src"
	}

	links
	{
		"TomCat"

	}


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
		symbols "On"

	filter "configurations:Release"
		defines "TC_RELEASE"
		optimize "On"

	filter "configurations:Dist"
		defines "TC_DIST"
		optimize "On"
