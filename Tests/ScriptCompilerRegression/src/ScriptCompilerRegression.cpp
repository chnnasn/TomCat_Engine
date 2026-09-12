#include "TomCat/Asset/AssetManager.h"
#include "TomCat/Core/Log.h"
#include "TomCat/Core/UUID.h"
#include "TomCat/Project/Project.h"
#include "TomCat/Scene/Components.h"
#include "TomCat/Scene/Entity.h"
#include "TomCat/Scene/Scene.h"
#include "TomCat/Scene/SceneSerializer.h"
#include "TomCat/Scene/Serialization/PrefabArchiveCodec.h"
#include "TomCat/Scripting/DotNetHost.h"
#include "TomCat/Scripting/ManagedRuntimeFactory.h"
#include "TomCat/Scripting/ScriptDiagnosticSink.h"
#include "TomCat/Scripting/ScriptEngine.h"

#include "Scripting/ScriptProjectCompiler.h"
#include "Scripting/ScriptMetadataCache.h"
#include "Player/PlayerBuilder.h"

#include <algorithm>
#include <charconv>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

#ifdef TC_PLATFORM_WINDOWS
#include <Windows.h>
#endif

namespace {

	void Require(bool condition, const std::string& message)
	{
		if (!condition)
			throw std::runtime_error(message);
	}

	void WriteTextFile(const std::filesystem::path& path, std::string_view contents)
	{
		std::filesystem::create_directories(path.parent_path());
		std::ofstream output(path, std::ios::binary | std::ios::trunc);
		Require(static_cast<bool>(output), "could not open " + path.string());
		output.write(contents.data(), static_cast<std::streamsize>(contents.size()));
		Require(static_cast<bool>(output), "could not write " + path.string());
	}

	std::string ReadTextFile(const std::filesystem::path& path)
	{
		std::ifstream input(path, std::ios::binary);
		Require(static_cast<bool>(input), "could not read " + path.string());
		return { std::istreambuf_iterator<char>(input),
			std::istreambuf_iterator<char>() };
	}

	std::vector<uint8_t> ReadBytes(const std::filesystem::path& path)
	{
		std::ifstream input(path, std::ios::binary);
		Require(static_cast<bool>(input), "could not read " + path.string());
		return { std::istreambuf_iterator<char>(input),
			std::istreambuf_iterator<char>() };
	}

	void WriteBytes(const std::filesystem::path& path,
		const std::vector<uint8_t>& bytes)
	{
		std::filesystem::create_directories(path.parent_path());
		std::ofstream output(path, std::ios::binary | std::ios::trunc);
		Require(static_cast<bool>(output), "could not open " + path.string());
		if (!bytes.empty())
			output.write(reinterpret_cast<const char*>(bytes.data()),
				static_cast<std::streamsize>(bytes.size()));
		Require(static_cast<bool>(output), "could not write " + path.string());
	}

	std::optional<std::string> ReadEnvironment(const char* name)
	{
		char* value = nullptr;
		size_t length = 0;
		if (_dupenv_s(&value, &length, name) != 0 || !value)
			return std::nullopt;
		std::string result(value);
		std::free(value);
		return result;
	}

	void WriteEnvironment(const char* name,
		const std::optional<std::string>& value)
	{
		Require(_putenv_s(name, value ? value->c_str() : "") == 0,
			std::string("could not update environment variable ") + name);
	}

	class ScopedEnvironmentVariable final
	{
	public:
		ScopedEnvironmentVariable(const char* name,
			const std::optional<std::string>& value)
			: m_Name(name), m_Previous(ReadEnvironment(name))
		{
			WriteEnvironment(m_Name.c_str(), value);
		}

		~ScopedEnvironmentVariable()
		{
			_putenv_s(m_Name.c_str(), m_Previous ? m_Previous->c_str() : "");
		}

		ScopedEnvironmentVariable(const ScopedEnvironmentVariable&) = delete;
		ScopedEnvironmentVariable& operator=(
			const ScopedEnvironmentVariable&) = delete;

	private:
		std::string m_Name;
		std::optional<std::string> m_Previous;
	};

	class ScopedDotNetEnvironmentIsolation final
	{
	public:
		explicit ScopedDotNetEnvironmentIsolation(bool enabled)
			: m_Enabled(enabled)
		{
			if (!m_Enabled)
				return;
			m_Path = ReadEnvironment("PATH");
			m_DotNetRoot = ReadEnvironment("DOTNET_ROOT");
			m_DotNetRootX64 = ReadEnvironment("DOTNET_ROOT_X64");
			m_ProgramFiles = ReadEnvironment("ProgramFiles");
			WriteEnvironment("PATH", std::string{});
			WriteEnvironment("DOTNET_ROOT",
				std::string("Z:\\TomCat-No-Global-DotNet"));
			WriteEnvironment("DOTNET_ROOT_X64",
				std::string("Z:\\TomCat-No-Global-DotNet"));
			WriteEnvironment("ProgramFiles",
				std::string("Z:\\TomCat-No-ProgramFiles"));
		}

		~ScopedDotNetEnvironmentIsolation()
		{
			if (!m_Enabled)
				return;
			_putenv_s("PATH", m_Path ? m_Path->c_str() : "");
			_putenv_s("DOTNET_ROOT", m_DotNetRoot ? m_DotNetRoot->c_str() : "");
			_putenv_s("DOTNET_ROOT_X64", m_DotNetRootX64
				? m_DotNetRootX64->c_str() : "");
			_putenv_s("ProgramFiles", m_ProgramFiles ? m_ProgramFiles->c_str() : "");
		}

		ScopedDotNetEnvironmentIsolation(
			const ScopedDotNetEnvironmentIsolation&) = delete;
		ScopedDotNetEnvironmentIsolation& operator=(
			const ScopedDotNetEnvironmentIsolation&) = delete;

	private:
		bool m_Enabled = false;
		std::optional<std::string> m_Path;
		std::optional<std::string> m_DotNetRoot;
		std::optional<std::string> m_DotNetRootX64;
		std::optional<std::string> m_ProgramFiles;
	};

	class ScopedDiagnosticCapture final
	{
	public:
		explicit ScopedDiagnosticCapture(std::vector<TomCat::Scripting::ScriptDiagnostic>& output)
		{
			TomCat::Scripting::SetScriptDiagnosticSink(
				[&output](const TomCat::Scripting::ScriptDiagnostic& diagnostic)
				{
					output.push_back(diagnostic);
				});
		}

		~ScopedDiagnosticCapture()
		{
			TomCat::Scripting::SetScriptDiagnosticSink({});
		}

		ScopedDiagnosticCapture(const ScopedDiagnosticCapture&) = delete;
		ScopedDiagnosticCapture& operator=(const ScopedDiagnosticCapture&) = delete;
	};

	std::string FormatDiagnostics(const TomCat::ScriptBuildResult& result)
	{
		std::ostringstream output;
		for (const TomCat::ScriptCompilerDiagnostic& diagnostic : result.Diagnostics)
			output << '[' << diagnostic.Code << "] " << diagnostic.Message << '\n';
		return output.str();
	}

	class TemporaryScriptProject final
	{
	public:
		TemporaryScriptProject()
		{
			TomCat::AssetManager::Get().Shutdown();
			Root = std::filesystem::temp_directory_path() /
				("tomcat_script_compiler_" +
					std::to_string(static_cast<uint64_t>(TomCat::UUID())));
		}

		~TemporaryScriptProject()
		{
			TomCat::AssetManager::Get().Shutdown();
			std::error_code error;
			std::filesystem::remove_all(Root, error);
		}

		std::filesystem::path Root;
	};

	class TemporaryDirectory final
	{
	public:
		explicit TemporaryDirectory(std::string_view prefix)
		{
			Root = std::filesystem::temp_directory_path()
				/ (std::string(prefix) + std::to_string(
					static_cast<uint64_t>(TomCat::UUID())));
			std::filesystem::create_directories(Root);
		}

		~TemporaryDirectory()
		{
			std::error_code error;
			std::filesystem::remove_all(Root, error);
		}

		TemporaryDirectory(const TemporaryDirectory&) = delete;
		TemporaryDirectory& operator=(const TemporaryDirectory&) = delete;

		std::filesystem::path Root;
	};

#ifdef TC_PLATFORM_WINDOWS
	class ScopedPath final
	{
	public:
		explicit ScopedPath(const std::filesystem::path& replacement)
		{
			const DWORD required = GetEnvironmentVariableW(L"PATH", nullptr, 0);
			if (required != 0)
			{
				std::vector<wchar_t> buffer(required);
				const DWORD copied = GetEnvironmentVariableW(
					L"PATH", buffer.data(), static_cast<DWORD>(buffer.size()));
				if (copied != 0)
				{
					m_HadValue = true;
					m_Value.assign(buffer.data(), copied);
				}
			}
			Require(SetEnvironmentVariableW(L"PATH", replacement.c_str()) != FALSE,
				"could not isolate PATH for the missing SDK diagnostic test");
		}

		~ScopedPath()
		{
			SetEnvironmentVariableW(L"PATH", m_HadValue ? m_Value.c_str() : nullptr);
		}

	private:
		bool m_HadValue = false;
		std::wstring m_Value;
	};

