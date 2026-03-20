project "Manager"
	kind "ConsoleAPP"
	language "C++"
	cppdialect"C++20"
	staticruntime "off"

	targetdir ("%{wks.location}/bin/" .. outputdir .."/%{prj.name}")
	objdir ("%{wks.location}/bin-int/" .. outputdir .."/%{prj.name}")

	files
	{
		"src/**.h",
		"src/**.cpp"
	}

	includedirs
	{
	
		"%{wks.location}/../TomCat/vendor/spdlog/include",
		"%{wks.location}/../TomCat/src",
		"%{wks.location}/../TomCat/vendor",
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
		"IMGUI_API=_declspec(dllimport);",
		"YAML_CPP_STATIC_DEFINE" 
	}

	filter "configurations:Debug"
		defines "TC_DEBUG"
		runtime "Debug"
		symbols "on"

		postbuildcommands { 
			"{COPY} \"%{LibraryDir.VulkanSDK}/../Bin/shaderc_shared.dll\" \"%{cfg.targetdir}/\"",
			"{COPYDIR} %{prj.location}/Editors %{cfg.targetdir}/Editors"
		}

	filter "configurations:Release"
		defines "TC_RELEASE"
		runtime "Release"
		optimize "on"
		postbuildcommands {
			"{COPY} \"%{LibraryDir.VulkanSDK}/../Bin/shaderc_shared.dll\" \"%{cfg.targetdir}/\"",
			"{COPYDIR} %{prj.location}/Editors %{cfg.targetdir}/Editors"
		}

	filter "configurations:Dist"
		defines "TC_DIST"
		runtime "Release"
		optimize "on"
		postbuildcommands {
			"{COPY} \"%{LibraryDir.VulkanSDK}/../Bin/shaderc_shared.dll\" \"%{cfg.targetdir}/\"",
			"{COPYDIR} %{prj.location}/Editors %{cfg.targetdir}/Editors"
		}
