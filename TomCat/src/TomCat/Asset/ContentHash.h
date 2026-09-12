#pragma once

#include <cstdint>
#include <filesystem>
#include <functional>
#include <span>
#include <string>
#include <vector>

namespace TomCat {

	// Shared content hashing helpers used by importers and the derived-data cache.
	// Digests are always returned as 64 lowercase hexadecimal characters.
	[[nodiscard]] std::string ComputeContentSHA256(std::span<const uint8_t> bytes);
	[[nodiscard]] bool ComputeFileContentSHA256(const std::filesystem::path& path,
		std::string& digest, std::string& errorMessage,
		const std::function<bool()>& cancellationRequested = {});
	[[nodiscard]] bool ReadFileForImport(const std::filesystem::path& path,
		std::vector<uint8_t>& bytes, std::string& errorMessage,
		const std::function<bool()>& cancellationRequested = {});

}
