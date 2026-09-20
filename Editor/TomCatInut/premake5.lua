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
		"%{IncludeDir.ImGuizmo}",
		"%{IncludeDir.yaml_cpp}"
	}

	libdirs
	{
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

	filter "configurations:Release"
		links { table.unpack(TomCatConsumerLinksRelease) }

	filter "configurations:Dist"
		links { table.unpack(TomCatConsumerLinksRelease) }

	filter "configurations:Debug"
		links { table.unpack(TomCatConsumerLinksDebug) }



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
		postbuildcommands {
			"if exist \"$(ProjectDir)..\\..\\vendor\\VulkanSDK\\Bin\\shaderc_shared.dll\" copy /Y \"$(ProjectDir)..\\..\\vendor\\VulkanSDK\\Bin\\shaderc_shared.dll\" \"%{cfg.targetdir}\\\" > nul",
			"if not exist \"$(ProjectDir)Packages\\Resources\\Sprites\\TomCat\\Circle.tga\" (echo ERROR: Required package asset is missing: Resources\\Sprites\\TomCat\\Circle.tga & exit /b 1)",
			"if not exist \"$(ProjectDir)Packages\\Resources\\Sprites\\TomCat\\Square.tga\" (echo ERROR: Required package asset is missing: Resources\\Sprites\\TomCat\\Square.tga & exit /b 1)",
			"if not exist \"$(ProjectDir)Packages\\fonts\\opensans\\OpenSans-Regular.ttf\" (echo ERROR: Required package asset is missing: fonts\\opensans\\OpenSans-Regular.ttf & exit /b 1)",
			"if exist \"$(ProjectDir)Packages\" xcopy /E /Y /I \"$(ProjectDir)Packages\" \"$(OutDir)Packages\\\" > nul",
			"if exist \"$(OutDir)Packages\\Resources\\Icons\\TomCat\\Asset\\assets-root.png\" del /Q \"$(OutDir)Packages\\Resources\\Icons\\TomCat\\Asset\\assets-root.png\"",
			"copy /Y \"$(ProjectDir)imgui.ini\" \"$(OutDir)imgui.ini\" > nul",
		}

	filter "configurations:Release"
		defines "TC_RELEASE"
		runtime "Release"
		optimize "on"
		postbuildcommands {
			"if exist \"$(ProjectDir)..\\..\\vendor\\VulkanSDK\\Bin\\shaderc_shared.dll\" copy /Y \"$(ProjectDir)..\\..\\vendor\\VulkanSDK\\Bin\\shaderc_shared.dll\" \"%{cfg.targetdir}\\\" > nul",
			"if not exist \"$(ProjectDir)Packages\\Resources\\Sprites\\TomCat\\Circle.tga\" (echo ERROR: Required package asset is missing: Resources\\Sprites\\TomCat\\Circle.tga & exit /b 1)",
			"if not exist \"$(ProjectDir)Packages\\Resources\\Sprites\\TomCat\\Square.tga\" (echo ERROR: Required package asset is missing: Resources\\Sprites\\TomCat\\Square.tga & exit /b 1)",
			"if not exist \"$(ProjectDir)Packages\\fonts\\opensans\\OpenSans-Regular.ttf\" (echo ERROR: Required package asset is missing: fonts\\opensans\\OpenSans-Regular.ttf & exit /b 1)",
			"if exist \"$(ProjectDir)Packages\" xcopy /E /Y /I \"$(ProjectDir)Packages\" \"$(OutDir)Packages\\\" > nul",
			"if exist \"$(OutDir)Packages\\Resources\\Icons\\TomCat\\Asset\\assets-root.png\" del /Q \"$(OutDir)Packages\\Resources\\Icons\\TomCat\\Asset\\assets-root.png\"",
			"copy /Y \"$(ProjectDir)imgui.ini\" \"$(OutDir)imgui.ini\" > nul",
		}

	filter "configurations:Dist"
		defines "TC_DIST"
		runtime "Release"
		optimize "on"
		postbuildcommands {
			"if exist \"$(ProjectDir)..\\..\\vendor\\VulkanSDK\\Bin\\shaderc_shared.dll\" copy /Y \"$(ProjectDir)..\\..\\vendor\\VulkanSDK\\Bin\\shaderc_shared.dll\" \"%{cfg.targetdir}\\\" > nul",
			"if not exist \"$(ProjectDir)Packages\\Resources\\Sprites\\TomCat\\Circle.tga\" (echo ERROR: Required package asset is missing: Resources\\Sprites\\TomCat\\Circle.tga & exit /b 1)",
			"if not exist \"$(ProjectDir)Packages\\Resources\\Sprites\\TomCat\\Square.tga\" (echo ERROR: Required package asset is missing: Resources\\Sprites\\TomCat\\Square.tga & exit /b 1)",
			"if not exist \"$(ProjectDir)Packages\\fonts\\opensans\\OpenSans-Regular.ttf\" (echo ERROR: Required package asset is missing: fonts\\opensans\\OpenSans-Regular.ttf & exit /b 1)",
			"if exist \"$(ProjectDir)Packages\" xcopy /E /Y /I \"$(ProjectDir)Packages\" \"$(OutDir)Packages\\\" > nul",
			"if exist \"$(OutDir)Packages\\Resources\\Icons\\TomCat\\Asset\\assets-root.png\" del /Q \"$(OutDir)Packages\\Resources\\Icons\\TomCat\\Asset\\assets-root.png\"",
			"copy /Y \"$(ProjectDir)imgui.ini\" \"$(OutDir)imgui.ini\" > nul",
		}
