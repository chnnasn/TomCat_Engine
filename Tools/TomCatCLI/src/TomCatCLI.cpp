#include <TomCat.h>

#include "Build/BuildPipeline.h"
#include "PakInspect.h"

#include "TomCat/Asset/AssetJobSystem.h"
#include "TomCat/Asset/AssetManager.h"
#include "TomCat/Editor/EditorRecoveryService.h"
#include "TomCat/Project/Project.h"
#include "TomCat/Module/ModuleSystem.h"
#include "TomCat/Utils/PathUtils.h"

#include <filesystem>
#include <iostream>
#include <optional>
#include <string>
#include <vector>

#ifdef TC_PLATFORM_WINDOWS
	#ifndef NOMINMAX
		#define NOMINMAX
	#endif
	#ifndef WIN32_LEAN_AND_MEAN
		#define WIN32_LEAN_AND_MEAN
	#endif
	#include <Windows.h>
#endif

namespace {

	std::filesystem::path ExecutableDirectory()
	{
#ifdef TC_PLATFORM_WINDOWS
		std::vector<wchar_t> buffer(MAX_PATH);
		for (;;)
		{
			const DWORD length = GetModuleFileNameW(nullptr, buffer.data(),
				static_cast<DWORD>(buffer.size()));
			if (length == 0)
				return {};
			if (length < buffer.size() - 1)
				return std::filesystem::path(
					std::wstring(buffer.data(), length)).parent_path();
			if (buffer.size() >= 32768)
				return {};
			buffer.resize(buffer.size() * 2);
		}
#else
		return TomCat::ApplicationPaths::GetExecutablePath().value_or(std::filesystem::path{}).parent_path();
#endif
	}

	enum class Command { Help, Cook, Build, Pak };

	struct Options
	{
		Command Operation = Command::Help;
		std::filesystem::path ProjectPath;
		std::filesystem::path OutputPath;
		std::filesystem::path TemplatePath;
		TomCat::CLI::PakInspectOptions Pak;
        TomCat::ScriptBuildProfile Profile = TomCat::ScriptBuildProfile::Production;
		std::string Error;
	};

	void PrintHelp()
	{
		std::cout
			<< "TomCatCLI - deterministic headless project build/cook\n\n"
			<< "Usage:\n"
			<< "  TomCatCLI cook  --project <Project.tcproj> [--output <Game.tcpak>] [--development]\n"
			<< "  TomCatCLI build --project <Project.tcproj> [--template <directory>] [--development]\n"
			<< "  TomCatCLI pak   --inspect <Game.tcpak> [--dump-textures <dir>] [--dump-scenes <dir>]\n\n"
			<< "pak --inspect validates and lists a cooked package: manifest, build scenes,\n"
			<< "every entry, scene entities with components and sprite references, and\n"
			<< "texture/sprite descriptors. --dump-textures writes each texture as PNG\n"
			<< "(Sprite slices cropped); --dump-scenes writes the cooked scene YAML.\n\n"
			<< "Both commands compile and validate the current C# sources first. Projects\n"
			<< "and assets must use the current format; older formats are rejected.\n";
	}

	bool MakeAbsolute(std::filesystem::path& path, std::string_view option,
		std::string& error)
	{
		if (path.empty())
			return true;
		std::error_code pathError;
		const std::filesystem::path absolute =
			std::filesystem::absolute(path, pathError);
		if (pathError)
		{
			error = "could not resolve " + std::string(option) + " path: " +
				pathError.message();
			return false;
		}
		path = absolute.lexically_normal();
		return true;
	}

