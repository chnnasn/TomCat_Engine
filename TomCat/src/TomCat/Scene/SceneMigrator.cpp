#include "tcpch.h"
#include "TomCat/Scene/SceneMigrator.h"

#include "TomCat/Asset/ContentHash.h"
#include "TomCat/Scene/Scene.h"
#include "TomCat/Scene/SceneSerializer.h"
#include "TomCat/Project/Project.h"
#include "TomCat/Utils/FileSystemUtils.h"
#include "TomCat/Utils/PathUtils.h"

#include <algorithm>
#include <fstream>
#include <random>
#include <yaml-cpp/yaml.h>

namespace TomCat {
	namespace {

		constexpr std::string_view BackupExtension = ".bak";
		constexpr std::string_view JournalExtension = ".migration-journal";
		constexpr std::string_view JournalStatePending = "Migrating";

		std::string FileSHA256(const std::filesystem::path& path,
			std::string& error)
		{
			std::string digest;
			if (!ComputeFileContentSHA256(path, digest, error))
				digest.clear();
			return digest;
		}

		std::string BytesSHA256(std::span<const uint8_t> bytes)
		{
			return ComputeContentSHA256(bytes);
		}

		std::vector<uint8_t> ToBytes(std::span<const char> text)
		{
			return { text.begin(), text.end() };
		}

		std::vector<uint8_t> ReadFileBytes(const std::filesystem::path& path,
			std::string& error)
		{
			std::ifstream input(path, std::ios::binary);
			if (!input)
			{
				error = "could not open " + PathToUTF8(path);
				return {};
			}
			std::vector<uint8_t> bytes{ std::istreambuf_iterator<char>(input),
				std::istreambuf_iterator<char>() };
			if (input.bad())
			{
				error = "could not read " + PathToUTF8(path);
				return {};
			}
			return bytes;
		}

		std::string MakeTransactionID()
		{
			std::random_device device;
			std::mt19937_64 generator(device());
			const uint64_t random = (static_cast<uint64_t>(generator()) << 32)
				| static_cast<uint64_t>(generator());
			char buffer[17] = {};
			for (int index = 0; index < 16; ++index)
				buffer[index] = "0123456789abcdef"[(random >> (index * 4)) & 0xf];
			return std::string(buffer, 16);
		}

		std::filesystem::path BackupFilePath(
			const std::filesystem::path& backupDirectory,
			const std::filesystem::path& scenePath, const std::string& digest)
		{
			return backupDirectory / (PathToUTF8(scenePath.filename()) + "."
				+ digest.substr(0, 16) + std::string(BackupExtension));
		}

		std::filesystem::path JournalFilePath(
			const std::filesystem::path& backupDirectory,
			const std::filesystem::path& scenePath)
		{
			return backupDirectory / (PathToUTF8(scenePath.filename())
				+ std::string(JournalExtension));
		}

		[[nodiscard]] bool RemoveFileIfPresent(const std::filesystem::path& path,
			std::string& error)
		{
			bool removed = false;
			return FileSystem::RemovePathSafely(path, removed, error);
		}

	}

	bool SceneMigrator::InspectSceneFile(const std::filesystem::path& scenePath,
		uint32_t& schemaVersion, std::string& error)
	{
		schemaVersion = 0;
		try
		{
			std::ifstream input(scenePath, std::ios::binary);
			if (!input)
			{
				error = "could not open scene " + PathToUTF8(scenePath);
				return false;
			}
			YAML::Node document = YAML::Load(input);
			if (!document.IsMap())
			{
				error = "scene is not a mapping: " + PathToUTF8(scenePath);
				return false;
			}
			static const char* RootFields[] = { "SchemaVersion", "SceneName",
				"Entities" };
			for (const char* field : RootFields)
			{
				if (!document[field])
				{
					error = std::string("scene is missing '") + field + "': "
						+ PathToUTF8(scenePath);
					return false;
				}
			}
			schemaVersion = document["SchemaVersion"].as<uint32_t>();
		}
		catch (const YAML::Exception& exception)
		{
			error = "scene is not valid YAML (" + std::string(exception.what())
				+ "): " + PathToUTF8(scenePath);
			return false;
		}
		catch (const std::exception& exception)
		{
			error = exception.what();
			return false;
		}
		if (schemaVersion < OldestSupportedSchemaVersion
			|| schemaVersion > CurrentSchemaVersion)
		{
			error = "scene SchemaVersion must be in ["
				+ std::to_string(OldestSupportedSchemaVersion) + ", "
				+ std::to_string(CurrentSchemaVersion) + "], got "
				+ std::to_string(schemaVersion) + ": " + PathToUTF8(scenePath);
			return false;
		}
		return true;
	}

