#include "tcpch.h"
#include "TextureArtifact.h"

#include <stb_image.h>

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <cstring>
#include <fstream>
#include <limits>

namespace TomCat {

	namespace {
		constexpr std::array<uint8_t, 4> Magic = { 'T', 'C', 'T', 'X' };
		constexpr uint16_t Version = 1;
		constexpr uint32_t HeaderSize = 32;
		constexpr uint32_t MipEntrySize = 24;
		constexpr uint32_t SRGBFlag = 1;
		constexpr uint32_t MaximumTextureDimension = 16'384;
		constexpr uint64_t MaximumTexturePixels = 64ULL * 1024ULL * 1024ULL;
		constexpr uint64_t ReservationSafetyBytes = 1024ULL * 1024ULL;

		bool CheckedAdd(uint64_t left, uint64_t right, uint64_t& result)
		{
			if (left > (std::numeric_limits<uint64_t>::max)() - right)
				return false;
			result = left + right;
			return true;
		}

		bool ComputeTextureReservation(uint32_t width, uint32_t height,
			uint64_t encodedBytes, uint64_t& reservationBytes, std::string& error)
		{
			reservationBytes = 0;
			if (width == 0 || height == 0 || width > MaximumTextureDimension
				|| height > MaximumTextureDimension
				|| static_cast<uint64_t>(width) * height > MaximumTexturePixels)
			{
				error = "texture dimensions exceed the 16384-axis/64-megapixel import limit";
				return false;
			}
			uint64_t rgbaMipBytes = 0;
			uint32_t mipWidth = width, mipHeight = height;
			uint32_t mipCount = 0;
			for (;;)
			{
				const uint64_t mipBytes = static_cast<uint64_t>(mipWidth)
					* mipHeight * 4;
				if (!CheckedAdd(rgbaMipBytes, mipBytes, rgbaMipBytes))
				{
					error = "texture mip memory estimate overflowed";
					return false;
				}
				++mipCount;
				if (mipWidth == 1 && mipHeight == 1)
					break;
				mipWidth = std::max(1u, mipWidth / 2);
				mipHeight = std::max(1u, mipHeight / 2);
			}
			uint64_t workingBytes = 0;
			// Six full RGBA mip chains cover the persistent mip/artifact buffers,
			// vector growth and stb's format-specific decode workspace (including
			// the wider temporary representation used by HDR inputs).
			if (rgbaMipBytes > (std::numeric_limits<uint64_t>::max)() / 6
				|| !CheckedAdd(encodedBytes, rgbaMipBytes * 6, workingBytes)
				|| !CheckedAdd(workingBytes,
					HeaderSize + static_cast<uint64_t>(mipCount) * MipEntrySize,
					workingBytes)
				|| !CheckedAdd(workingBytes, ReservationSafetyBytes, workingBytes))
			{
				error = "texture build memory estimate overflowed";
				return false;
			}
			reservationBytes = workingBytes;
			return true;
		}

		int ReadImageInfo(void* user, char* data, int size)
		{
			if (!user || !data || size <= 0)
				return 0;
			try
			{
				auto& input = *static_cast<std::ifstream*>(user);
				input.read(data, size);
				return static_cast<int>(input.gcount());
			}
			catch (...) { return 0; }
		}

		void SkipImageInfo(void* user, int count)
		{
			if (!user)
				return;
			try
			{
				auto& input = *static_cast<std::ifstream*>(user);
				input.clear();
				input.seekg(static_cast<std::streamoff>(count), std::ios::cur);
			}
			catch (...) {}
		}

		int EndOfImageInfo(void* user)
		{
			return !user || static_cast<std::ifstream*>(user)->eof() ? 1 : 0;
		}

		void AppendU16(std::vector<uint8_t>& output, uint16_t value)
		{
			output.push_back(static_cast<uint8_t>(value));
			output.push_back(static_cast<uint8_t>(value >> 8));
		}

		void AppendU32(std::vector<uint8_t>& output, uint32_t value)
		{
			for (uint32_t shift = 0; shift < 32; shift += 8)
				output.push_back(static_cast<uint8_t>(value >> shift));
		}

		void AppendU64(std::vector<uint8_t>& output, uint64_t value)
		{
			for (uint32_t shift = 0; shift < 64; shift += 8)
				output.push_back(static_cast<uint8_t>(value >> shift));
		}

