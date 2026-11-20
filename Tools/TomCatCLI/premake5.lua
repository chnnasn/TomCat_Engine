project "TomCatCLI"
	kind "ConsoleApp"
	language "C++"
	cppdialect "C++20"
	linkoptions (TomCatConsumerLinkOptions)
	staticruntime "off"

	targetdir ("%{wks.location}/bin/" .. outputdir .. "/%{prj.name}")
	objdir ("%{wks.location}/bin-int/" .. outputdir .. "/%{prj.name}")

	files
	{
		"src/**.cpp"
	}

	includedirs
	{
		"%{wks.location}/../TomCatBuild/src",
		"%{wks.location}/../TomCat/vendor/spdlog/include",
		"%{wks.location}/../TomCat/src",
		"%{wks.location}/../TomCatEditor/src",
		"%{wks.location}/../TomCat/vendor",
		"%{IncludeDir.Box2D}",
		"%{IncludeDir.glm}",
		"%{IncludeDir.entt}",
		"%{IncludeDir.yaml_cpp}"
	}

	libdirs
	{
		"../../TomCat/vendor/Box2D/bin/" .. outputdir .. "/Box2D",
		"../../TomCat/vendor/GLFW/bin/" .. outputdir .. "/GLFW",
		"../../TomCat/vendor/Glad/bin/" .. outputdir .. "/Glad",
		"../../TomCat/vendor/ImGui/bin/" .. outputdir .. "/ImGui",
		"../../TomCat/vendor/yaml-cpp/bin/" .. outputdir .. "/yaml-cpp"
	}

	links { "TomCatBuild", "TomCatEditor", "TomCat", table.unpack(TomCatConsumerLinks) }
	buildoptions "/utf-8"

	filter "system:windows"
        postbuildcommands (TomCatConsumerPostBuildCommands)
		systemversion "latest"
		defines { "TC_PLATFORM_WINDOWS", "YAML_CPP_STATIC_DEFINE" }
		postbuildcommands {
			"if not exist \"$(ProjectDir)..\\..\\Editor\\TomCatInut\\Packages\\Resources\\Sprites\\TomCat\\Circle.tga\" (echo ERROR: Required CLI package asset is missing: Circle.tga & exit /b 1)",
			"if not exist \"$(ProjectDir)..\\..\\Editor\\TomCatInut\\Packages\\Resources\\Sprites\\TomCat\\Square.tga\" (echo ERROR: Required CLI package asset is missing: Square.tga & exit /b 1)",
			"if not exist \"$(ProjectDir)..\\..\\Editor\\TomCatInut\\Packages\\fonts\\opensans\\OpenSans-Regular.ttf\" (echo ERROR: Required CLI package asset is missing: OpenSans-Regular.ttf & exit /b 1)",
			"if not exist \"%{cfg.targetdir}\\Packages\\Resources\\Sprites\\TomCat\" mkdir \"%{cfg.targetdir}\\Packages\\Resources\\Sprites\\TomCat\"",
			"if not exist \"%{cfg.targetdir}\\Packages\\fonts\\opensans\" mkdir \"%{cfg.targetdir}\\Packages\\fonts\\opensans\"",
			"copy /Y \"$(ProjectDir)..\\..\\Editor\\TomCatInut\\Packages\\Resources\\Sprites\\TomCat\\Circle.tga\" \"%{cfg.targetdir}\\Packages\\Resources\\Sprites\\TomCat\\Circle.tga\" > nul",
			"copy /Y \"$(ProjectDir)..\\..\\Editor\\TomCatInut\\Packages\\Resources\\Sprites\\TomCat\\Square.tga\" \"%{cfg.targetdir}\\Packages\\Resources\\Sprites\\TomCat\\Square.tga\" > nul",
			"copy /Y \"$(ProjectDir)..\\..\\Editor\\TomCatInut\\Packages\\fonts\\opensans\\OpenSans-Regular.ttf\" \"%{cfg.targetdir}\\Packages\\fonts\\opensans\\OpenSans-Regular.ttf\" > nul"
		}

	filter "configurations:Debug"
		defines "TC_DEBUG"
		runtime "Debug"
		symbols "on"
		links { table.unpack(TomCatConsumerLinksDebug) }

	-- Debug links shaderc_sharedd.lib (Dependencies.lua); ship the matching DLL.
	filter { "system:windows", "configurations:Debug" }
		postbuildcommands {
			"if exist \"$(ProjectDir)..\\..\\vendor\\VulkanSDK\\Bin\\shaderc_sharedd.dll\" copy /Y \"$(ProjectDir)..\\..\\vendor\\VulkanSDK\\Bin\\shaderc_sharedd.dll\" \"%{cfg.targetdir}\\\" > nul"
		}

	filter "configurations:Release"
		defines "TC_RELEASE"
		runtime "Release"
		optimize "on"
		links { table.unpack(TomCatConsumerLinksRelease) }

	filter "configurations:Dist"
		defines "TC_DIST"
		runtime "Release"
		optimize "on"
		links { table.unpack(TomCatConsumerLinksRelease) }

	filter { "system:windows", "configurations:Release or Dist" }
		postbuildcommands {
			"if exist \"$(ProjectDir)..\\..\\vendor\\VulkanSDK\\Bin\\shaderc_shared.dll\" copy /Y \"$(ProjectDir)..\\..\\vendor\\VulkanSDK\\Bin\\shaderc_shared.dll\" \"%{cfg.targetdir}\\\" > nul"
		}
