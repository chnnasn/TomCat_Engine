#pragma once

// Project Settings / Player Settings draft editing and persistence,
// extracted from EditorLayer. The project migration flow stays in
// EditorLayer because OpenProject owns it.

#include "TomCat/Core/Base.h"
#include "TomCat/Project/ProjectSettings.h"

#include <array>
#include <string>

namespace TomCat {

	class EditorLayer;
	class Project;

	class EditorProjectSettingsController
	{
		friend class EditorLayer;
	public:
		explicit EditorProjectSettingsController(EditorLayer& layer)
			: m_Layer(layer) {}

		void ClearProjectSettingsFeedback();
		void LoadProjectSettingsDraft();
		void SyncProjectSettingsLayerBuffers();
		void SyncPlayerSettingsBuffers();
		bool PersistProjectSettingsDraft();
		bool PersistPlayerSettingsDraft();
		void UI_ProjectSettings();

	private:
		EditorLayer& m_Layer;
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
