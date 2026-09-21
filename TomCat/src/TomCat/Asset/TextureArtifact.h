#pragma once

#include "Asset.h"

#include <cstdint>
#include <filesystem>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace TomCat {

	enum class TextureArtifactFormat : uint32_t
	{
		RGBA8 = 1,
		BC3 = 2,
        RGBA32F = 3 // Linear HDR radiance, little-endian IEEE 754 floats.
	};

	struct TextureArtifactMip
	{
		uint32_t Width = 0;
		uint32_t Height = 0;
		std::span<const uint8_t> Bytes;
	};

	struct TextureArtifactView
	{
		uint32_t Width = 0;
		uint32_t Height = 0;
		bool SRGB = true;
		TextureArtifactFormat Format = TextureArtifactFormat::RGBA8;
		std::vector<TextureArtifactMip> Mips;
	};

	[[nodiscard]] bool IsTextureArtifact(std::span<const uint8_t> bytes);
	[[nodiscard]] bool ParseTextureArtifact(std::span<const uint8_t> bytes,
		TextureArtifactView& artifact, std::string& error);
	// Performs decoder header inspection without allocating the pixel surface and
	// returns a conservative peak reservation for decode, mip generation and the
	// final artifact. The same dimension limits are enforced by the builder.
	[[nodiscard]] bool EstimateTextureBuildMemory(
		std::span<const uint8_t> encodedSource, uint64_t& reservationBytes,
		std::string& error, uint32_t* width = nullptr, uint32_t* height = nullptr);
	[[nodiscard]] bool EstimateTextureBuildMemory(
		const std::filesystem::path& sourcePath, uint64_t& reservationBytes,
		std::string& error, uint32_t* width = nullptr, uint32_t* height = nullptr);
	[[nodiscard]] bool BuildTextureArtifact(std::span<const uint8_t> encodedSource,
		const AssetImportSettings& settings, std::string_view platform,
		std::vector<uint8_t>& artifact, std::string& error,
		uint32_t* decodedWidth = nullptr, uint32_t* decodedHeight = nullptr);
	[[nodiscard]] bool DecompressTextureMip(const TextureArtifactMip& mip,
		TextureArtifactFormat format, std::vector<uint8_t>& rgba,
		std::string& error);

}
