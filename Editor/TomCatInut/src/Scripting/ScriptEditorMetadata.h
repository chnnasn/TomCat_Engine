#pragma once

#include "TomCat/Asset/Asset.h"
#include "TomCat/Scene/Components.h"

#include <optional>
#include <string>
#include <vector>

namespace TomCat {

	// Editor-friendly projection of the generated script manifest. The runtime may
	// populate this from its metadata domain without exposing reflection objects to
	// the editor UI.
	struct EditorScriptFieldMetadata
	{
		std::string FieldID;
		std::string Name;
		ScriptFieldType Type = ScriptFieldType::Bool;
		std::string TypeName;
		std::string Header;
		std::string Tooltip;
		std::optional<double> RangeMinimum;
		std::optional<double> RangeMaximum;
		std::vector<std::string> FormerNames;
		std::optional<ScriptFieldValue> DefaultValue;
		bool Hidden = false;
	};

	struct EditorScriptMetadata
	{
		AssetHandle ScriptAsset = AssetHandle(0);
		std::string TypeName;
		int32_t ExecutionOrder = 0;
		bool DisallowMultiple = false;
		std::vector<EditorScriptFieldMetadata> Fields;
	};

	ScriptFieldValue ScriptMetadataDefaultValue(
		const EditorScriptFieldMetadata& metadata);
	bool ReconcileScriptEntryFields(CSharpScriptEntry& entry,
		const EditorScriptMetadata& metadata);

}
