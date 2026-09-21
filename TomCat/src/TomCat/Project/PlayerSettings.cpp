#include "tcpch.h"
#include "PlayerSettings.h"

#include "TomCat/Utils/PathUtils.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <string_view>

namespace TomCat {

	namespace {

		bool IsSafeRelativeDirectory(const std::filesystem::path& path)
		{
			if (path.empty() || path.is_absolute() || path.has_root_name()
				|| path.has_root_directory())
				return false;
			const std::filesystem::path normalized = path.lexically_normal();
			if (normalized.empty() || normalized == ".")
				return false;
			for (const auto& part : normalized)
			{
				if (part == "..")
					return false;
			}
			return true;
		}

		bool ValidateLabel(const std::string& value, std::string_view name,
			size_t maximumLength, bool pathSegment, std::string& errorMessage)
		{
			if (value.empty() || value.size() > maximumLength)
			{
				errorMessage = std::string(name) + " must contain between 1 and "
					+ std::to_string(maximumLength) + " UTF-8 bytes";
				return false;
			}
			for (unsigned char character : value)
			{
				if (character < 0x20 || character == 0x7f)
				{
					errorMessage = std::string(name) + " cannot contain control characters";
					return false;
				}
				if (pathSegment && (character == '<' || character == '>'
					|| character == ':' || character == '"' || character == '/'
					|| character == '\\' || character == '|' || character == '?'
					|| character == '*'))
				{
					errorMessage = std::string(name)
						+ " must be a valid single file-system path segment";
					return false;
				}
			}
			if (!pathSegment)
				return true;
			if (value == "." || value == ".." || value.back() == ' '
				|| value.back() == '.')
			{
				errorMessage = std::string(name)
					+ " must be a valid single file-system path segment";
				return false;
			}
			std::string base(value.substr(0, value.find('.')));
			std::transform(base.begin(), base.end(), base.begin(),
				[](unsigned char character)
				{
					return static_cast<char>(std::toupper(character));
				});
			if (base == "CON" || base == "PRN" || base == "AUX" || base == "NUL"
				|| (base.size() == 4 && (base.rfind("COM", 0) == 0
					|| base.rfind("LPT", 0) == 0)
					&& base[3] >= '1' && base[3] <= '9'))
			{
				errorMessage = std::string(name) + " uses a reserved file-system name";
				return false;
			}
			return true;
		}

	}

	const char* PlayerWindowModeToString(PlayerWindowMode mode)
	{
		switch (mode)
		{
			case PlayerWindowMode::Windowed: return "Windowed";
			case PlayerWindowMode::Borderless: return "Borderless";
			case PlayerWindowMode::ExclusiveFullscreen: return "ExclusiveFullscreen";
		}
		return "";
	}

	bool PlayerWindowModeFromString(const std::string& value, PlayerWindowMode& mode)
	{
		if (value == "Windowed")
			mode = PlayerWindowMode::Windowed;
		else if (value == "Borderless")
			mode = PlayerWindowMode::Borderless;
		else if (value == "ExclusiveFullscreen")
			mode = PlayerWindowMode::ExclusiveFullscreen;
		else
			return false;
		return true;
	}

	PlayerSettings MakeDefaultPlayerSettings(const std::string& productName,
		const std::string& version)
	{
		PlayerSettings settings;
		if (!productName.empty())
			settings.ProductName = productName;
		if (!version.empty())
			settings.Version = version;
		return settings;
	}

	bool NormalizeAndValidatePlayerSettings(PlayerSettings& settings,
		std::string& errorMessage)
	{
		if (!ValidateLabel(settings.ProductName, "PlayerSettings.ProductName", 128,
			true, errorMessage)
			|| !ValidateLabel(settings.CompanyName, "PlayerSettings.CompanyName", 128,
				true, errorMessage)
			|| !ValidateLabel(settings.Version, "PlayerSettings.Version", 64,
				false, errorMessage))
			return false;
		if (settings.Width < 320 || settings.Width > 16384
			|| settings.Height < 200 || settings.Height > 16384)
		{
			errorMessage = "PlayerSettings display size must be between 320x200 and 16384x16384";
			return false;
		}
		switch (settings.WindowMode)
		{
			case PlayerWindowMode::Windowed:
			case PlayerWindowMode::Borderless:
			case PlayerWindowMode::ExclusiveFullscreen:
				break;
			default:
				errorMessage = "PlayerSettings.WindowMode is invalid";
				return false;
		}

		std::array<std::pair<std::filesystem::path*, const char*>, 3> directories = {{
			{ &settings.SaveDirectory, "PlayerSettings.Directories.Save" },
			{ &settings.LogDirectory, "PlayerSettings.Directories.Log" },
			{ &settings.CrashDirectory, "PlayerSettings.Directories.Crash" }
		}};
		for (auto& [directory, name] : directories)
		{
			if (!IsSafeRelativeDirectory(*directory))
			{
				errorMessage = std::string(name)
					+ " must be a non-empty relative directory without '..'";
				return false;
			}
			*directory = directory->lexically_normal();
			if (PathToUTF8(*directory).size() > 512)
			{
				errorMessage = std::string(name) + " is too long";
				return false;
			}
		}
		errorMessage.clear();
		return true;
	}

}
