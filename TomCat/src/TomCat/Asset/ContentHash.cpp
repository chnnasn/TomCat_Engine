#include "tcpch.h"
#include "ContentHash.h"

#include "TomCat/Utils/PathUtils.h"

#include <algorithm>
#include <array>
#include <cstring>
#include <fstream>
#include <limits>

namespace TomCat {

	namespace {

		uint32_t RotateRight(uint32_t value, uint32_t amount)
		{
			return (value >> amount) | (value << (32U - amount));
		}

		class Sha256 final
		{
		public:
			void Update(std::span<const uint8_t> bytes)
			{
				m_TotalBytes += static_cast<uint64_t>(bytes.size());
				size_t offset = 0;
				if (m_BufferSize != 0)
				{
					const size_t copied = (std::min)(bytes.size(),
						m_Buffer.size() - m_BufferSize);
					std::memcpy(m_Buffer.data() + m_BufferSize, bytes.data(), copied);
					m_BufferSize += copied;
					offset += copied;
					if (m_BufferSize == m_Buffer.size())
					{
						Transform(m_Buffer.data());
						m_BufferSize = 0;
					}
				}
				while (bytes.size() - offset >= m_Buffer.size())
				{
					Transform(bytes.data() + offset);
					offset += m_Buffer.size();
				}
				if (offset < bytes.size())
				{
					m_BufferSize = bytes.size() - offset;
					std::memcpy(m_Buffer.data(), bytes.data() + offset, m_BufferSize);
				}
			}

			std::array<uint8_t, 32> Final()
			{
				const uint64_t bitLength = m_TotalBytes * 8ULL;
				std::array<uint8_t, 64> padding{};
				padding[0] = 0x80;
				const size_t paddingLength = m_BufferSize < 56
					? 56 - m_BufferSize : 120 - m_BufferSize;
				Update(std::span<const uint8_t>(padding.data(), paddingLength));
				std::array<uint8_t, 8> length{};
				for (size_t index = 0; index < length.size(); ++index)
					length[length.size() - 1 - index] = static_cast<uint8_t>(
						(bitLength >> (index * 8)) & 0xffULL);
				Update(length);

				std::array<uint8_t, 32> result{};
				for (size_t word = 0; word < m_State.size(); ++word)
				{
					for (size_t byte = 0; byte < 4; ++byte)
						result[word * 4 + byte] = static_cast<uint8_t>(
							m_State[word] >> ((3 - byte) * 8));
				}
				return result;
			}

		private:
			void Transform(const uint8_t* block)
			{
				static constexpr std::array<uint32_t, 64> constants = {
					0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5,
					0x3956c25b, 0x59f111f1, 0x923f82a4, 0xab1c5ed5,
					0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3,
					0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174,
					0xe49b69c1, 0xefbe4786, 0x0fc19dc6, 0x240ca1cc,
					0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da,
					0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7,
					0xc6e00bf3, 0xd5a79147, 0x06ca6351, 0x14292967,
					0x27b70a85, 0x2e1b2138, 0x4d2c6dfc, 0x53380d13,
					0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85,
					0xa2bfe8a1, 0xa81a664b, 0xc24b8b70, 0xc76c51a3,
					0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070,
					0x19a4c116, 0x1e376c08, 0x2748774c, 0x34b0bcb5,
					0x391c0cb3, 0x4ed8aa4a, 0x5b9cca4f, 0x682e6ff3,
					0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208,
					0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2 };
				std::array<uint32_t, 64> words{};
				for (size_t index = 0; index < 16; ++index)
				{
					words[index] = (static_cast<uint32_t>(block[index * 4]) << 24)
						| (static_cast<uint32_t>(block[index * 4 + 1]) << 16)
						| (static_cast<uint32_t>(block[index * 4 + 2]) << 8)
						| static_cast<uint32_t>(block[index * 4 + 3]);
				}
				for (size_t index = 16; index < words.size(); ++index)
				{
					const uint32_t left = RotateRight(words[index - 15], 7)
						^ RotateRight(words[index - 15], 18)
						^ (words[index - 15] >> 3);
					const uint32_t right = RotateRight(words[index - 2], 17)
						^ RotateRight(words[index - 2], 19)
						^ (words[index - 2] >> 10);
					words[index] = words[index - 16] + left
						+ words[index - 7] + right;
				}

				uint32_t a = m_State[0], b = m_State[1], c = m_State[2], d = m_State[3];
				uint32_t e = m_State[4], f = m_State[5], g = m_State[6], h = m_State[7];
				for (size_t index = 0; index < words.size(); ++index)
				{
					const uint32_t sum1 = RotateRight(e, 6) ^ RotateRight(e, 11)
						^ RotateRight(e, 25);
					const uint32_t choose = (e & f) ^ (~e & g);
					const uint32_t temporary1 = h + sum1 + choose
						+ constants[index] + words[index];
					const uint32_t sum0 = RotateRight(a, 2) ^ RotateRight(a, 13)
						^ RotateRight(a, 22);
					const uint32_t majority = (a & b) ^ (a & c) ^ (b & c);
					const uint32_t temporary2 = sum0 + majority;
					h = g; g = f; f = e; e = d + temporary1;
					d = c; c = b; b = a; a = temporary1 + temporary2;
				}
				m_State[0] += a; m_State[1] += b; m_State[2] += c; m_State[3] += d;
				m_State[4] += e; m_State[5] += f; m_State[6] += g; m_State[7] += h;
			}

