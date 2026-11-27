#include "Build/BuildPipeline.h"

#include <TomCat/Asset/AssetManager.h>
#include <TomCat/Project/BuildSettings.h>
#include <TomCat/Project/Project.h>
#include <TomCat/Utils/PathUtils.h>

#include <algorithm>
#include <fstream>
#include <limits>
#include <system_error>
#include <utility>

namespace TomCat::Build {

	namespace {

		std::filesystem::path AbsoluteLexical(const std::filesystem::path& path)
		{
			if (path.empty())
				return {};
			std::error_code error;
			const std::filesystem::path absolute = std::filesystem::absolute(path, error);
			return (error ? path : absolute).lexically_normal();
		}

		bool Fail(BuildError& error, BuildFailure kind, std::string message)
		{
			error.Kind = kind;
			error.Message = std::move(message);
			return false;
		}

		CookOrBuildResult FailResult(BuildFailure kind, std::string message)
		{
			CookOrBuildResult result;
			result.Error.Kind = kind;
			result.Error.Message = std::move(message);
			return result;
		}

		bool ValidateEntrySceneAsset(AssetHandle entryScene, BuildError& error)
		{
			const AssetMetadata* metadata =
				AssetManager::Get().GetRegistry().GetMetadata(entryScene);
			if (!metadata || metadata->IsMissing || metadata->Type != AssetType::Scene)
				return Fail(error, BuildFailure::EntryScene,
					"the enabled Entry Scene is missing or is not a Scene asset.");
			return true;
		}

		// The AssetManager singleton holds the override until it is cleared, so
		// clear it on every exit, including an exception from CookToPackage.
		struct CookPayloadGuard
		{
			AssetManager& Assets;
			~CookPayloadGuard() { Assets.ClearManagedCookPayload(); }
		};

	}

	bool ReadBinaryFile(const std::filesystem::path& path,
		std::vector<uint8_t>& bytes, std::string& error)
	{
		bytes.clear();
		std::error_code sizeError;
		const uintmax_t fileSize = std::filesystem::file_size(path, sizeError);
		if (sizeError || fileSize == 0 ||
			fileSize > static_cast<uintmax_t>((std::numeric_limits<size_t>::max)()))
		{
			error = "could not read '" + PathToUTF8(path) + "': " +
				(sizeError ? sizeError.message()
					: fileSize == 0 ? std::string("file is empty") : std::string("file is too large"));
			return false;
		}
		std::ifstream input(path, std::ios::binary);
		if (!input)
		{
			error = "could not open '" + PathToUTF8(path) + "'.";
			return false;
		}
		bytes.resize(static_cast<size_t>(fileSize));
		input.read(reinterpret_cast<char*>(bytes.data()),
			static_cast<std::streamsize>(bytes.size()));
		if (!input || input.peek() != std::char_traits<char>::eof())
		{
			error = "could not read '" + PathToUTF8(path) + "' completely.";
			bytes.clear();
			return false;
		}
		return true;
	}

	bool ResolveEntryScene(const BuildSettings& settings, AssetHandle& entryScene,
		BuildError& error)
	{
		entryScene = AssetHandle(0);
		const auto entry = std::find_if(settings.Scenes.begin(), settings.Scenes.end(),
			[&](const BuildSceneSettings& scene)
			{
				return scene.Enabled && scene.Handle == settings.EntrySceneHandle;
			});
		if (static_cast<uint64_t>(settings.EntrySceneHandle) == 0 ||
			entry == settings.Scenes.end())
			return Fail(error, BuildFailure::EntryScene,
				"choose an enabled Entry Scene in Build Settings.");
		entryScene = settings.EntrySceneHandle;
		return true;
	}

	bool IsCandidateCurrent(ScriptProjectCompiler& compiler, const ScriptBuildResult& build)
	{
		return compiler.RefreshSourceState() &&
			compiler.IsCurrentSourceBuilt() &&
			compiler.GetCurrentSourceHash() == build.SourceHash &&
			compiler.GetLastGoodBuildID() == build.BuildID &&
			AbsoluteLexical(compiler.GetLastGoodAssemblyPath()) ==
				AbsoluteLexical(build.AssemblyPath);
	}