		bool ReadU16(std::span<const uint8_t> bytes, size_t offset, uint16_t& value)
		{
			if (offset > bytes.size() || bytes.size() - offset < 2) return false;
			value = static_cast<uint16_t>(bytes[offset])
				| static_cast<uint16_t>(bytes[offset + 1] << 8);
			return true;
		}

		bool ReadU32(std::span<const uint8_t> bytes, size_t offset, uint32_t& value)
		{
			if (offset > bytes.size() || bytes.size() - offset < 4) return false;
			value = static_cast<uint32_t>(bytes[offset])
				| (static_cast<uint32_t>(bytes[offset + 1]) << 8)
				| (static_cast<uint32_t>(bytes[offset + 2]) << 16)
				| (static_cast<uint32_t>(bytes[offset + 3]) << 24);
			return true;
		}

		bool ReadU64(std::span<const uint8_t> bytes, size_t offset, uint64_t& value)
		{
			if (offset > bytes.size() || bytes.size() - offset < 8) return false;
			value = 0;
			for (uint32_t shift = 0; shift < 64; shift += 8)
				value |= static_cast<uint64_t>(bytes[offset + shift / 8]) << shift;
			return true;
		}

		std::string Lower(std::string value)
		{
			std::transform(value.begin(), value.end(), value.begin(),
				[](unsigned char item) { return static_cast<char>(std::tolower(item)); });
			return value;
		}

		const std::string* FindSetting(const AssetImportSettings& settings,
			std::initializer_list<const char*> names)
		{
			for (const char* name : names)
			{
				const auto value = settings.find(name);
				if (value != settings.end()) return &value->second;
			}
			return nullptr;
		}

		bool ParseBool(const std::string* value, bool fallback, bool& output)
		{
			if (!value) { output = fallback; return true; }
			const std::string normalized = Lower(*value);
			if (normalized == "true" || normalized == "1" || normalized == "yes")
				output = true;
			else if (normalized == "false" || normalized == "0" || normalized == "no")
				output = false;
			else return false;
			return true;
		}

		float SRGBToLinear(float value)
		{
			return value <= 0.04045f ? value / 12.92f
				: std::pow((value + 0.055f) / 1.055f, 2.4f);
		}

		float LinearToSRGB(float value)
		{
			return value <= 0.0031308f ? value * 12.92f
				: 1.055f * std::pow(value, 1.0f / 2.4f) - 0.055f;
		}

		std::vector<uint8_t> BuildNextMip(std::span<const uint8_t> source,
			uint32_t width, uint32_t height, bool srgb)
		{
			const uint32_t nextWidth = std::max(1u, width / 2);
			const uint32_t nextHeight = std::max(1u, height / 2);
			std::vector<uint8_t> result(static_cast<size_t>(nextWidth)
				* nextHeight * 4);
			for (uint32_t y = 0; y < nextHeight; ++y)
			{
				for (uint32_t x = 0; x < nextWidth; ++x)
				{
					float sum[4]{};
					uint32_t samples = 0;
					for (uint32_t oy = 0; oy < 2; ++oy)
					for (uint32_t ox = 0; ox < 2; ++ox)
					{
						const uint32_t sx = std::min(width - 1, x * 2 + ox);
						const uint32_t sy = std::min(height - 1, y * 2 + oy);
						const size_t index = (static_cast<size_t>(sy) * width + sx) * 4;
						for (uint32_t channel = 0; channel < 4; ++channel)
						{
							float value = source[index + channel] / 255.0f;
							if (srgb && channel < 3) value = SRGBToLinear(value);
							sum[channel] += value;
						}
						++samples;
					}
					const size_t destination = (static_cast<size_t>(y) * nextWidth + x) * 4;
					for (uint32_t channel = 0; channel < 4; ++channel)
					{
						float value = sum[channel] / samples;
						if (srgb && channel < 3) value = LinearToSRGB(value);
						result[destination + channel] = static_cast<uint8_t>(
							std::clamp(std::lround(value * 255.0f), 0l, 255l));
					}
				}
			}
			return result;
		}

		uint16_t Pack565(const uint8_t* color)
		{
			return static_cast<uint16_t>(((color[0] >> 3) << 11)
				| ((color[1] >> 2) << 5) | (color[2] >> 3));
		}

