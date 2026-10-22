#include "Scripting/ScriptProjectCompiler.h"
#include "Scripting/ScriptMetadataCache.h"
#include "TomCat/Asset/AssetManager.h"
#include "TomCat/Asset/AssetJobSystem.h"
#include "TomCat/Core/Log.h"
#include "TomCat/Project/Project.h"
#include "TomCat/Scene/Scene.h"
#include "TomCat/Scene/Entity.h"
#include "TomCat/Scene/Components.h"
#include "TomCat/Scene/SceneSerializer.h"
#include "TomCat/Scripting/ManagedRuntimeFactory.h"
#include "TomCat/Utils/PathUtils.h"
#include "platform/Linux/LinuxProcess.h"
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <unistd.h>

namespace {
void Require(bool value, const std::string& message) { if (!value) throw std::runtime_error(message); }
void Write(const std::filesystem::path& path, std::string_view text) {
    std::filesystem::create_directories(path.parent_path());
    std::ofstream file(path, std::ios::binary); file << text;
    Require(bool(file), "write failed: " + path.string());
}
std::string Read(const std::filesystem::path& path) {
    std::ifstream file(path, std::ios::binary);
    return {std::istreambuf_iterator<char>(file), {}};
}
TomCat::Linux::ProcessResult Run(const std::vector<std::string>& command, const std::filesystem::path& cwd) {
    auto result = TomCat::Linux::RunProcess(command, cwd, std::chrono::minutes(4));
    std::cout << result.Output;
    Require(result.Error.empty(), result.Error);
    return result;
}
}
int main(int argc, char** argv) {
    TomCat::Log::Init();
    const auto root = std::filesystem::temp_directory_path() / ("TomCat-linux-" + std::to_string(getpid()));
    try {
        Require(argc == 4, "expected CLI, template and artifact output paths");
        Require(!std::filesystem::exists(root), "test directory already exists");
        const auto projectPath = root / TomCat::UTF8ToPath("项目 with spaces") / "Project.tcproj";
        TomCat::ProjectConfig config; config.Name = "Linux Acceptance"; config.Template = "2D";
        auto project = TomCat::Project::CreateNew(projectPath, config);
        Require(project != nullptr, "project creation failed");
        const auto scriptPath = project->GetAssetPath() / "Scripts" / "LinuxProbe.cs";
        const std::string script = R"CS(using TomCat;
using System;
using System.IO;
namespace Acceptance;
public sealed class LinuxProbe : MonoBehaviour
{
    private int _updates;
    private void Awake() { Entity.Tag = "linux-coreclr-awake"; }
    private void Update()
    {
        if (++_updates < 3) return;
        if (Entity.Tag != "linux-coreclr-awake") { Application.Quit(31); return; }
        var marker = Environment.GetEnvironmentVariable("TOMCAT_LINUX_MARKER");
        if (marker != null) File.WriteAllText(marker, "C# Awake + Update + native Entity API passed");
        Application.Quit(0);
    }
}
)CS";
        Write(scriptPath, script);
        // Ambient MSBuild imports must remain disabled on Linux as on Windows.
        Write(projectPath.parent_path() / "Directory.Build.targets",
            "<Project><Target Name=\"Poison\" BeforeTargets=\"CoreCompile\"><Error Text=\"Unexpected ambient import\" /></Target></Project>");
        auto& assets = TomCat::AssetManager::Get();
        Require(assets.SetProject(project) && assets.Refresh(), "asset discovery failed");
        TomCat::ScriptProjectCompiler compiler;
        compiler.SetDiagnosticCallback([](const auto& d) { std::cout << d.Code << ": " << d.Message << '\n'; });
        Require(compiler.Configure(project), "compiler configuration failed");
        const auto build = compiler.CompileNow();
        Require(build.Succeeded, "C# compile/host validation failed");
        std::string error, manifest;
        auto runtime = TomCat::Scripting::CreateManagedScriptRuntime(compiler.GetManagedRuntimeDirectory(),
            build.AssemblyPath, build.PdbPath, {}, &error);
        Require(runtime && runtime->IsReady() && runtime->ReadProjectMetadata(manifest), "CoreCLR metadata: " + error);
        TomCat::ScriptMetadataCache metadata;
        Require(metadata.ParseAndReplace(manifest, error), error);
        const auto* scriptAsset = assets.Registry().GetMetadata(scriptPath);
        Require(scriptAsset != nullptr, "script asset missing");
        const auto scriptMetadata = metadata.Find(scriptAsset->Handle);
        Require(scriptMetadata.has_value(), "compiled manifest omitted LinuxProbe");
        auto scene = TomCat::CreateRef<TomCat::Scene>();
        auto entity = scene->CreateEntity("Linux probe");
        TomCat::CSharpScriptEntry entry;
        entry.ScriptAsset = scriptAsset->Handle;
        entry.LastKnownClassName = scriptMetadata->TypeName;
        TomCat::ReconcileScriptEntryFields(entry, *scriptMetadata);
        entity.AddComponent<TomCat::CSharpScripts>().Scripts.push_back(std::move(entry));
        const auto scenePath = project->GetAssetPath() / "Main.tomcat";
        Require(TomCat::SceneSerializer(scene).Serialize(scenePath), "scene save failed");
        const auto* sceneAsset = assets.Registry().GetMetadata(scenePath);
        Require(sceneAsset != nullptr, "scene import failed");
        TomCat::BuildSettings settings;
        settings.EntrySceneHandle = sceneAsset->Handle;
        settings.Scenes = {{sceneAsset->Handle, true, "Main.tomcat"}};
        Require(project->SetBuildSettings(settings), "build settings failed");
        runtime.reset();
        Write(scriptPath, script + "\ninvalid C# syntax\n");
        Require(!compiler.CompileNow().Succeeded && compiler.GetLastGoodBuildID() == build.BuildID,
            "failed compile did not preserve last good build");
        Write(scriptPath, script);
        assets.Shutdown();
        TomCat::AssetJobSystem::Get().Shutdown();
        const std::vector<std::string> command{argv[1], "build", "--project", projectPath.string(), "--template", argv[2]};
        Require(Run(command, root).ExitCode == 0, "CLI export failed");
        const auto exported = projectPath.parent_path() / "Build" / "Linux Acceptance";
        const auto player = exported / "Linux Acceptance";
        Require(access(player.c_str(), X_OK) == 0 && std::filesystem::file_size(exported / "Game.tcpak") > 0,
            "export omitted executable permissions or package");
        const auto originalSettings = project->GetPlayerSettings();
        for (const auto& [product, filename] : std::vector<std::pair<std::string, std::string>>{
            {"tomcatplayer", "tomcatplayer"}, {"Game.tcpak", "_Game.tcpak"}}) {
            auto renamed = originalSettings;
            renamed.ProductName = product;
            Require(project->SetPlayerSettings(renamed), "could not set export product name");
            Require(Run(command, root).ExitCode == 0, "product-name export failed");
            const auto binary = projectPath.parent_path() / "Build" / filename / filename;
            Require(access(binary.c_str(), X_OK) == 0 &&
                Run({binary.string(), "--validate-package", "Game.tcpak"}, root).ExitCode == 0,
                "case-sensitive or reserved product name broke export");
        }
        Require(project->SetPlayerSettings(originalSettings), "could not restore product settings");
        // The published game must survive a missing SDK/source tree and unrelated cwd.
        std::filesystem::rename(project->GetAssetPath(), projectPath.parent_path() / "SourcesUnavailable");
        setenv("DOTNET_ROOT", "/no-system-dotnet", 1);
        setenv("DOTNET_ROOT_X64", "/no-system-dotnet", 1);
        setenv("PATH", "/no-sdk", 1);
        setenv("XDG_DATA_HOME", (root / "UserData").c_str(), 1);
        Require(Run({player.string(), "--validate-package", "Game.tcpak"}, root).ExitCode == 0,
            "private runtime package validation failed");
        const auto marker = root / "managed-executed.txt";
        setenv("TOMCAT_LINUX_MARKER", marker.c_str(), 1);
        setenv("TOMCAT_E2E_PLAYER_HEADLESS", "1", 1);
        Require(Run({player.string()}, root).ExitCode == 0 && Read(marker).find("native Entity API passed") != std::string::npos,
            "exported C# lifecycle did not execute");
        // Repeat with an actual GL window in Xvfb; this also exercises packaged shaders.
        unsetenv("TOMCAT_E2E_PLAYER_HEADLESS");
        std::filesystem::remove(marker);
        Require(Run({player.string()}, root).ExitCode == 0 && std::filesystem::exists(marker),
            "windowed exported Player failed");
        std::filesystem::rename(exported / "dotnet", exported / "dotnet-hidden");
        Require(Run({player.string(), "--validate-package", "Game.tcpak"}, root).ExitCode != 0,
            "Player accepted a missing private runtime");
        std::filesystem::rename(exported / "dotnet-hidden", exported / "dotnet");
        const auto artifact = std::filesystem::absolute(argv[3]);
        Require(!std::filesystem::exists(artifact), "artifact directory already exists");
        std::filesystem::create_directories(artifact.parent_path());
        std::filesystem::copy(exported, artifact, std::filesystem::copy_options::recursive);
        std::filesystem::remove_all(root);
        std::cout << "PASS Linux C# compile, failure recovery, CLI export, private CoreCLR, lifecycle and GL Player\n";
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "FAIL LinuxRuntimeRegression: " << e.what() << " (files: " << root << ")\n";
        return 1;
    }
}
