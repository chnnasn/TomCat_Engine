#include "tcpch.h"
#ifdef __EMSCRIPTEN__
#include "WebProjectSettingsPanel.h"
#include "TomCat/Asset/AssetManager.h"
#include "TomCat/Utils/PathUtils.h"
#include <imgui.h>
#include <algorithm>
namespace TomCat {
namespace {
std::string TrimASCIIWhitespace(std::string value) {
  const auto first=value.find_first_not_of(" \t\r\n");
  return first==std::string::npos ? std::string{} : value.substr(first,value.find_last_not_of(" \t\r\n")-first+1);
}
}
	void WebProjectSettingsPanel::ClearProjectSettingsFeedback()
	{
		m_ProjectSettingsError.clear();
		m_ProjectSettingsStatus.clear();
	}

	void WebProjectSettingsPanel::LoadProjectSettingsDraft()
	{
		m_ProjectSettingsDraftProject = m_CurrentProject;
		m_ProjectSettingsDraft = m_CurrentProject
			? m_CurrentProject->GetSettings() : ProjectSettings{};
		m_PlayerSettingsDraft = m_CurrentProject
			? m_CurrentProject->GetPlayerSettings() : PlayerSettings{};
		SyncProjectSettingsLayerBuffers();
		SyncPlayerSettingsBuffers();
		m_NewProjectTagBuffer.fill('\0');
		ClearProjectSettingsFeedback();
	}

	void WebProjectSettingsPanel::SyncProjectSettingsLayerBuffers()
	{
		for (std::size_t layer = 0; layer < Physics2DLayerCount; ++layer)
		{
			auto& buffer = m_ProjectLayerNameBuffers[layer];
			buffer.fill('\0');
			const std::string& name = m_ProjectSettingsDraft.TagsAndLayers.LayerNames[layer];
			const std::size_t count = std::min(name.size(), buffer.size() - 1);
			std::copy_n(name.data(), count, buffer.data());
		}
	}

	void WebProjectSettingsPanel::SyncPlayerSettingsBuffers()
	{
		auto copy = [](const std::string& value, auto& buffer)
		{
			buffer.fill('\0');
			const std::size_t count = std::min(value.size(), buffer.size() - 1);
			std::copy_n(value.data(), count, buffer.data());
		};
		copy(m_PlayerSettingsDraft.ProductName, m_PlayerProductNameBuffer);
		copy(m_PlayerSettingsDraft.CompanyName, m_PlayerCompanyNameBuffer);
		copy(m_PlayerSettingsDraft.Version, m_PlayerVersionBuffer);
		copy(PathToUTF8(m_PlayerSettingsDraft.SaveDirectory),
			m_PlayerSaveDirectoryBuffer);
		copy(PathToUTF8(m_PlayerSettingsDraft.LogDirectory),
			m_PlayerLogDirectoryBuffer);
		copy(PathToUTF8(m_PlayerSettingsDraft.CrashDirectory),
			m_PlayerCrashDirectoryBuffer);
	}

	bool WebProjectSettingsPanel::PersistProjectSettingsDraft()
	{
		ClearProjectSettingsFeedback();
		auto reportValidationError = [this](std::string message)
		{
			// Keep an invalid intermediate edit in memory so users can type through
			// a temporarily duplicate name (for example Player -> PlayerOnly). The
			// settings file remains on the last valid value until this draft validates.
			m_ProjectSettingsError = std::move(message);
			return false;
		};
		auto rejectSaveAndRestore = [this](std::string message)
		{
			if (m_CurrentProject)
				m_ProjectSettingsDraft = m_CurrentProject->GetSettings();
			else
				m_ProjectSettingsDraft = ProjectSettings{};
			SyncProjectSettingsLayerBuffers();
			m_ProjectSettingsError = std::move(message);
			return false;
		};

		if (!m_CurrentProject)
			return rejectSaveAndRestore("No project is open.");
		if (IsSceneRunning())
			return rejectSaveAndRestore("Stop Play Mode before changing project settings.");
		if (m_ProjectSettingsDraft == m_CurrentProject->GetSettings())
			return true;

		const auto& tags = m_ProjectSettingsDraft.TagsAndLayers.Tags;
		if (tags.empty() || tags.front() != "Untagged")
			return reportValidationError("The reserved Untagged tag must remain first.");
		for (std::size_t index = 0; index < tags.size(); ++index)
		{
			if (tags[index].empty())
				return reportValidationError("Tag names cannot be empty.");
			if (std::find(tags.begin(), tags.begin() + index, tags[index]) != tags.begin() + index)
				return reportValidationError("Duplicate tag: " + tags[index]);
		}
		const auto& layerNames = m_ProjectSettingsDraft.TagsAndLayers.LayerNames;
		if (layerNames[0] != "Default")
			return reportValidationError("Layer 0 must remain Default.");
		for (std::size_t index = 0; index < layerNames.size(); ++index)
		{
			if (layerNames[index].empty())
				continue;
			if (std::find(layerNames.begin(), layerNames.begin() + index,
				layerNames[index]) != layerNames.begin() + index)
				return reportValidationError("Duplicate layer name: " + layerNames[index]);
		}

		if (!m_CurrentProject->SetSettings(m_ProjectSettingsDraft))
			return rejectSaveAndRestore(
				"Project settings could not be saved. Verify that ProjectSettings.json is writable.");

		if (m_OnChanged) m_OnChanged();
		m_ProjectSettingsDraft = m_CurrentProject->GetSettings();
		SyncProjectSettingsLayerBuffers();
		m_ProjectSettingsStatus = "Applied to this session. Use File > Save to keep in this browser.";
		return true;
	}

	bool WebProjectSettingsPanel::PersistPlayerSettingsDraft()
	{
		ClearProjectSettingsFeedback();
		auto restore = [this](std::string message)
		{
			m_PlayerSettingsDraft = m_CurrentProject
				? m_CurrentProject->GetPlayerSettings() : PlayerSettings{};
			SyncPlayerSettingsBuffers();
			m_ProjectSettingsError = std::move(message);
			return false;
		};
		if (!m_CurrentProject)
			return restore("No project is open.");
		if (IsSceneRunning())
			return restore("Stop Play Mode before changing Player settings.");
		if (m_PlayerSettingsDraft == m_CurrentProject->GetPlayerSettings())
			return true;
		if (!m_CurrentProject->SetPlayerSettings(m_PlayerSettingsDraft))
			return restore(
				"Player settings could not be saved. Check the values and PlayerSettings.json permissions.");
		if (m_OnChanged) m_OnChanged();
		m_PlayerSettingsDraft = m_CurrentProject->GetPlayerSettings();
		SyncPlayerSettingsBuffers();
		m_ProjectSettingsStatus =
			"Applied to this session. Use File > Save to keep in this browser.";
		return true;
	}

void WebProjectSettingsPanel::Draw() {
#include "panels/ProjectSettingsView.inl"
}
}
#endif
