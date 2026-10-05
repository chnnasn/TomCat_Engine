#include "../EditorLayer.h"
#include "AutomationJson.h"
#include "TomCat/Scene/ComponentRegistry.h"
#include "TomCat/Scene/SceneCommandBuffer.h"
#include "TomCat/Scene/Serialization/SceneArchiveCodec.h"
#include "TomCat/Utils/PathUtils.h"
#include <yaml-cpp/yaml.h>
#include <algorithm>
#include <charconv>
#include <set>
#include <fstream>
#include <glad/glad.h>
#include "TomCat/Scene/Advanced2D.h"
#include "TomCat/Runtime/RuntimeUI.h"
#include "TomCat/Scene/SpriteAnimation.h"
#include "TomCat/Module/ModuleSystem.h"
#include "TomCat/Save/SaveDataStore.h"
#include <cstdlib>
#include "TomCat/Scripting/ScriptEngine.h"
#include "TomCat/Asset/AssetManager.h"
#include "TomCat/Utils/FileSystemUtils.h"
#include "TomCat/Debug/FrameProfiler.h"
#include "TomCat/Scene/Serialization/PrefabArchiveCodec.h"
#include "TomCat/Scene/Serialization/PrefabLink.h"

namespace TomCat {
namespace {
    using namespace AutomationJson;
    struct Failure : std::runtime_error
    {
        std::string Code;
        Failure(std::string code, std::string message) : std::runtime_error(message), Code(std::move(code)) {}
    };
    void Require(bool condition, const char* code, const std::string& message)
    { if (!condition) throw Failure(code, message); }
    std::string Text(const YAML::Node& args, const char* name)
    {
        Require(args[name] && args[name].IsScalar(), "INVALID_ARGUMENT", std::string("Required string: ") + name);
        return args[name].as<std::string>();
    }
    uint64_t Id(const YAML::Node& args, const char* name)
    {
        const auto text = Text(args, name);
        uint64_t result = 0;
        auto parsed = std::from_chars(text.data(), text.data() + text.size(), result);
        Require(!text.empty() && parsed.ec == std::errc{} && parsed.ptr == text.data() + text.size(),
            "INVALID_ARGUMENT", std::string(name) + " must be a decimal uint64 string");
        return result;
    }
    const ComponentDescriptor& Descriptor(const YAML::Node& args)
    {
        const auto* descriptor = ComponentRegistry::Get().Find(UUID(Id(args, "component_id")));
        Require(descriptor != nullptr, "UNKNOWN_COMPONENT", "Query component_get_schema for supported IDs.");
        return *descriptor;
    }
    Entity Find(const Ref<Scene>& scene, const YAML::Node& args)
    {
        Entity entity = scene->FindEntityByUUID(UUID(Id(args, "entity_id")));
        Require(bool(entity), "TARGET_NOT_FOUND", "Entity is absent from the selected scene.");
        return entity;
    }
    std::string Value(const PropertyValue& value)
    {
        return std::visit([](const auto& item) -> std::string {
            using T = std::decay_t<decltype(item)>;
            if constexpr (std::is_same_v<T, bool>) return item ? "true" : "false";
            else if constexpr (std::is_same_v<T, std::string>) return Quote(item);
            else if constexpr (std::is_same_v<T, int64_t> || std::is_same_v<T, uint64_t>) return Quote(std::to_string(item));
            else if constexpr (std::is_arithmetic_v<T>) return Number(item);
            else {
                std::vector<std::string> elements;
                for (int i = 0; i < item.length(); ++i) elements.push_back(Number(item[i]));
                return Array(elements);
            }
        }, value);
    }
    std::string Kind(PropertyKind kind)
    {
        static const char* names[] = {"", "bool", "int32", "int64", "uint32", "uint64", "float", "double", "string", "vector2", "vector3", "vector4"};
        return names[static_cast<unsigned>(kind)];
    }
    PropertyValue ParseValue(const YAML::Node& value, PropertyKind kind)
    {
        Require(bool(value), "INVALID_ARGUMENT", "Missing property value.");
        if (kind >= PropertyKind::Vector2)
        {
            const unsigned count = static_cast<unsigned>(kind) - static_cast<unsigned>(PropertyKind::Vector2) + 2;
            Require(value.IsSequence() && value.size() == count, "INVALID_ARGUMENT", "Wrong vector dimension.");
            glm::vec4 vector(0);
            for (unsigned i = 0; i < count; ++i)
            { vector[i] = value[i].as<float>(); Require(std::isfinite(vector[i]), "INVALID_ARGUMENT", "Vector must be finite."); }
            if (count == 2) return glm::vec2(vector);
            if (count == 3) return glm::vec3(vector);
            return vector;
        }
        Require(value.IsScalar(), "INVALID_ARGUMENT", "Expected scalar property value.");
        switch (kind)
        {
        case PropertyKind::Bool:
            Require(value.Scalar() == "true" || value.Scalar() == "false", "INVALID_ARGUMENT", "Expected boolean.");
            return value.as<bool>();
        case PropertyKind::Int32: return value.as<int32_t>();
        case PropertyKind::Int64: return value.as<int64_t>();
        case PropertyKind::UInt32: return value.as<uint32_t>();
        case PropertyKind::UInt64: return value.as<uint64_t>();
        case PropertyKind::Float: {
            const float result = value.as<float>();
            Require(std::isfinite(result), "INVALID_ARGUMENT", "Expected finite float."); return result; }
        case PropertyKind::Double: {
            const double result = value.as<double>();
            Require(std::isfinite(result), "INVALID_ARGUMENT", "Expected finite double."); return result; }
        case PropertyKind::String: return value.as<std::string>();
        default: throw Failure("INVALID_ARGUMENT", "Unsupported property kind.");
        }
    }
    std::string EntityInfo(const Ref<Scene>& scene, Entity entity, bool properties)
    {
        std::vector<std::string> components;
        for (const auto& descriptor : ComponentRegistry::Get().GetDescriptors())
        {
            if (!descriptor.Has || !descriptor.Has(entity)) continue;
            std::vector<std::string> values;
            if (properties) for (const auto& property : descriptor.Properties)
                if (property.Get) values.push_back(Object({
                    {"id", Quote(std::to_string(static_cast<uint64_t>(property.PropertyId)))},
                    {"name", Quote(property.StableName)}, {"value", Value(property.Get(entity))}}));
            components.push_back(Object({{"id", Quote(std::to_string(static_cast<uint64_t>(descriptor.TypeId)))},
                {"name", Quote(descriptor.StableName)}, {"properties", Array(values)}}));
        }
        auto parent = scene->GetParent(entity);
        return Object({{"id", Quote(std::to_string(static_cast<uint64_t>(entity.GetUUID())))},
            {"name", Quote(entity.GetName())}, {"parent_id", parent ? Quote(std::to_string(static_cast<uint64_t>(parent.GetUUID()))) : "null"},
            {"components", Array(components)}});
    }