	bool SceneMigrator::DecodeAndReserialize(const std::filesystem::path& scenePath,
		const std::vector<uint8_t>& originalBytes, std::string& migrated,
		std::string& error)
	{
		Ref<Scene> scene = CreateRef<Scene>();
		SceneSerializer serializer(scene);
		if (!serializer.DeserializeDocument(originalBytes, scenePath, false))
		{
			error = "scene could not be decoded for migration: "
				+ PathToUTF8(scenePath);
			return false;
		}
		if (!serializer.SerializeDocument(migrated, error))
		{
			error = "scene could not be re-serialized at schema "
				+ std::to_string(CurrentSchemaVersion) + ": " + error;
			return false;
		}
		if (!SceneSerializer::ValidateCurrentFormat(ToBytes(migrated), scenePath))
		{
			error = "migrated scene failed current-schema validation: "
				+ PathToUTF8(scenePath);
			return false;
		}
		return true;
	}

	bool SceneMigrator::PreviewSceneFileMigration(
		const std::filesystem::path& scenePath,
		const std::filesystem::path& backupDirectory,
		SceneFileMigrationPlan& plan, std::string& error)
	{
		plan = {};
		plan.ScenePath = scenePath;
		plan.RelativePath = scenePath;
		if (!InspectSceneFile(scenePath, plan.SourceSchemaVersion, error))
			return false;
		std::vector<uint8_t> bytes = ReadFileBytes(scenePath, error);
		if (!error.empty())
			return false;
		std::string migrated;
		return DecodeAndReserialize(scenePath, bytes, migrated, error);
	}

	bool SceneMigrator::MigrateSceneFile(const std::filesystem::path& scenePath,
		const std::filesystem::path& backupDirectory, std::string& error)
	{
		uint32_t schemaVersion = 0;
		if (!InspectSceneFile(scenePath, schemaVersion, error))
			return false;
		if (schemaVersion == CurrentSchemaVersion)
		{
			// Already current: only require that the scene still validates.
			std::vector<uint8_t> bytes = ReadFileBytes(scenePath, error);
			if (!error.empty())
				return false;
			return SceneSerializer::ValidateCurrentFormat(bytes, scenePath);
		}

		std::vector<uint8_t> originalBytes = ReadFileBytes(scenePath, error);
		if (!error.empty())
			return false;
		const std::string originalDigest = BytesSHA256(originalBytes);
		std::string migrated;
		if (!DecodeAndReserialize(scenePath, originalBytes, migrated, error))
			return false;
		const std::vector<uint8_t> migratedBytes = ToBytes(migrated);
		const std::string migratedDigest = BytesSHA256(migratedBytes);

		std::error_code code;
		if (!std::filesystem::is_directory(backupDirectory, code)
			&& !std::filesystem::create_directories(backupDirectory, code))
		{
			error = "could not create migration backup directory "
				+ PathToUTF8(backupDirectory) + ": " + code.message();
			return false;
		}

		SceneMigrationRecovery transaction;
		transaction.ScenePath = scenePath;
		transaction.JournalPath = JournalFilePath(backupDirectory, scenePath);
		transaction.BackupPath = BackupFilePath(backupDirectory, scenePath,
			originalDigest);
		transaction.TransactionID = MakeTransactionID();
		transaction.OriginalSHA256 = originalDigest;
		transaction.MigratedSHA256 = migratedDigest;
		transaction.JournalState = std::string(JournalStatePending);

		// Content-addressed backups keep repeated migrations cheap and make
		// every recovery verifiable against the exact original bytes.
		if (!FileSystem::WriteFileAtomically(transaction.BackupPath,
			std::string_view(reinterpret_cast<const char*>(originalBytes.data()),
				originalBytes.size()),
			error))
		{
			error = "could not write migration backup: " + error;
			return false;
		}
		if (!WriteJournal(transaction.JournalPath, transaction, error))
		{
			std::string cleanupError;
			(void)RemoveFileIfPresent(transaction.BackupPath, cleanupError);
			return false;
		}
		if (!FileSystem::WriteFileAtomically(scenePath,
			std::string_view(reinterpret_cast<const char*>(migratedBytes.data()),
				migratedBytes.size()),
			error))
		{
			error = "could not install migrated scene: " + error;
			return false;
		}

		// The journal is the commit point: verify the installed file before
		// removing it so an interrupted install is always recoverable.
		const std::string installedDigest = FileSHA256(scenePath, error);
		if (installedDigest != migratedDigest)
		{
			std::string restoreError;
			if (!FileSystem::WriteFileAtomically(scenePath,
				std::string_view(reinterpret_cast<const char*>(originalBytes.data()),
					originalBytes.size()),
				restoreError))
			{
				error += " (and the original could not be restored: "
					+ restoreError + "; recover from "
					+ PathToUTF8(transaction.BackupPath) + ")";
			}
			else
			{
				std::string journalError;
				(void)RemoveFileIfPresent(transaction.JournalPath, journalError);
			}
			error = "migrated scene verification failed: " + error;
			return false;
		}
		return RemoveFileIfPresent(transaction.JournalPath, error);
	}

