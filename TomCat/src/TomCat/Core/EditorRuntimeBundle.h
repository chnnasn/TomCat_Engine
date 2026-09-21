#pragma once

#include <filesystem>
#include <string>

namespace TomCat {

	struct EditorRuntimeBundleResult
	{
		std::filesystem::path Root;
		std::filesystem::path ManagedDirectory;
		std::filesystem::path PlayerTemplateDirectory;
		std::filesystem::path CliExecutable;
		std::string EngineBuildID;
		std::string ManifestSHA256;
		bool ReusedExisting = false;
	};

	// Validates the hashed virtual payload manifest and publishes an immutable,
	// physical runtime tree below cacheBaseRoot. Files become visible together by
	// renaming a fully validated sibling staging directory into place.
	[[nodiscard]] bool EnsureEditorRuntimeBundle(
		const std::filesystem::path& payloadRoot,
		const std::filesystem::path& cacheBaseRoot,
		EditorRuntimeBundleResult& result,
		std::string& errorMessage);

	// Production convenience entry point. The cache base comes from
	// ApplicationPaths and the successfully validated root is published there for
	// Editor consumers such as managed compilation and Player export.
	[[nodiscard]] bool ConfigurePackagedEditorRuntime(
		const std::filesystem::path& payloadRoot,
		EditorRuntimeBundleResult& result,
		std::string& errorMessage);

}
