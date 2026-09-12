#pragma once

#include "TomCat/Asset/Asset.h"
#include "TomCat/Core/Version.h"

#include <cstdint>
#include <filesystem>
#include <string>

namespace TomCat {

	enum class PlayerWindowMode : uint32_t
	{
		Windowed = 0,
		Borderless = 1,
		ExclusiveFullscreen = 2
	};

	struct PlayerSettings
	{
		std::string ProductName = "TomCat Game";
		std::string CompanyName = "DefaultCompany";
		std::string Version = std::string(TomCat::Version::ProductVersion);
		AssetHandle Icon = AssetHandle(0);
		uint32_t Width = 1280;
		uint32_t Height = 720;
		PlayerWindowMode WindowMode = PlayerWindowMode::Windowed;
		bool Resizable = true;
		bool VSync = true;
		std::filesystem::path SaveDirectory = "Saves";
		std::filesystem::path LogDirectory = "Logs";
		std::filesystem::path CrashDirectory = "Crashes";

		bool operator==(const PlayerSettings&) const = default;
	};

	const char* PlayerWindowModeToString(PlayerWindowMode mode);
	bool PlayerWindowModeFromString(const std::string& value, PlayerWindowMode& mode);
	PlayerSettings MakeDefaultPlayerSettings(const std::string& productName,
		const std::string& version);
	bool NormalizeAndValidatePlayerSettings(PlayerSettings& settings,
		std::string& errorMessage);

}
