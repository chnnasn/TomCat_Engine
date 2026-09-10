#pragma once

#include <filesystem>
#include <string>
#include <string_view>

namespace TomCat::FileSystem {

	// Returns a non-existing path next to destination. Keeping the temporary file
	// on the same volume is required for the final replacement to be atomic.
	[[nodiscard]] std::filesystem::path MakeTemporarySiblingPath(const std::filesystem::path& destination);

	// Installs a completed sibling temporary file without exposing a partially
	// written destination. The caller retains ownership of temporary on failure.
	[[nodiscard]] bool InstallTemporaryFileAtomically(const std::filesystem::path& temporary,
		const std::filesystem::path& destination, std::string& errorMessage);

	// Writes bytes to a sibling temporary file and atomically installs it. If
	// installation fails, the completed temporary file is retained for recovery.
	[[nodiscard]] bool WriteFileAtomically(const std::filesystem::path& destination,
		std::string_view contents, std::string& errorMessage);

}
