#pragma once

#include "TomCat/Asset/Asset.h"
#include "TomCat/Core/Version.h"
#include "BuildSettings.h"
#include "PlayerSettings.h"
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
		std::string Version = std::string(TomCat::Version::ProductVersion);
		std::string Description;
		std::string EditorVersion;
		std::string Template = "3D";
		std::filesystem::path AssetDirectory = "Assets";
		// Deprecated in-memory compatibility mirrors. Project schema v4 never
		// persists these fields; ProjectSettings/BuildSettings.json is authoritative.
		std::filesystem::path StartScene = "sample.tomcat";
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

	enum class ProjectMigrationChangeKind
	{
		Create,
		Replace
	};

	struct ProjectMigrationChange
	{
		std::filesystem::path RelativePath;
		ProjectMigrationChangeKind Kind = ProjectMigrationChangeKind::Create;
		uintmax_t OriginalSize = 0;
		std::string OriginalSHA256;
		std::string Reason;
	};

	// A read-only description of every file an opening project upgrade would
	// touch. Callers can present this before invoking LoadWithMigration().
	struct ProjectMigrationPreview
	{
		std::filesystem::path ProjectPath;
		uint32_t SourceSchemaVersion = 0;
		uint32_t TargetSchemaVersion = 0;
		std::filesystem::path BackupRoot = "ProjectSettings/MigrationBackups";
		std::vector<ProjectMigrationChange> Changes;

		bool RequiresMigration() const { return !Changes.empty(); }
	};

	enum class ProjectMigrationRecoveryAction
	{
		KeepCurrent,
		RestoreOriginal,
		RemoveCreatedFile,
		AlreadyRestored,
		AlreadyAbsent
	};

	struct ProjectMigrationRecoveryChange
	{
		std::filesystem::path RelativePath;
		bool OriginalExisted = false;
		uintmax_t OriginalSize = 0;
		std::string OriginalSHA256;
		bool CurrentExists = false;
		uintmax_t CurrentSize = 0;
		std::string CurrentSHA256;
		ProjectMigrationRecoveryAction Action =
			ProjectMigrationRecoveryAction::KeepCurrent;

		bool WillModifyProjectFile() const
		{
			return Action == ProjectMigrationRecoveryAction::RestoreOriginal
				|| Action == ProjectMigrationRecoveryAction::RemoveCreatedFile;
		}
	};

	// A read-only, content-addressed description of an interrupted migration.
	// The current target digests make an approval stale as soon as a user or tool
	// edits any affected file while the recovery prompt is open.
	struct ProjectMigrationRecoveryPreview
	{
		std::filesystem::path ProjectPath;
		std::string TransactionID;
		std::string JournalState;
		uintmax_t JournalSize = 0;
		std::string JournalSHA256;
		std::filesystem::path BackupDirectory;
		std::vector<ProjectMigrationRecoveryChange> Changes;

		bool HasPendingRecovery() const { return !TransactionID.empty(); }
		bool WillModifyProjectFiles() const
		{
			for (const auto& change : Changes)
			{
				if (change.WillModifyProjectFile())
					return true;
			}
			return false;
		}
	};

	class Project
	{
	public:
		static constexpr uint32_t OldestSupportedSchemaVersion =
			Version::ProjectFormatOldest;
		static constexpr uint32_t CurrentSchemaVersion =
			Version::ProjectFormatCurrent;

		Project() = default;
		Project(const std::filesystem::path& projectPath);

		const std::filesystem::path& GetProjectPath() const { return m_ProjectPath; }
		const std::filesystem::path& GetProjectDirectory() const { return m_Directory; }
		const ProjectConfig& GetConfig() const { return m_Config; }
		const ProjectSettings& GetSettings() const { return m_Settings; }
		const PlayerSettings& GetPlayerSettings() const { return m_PlayerSettings; }
		const BuildSettings& GetBuildSettings() const { return m_BuildSettings; }
		std::filesystem::path GetSettingsPath() const
		{
			return m_Directory / "ProjectSettings" / "ProjectSettings.json";
		}
		std::filesystem::path GetBuildSettingsPath() const
		{
			return m_Directory / "ProjectSettings" / "BuildSettings.json";
		}
		std::filesystem::path GetPlayerSettingsPath() const
		{
			return m_Directory / "ProjectSettings" / "PlayerSettings.json";
		}
		
		void SetConfig(const ProjectConfig& config) { m_Config = config; }
		// Validates and atomically persists shared project settings. The in-memory
		// value changes only if the file write succeeds.
		bool SetSettings(const ProjectSettings& settings);
		bool SetPlayerSettings(const PlayerSettings& settings);
		bool SetBuildSettings(const BuildSettings& settings);
		
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
		bool SetStartSceneHandle(AssetHandle handle);

		EditorProjectStateLoadResult LoadEditorState(EditorProjectState& state) const;
		bool SaveEditorState(const EditorProjectState& state) const;
		
		bool IsValid() const { return !m_ProjectPath.empty(); }
		
		static Ref<Project> CreateNew(const std::filesystem::path& projectPath, const ProjectConfig& config);
		// Parses and validates project metadata without migrations, directory
		// creation, ignore-file updates, or any other write.
		static Ref<Project> Inspect(const std::filesystem::path& projectPath);
		// Computes the exact file set that LoadWithMigration() would change without
		// changing the project tree. An unfinished journal is reported as an error
		// so the caller can explicitly recover it first.
		[[nodiscard]] static bool PreviewMigration(const std::filesystem::path& projectPath,
			ProjectMigrationPreview& preview, std::string& errorMessage);
		// Inspects and validates an interrupted migration journal and all original
		// backups without changing the project tree.
		[[nodiscard]] static bool PreviewInterruptedMigration(
			const std::filesystem::path& projectPath,
			ProjectMigrationRecoveryPreview& preview,
			std::string& errorMessage);
		// Restores the byte-for-byte originals recorded by an interrupted
		// migration journal only when the journal and every current target still
		// exactly match the caller-approved preview.
		[[nodiscard]] static bool RecoverInterruptedMigration(
			const std::filesystem::path& projectPath,
			const ProjectMigrationRecoveryPreview& approvedRecovery,
			std::string& errorMessage);
		// Exports the validated original backups, current affected files, and
		// journal into a new standalone directory without changing the project.
		[[nodiscard]] static bool ExportInterruptedMigrationBackup(
			const std::filesystem::path& projectPath,
			const ProjectMigrationRecoveryPreview& approvedRecovery,
			const std::filesystem::path& destinationDirectory,
			std::string& errorMessage);
		// Keeps every current project file and abandons the rollback by removing
		// only the active journal. The transaction backup archive is retained.
		[[nodiscard]] static bool AbandonInterruptedMigrationRecovery(
			const std::filesystem::path& projectPath,
			const ProjectMigrationRecoveryPreview& approvedRecovery,
			std::string& errorMessage);
		// Loads only projects that require no migration writes. Projects with an
		// active recovery journal or a pending migration are rejected.
		static Ref<Project> Load(const std::filesystem::path& projectPath);
		// Recomputes the migration plan and executes it only when it exactly matches
		// the caller-approved preview. Stale or incomplete plans are rejected.
		static Ref<Project> LoadWithMigration(const std::filesystem::path& projectPath,
			const ProjectMigrationPreview& approvedMigration);
		bool Save();
		bool SaveSettings() const;
		bool SavePlayerSettings() const;
		bool SaveBuildSettings() const;
		bool Reload();

	private:
		static Ref<Project> LoadInternal(const std::filesystem::path& projectPath,
			bool inspectOnly,
			const ProjectMigrationPreview* approvedMigration);
		void SynchronizeLegacyStartSceneMirror();

		std::filesystem::path m_ProjectPath;
		std::filesystem::path m_Directory;
		ProjectConfig m_Config;
		ProjectSettings m_Settings;
		PlayerSettings m_PlayerSettings;
		BuildSettings m_BuildSettings;
		std::string m_PreservedDocument;

		friend class ProjectManager;
	};

}
