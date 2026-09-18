#pragma once
#include "TomCat/Core/Base.h"
#include "TomCat/Editor/SceneHistory.h"
#include "TomCat/Scene/SceneManager.h"
#include "TomCat/Project/PlayerSettings.h"
#include <string>

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
  void EndUIEdit(uint64_t selection, bool cancel = false);
  std::string HistoryFromUI(bool redo);
  std::string OpenSceneAsset(uint64_t handle);
  PreviewMode GetPreviewMode() const { return m_PreviewMode; }
  Ref<Scene> GetPreviewScene() const { return m_Preview.GetActiveScene(); }
  void ControlPreview(const std::string& command);
  void AdvancePreview(float delta);
  void StopPreview();
private:
  std::string Snapshot() const;
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
};
}
