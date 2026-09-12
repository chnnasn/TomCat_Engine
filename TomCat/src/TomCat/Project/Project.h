#pragma once

#include "TomCat/Asset/Asset.h"
#include "ProjectSettings.h"

#include <string>
#include <filesystem>
#include <vector>
#include <ctime>
#include <cstdint>

namespace TomCat {

	struct ProjectConfig
	{
		std::string Name = "Untitled Project";
		std::string Version = "1.0.0";
		std::string Description;
		std::string EditorVersion;
		std::string Template = "3D";
		std::filesystem::path AssetDirectory = "Assets";
		std::filesystem::path StartScene = "sample.tomcat";
		// Stable identity is authoritative. The path is only an author-friendly
		// locator and is repaired from this handle.
		AssetHandle StartSceneHandle = AssetHandle(0);

		// User-local recency metadata is stored in Hub settings rather than rewriting
		// the shared project file whenever it is opened.
		std::string LastOperationTime;
	};

	// Project-local Editor state. This is intentionally kept outside ProjectConfig
	// because it is written to UserSettings/editor.json, never Project.tcproj.
	struct EditorProjectState
	{
		std::string ContentBrowserCurrentDirectory = ".";
		std::vector<std::string> ContentBrowserExpandedNodes;
		// User-local absolute path to the executable used for opening C# sources.
		// Kept out of Project.tcproj because different contributors may use
		// different editors.
		std::filesystem::path ExternalScriptEditor;
	};

	enum class EditorProjectStateLoadResult
	{
		Missing,
		Loaded,
		Failed
	};

	class Project
	{
	public:
		static constexpr uint32_t CurrentSchemaVersion = 3;

		Project() = default;
		Project(const std::filesystem::path& projectPath);

		const std::filesystem::path& GetProjectPath() const { return m_ProjectPath; }
		const std::filesystem::path& GetProjectDirectory() const { return m_Directory; }
		const ProjectConfig& GetConfig() const { return m_Config; }
		const ProjectSettings& GetSettings() const { return m_Settings; }
		std::filesystem::path GetSettingsPath() const
		{
			return m_Directory / "ProjectSettings" / "ProjectSettings.json";
		}
		
		void SetConfig(const ProjectConfig& config) { m_Config = config; }
		// Validates and atomically persists shared project settings. The in-memory
		// value changes only if the file write succeeds.
		bool SetSettings(const ProjectSettings& settings);
		
		const std::string& GetName() const { return m_Config.Name; }
		const std::string& GetEditorVersion() const { return m_Config.EditorVersion; }
		const std::string& GetLastOperationTime() const { return m_Config.LastOperationTime; }
		std::string GetLastOperationTimeAgo() const;
		void UpdateLastOperationTime();
		void Touch();

		std::filesystem::path GetAssetPath() const { return m_Directory / m_Config.AssetDirectory; };
		// Assets and their .tcmeta sidecars are source content. Everything under
		// Library is derived data and may be deleted and rebuilt at any time.
		std::filesystem::path GetLibraryPath() const { return m_Directory / "Library"; }
		std::filesystem::path GetCachePath() const { return m_Directory / "Cache"; }
		std::filesystem::path GetUserSettingsPath() const { return m_Directory / "UserSettings"; }

		bool SetStartScene(const std::filesystem::path& scenePath);
		void SetStartSceneHandle(AssetHandle handle) { m_Config.StartSceneHandle = handle; }

		EditorProjectStateLoadResult LoadEditorState(EditorProjectState& state) const;
		bool SaveEditorState(const EditorProjectState& state) const;
		
		bool IsValid() const { return !m_ProjectPath.empty(); }
		
		static Ref<Project> CreateNew(const std::filesystem::path& projectPath, const ProjectConfig& config);
		static Ref<Project> Load(const std::filesystem::path& projectPath);
		bool Save();
		bool SaveSettings() const;
		bool Reload();

	private:
		std::filesystem::path m_ProjectPath;
		std::filesystem::path m_Directory;
		ProjectConfig m_Config;
		ProjectSettings m_Settings;
		std::string m_PreservedDocument;

		friend class ProjectManager;
	};

}
