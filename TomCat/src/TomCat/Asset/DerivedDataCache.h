#pragma once

#include <atomic>
#include <cstdint>
#include <filesystem>
#include <span>
#include <string>
#include <vector>

namespace TomCat {

	struct DerivedDataCacheStats
	{
		uint64_t Hits = 0;
		uint64_t Misses = 0;
		uint64_t CorruptEntries = 0;
		uint64_t PublishedEntries = 0;
	};

	// Immutable, content-addressed artifact storage. Entries carry an independent
	// payload digest, so an interrupted or externally damaged cache is always a
	// miss and can be rebuilt from source.
	class DerivedDataCache final
	{
	public:
		[[nodiscard]] bool Initialize(const std::filesystem::path& rootDirectory);
		void Shutdown();
		[[nodiscard]] bool IsInitialized() const noexcept { return m_Initialized; }
		[[nodiscard]] const std::filesystem::path& GetRootDirectory() const noexcept
		{
			return m_RootDirectory;
		}

		[[nodiscard]] bool TryRead(const std::string& artifactKey,
			std::vector<uint8_t>& payload) const;
		[[nodiscard]] bool Publish(const std::string& artifactKey,
			std::span<const uint8_t> payload);
		[[nodiscard]] std::filesystem::path GetEntryPath(
			const std::string& artifactKey) const;
		// Returns the immutable payload subrange inside a validated DDC entry.
		// This lets stream readers retain a file range instead of resident bytes.
		[[nodiscard]] bool TryGetPayloadRange(const std::string& artifactKey,
			std::filesystem::path& path, uint64_t& offset, uint64_t& size) const;
		[[nodiscard]] DerivedDataCacheStats GetStats() const noexcept;

	private:
		std::filesystem::path m_RootDirectory;
		bool m_Initialized = false;
		mutable std::atomic_uint64_t m_Hits = 0;
		mutable std::atomic_uint64_t m_Misses = 0;
		mutable std::atomic_uint64_t m_CorruptEntries = 0;
		std::atomic_uint64_t m_PublishedEntries = 0;
	};

}