	void TestMissingDotNet10SdkIsActionable()
	{
		TemporaryScriptProject environment;
		TomCat::ProjectConfig config;
		config.Name = "Missing SDK Regression";
		config.AssetDirectory = "Assets";
		const TomCat::Ref<TomCat::Project> project = TomCat::Project::CreateNew(
			environment.Root / "Project.tcproj", config);
		Require(project != nullptr, "could not create the missing SDK test project");

		std::vector<TomCat::ScriptCompilerDiagnostic> diagnostics;
		TomCat::ScriptProjectCompiler compiler;
		compiler.SetDiagnosticCallback(
			[&diagnostics](const TomCat::ScriptCompilerDiagnostic& diagnostic)
			{
				diagnostics.push_back(diagnostic);
			});
		bool configured = true;
		{
			ScopedPath isolatedPath(environment.Root);
			configured = compiler.Configure(project);
		}

		Require(!configured,
			"ScriptProjectCompiler configured without a discoverable dotnet executable");
		const auto diagnostic = std::find_if(diagnostics.begin(), diagnostics.end(),
			[](const TomCat::ScriptCompilerDiagnostic& candidate)
			{
				return candidate.Code == "TCSP0020";
			});
		Require(diagnostic != diagnostics.end(),
			"missing .NET 10 SDK did not emit TCSP0020");
		Require(diagnostic->Message.find(".NET 10 SDK") != std::string::npos &&
			diagnostic->Message.find("dotnet.exe") != std::string::npos,
			"missing SDK diagnostic did not explain the required SDK and PATH fix");
	}
#endif

	void TestMSBuildDependencyInjectionIsBlocked()
	{
		TemporaryScriptProject environment;
		TomCat::ProjectConfig config;
		config.Name = "MSBuild Injection Regression";
		config.AssetDirectory = "Assets";
		const TomCat::Ref<TomCat::Project> project = TomCat::Project::CreateNew(
			environment.Root / "Project.tcproj", config);
		Require(project != nullptr,
			"could not create the MSBuild injection test project");

		const std::filesystem::path scriptPath =
			project->GetAssetPath() / "Scripts" / "MSBuildGuardProbe.cs";
		WriteTextFile(scriptPath,
			"using TomCat;\n"
			"namespace Regression;\n"
			"public sealed class MSBuildGuardProbe : TomCatBehaviour\n"
			"{\n"
			"    public int Count = 1;\n"
			"}\n");

		TomCat::AssetManager& assets = TomCat::AssetManager::Get();
		Require(assets.SetProject(project),
			"AssetManager could not open the MSBuild injection test project");
		TomCat::ScriptProjectCompiler compiler;
		Require(compiler.Configure(project),
			"ScriptProjectCompiler configuration failed for the injection test");
		const TomCat::ScriptBuildResult baseline = compiler.CompileNow();
		Require(baseline.Succeeded,
			"baseline build failed before MSBuild injection:\n"
			+ FormatDiagnostics(baseline));

		const std::string poisonProject = R"XML(<Project>
  <ItemGroup>
    <PackageReference Include="TomCat.SecurityRegression.MustNotRestore" Version="0.0.0" />
    <Reference Include="TomCat.SecurityRegression.UntrustedLocal">
      <HintPath>Z:\TomCat-Security-Regression\Untrusted.dll</HintPath>
    </Reference>
  </ItemGroup>
  <Target Name="TomCatSecurityRegressionPoison"
          BeforeTargets="_GenerateRestoreProjectSpec;PrepareForBuild;CoreCompile">
    <Error Code="TCINJECT0001" Text="TOMCAT_MSBUILD_INJECTION_EXECUTED" />
  </Target>
</Project>
)XML";
		WriteTextFile(environment.Root / "Directory.Build.props", poisonProject);
		WriteTextFile(environment.Root / "Directory.Build.targets", poisonProject);
		WriteTextFile(environment.Root / "Directory.Packages.props", poisonProject);

		const std::filesystem::path userExtensions =
			environment.Root / "MSBuildUserExtensions";
		const std::vector<std::filesystem::path> userImportHooks = {
			userExtensions / "Current" / "Imports" / "Microsoft.Common.props"
				/ "ImportBefore" / "TomCat.SecurityRegression.props",
			userExtensions / "Current" / "Imports" / "Microsoft.Common.props"
				/ "ImportAfter" / "TomCat.SecurityRegression.props",
			userExtensions / "Current" / "Microsoft.Common.targets"
				/ "ImportBefore" / "TomCat.SecurityRegression.targets",
			userExtensions / "Current" / "Microsoft.Common.targets"
				/ "ImportAfter" / "TomCat.SecurityRegression.targets",
			userExtensions / "Current" / "Microsoft.CSharp.targets"
				/ "ImportBefore" / "TomCat.SecurityRegression.targets",
			userExtensions / "Current" / "Microsoft.CSharp.targets"
				/ "ImportAfter" / "TomCat.SecurityRegression.targets"
		};
		for (const std::filesystem::path& hook : userImportHooks)
			WriteTextFile(hook, poisonProject);

		// Force a new source hash so this exercises a fresh candidate rather than
		// accepting the baseline last-good assembly.
		WriteTextFile(scriptPath,
			"using TomCat;\n"
			"namespace Regression;\n"
			"public sealed class MSBuildGuardProbe : TomCatBehaviour\n"
			"{\n"
			"    public int Count = 2;\n"
			"}\n");
		TomCat::ScriptBuildResult guarded;
		{
			ScopedEnvironmentVariable scopedUserExtensions("MSBuildUserExtensionsPath",
				std::optional<std::string>(userExtensions.string()));
			guarded = compiler.CompileNow();
		}
		Require(guarded.Succeeded,
			"Directory/NuGet/user MSBuild injection reached the generated project:\n"
			+ FormatDiagnostics(guarded));
		Require(guarded.BuildID != baseline.BuildID,
			"guarded build reused the baseline candidate instead of compiling");
		Require(std::none_of(guarded.Diagnostics.begin(), guarded.Diagnostics.end(),
			[](const TomCat::ScriptCompilerDiagnostic& diagnostic)
			{
				return diagnostic.Code == "TCINJECT0001" ||
					diagnostic.Message.find("TOMCAT_MSBUILD_INJECTION_EXECUTED")
					!= std::string::npos;
			}), "an injected MSBuild target executed inside script compilation");

