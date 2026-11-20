#pragma once

#include "Player/PlayerBuilder.h"
#include "Scripting/ScriptProjectCompiler.h"

#include <TomCat/Asset/Asset.h>
#include <TomCat/Core/Base.h>

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

// Shared cook/build pipeline for the Editor and TomCatCLI. Both front ends call
// the same steps in the same order so their validation cannot drift:
//
//   ResolveEntryScene -> PrepareManagedCandidate -> (front-end work) -> CookOrBuild
//
// The front ends only add their own pre-checks and present the result.
namespace TomCat {

	class AssetManager;
	class Project;
	struct BuildSettings;

}

namespace TomCat::Build {

	// Which pipeline step failed. TomCatCLI maps these to its exit codes.
	enum class BuildFailure : uint8_t
	{
		None = 0,
		EntryScene,      // no enabled entry scene, or it is not a Scene asset
		ManagedCompile,  // compile, validation or source-state check failed
		ArtifactRead,    // the compiled assembly or PDB could not be read
		Cook,            // payload rejected or CookToPackage failed
		PlayerBuild      // PlayerBuilder rejected or failed the build
	};

	struct BuildError
	{
		BuildFailure Kind = BuildFailure::None;
		std::string Message;
	};

	// A freshly compiled managed build plus the bytes that go into the package.
	struct ManagedCandidate
	{
		ScriptBuildResult Script;          // Script.ManifestJson is always set
		std::vector<uint8_t> Assembly;
		std::vector<uint8_t> Pdb;          // empty when the profile emitted none
	};

	enum class BuildOutput : uint8_t
	{
		Package,  // write Game.tcpak only
		Player    // cook and publish a standalone Player
	};

	struct CookOrBuildRequest
	{
		BuildOutput Output = BuildOutput::Player;
		Ref<Project> ProjectInstance;
		AssetHandle EntryScene = AssetHandle(0);
		// Package only. Empty selects <project>/Build/Game.tcpak.
		std::filesystem::path PackagePath;
		// Player only. Empty selects PlayerBuilder::FindDefaultTemplateDirectory().
		std::filesystem::path TemplateDirectory;
	};

	struct CookOrBuildResult
	{
		bool Succeeded = false;
		BuildError Error;
		// Package: the written .tcpak. Player: the published output directory.
		std::filesystem::path OutputPath;
		std::filesystem::path PlayerExecutable;  // Player only
		std::string Message;                     // success message for display
	};

	// The entry scene must be set and listed as an enabled scene.
	[[nodiscard]] bool ResolveEntryScene(const BuildSettings& settings,
		AssetHandle& entryScene, BuildError& error);

	// Compiles the current C# sources with `profile` into a new candidate, checks
	// that it was promoted to last-good and still matches the sources, and reads
	// the assembly and PDB. Never falls back to an earlier last-good build.
	// Set the compiler's diagnostic callback before calling; this configures it.
	[[nodiscard]] bool PrepareManagedCandidate(const Ref<Project>& project,
		ScriptProjectCompiler& compiler, ScriptBuildProfile profile,
		ManagedCandidate& candidate, BuildError& error);

	// True while `build` is still the compiler's last-good build of the current
	// sources. PlayerBuilder re-checks this just before publishing.
	[[nodiscard]] bool IsCandidateCurrent(ScriptProjectCompiler& compiler,
		const ScriptBuildResult& build);

	// Cooks or builds with `candidate`. The asset registry must already be
	// current; the Editor refreshes it after saving the open scene.
	// `compiler` must be the one that produced `candidate`.
	[[nodiscard]] CookOrBuildResult CookOrBuild(const CookOrBuildRequest& request,
		ScriptProjectCompiler& compiler, ManagedCandidate candidate);

	[[nodiscard]] bool ReadBinaryFile(const std::filesystem::path& path,
		std::vector<uint8_t>& bytes, std::string& error);

}