	Options ParseOptions(const std::vector<std::string>& argv)
	{
		const int argc = static_cast<int>(argv.size());
		Options result;
		if (argc < 2)
			return result;
		const std::string command(argv[1]);
		if (command == "cook") result.Operation = Command::Cook;
		else if (command == "build") result.Operation = Command::Build;
		else if (command == "pak") result.Operation = Command::Pak;
		else if (command == "help" || command == "--help" || command == "-h")
			return result;
		else
		{
			result.Error = "unknown command";
			return result;
		}
		for (int index = 2; index < argc; ++index)
		{
			const std::string option(argv[index]);
			auto value = [&]() -> std::optional<std::filesystem::path>
			{
				if (++index >= argc) return std::nullopt;
				return TomCat::UTF8ToPath(argv[index]);
			};
			if (result.Operation == Command::Pak)
			{
				std::filesystem::path* target = option == "--inspect" ? &result.Pak.PackagePath
					: option == "--dump-textures" ? &result.Pak.DumpTexturesDirectory
					: option == "--dump-scenes" ? &result.Pak.DumpScenesDirectory : nullptr;
				if (!target)
				{
					result.Error = "unknown pak option: " + option;
					return result;
				}
				const auto parsed = value();
				if (!parsed || parsed->empty()) { result.Error = option + " requires a path"; return result; }
				*target = *parsed;
				continue;
			}
			if (option == "--development") result.Profile = TomCat::ScriptBuildProfile::Development;
            else if (option == "--project")
			{
				const auto parsed = value();
				if (!parsed) { result.Error = "--project requires a path"; return result; }
				result.ProjectPath = *parsed;
			}
			else if (option == "--output")
			{
				const auto parsed = value();
				if (!parsed) { result.Error = "--output requires a path"; return result; }
				result.OutputPath = *parsed;
			}
			else if (option == "--template")
			{
				const auto parsed = value();
				if (!parsed) { result.Error = "--template requires a path"; return result; }
				result.TemplatePath = *parsed;
			}
			else
			{
				result.Error = "unknown option: " + TomCat::PathToUTF8(option);
				return result;
			}
		}
		if (result.Operation == Command::Pak)
		{
			if (result.Pak.PackagePath.empty())
				result.Error = "pak requires --inspect <Game.tcpak>";
			else if (!MakeAbsolute(result.Pak.PackagePath, "--inspect", result.Error)
				|| !MakeAbsolute(result.Pak.DumpTexturesDirectory, "--dump-textures", result.Error)
				|| !MakeAbsolute(result.Pak.DumpScenesDirectory, "--dump-scenes", result.Error))
				return result;
			return result;
		}
		if (result.ProjectPath.empty())
			result.Error = "--project is required";
		if (result.Operation == Command::Cook && !result.TemplatePath.empty())
			result.Error = "--template is valid only for build";
		if (result.Operation == Command::Build && !result.OutputPath.empty())
			result.Error = "--output is valid only for cook; build publishes to the project Build directory";
		if (result.Error.empty() &&
			(!MakeAbsolute(result.ProjectPath, "--project", result.Error) ||
				!MakeAbsolute(result.OutputPath, "--output", result.Error) ||
				!MakeAbsolute(result.TemplatePath, "--template", result.Error)))
			return result;
		return result;
	}

	// Exit codes are documented in Tools/TomCatCLI/README.md.
	int ExitCodeFor(TomCat::Build::BuildFailure failure)
	{
		using TomCat::Build::BuildFailure;
		switch (failure)
		{
			case BuildFailure::EntryScene: return 7;
			case BuildFailure::ManagedCompile: return 8;
			case BuildFailure::Cook: return 9;
			case BuildFailure::ArtifactRead: return 11;
			case BuildFailure::PlayerBuild: return 12;
			case BuildFailure::None: break;
		}
		return 1;
	}

