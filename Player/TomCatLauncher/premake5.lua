project "TomCatLauncher"
    kind "WindowedApp"
    language "C++"
    cppdialect "C++17"
    staticruntime "on"
    characterset "Unicode"
    targetdir ("%{wks.location}/bin/" .. outputdir .. "/%{prj.name}")
    objdir ("%{wks.location}/bin-int/" .. outputdir .. "/%{prj.name}")
    files { "main.cpp" }
    systemversion "latest"
