#pragma once

#include "TomCat/Core/Version.h"

#include <array>
#include <cstdint>
#include <string_view>

namespace TomCat::RuntimeCompatibility {

	inline constexpr std::string_view EngineBuildID = Version::EngineBuildID;

	inline constexpr std::array<char, 8> TcpakMagic = {
		'T', 'C', 'P', 'A', 'C', 'K', '0', '1'
	};
	inline constexpr uint32_t OldestSupportedTcpakVersion = Version::TcpakFormatOldest;
	inline constexpr uint32_t TcpakVersion = Version::TcpakFormatCurrent;
	inline constexpr uint32_t PlayerTemplateSchemaVersion =
		Version::PlayerTemplateFormatCurrent;
	inline constexpr uint32_t TcpakV5BaseHeaderSize = 72;
	inline constexpr uint32_t BootManifestSchemaVersion = 1;
	// v6 keeps the complete v5 fixed prefix, appends a fixed BootManifest
	// descriptor plus its bounded UTF-8 strings, then BuildSceneCount uint64
	// handles. HeaderSize records the start of the fixed-width asset index.
	inline constexpr uint32_t TcpakV6BaseHeaderSize = 124;
	// v7 retains the v6 header and extends each asset index entry with the
	// SHA-256 digest of its exact payload bytes.
	inline constexpr uint32_t TcpakV7BaseHeaderSize = TcpakV6BaseHeaderSize;
	inline constexpr uint32_t TcpakBaseHeaderSize = TcpakV7BaseHeaderSize;
	inline constexpr uint32_t MaximumBootManifestStringBytes = 512;
	inline constexpr uint32_t MaximumBootManifestBytes = 3072;
	inline constexpr uint32_t TcpakBootManifestVersion = 6;
	inline constexpr uint32_t TcpakEntryDigestVersion = 7;
	inline constexpr uint64_t TcpakLegacyEntrySize = 32;
	inline constexpr uint64_t TcpakEntryDigestSize = 32;
	inline constexpr uint64_t TcpakEntrySize =
		TcpakLegacyEntrySize + TcpakEntryDigestSize;
	inline constexpr uint64_t MaximumBuildSceneCount = 65536;

	[[nodiscard]] constexpr bool IsSupportedTcpakVersion(uint32_t version) noexcept
	{
		return version >= OldestSupportedTcpakVersion && version <= TcpakVersion;
	}

	[[nodiscard]] constexpr bool TcpakHasBootManifest(uint32_t version) noexcept
	{
		return version >= TcpakBootManifestVersion;
	}

	[[nodiscard]] constexpr bool TcpakHasEntryDigests(uint32_t version) noexcept
	{
		return version >= TcpakEntryDigestVersion;
	}

	[[nodiscard]] constexpr uint32_t TcpakBaseHeaderSizeForVersion(
		uint32_t version) noexcept
	{
		return TcpakHasBootManifest(version)
			? TcpakV6BaseHeaderSize : TcpakV5BaseHeaderSize;
	}

	[[nodiscard]] constexpr uint64_t TcpakEntrySizeForVersion(
		uint32_t version) noexcept
	{
		return TcpakHasEntryDigests(version)
			? TcpakEntrySize : TcpakLegacyEntrySize;
	}

	inline constexpr uint32_t PlayerAbiVersion = Version::PlayerAbiCurrent;
	inline constexpr uint32_t NativeApiVersion = Version::NativeApiCurrent;
	inline constexpr uint32_t ManagedApiVersion = Version::ManagedApiCurrent;
	inline constexpr uint32_t ScriptManifestVersion = Version::ScriptManifestCurrent;

}
