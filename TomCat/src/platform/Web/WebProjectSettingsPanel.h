#pragma once
#ifdef __EMSCRIPTEN__
#include "TomCat/Core/Base.h"
#include "TomCat/Project/Project.h"
#include <array>
#include <functional>
namespace TomCat {
class WebProjectSettingsPanel {
public:
  bool m_ShowProjectSettingsPanel=false, m_FocusProjectSettingsPanel=false;
  Ref<Project> m_CurrentProject;
  bool m_Running=false;
  std::function<void()> m_OnChanged;
  void Draw();
private:
  bool IsSceneRunning() const { return m_Running; }
  void ClearProjectSettingsFeedback();
  void LoadProjectSettingsDraft();
  void SyncProjectSettingsLayerBuffers();
  void SyncPlayerSettingsBuffers();
  bool PersistProjectSettingsDraft();
  bool PersistPlayerSettingsDraft();
		int m_ProjectSettingsPage = 0;
		Ref<Project> m_ProjectSettingsDraftProject;
		ProjectSettings m_ProjectSettingsDraft;
		PlayerSettings m_PlayerSettingsDraft;
		std::array<std::array<char, 128>, Physics2DLayerCount> m_ProjectLayerNameBuffers{};
		std::array<char, 128> m_NewProjectTagBuffer{};
		std::array<char, 129> m_PlayerProductNameBuffer{};
		std::array<char, 129> m_PlayerCompanyNameBuffer{};
		std::array<char, 65> m_PlayerVersionBuffer{};
		std::array<char, 513> m_PlayerSaveDirectoryBuffer{};
		std::array<char, 513> m_PlayerLogDirectoryBuffer{};
		std::array<char, 513> m_PlayerCrashDirectoryBuffer{};
		std::string m_ProjectSettingsError;
		std::string m_ProjectSettingsStatus;
 };
}
#endif
