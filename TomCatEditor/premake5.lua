-- Editor-side engine services: scene undo history, crash recovery and the
-- project lock, packaged Editor runtime extraction, Hub project management and
-- the Scene view camera. The Player links only TomCat; the Editor, Hub,
-- TomCatCLI and tool-facing regressions link this library as well.
project "TomCatEditor"
	kind "StaticLib"
	language "C++"
	cppdialect "C++20"
	staticruntime "off"

	targetdir ("bin/" .. outputdir .. "/%{prj.name}")
	objdir ("bin-int/" .. outputdir .. "/%{prj.name}")

	files
	{
		"src/TomCat/**.h",
		"src/TomCat/**.cpp"
	}

	-- src/platform/Web is built only by Web/CMakeLists.txt (Emscripten).

	defines
	{
		"_CRT_SECURE_NO_WARNINGS",
		"GLFW_INCLUDE_NONE",
		"YAML_CPP_STATIC_DEFINE"
	}

	includedirs
	{
		"src",
		"%{wks.location}/../TomCat/src",
		"%{wks.location}/../TomCat/vendor/spdlog/include",
		"%{IncludeDir.Box2D}",
		"%{IncludeDir.GLFW}",
		"%{IncludeDir.glm}",
		"%{IncludeDir.entt}",
		"%{IncludeDir.yaml_cpp}"
	}

	links { "TomCat" }

	buildoptions { "/utf-8" }

	filter "system:windows"
		systemversion "latest"
		defines { "TC_PLATFORM_WINDOWS" }

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
