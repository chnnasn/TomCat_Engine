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
    inline constexpr uint32_t BootManifestSchemaVersion = 1;
    inline constexpr uint32_t TcpakBaseHeaderSize = 124;
    inline constexpr uint32_t MaximumBootManifestStringBytes = 512;
    inline constexpr uint32_t MaximumBootManifestBytes = 3072;
    inline constexpr uint64_t TcpakEntryDigestOffset = 32;
    inline constexpr uint64_t TcpakEntryDigestSize = 32;
    inline constexpr uint64_t TcpakEntrySize = 64;
	inline constexpr uint64_t MaximumBuildSceneCount = 65536;

	[[nodiscard]] constexpr bool IsSupportedTcpakVersion(uint32_t version) noexcept
	{
		return version == TcpakVersion;
	}

	inline constexpr uint32_t PlayerAbiVersion = Version::PlayerAbiCurrent;
	inline constexpr uint32_t NativeApiVersion = Version::NativeApiCurrent;
	inline constexpr uint32_t ManagedApiVersion = Version::ManagedApiCurrent;
	inline constexpr uint32_t ScriptManifestVersion = Version::ScriptManifestCurrent;

}
