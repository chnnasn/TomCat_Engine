#pragma once

#include <array>
#include <cstdint>
#include <string_view>

namespace TomCat::RuntimeCompatibility {

	inline constexpr std::string_view EngineBuildID = "TomCat-Script-V1";

	inline constexpr std::array<char, 8> TcpakMagic = {
		'T', 'C', 'P', 'A', 'C', 'K', '0', '1'
	};
	inline constexpr uint32_t TcpakVersion = 5;
	inline constexpr uint32_t PlayerTemplateSchemaVersion = 1;
	// Fixed fields are followed by BuildSceneCount uint64 handles, then the
	// fixed-width asset index. HeaderSize records that variable boundary.
	inline constexpr uint32_t TcpakBaseHeaderSize = 72;
	inline constexpr uint64_t TcpakEntrySize = 32;
	inline constexpr uint64_t MaximumBuildSceneCount = 65536;

	inline constexpr uint32_t PlayerAbiVersion = 1;
	inline constexpr uint32_t NativeApiVersion = 1;
	inline constexpr uint32_t ManagedApiVersion = 1;
	inline constexpr uint32_t ScriptManifestVersion = 1;

}
