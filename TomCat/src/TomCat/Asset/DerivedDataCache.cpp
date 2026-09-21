#include "tcpch.h"
#include "DerivedDataCache.h"

#include "ArtifactKey.h"
#include "ContentHash.h"
#include "TomCat/Utils/FileSystemUtils.h"

#include <array>
#include <cstring>
#include <fstream>
#include <limits>

namespace TomCat {

	namespace {

		constexpr std::array<char, 8> kMagic = {
			'T', 'C', 'D', 'D', 'C', '0', '0', '1' };
		constexpr uint32_t kVersion = 1;
		constexpr uint64_t kMaximumPayloadSize = 2ULL * 1024ULL * 1024ULL * 1024ULL;
		constexpr size_t kHeaderSize = 8 + 4 + 8 + 64 + 64;

		void AppendU32(std::string& bytes, uint32_t value)
		{
			for (uint32_t shift = 0; shift < 32; shift += 8)
				bytes.push_back(static_cast<char>((value >> shift) & 0xffU));
		}

		void AppendU64(std::string& bytes, uint64_t value)
		{
			for (uint32_t shift = 0; shift < 64; shift += 8)
				bytes.push_back(static_cast<char>((value >> shift) & 0xffULL));
		}

		bool ReadU32(std::span<const uint8_t> bytes, size_t& offset, uint32_t& value)
		{
			if (bytes.size() - offset < 4)
				return false;
			value = 0;
			for (uint32_t byte = 0; byte < 4; ++byte)
				value |= static_cast<uint32_t>(bytes[offset++]) << (byte * 8);
			return true;
		}

		bool ReadU64(std::span<const uint8_t> bytes, size_t& offset, uint64_t& value)
		{
			if (bytes.size() - offset < 8)
				return false;
			value = 0;
			for (uint32_t byte = 0; byte < 8; ++byte)
				value |= static_cast<uint64_t>(bytes[offset++]) << (byte * 8);
			return true;
		}

	}

	bool DerivedDataCache::Initialize(const std::filesystem::path& rootDirectory)
	{
		Shutdown();
		if (rootDirectory.empty())
			return false;
		std::error_code error;
		const std::filesystem::path absolute = std::filesystem::absolute(rootDirectory, error);
		if (error)
			return false;
		std::filesystem::create_directories(absolute, error);
		if (error || !std::filesystem::is_directory(absolute, error) || error)
			return false;
		m_RootDirectory = absolute.lexically_normal();
		m_Initialized = true;
		m_Hits = 0;
		m_Misses = 0;
		m_CorruptEntries = 0;
		m_PublishedEntries = 0;
		return true;
	}

	void DerivedDataCache::Shutdown()
	{
		m_RootDirectory.clear();
		m_Initialized = false;
	}

	std::filesystem::path DerivedDataCache::GetEntryPath(
		const std::string& artifactKey) const
	{
		if (!m_Initialized || !IsArtifactKey(artifactKey))
			return {};
		return m_RootDirectory / artifactKey.substr(0, 2) /
			(artifactKey + ".tcddc");
	}

