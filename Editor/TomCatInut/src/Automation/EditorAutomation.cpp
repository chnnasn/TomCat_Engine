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
    bool IsEdit(const std::string& tool)
    {
        return tool == "entity_create" || tool == "entity_delete" || tool == "entity_reparent"
            || tool == "component_add" || tool == "component_remove" || tool == "component_set";
    }
    const std::map<std::string, std::set<std::string>> Arguments{
        {"editor_get_status", {}}, {"scene_get_tree", {"offset", "limit"}}, {"entity_get", {"entity_id"}},
        {"component_get_schema", {}}, {"console_get_entries", {"after", "limit"}},
        {"entity_create", {"name"}}, {"entity_delete", {"entity_id"}}, {"entity_reparent", {"entity_id", "parent_id"}},
        {"component_add", {"entity_id", "component_id"}}, {"component_remove", {"entity_id", "component_id"}},
        {"component_set", {"entity_id", "component_id", "property_id", "value"}},
        {"editor_play", {}}, {"editor_pause", {}}, {"editor_step", {}}, {"editor_stop", {}},
        {"scene_save", {}}, {"history_undo", {}}, {"history_redo", {}}
    };
    std::string Edit(const Ref<Scene>& scene, const std::string& tool, const YAML::Node& args)
    {
        std::string error;
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
        Require(static_cast<uint64_t>(descriptor.ProviderId) == 0 && descriptor.ScriptAccessible,
            "UNSUPPORTED_COMPONENT", "V1 edits engine-owned components with transactional validation only.");
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
        const bool readOnly = tool == "editor_get_status" || tool == "scene_get_tree" || tool == "entity_get"
            || tool == "component_get_schema" || tool == "console_get_entries";
        if (m_AutomationObservedScene != m_ActiveScene.get())
        { m_AutomationObservedScene = m_ActiveScene.get(); ++m_AutomationSceneEpoch; }
        auto revision = [&]() { return std::to_string(m_AutomationSceneEpoch) + ":" + std::to_string(m_SceneHistory.GetCurrentStateId()); };
        const auto project = m_CurrentProject ? PathToUTF8(m_CurrentProject->GetProjectPath()) : "";
        if (tool != "editor_get_status")
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
                && !m_OpenRecoveryModal && !m_PendingProjectMigration && !m_PendingProjectMigrationRecovery,
                "EDITOR_BUSY", "Complete the active editor interaction or dialog first.");
        }
        std::string data = "{}";
        if (tool == "editor_get_status")
            data = Object({{"session_id", Quote(m_AutomationSession)}, {"project", Quote(project)},
                {"scene_path", Quote(PathToUTF8(m_EditorScenePath))}, {"scene_version", Quote(revision())},
                {"state", Quote(m_SceneState == Edit ? "edit" : m_SceneState == Play ? "play" : "pause")},
                {"dirty", IsSceneDirty() ? "true" : "false"}, {"protocol_version", "1"}});
        else if (tool == "component_get_schema")
        {
            std::vector<std::string> components;
            for (const auto& descriptor : ComponentRegistry::Get().GetDescriptors())
            {
                const bool supported = static_cast<uint64_t>(descriptor.ProviderId) == 0 && descriptor.ScriptAccessible;
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
                    data = ::TomCat::Edit(staged, tool, args);
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