		const std::string generatedProject = ReadTextFile(
			project->GetLibraryPath() / "ScriptProject" / "Assembly-CSharp.csproj");
		const size_t sdkPropsImport = generatedProject.find(
			"<Import Project=\"Sdk.props\" Sdk=\"Microsoft.NET.Sdk\" />");
		Require(sdkPropsImport != std::string::npos,
			"generated project did not use an explicit, lockable Sdk.props import");
		for (const std::string_view policy : {
			"<ImportDirectoryBuildProps>false</ImportDirectoryBuildProps>",
			"<ImportDirectoryBuildTargets>false</ImportDirectoryBuildTargets>",
			"<ImportDirectoryPackagesProps>false</ImportDirectoryPackagesProps>",
			"<ImportProjectExtensionProps>false</ImportProjectExtensionProps>",
			"<ImportProjectExtensionTargets>false</ImportProjectExtensionTargets>",
			"<RestoreEnableGlobalPackageReference>false</RestoreEnableGlobalPackageReference>",
			"<ImportUserLocationsByWildcardBeforeMicrosoftCommonProps>false",
			"<ImportUserLocationsByWildcardAfterMicrosoftCSharpTargets>false" })
		{
			const size_t position = generatedProject.find(policy);
			Require(position != std::string::npos && position < sdkPropsImport,
				"generated project did not lock an MSBuild import policy before Sdk.props");
		}
		const size_t guardTarget = generatedProject.find(
			"<Target Name=\"TomCatValidateBuildInputs\"");
		const size_t packageGuard = generatedProject.find(
			"<_TomCatBlockedPackageItem Include=\"@(PackageReference)");
		const size_t referenceGuard = generatedProject.find(
			"<_TomCatBlockedReference Include=\"@(Reference)\"");
		const size_t sdkTargetsImport = generatedProject.find(
			"<Import Project=\"Sdk.targets\" Sdk=\"Microsoft.NET.Sdk\" />");
		Require(guardTarget != std::string::npos && sdkTargetsImport != std::string::npos
			&& packageGuard != std::string::npos
			&& referenceGuard != std::string::npos
			&& packageGuard < guardTarget && referenceGuard < guardTarget
			&& guardTarget < sdkTargetsImport,
			"generated project omitted its NuGet/local-reference rejection target");
		Require(generatedProject.find("TomCat.SecurityRegression.MustNotRestore")
			== std::string::npos
			&& generatedProject.find("TomCat.SecurityRegression.UntrustedLocal")
			== std::string::npos,
			"generated project incorporated a dependency from an external MSBuild file");
	}

	void TestAssetRefMarkerValidation()
	{
		TemporaryScriptProject environment;
		TomCat::ProjectConfig config;
		config.Name = "AssetRef Marker Regression";
		config.AssetDirectory = "Assets";
		const TomCat::Ref<TomCat::Project> project = TomCat::Project::CreateNew(
			environment.Root / "Project.tcproj", config);
		Require(project != nullptr,
			"could not create the AssetRef marker test project");

		const std::filesystem::path scriptPath =
			project->GetAssetPath() / "Scripts" / "AssetRefMarkerProbe.cs";
		WriteTextFile(scriptPath,
			"using TomCat;\n"
			"namespace Regression;\n"
			"public sealed class UnknownAsset;\n"
			"public sealed class AssetRefMarkerProbe : TomCatBehaviour\n"
			"{\n"
			"    public AssetRef<UnknownAsset> Asset;\n"
			"}\n");

		TomCat::AssetManager& assets = TomCat::AssetManager::Get();
		Require(assets.SetProject(project),
			"AssetManager could not open the AssetRef marker test project");
		TomCat::ScriptProjectCompiler compiler;
		Require(compiler.Configure(project),
			"ScriptProjectCompiler configuration failed for the AssetRef marker test");
		const TomCat::ScriptBuildResult unsupported = compiler.CompileNow();
		Require(!unsupported.Succeeded,
			"script generator accepted an unknown AssetRef<T> marker");
		Require(std::any_of(unsupported.Diagnostics.begin(), unsupported.Diagnostics.end(),
			[](const TomCat::ScriptCompilerDiagnostic& diagnostic)
			{
				return diagnostic.Code == "TCG010";
			}), "unknown AssetRef<T> marker did not emit TCG010");

		WriteTextFile(scriptPath,
			"using TomCat;\n"
			"namespace Regression;\n"
			"public sealed class AssetRefMarkerProbe : TomCatBehaviour\n"
			"{\n"
			"    public AssetRef<Texture2DAsset> Asset;\n"
			"}\n");
		const TomCat::ScriptBuildResult supported = compiler.CompileNow();
		Require(supported.Succeeded,
			"script generator rejected the built-in Texture2DAsset marker:\n"
			+ FormatDiagnostics(supported));
	}

	void TestFailedBuildPreservesLastGood()
	{
		TemporaryScriptProject environment;
		TomCat::ProjectConfig config;
		config.Name = "Script Compiler Regression";
		config.AssetDirectory = "Assets";
		config.StartScene = "Main.tomcat";
		config.StartSceneHandle = TomCat::AssetHandle(0);
		const TomCat::Ref<TomCat::Project> project = TomCat::Project::CreateNew(
			environment.Root / "Project.tcproj", config);
		Require(project != nullptr, "could not create the temporary project");

		const std::filesystem::path scriptPath =
			project->GetAssetPath() / "Scripts" / "LastGoodProbe.cs";
		WriteTextFile(scriptPath,
			"using TomCat;\n"
			"namespace Regression;\n"
			"public sealed class LastGoodProbe : TomCatBehaviour\n"
			"{\n"
			"    public int Count = 7;\n"
			"}\n");

		TomCat::AssetManager& assets = TomCat::AssetManager::Get();
		Require(assets.SetProject(project),
			"AssetManager could not open the temporary project");

		TomCat::ScriptProjectCompiler compiler;
		std::vector<TomCat::ScriptCompilerDiagnostic> emitted;
		compiler.SetDiagnosticCallback(
			[&emitted](const TomCat::ScriptCompilerDiagnostic& diagnostic)
			{
				emitted.push_back(diagnostic);
			});
		Require(compiler.Configure(project),
			"ScriptProjectCompiler configuration failed");

		const TomCat::ScriptBuildResult good = compiler.CompileNow();
		Require(good.Succeeded,
			"initial valid script build failed:\n" + FormatDiagnostics(good));
		Require(compiler.IsCurrentSourceBuilt(),
			"successful build was not promoted to current last-good");

		const std::string lastGoodBuildID = compiler.GetLastGoodBuildID();
		const std::filesystem::path lastGoodAssembly =
			compiler.GetLastGoodAssemblyPath();
		const std::filesystem::path lastGoodJson =
			project->GetLibraryPath() / "ScriptAssemblies" / "last-good.json";
		Require(!lastGoodBuildID.empty() &&
			std::filesystem::is_regular_file(lastGoodAssembly) &&
			std::filesystem::is_regular_file(lastGoodJson),
			"successful build did not produce a complete last-good record");
		const std::string jsonBefore = ReadTextFile(lastGoodJson);
		const std::vector<uint8_t> assemblyBefore = ReadBytes(lastGoodAssembly);
		Require(!assemblyBefore.empty(), "last-good assembly was empty");

		WriteTextFile(scriptPath,
			"using TomCat;\n"
			"namespace Regression;\n"
			"public sealed class LastGoodProbe : TomCatBehaviour\n"
			"{\n"
			"    public int Count = ;\n"
			"}\n");

		const TomCat::ScriptBuildResult broken = compiler.CompileNow();
		Require(!broken.Succeeded,
			"syntactically invalid C# source unexpectedly compiled");
		Require(broken.ExitCode != 0,
			"invalid source failed without a real compiler exit code");
		Require(std::any_of(broken.Diagnostics.begin(), broken.Diagnostics.end(),
			[](const TomCat::ScriptCompilerDiagnostic& diagnostic)
			{
				return diagnostic.Level ==
					TomCat::ScriptCompilerDiagnostic::Severity::Error;
			}), "failed build did not return an actionable error diagnostic");
		Require(compiler.GetLastGoodBuildID() == lastGoodBuildID,
			"failed build replaced the last-good build ID");
		Require(compiler.GetLastGoodAssemblyPath() == lastGoodAssembly,
			"failed build replaced the last-good assembly path");
		Require(ReadTextFile(lastGoodJson) == jsonBefore,
			"failed build modified last-good.json");
		Require(ReadBytes(lastGoodAssembly) == assemblyBefore,
			"failed build modified the last-good assembly bytes");
	}

	bool EnvironmentFlag(const char* name)
	{
		const std::optional<std::string> value = ReadEnvironment(name);
		return value && (*value == "1" || *value == "true" || *value == "TRUE");
	}

	TomCat::AssetHandle RequireScriptHandle(TomCat::AssetManager& assets,
		const std::filesystem::path& path)
	{
		const TomCat::AssetMetadata* metadata = assets.Registry().GetMetadata(path);
		Require(metadata && !metadata->IsMissing
			&& metadata->Type == TomCat::AssetType::CSharpScript
			&& static_cast<uint64_t>(metadata->Handle) != 0,
			"script source was not imported with a stable CSharpScript handle: "
			+ path.string());
		return metadata->Handle;
	}

	TomCat::CSharpScriptEntry MakeScriptEntry(
		const TomCat::ScriptMetadataCache& metadataCache,
		TomCat::AssetHandle scriptHandle)
	{
		const std::optional<TomCat::EditorScriptMetadata> metadata =
			metadataCache.Find(scriptHandle);
		Require(metadata.has_value(),
			"compiled manifest omitted an imported C# script handle");
		TomCat::CSharpScriptEntry entry;
		entry.ScriptAsset = scriptHandle;
		entry.LastKnownClassName = metadata->TypeName;
		TomCat::ReconcileScriptEntryFields(entry, *metadata);
		return entry;
	}

	void SetInt32Field(TomCat::CSharpScriptEntry& entry,
		std::string_view name, int32_t value)
	{
		const auto field = std::find_if(entry.Fields.begin(), entry.Fields.end(),
			[name](const TomCat::ScriptField& candidate)
			{
				return candidate.Name == name;
			});
		Require(field != entry.Fields.end()
			&& field->Type == TomCat::ScriptFieldType::Int32,
			"compiled manifest omitted the expected Int32 field");
		field->Value = value;
	}

	void SetAssetField(TomCat::CSharpScriptEntry& entry,
		std::string_view name, TomCat::AssetHandle value,
		std::string_view expectedTypeName)
	{
		const auto field = std::find_if(entry.Fields.begin(), entry.Fields.end(),
			[name](const TomCat::ScriptField& candidate)
			{
				return candidate.Name == name;
			});
		Require(field != entry.Fields.end()
			&& field->Type == TomCat::ScriptFieldType::AssetRef
			&& field->TypeName == expectedTypeName,
			"compiled manifest omitted the expected typed asset field");
		field->Value = static_cast<uint64_t>(value);
	}

	void SetPosition(TomCat::Entity entity, const glm::vec3& position)
	{
		auto& transform = entity.GetComponent<TomCat::Transform>();
		transform._Translation = position;
		transform._LocalTranslation = position;
	}

	void VerifyRuntimeBehaviour(const TomCat::Ref<TomCat::Scene>& scene,
		TomCat::UUID lifecycleEntityID, TomCat::UUID triggerEntityID,
		const std::shared_ptr<TomCat::Scripting::IScriptRuntime>& runtime)
	{
		std::vector<TomCat::Scripting::ScriptDiagnostic> diagnostics;
		ScopedDiagnosticCapture diagnosticCapture(diagnostics);
		TomCat::Scripting::ScriptEngine::Get().SetRuntime(runtime);
		bool started = false;
		try
		{
			started = scene->OnRuntimeStart();
			Require(started, "scene rejected the real compiled managed runtime");
			scene->OnUpdateRuntime(TomCat::Timestep(TomCat::Scene::FixedRuntimeTimestep));

			TomCat::Entity lifecycle = scene->FindEntityByUUID(lifecycleEntityID);
			TomCat::Entity trigger = scene->FindEntityByUUID(triggerEntityID);
			Require(lifecycle && trigger,
				"runtime invalidated smoke-test entities unexpectedly");
			Require(lifecycle.GetGameplayTag() == "updated",
				"serialized field or OnCreate/OnEnable/OnFixedUpdate/OnUpdate order was wrong");
			Require(lifecycle.GetName() == "collision-received",
				"compiled C# script did not receive CollisionEnter2D");
			Require(trigger.GetName() == "trigger-received",
				"compiled C# script did not receive TriggerEnter2D");
			const std::optional<glm::vec2> velocity =
				scene->GetLinearVelocity2D(lifecycleEntityID);
			Require(velocity && velocity->y > 1.0f,
				"compiled C# script did not apply a 2D impulse during FixedUpdate");
			Require(std::any_of(diagnostics.begin(), diagnostics.end(),
				[](const TomCat::Scripting::ScriptDiagnostic& diagnostic)
				{
					return diagnostic.Severity ==
						TomCat::Scripting::ScriptDiagnosticSeverity::Error
						&& diagnostic.Message.find("intentional e2e failure")
							!= std::string::npos;
				}), "faulting script did not emit its managed exception diagnostic");
		}
		catch (...)
		{
			if (started)
				scene->OnRuntimeStop();
			TomCat::Scripting::ScriptEngine::Get().SetRuntime({});
			throw;
		}
		scene->OnRuntimeStop();
		TomCat::Scripting::ScriptEngine::Get().SetRuntime({});
	}

	void ValidateWithIndependentPlayer(const std::filesystem::path& packagePath)
	{
		const std::optional<std::string> playerVariable =
			ReadEnvironment("TOMCAT_E2E_PLAYER_EXE");
		if (!playerVariable || playerVariable->empty())
		{
			Require(!EnvironmentFlag("TOMCAT_E2E_REQUIRE_PLAYER"),
				"independent Player smoke was required, but TOMCAT_E2E_PLAYER_EXE was not set");
			std::cout << "SKIP independent Player CLI validation (TomCatPlayer.exe was not supplied)\n";
			return;
		}

		const std::filesystem::path player = *playerVariable;
		Require(std::filesystem::is_regular_file(player),
			"TOMCAT_E2E_PLAYER_EXE does not name a Player executable");
#ifdef TC_PLATFORM_WINDOWS
		std::wstring command = L"\"" + player.wstring()
			+ L"\" --validate-package \"" + packagePath.wstring() + L"\"";
		std::vector<wchar_t> mutableCommand(command.begin(), command.end());
		mutableCommand.push_back(L'\0');
		STARTUPINFOW startup{};
		startup.cb = sizeof(startup);
		PROCESS_INFORMATION process{};
		const std::filesystem::path workingDirectory = packagePath.parent_path();
		const BOOL created = CreateProcessW(player.c_str(), mutableCommand.data(),
			nullptr, nullptr, FALSE, CREATE_NO_WINDOW, nullptr,
			workingDirectory.c_str(), &startup, &process);
		Require(created != FALSE,
			"could not launch TomCatPlayer.exe --validate-package");
		WaitForSingleObject(process.hProcess, INFINITE);
		DWORD exitCode = 1;
		GetExitCodeProcess(process.hProcess, &exitCode);
		CloseHandle(process.hThread);
		CloseHandle(process.hProcess);
		Require(exitCode == 0,
			"TomCatPlayer.exe rejected the freshly cooked package (exit code "
			+ std::to_string(exitCode) + ")");
#else
		(void)packagePath;
		Require(false, "independent Player smoke is supported only on Windows x64");
#endif
	}

