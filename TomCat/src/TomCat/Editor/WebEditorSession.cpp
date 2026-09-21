#include "tcpch.h"
#include "WebEditorSession.h"
#include "TomCat/Scene/ComponentRegistry.h"
#include "TomCat/Scene/Serialization/SceneArchiveCodec.h"
#include "TomCat/Asset/AssetManager.h"
#include "TomCat/Asset/SpriteAsset.h"
#include "TomCat/Project/Project.h"
#include "TomCat/Utils/PathUtils.h"
#include <yaml-cpp/yaml.h>
#include <charconv>
#include <cmath>
#include <fstream>
#include <limits>
#include <set>
#include <iomanip>
#include <locale>
#include <sstream>

namespace TomCat {
namespace {
struct RpcError : std::runtime_error {
  std::string Code;
  RpcError(std::string code, std::string message) : std::runtime_error(message), Code(std::move(code)) {}
};
void Require(bool condition, const std::string& message, const char* code = "INVALID_REQUEST") {
  if (!condition) throw RpcError(code, message);
}
std::string Quote(const std::string& value) {
  std::string out = "\"";
  constexpr char hex[] = "0123456789abcdef";
  for (unsigned char c : value) {
    if (c == '"' || c == '\\') { out += '\\'; out += c; }
    else if (c < 32) { out += "\\u00"; out += hex[c >> 4]; out += hex[c & 15]; }
    else out += c;
  }
  return out + '"';
}
std::string Array(const std::vector<std::string>& values) {
  std::string out = "[";
  for (const auto& value : values) { if (out.size() > 1) out += ','; out += value; }
  return out + ']';
}
std::string Object(std::initializer_list<std::pair<std::string, std::string>> fields) {
  std::string out = "{";
  for (const auto& [key, value] : fields) { if (out.size() > 1) out += ','; out += Quote(key) + ':' + value; }
  return out + '}';
}
std::string Bool(bool value) { return value ? "true" : "false"; }
template<class T> std::string NumericJson(T value) {
  Require(std::isfinite(value), "Scene contains a non-finite property", "INVALID_SCENE");
  std::ostringstream stream; stream.imbue(std::locale::classic());
  stream << std::setprecision(std::numeric_limits<T>::max_digits10) << value; return stream.str();
}
std::string Id(uint64_t value) { return Quote(std::to_string(value)); }
std::string String(const YAML::Node& node) {
  Require(node.IsScalar() && node.Tag() == "!", "Expected a JSON string");
  return node.Scalar();
}
uint64_t ParseId(const std::string& value) {
  uint64_t id = 0;
  const auto [end, error] = std::from_chars(value.data(), value.data() + value.size(), id);
  Require(!value.empty() && (value == "0" || value[0] != '0') && error == std::errc{} && end == value.data() + value.size(), "Invalid uint64 decimal string");
  return id;
}
uint64_t ReadId(const YAML::Node& node) { return ParseId(String(node)); }
double Number(const YAML::Node& node) {
  Require(node.IsScalar() && node.Tag() != "!", "Expected a JSON number");
  const double value = node.as<double>();
  Require(std::isfinite(value), "Number must be finite"); return value;
}
template<class T> T Integer(const YAML::Node& node) {
  double value = Number(node);
  Require(std::trunc(value) == value && value >= double(std::numeric_limits<T>::lowest()) && value <= double(std::numeric_limits<T>::max()), "Integer out of range");
  return static_cast<T>(value);
}
void ValidateTree(const YAML::Node& node, int depth, size_t& count) {
  Require(depth <= 32 && ++count <= 50000, "Request nesting or size limit exceeded");
  if (node.IsMap()) {
    std::set<std::string> keys;
    for (auto item : node) { Require(item.first.IsScalar() && keys.insert(item.first.Scalar()).second, "Duplicate or invalid key"); ValidateTree(item.second, depth + 1, count); }
  } else if (node.IsSequence()) for (auto child : node) ValidateTree(child, depth + 1, count);
}
std::string Encode(const Ref<Scene>& scene) {
  std::string archive, error;
  Require(SceneArchiveCodec::Encode(scene, archive, error), error, "SERIALIZE_FAILED");
  return archive;
}
Ref<Scene> Decode(const std::string& archive) {
  Require(archive.size() <= 4 * 1024 * 1024, "Scene archive exceeds 4 MiB");
  auto scene = CreateRef<Scene>();
  Require(SceneArchiveCodec::Decode({archive.begin(), archive.end()}, scene, "WebScene.tomcat", false), "Invalid scene archive", "INVALID_SCENE");
  return scene;
}
std::vector<Entity> Entities(const Ref<Scene>& scene) {
  std::vector<Entity> result;
  auto pending = scene->GetRootEntityUUIDs();
  for (size_t i = 0; i < pending.size(); ++i) {
    Require(i < 10000, "Scene exceeds 10000 entities");
    auto entity = scene->FindEntityByUUID(pending[i]);
    if (!entity) continue;
    result.push_back(entity);
    auto children = scene->GetChildrenUUIDs(entity); pending.insert(pending.end(), children.begin(), children.end());
  }
  return result;
}
std::string ValueJson(const PropertyValue& value) {
  return std::visit([](const auto& item) -> std::string {
    using T = std::decay_t<decltype(item)>;
    if constexpr (std::is_same_v<T, std::string>) return Quote(item);
    else if constexpr (std::is_same_v<T, bool>) return Bool(item);
    else if constexpr (std::is_same_v<T, uint64_t> || std::is_same_v<T, int64_t>) return Quote(std::to_string(item));
    else if constexpr (std::is_floating_point_v<T>) return NumericJson(item);
    else if constexpr (std::is_arithmetic_v<T>) return std::to_string(item);
    else { std::vector<std::string> values; for (int i = 0; i < item.length(); ++i) values.push_back(NumericJson(item[i])); return Array(values); }
  }, value);
}
PropertyValue ReadValue(const YAML::Node& node, PropertyKind kind) {
  switch (kind) {
    case PropertyKind::Bool: Require(node.IsScalar() && node.Tag() != "!" && (node.Scalar() == "true" || node.Scalar() == "false"), "Expected boolean"); return node.as<bool>();
    case PropertyKind::Int32: return Integer<int32_t>(node);
    case PropertyKind::UInt32: return Integer<uint32_t>(node);
    case PropertyKind::UInt64: return ReadId(node);
    case PropertyKind::Int64: {
      auto text = String(node); int64_t value{}; auto [end, ec] = std::from_chars(text.data(), text.data() + text.size(), value);
      Require(ec == std::errc{} && end == text.data() + text.size(), "Invalid int64 string"); return value;
    }
    case PropertyKind::Float: { double value = Number(node); Require(std::abs(value) <= std::numeric_limits<float>::max(), "Float out of range"); return float(value); }
    case PropertyKind::Double: return Number(node);
    case PropertyKind::String: { auto value = String(node); Require(value.size() <= 65536, "String too long"); return value; }
    default: {
      const size_t count = kind == PropertyKind::Vector2 ? 2 : kind == PropertyKind::Vector3 ? 3 : 4;
      Require(node.IsSequence() && node.size() == count, "Invalid vector dimension");
      glm::vec4 value(0); for (size_t i = 0; i < count; ++i) value[i] = std::get<float>(ReadValue(node[i], PropertyKind::Float));
      if (count == 2) return glm::vec2(value); if (count == 3) return glm::vec3(value); return value;
    }
  }
}
void Apply(const Ref<Scene>& scene, const YAML::Node& operation, uint64_t& selected) {
  const auto op = String(operation["op"]);
  const uint64_t id = ReadId(operation["entityId"]);
  Require(id != 0, "Entity ID cannot be zero");
  auto entity = scene->FindEntityByUUID(UUID(id));
  if (op == "entity.create") {
    Require(!entity, "Entity ID already exists", "ENTITY_EXISTS");
    auto name = String(operation["name"]); Require(!name.empty() && name.size() <= 256, "Invalid entity name");
    entity = scene->CreateEntityWithUUID(UUID(id), name);
    if (operation["parentId"] && !operation["parentId"].IsNull()) {
      auto parent = scene->FindEntityByUUID(UUID(ReadId(operation["parentId"])));
      Require(bool(parent) && scene->SetParent(entity, parent), "Invalid parent");
    }
    selected = id; return;
  }
  Require(bool(entity), "Entity not found", "ENTITY_NOT_FOUND");
  if (op == "entity.rename") {
    auto name = String(operation["name"]); Require(!name.empty() && name.size() <= 256, "Invalid entity name");
    entity.GetComponent<Tag>()._Tag = name; return;
  }
  if (op == "entity.delete") { scene->DestroyEntity(entity); if (!scene->FindEntityByUUID(UUID(selected))) selected = 0; return; }
  if (op == "entity.set-parent") {
    auto node = operation["parentId"]; Require(bool(node), "parentId required");
    Entity parent;
    if (!node.IsNull()) { parent = scene->FindEntityByUUID(UUID(ReadId(node))); Require(bool(parent), "Parent not found"); }
    Require(scene->SetParent(entity, parent), "Cannot create cyclic or invalid hierarchy"); return;
  }
  auto& registry = ComponentRegistry::Get();
  const auto* descriptor = registry.Find(UUID(ReadId(operation["componentId"])));
  Require(descriptor && uint64_t(descriptor->ProviderId) == 0, "Component is unavailable for Web authoring", "COMPONENT_UNAVAILABLE");
  std::string error;
  if (op == "component.add") {
    Require(descriptor->AddableInInspector && !registry.Has(entity, descriptor->TypeId), "Component cannot be added");
    Require(registry.Add(entity, descriptor->TypeId, error), error); return;
  }
  if (op == "component.remove") {
    Require(descriptor->Removable && registry.Has(entity, descriptor->TypeId), "Component cannot be removed");
    Require(registry.Remove(entity, descriptor->TypeId, error), error); return;
  }
  Require(op == "component.patch", "Unknown editor operation");
  Require(registry.Has(entity, descriptor->TypeId), "Component not present");
  auto properties = operation["properties"]; Require(properties.IsMap() && properties.size() > 0, "Properties required");
  for (auto field : properties) {
    const auto propertyId = ParseId(field.first.Scalar());
    auto it = std::find_if(descriptor->Properties.begin(), descriptor->Properties.end(), [&](const auto& property) { return uint64_t(property.PropertyId) == propertyId; });
    Require(it != descriptor->Properties.end() && bool(it->Set), "Property is unknown or read-only", "PROPERTY_READ_ONLY");
    auto value = ReadValue(field.second, it->Kind);
    if (it->AssetReference) {
      const auto handle = std::get<uint64_t>(value);
      if (handle) {
        const auto& assets = AssetManager::Get().GetRegistry();
        const auto* sub = assets.GetSubAsset(UUID(handle));
        const auto* asset = sub ? assets.GetSubAssetOwner(UUID(handle)) : assets.GetMetadata(UUID(handle));
        Require(FindBuiltInSpriteAsset(UUID(handle)) ? it->AssetReference->Accepts(AssetType::Texture2D,false) : (asset && !asset->IsMissing && it->AssetReference->Accepts(sub ? sub->Type : asset->Type, sub != nullptr)), "Asset reference has the wrong type or is missing", "INVALID_ASSET");
      }
    }
    if (it->EntityReference) { auto target = std::get<uint64_t>(value); Require(!target || bool(scene->FindEntityByUUID(UUID(target))), "Referenced entity not found"); }
    Require(it->Set(entity, value, error), error.empty() ? "Property rejected by engine" : error, "PROPERTY_REJECTED");
    if (uint64_t(descriptor->TypeId) == ComponentIds::Transform) {
      const auto& transform = entity.GetComponent<Transform>();
      Require(propertyId <= 3 ? scene->SetWorldTransform(entity, transform.GetTransform())
        : scene->SetLocalTransform(entity, transform.GetLocalTransform()), "Transform cannot be applied to hierarchy", "PROPERTY_REJECTED");
    }
  }
}
}

void WebEditorSession::StopPreview() {
  m_Preview.Stop(); m_Preview.DeactivateRuntime(); m_PreviewMode = PreviewMode::Edit;
}
void WebEditorSession::ControlPreview(const std::string& command) {
  Require(bool(m_Scene), "Open a project first", "NO_PROJECT");
  if (command == "stop") { StopPreview(); return; }
  if (command == "play") {
    Require(m_PreviewMode == PreviewMode::Edit, "Preview already running", "PREVIEW_STATE");
    EndUIEdit(m_Selected);
    for (const auto entity : Entities(m_Scene))
      Require(!entity.HasComponent<CSharpScripts>() || entity.GetComponent<CSharpScripts>().Scripts.empty(),
        "C# browser execution is not available. Stop preview and use a native-only scene.", "SCRIPT_UNSUPPORTED");
    Require(m_Scene->HasAuthoredPrimaryCamera(), "Add a Primary Camera before Play", "CAMERA_REQUIRED");
    BuildSettings settings; settings.EntrySceneHandle = UUID(m_SceneHandle);
    settings.Scenes.push_back({UUID(m_SceneHandle),true,{}});
    try {
      Require(m_Preview.ConfigureBuildSettings(settings), m_Preview.GetLastError());
      Require(m_Preview.ActivateRuntime(), m_Preview.GetLastError());
      auto copy = Scene::Copy(m_Scene);
      Require(copy && m_Preview.StartPreparedScene(copy,UUID(m_SceneHandle)), "Preview startup failed: " + m_Preview.GetLastError());
      m_PreviewMode = PreviewMode::Play; m_PreviewFrames = 0;
    } catch (...) { StopPreview(); throw; }
    return;
  }
  Require(m_PreviewMode != PreviewMode::Edit, "Start Play first", "PREVIEW_STATE");
  if (command == "pause") m_PreviewMode = PreviewMode::Pause;
  else if (command == "resume") m_PreviewMode = PreviewMode::Play;
  else if (command == "step") {
    Require(m_PreviewMode == PreviewMode::Pause, "Pause before stepping", "PREVIEW_STATE");
    m_Preview.GetActiveScene()->OnRuntimeStep(false); ++m_PreviewFrames;
  } else throw RpcError("INVALID_REQUEST", "Unknown preview command");
}
void WebEditorSession::AdvancePreview(float delta) {
  if (m_PreviewMode != PreviewMode::Play) return;
  m_Preview.GetActiveScene()->OnUpdateRuntime(std::clamp(delta,0.0f,0.1f),false); ++m_PreviewFrames;
}
bool WebEditorSession::SettingsDirty() const {
  return m_Project && (m_Project->GetSettings()!=m_SavedSettings || m_Project->GetPlayerSettings()!=m_SavedPlayerSettings);
}
std::string WebEditorSession::Status() const {
  return Object({{"mode",Quote(m_PreviewMode == PreviewMode::Edit ? "edit" : m_PreviewMode == PreviewMode::Play ? "play" : "pause")}, {"previewFrames",std::to_string(m_PreviewFrames)}, {"sceneHandle", Id(m_SceneHandle)}, {"revision", std::to_string(m_Revision)}, {"selectedEntityId", m_Selected ? Id(m_Selected) : "null"}, {"dirty", Bool(m_History.IsDirty() || m_History.HasActiveTransaction() || SettingsDirty())}, {"canUndo", Bool(m_History.CanUndo())}, {"canRedo", Bool(m_History.CanRedo())}});
}
void WebEditorSession::SelectFromUI(uint64_t entity) {
  if (!m_Scene || (entity && !m_Scene->FindEntityByUUID(UUID(entity)))) return;
  m_Selected = entity; m_History.SetCurrentSelection(entity);
}
void WebEditorSession::BeginUIEdit() {
  if (m_Scene && !m_History.HasActiveTransaction()) m_History.BeginTransaction("ImGui edit");
}
void WebEditorSession::EndUIEdit(uint64_t selection, bool cancel) {
  if (!m_Scene || !m_History.HasActiveTransaction()) return;
  if (cancel) {
    const auto* previous = m_History.GetCurrentSnapshot();
    if (previous && previous->Archive) { m_Scene = Decode(*previous->Archive); m_Selected = previous->SelectedEntity; ++m_Revision; }
    m_History.CancelTransaction(); return;
  }
  Require(m_Scene->SyncTransformHierarchy(), "Invalid transform hierarchy");
  if (!m_Scene->FindEntityByUUID(UUID(selection))) selection = 0;
  if (m_History.CommitTransaction(Encode(m_Scene), selection)) ++m_Revision;
  m_Selected = selection;
}
std::string WebEditorSession::HistoryFromUI(bool redo) {
  EndUIEdit(m_Selected);
  return Invoke(Object({{"protocol",Quote("tomcat.web.v1")},{"requestId",Quote("imgui-history")},{"type",Quote(redo ? "history.redo" : "history.undo")},{"payload",Object({{"sceneHandle",Id(m_SceneHandle)},{"baseRevision",std::to_string(m_Revision)}})}}));
}
std::string WebEditorSession::OpenSceneAsset(uint64_t handle) {
  const auto* metadata = AssetManager::Get().GetRegistry().GetMetadata(UUID(handle));
  Require(metadata && metadata->Type == AssetType::Scene, "Expected Scene asset");
  std::ifstream file(AssetManager::Get().GetRegistry().GetFileSystemPath(UUID(handle)),std::ios::binary);
  Require(bool(file), "Scene asset missing");
  std::string archive((std::istreambuf_iterator<char>(file)),{});
  EndUIEdit(m_Selected);
  return Invoke(Object({{"protocol",Quote("tomcat.web.v1")},{"requestId",Quote("imgui-open")},{"type",Quote("scene.loadArchive")},{"payload",Object({{"sceneHandle",Id(m_SceneHandle)},{"baseRevision",std::to_string(m_Revision)},{"archive",Quote(archive)}})}}));
}

std::string WebEditorSession::Snapshot() const {
  std::vector<std::string> entities, schemas;
  for (const auto& descriptor : ComponentRegistry::Get().GetDescriptors()) {
    if (!descriptor.InspectorVisible || uint64_t(descriptor.ProviderId) != 0) continue;
    std::vector<std::string> properties;
    for (const auto& property : descriptor.Properties) {
      std::vector<std::string> accepted;
      if (property.AssetReference) for (auto type : property.AssetReference->AcceptedTypes) accepted.push_back(Quote(AssetTypeToString(type)));
      properties.push_back(Object({{"id", Id(property.PropertyId)}, {"name", Quote(property.StableName)}, {"label", Quote(property.DisplayName)}, {"kind", std::to_string(uint32_t(property.Kind))}, {"writable", Bool(bool(property.Set))}, {"assetTypes", Array(accepted)}, {"entityReference", Bool(property.EntityReference)}}));
    }
    schemas.push_back(Object({{"id", Id(descriptor.TypeId)}, {"name", Quote(descriptor.StableName)}, {"label", Quote(descriptor.DisplayName)}, {"addable", Bool(descriptor.AddableInInspector)}, {"removable", Bool(descriptor.Removable)}, {"properties", Array(properties)}}));
  }
  for (auto entity : Entities(m_Scene)) {
    std::vector<std::string> components;
    for (const auto& descriptor : ComponentRegistry::Get().GetDescriptors()) {
      if (!descriptor.Has || !descriptor.Has(entity) || !descriptor.InspectorVisible) continue;
      std::string properties = "{";
      for (const auto& property : descriptor.Properties) if (property.Get) {
        if (properties.size() > 1) properties += ',';
        properties += Id(property.PropertyId) + ':' + ValueJson(property.Get(entity));
      }
      components.push_back(Object({{"id", Id(descriptor.TypeId)}, {"values", properties + '}'}}));
    }
    auto parent = m_Scene->GetParent(entity);
    entities.push_back(Object({{"id", Id(entity.GetUUID())}, {"name", Quote(entity.GetName())}, {"parentId", parent ? Id(parent.GetUUID()) : "null"}, {"components", Array(components)}}));
  }
  return Object({{"sceneHandle", Id(m_SceneHandle)}, {"name", Quote(m_Scene->GetSceneName())}, {"revision", std::to_string(m_Revision)}, {"selectedEntityId", m_Selected ? Id(m_Selected) : "null"}, {"archive", Quote(Encode(m_Scene))}, {"dirty", Bool(m_History.IsDirty() || SettingsDirty())}, {"canUndo", Bool(m_History.CanUndo())}, {"canRedo", Bool(m_History.CanRedo())}, {"entities", Array(entities)}, {"schemas", Array(schemas)}});
}

std::string WebEditorSession::Invoke(const std::string& request) {
  std::string requestId;
  const bool existingTransaction = m_History.HasActiveTransaction();
  try {
    Require(request.size() <= 8 * 1024 * 1024, "Request exceeds 8 MiB");
    auto root = YAML::Load(request); size_t count = 0; ValidateTree(root, 0, count);
    Require(root.IsMap(), "Expected request object"); requestId = String(root["requestId"]);
    Require(String(root["protocol"]) == "tomcat.web.v1", "Unsupported protocol");
    auto type = String(root["type"]); auto payload = root["payload"]; Require(payload.IsMap(), "Expected payload object");
    std::string result;
    if (type == "system.capabilities") {
      result = Object({{"engineBuildId", Quote("tomcat-web-editor-v1")}, {"protocolVersion", "1"}, {"capabilities", Array({Quote("scene.transact"), Quote("history.undo"), Quote("history.redo"), Quote("component.schema"), Quote("asset.list"), Quote("scene.archive"), Quote("preview.control"), Quote("preview.snapshot")})}});
    } else if (type == "preview.control") {
      ControlPreview(String(payload["command"])); result = Status();
    } else if (type == "preview.snapshot") {
      Require(bool(GetPreviewScene()), "Start Play first", "PREVIEW_STATE");
      result = Object({{"archive",Quote(Encode(GetPreviewScene()))},{"frames",std::to_string(m_PreviewFrames)}});
    } else if (type == "project.open" || type == "project.new") {
      Require(m_PreviewMode == PreviewMode::Edit, "Stop Play before opening a project", "PREVIEW_ACTIVE");
      const std::string path = type == "project.open" ? String(payload["projectPath"]) : "/Samples/PhysicsPlayground/Project.tcproj";
      Require(path == "/Samples/PhysicsPlayground/Project.tcproj", "This host mounts only the bundled project; import a scene archive for other scenes", "PROJECT_UNAVAILABLE");
      auto project = Project::Load(path); Require(bool(project), "Project could not be loaded");
      Require(AssetManager::Get().SetProject(project), "Project assets could not be initialized");
      Ref<Scene> scene;
      uint64_t handle;
      if (type == "project.open") {
        handle = project->GetBuildSettings().EntrySceneHandle;
        auto scenePath = AssetManager::Get().GetRegistry().GetFileSystemPath(UUID(handle));
        std::ifstream file(scenePath, std::ios::binary); Require(bool(file), "Entry scene missing");
        std::string archive((std::istreambuf_iterator<char>(file)), {}); scene = Decode(archive);
      } else {
        scene = CreateRef<Scene>(); scene->SetSceneName(String(payload["name"])); handle = UUID();
        scene->CreateEntity("Main Camera").AddComponent<C_Camera>();
        if (payload["template"] && String(payload["template"]) == "2D") scene->CreateEntity("Player").AddComponent<SpriteRenderer>();
      }
      m_Project = project; m_SavedSettings=project->GetSettings(); m_SavedPlayerSettings=project->GetPlayerSettings(); m_Scene = scene; m_SceneHandle = handle; m_Selected = 0;
      m_History.Reset(Encode(scene), 0, true); ++m_Revision; result = Snapshot();
    } else {
      Require(bool(m_Scene), "Open a project first", "NO_PROJECT");
      if (type == "asset.import") {
        Require(m_PreviewMode == PreviewMode::Edit, "Stop Play before importing assets", "PREVIEW_ACTIVE");
        auto name = String(payload["name"]);
        Require(!name.empty() && name.size() <= 160 && std::all_of(name.begin(), name.end(), [](unsigned char c) { return std::isalnum(c) || c == '-' || c == '_' || c == '.'; }), "Invalid asset file name");
        auto relative = std::filesystem::path("WebImports") / name;
        auto extension = relative.extension().string();
        Require(extension == ".png" || extension == ".jpg" || extension == ".jpeg" || extension == ".tga", "Web image import supports PNG, JPEG and TGA");
        auto path = m_Project->GetAssetPath() / relative;
        Require(std::filesystem::is_regular_file(path) && std::filesystem::file_size(path) <= 2 * 1024 * 1024, "Image missing or exceeds 2 MiB");
        Require(AssetManager::Get().GetRegistry().GetMetadata(path) == nullptr, "Asset already imported");
        auto handle = AssetManager::Get().ImportAsset(path);
        Require(uint64_t(handle) != 0, "Engine asset import failed", "IMPORT_FAILED");
        auto loaded = AssetManager::Get().LoadImportedArtifact(handle);
        if (loaded.Status != AssetLoadStatus::Success) {
          AssetManager::Get().DeleteAsset(path, true);
          throw RpcError("IMPORT_FAILED", "Image decoding failed");
        }
        result = Object({{"handle", Id(handle)}});
      } else if (type == "asset.list") {
        std::vector<std::string> assets;
        for (const auto& [handle, metadata] : AssetManager::Get().GetRegistry().GetAssets()) {
          if (metadata.IsMissing) continue;
          assets.push_back(Object({{"handle", Id(handle)}, {"type", Quote(AssetTypeToString(metadata.Type))}, {"pathHint", Quote(PathToUTF8(metadata.FilePath))}}));
          for (const auto& sub : metadata.SubAssets) assets.push_back(Object({{"handle", Id(sub.Handle)}, {"type", Quote(AssetTypeToString(sub.Type))}, {"pathHint", Quote(PathToUTF8(metadata.FilePath) + "#" + sub.Name)}}));
        }
        for (const auto& asset : GetBuiltInSpriteAssets())
          assets.push_back(Object({{"handle",Id(asset.Handle)},{"type",Quote("Texture2D")},{"pathHint",Quote(PathToUTF8(GetBuiltInSpriteAssetPath(asset.Handle)))}}));
        std::sort(assets.begin(), assets.end()); result = Object({{"assets", Array(assets)}});
      } else {
        Require(ReadId(payload["sceneHandle"]) == m_SceneHandle, "Scene handle does not match", "SCENE_NOT_FOUND");
        if (type == "scene.snapshot") {
          Require(!m_History.HasActiveTransaction(), "Finish the active ImGui edit first", "EDIT_IN_PROGRESS");
          result = Snapshot();
        }
        else if (type == "scene.select") {
          const auto selected = payload["entityId"].IsNull() ? 0 : ReadId(payload["entityId"]);
          Require(selected == 0 || bool(m_Scene->FindEntityByUUID(UUID(selected))), "Selected entity missing");
          m_Selected = selected; m_History.SetCurrentSelection(selected); result = Snapshot();
        }
        else {
          Require(!m_History.HasActiveTransaction(), "Finish the active ImGui edit first", "EDIT_IN_PROGRESS");
          Require(m_PreviewMode == PreviewMode::Edit || type == "scene.markSaved", "Stop Play before editing the scene", "PREVIEW_ACTIVE");
          const double base = Number(payload["baseRevision"]);
          Require(base >= 0 && std::trunc(base) == base && base <= 9007199254740991.0, "Invalid revision");
          Require(uint64_t(base) == m_Revision, "Scene changed; reload the latest snapshot", "REVISION_CONFLICT");
          if (type == "scene.transact" || type == "scene.loadArchive") {
            auto label = type == "scene.transact" ? String(payload["label"]) : "Import scene";
            Require(!label.empty() && label.size() <= 256, "Transaction label required");
            auto candidate = type == "scene.loadArchive" ? Decode(String(payload["archive"])) : Decode(Encode(m_Scene));
            uint64_t selected = m_Selected;
            if (type == "scene.transact") {
              auto operations = payload["operations"]; Require(operations.IsSequence() && operations.size() > 0 && operations.size() <= 128, "Transaction needs 1-128 operations");
              ComponentMutationPhaseScope phase(ComponentMutationPhase::Validation);
              for (auto operation : operations) Apply(candidate, operation, selected);
            }
            Require(candidate->SyncTransformHierarchy(), "Invalid transform hierarchy");
            (void)Entities(candidate);
            if (!candidate->FindEntityByUUID(UUID(selected))) selected = 0;
            auto archive = Encode(candidate);
            Require(m_History.BeginTransaction(label), "History transaction already active");
            if (m_History.CommitTransaction(archive, selected)) { m_Scene = candidate; m_Selected = selected; ++m_Revision; }
          } else if (type == "history.undo" || type == "history.redo") {
            auto restore = [&](const SceneHistory::Snapshot& snapshot) { auto scene = Decode(*snapshot.Archive); m_Scene = scene; m_Selected = snapshot.SelectedEntity; return true; };
            bool changed = type == "history.undo" ? m_History.Undo(restore) : m_History.Redo(restore);
            Require(changed, "No history available", "HISTORY_EMPTY"); ++m_Revision;
          } else if (type == "scene.markSaved") { m_History.MarkSaved(); m_SavedSettings=m_Project->GetSettings(); m_SavedPlayerSettings=m_Project->GetPlayerSettings(); }
          else throw RpcError("UNKNOWN_COMMAND", "Unknown editor command");
          result = Snapshot();
        }
      }
    }
    return Object({{"protocol", Quote("tomcat.web.v1")}, {"requestId", Quote(requestId)}, {"ok", "true"}, {"result", result}});
  } catch (const std::exception& error) {
    if (!existingTransaction) m_History.CancelTransaction();
    const auto* rpc = dynamic_cast<const RpcError*>(&error);
    return Object({{"protocol", Quote("tomcat.web.v1")}, {"requestId", Quote(requestId)}, {"ok", "false"}, {"error", Object({{"code", Quote(rpc ? rpc->Code : "INVALID_REQUEST")}, {"message", Quote(error.what())}})}});
  }
}
}
