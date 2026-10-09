#pragma once
#include "TomCat/Core/Base.h"
#include "TomCat/Editor/SceneHistory.h"
#include "TomCat/Scene/SceneManager.h"
#include "TomCat/Project/PlayerSettings.h"
#include <string>
#include <filesystem>
#include <span>
#include <vector>

namespace TomCat {
class Scene;
class Project;
// Authoring scene and history remain separate from the disposable Play scene.
class WebEditorSession final {
public:
  enum class PreviewMode { Edit, Play, Pause };
  ~WebEditorSession() { StopPreview(); }
  std::string Invoke(const std::string& request);
  Ref<Scene> GetScene() const { return m_Scene; }
  Ref<Project> GetProject() const { return m_Project; }
  uint64_t GetSelection() const { return m_Selected; }
  std::string Status() const;
  void SelectFromUI(uint64_t entity);
  void BeginUIEdit();
  bool HasUIEdit() const { return m_History.HasActiveTransaction(); }
  void EndUIEdit(uint64_t selection, bool cancel = false);
  std::string HistoryFromUI(bool redo);
  std::string OpenSceneAsset(uint64_t handle, bool discardDetached = false);
  std::filesystem::path ActiveScenePath() const;
  void SyncSceneAssetName();
  void PersistActiveScene();
  PreviewMode GetPreviewMode() const { return m_PreviewMode; }
  Ref<Scene> GetPreviewScene() const { return m_Preview.GetActiveScene(); }
  void ControlPreview(const std::string& command);
  void AdvancePreview(float delta);
  void StopPreview();
  bool SetManagedAssembly(std::span<const uint8_t> assembly,
    std::span<const uint8_t> pdb, std::string& error);
private:
  std::string Snapshot();
  bool SettingsDirty() const;
  ProjectSettings m_SavedSettings;
  PlayerSettings m_SavedPlayerSettings;
  Ref<Scene> m_Scene;
  Ref<Project> m_Project;
  SceneHistory m_History;
  uint64_t m_SceneHandle = 0;
  uint64_t m_Revision = 0;
  uint64_t m_Selected = 0;
  SceneManager m_Preview;
  PreviewMode m_PreviewMode = PreviewMode::Edit;
  uint64_t m_PreviewFrames = 0;
  std::vector<uint8_t> m_ManagedAssembly;
  std::vector<uint8_t> m_ManagedPdb;
};
}
