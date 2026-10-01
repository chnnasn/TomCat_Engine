include "./../vendor/premake/premake_customization/solution_items.lua"
include "../Dependencies.lua"

workspace "Tests"
	architecture "x64"
	startproject "PhysicsRegression"

	configurations
	{
		"Debug",
		"Release",
		"Dist"
	}

	flags
	{
		"MultiProcessorCompile"
	}

outputdir = "%{cfg.buildcfg}-%{cfg.system}-%{cfg.architecture}"

group "Dependencies"
	include "../vendor/premake"
	include "../TomCat/vendor/Box2D"
	include "../TomCat/vendor/GLFW"
	include "../TomCat/vendor/Glad"
	include "../TomCat/vendor/ImGui"
	include "../TomCat/vendor/yaml-cpp"
group ""

include "../TomCat"
include "PhysicsRegression"
include "SpriteAssetRegression"
include "ScriptCompilerRegression"
include "P0SafetyRegression"
include "SaveDataRegression"
include "SceneMigrationRegression"
include "TestModule"
include "ModuleSdkRegression"
include "EditorRecoveryRegression"
include "AudioRegression"
include "ImporterRegression"
include "InputRegression"
include "Advanced2DRegression"
include "ProfilerRegression"

-- Stage the shader compiler even for tests that reach it indirectly through Scene.
for _, name in ipairs({ "Advanced2DRegression", "AudioRegression",
    "EditorRecoveryRegression", "InputRegression", "ModuleSdkRegression",
    "P0SafetyRegression", "ProfilerRegression", "SaveDataRegression",
    "SceneMigrationRegression" }) do
    project(name)
    filter { "system:windows", "configurations:Debug" }
        postbuildcommands { '{COPYFILE} "$(ProjectDir)../../vendor/VulkanSDK/Bin/shaderc_sharedd.dll" "%{cfg.targetdir}"' }
    filter { "system:windows", "configurations:Release or Dist" }
        postbuildcommands { '{COPYFILE} "$(ProjectDir)../../vendor/VulkanSDK/Bin/shaderc_shared.dll" "%{cfg.targetdir}"' }
    filter {}
end
