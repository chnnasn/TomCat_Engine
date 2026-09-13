#include "tcpch.h"
#include "ArtifactKey.h"

#include "ContentHash.h"

#include <algorithm>
#include <charconv>

namespace TomCat {

	namespace {

		void AppendField(std::string& output, std::string_view name,
			std::string_view value)
		{
			output.append(name);
			output.push_back('=');
			output.append(std::to_string(value.size()));
			output.push_back(':');
			output.append(value);
			output.push_back('\n');
		}

		void AppendNumber(std::string& output, std::string_view name, uint64_t value)
		{
			AppendField(output, name, std::to_string(value));
		}

	}

	bool IsArtifactKey(std::string_view value)
	{
		if (value.size() != 64)
			return false;
		return std::all_of(value.begin(), value.end(), [](char character)
		{
			return (character >= '0' && character <= '9') ||
				(character >= 'a' && character <= 'f');
		});
	}

	std::string CanonicalizeArtifactKeyInput(ArtifactKeyInput input)
	{
		if (input.SchemaVersion == 0 || input.ImporterID.empty() ||
			input.ImporterVersion == 0 || input.Type == AssetType::None ||
			!IsArtifactKey(input.SourceSHA256) || input.Platform.empty() ||
			input.Backend.empty())
			return {};
		for (const std::string& key : input.DependencyKeys)
		{
			if (!IsArtifactKey(key))
				return {};
		}
		std::sort(input.DependencyKeys.begin(), input.DependencyKeys.end());

		std::string canonical;
		canonical.reserve(256 + input.Settings.size() * 32 +
			input.DependencyKeys.size() * 72);
		canonical += "TomCat.ArtifactKey\n";
		AppendNumber(canonical, "schema", input.SchemaVersion);
		AppendField(canonical, "importer", input.ImporterID);
		AppendNumber(canonical, "importerVersion", input.ImporterVersion);
		AppendNumber(canonical, "assetType", static_cast<uint16_t>(input.Type));
		AppendField(canonical, "sourceSHA256", input.SourceSHA256);
		AppendField(canonical, "platform", input.Platform);
		AppendField(canonical, "backend", input.Backend);
		AppendNumber(canonical, "settingCount", input.Settings.size());
		for (const auto& [key, value] : input.Settings)
		{
			AppendField(canonical, "settingKey", key);
			AppendField(canonical, "settingValue", value);
		}
		AppendNumber(canonical, "dependencyCount", input.DependencyKeys.size());
		for (const std::string& key : input.DependencyKeys)
			AppendField(canonical, "dependency", key);
		return canonical;
	}

	std::string BuildArtifactKey(ArtifactKeyInput input)
	{
		const std::string canonical = CanonicalizeArtifactKeyInput(std::move(input));
		if (canonical.empty())
			return {};
		return ComputeContentSHA256(std::span<const uint8_t>(
			reinterpret_cast<const uint8_t*>(canonical.data()), canonical.size()));
	}

}
