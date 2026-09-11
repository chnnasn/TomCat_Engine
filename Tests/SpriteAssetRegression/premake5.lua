project "SpriteAssetRegression"
	kind "ConsoleApp"
	language "C++"
	cppdialect "C++20"
	staticruntime "off"

	targetdir ("%{wks.location}/bin/" .. outputdir .. "/%{prj.name}")
	objdir ("%{wks.location}/bin-int/" .. outputdir .. "/%{prj.name}")

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
		"%{IncludeDir.Box2D}",
		"%{IncludeDir.glm}",
		"%{IncludeDir.entt}",
		"%{IncludeDir.stb_image}"
	}

	libdirs
	{
		"../../TomCat/vendor/Box2D/bin/" .. outputdir .. "/Box2D",
		"../../TomCat/vendor/GLFW/bin/" .. outputdir .. "/GLFW",
		"../../TomCat/vendor/Glad/bin/" .. outputdir .. "/Glad",
		"../../TomCat/vendor/ImGui/bin/" .. outputdir .. "/ImGui",
		"../../TomCat/vendor/yaml-cpp/bin/" .. outputdir .. "/yaml-cpp"
	}

	links
	{
		"TomCat",
		table.unpack(TomCatConsumerLinks)
	}

	buildoptions "/utf-8"

	filter "system:windows"
		systemversion "latest"
		defines
		{
			"TC_PLATFORM_WINDOWS",
			"YAML_CPP_STATIC_DEFINE"
		}

	filter "configurations:Debug"
		defines "TC_DEBUG"
		runtime "Debug"
		symbols "on"
		links { table.unpack(TomCatConsumerLinksDebug) }
		postbuildcommands {
			"if exist \"$(ProjectDir)..\\..\\vendor\\VulkanSDK\\Bin\\shaderc_sharedd.dll\" copy /Y \"$(ProjectDir)..\\..\\vendor\\VulkanSDK\\Bin\\shaderc_sharedd.dll\" \"%{cfg.targetdir}\\\" > nul"
		}

	filter "configurations:Release"
		defines "TC_RELEASE"
		runtime "Release"
		optimize "on"
		links { table.unpack(TomCatConsumerLinksRelease) }
		postbuildcommands {
			"if exist \"$(ProjectDir)..\\..\\vendor\\VulkanSDK\\Bin\\shaderc_shared.dll\" copy /Y \"$(ProjectDir)..\\..\\vendor\\VulkanSDK\\Bin\\shaderc_shared.dll\" \"%{cfg.targetdir}\\\" > nul"
		}

	filter "configurations:Dist"
		defines "TC_DIST"
		runtime "Release"
		optimize "on"
		links { table.unpack(TomCatConsumerLinksRelease) }
		postbuildcommands {
			"if exist \"$(ProjectDir)..\\..\\vendor\\VulkanSDK\\Bin\\shaderc_shared.dll\" copy /Y \"$(ProjectDir)..\\..\\vendor\\VulkanSDK\\Bin\\shaderc_shared.dll\" \"%{cfg.targetdir}\\\" > nul"
		}