		std::array<uint8_t, 3> Unpack565(uint16_t value)
		{
			return { static_cast<uint8_t>(((value >> 11) & 31) * 255 / 31),
				static_cast<uint8_t>(((value >> 5) & 63) * 255 / 63),
				static_cast<uint8_t>((value & 31) * 255 / 31) };
		}

		uint32_t ColorDistance(const uint8_t* value,
			const std::array<uint8_t, 3>& candidate)
		{
			const int r = static_cast<int>(value[0]) - candidate[0];
			const int g = static_cast<int>(value[1]) - candidate[1];
			const int b = static_cast<int>(value[2]) - candidate[2];
			return static_cast<uint32_t>(r * r + g * g + b * b);
		}

		std::vector<uint8_t> CompressBC3(std::span<const uint8_t> rgba,
			uint32_t width, uint32_t height)
		{
			const uint32_t blocksX = (width + 3) / 4;
			const uint32_t blocksY = (height + 3) / 4;
			std::vector<uint8_t> output(static_cast<size_t>(blocksX) * blocksY * 16);
			for (uint32_t by = 0; by < blocksY; ++by)
			for (uint32_t bx = 0; bx < blocksX; ++bx)
			{
				std::array<std::array<uint8_t, 4>, 16> pixels{};
				uint8_t minimumAlpha = 255, maximumAlpha = 0;
				uint32_t darkest = 0, brightest = 0;
				uint32_t darkestValue = (std::numeric_limits<uint32_t>::max)();
				uint32_t brightestValue = 0;
				for (uint32_t index = 0; index < 16; ++index)
				{
					const uint32_t x = std::min(width - 1, bx * 4 + index % 4);
					const uint32_t y = std::min(height - 1, by * 4 + index / 4);
					std::memcpy(pixels[index].data(),
						rgba.data() + (static_cast<size_t>(y) * width + x) * 4, 4);
					minimumAlpha = std::min(minimumAlpha, pixels[index][3]);
					maximumAlpha = std::max(maximumAlpha, pixels[index][3]);
					const uint32_t luminance = pixels[index][0] * 77
						+ pixels[index][1] * 150 + pixels[index][2] * 29;
					if (luminance < darkestValue) { darkestValue = luminance; darkest = index; }
					if (luminance >= brightestValue) { brightestValue = luminance; brightest = index; }
				}
				uint8_t* block = output.data()
					+ (static_cast<size_t>(by) * blocksX + bx) * 16;
				block[0] = maximumAlpha; block[1] = minimumAlpha;
				std::array<uint8_t, 8> alphaPalette{};
				alphaPalette[0] = maximumAlpha; alphaPalette[1] = minimumAlpha;
				for (uint32_t index = 1; index <= 6; ++index)
					alphaPalette[index + 1] = static_cast<uint8_t>(
						((7 - index) * maximumAlpha + index * minimumAlpha) / 7);
				uint64_t alphaBits = 0;
				for (uint32_t index = 0; index < 16; ++index)
				{
					uint32_t best = 0, distance = 256;
					for (uint32_t candidate = 0; candidate < 8; ++candidate)
					{
						const uint32_t current = static_cast<uint32_t>(std::abs(
							static_cast<int>(pixels[index][3]) - alphaPalette[candidate]));
						if (current < distance) { distance = current; best = candidate; }
					}
					alphaBits |= static_cast<uint64_t>(best) << (index * 3);
				}
				for (uint32_t byte = 0; byte < 6; ++byte)
					block[2 + byte] = static_cast<uint8_t>(alphaBits >> (byte * 8));

				uint16_t color0 = Pack565(pixels[brightest].data());
				uint16_t color1 = Pack565(pixels[darkest].data());
				if (color0 <= color1)
				{
					if (color1 != 0xffffu) color0 = static_cast<uint16_t>(color1 + 1);
					else color1 = static_cast<uint16_t>(color0 - 1);
				}
				block[8] = static_cast<uint8_t>(color0);
				block[9] = static_cast<uint8_t>(color0 >> 8);
				block[10] = static_cast<uint8_t>(color1);
				block[11] = static_cast<uint8_t>(color1 >> 8);
				std::array<std::array<uint8_t, 3>, 4> palette{};
				palette[0] = Unpack565(color0); palette[1] = Unpack565(color1);
				for (uint32_t channel = 0; channel < 3; ++channel)
				{
					palette[2][channel] = static_cast<uint8_t>((2 * palette[0][channel]
						+ palette[1][channel]) / 3);
					palette[3][channel] = static_cast<uint8_t>((palette[0][channel]
						+ 2 * palette[1][channel]) / 3);
				}
				uint32_t colorBits = 0;
				for (uint32_t index = 0; index < 16; ++index)
				{
					uint32_t best = 0, distance = (std::numeric_limits<uint32_t>::max)();
					for (uint32_t candidate = 0; candidate < 4; ++candidate)
					{
						const uint32_t current = ColorDistance(pixels[index].data(), palette[candidate]);
						if (current < distance) { distance = current; best = candidate; }
					}
					colorBits |= best << (index * 2);
				}
				for (uint32_t byte = 0; byte < 4; ++byte)
					block[12 + byte] = static_cast<uint8_t>(colorBits >> (byte * 8));
			}
			return output;
		}

	}

