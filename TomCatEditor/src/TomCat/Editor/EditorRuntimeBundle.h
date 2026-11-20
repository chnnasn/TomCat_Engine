#pragma once

#include <filesystem>
#include <functional>
#include <cstdint>
#include <string>

namespace TomCat {

	enum class EditorRuntimeStage { Preparing, Extracting, Verifying };
	// Synchronous observer: must not throw. Extracting reports bytes successfully
	// written across the manifest; total == 0 means indeterminate work. Cached
	// trees report verification, never fictional extraction progress.
	using EditorRuntimeProgress = std::function<void(EditorRuntimeStage, uint64_t, uint64_t)>;

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
		std::string& errorMessage,
		const EditorRuntimeProgress& progress = {});

	// Production convenience entry point. The cache base comes from
	// ApplicationPaths and the successfully validated root is published there for
	// Editor consumers such as managed compilation and Player export.
	[[nodiscard]] bool ConfigurePackagedEditorRuntime(
		const std::filesystem::path& payloadRoot,
		EditorRuntimeBundleResult& result,
		std::string& errorMessage,
		const EditorRuntimeProgress& progress = {});

}
