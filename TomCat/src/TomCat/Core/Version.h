#pragma once

#include <cstdint>
#include <string_view>

namespace TomCat::Version {

	// This is the single checked-in source for product, persistent format, and
	// runtime compatibility versions. Native code and release scripts consume it.
	inline constexpr std::string_view ProductVersion = "0.6.0";
	inline constexpr std::string_view EngineBuildID = "TomCat-0.6.0";

	// Read and write only current formats; minimum fields are retained for product-info consumers.
	inline constexpr uint32_t ProjectFormatOldest = 4;
	inline constexpr uint32_t ProjectFormatCurrent = 4;
	inline constexpr uint32_t SceneFormatOldest = 11;
	inline constexpr uint32_t SceneFormatCurrent = 11;
	inline constexpr uint32_t PrefabFormatCurrent = 1;

	inline constexpr uint32_t TcpakFormatOldest = 8;
	inline constexpr uint32_t TcpakFormatCurrent = 8;
	inline constexpr uint32_t PlayerTemplateFormatCurrent = 1;
	inline constexpr uint32_t SaveFormatCurrent = 1;
	inline constexpr uint32_t PlayerAbiCurrent = 1;
	inline constexpr uint32_t NativeApiCurrent = 1;
	inline constexpr uint32_t ManagedApiCurrent = 5;
	inline constexpr uint32_t ScriptManifestCurrent = 1;
	// Removing ComponentDescriptor's migration callbacks changes the native
	// descriptor layout passed across the module boundary. Rebuild modules.
	inline constexpr uint32_t ModuleAbiCurrent = 2;

}
