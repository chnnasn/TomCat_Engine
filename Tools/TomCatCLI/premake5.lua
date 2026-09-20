project "TomCatCLI"
	kind "ConsoleApp"
	language "C++"
	cppdialect "C++20"
	staticruntime "off"

	targetdir ("%{wks.location}/bin/" .. outputdir .. "/%{prj.name}")
	objdir ("%{wks.location}/bin-int/" .. outputdir .. "/%{prj.name}")

	files
	{
		"src/**.cpp",
		"../../Editor/TomCatInut/src/Player/PlayerBuilder.cpp",
		"../../Editor/TomCatInut/src/Player/PlayerBuilder.h",
		"../../Editor/TomCatInut/src/Scripting/ScriptProjectCompiler.cpp",
		"../../Editor/TomCatInut/src/Scripting/ScriptProjectCompiler.h",
		"../../Editor/TomCatInut/src/Scripting/ScriptMetadataCache.cpp",
		"../../Editor/TomCatInut/src/Scripting/ScriptMetadataCache.h"
	}

	includedirs
	{
		"%{wks.location}/../Editor/TomCatInut/src",
		"%{wks.location}/../TomCat/vendor/spdlog/include",
		"%{wks.location}/../TomCat/src",
		"%{wks.location}/../TomCat/vendor",
		"%{IncludeDir.Butter}",
		"%{IncludeDir.glm}",
		"%{IncludeDir.entt}",
		"%{IncludeDir.yaml_cpp}"
	}

	libdirs
	{
		"../../TomCat/vendor/GLFW/bin/" .. outputdir .. "/GLFW",
		"../../TomCat/vendor/Glad/bin/" .. outputdir .. "/Glad",
		"../../TomCat/vendor/ImGui/bin/" .. outputdir .. "/ImGui",
		"../../TomCat/vendor/yaml-cpp/bin/" .. outputdir .. "/yaml-cpp"
	}

	links { "TomCat", table.unpack(TomCatConsumerLinks) }
	buildoptions "/utf-8"

	filter "system:windows"
		systemversion "latest"
		defines { "TC_PLATFORM_WINDOWS", "YAML_CPP_STATIC_DEFINE" }
		postbuildcommands {
			"if exist \"$(ProjectDir)..\\..\\vendor\\VulkanSDK\\Bin\\shaderc_shared.dll\" copy /Y \"$(ProjectDir)..\\..\\vendor\\VulkanSDK\\Bin\\shaderc_shared.dll\" \"%{cfg.targetdir}\\\" > nul",
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