	bool SceneMigrator::WriteJournal(const std::filesystem::path& journalPath,
		const SceneMigrationRecovery& transaction, std::string& error)
	{
		YAML::Emitter out;
		out << YAML::BeginMap;
		out << YAML::Key << "JournalVersion" << YAML::Value << 1;
		out << YAML::Key << "TransactionID" << YAML::Value
			<< transaction.TransactionID;
		out << YAML::Key << "State" << YAML::Value << transaction.JournalState;
		out << YAML::Key << "ScenePath" << YAML::Value
			<< PathToUTF8(transaction.ScenePath);
		out << YAML::Key << "OriginalSHA256" << YAML::Value
			<< transaction.OriginalSHA256;
		out << YAML::Key << "MigratedSHA256" << YAML::Value
			<< transaction.MigratedSHA256;
		out << YAML::Key << "BackupFile" << YAML::Value
			<< PathToUTF8(transaction.BackupPath.filename());
		out << YAML::EndMap;
		if (!out.good())
		{
			error = out.GetLastError();
			return false;
		}
		return FileSystem::WriteFileAtomically(journalPath,
			std::string_view(out.c_str(), out.size()), error);
	}

	std::filesystem::path SceneMigrator::ProjectBackupRoot(
		const Project& project)
	{
		return project.GetProjectDirectory() / "ProjectSettings"
			/ "SceneMigrationBackups";
	}

	namespace {

		bool CollectSceneFiles(const std::filesystem::path& assetsRoot,
			std::vector<std::filesystem::path>& scenes, std::string& error)
		{
			std::error_code code;
			std::filesystem::recursive_directory_iterator iterator(assetsRoot,
				std::filesystem::directory_options::skip_permission_denied, code);
			const std::filesystem::recursive_directory_iterator end;
			for (; !code && iterator != end; iterator.increment(code))
			{
				const std::filesystem::directory_entry& entry = *iterator;
				if (!entry.is_regular_file(code) || code)
					continue;
				if (PathToUTF8(entry.path().extension()) != ".tomcat")
					continue;
				scenes.push_back(entry.path());
			}
			if (code)
			{
				error = "could not enumerate assets under "
					+ PathToUTF8(assetsRoot) + ": " + code.message();
				return false;
			}
			std::sort(scenes.begin(), scenes.end());
			return true;
		}

	}

