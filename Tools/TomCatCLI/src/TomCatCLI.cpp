#include <TomCat.h>

#include "Player/PlayerBuilder.h"
#include "Scripting/ScriptProjectCompiler.h"

#include "TomCat/Asset/AssetJobSystem.h"
#include "TomCat/Asset/AssetManager.h"
#include "TomCat/Editor/EditorRecoveryService.h"
#include "TomCat/Project/Project.h"
#include "TomCat/Module/ModuleSystem.h"
#include "TomCat/Scripting/ManagedRuntimeFactory.h"
#include "TomCat/Utils/PathUtils.h"

#include <filesystem>
#include <fstream>
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
		return {};
#endif
	}

	enum class Command { Help, Cook, Build };

	struct Options
	{
		Command Operation = Command::Help;
		std::filesystem::path ProjectPath;
		std::filesystem::path OutputPath;
		std::filesystem::path TemplatePath;
        TomCat::ScriptBuildProfile Profile = TomCat::ScriptBuildProfile::Production;
		std::string Error;
	};

	void PrintHelp()
	{
		std::cout
			<< "TomCatCLI - deterministic headless project build/cook\n\n"
			<< "Usage:\n"
			<< "  TomCatCLI cook  --project <Project.tcproj> [--output <Game.tcpak>] [--development]\n"
			<< "  TomCatCLI build --project <Project.tcproj> [--template <directory>] [--development]\n\n"
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

	Options ParseOptions(int argc, wchar_t** argv)
	{
		Options result;
		if (argc < 2)
			return result;
		const std::wstring command(argv[1]);
		if (command == L"cook") result.Operation = Command::Cook;
		else if (command == L"build") result.Operation = Command::Build;
		else if (command == L"help" || command == L"--help" || command == L"-h")
			return result;
		else
		{
			result.Error = "unknown command";
			return result;
		}
		for (int index = 2; index < argc; ++index)
		{
			const std::wstring option(argv[index]);
			auto value = [&]() -> std::optional<std::filesystem::path>
			{
				if (++index >= argc) return std::nullopt;
				return std::filesystem::path(argv[index]);
			};
			if (option == L"--development") result.Profile = TomCat::ScriptBuildProfile::Development;
            else if (option == L"--project")
			{
				const auto parsed = value();
				if (!parsed) { result.Error = "--project requires a path"; return result; }
				result.ProjectPath = *parsed;
			}
			else if (option == L"--output")
			{
				const auto parsed = value();
				if (!parsed) { result.Error = "--output requires a path"; return result; }
				result.OutputPath = *parsed;
			}
			else if (option == L"--template")
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

	bool ReadBinary(const std::filesystem::path& path,
		std::vector<uint8_t>& bytes, std::string& error)
	{
		bytes.clear();
		std::ifstream input(path, std::ios::binary | std::ios::ate);
		const std::streamoff end = input ? static_cast<std::streamoff>(input.tellg()) : -1;
		if (end <= 0)
		{
			error = "could not read '" + TomCat::PathToUTF8(path) + "'";
			return false;
		}
		bytes.resize(static_cast<size_t>(end));
		input.seekg(0, std::ios::beg);
		if (!input.read(reinterpret_cast<char*>(bytes.data()), end))
		{
			error = "could not read the complete file '" + TomCat::PathToUTF8(path) + "'";
			return false;
		}
		return true;
	}

	bool CompileManaged(const TomCat::Ref<TomCat::Project>& project,
		TomCat::ScriptProjectCompiler& compiler, TomCat::ScriptBuildResult& build, TomCat::ScriptBuildProfile profile,
		std::string& error)
	{
		compiler.SetDiagnosticCallback([](const TomCat::ScriptCompilerDiagnostic& value)
		{
			std::ostream& stream = value.Level ==
				TomCat::ScriptCompilerDiagnostic::Severity::Error ? std::cerr : std::cout;
			stream << (value.Code.empty() ? "C#" : value.Code) << ": "
				<< value.Message << '\n';
		});
		if (!compiler.Configure(project, {}, {}, profile))
		{
			error = "could not configure the managed Release compiler";
			return false;
		}
		build = compiler.CompileNow();
		if (!build.Succeeded || build.SourceChangedDuringBuild
			|| build.BuildID.empty() || build.AssemblyPath.empty())
		{
			error = build.SourceChangedDuringBuild
				? "C# sources changed during compilation; run again"
				: "the managed Release build failed";
			return false;
		}
		return true;
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

		const TomCat::BuildSettings& settings = project->GetBuildSettings();
		if (static_cast<uint64_t>(settings.EntrySceneHandle) == 0)
		{
			std::cerr << "BuildSettings has no enabled entry scene\n";
			return 7;
		}
		TomCat::ScriptProjectCompiler compiler;
		TomCat::ScriptBuildResult managedBuild;
		if (!CompileManaged(project, compiler, managedBuild, options.Profile, error))
		{
			std::cerr << error << '\n';
			return 8;
		}


		std::string runtimeError;
		auto runtime = TomCat::Scripting::CreateManagedScriptRuntime(
			compiler.GetManagedRuntimeDirectory(), managedBuild.AssemblyPath,
			managedBuild.PdbPath, {}, &runtimeError);
		std::string manifest;
		if (!runtime || !runtime->IsReady() || !runtime->ReadProjectMetadata(manifest))
		{
			std::cerr << "Could not read the fresh managed manifest: "
				<< runtimeError << '\n';
			return 10;
		}
		runtime.reset();
		std::vector<uint8_t> assembly;
		if (!ReadBinary(managedBuild.AssemblyPath, assembly, error))
		{
			std::cerr << error << '\n';
			return 11;
		}

        if (options.Operation == Command::Cook)
        {
            // Use the exact build selected above, including its build profile.
            // Rediscovering last-good.json would silently select Development.
            std::vector<uint8_t> pdb;
            if (!managedBuild.PdbPath.empty() && !ReadBinary(managedBuild.PdbPath, pdb, error))
            {
                std::cerr << error << '\n';
                return 11;
            }
            if (!assets.SetManagedCookPayload(std::move(assembly), manifest,
                managedBuild.BuildID, std::move(pdb)))
            {
                std::cerr << "The fresh managed cook payload was rejected\n";
                return 9;
            }
            const std::filesystem::path output = options.OutputPath.empty()
                ? project->GetProjectDirectory() / "Build" / "Game.tcpak"
                : options.OutputPath;
            const bool cooked = assets.CookToPackage(output, settings.EntrySceneHandle);
            assets.ClearManagedCookPayload();
            if (!cooked)
            {
                std::cerr << "Cook failed; see diagnostics above\n";
                return 9;
            }
            std::cout << "Cook succeeded: " << TomCat::PathToUTF8(output) << '\n';
            return 0;
        }

		TomCat::PlayerBuildRequest request;
		request.ProjectInstance = project;
		request.EntryScene = settings.EntrySceneHandle;
		request.TemplateDirectory = options.TemplatePath.empty()
			? TomCat::PlayerBuilder::FindDefaultTemplateDirectory()
			: options.TemplatePath;
		request.ManagedAssembly = std::move(assembly);
		request.ScriptManifestJson = std::move(manifest);
		request.ScriptBuildID = managedBuild.BuildID;
		request.ValidateBeforePublish = [&compiler, managedBuild]()
		{
			return compiler.RefreshSourceState() && compiler.IsCurrentSourceBuilt()
				&& compiler.GetCurrentSourceHash() == managedBuild.SourceHash
				&& compiler.GetLastGoodBuildID() == managedBuild.BuildID;
		};
		const TomCat::PlayerBuildResult result = TomCat::PlayerBuilder::Build(
			std::move(request));
		if (!result.Succeeded)
		{
			std::cerr << result.Message << '\n';
			return 12;
		}
		std::cout << result.Message << '\n';
		return 0;
	}

}

int wmain(int argc, wchar_t** argv)
{
	// Keep the caller's working directory intact so relative project/output/
	// template arguments retain their command-line meaning. Engine-owned package
	// assets are resolved independently from the CLI executable/runtime root.
	TomCat::ApplicationPaths::SetRuntimePackageRoot(ExecutableDirectory());
	TomCat::Log::Init(TomCat::ApplicationProduct::Unknown);
	const Options options = ParseOptions(argc, argv);
	int exitCode = 0;
	if (!options.Error.empty())
	{
		std::cerr << "TomCatCLI: " << options.Error << "\n\n";
		PrintHelp();
		exitCode = 2;
	}
	else if (options.Operation == Command::Help)
		PrintHelp();
	else
		exitCode = Run(options);
	TomCat::Log::Shutdown();
	TomCat::ApplicationPaths::ClearRuntimePackageRoot();
	return exitCode;
}