#ifdef TC_PLATFORM_WINDOWS
	void RunPlayerProcessAndExpectScriptExit(
		const std::filesystem::path& player, std::wstring command,
		const std::filesystem::path& workingDirectory,
		uint32_t expectedScriptExitCode, const std::string& description)
	{
		// EntryPoint anchors packaged Player process state to the executable root
		// before Log::Init, independent of the caller-provided working directory.
		const std::filesystem::path logPath = player.parent_path() / "TomCat.log";
		std::error_code removeError;
		std::filesystem::remove(logPath, removeError);
		Require(!removeError,
			"could not clear the " + description + " log: " + removeError.message());

		std::vector<wchar_t> mutableCommand(command.begin(), command.end());
		mutableCommand.push_back(L'\0');
		STARTUPINFOW startup{};
		startup.cb = sizeof(startup);
		PROCESS_INFORMATION process{};
		const BOOL created = CreateProcessW(player.c_str(), mutableCommand.data(),
			nullptr, nullptr, FALSE, CREATE_NO_WINDOW, nullptr,
			workingDirectory.c_str(), &startup, &process);
		Require(created != FALSE,
			"could not launch " + description);
		CloseHandle(process.hThread);

		const DWORD waitResult = WaitForSingleObject(process.hProcess, 30000);
		if (waitResult == WAIT_TIMEOUT)
		{
			TerminateProcess(process.hProcess, 124);
			WaitForSingleObject(process.hProcess, 5000);
			CloseHandle(process.hProcess);
			throw std::runtime_error(
				description + " timed out before the scripted completion signal");
		}
		if (waitResult != WAIT_OBJECT_0)
		{
			TerminateProcess(process.hProcess, 125);
			WaitForSingleObject(process.hProcess, 5000);
			CloseHandle(process.hProcess);
			throw std::runtime_error(
				"waiting for " + description + " failed");
		}
		DWORD exitCode = 1;
		const BOOL readExitCode = GetExitCodeProcess(process.hProcess, &exitCode);
		CloseHandle(process.hProcess);
		Require(readExitCode != FALSE,
			"could not read the " + description + " exit code");

		const std::string playerLog = std::filesystem::is_regular_file(logPath)
			? ReadTextFile(logPath) : std::string{};
		Require(exitCode == expectedScriptExitCode,
			description + " returned exit "
			+ std::to_string(exitCode) + "; expected the script-only completion code "
			+ std::to_string(expectedScriptExitCode) + "\n"
			+ playerLog);
		std::cout << "PASS " << description << " reached script completion exit "
			<< exitCode << '\n';
	}