	bool EstimateTextureBuildMemory(std::span<const uint8_t> encodedSource,
		uint64_t& reservationBytes, std::string& error, uint32_t* width,
		uint32_t* height)
	{
		reservationBytes = 0;
		error.clear();
		if (width) *width = 0;
		if (height) *height = 0;
		if (encodedSource.empty() || encodedSource.size()
			> static_cast<size_t>((std::numeric_limits<int>::max)()))
		{
			error = "source image is empty or exceeds the decoder limit";
			return false;
		}
		int decodedWidth = 0, decodedHeight = 0, channels = 0;
		if (!stbi_info_from_memory(encodedSource.data(),
			static_cast<int>(encodedSource.size()), &decodedWidth, &decodedHeight,
			&channels) || decodedWidth <= 0 || decodedHeight <= 0)
		{
			error = std::string("source image header is invalid: ")
				+ (stbi_failure_reason() ? stbi_failure_reason() : "unknown error");
			return false;
		}
		if (!ComputeTextureReservation(static_cast<uint32_t>(decodedWidth),
			static_cast<uint32_t>(decodedHeight), encodedSource.size(),
			reservationBytes, error))
			return false;
		if (width) *width = static_cast<uint32_t>(decodedWidth);
		if (height) *height = static_cast<uint32_t>(decodedHeight);
		return true;
	}

	bool EstimateTextureBuildMemory(const std::filesystem::path& sourcePath,
		uint64_t& reservationBytes, std::string& error, uint32_t* width,
		uint32_t* height)
	{
		reservationBytes = 0;
		error.clear();
		if (width) *width = 0;
		if (height) *height = 0;
		std::error_code fileError;
		const uintmax_t fileBytes = std::filesystem::file_size(sourcePath, fileError);
		if (fileError || fileBytes == 0 || fileBytes
			> static_cast<uintmax_t>((std::numeric_limits<int>::max)()))
		{
			error = "source image is missing, empty, or exceeds the decoder limit";
			return false;
		}
		std::ifstream input(sourcePath, std::ios::binary);
		if (!input)
		{
			error = "source image could not be opened for header inspection";
			return false;
		}
		const stbi_io_callbacks callbacks = {
			ReadImageInfo, SkipImageInfo, EndOfImageInfo };
		int decodedWidth = 0, decodedHeight = 0, channels = 0;
		if (!stbi_info_from_callbacks(&callbacks, &input, &decodedWidth,
			&decodedHeight, &channels) || decodedWidth <= 0 || decodedHeight <= 0)
		{
			error = std::string("source image header is invalid: ")
				+ (stbi_failure_reason() ? stbi_failure_reason() : "unknown error");
			return false;
		}
		if (!ComputeTextureReservation(static_cast<uint32_t>(decodedWidth),
			static_cast<uint32_t>(decodedHeight), static_cast<uint64_t>(fileBytes),
			reservationBytes, error))
			return false;
		if (width) *width = static_cast<uint32_t>(decodedWidth);
		if (height) *height = static_cast<uint32_t>(decodedHeight);
		return true;
	}

	bool IsTextureArtifact(std::span<const uint8_t> bytes)
	{
		return bytes.size() >= Magic.size()
			&& std::equal(Magic.begin(), Magic.end(), bytes.begin());
	}

