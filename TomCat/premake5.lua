project "TomCat"
	kind"StaticLib"
	language "C++"
	cppdialect"C++20"
	staticruntime "off"

targetdir ("../TomCat/bin/" .. outputdir .."/%{prj.name}")
objdir ("../TomCat/bin-int/" .. outputdir .."/%{prj.name}")

	pchheader "tcpch.h"
	pchsource "src/tcpch.cpp"

	files
	{
		"src/**.h",
		"src/**.cpp",
		"vendor/glm/glm/**.hpp",
		"vendor/glm/glm/**.inl",
		"vendor/stb_image/**.h",
		"vendor/stb_image/**.cpp",

		"vendor/ImGuizmo/ImGuizmo.h",
		"vendor/ImGuizmo/ImGuizmo.cpp"
	}

	defines
	{
		"_CRT_SECURE_NO_WARNINGS",
		"GLFW_INCLUDE_NONE",
		"YAML_CPP_STATIC_DEFINE"
	}


	includedirs
	{
		"src",
		"vendor/spdlog/include",
		"%{IncludeDir.Box2D}",
		"%{IncludeDir.GLFW}",
		"%{IncludeDir.ImGui}",
		"%{IncludeDir.Glad}",
		"%{IncludeDir.glm}",
		"%{IncludeDir.stb_image}",
		"%{IncludeDir.entt}",
		"%{IncludeDir.yaml_cpp}",


		"%{IncludeDir.ImGuizmo}",
		"%{IncludeDir.VulkanSDK}"
	}

	dependson
	{
		"Box2D",
		"GLFW",
		"Glad",
		"ImGui",
		"yaml-cpp"
	}
	buildoptions "/utf-8"

filter "files:vendor/ImGuizmo/**.cpp"
    flags { "NoPCH" }


	filter "system:windows"
		systemversion "latest"

	defines
	{
		"TC_PLATFORM_WINDOWS",
	}

	filter "configurations:Release"
		defines "TC_RELEASE"
		runtime "Release"
		optimize "on"

	filter "configurations:Dist"
		defines "TC_DIST"
		runtime "Release"
		optimize "on"

	filter "configurations:Debug"
		defines "TC_DEBUG"
		runtime "Debug"
		symbols "on"

