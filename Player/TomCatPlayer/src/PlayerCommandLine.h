#pragma once

#include <TomCat/Core/Application.h>

#include <filesystem>
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
		std::string Error;
	};

	PlayerCommandLine ParsePlayerCommandLine(ApplicationCommandLineArgs args);
	std::filesystem::path GetPlayerExecutableDirectory();
	std::filesystem::path ResolvePlayerPackagePath(const std::filesystem::path& path);
	const char* GetPlayerHelpText();

}