	bool ParseTextureArtifact(std::span<const uint8_t> bytes,
		TextureArtifactView& artifact, std::string& error)
	{
		artifact = {};
		error.clear();
		uint16_t version = 0, headerSize = 0;
		uint32_t width = 0, height = 0, mipCount = 0, rawFormat = 0,
			flags = 0, tableOffset = 0;
		if (!IsTextureArtifact(bytes) || !ReadU16(bytes, 4, version)
			|| !ReadU16(bytes, 6, headerSize) || !ReadU32(bytes, 8, width)
			|| !ReadU32(bytes, 12, height) || !ReadU32(bytes, 16, mipCount)
			|| !ReadU32(bytes, 20, rawFormat) || !ReadU32(bytes, 24, flags)
			|| !ReadU32(bytes, 28, tableOffset))
		{
			error = "texture artifact header is truncated";
			return false;
		}
		if (version != Version || headerSize != HeaderSize || tableOffset != HeaderSize
			|| width == 0 || height == 0 || mipCount == 0 || mipCount > 32
			|| width > MaximumTextureDimension || height > MaximumTextureDimension
			|| static_cast<uint64_t>(width) * height > MaximumTexturePixels
			|| flags & ~SRGBFlag || (rawFormat != 1 && rawFormat != 2)
			|| static_cast<uint64_t>(tableOffset) + static_cast<uint64_t>(mipCount)
				* MipEntrySize > bytes.size())
		{
			error = "texture artifact header is invalid or unsupported";
			return false;
		}
		artifact.Width = width; artifact.Height = height;
		artifact.SRGB = (flags & SRGBFlag) != 0;
		artifact.Format = static_cast<TextureArtifactFormat>(rawFormat);
		artifact.Mips.reserve(mipCount);
		uint32_t expectedWidth = width, expectedHeight = height;
		uint64_t previousEnd = static_cast<uint64_t>(tableOffset)
			+ static_cast<uint64_t>(mipCount) * MipEntrySize;
		for (uint32_t index = 0; index < mipCount; ++index)
		{
			const size_t entry = tableOffset + static_cast<size_t>(index) * MipEntrySize;
			uint32_t mipWidth = 0, mipHeight = 0;
			uint64_t offset = 0, size = 0;
			if (!ReadU32(bytes, entry, mipWidth) || !ReadU32(bytes, entry + 4, mipHeight)
				|| !ReadU64(bytes, entry + 8, offset) || !ReadU64(bytes, entry + 16, size)
				|| mipWidth != expectedWidth || mipHeight != expectedHeight
				|| offset != previousEnd || offset > bytes.size()
				|| size > bytes.size() - offset)
			{
				error = "texture artifact mip table is invalid";
				return false;
			}
			const uint64_t expectedSize = artifact.Format == TextureArtifactFormat::RGBA8
				? static_cast<uint64_t>(mipWidth) * mipHeight * 4
				: static_cast<uint64_t>((mipWidth + 3) / 4)
					* ((mipHeight + 3) / 4) * 16;
			if (size != expectedSize)
			{
				error = "texture artifact mip byte count is invalid";
				return false;
			}
			artifact.Mips.push_back({ mipWidth, mipHeight,
				bytes.subspan(static_cast<size_t>(offset), static_cast<size_t>(size)) });
			previousEnd = offset + size;
			expectedWidth = std::max(1u, expectedWidth / 2);
			expectedHeight = std::max(1u, expectedHeight / 2);
		}
		if (previousEnd != bytes.size())
		{
			error = "texture artifact has trailing or unreferenced bytes";
			return false;
		}
		return true;
	}

