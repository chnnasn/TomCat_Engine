#pragma once

#include <TomCat/Core/Application.h>

#include <cstdint>
#include <filesystem>
#include <map>
#include <string>

namespace TomCat {

	enum class PlayerCommandMode
	{
		Run,
		ValidatePackage,
		ShowVersion,
		ShowHelp,
		Invalid
	};

	struct PlayerCommandLine
	{
		PlayerCommandMode Mode = PlayerCommandMode::Run;
		std::filesystem::path PackagePath;
		// Optional --render-diag <file.json>: the latest 2D scene collection
		// counters and draw order, rewritten atomically about once a second.
		std::filesystem::path RenderDiagnosticsPath;
		// --screenshot <out.png>: write the rendered frame --frames N (default
		// 60) as PNG, then exit. --frames alone exits after N frames.
		std::filesystem::path ScreenshotPath;
		uint32_t Frames = 0;
		// --headless: render into a hidden window. Unlike a visible window it
		// is never occluded, never takes focus and ignores DPI-scaled capture.
		bool Headless = false;
		// --fixed-delta <seconds>: advance every frame by exactly this much.
		float FixedDelta = 0.0f;
		// --test-arg key=value (repeatable), read by Application.GetTestArgument.
		std::map<std::string, std::string> TestArguments;
		std::string Error;
	};

	PlayerCommandLine ParsePlayerCommandLine(ApplicationCommandLineArgs args);
	std::filesystem::path GetPlayerExecutableDirectory();
	std::filesystem::path ResolvePlayerPackagePath(const std::filesystem::path& path);
	const char* GetPlayerHelpText();

}
