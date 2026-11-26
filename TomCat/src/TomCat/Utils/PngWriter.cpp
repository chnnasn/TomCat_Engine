#include "tcpch.h"
#include "PngWriter.h"

#include <algorithm>
#include <array>

namespace TomCat {

	namespace {

		uint32_t Crc32(std::span<const uint8_t> bytes)
		{
			static const std::array<uint32_t, 256> table = []
			{
				std::array<uint32_t, 256> values{};
				for (uint32_t n = 0; n < 256; ++n)
				{
					uint32_t c = n;
					for (int k = 0; k < 8; ++k)
						c = (c & 1) ? 0xEDB88320u ^ (c >> 1) : c >> 1;
					values[n] = c;
				}
				return values;
			}();
			uint32_t crc = 0xFFFFFFFFu;
			for (uint8_t byte : bytes)
				crc = table[(crc ^ byte) & 0xFF] ^ (crc >> 8);
			return ~crc;
		}

		void AppendBE32(std::vector<uint8_t>& output, uint32_t value)
		{
			for (int shift = 24; shift >= 0; shift -= 8)
				output.push_back(static_cast<uint8_t>(value >> shift));
		}

		void AppendChunk(std::vector<uint8_t>& output, const char (&type)[5],
			std::span<const uint8_t> data)
		{
			AppendBE32(output, static_cast<uint32_t>(data.size()));
			const size_t typeOffset = output.size();
			output.insert(output.end(), type, type + 4);
			output.insert(output.end(), data.begin(), data.end());
			AppendBE32(output, Crc32(std::span<const uint8_t>(
				output.data() + typeOffset, 4 + data.size())));
		}

		std::vector<uint8_t> Encode(uint32_t width, uint32_t height,
			std::span<const uint8_t> rgba, bool bottomUp)
		{
			const size_t stride = static_cast<size_t>(width) * 4;
			if (width == 0 || height == 0 || rgba.size() != stride * height)
				return {};
			std::vector<uint8_t> raw;
			raw.reserve((stride + 1) * height);
			for (uint32_t row = 0; row < height; ++row)
			{
				const uint32_t source = bottomUp ? height - 1 - row : row;
				raw.push_back(0); // filter type: none
				const uint8_t* line = rgba.data() + source * stride;
				raw.insert(raw.end(), line, line + stride);
			}

			std::vector<uint8_t> zlib = { 0x78, 0x01 };
			zlib.reserve(raw.size() + raw.size() / 65535 * 5 + 16);
			uint32_t a = 1, b = 0;
			for (uint8_t byte : raw)
			{
				a = (a + byte) % 65521u;
				b = (b + a) % 65521u;
			}
			size_t offset = 0;
			do
			{
				const size_t block = std::min<size_t>(65535, raw.size() - offset);
				const bool last = offset + block == raw.size();
				zlib.push_back(last ? 1 : 0);
				zlib.push_back(static_cast<uint8_t>(block));
				zlib.push_back(static_cast<uint8_t>(block >> 8));
				zlib.push_back(static_cast<uint8_t>(~block));
				zlib.push_back(static_cast<uint8_t>(~block >> 8));
				zlib.insert(zlib.end(), raw.begin() + offset, raw.begin() + offset + block);
				offset += block;
			} while (offset < raw.size());
			AppendBE32(zlib, (b << 16) | a);

			std::vector<uint8_t> png = { 0x89, 'P', 'N', 'G', '\r', '\n', 0x1A, '\n' };
			std::vector<uint8_t> header;
			AppendBE32(header, width);
			AppendBE32(header, height);
			header.insert(header.end(), { 8, 6, 0, 0, 0 }); // 8-bit RGBA, no interlace
			AppendChunk(png, "IHDR", header);
			AppendChunk(png, "IDAT", zlib);
			AppendChunk(png, "IEND", {});
			return png;
		}

	}

	std::vector<uint8_t> EncodePngRGBA(uint32_t width, uint32_t height,
		std::span<const uint8_t> rgba)
	{
		return Encode(width, height, rgba, false);
	}

	std::vector<uint8_t> EncodePngRGBABottomUp(uint32_t width, uint32_t height,
		std::span<const uint8_t> rgba)
	{
		return Encode(width, height, rgba, true);
	}

}