	bool BuildTextureArtifact(std::span<const uint8_t> encodedSource,
		const AssetImportSettings& settings, std::string_view platform,
		std::vector<uint8_t>& artifact, std::string& error,
		uint32_t* decodedWidth, uint32_t* decodedHeight)
	{
		artifact.clear(); error.clear();
		if (encodedSource.empty() || encodedSource.size()
			> static_cast<size_t>((std::numeric_limits<int>::max)()))
		{
			error = "source image is empty or exceeds the decoder limit";
			return false;
		}
		bool srgb = true, mipmaps = true;
		if (!ParseBool(FindSetting(settings, { "sRGB", "srgb" }), true, srgb)
			|| !ParseBool(FindSetting(settings, { "generateMipmaps", "mipmaps" }),
				true, mipmaps))
		{
			error = "texture boolean import setting is invalid";
			return false;
		}
		if (const std::string* colorSpace = FindSetting(settings, { "colorSpace" }))
		{
			const std::string value = Lower(*colorSpace);
			if (value == "srgb") srgb = true;
			else if (value == "linear") srgb = false;
			else { error = "colorSpace must be sRGB or Linear"; return false; }
		}
		TextureArtifactFormat format = platform == "windows-x64"
			? TextureArtifactFormat::BC3 : TextureArtifactFormat::RGBA8;
		if (const std::string* compression = FindSetting(settings, { "compression" }))
		{
			const std::string value = Lower(*compression);
			if (value == "auto") {}
			else if (value == "rgba8" || value == "none")
				format = TextureArtifactFormat::RGBA8;
			else if (value == "bc3" || value == "dxt5")
				format = TextureArtifactFormat::BC3;
			else { error = "compression must be Auto, RGBA8 or BC3"; return false; }
		}

		uint64_t estimatedMemory = 0;
		uint32_t inspectedWidth = 0, inspectedHeight = 0;
		if (!EstimateTextureBuildMemory(encodedSource, estimatedMemory, error,
			&inspectedWidth, &inspectedHeight))
			return false;
		(void)estimatedMemory;

		int width = 0, height = 0, channels = 0;
		stbi_set_flip_vertically_on_load_thread(1);
		stbi_uc* decoded = stbi_load_from_memory(encodedSource.data(),
			static_cast<int>(encodedSource.size()), &width, &height, &channels,
			STBI_rgb_alpha);
		if (!decoded || width <= 0 || height <= 0
			|| static_cast<uint32_t>(width) != inspectedWidth
			|| static_cast<uint32_t>(height) != inspectedHeight)
		{
			error = std::string("source image could not be decoded: ")
				+ (stbi_failure_reason() ? stbi_failure_reason() : "unknown error");
			if (decoded) stbi_image_free(decoded);
			return false;
		}
		const uint64_t baseBytes = static_cast<uint64_t>(width) * height * 4;
		if (baseBytes > (std::numeric_limits<size_t>::max)())
		{
			stbi_image_free(decoded); error = "decoded texture is too large"; return false;
		}
		std::vector<std::vector<uint8_t>> rgbaMips;
		rgbaMips.emplace_back(decoded, decoded + static_cast<size_t>(baseBytes));
		stbi_image_free(decoded);
		uint32_t mipWidth = static_cast<uint32_t>(width);
		uint32_t mipHeight = static_cast<uint32_t>(height);
		while (mipmaps && (mipWidth > 1 || mipHeight > 1))
		{
			rgbaMips.push_back(BuildNextMip(rgbaMips.back(), mipWidth, mipHeight, srgb));
			mipWidth = std::max(1u, mipWidth / 2);
			mipHeight = std::max(1u, mipHeight / 2);
		}
		std::vector<std::vector<uint8_t>> payloads;
		payloads.reserve(rgbaMips.size());
		mipWidth = static_cast<uint32_t>(width); mipHeight = static_cast<uint32_t>(height);
		for (auto& mip : rgbaMips)
		{
			payloads.push_back(format == TextureArtifactFormat::BC3
				? CompressBC3(mip, mipWidth, mipHeight) : std::move(mip));
			mipWidth = std::max(1u, mipWidth / 2);
			mipHeight = std::max(1u, mipHeight / 2);
		}
		uint64_t artifactBytes = HeaderSize
			+ static_cast<uint64_t>(payloads.size()) * MipEntrySize;
		for (const auto& payload : payloads)
		{
			if (!CheckedAdd(artifactBytes, payload.size(), artifactBytes))
			{
				error = "texture artifact byte count overflowed";
				return false;
			}
		}
		if (artifactBytes > (std::numeric_limits<size_t>::max)())
		{
			error = "texture artifact is too large for this platform";
			return false;
		}
		artifact.reserve(static_cast<size_t>(artifactBytes));
		artifact.insert(artifact.end(), Magic.begin(), Magic.end());
		AppendU16(artifact, Version); AppendU16(artifact, HeaderSize);
		AppendU32(artifact, static_cast<uint32_t>(width));
		AppendU32(artifact, static_cast<uint32_t>(height));
		AppendU32(artifact, static_cast<uint32_t>(payloads.size()));
		AppendU32(artifact, static_cast<uint32_t>(format));
		AppendU32(artifact, srgb ? SRGBFlag : 0); AppendU32(artifact, HeaderSize);
		uint64_t offset = HeaderSize + static_cast<uint64_t>(payloads.size()) * MipEntrySize;
		mipWidth = static_cast<uint32_t>(width); mipHeight = static_cast<uint32_t>(height);
		for (const auto& payload : payloads)
		{
			AppendU32(artifact, mipWidth); AppendU32(artifact, mipHeight);
			AppendU64(artifact, offset); AppendU64(artifact, payload.size());
			offset += payload.size();
			mipWidth = std::max(1u, mipWidth / 2);
			mipHeight = std::max(1u, mipHeight / 2);
		}
		for (const auto& payload : payloads)
			artifact.insert(artifact.end(), payload.begin(), payload.end());
		if (decodedWidth) *decodedWidth = static_cast<uint32_t>(width);
		if (decodedHeight) *decodedHeight = static_cast<uint32_t>(height);
		return true;
	}

