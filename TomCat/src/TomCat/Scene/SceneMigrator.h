#pragma once

#include <TomCat/Core/Version.h>

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace TomCat {

	class Project;

	// General scene-format migration tooling. Old .tomcat scene files (schema
	// 9/10) are upgraded to the current schema through a transactional
	// per-file pipeline: decode with the tolerant readers (which run every
	// registered per-component schema migration and round-trip opaque missing
	// plugin components), re-serialize at the current schema, verify the
	// result re-reads, then install atomically. The original bytes are backed
	// up under ProjectSettings/SceneMigrationBackups and a journal records the
	// transaction so an interrupted migration can be recovered or abandoned
	// explicitly.

	struct SceneFileMigrationPlan
	{
		std::filesystem::path ScenePath;
		std::filesystem::path RelativePath;
		uint32_t SourceSchemaVersion = 0;

		bool RequiresMigration() const
		{
			return SourceSchemaVersion != 0
				&& SourceSchemaVersion < Version::SceneFormatCurrent;
		}
	};

	// Read-only description of every scene file a project upgrade would
	// rewrite. Callers present this before invoking MigrateProjectScenes.
	struct SceneMigrationPreview
	{
		std::filesystem::path ProjectPath;
		std::filesystem::path BackupRoot;
		std::vector<SceneFileMigrationPlan> Scenes;

		bool RequiresMigration() const
		{
			for (const SceneFileMigrationPlan& scene : Scenes)
			{
				if (scene.RequiresMigration())
					return true;
			}
			return false;
		}
	};

	enum class SceneMigrationRecoveryAction
	{
		KeepCurrent,
		RestoreOriginal,
		FinishTransaction
	};

	// A validated description of an interrupted per-scene migration. The
	// digests make a stale journal detectable: the recommended action is only
	// safe while the scene file on disk still matches the journal.
	struct SceneMigrationRecovery
	{
		std::filesystem::path ScenePath;
		std::filesystem::path JournalPath;
		std::filesystem::path BackupPath;
		std::string TransactionID;
		std::string JournalState;
		std::string OriginalSHA256;
		std::string MigratedSHA256;
		std::string CurrentSHA256;
		bool BackupPresent = false;
		SceneMigrationRecoveryAction RecommendedAction =
			SceneMigrationRecoveryAction::RestoreOriginal;
	};

	class SceneMigrator
	{
	public:
		static constexpr uint32_t CurrentSchemaVersion =
			Version::SceneFormatCurrent;
		static constexpr uint32_t OldestSupportedSchemaVersion =
			Version::SceneFormatOldest;

		// Parses only the scene's SchemaVersion declaration. Versions outside
		// [Oldest, Current] report an error.
		[[nodiscard]] static bool InspectSceneFile(
			const std::filesystem::path& scenePath, uint32_t& schemaVersion,
			std::string& error);

		// Full read-only check that the scene decodes and would re-serialize at
		// the current schema, and computes the planned backup path.
		[[nodiscard]] static bool PreviewSceneFileMigration(
			const std::filesystem::path& scenePath,
			const std::filesystem::path& backupDirectory,
			SceneFileMigrationPlan& plan, std::string& error);

		// Transactional in-place upgrade of one scene file. Safe to call on a
		// scene already at the current schema (no-op success).
		[[nodiscard]] static bool MigrateSceneFile(
			const std::filesystem::path& scenePath,
			const std::filesystem::path& backupDirectory, std::string& error);

		// Enumerates Assets/**/*.tomcat below the project and inspects every
		// scene. projectPath addresses the Project.tcproj file. Rejects projects
		// with an interrupted scene-migration journal.
		[[nodiscard]] static bool PreviewProjectMigration(
			const std::filesystem::path& projectPath,
			SceneMigrationPreview& preview, std::string& error);

		// Executes the approved plan. Every scene is an independent
		// transaction; a failing scene rolls itself back and is reported while
		// previously migrated scenes stay migrated.
		[[nodiscard]] static bool MigrateProjectScenes(
			const std::filesystem::path& projectPath,
			const SceneMigrationPreview& approvedPreview, std::string& error);

		// Validates an interrupted migration journal and its backup without
		// touching the scene. Fails when no journal exists.
		[[nodiscard]] static bool PreviewInterruptedMigration(
			const std::filesystem::path& scenePath,
			const std::filesystem::path& backupDirectory,
			SceneMigrationRecovery& recovery, std::string& error);

		// Completes an interrupted migration. keepCurrent=true finishes the
		// transaction keeping the migrated file; false restores the verified
		// byte-for-byte original. Both actions remove the journal.
		[[nodiscard]] static bool RecoverInterruptedMigration(
			const std::filesystem::path& scenePath,
			const std::filesystem::path& backupDirectory,
			const SceneMigrationRecovery& approvedRecovery, bool keepCurrent,
			std::string& error);

		[[nodiscard]] static std::filesystem::path ProjectBackupRoot(
			const Project& project);

	private:
		[[nodiscard]] static bool DecodeAndReserialize(
			const std::filesystem::path& scenePath,
			const std::vector<uint8_t>& originalBytes, std::string& migrated,
			std::string& error);
		[[nodiscard]] static bool WriteJournal(
			const std::filesystem::path& journalPath,
			const SceneMigrationRecovery& transaction, std::string& error);
	};

}