#endif

	void RunWithIndependentPlayer(const std::filesystem::path& packagePath,
		uint32_t expectedScriptExitCode)
	{
		const std::optional<std::string> playerVariable =
			ReadEnvironment("TOMCAT_E2E_PLAYER_EXE");
		if (!playerVariable || playerVariable->empty())
		{
			Require(!EnvironmentFlag("TOMCAT_E2E_REQUIRE_PLAYER"),
				"real Player runtime smoke was required, but TOMCAT_E2E_PLAYER_EXE was not set");
			std::cout << "SKIP real TomCatPlayer --package runtime (TomCatPlayer.exe was not supplied)\n";
			return;
		}

		const std::filesystem::path player = *playerVariable;
		Require(std::filesystem::is_regular_file(player),
			"TOMCAT_E2E_PLAYER_EXE does not name a Player executable");
#ifdef TC_PLATFORM_WINDOWS
		const std::filesystem::path workingDirectory = packagePath.parent_path();
		std::wstring command = L"\"" + player.wstring()
			+ L"\" --package \"" + packagePath.wstring() + L"\"";
		RunPlayerProcessAndExpectScriptExit(player, std::move(command),
			workingDirectory, expectedScriptExitCode,
			"real TomCatPlayer --package runtime");
#else
		(void)packagePath;
		(void)expectedScriptExitCode;
		Require(false, "real Player runtime smoke is supported only on Windows x64");
#endif
	}

	void RunBuiltPlayerWithoutArguments(const std::filesystem::path& player,
		const std::filesystem::path& workingDirectory,
		uint32_t expectedScriptExitCode)
	{
		Require(std::filesystem::is_regular_file(player),
			"PlayerBuilder output omitted TomCatPlayer.exe");
		Require(std::filesystem::is_directory(workingDirectory),
			"no-argument Player smoke working directory is missing");
#ifdef TC_PLATFORM_WINDOWS
		std::error_code equivalentError;
		const bool sameDirectory = std::filesystem::equivalent(
			player.parent_path(), workingDirectory, equivalentError);
		Require(!equivalentError && !sameDirectory,
			"no-argument Player smoke must launch from outside the exported game directory");
		std::wstring command = L"\"" + player.wstring() + L"\"";
		RunPlayerProcessAndExpectScriptExit(player, std::move(command),
			workingDirectory, expectedScriptExitCode,
			"PlayerBuilder output with no command-line arguments");
#else
		(void)expectedScriptExitCode;
		Require(false, "real Player runtime smoke is supported only on Windows x64");
#endif
	}

	std::filesystem::path CurrentExecutablePath()
	{
#ifdef TC_PLATFORM_WINDOWS
		std::vector<wchar_t> buffer(MAX_PATH);
		for (;;)
		{
			const DWORD length = GetModuleFileNameW(nullptr, buffer.data(),
				static_cast<DWORD>(buffer.size()));
			Require(length != 0, "could not resolve the smoke executable path");
			if (length < buffer.size() - 1)
				return std::filesystem::path(std::wstring(buffer.data(), length));
			Require(buffer.size() < 32768,
				"smoke executable path exceeds the Windows path limit");
			buffer.resize(buffer.size() * 2);
		}
#else
		return {};
#endif
	}

	uint64_t ParseUInt64(std::string_view value, const char* description)
	{
		uint64_t result = 0;
		const auto parsed = std::from_chars(value.data(), value.data() + value.size(),
			result);
		Require(parsed.ec == std::errc{} && parsed.ptr == value.data() + value.size()
			&& result != 0, std::string("invalid ") + description);
		return result;
	}

	void RunFreshPrivateRuntimeProcess(const std::filesystem::path& packagePath,
		TomCat::UUID lifecycleEntityID, TomCat::UUID triggerEntityID)
	{
#ifdef TC_PLATFORM_WINDOWS
		const std::filesystem::path executable = CurrentExecutablePath();
		std::wstring command = L"\"" + executable.wstring()
			+ L"\" --cooked-runtime-only \"" + packagePath.wstring() + L"\" "
			+ std::to_wstring(static_cast<uint64_t>(lifecycleEntityID)) + L" "
			+ std::to_wstring(static_cast<uint64_t>(triggerEntityID));
		std::vector<wchar_t> mutableCommand(command.begin(), command.end());
		mutableCommand.push_back(L'\0');
		STARTUPINFOW startup{};
		startup.cb = sizeof(startup);
		PROCESS_INFORMATION process{};
		const BOOL created = CreateProcessW(executable.c_str(), mutableCommand.data(),
			nullptr, nullptr, FALSE, CREATE_NO_WINDOW, nullptr,
			packagePath.parent_path().c_str(), &startup, &process);
		Require(created != FALSE,
			"could not launch the fresh-process private-runtime smoke");
		WaitForSingleObject(process.hProcess, INFINITE);
		DWORD exitCode = 1;
		GetExitCodeProcess(process.hProcess, &exitCode);
		CloseHandle(process.hThread);
		CloseHandle(process.hProcess);
		Require(exitCode == 0,
			"fresh-process cooked runtime failed with global .NET discovery disabled (exit code "
			+ std::to_string(exitCode) + ")");
#else
		(void)packagePath;
		(void)lifecycleEntityID;
		(void)triggerEntityID;
		Require(false, "private-runtime process smoke is supported only on Windows x64");
#endif
	}

	void TestCookedRuntimeOnly(const std::filesystem::path& packagePath,
		TomCat::UUID lifecycleEntityID, TomCat::UUID triggerEntityID)
	{
		const std::optional<std::string> managedVariable =
			ReadEnvironment("TOMCAT_E2E_MANAGED_DIR");
		const std::optional<std::string> dotnetVariable =
			ReadEnvironment("TOMCAT_E2E_DOTNET_ROOT");
		Require(managedVariable && !managedVariable->empty()
			&& dotnetVariable && !dotnetVariable->empty(),
			"fresh cooked-runtime mode requires packaged Managed and dotnet roots");
		const std::filesystem::path managedDirectory = *managedVariable;
		const std::filesystem::path dotnetRoot = *dotnetVariable;
		ScopedDotNetEnvironmentIsolation environmentIsolation(true);

		TomCat::DotNetHost host;
		TomCat::DotNetHost::Configuration hostConfiguration;
		hostConfiguration.RuntimeConfigPath = managedDirectory
			/ "TomCat.ScriptHost.runtimeconfig.json";
		hostConfiguration.ScriptHostAssemblyPath = managedDirectory
			/ "TomCat.ScriptHost.dll";
		hostConfiguration.DotNetRoot = dotnetRoot;
		Require(host.Initialize(hostConfiguration),
			"fresh process could not initialize the explicit private .NET runtime: "
			+ host.GetLastError());

		TomCat::AssetManager& assets = TomCat::AssetManager::Get();
		Require(assets.MountCookedPackage(packagePath),
			"fresh process could not mount the cooked package");
		try
		{
			const TomCat::ManagedPackagePayload* payload =
				assets.GetCookedManagedPayload();
			Require(payload && !payload->Assembly.empty(),
				"fresh cooked-runtime package has no managed payload");
			auto scene = TomCat::CreateRef<TomCat::Scene>();
			TomCat::SceneSerializer reader(scene);
			Require(reader.Deserialize(assets.GetCookedStartSceneHandle()),
				"fresh process could not deserialize the cooked entry scene");
			scene->SetPhysics2DSettings(assets.GetPhysics2DSettings());

			TemporaryDirectory shadow("tomcat_player_shadow_");
			const std::filesystem::path assemblyPath =
				shadow.Root / "Assembly-CSharp.dll";
			WriteBytes(assemblyPath, payload->Assembly);
			std::string runtimeError;
			auto runtime = TomCat::Scripting::CreateManagedScriptRuntime(
				managedDirectory, assemblyPath, {}, dotnetRoot, &runtimeError);
			Require(runtime != nullptr,
				"fresh process could not load the cooked assembly from the private runtime: "
				+ runtimeError);
			std::string runtimeManifest;
			Require(runtime->ReadProjectMetadata(runtimeManifest)
				&& !runtimeManifest.empty(),
				"fresh process could not read cooked script metadata");
			VerifyRuntimeBehaviour(scene, lifecycleEntityID, triggerEntityID, runtime);
			runtime.reset();
		}
		catch (...)
		{
			TomCat::Scripting::ScriptEngine::Get().SetRuntime({});
			assets.UnmountCookedPackage();
			throw;
		}
		assets.UnmountCookedPackage();
	}

	void TestCompiledScriptCookedRuntime()
	{
		TemporaryScriptProject environment;
		TomCat::ProjectConfig config;
		config.Name = "Compiled Script E2E";
		config.Template = "2D";
		config.AssetDirectory = "Assets";
		config.StartScene = "Main.tomcat";
		const TomCat::Ref<TomCat::Project> project = TomCat::Project::CreateNew(
			environment.Root / "Project.tcproj", config);
		Require(project != nullptr, "could not create the e2e temporary project");

		const std::filesystem::path scripts = project->GetAssetPath() / "Scripts";
		const std::filesystem::path lifecycleSource = scripts / "LifecycleProbe.cs";
		const std::filesystem::path faultySource = scripts / "FaultyProbe.cs";
		const std::filesystem::path triggerSource = scripts / "TriggerProbe.cs";
		const std::filesystem::path transitionSource = scripts / "EntryTransitionProbe.cs";
		const std::filesystem::path spawnerSource = scripts / "PrefabSpawnerProbe.cs";
		const std::filesystem::path bulletSource = scripts / "BulletProbe.cs";
		constexpr std::string_view sourceMarker =
			"TOMCAT_E2E_SOURCE_MUST_NOT_BE_COOKED_7043a778";
		constexpr std::string_view transitionSourceMarker =
			"TOMCAT_PLAYER_ENTRY_SOURCE_MUST_NOT_BE_COOKED_89f2c313";
		constexpr std::string_view spawnerSourceMarker =
			"TOMCAT_PLAYER_SPAWNER_SOURCE_MUST_NOT_BE_COOKED_5891134a";
		constexpr std::string_view bulletSourceMarker =
			"TOMCAT_PLAYER_PREFAB_SOURCE_MUST_NOT_BE_COOKED_b36718ec";
		WriteTextFile(lifecycleSource, R"CS(using TomCat;
namespace E2E;
[DefaultExecutionOrder(0)]
public sealed class LifecycleProbe : TomCatBehaviour
{
    [SerializeField] private int _seed = 3;
    protected override void OnCreate()
    {
        Entity.Tag = _seed == 17 ? "created" : "bad-field";
        Transform.Position = new Vector3(0.0f, 0.0f, 1.0f);
    }
    protected override void OnEnable() => Entity.Tag = Entity.Tag == "created" ? "enabled" : "bad-enable";
    protected override void OnFixedUpdate(float dt)
    {
        Entity.Tag = Entity.Tag == "enabled" ? "fixed" : "bad-fixed";
        GetComponent<Rigidbody2D>().LinearVelocity = new Vector2(0.0f, 5.0f);
    }
    protected override void OnUpdate(float dt) => Entity.Tag = Entity.Tag == "fixed" ? "updated" : "bad-update";
    protected override void OnCollisionEnter2D(Collision2D collision) => Entity.Name = "collision-received";
}
)CS" + std::string("// ") + std::string(sourceMarker) + "\n");
		WriteTextFile(faultySource, R"CS(using System;
using TomCat;
namespace E2E;
[DefaultExecutionOrder(-100)]
public sealed class FaultyProbe : TomCatBehaviour
{
    protected override void OnUpdate(float dt) => throw new InvalidOperationException("intentional e2e failure");
}
)CS");
		WriteTextFile(triggerSource, R"CS(using TomCat;
namespace E2E;
public sealed class TriggerProbe : TomCatBehaviour
{
    protected override void OnTriggerEnter2D(Trigger2D trigger) => Entity.Name = "trigger-received";
}
)CS");
		WriteTextFile(transitionSource, R"CS(using System;
using TomCat;
namespace E2E;
public sealed class EntryTransitionProbe : TomCatBehaviour
{
    public SceneAsset NextScene;
    protected override void OnCreate()
    {
        if (SceneManager.ActiveBuildIndex != 0)
            Environment.Exit(81);
        if (!NextScene.IsValid)
            Environment.Exit(82);
        if (!SceneManager.LoadScene(NextScene))
            Environment.Exit(83);
        Log.Info("TOMCAT_PLAYER_E2E_ENTRY_REQUESTED_SCENE_2");
    }
}
)CS" + std::string("// ") + std::string(transitionSourceMarker) + "\n");
		WriteTextFile(spawnerSource, R"CS(using System;
using TomCat;
namespace E2E;
[DefaultExecutionOrder(100)]
public sealed class PrefabSpawnerProbe : TomCatBehaviour
{
    public PrefabAsset BulletPrefab;
    private int _frame;
    protected override void OnCreate()
    {
        if (SceneManager.ActiveBuildIndex != 1 || !BulletPrefab.IsValid)
            Environment.Exit(84);
        Log.Info("TOMCAT_PLAYER_E2E_SCENE_2_STARTED");
    }
    protected override void OnUpdate(float deltaTime)
    {
        _frame++;
        if (_frame == 1)
        {
            if (PlayerAcceptanceState.Created != 0 ||
                !Instantiate(BulletPrefab, new Vector3(2.0f, 3.0f, 0.0f)))
                Environment.Exit(85);
            Log.Info("TOMCAT_PLAYER_E2E_PREFAB_FRAME_1_QUEUED");
        }
        else if (_frame == 2)
        {
            if (PlayerAcceptanceState.Created != 1 ||
                !Instantiate(BulletPrefab, new Vector3(4.0f, 3.0f, 0.0f)))
                Environment.Exit(86);
            Log.Info("TOMCAT_PLAYER_E2E_PREFAB_FRAME_2_QUEUED");
        }
        else if (_frame > 600)
            Environment.Exit(87);
    }
}
)CS" + std::string("// ") + std::string(spawnerSourceMarker) + "\n");
		WriteTextFile(bulletSource, R"CS(using System;
using TomCat;
namespace E2E;
public static class PlayerAcceptanceState
{
    public static int Created;
}
[DefaultExecutionOrder(-100)]
public sealed class BulletProbe : TomCatBehaviour
{
    public int ExpectedSeed = 7;
    protected override void OnCreate()
    {
        if (SceneManager.ActiveBuildIndex != 1)
            Environment.Exit(88);
        if (ExpectedSeed != 4242)
            Environment.Exit(89);
        if (!HasComponent<Rigidbody2D>() || !HasComponent<BoxCollider2D>())
            Environment.Exit(90);
		var body = GetComponent<Rigidbody2D>();
		body.LinearVelocity = new Vector2(0.0f, 2.0f);
		if (body.LinearVelocity.Y < 1.5f || body.LinearVelocity.Y > 2.5f)
			Environment.Exit(92);
    }
    protected override void OnEnable()
    {
        PlayerAcceptanceState.Created++;
        Log.Info("TOMCAT_PLAYER_E2E_PREFAB_ENABLED_" + PlayerAcceptanceState.Created);
        if (PlayerAcceptanceState.Created == 2)
        {
            Log.Info("TOMCAT_PLAYER_E2E_TWO_SCENES_TWO_PREFABS_OK");
            Environment.Exit(73);
        }
        if (PlayerAcceptanceState.Created > 2)
            Environment.Exit(91);
    }
}
)CS" + std::string("// ") + std::string(bulletSourceMarker) + "\n");

		TomCat::AssetManager& assets = TomCat::AssetManager::Get();
		Require(assets.SetProject(project),
			"AssetManager could not open the e2e temporary project");
		const TomCat::AssetHandle lifecycleHandle =
			RequireScriptHandle(assets, lifecycleSource);
		const TomCat::AssetHandle faultyHandle = RequireScriptHandle(assets, faultySource);
		const TomCat::AssetHandle triggerHandle = RequireScriptHandle(assets, triggerSource);
		const TomCat::AssetHandle transitionHandle =
			RequireScriptHandle(assets, transitionSource);
		const TomCat::AssetHandle spawnerHandle =
			RequireScriptHandle(assets, spawnerSource);
		const TomCat::AssetHandle bulletHandle =
			RequireScriptHandle(assets, bulletSource);

		const std::optional<std::string> managedDirectoryVariable =
			ReadEnvironment("TOMCAT_E2E_MANAGED_DIR");
		const std::filesystem::path requestedManagedDirectory =
			managedDirectoryVariable ? std::filesystem::path(*managedDirectoryVariable)
				: std::filesystem::path{};
		TomCat::ScriptProjectCompiler compiler;
		const bool configured = requestedManagedDirectory.empty()
			? compiler.Configure(project)
			: compiler.Configure(project,
				requestedManagedDirectory / "TomCat.Managed.dll",
				requestedManagedDirectory / "TomCat.ScriptGenerator.dll");
		Require(configured,
			"ScriptProjectCompiler could not use the packaged Managed directory");
		const TomCat::ScriptBuildResult build = compiler.CompileNow();
		Require(build.Succeeded,
			"newly-created e2e scripts did not compile:\n" + FormatDiagnostics(build));
		const std::filesystem::path managedDirectory =
			requestedManagedDirectory.empty() ? compiler.GetManagedRuntimeDirectory()
				: requestedManagedDirectory;
		Require(std::filesystem::is_regular_file(managedDirectory
			/ "TomCat.ScriptHost.dll"),
			"packaged Managed directory omitted TomCat.ScriptHost.dll");
		if (EnvironmentFlag("TOMCAT_E2E_REQUIRE_DETACHED"))
		{
			std::string generatedProject = ReadTextFile(project->GetLibraryPath()
				/ "ScriptProject" / "Assembly-CSharp.csproj");
			std::replace(generatedProject.begin(), generatedProject.end(), '\\', '/');
			const std::string packagedReference = managedDirectory.generic_string();
			Require(generatedProject.find(packagedReference) != std::string::npos,
				"detached script build did not reference the packaged Managed directory");
			for (std::filesystem::path cursor = managedDirectory; !cursor.empty();
				cursor = cursor.parent_path())
			{
				Require(!std::filesystem::is_regular_file(cursor / "Managed"
					/ "TomCat.Managed.slnx"),
					"detached smoke accidentally ran from a source checkout");
				if (cursor == cursor.root_path() || cursor.parent_path() == cursor)
					break;
			}
		}

		const std::optional<std::string> dotnetRootVariable =
			ReadEnvironment("TOMCAT_E2E_DOTNET_ROOT");
		const std::filesystem::path dotnetRoot = dotnetRootVariable
			? std::filesystem::path(*dotnetRootVariable) : std::filesystem::path{};
		const bool isolateDotNet = EnvironmentFlag("TOMCAT_E2E_ISOLATE_DOTNET");
		Require(!isolateDotNet || !dotnetRoot.empty(),
			"private runtime isolation requires TOMCAT_E2E_DOTNET_ROOT");
		ScopedDotNetEnvironmentIsolation environmentIsolation(isolateDotNet);
		if (isolateDotNet)
		{
			TomCat::DotNetHost host;
			TomCat::DotNetHost::Configuration hostConfiguration;
			hostConfiguration.RuntimeConfigPath = managedDirectory
				/ "TomCat.ScriptHost.runtimeconfig.json";
			hostConfiguration.ScriptHostAssemblyPath = managedDirectory
				/ "TomCat.ScriptHost.dll";
			hostConfiguration.DotNetRoot = dotnetRoot;
			Require(host.Initialize(hostConfiguration),
				"private .NET runtime failed while PATH and DOTNET_ROOT were isolated: "
				+ host.GetLastError());
			std::error_code equivalentError;
			Require(std::filesystem::equivalent(host.GetDotNetRoot(), dotnetRoot,
				equivalentError) && !equivalentError,
				"CoreCLR did not initialize from the explicitly selected private root");
		}

		std::string runtimeError;
		auto runtime = TomCat::Scripting::CreateManagedScriptRuntime(
			managedDirectory, build.AssemblyPath, build.PdbPath, dotnetRoot,
			&runtimeError);
		Require(runtime != nullptr,
			"compiled project assembly could not enter CoreCLR: " + runtimeError);
		std::string manifest;
		Require(runtime->ReadProjectMetadata(manifest) && !manifest.empty(),
			"compiled project assembly did not expose generated metadata");
		TomCat::ScriptMetadataCache metadataCache;
		std::string metadataError;
		Require(metadataCache.ParseAndReplace(manifest, metadataError),
			"generated script metadata was invalid: " + metadataError);

		auto sourceScene = TomCat::CreateRef<TomCat::Scene>();
		sourceScene->SetSceneName("Compiled C# E2E");
		TomCat::Entity lifecycle = sourceScene->CreateEntity("Lifecycle target");
		const TomCat::UUID lifecycleEntityID = lifecycle.GetUUID();
		TomCat::CSharpScriptEntry lifecycleEntry =
			MakeScriptEntry(metadataCache, lifecycleHandle);
		SetInt32Field(lifecycleEntry, "_seed", 17);
		lifecycle.AddComponent<TomCat::CSharpScripts>().Scripts.push_back(
			std::move(lifecycleEntry));
		lifecycle.AddComponent<TomCat::Rigidbody2D>().Type =
			TomCat::Rigidbody2D::BodyType::Dynamic;
		lifecycle.AddComponent<TomCat::BoxCollider2D>();

		TomCat::Entity wall = sourceScene->CreateEntity("Collision wall");
		SetPosition(wall, { 0.75f, 0.0f, 0.0f });
		wall.AddComponent<TomCat::Rigidbody2D>().Type =
			TomCat::Rigidbody2D::BodyType::Static;
		wall.AddComponent<TomCat::BoxCollider2D>();

		TomCat::Entity faulty = sourceScene->CreateEntity("Faulty script");
		SetPosition(faulty, { 20.0f, 20.0f, 0.0f });
		faulty.AddComponent<TomCat::CSharpScripts>().Scripts.push_back(
			MakeScriptEntry(metadataCache, faultyHandle));

		TomCat::Entity trigger = sourceScene->CreateEntity("Trigger receiver");
		const TomCat::UUID triggerEntityID = trigger.GetUUID();
		SetPosition(trigger, { 4.0f, 0.0f, 0.0f });
		trigger.AddComponent<TomCat::CSharpScripts>().Scripts.push_back(
			MakeScriptEntry(metadataCache, triggerHandle));
		trigger.AddComponent<TomCat::Rigidbody2D>().Type =
			TomCat::Rigidbody2D::BodyType::Static;
		trigger.AddComponent<TomCat::BoxCollider2D>().IsTrigger = true;

		TomCat::Entity triggerBody = sourceScene->CreateEntity("Trigger body");
		SetPosition(triggerBody, { 4.0f, 0.0f, 0.0f });
		triggerBody.AddComponent<TomCat::Rigidbody2D>().Type =
			TomCat::Rigidbody2D::BodyType::Dynamic;
		triggerBody.AddComponent<TomCat::CircleCollider2D>();

		const std::filesystem::path scenePath = project->GetAssetPath() / "Main.tomcat";
		TomCat::SceneSerializer sceneWriter(sourceScene);
		Require(sceneWriter.Serialize(scenePath),
			"could not save the e2e scene with reconciled script fields");
		const TomCat::AssetMetadata* sceneMetadata =
			assets.Registry().GetMetadata(scenePath);
		Require(sceneMetadata && sceneMetadata->Type == TomCat::AssetType::Scene,
			"saved e2e scene was not imported as a Scene asset");
		const TomCat::AssetHandle sceneHandle = sceneMetadata->Handle;
		TomCat::BuildSettings lifecycleBuild;
		lifecycleBuild.EntrySceneHandle = sceneHandle;
		lifecycleBuild.Scenes.push_back(
			{ sceneHandle, true, std::filesystem::path("Main.tomcat") });
		Require(project->SetBuildSettings(lifecycleBuild),
			"could not persist the lifecycle package BuildSettings");

		auto reloadedScene = TomCat::CreateRef<TomCat::Scene>();
		TomCat::SceneSerializer sceneReader(reloadedScene);
		Require(sceneReader.Deserialize(scenePath),
			"Editor-restart scene reload failed");
		TomCat::Entity reloadedLifecycle =
			reloadedScene->FindEntityByUUID(lifecycleEntityID);
		Require(reloadedLifecycle
			&& reloadedLifecycle.HasComponent<TomCat::CSharpScripts>(),
			"Editor-restart scene reload lost its C# attachment");
		const auto& restoredFields = reloadedLifecycle
			.GetComponent<TomCat::CSharpScripts>().Scripts.front().Fields;
		Require(std::any_of(restoredFields.begin(), restoredFields.end(),
			[](const TomCat::ScriptField& field)
			{
				return field.Name == "_seed"
					&& std::holds_alternative<int32_t>(field.Value)
					&& std::get<int32_t>(field.Value) == 17;
			}), "serialized C# field did not survive an Editor-style scene reload");
		VerifyRuntimeBehaviour(reloadedScene, lifecycleEntityID, triggerEntityID,
			runtime);
		runtime.reset();

		const std::vector<uint8_t> assembly = ReadBytes(build.AssemblyPath);
		Require(assets.SetManagedCookPayload(assembly, manifest, build.BuildID),
			"cook rejected the real compiled managed payload");
		const std::filesystem::path packagePath =
			environment.Root / "Build" / "Game.tcpak";
		Require(assets.CookToPackage(packagePath),
			"real compiled scripts and their scene did not cook");

		// Build a second package that exercises the complete standalone Player path:
		// entry Scene -> frame-end transition -> two cross-frame Prefab requests ->
		// dynamic C# lifecycle and live Rigidbody2D access. The Prefab is reached only
		// through a strongly typed serialized script field.
		auto prefabSourceScene = TomCat::CreateRef<TomCat::Scene>();
		prefabSourceScene->SetSceneName("Player acceptance Prefab source");
		TomCat::Entity bulletRoot = prefabSourceScene->CreateEntity("Runtime bullet");
		TomCat::CSharpScriptEntry bulletEntry =
			MakeScriptEntry(metadataCache, bulletHandle);
		SetInt32Field(bulletEntry, "ExpectedSeed", 4242);
		bulletRoot.AddComponent<TomCat::CSharpScripts>().Scripts.push_back(
			std::move(bulletEntry));
		bulletRoot.AddComponent<TomCat::Rigidbody2D>().Type =
			TomCat::Rigidbody2D::BodyType::Dynamic;
		bulletRoot.AddComponent<TomCat::BoxCollider2D>();
		const std::filesystem::path prefabPath =
			project->GetAssetPath() / "Prefabs" / "RuntimeBullet.tcprefab";
		std::error_code prefabDirectoryError;
		std::filesystem::create_directories(prefabPath.parent_path(),
			prefabDirectoryError);
		Require(!prefabDirectoryError,
			"could not create the Player acceptance Prefab directory: "
			+ prefabDirectoryError.message());
		TomCat::AssetHandle prefabHandle;
		Require(TomCat::PrefabArchiveCodec::SaveSubtree(prefabSourceScene,
			bulletRoot, prefabPath, &prefabHandle)
			&& static_cast<uint64_t>(prefabHandle) != 0,
			"could not save the C#/physics Player acceptance Prefab");

		auto gameplayScene = TomCat::CreateRef<TomCat::Scene>();
		gameplayScene->SetSceneName("Player acceptance gameplay");
		TomCat::Entity spawner = gameplayScene->CreateEntity("Prefab spawner");
		TomCat::CSharpScriptEntry spawnerEntry =
			MakeScriptEntry(metadataCache, spawnerHandle);
		SetAssetField(spawnerEntry, "BulletPrefab", prefabHandle,
			"TomCat.PrefabAsset");
		spawner.AddComponent<TomCat::CSharpScripts>().Scripts.push_back(
			std::move(spawnerEntry));
		const std::filesystem::path gameplayScenePath =
			project->GetAssetPath() / "PlayerGameplay.tomcat";
		TomCat::SceneSerializer gameplayWriter(gameplayScene);
		Require(gameplayWriter.Serialize(gameplayScenePath),
			"could not save the Player acceptance gameplay Scene");
		const TomCat::AssetMetadata* gameplayMetadata =
			assets.Registry().GetMetadata(gameplayScenePath);
		Require(gameplayMetadata
			&& gameplayMetadata->Type == TomCat::AssetType::Scene,
			"Player acceptance gameplay Scene was not imported");
		const TomCat::AssetHandle gameplayHandle = gameplayMetadata->Handle;

		auto entryScene = TomCat::CreateRef<TomCat::Scene>();
		entryScene->SetSceneName("Player acceptance entry");
		TomCat::Entity transition = entryScene->CreateEntity("Scene portal");
		TomCat::CSharpScriptEntry transitionEntry =
			MakeScriptEntry(metadataCache, transitionHandle);
		SetAssetField(transitionEntry, "NextScene", gameplayHandle,
			"TomCat.SceneAsset");
		transition.AddComponent<TomCat::CSharpScripts>().Scripts.push_back(
			std::move(transitionEntry));
		const std::filesystem::path entryScenePath =
			project->GetAssetPath() / "PlayerEntry.tomcat";
		TomCat::SceneSerializer entryWriter(entryScene);
		Require(entryWriter.Serialize(entryScenePath),
			"could not save the Player acceptance entry Scene");
		const TomCat::AssetMetadata* entryMetadata =
			assets.Registry().GetMetadata(entryScenePath);
		Require(entryMetadata && entryMetadata->Type == TomCat::AssetType::Scene,
			"Player acceptance entry Scene was not imported");
		const TomCat::AssetHandle entryHandle = entryMetadata->Handle;

		TomCat::BuildSettings playerBuild;
		playerBuild.EntrySceneHandle = entryHandle;
		playerBuild.Scenes = {
			{ entryHandle, true, std::filesystem::path("PlayerEntry.tomcat") },
			{ gameplayHandle, true, std::filesystem::path("PlayerGameplay.tomcat") }
		};
		Require(project->SetBuildSettings(playerBuild),
			"could not persist the ordered two-Scene Player BuildSettings");
		const std::filesystem::path playerPackagePath =
			environment.Root / "Build" / "PlayerAcceptance.tcpak";
		Require(assets.CookToPackage(playerPackagePath),
			"two Scenes and the referenced C#/physics Prefab did not cook");

		std::filesystem::path builtPlayerExecutable;
		const std::optional<std::string> templateVariable =
			ReadEnvironment("TOMCAT_E2E_PLAYER_TEMPLATE");
		if (templateVariable && !templateVariable->empty())
		{
			TomCat::PlayerBuildRequest playerRequest;
			playerRequest.ProjectInstance = project;
			playerRequest.EntryScene = entryHandle;
			playerRequest.TemplateDirectory = *templateVariable;
			playerRequest.ManagedAssembly = assembly;
			playerRequest.ScriptManifestJson = manifest;
			playerRequest.ScriptBuildID = build.BuildID;
			playerRequest.ValidateBeforePublish = [&compiler, &build]()
			{
				return compiler.RefreshSourceState()
					&& compiler.IsCurrentSourceBuilt()
					&& compiler.GetCurrentSourceHash() == build.SourceHash
					&& compiler.GetLastGoodBuildID() == build.BuildID
					&& compiler.GetLastGoodAssemblyPath().lexically_normal()
						== build.AssemblyPath.lexically_normal();
			};
			TomCat::PlayerBuildRequest tamperedRequest = playerRequest;
			const TomCat::PlayerBuildResult playerResult =
				TomCat::PlayerBuilder::Build(std::move(playerRequest));
			Require(playerResult.Succeeded,
				"PlayerBuilder rejected the real hashed template or export inputs: "
				+ playerResult.Message);
			Require(std::filesystem::is_regular_file(
				playerResult.OutputDirectory / "Game.tcpak"),
				"PlayerBuilder output omitted Game.tcpak");
			builtPlayerExecutable = playerResult.PlayerExecutable;
			const auto publishedPackage = ReadBytes(
				playerResult.OutputDirectory / "Game.tcpak");
			const auto publishedExecutable = ReadBytes(builtPlayerExecutable);
			tamperedRequest.TemplateDirectory = environment.Root / "TamperedTemplate";
			std::error_code templateCopyError;
			std::filesystem::copy(std::filesystem::path(*templateVariable),
				tamperedRequest.TemplateDirectory,
				std::filesystem::copy_options::recursive, templateCopyError);
			Require(!templateCopyError,
				"could not create a disposable template for hash validation: "
				+ templateCopyError.message());
			WriteTextFile(tamperedRequest.TemplateDirectory /
				"Packages" / "Shaders" / "Texture.glsl", "tampered template file\n");
			const auto rejectedBuild = TomCat::PlayerBuilder::Build(
				std::move(tamperedRequest));
			Require(!rejectedBuild.Succeeded,
				"PlayerBuilder accepted a template whose file no longer matches its SHA-256");
			Require(ReadBytes(playerResult.OutputDirectory / "Game.tcpak")
					== publishedPackage
				&& ReadBytes(builtPlayerExecutable) == publishedExecutable,
				"rejected template build changed the last successful Player output");
			std::cout << "PASS tampered Player template rejected; previous export preserved\n";
		}
		else
		{
			Require(!EnvironmentFlag("TOMCAT_E2E_REQUIRE_PLAYER"),
				"real Player export was required, but TOMCAT_E2E_PLAYER_TEMPLATE was not set");
			std::cout << "SKIP real PlayerBuilder export (Player template was not supplied)\n";
		}
		assets.Shutdown();
		ValidateWithIndependentPlayer(packagePath);
		RunFreshPrivateRuntimeProcess(packagePath, lifecycleEntityID,
			triggerEntityID);
		const std::vector<uint8_t> packageBytes = ReadBytes(packagePath);
		const std::string packageText(packageBytes.begin(), packageBytes.end());
		Require(packageText.find(sourceMarker) == std::string::npos,
			"cooked Player package leaked C# source text");

		Require(assets.MountCookedPackage(packagePath),
			"cooked e2e package could not be mounted without its source project");
		Require(assets.GetCookedStartSceneHandle() == sceneHandle,
			"cooked e2e package lost the start-scene identity");
		std::vector<uint8_t> forbiddenSource;
		Require(!assets.ReadAssetBytes(lifecycleHandle, forbiddenSource),
			"cooked Player exposed a C# source asset entry");
		const TomCat::ManagedPackagePayload* cookedPayload =
			assets.GetCookedManagedPayload();
		Require(cookedPayload && cookedPayload->Assembly == assembly,
			"cooked e2e package changed its compiled assembly");
		Require(manifest.find("\"defaultValue\"") != std::string::npos,
			"Editor metadata did not include constructor-derived field defaults");
		Require(cookedPayload->ScriptManifestJson.find("\"defaultValue\"")
			== std::string::npos,
			"cooked package retained Editor-only constructor defaults instead of the canonical embedded manifest");
		assets.UnmountCookedPackage();

		ValidateWithIndependentPlayer(playerPackagePath);
		RunWithIndependentPlayer(playerPackagePath, 73);
		if (!builtPlayerExecutable.empty())
		{
			const std::filesystem::path foreignWorkingDirectory =
				environment.Root / "NoArgsCaller";
			std::error_code directoryError;
			std::filesystem::create_directory(foreignWorkingDirectory,
				directoryError);
			Require(!directoryError,
				"could not create the foreign Player working directory: "
				+ directoryError.message());
			RunBuiltPlayerWithoutArguments(builtPlayerExecutable,
				foreignWorkingDirectory, 73);
		}
		const std::vector<uint8_t> playerPackageBytes = ReadBytes(playerPackagePath);
		const std::string playerPackageText(playerPackageBytes.begin(),
			playerPackageBytes.end());
		for (const std::string_view marker : { sourceMarker, transitionSourceMarker,
			spawnerSourceMarker, bulletSourceMarker })
		{
			Require(playerPackageText.find(marker) == std::string::npos,
				"standalone Player package leaked C# source marker");
		}
		// Generated Assembly-CSharp code legitimately carries the generator's name
		// in compiler metadata; only the Editor-only binary itself is forbidden.
		Require(playerPackageText.find("TomCat.ScriptGenerator.dll") == std::string::npos,
			"standalone Player package retained TomCat.ScriptGenerator.dll");

		Require(assets.MountCookedPackage(playerPackagePath),
			"standalone Player acceptance package could not be remounted");
		const std::vector<TomCat::AssetHandle>& buildScenes =
			assets.GetCookedBuildSceneHandles();
		Require(assets.GetCookedStartSceneHandle() == entryHandle
			&& buildScenes.size() == 2 && buildScenes[0] == entryHandle
			&& buildScenes[1] == gameplayHandle,
			"tcpak v5 did not preserve the ordered two-Scene BuildSettings manifest");
		TomCat::AssetType cookedType = TomCat::AssetType::None;
		std::vector<uint8_t> cookedAsset;
		Require(assets.ReadAssetBytes(prefabHandle, cookedAsset, &cookedType)
			&& cookedType == TomCat::AssetType::Prefab && !cookedAsset.empty(),
			"Prefab dependency closure omitted the referenced .tcprefab");
		Require(!assets.ReadAssetBytes(sceneHandle, cookedAsset),
			"strict build-scene closure retained the unrelated lifecycle Scene");
		for (TomCat::AssetHandle sourceHandle : { lifecycleHandle, faultyHandle,
			triggerHandle, transitionHandle, spawnerHandle, bulletHandle })
		{
			Require(!assets.ReadAssetBytes(sourceHandle, cookedAsset),
				"standalone Player package exposed a C# source asset entry");
		}
		const TomCat::ManagedPackagePayload* playerPayload =
			assets.GetCookedManagedPayload();
		Require(playerPayload && playerPayload->Assembly == assembly,
			"standalone Player package omitted or changed Assembly-CSharp.dll");
		assets.UnmountCookedPackage();
	}

}