	bool SceneMigrator::PreviewProjectMigration(
		const std::filesystem::path& projectPath,
		SceneMigrationPreview& preview, std::string& error)
	{
		preview = {};
		preview.ProjectPath = projectPath;

		Ref<Project> project = Project::Inspect(projectPath);
		if (!project)
		{
			error = "project could not be inspected: " + PathToUTF8(projectPath);
			return false;
		}
		preview.BackupRoot = ProjectBackupRoot(*project);

		// An interrupted per-scene migration must be reviewed explicitly before
		// any new migration runs, mirroring the project-format journal rule.
		std::error_code code;
		if (std::filesystem::is_directory(preview.BackupRoot, code))
		{
			std::filesystem::directory_iterator iterator(preview.BackupRoot, code);
			const std::filesystem::directory_iterator iteratorEnd;
			for (; !code && iterator != iteratorEnd; iterator.increment(code))
			{
				const std::filesystem::directory_entry& entry = *iterator;
				if (!entry.is_regular_file(code) || code)
					continue;
				const std::string name = PathToUTF8(entry.path().filename());
				if (name.size() > JournalExtension.size()
					&& name.substr(name.size() - JournalExtension.size())
						== JournalExtension)
				{
					error = "an interrupted scene migration journal exists ("
						+ name + "); review it before migrating this project";
					return false;
				}
			}
			if (code)
			{
				error = "could not inspect the scene migration backup root: "
					+ code.message();
				return false;
			}
		}

		std::vector<std::filesystem::path> scenes;
		if (!CollectSceneFiles(project->GetAssetPath(), scenes, error))
			return false;

		for (const std::filesystem::path& scenePath : scenes)
		{
			SceneFileMigrationPlan plan;
			if (!PreviewSceneFileMigration(scenePath, preview.BackupRoot, plan,
				error))
				return false;
			std::error_code relativeError;
			plan.RelativePath = std::filesystem::relative(scenePath, projectPath,
				relativeError).lexically_normal();
			if (relativeError)
				plan.RelativePath = scenePath;
			preview.Scenes.push_back(std::move(plan));
		}
		return true;
	}

	bool SceneMigrator::MigrateProjectScenes(
		const std::filesystem::path& projectPath,
		const SceneMigrationPreview& approvedPreview, std::string& error)
	{
		// Re-plan and require an exact match with the caller-approved preview so
		// a stale approval cannot touch files it did not present.
		SceneMigrationPreview current;
		if (!PreviewProjectMigration(projectPath, current, error))
			return false;
		if (current.Scenes.size() != approvedPreview.Scenes.size())
		{
			error = "the approved scene migration plan is stale: the project now "
				"contains " + std::to_string(current.Scenes.size()) + " scenes "
				"instead of " + std::to_string(approvedPreview.Scenes.size());
			return false;
		}
		for (size_t index = 0; index < current.Scenes.size(); ++index)
		{
			const SceneFileMigrationPlan& approved = approvedPreview.Scenes[index];
			const SceneFileMigrationPlan& plan = current.Scenes[index];
			if (plan.ScenePath != approved.ScenePath
				|| plan.SourceSchemaVersion != approved.SourceSchemaVersion)
			{
				error = "the approved scene migration plan is stale: "
					+ PathToUTF8(approved.ScenePath) + " changed";
				return false;
			}
		}

		for (const SceneFileMigrationPlan& plan : current.Scenes)
		{
			if (!plan.RequiresMigration())
				continue;
			std::string sceneError;
			if (!MigrateSceneFile(plan.ScenePath, current.BackupRoot, sceneError))
			{
				error = PathToUTF8(plan.RelativePath) + ": " + sceneError;
				return false;
			}
		}
		return true;
	}

	namespace {

		bool ParseJournal(const std::filesystem::path& journalPath,
			std::string& transactionID, std::string& originalDigest,
			std::string& migratedDigest, std::string& backupFile,
			std::string& error)
		{
			try
			{
				std::ifstream input(journalPath, std::ios::binary);
				if (!input)
				{
					error = "could not open migration journal "
						+ PathToUTF8(journalPath);
					return false;
				}
				YAML::Node journal = YAML::Load(input);
				if (!journal.IsMap() || journal.size() != 7)
				{
					error = "migration journal has an unexpected layout";
					return false;
				}
				static const char* Fields[] = { "JournalVersion", "TransactionID",
					"State", "ScenePath", "OriginalSHA256", "MigratedSHA256",
					"BackupFile" };
				for (const char* field : Fields)
				{
					if (!journal[field])
					{
						error = std::string("migration journal is missing '")
							+ field + "'";
						return false;
					}
				}
				if (journal["JournalVersion"].as<uint32_t>() != 1)
				{
					error = "migration journal version is not supported";
					return false;
				}
				transactionID = journal["TransactionID"].as<std::string>();
				originalDigest = journal["OriginalSHA256"].as<std::string>();
				migratedDigest = journal["MigratedSHA256"].as<std::string>();
				backupFile = journal["BackupFile"].as<std::string>();
				(void)journal["State"].as<std::string>();
			}
			catch (const YAML::Exception& exception)
			{
				error = std::string("migration journal is not valid YAML: ")
					+ exception.what();
				return false;
			}
			return true;
		}

	}