    std::filesystem::path BoundedPath(const std::filesystem::path& root, const std::string& value)
    {
        const auto relative = UTF8ToPath(value);
        Require(!value.empty() && value.find('\0') == std::string::npos && !relative.is_absolute()
            && !relative.has_root_name(), "INVALID_PATH", "Expected a relative path.");
        for (const auto& part : relative)
            Require(part != ".." && part != "." && PathToUTF8(part).find(':') == std::string::npos,
                "INVALID_PATH", "Traversal and alternate streams are forbidden.");
        const auto absolute = std::filesystem::absolute(root / relative).lexically_normal();
        auto parent = absolute.parent_path();
        while (!std::filesystem::exists(parent)) parent = parent.parent_path();
        FileSystem::PinnedDirectoryChain guard;
        std::string error;
        Require(guard.Acquire(parent, error), "INVALID_PATH", error);
        Require(!std::filesystem::is_symlink(std::filesystem::symlink_status(absolute)), "INVALID_PATH", "Links are forbidden.");
        return absolute;
    }
    std::filesystem::path AssetPath(const Ref<Project>& project, const std::string& value)
    {
        Require(bool(project), "PROJECT_REQUIRED", "Open a project first.");
        const auto path = BoundedPath(project->GetAssetPath(), value);
        Require(!AssetRegistry::IsMetaFile(path) && AssetManager::Get().GetRegistry().IsManagedPath(path, true),
            "INVALID_PATH", "Expected an asset path with an existing parent directory.");
        return path;
    }
    std::string ReadAutomationText(const std::filesystem::path& path)
    {
        Require(std::filesystem::is_regular_file(path) && std::filesystem::file_size(path) <= 512 * 1024,
            "READ_FAILED", "Expected a regular text asset up to 512 KiB.");
        std::ifstream input(path, std::ios::binary);
        Require(bool(input), "READ_FAILED", "Could not open text asset.");
        std::string text((std::istreambuf_iterator<char>(input)), {});
        Require(text.find('\0') == std::string::npos, "READ_FAILED", "Asset is binary.");
        return text;
    }
    bool IsEdit(const std::string& tool)
    {
        return tool == "tilemap_set_cells" || tool == "script_attach" || tool == "scene_set_archive" || tool == "prefab_instantiate" || tool == "prefab_update" || tool == "entity_create" || tool == "entity_delete" || tool == "entity_reparent"
            || tool == "component_add" || tool == "component_remove" || tool == "component_set";
    }
    const std::map<std::string, std::set<std::string>> Arguments{
        {"automation_get_capabilities", {}},
        {"project_create", {"path", "name", "template"}},
        {"project_open", {"path"}},
        {"scene_new", {}},
        {"scene_open", {"path"}},
        {"scene_save_as", {"path"}},
        {"scene_get_archive", {}},
        {"scene_set_archive", {"document"}},
        {"asset_list", {"offset", "limit"}},
        {"asset_read_text", {"path"}},
        {"asset_write_text", {"path", "text", "expected_text"}},
        {"asset_import", {"path"}},
        {"asset_set_import_settings", {"handle", "settings"}},
        {"asset_move", {"handle", "path"}},
        {"asset_delete", {"handle"}},
        {"script_attach", {"entity_id", "handle"}}, {"script_compile", {}},
        {"script_get_status", {}},
        {"build_get_settings", {}},
        {"build_set_scenes", {"entry_scene", "scenes"}},
        {"build_player", {"development"}},
        {"prefab_save", {"entity_id", "path"}},
        {"prefab_instantiate", {"handle", "parent_id"}},
        {"prefab_update", {"entity_id", "revert"}},
        {"profiler_set_recording", {"enabled"}},
        {"profiler_get_frames", {}},
        {"runtime_start", {"width", "height"}}, {"runtime_step", {"events", "frames"}},
        {"runtime_get_input", {}}, {"runtime_assert", {"entity_id", "component_id", "property_id", "value", "tolerance"}},
        {"asset_validate", {"handle"}},
        {"runtime_get_ui", {}},
        {"runtime_scene_status", {}},
        {"runtime_scene_load", {"handle", "additive"}},
        {"runtime_scene_unload", {"handle"}},
        {"runtime_scene_activation", {"allow", "maximum_units", "milliseconds"}},
        {"runtime_scene_cancel", {}},
        {"runtime_animator", {"entity_id", "action", "name", "value"}},
        {"module_list", {}},
        {"module_invoke", {"module", "command"}},
        {"save_list", {}},
        {"save_read", {"slot"}},
        {"save_write", {"slot", "data_version", "text"}},
        {"save_delete", {"slot"}},
        {"tilemap_set_cells", {"entity_id", "cells"}},
        {"runtime_capture", {}},
        {"profiler_export", {}},
        {"asset_create_directory", {"path"}},
        {"project_get_settings", {}},
        {"project_set_settings", {"tags", "layer_names", "collision_masks"}},
        {"player_get_settings", {}},
        {"player_set_settings", {"product", "company", "version", "icon", "width", "height", "window_mode", "resizable", "vsync", "save_directory", "log_directory", "crash_directory"}},
        {"prefab_get_overrides", {"entity_id"}}, {"prefab_action", {"entity_id", "action", "target_id", "component_id", "property_id"}},
        {"editor_get_status", {}}, {"scene_get_tree", {"offset", "limit"}}, {"entity_get", {"entity_id"}},
        {"component_get_schema", {}}, {"console_get_entries", {"after", "limit"}},
        {"entity_create", {"name"}}, {"entity_delete", {"entity_id"}}, {"entity_reparent", {"entity_id", "parent_id"}},
        {"component_add", {"entity_id", "component_id"}}, {"component_remove", {"entity_id", "component_id"}},
        {"component_set", {"entity_id", "component_id", "property_id", "value"}},
        {"editor_play", {}}, {"editor_pause", {}}, {"editor_step", {}}, {"editor_stop", {}},
        {"scene_save", {}}, {"history_undo", {}}, {"history_redo", {}}
    };
    std::string Edit(const Ref<Scene>& scene, const std::string& tool, const YAML::Node& args, const ScriptMetadataCache& metadata)
    {
        std::string error;
        if (tool == "tilemap_set_cells")
        {
            auto entity = Find(scene, args);
            Require(entity.HasComponent<Tilemap2D>(), "MISSING_COMPONENT", "Add Tilemap2D first.");
            Require(args["cells"].IsSequence() && args["cells"].size() <= 10000, "INVALID_ARGUMENT", "Expected at most 10000 cells.");
            auto& tilemap = entity.GetComponent<Tilemap2D>();
            for (const auto& item : args["cells"])
            {
                const glm::ivec2 coordinate(item["x"].as<int32_t>(), item["y"].as<int32_t>());
                if (item["erase"] && item["erase"].as<bool>()) { Tilemap2DRuntime::EraseCell(tilemap, coordinate); continue; }
                TilemapCell cell;
                cell.Coordinate = coordinate;
                cell.SpriteHandle = AssetHandle(Id(item, "sprite"));
                if (item["tint"]) cell.Tint = std::get<glm::vec4>(ParseValue(item["tint"], PropertyKind::Vector4));
                cell.FlipX = item["flip_x"] && item["flip_x"].as<bool>();
                cell.FlipY = item["flip_y"] && item["flip_y"].as<bool>();
                cell.RotationQuarterTurns = item["rotation"] ? item["rotation"].as<int32_t>() : 0;
                (void)Tilemap2DRuntime::SetCell(tilemap, cell);
            }
            return Object({{"cell_count", std::to_string(tilemap.Cells.size())}});
        }
        if (tool == "script_attach")
        {
            auto entity = Find(scene, args);
            const auto handle = AssetHandle(Id(args, "handle"));
            const auto script = metadata.Find(handle);
            Require(script.has_value(), "COMPILE_REQUIRED", "Compile the current source before attaching this script.");
            auto& scripts = entity.HasComponent<CSharpScripts>() ? entity.GetComponent<CSharpScripts>() : entity.AddComponent<CSharpScripts>();
            Require(!script->DisallowMultiple || std::none_of(scripts.Scripts.begin(), scripts.Scripts.end(),
                [handle](const auto& existing) { return existing.ScriptAsset == handle; }), "EDIT_REJECTED", "Script disallows multiple attachments.");
            CSharpScriptEntry entry;
            entry.ScriptAsset = handle;
            entry.LastKnownClassName = script->TypeName;
            ReconcileScriptEntryFields(entry, *script);
            const auto id = entry.AttachmentID;
            scripts.Scripts.push_back(std::move(entry));
            return Object({{"attachment_id", Quote(std::to_string(uint64_t(id)))}});
        }
        if (tool == "scene_set_archive")
        {
            const auto text = Text(args, "document");
            Require(SceneArchiveCodec::Decode(std::vector<uint8_t>(text.begin(), text.end()), scene, "automation.tomcat", true),
                "EDIT_REJECTED", "Scene archive validation failed; see console diagnostics.");
            return "{}";
        }
        if (tool == "prefab_instantiate")
        {
            const auto handle = AssetHandle(Id(args, "handle"));
            PrefabArchive archive;
            Require(PrefabArchiveCodec::Load(handle, archive, error), "EDIT_REJECTED", error);
            PrefabInstantiateOptions options;
            if (args["parent_id"] && !args["parent_id"].IsNull()) options.Parent = UUID(Id(args, "parent_id"));
            PrefabInstantiationResult result;
            Require(PrefabArchiveCodec::Instantiate(archive, *scene, options, result, error), "EDIT_REJECTED", error);
            Require(PrefabLinkedInstance::Attach(scene, handle, archive, result, error), "EDIT_REJECTED", error);
            return EntityInfo(scene, result.Root, true);
        }
        if (tool == "prefab_update")
        {
            auto entity = Find(scene, args);
            Require(entity.HasComponent<PrefabLink>(), "EDIT_REJECTED", "Entity has no prefab link.");
            PrefabArchive archive;
            Require(PrefabArchiveCodec::Load(entity.GetComponent<PrefabLink>().Source, archive, error), "EDIT_REJECTED", error);
            Require(PrefabLinkedInstance::Update(scene, entity.GetUUID(), archive,
                args["revert"] && args["revert"].as<bool>(), error), "EDIT_REJECTED", error);
            return "{}";
        }
        if (tool == "entity_create")
        {
            auto name = Text(args, "name");
            Require(!name.empty() && name.size() <= 256 && name.find('\0') == std::string::npos,
                "INVALID_ARGUMENT", "Name must contain 1..256 UTF-8 bytes and no NUL.");
            return EntityInfo(scene, scene->CreateEntity(name), true);
        }
        auto entity = Find(scene, args);
        if (tool == "entity_delete" || tool == "entity_reparent")
        {
            SceneCommandBuffer commands(*scene);
            if (tool == "entity_delete") commands.DestroyEntity(entity.GetUUID());
            else {
                std::optional<UUID> parent;
                if (args["parent_id"] && !args["parent_id"].IsNull()) parent = UUID(Id(args, "parent_id"));
                commands.ReparentEntity(entity.GetUUID(), parent);
            }
            Require(commands.Flush(error), "EDIT_REJECTED", error);
            return "{}";
        }
        const auto& descriptor = Descriptor(args);
        Require(static_cast<uint64_t>(descriptor.ProviderId) == 0,
            "UNSUPPORTED_COMPONENT", "Automation edits engine-owned components through staged validation.");
        if (tool == "component_add")
        {
            Require(descriptor.AddableInInspector, "EDIT_REJECTED", "Component is not addable.");
            Require(ComponentRegistry::Get().Add(entity, descriptor.TypeId, error), "EDIT_REJECTED", error);
        }
        else if (tool == "component_remove")
        {
            Require(descriptor.Removable, "EDIT_REJECTED", "Component is not removable.");
            Require(ComponentRegistry::Get().Remove(entity, descriptor.TypeId, error), "EDIT_REJECTED", error);
        }
        else
        {
            Require(descriptor.Has && descriptor.Has(entity), "MISSING_COMPONENT", "Add the component before setting its properties.");
            const auto propertyId = Id(args, "property_id");
            auto property = std::find_if(descriptor.Properties.begin(), descriptor.Properties.end(),
                [propertyId](const auto& p) { return static_cast<uint64_t>(p.PropertyId) == propertyId; });
            Require(property != descriptor.Properties.end() && bool(property->Set), "UNKNOWN_PROPERTY", "Query component_get_schema for writable properties.");
            const auto value = ParseValue(args["value"], property->Kind);
            Require(property->Set(entity, value, error), "EDIT_REJECTED", error);
            if (static_cast<uint64_t>(descriptor.TypeId) == ComponentIds::Transform)
            {
                const auto& transform = entity.GetComponent<Transform>();
                const bool local = propertyId >= ComponentIds::TransformProperties::LocalTranslation;
                Require(local ? scene->SetLocalTransform(entity, transform.GetLocalTransform())
                    : scene->SetWorldTransform(entity, transform.GetTransform()),
                    "EDIT_REJECTED", "Transform cannot be represented in the current hierarchy.");
            }
        }
        return EntityInfo(scene, entity, true);
    }
}

std::string EditorLayer::ExecuteAutomation(const std::string& request)
{
    using namespace AutomationJson;
    std::string requestId;
    bool remember = false;
    std::string response;
    try
    {
        const auto root = YAML::Load(request);
        Require(root.IsMap(), "INVALID_ARGUMENT", "Expected request object.");
        const auto tool = Text(root, "tool");
        const auto args = root["arguments"];
        Require(args.IsMap(), "INVALID_ARGUMENT", "arguments must be an object.");
        const auto spec = Arguments.find(tool);
        Require(spec != Arguments.end(), "UNKNOWN_TOOL", "Unknown automation tool.");
        for (const auto& field : args)
            Require(spec->second.contains(field.first.as<std::string>()), "INVALID_ARGUMENT", "Unknown argument: " + field.first.as<std::string>());
        static const std::set<std::string> ReadOnly{
            "editor_get_status",
            "scene_get_tree",
            "entity_get",
            "component_get_schema",
            "console_get_entries",
            "automation_get_capabilities",
            "scene_get_archive",
            "asset_list",
            "asset_read_text",
            "script_get_status",
            "build_get_settings",
            "prefab_get_overrides",
            "profiler_get_frames",
            "runtime_get_input",
            "runtime_assert",
            "runtime_get_ui",
            "runtime_scene_status",
            "module_list",
            "save_list",
            "project_get_settings",
            "player_get_settings",
        };
        const bool readOnly = ReadOnly.contains(tool);
        if (m_AutomationObservedScene != m_ActiveScene.get())
        { m_AutomationObservedScene = m_ActiveScene.get(); ++m_AutomationSceneEpoch; }
        auto revision = [&]() { return std::to_string(m_AutomationSceneEpoch) + ":" + std::to_string(m_SceneHistory.GetCurrentStateId()); };
        const auto project = m_CurrentProject ? PathToUTF8(m_CurrentProject->GetProjectPath()) : "";
        if (tool != "editor_get_status" && tool != "automation_get_capabilities" && tool != "project_create" && tool != "project_open")
        {
            Require(root["project"] && !project.empty(), "PROJECT_REQUIRED", "Bind the client to the open .tcproj file.");
            std::error_code error;
            Require(std::filesystem::equivalent(UTF8ToPath(Text(root, "project")), m_CurrentProject->GetProjectPath(), error)
                && !error, "PROJECT_MISMATCH", "The Editor has a different project open.");
        }
        if (!readOnly)
        {
            Require(Text(root, "session_id") == m_AutomationSession, "SESSION_MISMATCH", "Editor restarted; reconnect before writing.");
            requestId = Text(root, "request_id");
            Require(!requestId.empty() && requestId.size() <= 128, "INVALID_ARGUMENT", "request_id must contain 1..128 bytes.");
            if (auto found = m_AutomationResponses.find(requestId); found != m_AutomationResponses.end())
            {
                Require(found->second.first == request, "REQUEST_ID_REUSED", "Reuse an ID only with the identical request body.");
                return found->second.second;
            }
            Require(!m_AutomationSeenRequests.contains(requestId), "REQUEST_EXPIRED",
                "The cached response expired; inspect current state instead of replaying this write.");
            Require(m_AutomationSeenRequests.size() < 65536, "SESSION_LIMIT",
                "This automation session reached its write limit. Save and restart the Editor.");
            remember = true;
            Require(Text(root, "scene_version") == revision(), "SCENE_CHANGED", "Read scene state again before editing.");
            Require(!m_SceneHistory.HasActiveTransaction() && !m_PendingUnsavedAction
                && !m_OpenRecoveryModal,
                "EDITOR_BUSY", "Complete the active editor interaction or dialog first.");
        }
        std::string data = "{}";
        if (tool == "editor_get_status")
            data = Object({{"session_id", Quote(m_AutomationSession)}, {"project", Quote(project)},
                {"scene_path", Quote(PathToUTF8(m_EditorScenePath))}, {"scene_version", Quote(revision())},
                {"state", Quote(m_SceneState == Edit ? "edit" : m_SceneState == Play ? "play" : "pause")},
                {"dirty", IsSceneDirty() ? "true" : "false"}, {"protocol_version", "1"}});
        else if (tool == "runtime_start")
        {
            Require(m_SceneState == SceneState::Edit, "EDIT_MODE_REQUIRED", "Stop Play before starting a deterministic runtime.");
            const int width = args["width"] ? args["width"].as<int>() : 640;
            const int height = args["height"] ? args["height"].as<int>() : 360;
            Require(width >= 16 && height >= 16 && width <= 4096 && height <= 4096, "INVALID_ARGUMENT", "Viewport dimensions must be 16..4096.");
            struct StartingScope { bool& Flag; StartingScope(bool& flag) : Flag(flag) { Flag = true; } ~StartingScope() { Flag = false; } } starting(m_AutomationStarting);
            OnScenePlay();
            Require(IsSceneRunning(), "PLAY_FAILED", "Compile and enable the saved scene in Build Settings; inspect console diagnostics.");
            OnScenePause();
            m_AutomationWidth = width;
            m_AutomationHeight = height;
            m_AutomationRuntimeFrame = 0;
            m_AutomationInputQueue.ClearState();
            m_AutomationInput = {};
            Input::SetAutomationFrame(m_AutomationInput);
            Scripting::ScriptEngine::Get().SetInputEnabled(true);
            Scripting::ScriptEngine::Get().CaptureInputState();
            data = Object({{"frame", "0"}});
        }
        else if (tool == "runtime_step")
        {
            Require(m_SceneState == SceneState::Pause && Input::HasAutomationFrame(), "INVALID_STATE", "Call runtime_start first.");
            const int frames = args["frames"] ? args["frames"].as<int>() : 1;
            Require(frames >= 1 && frames <= 600, "INVALID_ARGUMENT", "frames must be 1..600.");
            const auto events = args["events"];
            Require(!events || (events.IsSequence() && events.size() <= 1024), "INVALID_ARGUMENT", "events must be an array of at most 1024 events.");
            // Validate and stage the complete batch before advancing any frame.
            auto queue = m_AutomationInputQueue;
            auto input = m_AutomationInput;
            input.Text.clear(); input.ScrollX = input.ScrollY = 0;
            if (events) for (const auto& event : events)
            {
                const auto type = Text(event, "type");
                const double timestamp = double(m_AutomationRuntimeFrame) * Scene::FixedRuntimeTimestep;
                if (type == "key" || type == "button")
                {
                    const auto code = event["code"].as<uint32_t>();
                    const auto action = Text(event, "action");
                    Require(code < (type == "key" ? 512u : 8u) && (action == "press" || action == "release" || action == "repeat"),
                        "INVALID_ARGUMENT", "Invalid key/button code or action.");
                    Require(queue.Push(type == "key" ? InputEventQueue::Device::Keyboard : InputEventQueue::Device::Mouse,
                        code, action == "press" ? InputEventQueue::Action::Pressed : action == "release" ? InputEventQueue::Action::Released : InputEventQueue::Action::Repeated,
                        timestamp), "INVALID_ARGUMENT", "Invalid input transition.");
                }
                else if (type == "pointer" || type == "scroll")
                {
                    const float x = event["x"].as<float>(), y = event["y"].as<float>();
                    Require(std::isfinite(x) && std::isfinite(y) && std::abs(x) <= 100000 && std::abs(y) <= 100000, "INVALID_ARGUMENT", "Input coordinates must be finite and bounded.");
                    if (type == "pointer") { input.MouseX = x; input.MouseY = y; }
                    else { input.ScrollX += x; input.ScrollY += y; }
                }
                else if (type == "text")
                {
                    input.Text += Text(event, "text");
                    Require(input.Text.size() <= 65536 && input.Text.find('\0') == std::string::npos, "INVALID_ARGUMENT", "Text must contain at most 64 KiB and no NUL.");
                }
                else if (type == "focus")
                {
                    input.Focused = event["focused"].as<bool>();
                    if (!input.Focused) queue.ReleaseAll(timestamp);
                }
                else throw Failure("INVALID_ARGUMENT", "Unknown input event type.");
            }
            m_AutomationInputQueue = std::move(queue);
            m_AutomationInput = std::move(input);
            for (int i = 0; i < frames; ++i)
            {
                if (i) { m_AutomationInput.Text.clear(); m_AutomationInput.ScrollX = m_AutomationInput.ScrollY = 0; }
                m_AutomationInputQueue.Freeze();
                m_AutomationInput.Snapshot = m_AutomationInputQueue.GetSnapshot();
                Input::SetAutomationFrame(m_AutomationInput);
                Scripting::ScriptEngine::Get().SetInputEnabled(true);
                Scripting::ScriptEngine::Get().CaptureInputState();
                m_RuntimeSceneManager.SetViewportSize(m_AutomationWidth, m_AutomationHeight);
                m_RuntimeSceneManager.SetRuntimeUIViewportMetrics({0, 0}, 1.0f, {1, 1});
                m_ActiveScene->OnRuntimeStep(false);
                Require(m_ActiveScene->IsRuntimeRunning(), "RUNTIME_FAILED", "Runtime stopped during the step; inspect console diagnostics.");
                CommitRuntimeSceneTransition();
                ++m_AutomationRuntimeFrame;
            }
            data = Object({{"frame", std::to_string(m_AutomationRuntimeFrame)}, {"advanced_seconds", Number(frames * Scene::FixedRuntimeTimestep)}});
        }
        else if (tool == "runtime_get_input")
        {
            Require(Input::HasAutomationFrame(), "INVALID_STATE", "Call runtime_start first.");
            std::vector<std::string> held, pressed, released;
            const auto& snapshot = Input::GetFrameSnapshot();
            for (unsigned i = 0; i < 512; ++i)
            {
                if (snapshot.KeysHeld[i]) held.push_back(std::to_string(i));
                if (snapshot.KeysPressed[i]) pressed.push_back(std::to_string(i));
                if (snapshot.KeysReleased[i]) released.push_back(std::to_string(i));
            }
            data = Object({{"frame", std::to_string(m_AutomationRuntimeFrame)}, {"keys_held", Array(held)},
                {"keys_pressed", Array(pressed)}, {"keys_released", Array(released)}, {"text", Quote(Input::GetTextInput())}});
        }
        else if (tool == "runtime_assert")
        {
            Require(bool(m_ActiveScene), "NO_SCENE", "Open a scene first.");
            auto entity = Find(m_ActiveScene, args);
            const auto& descriptor = Descriptor(args);
            const auto id = Id(args, "property_id");
            Require(descriptor.Has && descriptor.Has(entity), "ASSERTION_FAILED", "Component is missing.");
            auto property = std::find_if(descriptor.Properties.begin(), descriptor.Properties.end(), [id](const auto& p) { return uint64_t(p.PropertyId) == id; });
            Require(property != descriptor.Properties.end() && property->Get, "UNKNOWN_PROPERTY", "Property is not readable.");
            const auto actual = property->Get(entity), expected = ParseValue(args["value"], property->Kind);
            const double tolerance = args["tolerance"] ? args["tolerance"].as<double>() : 0.0;
            Require(std::isfinite(tolerance) && tolerance >= 0, "INVALID_ARGUMENT", "tolerance must be finite and nonnegative.");
            const bool equal = std::visit([&](const auto& a) {
                using T = std::decay_t<decltype(a)>;
                const auto* b = std::get_if<T>(&expected);
                if (!b) return false;
                if constexpr (std::is_floating_point_v<T>) return std::abs(a - *b) <= tolerance;
                else if constexpr (std::is_same_v<T, glm::vec2> || std::is_same_v<T, glm::vec3> || std::is_same_v<T, glm::vec4>)
                { for (int i = 0; i < a.length(); ++i) if (std::abs(a[i] - (*b)[i]) > tolerance) return false; return true; }
                else return a == *b;
            }, actual);
            Require(equal, "ASSERTION_FAILED", "Property mismatch. Actual=" + Value(actual) + " expected=" + Value(expected));
            data = Object({{"passed", "true"}, {"actual", Value(actual)}});
        }
        else if (tool == "asset_validate")
        {
            const auto result = AssetManager::Get().LoadImportedArtifact(AssetHandle(Id(args, "handle")));
            Require(result.Succeeded(), "IMPORT_FAILED", result.Error);
            data = Object({{"valid", "true"}});
        }
        else if (tool == "runtime_get_ui")
        {
            Require(Input::HasAutomationFrame(), "INVALID_STATE", "Call runtime_start first.");
            const auto layout = RuntimeUISystem::BuildLayout(*m_ActiveScene, m_AutomationWidth, m_AutomationHeight);
            std::vector<std::string> elements;
            for (const auto& [id, rect] : layout.Rectangles)
            {
                const auto entity = m_ActiveScene->FindEntityByUUID(id);
                auto matrix = layout.Transforms.find(id);
                const glm::mat4 transform = matrix != layout.Transforms.end() ? matrix->second : glm::mat4(1.0f);
                const auto center = transform * glm::vec4(rect.X + rect.Width * .5f, rect.Y + rect.Height * .5f, 0, 1);
                elements.push_back(Object({{"entity_id", Quote(std::to_string(uint64_t(id)))},
                    {"name", Quote(entity.GetName())}, {"rect", Value(glm::vec4(rect.X, float(m_AutomationHeight) - rect.Y - rect.Height, rect.Width, rect.Height))},
                    {"center", Value(glm::vec2(center.x, float(m_AutomationHeight) - center.y))}, {"text", Quote(RuntimeUISystem::ResolveText(*m_ActiveScene, entity))},
                    {"clicked", RuntimeUISystem::WasButtonClicked(entity) ? "true" : "false"}}));
            }
            data = Object({{"elements", Array(elements)}});
        }
        else if (tool == "runtime_scene_status" || tool == "runtime_scene_load" || tool == "runtime_scene_unload"
            || tool == "runtime_scene_activation" || tool == "runtime_scene_cancel")
        {
            Require(IsSceneRunning(), "INVALID_STATE", "Start Play first.");
            if (tool == "runtime_scene_load")
            {
                const auto mode = args["additive"] && args["additive"].as<bool>() ? SceneLoadMode::Additive : SceneLoadMode::Single;
                Require(m_RuntimeSceneManager.RequestLoadSceneAsync(AssetHandle(Id(args, "handle")), mode), "LOAD_FAILED", m_RuntimeSceneManager.GetLastError());
            }
            else if (tool == "runtime_scene_unload")
                Require(m_RuntimeSceneManager.RequestUnloadScene(AssetHandle(Id(args, "handle"))), "LOAD_FAILED", m_RuntimeSceneManager.GetLastError());
            else if (tool == "runtime_scene_cancel")
                Require(m_RuntimeSceneManager.CancelPendingLoad(), "INVALID_STATE", "No cancellable load is pending.");
            else if (tool == "runtime_scene_activation")
            {
                if (args["maximum_units"] || args["milliseconds"])
                    Require(m_RuntimeSceneManager.SetActivationBudget({args["maximum_units"].as<uint32_t>(), args["milliseconds"].as<double>()}), "INVALID_ARGUMENT", "Invalid activation budget.");
                m_RuntimeSceneManager.SetAllowSceneActivation(args["allow"].as<bool>());
            }
            const auto statistics = m_RuntimeSceneManager.GetActivationStatistics();
            std::vector<std::string> loaded;
            for (auto handle : m_RuntimeSceneManager.GetLoadedSceneHandles()) loaded.push_back(Quote(std::to_string(uint64_t(handle))));
            static const char* states[] = {"idle", "reading", "ready", "completed", "failed", "cancelled", "decoding", "activating"};
            data = Object({{"state", Quote(states[static_cast<unsigned>(m_RuntimeSceneManager.GetLoadState())])},
                {"progress", Number(m_RuntimeSceneManager.GetLoadProgress())}, {"loaded", Array(loaded)},
                {"active", Quote(std::to_string(uint64_t(m_RuntimeSceneManager.GetActiveSceneHandle())))},
                {"activation_frames", Quote(std::to_string(statistics.Frames))}, {"peak_ms", Number(statistics.PeakMilliseconds)},
                {"error", Quote(m_RuntimeSceneManager.GetLastError())}});
        }
        else if (tool == "runtime_animator")
        {
            Require(IsSceneRunning(), "INVALID_STATE", "Start Play first.");
            auto entity = Find(m_ActiveScene, args);
            Require(entity.HasComponent<SpriteAnimator>() && entity.HasComponent<SpriteRenderer>(), "MISSING_COMPONENT", "Animator and renderer are required.");
            auto& animator = entity.GetComponent<SpriteAnimator>();
            const auto action = Text(args, "action");
            bool succeeded = true;
            if (action == "play") succeeded = SpriteAnimatorRuntime::Play(animator, entity.GetComponent<SpriteRenderer>(), Text(args, "name"));
            else if (action == "stop") SpriteAnimatorRuntime::Stop(animator);
            else if (action == "bool") succeeded = SpriteAnimatorRuntime::SetBool(animator, Text(args, "name"), args["value"].as<bool>());
            else if (action == "int") succeeded = SpriteAnimatorRuntime::SetInt(animator, Text(args, "name"), args["value"].as<int32_t>());
            else if (action == "float")
            {
                const float value = args["value"].as<float>();
                Require(std::isfinite(value), "INVALID_ARGUMENT", "Expected finite float.");
                succeeded = SpriteAnimatorRuntime::SetFloat(animator, Text(args, "name"), value);
            }
            else if (action == "trigger") succeeded = SpriteAnimatorRuntime::SetTrigger(animator, Text(args, "name"));
            else if (action != "status") throw Failure("INVALID_ARGUMENT", "Unknown animator action.");
            Require(succeeded, "INVALID_ARGUMENT", "Unknown clip/parameter or mismatched type.");
            data = Object({{"state", Quote(std::string(SpriteAnimatorRuntime::CurrentState(animator)))},
                {"frame", Number(animator.RuntimeFrameIndex)}, {"playing", animator.RuntimePlaying ? "true" : "false"}});
        }
        else if (tool == "module_list" || tool == "module_invoke")
        {
            auto& modules = ModuleSystem::Get();
            const auto commands = modules.GetEditorCommands();
            if (tool == "module_invoke")
            {
                Require(m_SceneState == SceneState::Edit, "EDIT_MODE_REQUIRED", "Stop Play first.");
                const auto module = Text(args, "module"), label = Text(args, "command");
                std::vector<ModuleEditorCommand> found;
                for (const auto& command : commands) if (command.ModuleName == module && command.Label == label) found.push_back(command);
                Require(found.size() == 1 && found.front().Callback, "INVALID_ARGUMENT", "Module command must resolve uniquely.");
                found.front().Callback();
                CommitImmediateSceneTransaction("Automation module command");
            }
            std::vector<std::string> loaded, actions;
            for (const auto& module : modules.GetLoadedModules()) loaded.push_back(Object({{"name", Quote(module.Name)}, {"version", Quote(module.Version)}}));
            for (const auto& command : commands) actions.push_back(Object({{"module", Quote(command.ModuleName)}, {"command", Quote(command.Label)}}));
            data = Object({{"loaded", Array(loaded)}, {"commands", Array(actions)}});
        }
        else if (tool == "save_list" || tool == "save_read" || tool == "save_write" || tool == "save_delete")
        {
            Require(Input::HasAutomationFrame(), "INVALID_STATE", "Save automation requires an isolated runtime_start session.");
            std::string error;
            auto store = Save::SaveDataStore::ResolveForRunningGame(error);
            Require(store.has_value(), "SAVE_FAILED", error);
            if (tool == "save_list")
            {
                std::vector<std::string> slots;
                for (const auto& slot : store->List(error)) slots.push_back(Object({{"slot", Quote(slot.Slot)},
                    {"data_version", Number(slot.DataVersion)}, {"bytes", Quote(std::to_string(slot.PayloadBytes))}, {"corrupted", slot.Corrupted ? "true" : "false"}}));
                Require(error.empty(), "SAVE_FAILED", error);
                data = Object({{"slots", Array(slots)}});
            }
            else if (tool == "save_write")
            {
                const auto text = Text(args, "text");
                Require(store->Write(Text(args, "slot"), args["data_version"].as<uint32_t>(),
                    std::span<const uint8_t>(reinterpret_cast<const uint8_t*>(text.data()), text.size()), error), "SAVE_FAILED", error);
            }
            else if (tool == "save_delete")
            {
                bool removed = false;
                Require(store->Delete(Text(args, "slot"), removed, error), "SAVE_FAILED", error);
                data = Object({{"removed", removed ? "true" : "false"}});
            }
            else
            {
                Save::SaveEnvelope envelope;
                std::vector<uint8_t> bytes;
                const auto status = store->Read(Text(args, "slot"), envelope, bytes, error);
                Require(status == Save::SlotReadStatus::Ok || status == Save::SlotReadStatus::RecoveredFromBackup, "SAVE_FAILED", error);
                std::string hex;
                Require(bytes.size() <= 512 * 1024, "RESPONSE_TOO_LARGE", "Save payload exceeds automation read limit.");
                constexpr char digits[] = "0123456789abcdef";
                for (auto b : bytes) { hex += digits[b >> 4]; hex += digits[b & 15]; }
                data = Object({{"payload_hex", Quote(hex)}, {"data_version", Number(envelope.DataVersion)},
                    {"recovered", status == Save::SlotReadStatus::RecoveredFromBackup ? "true" : "false"}});
            }
        }
        else if (tool == "asset_create_directory")
        {
            Require(m_SceneState == SceneState::Edit, "EDIT_MODE_REQUIRED", "Stop Play first.");
            const auto path = BoundedPath(m_CurrentProject->GetAssetPath(), Text(args, "path"));
            FileSystem::PinnedDirectoryChain guard;
            bool created = false;
            std::string error;
            Require(FileSystem::CreateDirectoryAndPin(path, guard, created, error), "WRITE_FAILED", error);
            data = Object({{"created", created ? "true" : "false"}});
        }
        else if (tool == "project_get_settings" || tool == "project_set_settings")
        {
            auto settings = m_CurrentProject->GetSettings();
            if (tool == "project_set_settings")
            {
                Require(m_SceneState == SceneState::Edit, "EDIT_MODE_REQUIRED", "Stop Play before changing settings.");
                if (args["tags"]) settings.TagsAndLayers.Tags = args["tags"].as<std::vector<std::string>>();
                if (args["layer_names"])
                {
                    Require(args["layer_names"].IsSequence() && args["layer_names"].size() == 16, "INVALID_ARGUMENT", "Expected 16 layer names.");
                    for (int i = 0; i < 16; ++i) settings.TagsAndLayers.LayerNames[i] = args["layer_names"][i].as<std::string>();
                }
                if (args["collision_masks"])
                {
                    Require(args["collision_masks"].IsSequence() && args["collision_masks"].size() == 16, "INVALID_ARGUMENT", "Expected 16 symmetric collision masks.");
                    for (int i = 0; i < 16; ++i) settings.Physics2D.CollisionMasks[i] = args["collision_masks"][i].as<uint16_t>();
                }
                Require(m_CurrentProject->SetSettings(settings), "SAVE_FAILED", "Invalid project settings or write failed.");
            }
            std::vector<std::string> tags, layers, masks;
            for (const auto& tag : settings.TagsAndLayers.Tags) tags.push_back(Quote(tag));
            for (const auto& layer : settings.TagsAndLayers.LayerNames) layers.push_back(Quote(layer));
            for (auto mask : settings.Physics2D.CollisionMasks) masks.push_back(Number(mask));
            data = Object({{"tags", Array(tags)}, {"layer_names", Array(layers)}, {"collision_masks", Array(masks)}});
        }
        else if (tool == "player_get_settings" || tool == "player_set_settings")
        {
            auto settings = m_CurrentProject->GetPlayerSettings();
            if (tool == "player_set_settings")
            {
                Require(m_SceneState == SceneState::Edit, "EDIT_MODE_REQUIRED", "Stop Play before changing settings.");
                if (args["product"]) settings.ProductName = Text(args, "product");
                if (args["company"]) settings.CompanyName = Text(args, "company");
                if (args["version"]) settings.Version = Text(args, "version");
                if (args["icon"]) settings.Icon = AssetHandle(Id(args, "icon"));
                if (args["width"]) settings.Width = args["width"].as<uint32_t>();
                if (args["height"]) settings.Height = args["height"].as<uint32_t>();
                if (args["window_mode"]) Require(PlayerWindowModeFromString(Text(args, "window_mode"), settings.WindowMode), "INVALID_ARGUMENT", "Unknown window mode.");
                if (args["resizable"]) settings.Resizable = args["resizable"].as<bool>();
                if (args["vsync"]) settings.VSync = args["vsync"].as<bool>();
                if (args["save_directory"]) settings.SaveDirectory = UTF8ToPath(Text(args, "save_directory"));
                if (args["log_directory"]) settings.LogDirectory = UTF8ToPath(Text(args, "log_directory"));
                if (args["crash_directory"]) settings.CrashDirectory = UTF8ToPath(Text(args, "crash_directory"));
                Require(m_CurrentProject->SetPlayerSettings(settings), "SAVE_FAILED", "Invalid Player settings or write failed.");
            }
            data = Object({{"product", Quote(settings.ProductName)}, {"company", Quote(settings.CompanyName)},
                {"version", Quote(settings.Version)}, {"icon", Quote(std::to_string(uint64_t(settings.Icon)))},
                {"width", Number(settings.Width)}, {"height", Number(settings.Height)}, {"window_mode", Quote(PlayerWindowModeToString(settings.WindowMode))},
                {"resizable", settings.Resizable ? "true" : "false"}, {"vsync", settings.VSync ? "true" : "false"},
                {"save_directory", Quote(PathToUTF8(settings.SaveDirectory))}, {"log_directory", Quote(PathToUTF8(settings.LogDirectory))},
                {"crash_directory", Quote(PathToUTF8(settings.CrashDirectory))}});
        }
        else if (tool == "runtime_capture" || tool == "profiler_export")
        {
            auto directory = m_CurrentProject->GetLibraryPath();
            std::string error;
            FileSystem::PinnedDirectoryChain guard;
            bool created = false;
            for (const auto& part : {std::string("Automation"), m_AutomationSession})
            {
                directory /= part;
                Require(FileSystem::CreateDirectoryAndPin(directory, guard, created, error), "WRITE_FAILED", error);
            }
            const auto path = directory / (tool == "runtime_capture" ? "frame-" + std::to_string(m_AutomationRuntimeFrame) + ".ppm" : "profile.json");
            if (tool == "profiler_export")
                Require(FrameProfiler::Get().ExportTrace(path), "WRITE_FAILED", "Could not export profiler trace.");
            else
            {
                Require(Input::HasAutomationFrame(), "INVALID_STATE", "Call runtime_start first.");
                Require(m_GameFramebuffer->Resize(m_AutomationWidth, m_AutomationHeight), "RENDER_FAILED", "Framebuffer resize failed.");
                m_ActiveScene->OnViewportResize(m_AutomationWidth, m_AutomationHeight);
                m_ActiveScene->SetRuntimeUIViewportMetrics({0, 0}, 1.0f, {1, 1});
                std::vector<uint8_t> pixels(size_t(m_AutomationWidth) * m_AutomationHeight * 3);
                m_GameFramebuffer->Bind();
                try
                {
                    m_ActiveScene->OnRenderRuntime();
                    GLint previousAlignment = 4;
                    glGetIntegerv(GL_PACK_ALIGNMENT, &previousAlignment);
                    glPixelStorei(GL_PACK_ALIGNMENT, 1);
                    glReadBuffer(GL_COLOR_ATTACHMENT0);
                    glReadPixels(0, 0, m_AutomationWidth, m_AutomationHeight, GL_RGB, GL_UNSIGNED_BYTE, pixels.data());
                    glPixelStorei(GL_PACK_ALIGNMENT, previousAlignment);
                }
                catch (...) { m_GameFramebuffer->Unbind(); throw; }
                m_GameFramebuffer->Unbind();
                std::string ppm = "P6\n" + std::to_string(m_AutomationWidth) + " " + std::to_string(m_AutomationHeight) + "\n255\n";
                for (int row = int(m_AutomationHeight) - 1; row >= 0; --row)
                    ppm.append(reinterpret_cast<const char*>(pixels.data() + size_t(row) * m_AutomationWidth * 3), size_t(m_AutomationWidth) * 3);
                Require(FileSystem::WriteFileAtomically(path, ppm, error), "WRITE_FAILED", error);
            }
            data = Object({{"path", Quote(PathToUTF8(path))}});
        }
        else if (tool == "automation_get_capabilities")
        {
            std::vector<std::string> names;
            for (const auto& [name, fields] : Arguments) names.push_back(Quote(name));
            data = Object({{"tools", Array(names)}, {"workflow_version", "1"},
                {"input_coordinates", Quote("game framebuffer pixels")}});
        }
        else if (tool == "project_create" || tool == "project_open")
        {
            Require(m_SceneState == SceneState::Edit && !IsSceneDirty(), "UNSAVED_CHANGES", "Save the scene and stop Play before switching projects.");
            Require(!m_ScriptCompiler.IsCompileInProgress(), "EDITOR_BUSY", "Wait for compilation before switching projects.");
            char* workspaceValue = nullptr;
            size_t workspaceLength = 0;
            _dupenv_s(&workspaceValue, &workspaceLength, "TOMCAT_AUTOMATION_WORKSPACE");
            const std::string workspace = workspaceValue ? workspaceValue : "";
            free(workspaceValue);
            Require(!workspace.empty(), "WORKSPACE_REQUIRED", "Set TOMCAT_AUTOMATION_WORKSPACE when launching the Editor to enable project creation/open.");
            const auto path = BoundedPath(UTF8ToPath(workspace), Text(args, "path"));
            Require(path.extension() == ".tcproj", "INVALID_ARGUMENT", "Expected a .tcproj path relative to the automation workspace.");
            if (tool == "project_create")
            {
                ProjectConfig config;
                config.Name = Text(args, "name");
                config.Template = args["template"] ? Text(args, "template") : "3D";
                Require(config.Template == "2D" || config.Template == "3D", "INVALID_ARGUMENT", "template must be 2D or 3D.");
                Require(bool(Project::CreateNew(path, config)), "CREATE_FAILED", "Project creation failed; destination must be new or empty.");
            }
            Require(bool(Project::Inspect(path)), "OPEN_FAILED", "Project metadata is invalid.");
            OpenProject(path);
            std::error_code ec;
            Require(m_CurrentProject && std::filesystem::equivalent(path, m_CurrentProject->GetProjectPath(), ec) && !ec,
                "OPEN_FAILED", "Editor could not open the project; inspect console diagnostics.");
            ++m_AutomationSceneEpoch;
            data = Object({{"project", Quote(PathToUTF8(m_CurrentProject->GetProjectPath()))}});
        }
        else if (tool == "script_get_status" || tool == "script_compile")
        {
            Require(bool(m_CurrentProject), "PROJECT_REQUIRED", "Open a project first.");
            UpdateScriptCompilation(Timestep(0.0f));
            if (tool == "script_compile")
            {
                Require(m_SceneState == SceneState::Edit, "EDIT_MODE_REQUIRED", "Stop Play before compiling.");
                if (!m_ScriptCompiler.IsCompileInProgress())
                    Require(m_ScriptCompiler.StartCompile(true), "COMPILE_FAILED", "Could not start compilation; inspect console diagnostics.");
            }
            static const char* states[] = {"unconfigured", "dirty", "building", "succeeded", "failed"};
            data = Object({{"state", Quote(states[static_cast<unsigned>(m_ScriptCompiler.GetState())])},
                {"current", m_ScriptCompiler.IsCurrentSourceBuilt() ? "true" : "false"},
                {"building", m_ScriptCompiler.IsCompileInProgress() ? "true" : "false"},
                {"build_id", Quote(m_ScriptCompiler.GetLastGoodBuildID())}});
        }
        else if (tool == "scene_new" || tool == "scene_open" || tool == "scene_save_as")
        {
            Require(m_SceneState == SceneState::Edit, "EDIT_MODE_REQUIRED", "Stop Play first.");
            if (tool == "scene_new" || tool == "scene_open")
                Require(!IsSceneDirty(), "UNSAVED_CHANGES", "Save the current scene first.");
            if (tool == "scene_new") NewScene();
            else
            {
                const auto path = AssetPath(m_CurrentProject, Text(args, "path"));
                Require(path.extension() == ".tomcat", "INVALID_ARGUMENT", "Expected .tomcat scene path relative to Assets.");
                if (tool == "scene_open")
                {
                    const auto before = m_EditorScene;
                    OpenScene(path);
                    Require(before != m_EditorScene, "OPEN_FAILED", "Scene could not be opened; inspect diagnostics.");
                }
                else
                {
                    Require(!std::filesystem::exists(path) || path == m_EditorScenePath, "ALREADY_EXISTS", "Save As will not overwrite another asset.");
                    Require(SerializeScene(m_EditorScene, path), "SAVE_FAILED", "Could not save scene.");
                    m_EditorScenePath = path;
                    m_ContentBrowserPanel.SetActiveScenePath(path);
                    MarkCurrentSceneSaved();
                }
            }
            data = Object({{"path", Quote(PathToUTF8(m_EditorScenePath))}});
        }
        else if (tool == "scene_get_archive")
        {
            Require(bool(m_ActiveScene), "NO_SCENE", "Open a scene first.");
            std::string archive, error;
            Require(SceneArchiveCodec::Encode(m_ActiveScene, archive, error), "READ_FAILED", error);
            data = Object({{"document", Quote(archive)}});
        }
        else if (tool == "asset_list")
        {
            const int offset = args["offset"] ? args["offset"].as<int>() : 0;
            const int limit = args["limit"] ? args["limit"].as<int>() : 100;
            Require(offset >= 0 && limit > 0 && limit <= 500, "INVALID_ARGUMENT", "Invalid pagination.");
            std::vector<std::pair<std::string, std::string>> sorted;
            for (const auto& [handle, meta] : AssetManager::Get().GetRegistry().GetAssets())
                sorted.emplace_back(PathToUTF8(meta.FilePath), Object({{"handle", Quote(std::to_string(uint64_t(handle)))},
                    {"path", Quote(PathToUTF8(meta.FilePath))}, {"type", Number(static_cast<unsigned>(meta.Type))},
                    {"missing", meta.IsMissing ? "true" : "false"}}));
            std::sort(sorted.begin(), sorted.end());
            std::vector<std::string> assets;
            for (size_t i = offset; i < sorted.size() && assets.size() < size_t(limit); ++i) assets.push_back(sorted[i].second);
            data = Object({{"assets", Array(assets)}, {"total", std::to_string(sorted.size())}});
        }
        else if (tool == "asset_read_text" || tool == "asset_write_text" || tool == "asset_import")
        {
            const auto path = AssetPath(m_CurrentProject, Text(args, "path"));
            if (tool == "asset_read_text")
                data = Object({{"text", Quote(ReadAutomationText(path))}});
            else
            {
                Require(m_SceneState == SceneState::Edit, "EDIT_MODE_REQUIRED", "Stop Play before changing assets.");
                if (tool == "asset_write_text")
                {
                    // Sidecar identities and scene state must use their dedicated APIs.
                    const auto extension = PathToUTF8(path.extension());
                    static const std::set<std::string> extensions{ ".cs", ".glsl", ".json", ".txt", ".csv", ".tcanim", ".tccontroller", ".tctilepalette", ".tcmat", ".obj", ".vert", ".frag" };
                    Require(extensions.contains(extension), "INVALID_ARGUMENT", "Unsupported text asset extension.");
                    const auto text = Text(args, "text");
                    if (std::filesystem::exists(path))
                        Require(args["expected_text"] && ReadAutomationText(path) == Text(args, "expected_text"),
                            "ASSET_CHANGED", "Provide expected_text matching the existing source before replacing it.");
                    else Require(!args["expected_text"], "ASSET_CHANGED", "Asset no longer exists.");
                    std::string error;
                    Require(FileSystem::WriteFileAtomically(path, text, error), "WRITE_FAILED", error);
                }
                const auto handle = AssetManager::Get().ImportAsset(path);
                Require(uint64_t(handle) != 0, "IMPORT_FAILED", "Could not register asset.");
                data = Object({{"handle", Quote(std::to_string(uint64_t(handle)))}});
            }
        }
        else if (tool == "asset_set_import_settings" || tool == "asset_move" || tool == "asset_delete")
        {
            Require(m_SceneState == SceneState::Edit, "EDIT_MODE_REQUIRED", "Stop Play before changing assets.");
            const auto handle = AssetHandle(Id(args, "handle"));
            if (tool == "asset_move")
            {
                const bool currentScene = handle == GetSavedEditorSceneHandle();
                const auto destination = AssetPath(m_CurrentProject, Text(args, "path"));
                Require(AssetManager::Get().MoveAsset(handle, destination), "MOVE_FAILED", "Asset move rejected.");
                if (currentScene)
                {
                    m_EditorScenePath = destination;
                    m_ContentBrowserPanel.SetActiveScenePath(destination);
                }
            }
            else if (tool == "asset_delete")
            {
                Require(handle != GetSavedEditorSceneHandle(), "DELETE_FAILED", "Open a different scene before deleting the current scene asset.");
                Require(AssetManager::Get().DeleteAsset(handle, false), "DELETE_FAILED", "Asset missing, referenced, or deletion rejected.");
            }
            else
            {
                Require(args["settings"].IsMap(), "INVALID_ARGUMENT", "settings must be a string map.");
                AssetImportSettings settings;
                for (const auto& item : args["settings"]) settings[item.first.as<std::string>()] = item.second.as<std::string>();
                Require(AssetManager::Get().SetImportSettings(handle, settings), "IMPORT_FAILED", "Import settings rejected.");
            }
        }
        else if (tool == "build_get_settings" || tool == "build_set_scenes" || tool == "build_player")
        {
            Require(bool(m_CurrentProject), "PROJECT_REQUIRED", "Open a project first.");
            if (tool == "build_set_scenes")
            {
                Require(m_SceneState == SceneState::Edit, "EDIT_MODE_REQUIRED", "Stop Play first.");
                Require(args["scenes"].IsSequence() && args["scenes"].size() <= 256, "INVALID_ARGUMENT", "scenes must be an array of at most 256 objects.");
                BuildSettings settings;
                settings.EntrySceneHandle = AssetHandle(Id(args, "entry_scene"));
                for (const auto& item : args["scenes"])
                {
                    const auto handle = AssetHandle(Id(item, "handle"));
                    const auto* meta = AssetManager::Get().GetRegistry().GetMetadata(handle);
                    Require(meta && !meta->IsMissing && meta->Type == AssetType::Scene, "INVALID_ARGUMENT", "Build scene must be a live scene asset.");
                    settings.Scenes.push_back({handle, item["enabled"] ? item["enabled"].as<bool>() : true, meta->FilePath});
                }
                Require(m_CurrentProject->SetBuildSettings(settings), "SAVE_FAILED", "Build settings rejected.");
            }
            if (tool == "build_player")
            {
                Require(!m_EditorScenePath.empty(), "SCENE_PATH_REQUIRED", "Use scene_save_as before building.");
                m_BuildState.DevelopmentBuild = args["development"] && args["development"].as<bool>();
                Require(m_Build.BuildPlayer(false), "BUILD_FAILED", m_BuildState.PlayerBuildStatus);
                data = Object({{"status", Quote(m_BuildState.PlayerBuildStatus)}, {"output_directory", Quote(m_BuildState.PlayerOutputDirectory)}});
            }
            else
            {
                const auto& settings = m_CurrentProject->GetBuildSettings();
                std::vector<std::string> scenes;
                for (const auto& scene : settings.Scenes) scenes.push_back(Object({
                    {"handle", Quote(std::to_string(uint64_t(scene.Handle)))}, {"enabled", scene.Enabled ? "true" : "false"}, {"path", Quote(PathToUTF8(scene.PathHint))}}));
                data = Object({{"entry_scene", Quote(std::to_string(uint64_t(settings.EntrySceneHandle)))}, {"scenes", Array(scenes)}});
            }
        }
        else if (tool == "prefab_get_overrides")
        {
            std::vector<std::string> paths, values;
            std::string error;
            Require(PrefabLinkedInstance::GetOverridePaths(m_ActiveScene, Find(m_ActiveScene, args).GetUUID(), paths, error), "EDIT_REJECTED", error);
            for (const auto& path : paths) values.push_back(Quote(path));
            data = Object({{"paths", Array(values)}});
        }
        else if (tool == "prefab_action")
        {
            Require(m_SceneState == SceneState::Edit, "EDIT_MODE_REQUIRED", "Stop Play first.");
            const auto action = Text(args, "action");
            const int code = action == "apply" ? 2 : action == "unpack" ? 3 : action == "apply_property" ? 5 : action == "revert_property" ? 6 : -1;
            Require(code >= 0, "INVALID_ARGUMENT", "Unknown prefab action.");
            const UUID target(code >= 5 ? Id(args, "target_id") : 0);
            const UUID component(code >= 5 ? Id(args, "component_id") : 0);
            const UUID property(code >= 5 ? Id(args, "property_id") : 0);
            std::string error;
            Require(ApplyPrefabAction(Find(m_EditorScene, args), code, target, component, property, error), "EDIT_REJECTED", error);
        }
        else if (tool == "prefab_save")
        {
            Require(m_SceneState == SceneState::Edit, "EDIT_MODE_REQUIRED", "Stop Play first.");
            const auto path = AssetPath(m_CurrentProject, Text(args, "path"));
            Require(path.extension() == ".tcprefab" && !std::filesystem::exists(path), "INVALID_ARGUMENT", "Expected a new .tcprefab asset path.");
            AssetHandle handle{0};
            Require(PrefabArchiveCodec::SaveSubtree(m_EditorScene, Find(m_EditorScene, args), path, &handle), "SAVE_FAILED", "Could not save prefab.");
            data = Object({{"handle", Quote(std::to_string(uint64_t(handle)))}});
        }
        else if (tool == "profiler_set_recording")
        {
            m_AutomationCapturing = args["enabled"].as<bool>();
            FrameProfiler::Get().SetRecording(m_AutomationCapturing);
        }
        else if (tool == "profiler_get_frames")
        {
            std::vector<std::string> frames;
            for (const auto& frame : FrameProfiler::Get().Summaries()) frames.push_back(Object({
                {"id", Quote(std::to_string(frame.ID))}, {"cpu_ms", Number(frame.CpuMilliseconds)},
                {"gpu_ms", Number(frame.GpuMilliseconds)}, {"draw_calls", Number(frame.DrawCalls)},
                {"texture_bytes", Quote(std::to_string(frame.Resources.TextureBytes))}}));
            data = Object({{"frames", Array(frames)}});
        }
        else if (tool == "component_get_schema")
        {
            std::vector<std::string> components;
            for (const auto& descriptor : ComponentRegistry::Get().GetDescriptors())
            {
                const bool supported = static_cast<uint64_t>(descriptor.ProviderId) == 0;
                std::vector<std::string> properties;
                for (const auto& property : descriptor.Properties)
                    properties.push_back(Object({{"id", Quote(std::to_string(static_cast<uint64_t>(property.PropertyId)))},
                        {"name", Quote(property.StableName)}, {"kind", Quote(Kind(property.Kind))},
                        {"writable", supported && property.Set ? "true" : "false"}}));
                components.push_back(Object({{"id", Quote(std::to_string(static_cast<uint64_t>(descriptor.TypeId)))},
                    {"name", Quote(descriptor.StableName)}, {"addable", supported && descriptor.AddableInInspector ? "true" : "false"},
                    {"removable", supported && descriptor.Removable ? "true" : "false"}, {"properties", Array(properties)}}));
            }
            data = Object({{"components", Array(components)}});
        }
        else if (tool == "console_get_entries")
        {
            const auto after = args["after"] ? Id(args, "after") : 0;
            const int limit = args["limit"] ? args["limit"].as<int>() : 100;
            Require(limit > 0 && limit <= 500, "INVALID_ARGUMENT", "limit must be 1..500.");
            std::vector<std::string> entries;
            for (const auto& item : m_ConsolePanel.Snapshot())
                if (item.Sequence > after && entries.size() < static_cast<size_t>(limit))
                    entries.push_back(Object({{"sequence", Quote(std::to_string(item.Sequence))},
                        {"severity", std::to_string(static_cast<int>(item.Severity))}, {"source", Quote(item.Source)},
                        {"code", Quote(item.Code)}, {"message", Quote(item.Text)}, {"file", Quote(PathToUTF8(item.File))},
                        {"line", std::to_string(item.Line)}, {"stack", Quote(item.StackTrace)}}));
            data = Object({{"entries", Array(entries)}});
        }
        else
        {
            Require(bool(m_ActiveScene), "NO_SCENE", "Open a scene first.");
            if (tool == "entity_get") data = EntityInfo(m_ActiveScene, Find(m_ActiveScene, args), true);
            else if (tool == "scene_get_tree")
            {
                const int offset = args["offset"] ? args["offset"].as<int>() : 0;
                const int limit = args["limit"] ? args["limit"].as<int>() : 100;
                Require(offset >= 0 && limit > 0 && limit <= 500, "INVALID_ARGUMENT", "Invalid pagination.");
                auto ids = m_ActiveScene->GetRootEntityUUIDs();
                std::vector<std::string> entities;
                for (size_t i = 0; i < ids.size(); ++i)
                {
                    auto entity = m_ActiveScene->FindEntityByUUID(ids[i]);
                    auto children = m_ActiveScene->GetChildrenUUIDs(entity);
                    ids.insert(ids.end(), children.begin(), children.end());
                    if (i >= static_cast<size_t>(offset) && entities.size() < static_cast<size_t>(limit))
                        entities.push_back(EntityInfo(m_ActiveScene, entity, false));
                }
                data = Object({{"entities", Array(entities)}, {"total", std::to_string(ids.size())}, {"scene_version", Quote(revision())}});
            }
            else if (IsEdit(tool))
            {
                Require(m_SceneState == SceneState::Edit, "EDIT_MODE_REQUIRED", "Stop Play before editing the authoring scene.");
                Ref<Scene> staged;
                {
                    ComponentMutationPhaseScope phase(ComponentMutationPhase::Validation);
                    staged = Scene::Copy(m_EditorScene);
                    Require(bool(staged), "EDIT_REJECTED", "Could not stage scene.");
                    data = ::TomCat::Edit(staged, tool, args, m_ScriptMetadata);
                }
                // Publish a fully validated scene and one undo entry. No partial
                // property/structural mutation is ever applied to the live scene.
                const auto selected = m_SceneHierarchyPanel.GetSelectedEntity();
                const auto selectedId = selected ? selected.GetUUID() : UUID(0);
                std::string archive, error;
                Require(SceneArchiveCodec::Encode(staged, archive, error), "EDIT_REJECTED", error);
                Require(m_SceneHistory.GetCurrentSnapshot() != nullptr
                    && m_SceneHistory.BeginTransaction(tool), "HISTORY_UNAVAILABLE", "Scene history is not ready.");
                const bool changed = m_SceneHistory.CommitTransaction(std::move(archive),
                    staged->FindEntityByUUID(selectedId) ? static_cast<uint64_t>(selectedId) : 0);
                m_EditorScene = staged;
                m_ActiveScene = staged;
                ResizeSceneForGameView(staged);
                m_SceneHierarchyPanel.SetContext(staged);
                m_SceneHierarchyPanel.SetSelectedEntity(staged->FindEntityByUUID(selectedId));
                ResetSceneInteractionState();
                if (changed) ScheduleCurrentSceneAutosave();
                m_AutomationObservedScene = staged.get();
                ++m_AutomationSceneEpoch;
            }
            else if (tool == "editor_play")
            {
                Require(m_SceneState == SceneState::Edit, "INVALID_STATE", "Play requires Edit mode.");
                OnScenePlay();
                Require(IsSceneRunning(), "PLAY_FAILED", "Play was rejected; read console_get_entries for diagnostics.");
            }
            else if (tool == "editor_pause")
            {
                Require(IsSceneRunning(), "INVALID_STATE", "Pause requires Play mode.");
                if (m_SceneState == SceneState::Play) OnScenePause();
            }
            else if (tool == "editor_step")
            {
                Require(m_SceneState == SceneState::Pause, "INVALID_STATE", "Pause before stepping.");
                Require(!m_StepRequested, "EDITOR_BUSY", "A manual Step is already pending.");
                // OnRuntimeStep also renders. The automation safe point precedes
                // normal rendering, so explicitly use the Game render target.
                m_GameFramebuffer->Bind();
                try { m_ActiveScene->OnRuntimeStep(); }
                catch (...) { m_GameFramebuffer->Unbind(); throw; }
                m_GameFramebuffer->Unbind();
                CommitRuntimeSceneTransition();
                data = Object({{"advanced_seconds", Number(Scene::FixedRuntimeTimestep)}});
            }
            else if (tool == "editor_stop") OnSceneStop();
            else
            {
                Require(m_SceneState == SceneState::Edit, "EDIT_MODE_REQUIRED", "Stop Play first.");
                if (tool == "scene_save")
                {
                    Require(!m_EditorScenePath.empty(), "SCENE_PATH_REQUIRED", "Use Editor Save As once before automation saves this scene.");
                    Require(SerializeScene(m_EditorScene, m_EditorScenePath), "SAVE_FAILED", "Scene could not be saved.");
                    MarkCurrentSceneSaved();
                }
                else if (tool == "history_undo") Require(UndoScene(), "UNDO_UNAVAILABLE", "No undo entry available.");
                else if (tool == "history_redo") Require(RedoScene(), "REDO_UNAVAILABLE", "No redo entry available.");
            }
        }
        response = Object({{"ok", "true"}, {"data", data}});
    }
    catch (const Failure& failure) { response = Error(failure.Code, failure.what()); }
    catch (const YAML::Exception& failure) { response = Error("INVALID_ARGUMENT", failure.what()); }
    catch (const std::exception& failure) { response = Error("INTERNAL_ERROR", failure.what()); }
    if (remember)
    {
        m_AutomationSeenRequests.insert(requestId);
        m_AutomationResponses.emplace(requestId, std::make_pair(request, response));
        m_AutomationResponseOrder.push_back(requestId);
        if (m_AutomationResponseOrder.size() > 256)
        { m_AutomationResponses.erase(m_AutomationResponseOrder.front()); m_AutomationResponseOrder.pop_front(); }
    }
    return response;
}
}
