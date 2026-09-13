#pragma once

#include "Asset.h"

#include <cstdint>
#include <span>
#include <string>
#include <vector>

namespace TomCat {

	// Produces a canonical, directly streamable RIFF/WAVE artifact. All supported
	// PCM/float WAV sources are normalized to interleaved signed PCM16 while
	// preserving channel count, sample rate and frame count.
	[[nodiscard]] bool BuildAudioArtifact(std::span<const uint8_t> source,
		const AssetImportSettings& settings, std::vector<uint8_t>& artifact,
		std::string& error);

}
