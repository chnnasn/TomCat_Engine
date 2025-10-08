project "Z_Examples"
	kind "ConsoleAPP"
	language "C++"
	cppdialect"C++20"
	staticruntime "on"

	targetdir ("%{wks.location}/bin/" .. outputdir .."/%{prj.name}")
	objdir ("%{wks.location}/bin-int/" .. outputdir .."/%{prj.name}")

	files
	{
		"src/**.h",
		"src/**.cpp"
	}

	includedirs
	{
	
		"%{wks.location}/TomCat/vendor/spdlog/include",
		"%{wks.location}/TomCat/src",
		"%{wks.location}/TomCat/vendor",
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
