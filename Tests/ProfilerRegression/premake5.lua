project "ProfilerRegression"
	kind "ConsoleApp"
	language "C++"
	cppdialect "C++20"
	staticruntime "off"
	targetdir ("%{wks.location}/bin/" .. outputdir .. "/%{prj.name}")
	objdir ("%{wks.location}/bin-int/" .. outputdir .. "/%{prj.name}")
	files { "src/**.cpp", "../../TomCat/src/platform/OpenGL/OpenGLProfiler.cpp" }
	includedirs { "../../TomCat/src", "../../TomCat/vendor/spdlog/include", "../../TomCat/vendor/Glad/include", "../../TomCat/vendor/glm" }
	buildoptions "/utf-8"
	filter "system:windows"
		systemversion "latest"
	filter "configurations:Debug"
		runtime "Debug"
		symbols "on"
	filter "configurations:Release or Dist"
		runtime "Release"
		optimize "on"
