#pragma once

#include <array>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <iosfwd>
#include <span>
#include <string>
#include <vector>

namespace TomCat {

	using ContentSHA256Digest = std::array<uint8_t, 32>;

	// Shared content hashing helpers used by importers and the derived-data cache.
	// Digests are always returned as 64 lowercase hexadecimal characters.
	[[nodiscard]] ContentSHA256Digest ComputeContentSHA256Digest(
		std::span<const uint8_t> bytes);
	[[nodiscard]] std::string ComputeContentSHA256(std::span<const uint8_t> bytes);
	[[nodiscard]] bool ComputeStreamRangeContentSHA256(std::istream& input,
		uint64_t offset, uint64_t size, ContentSHA256Digest& digest);
	[[nodiscard]] bool VerifyContentSHA256(std::span<const uint8_t> bytes,
		const ContentSHA256Digest& expectedDigest);
	[[nodiscard]] bool VerifyStreamRangeContentSHA256(std::istream& input,
		uint64_t offset, uint64_t size,
		const ContentSHA256Digest& expectedDigest);
	[[nodiscard]] bool ComputeFileContentSHA256(const std::filesystem::path& path,
		std::string& digest, std::string& errorMessage,
		const std::function<bool()>& cancellationRequested = {});
	[[nodiscard]] bool ReadFileForImport(const std::filesystem::path& path,
		std::vector<uint8_t>& bytes, std::string& errorMessage,
		const std::function<bool()>& cancellationRequested = {});

}
