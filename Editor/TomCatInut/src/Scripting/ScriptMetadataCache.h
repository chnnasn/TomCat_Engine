#pragma once

#include "ScriptEditorMetadata.h"

#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>

namespace TomCat {

	// Immutable-at-publication Editor projection of ScriptManifest.Json. Parsing
	// happens synchronously after a successful build; the Inspector only receives
	// value copies, so a failed refresh can never expose a partially parsed map.
	class ScriptMetadataCache
	{
	public:
		bool ParseAndReplace(std::string_view manifestJson, std::string& errorMessage);
		void Clear() { m_Scripts.clear(); }
		std::optional<EditorScriptMetadata> Find(AssetHandle handle) const;

	private:
		std::unordered_map<uint64_t, EditorScriptMetadata> m_Scripts;
	};

}