	bool SceneMigrator::PreviewInterruptedMigration(
		const std::filesystem::path& scenePath,
		const std::filesystem::path& backupDirectory,
		SceneMigrationRecovery& recovery, std::string& error)
	{
		recovery = {};
		recovery.ScenePath = scenePath;
		recovery.JournalPath = JournalFilePath(backupDirectory, scenePath);
		std::error_code code;
		if (!std::filesystem::is_regular_file(recovery.JournalPath, code))
		{
			error = "no interrupted migration journal exists for "
				+ PathToUTF8(scenePath.filename());
			return false;
		}

		std::string transactionID;
		std::string originalDigest;
		std::string migratedDigest;
		std::string backupFile;
		if (!ParseJournal(recovery.JournalPath, transactionID, originalDigest,
			migratedDigest, backupFile, error))
			return false;

		recovery.TransactionID = transactionID;
		recovery.OriginalSHA256 = originalDigest;
		recovery.MigratedSHA256 = migratedDigest;
		recovery.BackupPath = backupDirectory / backupFile;
		recovery.BackupPresent = std::filesystem::is_regular_file(
			recovery.BackupPath, code);

		if (recovery.BackupPresent)
		{
			std::string backupError;
			const std::string backupDigest = FileSHA256(recovery.BackupPath,
				backupError);
			if (!backupError.empty())
			{
				error = "could not read the migration backup: " + backupError;
				return false;
			}
			if (backupDigest != originalDigest)
			{
				error = "the migration backup no longer matches the journal "
					"original";
				return false;
			}
		}

		recovery.CurrentSHA256 = FileSHA256(scenePath, error);
		if (!error.empty())
			return false;

		if (recovery.CurrentSHA256 == originalDigest)
		{
			// The install never happened; the original is intact.
			recovery.RecommendedAction = SceneMigrationRecoveryAction::FinishTransaction;
		}
		else if (recovery.CurrentSHA256 == migratedDigest)
		{
			if (!recovery.BackupPresent)
			{
				error = "the migration backup is missing; the migrated scene can "
					"only be kept, not restored";
				return false;
			}
			recovery.RecommendedAction = SceneMigrationRecoveryAction::KeepCurrent;
		}
		else
		{
			error = "the scene changed after the interrupted migration; resolve "
				"it manually before recovering";
			return false;
		}
		return true;
	}

	bool SceneMigrator::RecoverInterruptedMigration(
		const std::filesystem::path& scenePath,
		const std::filesystem::path& backupDirectory,
		const SceneMigrationRecovery& approvedRecovery, bool keepCurrent,
		std::string& error)
	{
		SceneMigrationRecovery current;
		if (!PreviewInterruptedMigration(scenePath, backupDirectory, current,
			error))
			return false;
		if (current.TransactionID != approvedRecovery.TransactionID
			|| current.CurrentSHA256 != approvedRecovery.CurrentSHA256)
		{
			error = "the approved recovery is stale: the interrupted migration "
				"changed since it was presented";
			return false;
		}

		if (keepCurrent)
			return RemoveFileIfPresent(current.JournalPath, error);

		// Restore the verified byte-for-byte original from the backup.
		std::vector<uint8_t> backupBytes = ReadFileBytes(current.BackupPath,
			error);
		if (!error.empty())
			return false;
		if (BytesSHA256(backupBytes) != current.OriginalSHA256)
		{
			error = "the migration backup no longer matches the journal original";
			return false;
		}
		if (!FileSystem::WriteFileAtomically(scenePath,
			std::string_view(reinterpret_cast<const char*>(backupBytes.data()),
				backupBytes.size()),
			error))
		{
			error = "could not restore the original scene: " + error;
			return false;
		}
		const std::string restoredDigest = FileSHA256(scenePath, error);
		if (!error.empty())
			return false;
		if (restoredDigest != current.OriginalSHA256)
		{
			error = "the restored scene does not match the journal original";
			return false;
		}
		return RemoveFileIfPresent(current.JournalPath, error);
	}

}
