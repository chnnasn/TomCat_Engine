#include "TomCat/Asset/AssetManager.h"
#include "TomCat/Core/Log.h"
#include "TomCat/Core/UUID.h"
#include "TomCat/Project/Project.h"

#include "Scripting/ScriptProjectCompiler.h"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

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

}

int main()
{
	TomCat::Log::Init();
	try
	{
		TestFailedBuildPreservesLastGood();
		std::cout << "PASS failed C# build preserves last-good\n";
		return 0;
	}
	catch (const std::exception& exception)
	{
		std::cerr << "FAIL failed C# build preserves last-good: "
			<< exception.what() << '\n';
		return 1;
	}
}
