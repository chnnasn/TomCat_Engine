#pragma once

#include "Asset.h"

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace TomCat {

	inline constexpr uint32_t ArtifactKeySchemaVersion = 1;

	struct ArtifactKeyInput
	{
		uint32_t SchemaVersion = ArtifactKeySchemaVersion;
		std::string ImporterID;
		uint32_t ImporterVersion = 0;
		AssetType Type = AssetType::None;
		std::string SourceSHA256;
		AssetImportSettings Settings;
		std::string Platform;
		std::string Backend;
		std::vector<std::string> DependencyKeys;
	};

	// Produces a length-prefixed, locale-independent representation. Dependency
	// keys are sorted because a graph's enumeration order is not semantically
	// meaningful. The resulting artifact key is SHA-256(canonical form).
	[[nodiscard]] std::string CanonicalizeArtifactKeyInput(ArtifactKeyInput input);
	[[nodiscard]] std::string BuildArtifactKey(ArtifactKeyInput input);
	[[nodiscard]] bool IsArtifactKey(std::string_view value);

}
