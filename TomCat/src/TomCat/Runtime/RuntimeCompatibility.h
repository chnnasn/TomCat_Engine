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
	inline constexpr uint32_t TcpakBaseHeaderSize = TcpakV6BaseHeaderSize;
	inline constexpr uint32_t MaximumBootManifestStringBytes = 512;
	inline constexpr uint32_t MaximumBootManifestBytes = 3072;
	inline constexpr uint64_t TcpakEntrySize = 32;
	inline constexpr uint64_t MaximumBuildSceneCount = 65536;

	inline constexpr uint32_t PlayerAbiVersion = Version::PlayerAbiCurrent;
	inline constexpr uint32_t NativeApiVersion = Version::NativeApiCurrent;
	inline constexpr uint32_t ManagedApiVersion = Version::ManagedApiCurrent;
	inline constexpr uint32_t ScriptManifestVersion = Version::ScriptManifestCurrent;

}