	int Run(const Options& options)
	{
		std::string error;
		TomCat::EditorProjectLock projectLock;
		const TomCat::ProjectLockAcquireResult lockResult =
			projectLock.Acquire(options.ProjectPath, error);
		if (lockResult != TomCat::ProjectLockAcquireResult::Acquired)
		{
			std::cerr << (lockResult == TomCat::ProjectLockAcquireResult::LiveOwner
				? "Project is already in use by another Editor or TomCatCLI process: "
				: "Project write lock is unavailable: ")
				<< error << '\n';
			return 13;
		}
		struct AssetSystemCleanup
		{
			~AssetSystemCleanup()
			{
				TomCat::AssetManager::Get().Shutdown();
				TomCat::AssetJobSystem::Get().Shutdown();
				std::string moduleError;
				(void)TomCat::ModuleSystem::Get().UnloadAllModules(moduleError);
			}
		} assetSystemCleanup;
		auto project = TomCat::Project::Load(options.ProjectPath);
		if (!project)
		{
			std::cerr << "Project load failed\n";
			return 5;
		}

		if (!TomCat::ModuleSystem::Get().LoadProjectModules(project->GetProjectDirectory(),
				error, true, TomCatModule::HostKind::Tool))
		{ std::cerr << "Project module initialization failed: " << error << '\n'; return 6; }

		TomCat::AssetManager& assets = TomCat::AssetManager::Get();
		if (!assets.SetProject(project) || !assets.Refresh())
		{
			std::cerr << "Asset registry initialization failed\n";
			return 6;
		}

		// The Editor runs the same pipeline (TomCatBuild/src/Build/BuildPipeline.h).
		auto fail = [](const TomCat::Build::BuildError& failure)
		{
			std::cerr << failure.Message << '\n';
			return ExitCodeFor(failure.Kind);
		};
		TomCat::Build::BuildError buildError;
		TomCat::AssetHandle entryScene;
		if (!TomCat::Build::ResolveEntryScene(project->GetBuildSettings(), entryScene, buildError))
			return fail(buildError);

		TomCat::ScriptProjectCompiler compiler;
		compiler.SetDiagnosticCallback([](const TomCat::ScriptCompilerDiagnostic& value)
		{
			std::ostream& stream = value.Level ==
				TomCat::ScriptCompilerDiagnostic::Severity::Error ? std::cerr : std::cout;
			if (!value.File.empty())
			{
				stream << value.File.string();
				if (value.Line > 0)
					stream << '(' << value.Line << ", " << value.Column << ')';
				stream << ": ";
			}
			stream << (value.Code.empty() ? "C#" : value.Code) << ": "
				<< value.Message << '\n';
		});
		TomCat::Build::ManagedCandidate candidate;
		if (!TomCat::Build::PrepareManagedCandidate(project, compiler, options.Profile,
			candidate, buildError))
			return fail(buildError);

		// Cook uses the exact candidate compiled above, including its profile.
		// Rediscovering last-good.json would silently select Development.
		TomCat::Build::CookOrBuildRequest request;
		request.Output = options.Operation == Command::Cook
			? TomCat::Build::BuildOutput::Package : TomCat::Build::BuildOutput::Player;
		request.ProjectInstance = project;
		request.EntryScene = entryScene;
		request.PackagePath = options.OutputPath;
		request.TemplateDirectory = options.TemplatePath;
		const TomCat::Build::CookOrBuildResult result =
			TomCat::Build::CookOrBuild(request, compiler, std::move(candidate));
		if (!result.Succeeded)
			return fail(result.Error);
		std::cout << result.Message << '\n';
		return 0;
	}

}

#ifdef TC_PLATFORM_WINDOWS
int wmain(int argc, wchar_t** nativeArgs)
#else
int main(int argc, char** nativeArgs)
#endif
{
    std::vector<std::string> argv;
    for (int index = 0; index < argc; ++index)
        argv.push_back(TomCat::PathToUTF8(std::filesystem::path(nativeArgs[index])));
	// Keep the caller's working directory intact so relative project/output/
	// template arguments retain their command-line meaning. Engine-owned package
	// assets are resolved independently from the CLI executable/runtime root.
	TomCat::ApplicationPaths::SetRuntimePackageRoot(ExecutableDirectory());
	TomCat::Log::Init(TomCat::ApplicationProduct::Unknown);
	const Options options = ParseOptions(argv);
	int exitCode = 0;
	if (!options.Error.empty())
	{
		std::cerr << "TomCatCLI: " << options.Error << "\n\n";
		PrintHelp();
		exitCode = 2;
	}
	else if (options.Operation == Command::Help)
		PrintHelp();
	else if (options.Operation == Command::Pak)
		exitCode = TomCat::CLI::InspectPackage(options.Pak);
	else
		exitCode = Run(options);
	TomCat::Log::Shutdown();
	TomCat::ApplicationPaths::ClearRuntimePackageRoot();
	return exitCode;
}
