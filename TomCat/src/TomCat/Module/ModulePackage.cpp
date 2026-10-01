#include "tcpch.h"
#include "ModulePackage.h"
#include "ModuleSystem.h"
#include "TomCat/Asset/ContentHash.h"
#include "TomCat/Core/Version.h"
#include "TomCat/Utils/FileSystemUtils.h"
#include "TomCat/Utils/PathUtils.h"
#include <yaml-cpp/yaml.h>
#include <set>

namespace TomCat::ModulePackage {
namespace {
    struct File { std::filesystem::path Path; std::vector<uint8_t> Bytes; };
    struct Module { ModuleManifest Manifest; std::string Text; std::vector<File> Files; };
    std::string Key(const std::filesystem::path& path) {
        std::string key = PathToUTF8(path.lexically_normal());
        std::transform(key.begin(), key.end(), key.begin(), [](unsigned char c) {
            return static_cast<char>(std::tolower(c)); });
        return key;
    }
    bool Read(const std::filesystem::path& path, std::vector<uint8_t>& bytes,
        std::string& error) {
        FileSystem::PinnedDirectoryChain guard;
        if (!guard.Acquire(path.parent_path(), error)) return false;
        std::error_code code;
        const auto status = std::filesystem::symlink_status(path, code);
        if (code || !std::filesystem::is_regular_file(status)) {
            error = "module input is not a regular file: " + PathToUTF8(path); return false;
        }
        std::ifstream input(path, std::ios::binary | std::ios::ate);
        const auto length = input.tellg();
        if (!input || length <= 0 || length > 64 * 1024 * 1024) {
            error = "module input is missing, empty or exceeds 64 MiB"; return false;
        }
        bytes.resize(static_cast<size_t>(length)); input.seekg(0);
        if (!input.read(reinterpret_cast<char*>(bytes.data()), length)) {
            error = "module input read failed"; return false;
        }
        return guard.Verify(error);
    }
    bool Parse(std::span<const uint8_t> bytes, std::vector<Module>& modules,
        std::string& error) {
        modules.clear();
        if (bytes.empty() || bytes.size() > MaximumBytes) {
            error = "invalid native module envelope size"; return false;
        }
        try {
            auto root = YAML::Load(std::string(reinterpret_cast<const char*>(bytes.data()), bytes.size()));
            if (!root.IsMap() || root.size() != 3 || root["ModulePackageVersion"].as<uint32_t>() != 1
                || root["EngineBuildID"].as<std::string>() != Version::EngineBuildID
                || !root["Modules"].IsSequence() || root["Modules"].size() == 0
                || root["Modules"].size() > 64) {
                error = "native module package version/build/shape mismatch"; return false;
            }
            std::set<std::string> names;
            size_t total = 0;
            for (const auto& record : root["Modules"]) {
                Module module;
                if (!record.IsMap() || record.size() != 2 || !record["Files"].IsSequence()) {
                    error = "invalid module record"; return false;
                }
                module.Text = record["Manifest"].as<std::string>();
                if (module.Text.size() > 65536 || !ModuleSystem::ParseManifestText(module.Text, module.Manifest, error)) return false;
                if (!module.Manifest.Enabled || !module.Manifest.Runtime
                    || module.Manifest.EngineBuildID != Version::EngineBuildID
                    || !names.insert(Key(module.Manifest.Name)).second) {
                    error = "invalid or duplicate runtime module identity"; return false;
                }
                std::set<std::string> expected{Key(module.Manifest.Library)}, actual;
                for (const auto& file : module.Manifest.RuntimeFiles) expected.insert(Key(file));
                if (record["Files"].size() != expected.size()) { error = "module file set mismatch"; return false; }
                for (const auto& entry : record["Files"]) {
                    if (!entry.IsMap() || entry.size() != 2) { error = "invalid module file record"; return false; }
                    File file; file.Path = UTF8ToPath(entry["Path"].as<std::string>());
                    if (!expected.contains(Key(file.Path)) || file.Path != file.Path.lexically_normal()
                        || !actual.insert(Key(file.Path)).second) { error = "unexpected module file path"; return false; }
                    const auto binary = entry["Bytes"].as<YAML::Binary>();
                    if (binary.size() == 0 || binary.size() > 64u * 1024u * 1024u
                        || binary.size() > MaximumBytes - total) { error = "module file size exceeded"; return false; }
                    total += binary.size(); file.Bytes.assign(binary.data(), binary.data() + binary.size());
                    module.Files.push_back(std::move(file));
                }
                modules.push_back(std::move(module));
            }
            // Validate the complete dependency graph before writing or loading any DLL.
            std::vector<uint8_t> state(modules.size());
            std::function<bool(size_t)> visit = [&](size_t index) {
                if (state[index] == 2) return true;
                if (state[index] == 1) { error = "cyclic runtime module dependency"; return false; }
                state[index] = 1;
                for (const auto& name : modules[index].Manifest.Dependencies) {
                    const auto found = std::find_if(modules.begin(), modules.end(), [&](const Module& m) { return m.Manifest.Name == name; });
                    if (found == modules.end()) { error = "runtime dependency missing: " + name; return false; }
                    if (!visit(static_cast<size_t>(found - modules.begin()))) return false;
                }
                state[index] = 2; return true;
            };
            for (size_t index = 0; index < modules.size(); ++index) if (!visit(index)) return false;
            return true;
        } catch (const std::exception& exception) { error = exception.what(); return false; }
    }
}
bool Build(const std::filesystem::path& project, std::vector<uint8_t>& bytes, std::string& error) {
    try {
    bytes.clear();
    std::vector<std::pair<std::filesystem::path, ModuleManifest>> discovered;
    if (!ModuleSystem::DiscoverModules(project, discovered, error)) return false;
    YAML::Node records(YAML::NodeType::Sequence);
    size_t total = 0;
    for (const auto& [manifestPath, manifest] : discovered) {
        if (!manifest.Enabled || !manifest.Runtime) continue;
        if (!ModuleSystem::Get().HasModule(manifest.Name)) { error = "runtime module was not loaded: " + manifest.Name; return false; }
        std::vector<uint8_t> manifestBytes;
        if (!Read(manifestPath, manifestBytes, error)) return false;
        YAML::Node document = YAML::Load(std::string(manifestBytes.begin(), manifestBytes.end()));
        // Pin unversioned authoring manifests to this exact packaged engine.
        if (!manifest.EngineBuildID.empty() && manifest.EngineBuildID != Version::EngineBuildID) { error = "module EngineBuildID mismatch"; return false; }
        document["EngineBuildID"] = std::string(Version::EngineBuildID);
        YAML::Node record; record["Manifest"] = YAML::Dump(document);
        YAML::Node files(YAML::NodeType::Sequence);
        auto paths = manifest.RuntimeFiles; paths.insert(paths.begin(), manifest.Library);
        for (const auto& relative : paths) {
            std::vector<uint8_t> data;
            if (!Read(manifestPath.parent_path() / relative, data, error)) return false;
            if (data.size() > MaximumBytes - total) { error = "module payload exceeds budget"; return false; }
            total += data.size();
            YAML::Node file; file["Path"] = PathToUTF8(relative.lexically_normal());
            file["Bytes"] = YAML::Binary(data.data(), data.size()); files.push_back(file);
        }
        record["Files"] = files; records.push_back(record);
    }
    if (records.size() == 0) return true;
    YAML::Node root; root["ModulePackageVersion"] = 1;
    root["EngineBuildID"] = std::string(Version::EngineBuildID); root["Modules"] = records;
    const std::string text = YAML::Dump(root); bytes.assign(text.begin(), text.end());
    return Validate(bytes, error);
    } catch (const std::exception& exception) { bytes.clear(); error = exception.what(); return false; }
}
bool Validate(std::span<const uint8_t> bytes, std::string& error) {
    std::vector<Module> modules; return Parse(bytes, modules, error);
}
bool Extract(std::span<const uint8_t> bytes, const std::filesystem::path& root, std::string& error) {
    std::vector<Module> modules;
    if (!Parse(bytes, modules, error)) return false;
    for (const auto& module : modules) {
        const auto directory = root / "Modules" / module.Manifest.Name;
        std::filesystem::create_directories(directory);
        if (!FileSystem::WriteFileAtomically(directory / "module.tomcat", module.Text, error)) return false;
        for (const auto& file : module.Files) {
            const auto target = directory / file.Path;
            std::filesystem::create_directories(target.parent_path());
            if (!FileSystem::WriteFileAtomically(target,
                {reinterpret_cast<const char*>(file.Bytes.data()), file.Bytes.size()}, error)) return false;
        }
    }
    return true;
}
}

namespace TomCat {
bool ModuleSystem::LoadCookedModules(std::span<const uint8_t> payload, std::string& error) {
    if (payload.empty()) return true;
#ifdef __EMSCRIPTEN__
    error = "native module packages require a desktop Player"; return false;
#endif
    if (!m_Loaded.empty() || !m_CookedModuleRoot.empty()) { error = "module host is already occupied"; return false; }
    try {
        const auto base = std::filesystem::temp_directory_path();
        for (unsigned attempt = 0; attempt < 64; ++attempt) {
            const auto root = base / ("TomCatModules-" + std::to_string(static_cast<uint64_t>(UUID())));
            if (std::filesystem::create_directory(root)) { m_CookedModuleRoot = root; break; }
        }
        if (m_CookedModuleRoot.empty()) { error = "could not allocate private module directory"; return false; }
        if (ModulePackage::Extract(payload, m_CookedModuleRoot, error)
            && LoadProjectModules(m_CookedModuleRoot, error, true, TomCatModule::HostKind::Player)) return true;
    } catch (const std::exception& exception) { error = exception.what(); }
    std::string cleanup; (void)UnloadAllModules(cleanup); return false;
}
}
