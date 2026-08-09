project "TomCatInut"
	kind "ConsoleApp"
	language "C++"
	cppdialect "C++20"
	staticruntime "off"

	targetdir ("%{wks.location}/bin/" .. outputdir .. "/%{prj.name}")
	objdir ("%{wks.location}/bin-int/" .. outputdir .. "/%{prj.name}")

	files
	{
		"src/**.h",
		"src/**.cpp",
		"TomCatInut.rc"
	}

	includedirs
	{
		"%{wks.location}/../TomCat/vendor/spdlog/include",
		"%{wks.location}/../TomCat/src",
		"%{wks.location}/../TomCat/vendor",
		"%{IncludeDir.glm}",
		"%{IncludeDir.entt}",
		"%{IncludeDir.ImGuizmo}"
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
			"if defined VULKAN_SDK (if exist \"%VULKAN_SDK%\\Bin\\shaderc_shared.dll\" copy /Y \"%VULKAN_SDK%\\Bin\\shaderc_shared.dll\" \"%{cfg.targetdir}\\\" > nul)",
			"if exist \"$(ProjectDir)Packages\" xcopy /E /Y /I \"$(ProjectDir)Packages\" \"$(OutDir)Packages\\\" > nul",
			"copy /Y \"$(ProjectDir)imgui.ini\" \"$(OutDir)imgui.ini\" > nul",
		}

	filter "configurations:Release"
		defines "TC_RELEASE"
		runtime "Release"
		optimize "on"
		postbuildcommands {
			"if defined VULKAN_SDK (if exist \"%VULKAN_SDK%\\Bin\\shaderc_shared.dll\" copy /Y \"%VULKAN_SDK%\\Bin\\shaderc_shared.dll\" \"%{cfg.targetdir}\\\" > nul)",
			"if exist \"$(ProjectDir)Packages\" xcopy /E /Y /I \"$(ProjectDir)Packages\" \"$(OutDir)Packages\\\" > nul",
			"copy /Y \"$(ProjectDir)imgui.ini\" \"$(OutDir)imgui.ini\" > nul",
		}

	filter "configurations:Dist"
		defines "TC_DIST"
		runtime "Release"
		optimize "on"
		postbuildcommands {
			"if defined VULKAN_SDK (if exist \"%VULKAN_SDK%\\Bin\\shaderc_shared.dll\" copy /Y \"%VULKAN_SDK%\\Bin\\shaderc_shared.dll\" \"%{cfg.targetdir}\\\" > nul)",
			"if exist \"$(ProjectDir)Packages\" xcopy /E /Y /I \"$(ProjectDir)Packages\" \"$(OutDir)Packages\\\" > nul",
			"copy /Y \"$(ProjectDir)imgui.ini\" \"$(OutDir)imgui.ini\" > nul",
		}