#include <TomCat/Editor/EditorRecoveryService.h>
#include <TomCat/Editor/SceneHistory.h>
#include <TomCat/Utils/PathUtils.h>

#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>

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
			const auto nonce = std::chrono::steady_clock::now()
				.time_since_epoch().count();
			Path = std::filesystem::temp_directory_path()
				/ ("TomCat-EditorRecovery-" + std::to_string(nonce));
			std::filesystem::create_directories(Path);
		}

		~TemporaryDirectory()
		{
			std::error_code error;
			std::filesystem::remove_all(Path, error);
		}

		std::filesystem::path Path;
	};

	void WriteText(const std::filesystem::path& path,
		const std::string& contents)
	{
		std::filesystem::create_directories(path.parent_path());
		std::ofstream output(path, std::ios::binary | std::ios::trunc);
		output.write(contents.data(), static_cast<std::streamsize>(contents.size()));
		output.close();
		Require(static_cast<bool>(output), "could not write regression fixture");
	}

	std::string ReadText(const std::filesystem::path& path)
	{
		std::ifstream input(path, std::ios::binary);
		Require(static_cast<bool>(input), "could not open regression fixture");
		std::string contents((std::istreambuf_iterator<char>(input)),
			std::istreambuf_iterator<char>());
		Require(!input.bad(), "could not read regression fixture");
		return contents;
	}

	void TestHistoryBranchAndSelection()
	{
		TomCat::SceneHistory history;
		Require(history.Reset("scene-A", 101, true), "history reset failed");
		const auto saved = history.GetCurrentStateId();
		Require(!history.IsDirty(), "initial saved state is dirty");

		Require(history.BeginTransaction("Inspector drag"),
			"could not begin first transaction");
		Require(history.CommitTransaction("scene-B", 202),
			"could not commit first transaction");
		const auto branchBase = history.GetCurrentStateId();
		Require(branchBase > saved && history.IsDirty(),
			"committed edit did not advance dirty state");

		history.MarkSaved();
		Require(!history.IsDirty(), "MarkSaved did not track the explicit state id");
		Require(history.BeginTransaction("Gizmo drag"), "could not begin gizmo edit");
		Require(history.CommitTransaction("scene-C", 303),
			"could not commit gizmo edit");
		Require(history.IsDirty(), "post-save edit is not dirty");

		uint64_t restoredSelection = 0;
		Require(history.Undo([&](const TomCat::SceneHistory::Snapshot& snapshot)
		{
			restoredSelection = snapshot.SelectedEntity;
			return snapshot.Archive && *snapshot.Archive == "scene-B";
		}), "undo did not restore the saved snapshot");
		Require(restoredSelection == 202,
			"undo did not restore selection by stable UUID");
		Require(!history.IsDirty(),
			"undoing to savedStateId did not clear dirty state");
		Require(history.CanRedo(), "undo did not expose redo");

		Require(history.BeginTransaction("Branched edit"),
			"could not begin branch transaction");
		Require(history.CommitTransaction("scene-D", 404),
			"could not commit branch transaction");
		Require(!history.CanRedo(), "new edit after undo did not truncate redo");
		Require(history.GetCurrentStateId() > branchBase,
			"branched state id was reused");
	}

	void TestHistoryMemoryCap()
	{
		TomCat::SceneHistory history({ 3, 40 });
		Require(history.Reset(std::string(10, '0'), 0, false),
			"capped history reset failed");
		for (int index = 1; index <= 10; ++index)
		{
			Require(history.BeginTransaction("edit"), "capped transaction begin failed");
			Require(history.CommitTransaction(std::string(10,
				static_cast<char>('0' + index)), static_cast<uint64_t>(index)),
				"capped transaction commit failed");
		}
		Require(history.GetEntryCount() <= 3,
			"history exceeded its entry limit");
		Require(history.GetMemoryUsage() <= 40,
			"history exceeded its byte budget");
		Require(history.GetCurrentSnapshot()
			&& history.GetCurrentSnapshot()->SelectedEntity == 10,
			"history eviction removed the current state");
	}

	void TestImmutableAsyncRecovery()
	{
		TemporaryDirectory temporary;
		const std::filesystem::path productRoot = temporary.Path / "Editor";
		const std::filesystem::path project =
			temporary.Path / "Project" / "Project.tcproj";
		const std::filesystem::path scene =
			temporary.Path / "Project" / "Assets" / "Main.tomcat";
		WriteText(project, "project");
		WriteText(scene, "source-scene");
		std::filesystem::last_write_time(scene,
			std::filesystem::file_time_type::clock::now()
				- std::chrono::seconds(5));

		TomCat::EditorRecoveryService recovery(productRoot);
		std::string error;
		Require(recovery.Configure(project, error),
			"recovery configure failed: " + error);
		std::string mutableScene = "committed-scene";
		auto immutable =
			std::make_shared<const std::string>(mutableScene);
		Require(recovery.ScheduleAutosave(scene, 42, 9001, immutable, error),
			"autosave schedule failed: " + error);
		mutableScene = "mutated-after-schedule";
		immutable.reset();
		Require(recovery.Flush(error), "autosave flush failed: " + error);
		Require(ReadText(scene) == "source-scene",
			"recovery write overwrote the source scene");

		auto candidate = recovery.FindRecovery(scene, error);
		Require(candidate.has_value(), "newer differing recovery was not detected");
		Require(candidate->Archive
			&& *candidate->Archive == "committed-scene",
			"background writer observed mutable caller state");
		Require(candidate->StateId == 42 && candidate->SelectedEntity == 9001,
			"recovery metadata did not round-trip");
		Require(ReadText(scene) == "source-scene",
			"recovery detection modified the source scene");

		const std::filesystem::path recoveryPath =
			recovery.GetRecoveryPath(scene);
		std::filesystem::last_write_time(recoveryPath,
			std::filesystem::last_write_time(scene)
				- std::chrono::seconds(1));
		error.clear();
		Require(!recovery.FindRecovery(scene, error) && error.empty(),
			"an older recovery was offered");

		auto sameAsSource =
			std::make_shared<const std::string>("source-scene");
		Require(recovery.ScheduleAutosave(scene, 43, 9001,
			sameAsSource, error), "same-content autosave schedule failed");
		Require(recovery.Flush(error), "same-content autosave flush failed");
		Require(!recovery.FindRecovery(scene, error) && error.empty(),
			"a recovery identical to the source scene was offered");

		auto untitled =
			std::make_shared<const std::string>("untitled-committed");
		Require(recovery.ScheduleAutosave({}, 44, 77, untitled, error),
			"untitled autosave schedule failed: " + error);
		Require(recovery.Flush(error), "untitled autosave flush failed: " + error);
		auto untitledCandidate = recovery.FindRecovery({}, error);
		Require(untitledCandidate && untitledCandidate->Archive
			&& *untitledCandidate->Archive == "untitled-committed",
			"untitled recovery is unsupported");
	}

	void TestProjectLockStaleAndLive()
	{
		TemporaryDirectory temporary;
		const std::filesystem::path productRoot = temporary.Path / "Editor";
		const std::filesystem::path project =
			temporary.Path / "Project" / "Project.tcproj";
		WriteText(project, "project");

		std::string error;
		TomCat::EditorProjectLock first;
		Require(first.Acquire(project, error, productRoot)
			== TomCat::ProjectLockAcquireResult::Acquired,
			"first project lock failed: " + error);
		const TomCat::ProjectLockRecord record = first.GetRecord();
		Require(record.CanonicalProjectPath
				== TomCat::EditorProjectLock::CanonicalizePath(project)
			&& record.ProcessId != 0 && record.ProcessStart != 0
			&& !record.Token.empty(),
			"lock does not contain canonical path/PID/start/token");
		TomCat::ProjectLockRecord persisted;
		Require(TomCat::EditorProjectLock::ReadRecord(
			first.GetLockPath(), persisted, error)
			&& persisted.CanonicalProjectPath == record.CanonicalProjectPath
			&& persisted.ProcessId == record.ProcessId
			&& persisted.ProcessStart == record.ProcessStart
			&& persisted.Token == record.Token,
			"lock identity was not persisted beside the project key");

		TomCat::EditorProjectLock second;
		Require(second.Acquire(project, error, productRoot)
			== TomCat::ProjectLockAcquireResult::LiveOwner,
			"a live lock allowed a second writer");
		first.Release();

		TomCat::ProjectLockRecord stale;
		stale.CanonicalProjectPath =
			TomCat::EditorProjectLock::CanonicalizePath(project);
		stale.ProcessId = (std::numeric_limits<uint64_t>::max)();
		stale.ProcessStart = 1;
		stale.Token = "stale-owner";
		const std::filesystem::path lockPath =
			TomCat::EditorProjectLock::ResolveLockPath(productRoot, project);
		WriteText(lockPath, TomCat::EditorProjectLock::SerializeRecord(stale));

		TomCat::EditorProjectLock replacement;
		Require(replacement.Acquire(project, error, productRoot)
			== TomCat::ProjectLockAcquireResult::Acquired,
			"stale lock was not replaced safely: " + error);
		Require(replacement.OwnsProject(project),
			"replacement lock does not own the canonical project");
	}

}

int main()
{
	try
	{
		TestHistoryBranchAndSelection();
		TestHistoryMemoryCap();
		TestImmutableAsyncRecovery();
		TestProjectLockStaleAndLive();
		std::cout << "PASS Editor recovery: history branching/cap/saved state, "
			"immutable autosave, non-destructive recovery, and stale/live locks\n";
		return 0;
	}
	catch (const std::exception& exception)
	{
		std::cerr << "FAIL Editor recovery regression: "
			<< exception.what() << '\n';
		return 1;
	}
}