			std::array<uint32_t, 8> m_State = { 0x6a09e667, 0xbb67ae85,
				0x3c6ef372, 0xa54ff53a, 0x510e527f, 0x9b05688c,
				0x1f83d9ab, 0x5be0cd19 };
			std::array<uint8_t, 64> m_Buffer{};
			size_t m_BufferSize = 0;
			uint64_t m_TotalBytes = 0;
		};

		std::string ToLowerHex(const ContentSHA256Digest& digest)
		{
			static constexpr char hex[] = "0123456789abcdef";
			std::string result;
			result.reserve(digest.size() * 2);
			for (const uint8_t byte : digest)
			{
				result.push_back(hex[byte >> 4]);
				result.push_back(hex[byte & 0x0f]);
			}
			return result;
		}

	}

	ContentSHA256Digest ComputeContentSHA256Digest(
		std::span<const uint8_t> bytes)
	{
		Sha256 hasher;
		hasher.Update(bytes);
		return hasher.Final();
	}

	std::string ComputeContentSHA256(std::span<const uint8_t> bytes)
	{
		return ToLowerHex(ComputeContentSHA256Digest(bytes));
	}

	bool ComputeStreamRangeContentSHA256(std::istream& input, uint64_t offset,
		uint64_t size, ContentSHA256Digest& digest)
	{
		digest = {};
		if (offset > static_cast<uint64_t>(
			(std::numeric_limits<std::streamoff>::max)()))
			return false;

		input.clear();
		input.seekg(static_cast<std::streamoff>(offset), std::ios::beg);
		if (!input)
			return false;

		Sha256 hasher;
		std::array<uint8_t, 64 * 1024> buffer{};
		uint64_t remaining = size;
		while (remaining > 0)
		{
			const size_t chunk = static_cast<size_t>((std::min)(remaining,
				static_cast<uint64_t>(buffer.size())));
			if (!input.read(reinterpret_cast<char*>(buffer.data()),
				static_cast<std::streamsize>(chunk)))
				return false;
			hasher.Update(std::span<const uint8_t>(buffer.data(), chunk));
			remaining -= chunk;
		}
		digest = hasher.Final();
		return true;
	}

	bool VerifyContentSHA256(std::span<const uint8_t> bytes,
		const ContentSHA256Digest& expectedDigest)
	{
		return ComputeContentSHA256Digest(bytes) == expectedDigest;
	}

	bool VerifyStreamRangeContentSHA256(std::istream& input, uint64_t offset,
		uint64_t size, const ContentSHA256Digest& expectedDigest)
	{
		ContentSHA256Digest actualDigest{};
		return ComputeStreamRangeContentSHA256(input, offset, size, actualDigest)
			&& actualDigest == expectedDigest;
	}

	bool ComputeFileContentSHA256(const std::filesystem::path& path,
		std::string& digest, std::string& errorMessage,
		const std::function<bool()>& cancellationRequested)
	{
		digest.clear();
		errorMessage.clear();
		std::ifstream input(path, std::ios::binary);
		if (!input)
		{
			errorMessage = "could not open source file: " + PathToUTF8(path);
			return false;
		}
		Sha256 hasher;
		std::array<uint8_t, 64 * 1024> buffer{};
		for (;;)
		{
			if (cancellationRequested && cancellationRequested())
			{
				errorMessage = "cancelled";
				return false;
			}
			input.read(reinterpret_cast<char*>(buffer.data()),
				static_cast<std::streamsize>(buffer.size()));
			const std::streamsize read = input.gcount();
			if (read > 0)
				hasher.Update(std::span<const uint8_t>(buffer.data(),
					static_cast<size_t>(read)));
			if (input.eof())
				break;
			if (!input)
			{
				errorMessage = "could not read complete source file";
				return false;
			}
		}
		digest = ToLowerHex(hasher.Final());
		return true;
	}

	bool ReadFileForImport(const std::filesystem::path& path,
		std::vector<uint8_t>& bytes, std::string& errorMessage,
		const std::function<bool()>& cancellationRequested)
	{
		bytes.clear();
		errorMessage.clear();
		std::ifstream input(path, std::ios::binary | std::ios::ate);
		if (!input)
		{
			errorMessage = "could not open source file: " + PathToUTF8(path);
			return false;
		}
		const std::streamoff end = input.tellg();
		if (end < 0 || static_cast<uint64_t>(end) >
			static_cast<uint64_t>((std::numeric_limits<size_t>::max)()))
		{
			errorMessage = "source file is too large";
			return false;
		}
		try
		{
			bytes.resize(static_cast<size_t>(end));
		}
		catch (const std::exception&)
		{
			errorMessage = "could not allocate source buffer";
			return false;
		}
		input.seekg(0, std::ios::beg);
		constexpr size_t chunkSize = 64 * 1024;
		for (size_t offset = 0; offset < bytes.size();)
		{
			if (cancellationRequested && cancellationRequested())
			{
				bytes.clear();
				errorMessage = "cancelled";
				return false;
			}
			const size_t chunk = (std::min)(chunkSize, bytes.size() - offset);
			if (!input.read(reinterpret_cast<char*>(bytes.data() + offset),
				static_cast<std::streamsize>(chunk)))
			{
				bytes.clear();
				errorMessage = "could not read complete source file";
				return false;
			}
			offset += chunk;
		}
		return true;
	}

}