	bool PrepareManagedCandidate(const Ref<Project>& project,
		ScriptProjectCompiler& compiler, ScriptBuildProfile profile,
		ManagedCandidate& candidate, BuildError& error)
	{
		candidate = {};
		if (!project)
			return Fail(error, BuildFailure::ManagedCompile, "no project is open.");
		if (!compiler.Configure(project, {}, {}, profile))
			return Fail(error, BuildFailure::ManagedCompile,
				"could not configure the C# compiler for the selected build profile.");

		ScriptBuildResult build = compiler.CompileNow();
		if (!build.Succeeded || build.SourceChangedDuringBuild ||
			build.BuildID.empty() || build.AssemblyPath.empty() || build.ManifestJson.empty())
		{
			return Fail(error, BuildFailure::ManagedCompile, build.SourceChangedDuringBuild
				? "C# sources changed during compilation. Build again."
				: "the fresh C# candidate did not compile or validate. See the C# diagnostics.");
		}
		if (!IsCandidateCurrent(compiler, build))
			return Fail(error, BuildFailure::ManagedCompile,
				"the fresh C# candidate no longer matches the current sources.");

		std::string readError;
		if (!ReadBinaryFile(build.AssemblyPath, candidate.Assembly, readError))
			return Fail(error, BuildFailure::ArtifactRead, std::move(readError));
		if (!build.PdbPath.empty() && !ReadBinaryFile(build.PdbPath, candidate.Pdb, readError))
		{
			candidate.Assembly.clear();
			return Fail(error, BuildFailure::ArtifactRead, std::move(readError));
		}
		candidate.Script = std::move(build);
		return true;
	}

	CookOrBuildResult CookOrBuild(const CookOrBuildRequest& request,
		ScriptProjectCompiler& compiler, ManagedCandidate candidate)
	{
		if (!request.ProjectInstance)
			return FailResult(BuildFailure::PlayerBuild, "no project is open.");
		BuildError entryError;
		if (!ValidateEntrySceneAsset(request.EntryScene, entryError))
			return FailResult(entryError.Kind, std::move(entryError.Message));

		if (request.Output == BuildOutput::Package)
		{
			const std::filesystem::path output = request.PackagePath.empty()
				? request.ProjectInstance->GetProjectDirectory() / "Build" / "Game.tcpak"
				: request.PackagePath;
			AssetManager& assets = AssetManager::Get();
			CookOrBuildResult result;
			bool cooked = false;
			{
				if (!assets.SetManagedCookPayload(std::move(candidate.Assembly),
					candidate.Script.ManifestJson, candidate.Script.BuildID,
					std::move(candidate.Pdb)))
					return FailResult(BuildFailure::Cook,
						"the fresh managed cook payload was rejected.");
				CookPayloadGuard payloadGuard{ assets };
				cooked = assets.CookToPackage(output, request.EntryScene,
                    request.IncludeAssetReport ? &result.AssetReport : nullptr);
			}
			if (!cooked)
				return FailResult(BuildFailure::Cook, "cook failed. See the preceding diagnostics.");

			result.Succeeded = true;
			result.OutputPath = output;
			result.Message = "Cook succeeded: " + PathToUTF8(output);
			return result;
		}

		PlayerBuildRequest playerRequest;
		playerRequest.ProjectInstance = request.ProjectInstance;
		playerRequest.EntryScene = request.EntryScene;
		playerRequest.TemplateDirectory = request.TemplateDirectory.empty()
			? PlayerBuilder::FindDefaultTemplateDirectory()
			: request.TemplateDirectory;
		playerRequest.ManagedAssembly = std::move(candidate.Assembly);
		playerRequest.ManagedPdb = std::move(candidate.Pdb);
		playerRequest.ScriptManifestJson = candidate.Script.ManifestJson;
		playerRequest.ScriptBuildID = candidate.Script.BuildID;
		playerRequest.ValidateBeforePublish = [&compiler, build = std::move(candidate.Script)]()
		{
			return IsCandidateCurrent(compiler, build);
		};
		PlayerBuildResult player = PlayerBuilder::Build(std::move(playerRequest));
		if (!player.Succeeded)
			return FailResult(BuildFailure::PlayerBuild, player.Message);

		CookOrBuildResult result;
		result.Succeeded = true;
		result.OutputPath = player.OutputDirectory;
		result.PlayerExecutable = player.PlayerExecutable;
		result.Message = player.Message;
		result.AssetReport = std::move(player.AssetReport);
		return result;
	}

}
