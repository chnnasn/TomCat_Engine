#pragma once
#include "TomCat/Core/Base.h"
#include "TomCat/Editor/SceneHistory.h"
#include <string>

namespace TomCat {
class Scene;
class Project;
// Authoring-only session. No runtime Scene or managed callbacks are mutated.
class WebEditorSession final {
public:
  std::string Invoke(const std::string& request);
  Ref<Scene> GetScene() const { return m_Scene; }
private:
  std::string Snapshot() const;
  Ref<Scene> m_Scene;
  Ref<Project> m_Project;
  SceneHistory m_History;
  uint64_t m_SceneHandle = 0;
  uint64_t m_Revision = 0;
  uint64_t m_Selected = 0;
};
}
