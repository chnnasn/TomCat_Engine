#include <TomCat/Asset/ContentHash.h>
#include <TomCat/Core/Base.h>
#include <TomCat/Core/Log.h>
#include <TomCat/Project/Project.h>
#include <TomCat/Scene/SceneMigrator.h>
#include <TomCat/Scene/SceneSerializer.h>
#include <TomCat/Utils/PathUtils.h>

#include <yaml-cpp/yaml.h>

#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

	void Require(bool condition, const std::string& message)
	{
		if (!condition)
			throw std::runtime_error(message);
	}

	class TemporaryDirectory
	{
	public:
		TemporaryDirectory()
		{
			const auto nonce = std::chrono::steady_clock::now().time_since_epoch()
				.count();
			Path = std::filesystem::temp_directory_path()
				/ ("TomCat-SceneMigration-" + std::to_string(nonce));
			std::filesystem::create_directories(Path);
		}

		~TemporaryDirectory()
		{
			std::error_code error;
			std::filesystem::remove_all(Path, error);
		}

		std::filesystem::path Path;
	};

	// Locates the checked-in schema-11 sample scene from the repository so the
	// fixtures are real, validated engine output rather than hand-written YAML.
	std::filesystem::path FindSampleScene()
	{
		std::error_code error;
		std::filesystem::path candidate = std::filesystem::current_path(error);
		for (int depth = 0; depth < 8; ++depth)
		{
			const std::filesystem::path sample = candidate
				/ "Samples/PhysicsPlayground/Assets/Scene/sample.tomcat";
			if (std::filesystem::is_regular_file(sample, error))
				return sample;
			if (!candidate.has_parent_path())
				break;
			candidate = candidate.parent_path();
		}
		throw std::runtime_error("the sample scene fixture could not be located");
	}

	std::string ReadFileText(const std::filesystem::path& path)
	{
		std::ifstream input(path, std::ios::binary);
		Require(static_cast<bool>(input), "could not open " + path.string());
		return { std::istreambuf_iterator<char>(input),
			std::istreambuf_iterator<char>() };
	}

	void WriteFileText(const std::filesystem::path& path, const std::string& text)
	{
		std::ofstream output(path, std::ios::binary | std::ios::trunc);
		Require(static_cast<bool>(output), "could not write " + path.string());
		output << text;
	}

	std::string FileSHA256(const std::filesystem::path& path)
	{
		std::string error;
		std::string digest;
		Require(TomCat::ComputeFileContentSHA256(path, digest, error),
			"could not hash " + path.string() + ": " + error);
		return digest;
	}

	std::string FileSHA256FromText(const std::string& text)
	{
		TemporaryDirectory scratch;
		const std::filesystem::path path = scratch.Path / "bytes";
		WriteFileText(path, text);
		return FileSHA256(path);
	}

	// A current schema-11 scene without the per-entity Components sequence is a
	// valid schema-10 scene; removing the v10-only optional components yields
	// schema-9 input.
	std::string DowngradeScene(const std::string& sceneText, uint32_t targetVersion)
	{
		YAML::Node document = YAML::Load(sceneText);
		document["SchemaVersion"] = targetVersion;
		for (YAML::Node entity : document["Entities"])
		{
			entity.remove("Components");
			if (targetVersion == 9)
			{
				entity.remove("CSharpScripts");
				entity.remove("AudioSource");
				entity.remove("AudioListener");
				entity.remove("SpriteAnimator");
			}
		}
		YAML::Emitter out;
		out << document;
		Require(out.good(), "fixture downgrade failed to emit");
		return std::string(out.c_str(), out.size());
	}

	// The transaction journal exactly as SceneMigrator writes it; used by the
	// recovery tests to construct interrupted states deterministically.
	void WriteJournal(const std::filesystem::path& backupRoot,
		const std::string& sceneFileName, const std::string& transactionID,
		const std::string& originalDigest, const std::string& migratedDigest,
		const std::string& backupFile)
	{
		YAML::Emitter out;
		out << YAML::BeginMap;
		out << YAML::Key << "JournalVersion" << YAML::Value << 1;
		out << YAML::Key << "TransactionID" << YAML::Value << transactionID;
		out << YAML::Key << "State" << YAML::Value << "Migrating";
		out << YAML::Key << "ScenePath" << YAML::Value << sceneFileName;
		out << YAML::Key << "OriginalSHA256" << YAML::Value << originalDigest;
		out << YAML::Key << "MigratedSHA256" << YAML::Value << migratedDigest;
		out << YAML::Key << "BackupFile" << YAML::Value << backupFile;
		out << YAML::EndMap;
		Require(out.good(), "journal emission failed");
		WriteFileText(backupRoot / (sceneFileName + ".migration-journal"),
			std::string(out.c_str(), out.size()));
	}

	struct TestProject
	{
		TemporaryDirectory Root;
		std::filesystem::path Assets;
		std::filesystem::path BackupRoot;
		std::filesystem::path ScenePath;

		explicit TestProject(const std::string& sceneFileName,
			const std::string& sceneText)
		{
			Assets = Root.Path / "Assets" / "Scene";
			std::filesystem::create_directories(Assets);
			std::filesystem::create_directories(Root.Path / "ProjectSettings");
			BackupRoot = Root.Path / "ProjectSettings" / "SceneMigrationBackups";
			ScenePath = Assets / sceneFileName;
			WriteFileText(ScenePath, sceneText);
			// A minimal but valid schema-3 project file so Project::Inspect
			// succeeds without ProjectSettings JSON files.
			WriteFileText(Root.Path / "Project.tcproj",
				"Project:\n"
				"  Name: MigrationFixture\n"
				"  Version: 1.0.0\n"
				"  Description: \"\"\n"
				"  EditorVersion: 0.4.0\n"
				"  Template: 2D\n"
				"  AssetDirectory: Assets\n"
				"  StartScene: Scene/" + sceneFileName + "\n"
				"  StartSceneHandle: 0\n"
				"SchemaVersion: 3\n");
		}
	};

	void TestInspectDetectsVersions()
	{
		const std::string current = ReadFileText(FindSampleScene());
		TemporaryDirectory directory;
		const std::filesystem::path currentPath = directory.Path / "current.tomcat";
		WriteFileText(currentPath, current);
		uint32_t version = 0;
		std::string error;
		Require(TomCat::SceneMigrator::InspectSceneFile(currentPath, version,
			error) && version == 11, "current scene not detected as schema 11");

		const std::filesystem::path v10Path = directory.Path / "v10.tomcat";
		WriteFileText(v10Path, DowngradeScene(current, 10));
		Require(TomCat::SceneMigrator::InspectSceneFile(v10Path, version, error)
			&& version == 10, "downgraded scene not detected as schema 10");

		const std::filesystem::path v9Path = directory.Path / "v9.tomcat";
		WriteFileText(v9Path, DowngradeScene(current, 9));
		Require(TomCat::SceneMigrator::InspectSceneFile(v9Path, version, error)
			&& version == 9, "downgraded scene not detected as schema 9");

		WriteFileText(v9Path, "SchemaVersion: 8\nSceneName: x\nEntities: []\n");
		Require(!TomCat::SceneMigrator::InspectSceneFile(v9Path, version, error),
			"schema 8 was accepted");
	}

	void TestProjectMigrationUpgradeAndBackup()
	{
		const std::string current = ReadFileText(FindSampleScene());
		TestProject project("upgrade.tomcat", DowngradeScene(current, 10));

		TomCat::SceneMigrationPreview preview;
		std::string error;
		const bool okPreview = TomCat::SceneMigrator::PreviewProjectMigration(
			project.Root.Path / "Project.tcproj", preview, error);
		Require(okPreview, "project preview failed: " + error);
		Require(preview.RequiresMigration(), "preview did not require migration");
		Require(preview.Scenes.size() == 1
			&& preview.Scenes[0].SourceSchemaVersion == 10,
			"preview did not report the schema-10 scene");

		const bool okMigrate = TomCat::SceneMigrator::MigrateProjectScenes(
			project.Root.Path / "Project.tcproj", preview, error);
		Require(okMigrate, "migration failed: " + error);

		uint32_t version = 0;
		Require(TomCat::SceneMigrator::InspectSceneFile(project.ScenePath,
			version, error) && version == 11, "scene is not schema 11 after "
			"migration: " + error);
		Require(TomCat::SceneSerializer::ValidateCurrentFormat(
			project.ScenePath), "migrated scene failed validation");

		// The original bytes must survive in the backup root.
		Require(std::filesystem::is_directory(project.BackupRoot),
			"backup root was not created");
		bool backupFound = false;
		for (const auto& entry :
			std::filesystem::directory_iterator(project.BackupRoot))
		{
			if (entry.path().extension() == ".bak")
			{
				backupFound = true;
				Require(entry.file_size() > 0, "backup is empty");
			}
		}
		Require(backupFound, "no backup file was written");

		TomCat::SceneMigrationPreview settled;
		const bool okSettled = TomCat::SceneMigrator::PreviewProjectMigration(
			project.Root.Path / "Project.tcproj", settled, error);
		Require(okSettled, "post-migration preview failed: " + error);
		Require(!settled.RequiresMigration(),
			"the migrated project still requires migration");

		// Re-running the exact same approved plan must reject it as stale
		// because every source version moved.
		bool staleAccepted = TomCat::SceneMigrator::MigrateProjectScenes(
			project.Root.Path / "Project.tcproj", preview, error);
		Require(!staleAccepted, "a stale plan was accepted");
	}

	void TestSchema9Migration()
	{
		const std::string current = ReadFileText(FindSampleScene());
		TestProject project("legacy.tomcat", DowngradeScene(current, 9));
		TomCat::SceneMigrationPreview preview;
		std::string error;
		const bool okPreview = TomCat::SceneMigrator::PreviewProjectMigration(
			project.Root.Path / "Project.tcproj", preview, error);
		Require(okPreview, "v9 preview failed: " + error);
		Require(preview.Scenes.size() == 1
			&& preview.Scenes[0].SourceSchemaVersion == 9,
			"v9 scene was not previewed");
		const bool okMigrate = TomCat::SceneMigrator::MigrateProjectScenes(
			project.Root.Path / "Project.tcproj", preview, error);
		Require(okMigrate, "v9 migration failed: " + error);
		uint32_t version = 0;
		Require(TomCat::SceneMigrator::InspectSceneFile(project.ScenePath,
			version, error) && version == 11, "v9 scene is not schema 11: "
			+ error);

		// Component data must survive: the sprite handle and the collider
		// sections from the sample scene reappear in the migrated document.
		const std::string migrated = ReadFileText(project.ScenePath);
		Require(migrated.find("Components:") != std::string::npos,
			"migrated scene lost the Components sequence");
		Require(migrated.find("SpriteHandle:") != std::string::npos,
			"migrated scene lost the sprite renderer data");
		Require(migrated.find("BoxCollider2D:") != std::string::npos,
			"migrated scene lost the collider data");
	}

	void TestMigrationPreservesComponentValues()
	{
		const std::string current = ReadFileText(FindSampleScene());
		TestProject project("values.tomcat", DowngradeScene(current, 10));

		// Collect every 64-bit sprite handle from the v10 fixture.
		YAML::Node before = YAML::Load(DowngradeScene(current, 10));
		std::vector<uint64_t> handlesBefore;
		for (const YAML::Node& entity : before["Entities"])
		{
			if (YAML::Node sprite = entity["SpriteRenderer"])
				handlesBefore.push_back(sprite["SpriteHandle"].as<uint64_t>());
		}
		Require(!handlesBefore.empty(), "fixture has no sprite handles");

		TomCat::SceneMigrationPreview preview;
		std::string error;
		const bool okPreview = TomCat::SceneMigrator::PreviewProjectMigration(
			project.Root.Path / "Project.tcproj", preview, error);
		Require(okPreview, "preview failed: " + error);
		const bool okMigrate = TomCat::SceneMigrator::MigrateProjectScenes(
			project.Root.Path / "Project.tcproj", preview, error);
		Require(okMigrate, "migration failed: " + error);

		YAML::Node after = YAML::Load(ReadFileText(project.ScenePath));
		std::vector<uint64_t> handlesAfter;
		for (const YAML::Node& entity : after["Entities"])
		{
			if (YAML::Node sprite = entity["SpriteRenderer"])
				handlesAfter.push_back(sprite["SpriteHandle"].as<uint64_t>());
		}
		Require(handlesBefore == handlesAfter,
			"sprite handles changed during migration");

		// The v11 document must mirror the sprite into the Components sequence
		// with the same stable identity as the built-in descriptor.
		bool foundInSequence = false;
		for (const YAML::Node& entity : after["Entities"])
		{
			for (const YAML::Node& component : entity["Components"])
			{
				if (component["StableName"].as<std::string>() == "TomCat.SpriteRenderer")
				{
					foundInSequence = true;
					Require(component["Properties"][0]["PropertyId"].IsDefined(),
						"mirrored sprite has no properties");
				}
			}
		}
		Require(foundInSequence, "the v11 Components sequence lost the sprite");
	}

	void TestInterruptedMigrationDetection()
	{
		const std::string current = ReadFileText(FindSampleScene());
		TemporaryDirectory directory;
		const std::filesystem::path backupRoot = directory.Path / "Backups";
		std::filesystem::create_directories(backupRoot);
		const std::filesystem::path scene = directory.Path / "crash.tomcat";
		const std::string original = DowngradeScene(current, 10);
		WriteFileText(scene, original);
		const std::string originalDigest = FileSHA256(scene);
		const std::string syntheticMigratedDigest(64, 'a');

		// Interrupted before install: scene still holds the original.
		WriteFileText(backupRoot / "crash.tomcat.aaaaaaaaaaaaaaaa.bak", original);
		WriteJournal(backupRoot, "crash.tomcat", "interrupt01", originalDigest,
			syntheticMigratedDigest, "crash.tomcat.aaaaaaaaaaaaaaaa.bak");
		TomCat::SceneMigrationRecovery recovery;
		std::string error;
		const bool okDetect = TomCat::SceneMigrator::PreviewInterruptedMigration(
			scene, backupRoot, recovery, error);
		Require(okDetect, "pre-install interruption was not detected: " + error);
		Require(recovery.RecommendedAction
			== TomCat::SceneMigrationRecoveryAction::FinishTransaction,
			"an untouched original should finish the transaction");
		const bool okFinish = TomCat::SceneMigrator::RecoverInterruptedMigration(
			scene, backupRoot, recovery, true, error);
		Require(okFinish, "finishing a pre-install transaction failed: " + error);
		Require(!std::filesystem::exists(backupRoot
			/ "crash.tomcat.migration-journal"), "journal survived finishing");
		Require(FileSHA256(scene) == originalDigest,
			"finishing touched the original file");

		// Interrupted after install: scene holds migrated bytes. Keeping the
		// current file removes the journal; restoring brings the original back.
		const std::string migrated = original + "# migrated\n";
		WriteJournal(backupRoot, "crash.tomcat", "interrupt02", originalDigest,
			FileSHA256FromText(migrated), "crash.tomcat.aaaaaaaaaaaaaaaa.bak");
		WriteFileText(scene, migrated);
		const bool okDetect2 = TomCat::SceneMigrator::PreviewInterruptedMigration(
			scene, backupRoot, recovery, error);
		Require(okDetect2, "post-install interruption was not detected: " + error);
		Require(recovery.RecommendedAction
			== TomCat::SceneMigrationRecoveryAction::KeepCurrent,
			"a migrated scene should recommend keeping the current file");
		const bool okKeep = TomCat::SceneMigrator::RecoverInterruptedMigration(
			scene, backupRoot, recovery, true, error);
		Require(okKeep, "keep-current recovery failed: " + error);
		Require(FileSHA256(scene) == FileSHA256FromText(migrated),
			"keep-current recovery changed the scene");

		// Restore the original for the final recovery branch.
		WriteJournal(backupRoot, "crash.tomcat", "interrupt03", originalDigest,
			FileSHA256FromText(migrated), "crash.tomcat.aaaaaaaaaaaaaaaa.bak");
		const bool okDetect3 = TomCat::SceneMigrator::PreviewInterruptedMigration(
			scene, backupRoot, recovery, error);
		Require(okDetect3, "third preview failed: " + error);
		const bool okRestore = TomCat::SceneMigrator::RecoverInterruptedMigration(
			scene, backupRoot, recovery, false, error);
		Require(okRestore, "restore-original recovery failed: " + error);
		Require(FileSHA256(scene) == originalDigest,
			"recovery did not restore the original bytes");
		Require(!std::filesystem::exists(backupRoot
			/ "crash.tomcat.migration-journal"),
			"recovery left the journal behind");
	}

	void TestStaleRecoveryApprovalIsRejected()
	{
		const std::string current = ReadFileText(FindSampleScene());
		TemporaryDirectory directory;
		const std::filesystem::path backupRoot = directory.Path / "Backups";
		std::filesystem::create_directories(backupRoot);
		const std::filesystem::path scene = directory.Path / "stale.tomcat";
		const std::string original = DowngradeScene(current, 10);
		WriteFileText(scene, original);
		const std::string originalDigest = FileSHA256(scene);
		const std::string migrated = original + "# migrated\n";
		WriteFileText(backupRoot / "stale.tomcat.aaaaaaaaaaaaaaaa.bak", original);
		WriteJournal(backupRoot, "stale.tomcat", "stale0001", originalDigest,
			FileSHA256FromText(migrated), "stale.tomcat.aaaaaaaaaaaaaaaa.bak");
		WriteFileText(scene, migrated);

		TomCat::SceneMigrationRecovery recovery;
		std::string error;
		const bool okPreview = TomCat::SceneMigrator::PreviewInterruptedMigration(
			scene, backupRoot, recovery, error);
		Require(okPreview, "preview failed: " + error);
		WriteFileText(scene, migrated + "# edited afterwards\n");
		const bool staleOk = TomCat::SceneMigrator::RecoverInterruptedMigration(
			scene, backupRoot, recovery, true, error);
		Require(!staleOk, "a stale recovery approval was accepted");
	}

	void TestJournalBlocksProjectMigration()
	{
		const std::string current = ReadFileText(FindSampleScene());
		TestProject project("journal.tomcat", DowngradeScene(current, 10));
		std::filesystem::create_directories(project.BackupRoot);
		WriteFileText(project.BackupRoot / "journal.tomcat.migration-journal",
			"JournalVersion: 1\nTransactionID: deadbeefdeadbeef\n"
			"State: Migrating\nScenePath: journal.tomcat\nOriginalSHA256: aa\n"
			"MigratedSHA256: bb\nBackupFile: journal.tomcat.aaaaaaaaaaaaaaaa.bak\n");
		TomCat::SceneMigrationPreview preview;
		std::string error;
		const bool okPreview = TomCat::SceneMigrator::PreviewProjectMigration(
			project.Root.Path / "Project.tcproj", preview, error);
		Require(!okPreview, "a pending journal did not block migration");
		Require(error.find("journal") != std::string::npos,
			"error did not name the journal");
	}

}

int main()
{
	// The serializer logs every decode; the engine convention requires every
	// entry point to initialize the core logger first.
	TomCat::Log::Init();
	try
	{
		TestInspectDetectsVersions();
		TestProjectMigrationUpgradeAndBackup();
		TestSchema9Migration();
		TestMigrationPreservesComponentValues();
		TestInterruptedMigrationDetection();
		TestStaleRecoveryApprovalIsRejected();
		TestJournalBlocksProjectMigration();
		std::cout << "PASS scene migration: schema detection, project preview "
			"with exact-plan approval, v9/v10 -> v11 upgrades, SHA-256 backups, "
			"component data preservation, interrupted-journal recovery and "
			"stale approval rejection\n";
		return 0;
	}
	catch (const std::exception& exception)
	{
		std::cerr << "FAIL scene migration regression: " << exception.what()
			<< '\n';
		return 1;
	}
}
