project "TomCat"
	kind"StaticLib"
	language "C++"
	cppdialect"C++20"
	staticruntime "on"

	targetdir ("%{wks.location}/bin/" .. outputdir .."/%{prj.name}")
	objdir ("%{wks.location}/bin-int/" .. outputdir .."/%{prj.name}")

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
		"GLFW_INCLUDE_NONE"
	}

	includedirs
	{
		"src",
		"vendor/spdlog/include",
		"%{IncludeDir.GLFW}",
		"%{IncludeDir.ImGui}",
		"%{IncludeDir.Glad}",
		"%{IncludeDir.glm}",
		"%{IncludeDir.stb_image}",
		"%{IncludeDir.entt}",
		"%{IncludeDir.yaml_cpp}",
		"%{IncludeDir.ImGuizmo}"
	}

	
	links 
	{ 
		"GLFW",
		"Glad",
		"ImGui",
		"yaml-cpp",
		"opengl32.lib"
	}
	buildoptions "/utf-8"

filter "files:vendor/ImGuizmo/**.cpp"
    flags { "NoPCH" }


	filter "system:windows"
		systemversion "latest"

	defines
	{
		"TC_PLAYTFORM_WINDOWS",
		"TC_BUILD_DLL",
		"GLFW_INCLUDE_NONE",
		"IMGUI_API=_declspec(dllexport);"
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