int main(int argc, char** argv)
{
	TomCat::Log::Init();
	try
	{
		const bool e2eOnly = argc == 2 && argv[1]
			&& std::string_view(argv[1]) == "--e2e-only";
		const bool cookedRuntimeOnly = argc == 5 && argv[1]
			&& std::string_view(argv[1]) == "--cooked-runtime-only";
		Require(argc == 1 || e2eOnly || cookedRuntimeOnly,
			"usage: ScriptCompilerRegression.exe [--e2e-only | --cooked-runtime-only <package> <lifecycle-id> <trigger-id>]");
		if (cookedRuntimeOnly)
		{
			TestCookedRuntimeOnly(std::filesystem::path(argv[2]),
				TomCat::UUID(ParseUInt64(argv[3], "lifecycle entity ID")),
				TomCat::UUID(ParseUInt64(argv[4], "trigger entity ID")));
			std::cout << "PASS cooked scene started from an explicit private .NET root in a fresh process\n";
			return 0;
		}
		if (e2eOnly)
		{
			TestCompiledScriptCookedRuntime();
			std::cout << "PASS C# -> scene reload/physics -> tcpak v5 -> private runtime -> real Player scene/Prefab chain\n";
			return 0;
		}
#ifdef TC_PLATFORM_WINDOWS
		TestMissingDotNet10SdkIsActionable();
		std::cout << "PASS missing .NET 10 SDK has an actionable diagnostic\n";
#endif
		TestMSBuildDependencyInjectionIsBlocked();
		std::cout << "PASS MSBuild/NuGet/local reference injection is blocked\n";
		TestAssetRefMarkerValidation();
		std::cout << "PASS AssetRef<T> accepts only built-in asset markers\n";
		TestFailedBuildPreservesLastGood();
		std::cout << "PASS failed C# build preserves last-good\n";
		return 0;
	}
	catch (const std::exception& exception)
	{
		std::cerr << "FAIL ScriptCompilerRegression: "
			<< exception.what() << '\n';
		return 1;
	}
}
