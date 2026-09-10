#pragma once

#include <filesystem>
#include <optional>

namespace TomCat {

	// Returns %LOCALAPPDATA%/TomCat/TomCatSettings for machine-local application state.
	// Failure never falls back to writing beside a packaged executable.
	std::optional<std::filesystem::path> GetTomCatSettingsRoot();

	class FileDialogs
	{
	public:

		static std::filesystem::path OpenFile(const char* filter);
		static std::filesystem::path SaveFile(const char* filter);
		static std::filesystem::path OpenFolder();

	private:

	};

}
