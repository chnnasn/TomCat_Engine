#pragma once

#include <cstdint>
#include <string_view>

namespace TomCat::Version {

	// This is the single checked-in source for product, persistent format, and
	// runtime compatibility versions. Native code and release scripts consume it.
	inline constexpr std::string_view ProductVersion = "1.0.0";
	inline constexpr std::string_view EngineBuildID = "TomCat-1.0.0";

	inline constexpr uint32_t ProjectFormatOldest = 3;
	inline constexpr uint32_t ProjectFormatCurrent = 4;
	inline constexpr uint32_t SceneFormatOldest = 9;
	inline constexpr uint32_t SceneFormatCurrent = 11;
	inline constexpr uint32_t PrefabFormatCurrent = 1;

	inline constexpr uint32_t TcpakFormatOldest = 5;
	inline constexpr uint32_t TcpakFormatCurrent = 6;
	inline constexpr uint32_t PlayerTemplateFormatCurrent = 1;
	inline constexpr uint32_t PlayerAbiCurrent = 1;
	inline constexpr uint32_t NativeApiCurrent = 1;
	inline constexpr uint32_t ManagedApiCurrent = 1;
	inline constexpr uint32_t ScriptManifestCurrent = 1;

}