	bool DecompressTextureMip(const TextureArtifactMip& mip,
		TextureArtifactFormat format, std::vector<uint8_t>& rgba,
		std::string& error)
	{
		error.clear(); rgba.clear();
		if (format == TextureArtifactFormat::RGBA8)
		{
			rgba.assign(mip.Bytes.begin(), mip.Bytes.end());
			return true;
		}
		if (format != TextureArtifactFormat::BC3 || mip.Width == 0 || mip.Height == 0)
		{
			error = "unsupported compressed texture format"; return false;
		}
		rgba.resize(static_cast<size_t>(mip.Width) * mip.Height * 4);
		const uint32_t blocksX = (mip.Width + 3) / 4;
		const uint32_t blocksY = (mip.Height + 3) / 4;
		if (mip.Bytes.size() != static_cast<size_t>(blocksX) * blocksY * 16)
		{
			error = "BC3 mip byte count is invalid"; return false;
		}
		for (uint32_t by = 0; by < blocksY; ++by)
		for (uint32_t bx = 0; bx < blocksX; ++bx)
		{
			const uint8_t* block = mip.Bytes.data()
				+ (static_cast<size_t>(by) * blocksX + bx) * 16;
			std::array<uint8_t, 8> alpha{}; alpha[0] = block[0]; alpha[1] = block[1];
			if (alpha[0] > alpha[1])
				for (uint32_t i = 1; i <= 6; ++i) alpha[i + 1] = static_cast<uint8_t>(
					((7 - i) * alpha[0] + i * alpha[1]) / 7);
			else
			{
				for (uint32_t i = 1; i <= 4; ++i) alpha[i + 1] = static_cast<uint8_t>(
					((5 - i) * alpha[0] + i * alpha[1]) / 5);
				alpha[6] = 0; alpha[7] = 255;
			}
			uint64_t alphaBits = 0;
			for (uint32_t byte = 0; byte < 6; ++byte)
				alphaBits |= static_cast<uint64_t>(block[2 + byte]) << (byte * 8);
			const uint16_t color0 = static_cast<uint16_t>(block[8] | block[9] << 8);
			const uint16_t color1 = static_cast<uint16_t>(block[10] | block[11] << 8);
			std::array<std::array<uint8_t, 3>, 4> colors{};
			colors[0] = Unpack565(color0); colors[1] = Unpack565(color1);
			for (uint32_t channel = 0; channel < 3; ++channel)
			{
				colors[2][channel] = static_cast<uint8_t>((2 * colors[0][channel]
					+ colors[1][channel]) / 3);
				colors[3][channel] = static_cast<uint8_t>((colors[0][channel]
					+ 2 * colors[1][channel]) / 3);
			}
			const uint32_t colorBits = static_cast<uint32_t>(block[12])
				| (static_cast<uint32_t>(block[13]) << 8)
				| (static_cast<uint32_t>(block[14]) << 16)
				| (static_cast<uint32_t>(block[15]) << 24);
			for (uint32_t index = 0; index < 16; ++index)
			{
				const uint32_t x = bx * 4 + index % 4, y = by * 4 + index / 4;
				if (x >= mip.Width || y >= mip.Height) continue;
				const auto& color = colors[(colorBits >> (index * 2)) & 3];
				const size_t destination = (static_cast<size_t>(y) * mip.Width + x) * 4;
				rgba[destination] = color[0]; rgba[destination + 1] = color[1];
				rgba[destination + 2] = color[2];
				rgba[destination + 3] = alpha[(alphaBits >> (index * 3)) & 7];
			}
		}
		return true;
	}

}
