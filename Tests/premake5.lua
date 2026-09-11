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