	bool DerivedDataCache::TryRead(const std::string& artifactKey,
		std::vector<uint8_t>& payload) const
	{
		payload.clear();
		const std::filesystem::path path = GetEntryPath(artifactKey);
		if (path.empty())
		{
			++m_Misses;
			return false;
		}
		std::ifstream input(path, std::ios::binary | std::ios::ate);
		if (!input)
		{
			++m_Misses;
			return false;
		}
		const std::streamoff end = input.tellg();
		if (end < static_cast<std::streamoff>(kHeaderSize) ||
			static_cast<uint64_t>(end) > kHeaderSize + kMaximumPayloadSize)
		{
			++m_Misses;
			++m_CorruptEntries;
			return false;
		}
		std::vector<uint8_t> entry(static_cast<size_t>(end));
		input.seekg(0, std::ios::beg);
		if (!input.read(reinterpret_cast<char*>(entry.data()), end))
		{
			++m_Misses;
			++m_CorruptEntries;
			return false;
		}

		size_t offset = 0;
		if (!std::equal(kMagic.begin(), kMagic.end(), entry.begin()))
		{
			++m_Misses; ++m_CorruptEntries; return false;
		}
		offset += kMagic.size();
		uint32_t version = 0;
		uint64_t payloadSize = 0;
		if (!ReadU32(entry, offset, version) || version != kVersion ||
			!ReadU64(entry, offset, payloadSize) || payloadSize > kMaximumPayloadSize ||
			entry.size() != kHeaderSize + payloadSize)
		{
			++m_Misses; ++m_CorruptEntries; return false;
		}
		const std::string storedKey(reinterpret_cast<const char*>(entry.data() + offset), 64);
		offset += 64;
		const std::string storedHash(reinterpret_cast<const char*>(entry.data() + offset), 64);
		offset += 64;
		if (storedKey != artifactKey || !IsArtifactKey(storedHash))
		{
			++m_Misses; ++m_CorruptEntries; return false;
		}
		const std::span<const uint8_t> storedPayload(entry.data() + offset,
			static_cast<size_t>(payloadSize));
		if (ComputeContentSHA256(storedPayload) != storedHash)
		{
			++m_Misses; ++m_CorruptEntries; return false;
		}
		payload.assign(storedPayload.begin(), storedPayload.end());
		++m_Hits;
		return true;
	}

	bool DerivedDataCache::TryGetPayloadRange(const std::string& artifactKey,
		std::filesystem::path& path, uint64_t& offset, uint64_t& size) const
	{
		path.clear(); offset = 0; size = 0;
		const std::filesystem::path candidate = GetEntryPath(artifactKey);
		if (candidate.empty())
			return false;
		std::ifstream input(candidate, std::ios::binary | std::ios::ate);
		std::streamoff end = -1;
		if (input)
			end = static_cast<std::streamoff>(input.tellg());
		if (end < static_cast<std::streamoff>(kHeaderSize)
			|| static_cast<uint64_t>(end) > kHeaderSize + kMaximumPayloadSize)
			return false;
		std::array<uint8_t, kHeaderSize> header{};
		input.seekg(0, std::ios::beg);
		if (!input.read(reinterpret_cast<char*>(header.data()), header.size())
			|| !std::equal(kMagic.begin(), kMagic.end(), header.begin()))
			return false;
		size_t cursor = kMagic.size();
		uint32_t version = 0;
		uint64_t payloadSize = 0;
		if (!ReadU32(header, cursor, version) || version != kVersion
			|| !ReadU64(header, cursor, payloadSize)
			|| payloadSize > kMaximumPayloadSize
			|| static_cast<uint64_t>(end) != kHeaderSize + payloadSize)
			return false;
		const std::string storedKey(reinterpret_cast<const char*>(
			header.data() + cursor), 64);
		cursor += 64;
		const std::string storedHash(reinterpret_cast<const char*>(
			header.data() + cursor), 64);
		if (storedKey != artifactKey || !IsArtifactKey(storedHash))
			return false;
		path = candidate;
		offset = kHeaderSize;
		size = payloadSize;
		return true;
	}

	bool DerivedDataCache::Publish(const std::string& artifactKey,
		std::span<const uint8_t> payload)
	{
		if (payload.size() > kMaximumPayloadSize)
			return false;
		const std::filesystem::path path = GetEntryPath(artifactKey);
		if (path.empty())
			return false;
		std::error_code error;
		std::filesystem::create_directories(path.parent_path(), error);
		if (error)
			return false;

		std::string entry;
		entry.reserve(kHeaderSize + payload.size());
		entry.append(kMagic.data(), kMagic.size());
		AppendU32(entry, kVersion);
		AppendU64(entry, payload.size());
		entry.append(artifactKey);
		entry.append(ComputeContentSHA256(payload));
		entry.append(reinterpret_cast<const char*>(payload.data()), payload.size());
		std::string writeError;
		if (!FileSystem::WriteFileAtomically(path,
			std::string_view(entry.data(), entry.size()), writeError))
			return false;
		++m_PublishedEntries;
		return true;
	}

	DerivedDataCacheStats DerivedDataCache::GetStats() const noexcept
	{
		return { m_Hits.load(), m_Misses.load(), m_CorruptEntries.load(),
			m_PublishedEntries.load() };
	}

}
