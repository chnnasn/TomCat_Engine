#include <TomCat/Asset/ContentHash.h>
#include <TomCat/Core/Version.h>
#include <TomCat/Save/SaveDataStore.h>
#include <TomCat/Utils/FileSystemUtils.h>
#include <TomCat/Utils/PathUtils.h>

#include <algorithm>
#include <chrono>
#include <cstring>
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
				/ ("TomCat-SaveData-" + std::to_string(nonce));
			std::filesystem::create_directories(Path);
		}

		~TemporaryDirectory()
		{
			std::error_code error;
			std::filesystem::remove_all(Path, error);
		}

		std::filesystem::path Path;
	};

	std::vector<uint8_t> MakePayload(const std::string& text)
	{
		return { text.begin(), text.end() };
	}

	bool WriteFileRaw(const std::filesystem::path& path,
		std::span<const uint8_t> bytes)
	{
		std::ofstream output(path, std::ios::binary | std::ios::trunc);
		if (!output)
			return false;
		if (!bytes.empty())
			output.write(reinterpret_cast<const char*>(bytes.data()),
				static_cast<std::streamsize>(bytes.size()));
		return static_cast<bool>(output);
	}

	std::vector<uint8_t> ReadFileRaw(const std::filesystem::path& path)
	{
		std::ifstream input(path, std::ios::binary);
		if (!input)
			throw std::runtime_error("test could not read " + path.string());
		return { std::istreambuf_iterator<char>(input),
			std::istreambuf_iterator<char>() };
	}

	// Overwrites one byte inside the file payload region while preserving the
	// length; the container digest must reject it.
	void CorruptByte(const std::filesystem::path& path, size_t offset)
	{
		std::vector<uint8_t> bytes = ReadFileRaw(path);
		Require(offset < bytes.size(), "corruption offset out of range");
		bytes[offset] ^= 0x5a;
		Require(WriteFileRaw(path, bytes), "could not corrupt test file");
	}

	void TestSlotNameValidation()
	{
		Require(TomCat::Save::IsValidSlotName("slot1"), "plain name rejected");
		Require(TomCat::Save::IsValidSlotName("Auto-Save_2"),
			"mixed separators rejected");
		Require(TomCat::Save::IsValidSlotName(std::string(64, 'a')),
			"64 character name rejected");
		Require(!TomCat::Save::IsValidSlotName(""), "empty name accepted");
		Require(!TomCat::Save::IsValidSlotName(std::string(65, 'a')),
			"65 character name accepted");
		Require(!TomCat::Save::IsValidSlotName("../escape"), "traversal accepted");
		Require(!TomCat::Save::IsValidSlotName("a/b"), "slash accepted");
		Require(!TomCat::Save::IsValidSlotName("a\\b"), "backslash accepted");
		Require(!TomCat::Save::IsValidSlotName("slot 1"), "space accepted");
		Require(!TomCat::Save::IsValidSlotName(".hidden"), "dot name accepted");
		Require(!TomCat::Save::IsValidSlotName("\xE6\xA7\xBD\xE4\xBD\x8D"),
			"non-ASCII accepted");
	}

	void TestRoundTrip()
	{
		TemporaryDirectory directory;
		TomCat::Save::SaveDataStore store(directory.Path);
		const std::vector<uint8_t> payload = MakePayload("progress-1");

		std::string error;
		Require(store.Write("slot1", 7, payload, error),
			"write failed: " + error);
		Require(store.Exists("slot1"), "slot missing after write");

		TomCat::Save::SaveEnvelope envelope;
		std::vector<uint8_t> readPayload;
		const TomCat::Save::SlotReadStatus status = store.Read("slot1", envelope,
			readPayload, error);
		Require(status == TomCat::Save::SlotReadStatus::Ok,
			"round-trip read failed: " + error);
		Require(envelope.Slot == "slot1", "envelope slot mismatch");
		Require(envelope.DataVersion == 7, "envelope data version mismatch");
		Require(envelope.FormatVersion == TomCat::Version::SaveFormatCurrent,
			"envelope format version mismatch");
		Require(envelope.PayloadBytes == payload.size(),
			"envelope payload length mismatch");
		Require(readPayload == payload, "payload mismatch");
		Require(envelope.SavedAtUtcUnixSeconds > 1600000000
			&& envelope.SavedAtUtcUnixSeconds <= std::chrono::duration_cast<
				std::chrono::seconds>(std::chrono::system_clock::now()
					.time_since_epoch()).count(),
			"envelope timestamp out of range");

		// Empty payloads are legal.
		Require(store.Write("empty", 0, {}, error), "empty write failed: "
			+ error);
		TomCat::Save::SaveEnvelope emptyEnvelope;
		std::vector<uint8_t> emptyPayload;
		Require(store.Read("empty", emptyEnvelope, emptyPayload, error)
			== TomCat::Save::SlotReadStatus::Ok, "empty read failed: " + error);
		Require(emptyPayload.empty(), "empty payload not empty");
	}

	void TestBackupRotationAndRecovery()
	{
		TemporaryDirectory directory;
		TomCat::Save::SaveDataStore store(directory.Path);
		std::string error;

		Require(store.Write("slot", 1, MakePayload("first"), error),
			"first write failed: " + error);
		Require(store.Write("slot", 2, MakePayload("second"), error),
			"second write failed: " + error);

		TomCat::Save::SaveEnvelope envelope;
		std::vector<uint8_t> payload;
		Require(store.Read("slot", envelope, payload, error)
			== TomCat::Save::SlotReadStatus::Ok, "read after rewrite failed");
		Require(envelope.DataVersion == 2 && payload == MakePayload("second"),
			"rewrite did not replace the primary slot");

		const std::filesystem::path primary = directory.Path / "slot.tcsav";
		const std::filesystem::path backup = directory.Path / "slot.tcsav.bak";
		Require(std::filesystem::is_regular_file(backup),
			"backup was not rotated in");

		// Delete the verified primary: the backup must answer the read and heal
		// the primary file back to the older version.
		const std::vector<uint8_t> backupBytes = ReadFileRaw(backup);
		std::error_code removeError;
		std::filesystem::remove(primary, removeError);
		TomCat::Save::SaveEnvelope backupEnvelope;
		const TomCat::Save::SlotReadStatus recovered = store.Read("slot",
			backupEnvelope, payload, error);
		Require(recovered == TomCat::Save::SlotReadStatus::RecoveredFromBackup,
			"backup was not used: " + error);
		Require(backupEnvelope.DataVersion == 1
			&& payload == MakePayload("first"), "backup content mismatch");
		Require(ReadFileRaw(primary) == backupBytes,
			"primary was not healed from backup");
		Require(store.ReadPrimary("slot", backupEnvelope, payload, error)
			== TomCat::Save::SlotReadStatus::Ok,
			"healed primary failed verification");
	}

	void TestCorruptionDetection()
	{
		TemporaryDirectory directory;
		TomCat::Save::SaveDataStore store(directory.Path);
		std::string error;
		Require(store.Write("slot", 4, MakePayload("payload-bytes"), error),
			"write failed: " + error);
		const std::filesystem::path primary = directory.Path / "slot.tcsav";
		const std::vector<uint8_t> goodBytes = ReadFileRaw(primary);

		// Flip one payload byte: digest verification must fail and there is no
		// usable backup yet, so the read is Corrupted.
		CorruptByte(primary, goodBytes.size() - 33);
		TomCat::Save::SaveEnvelope envelope;
		std::vector<uint8_t> payload;
		Require(store.ReadPrimary("slot", envelope, payload, error)
			== TomCat::Save::SlotReadStatus::Corrupted,
			"payload corruption was not detected");
		Require(store.Read("slot", envelope, payload, error)
			== TomCat::Save::SlotReadStatus::Corrupted,
			"corruption without backup was not reported");

		// Restore, then corrupt the envelope header bytes instead.
		Require(WriteFileRaw(primary, goodBytes), "restore failed");
		CorruptByte(primary, 12);
		Require(store.Read("slot", envelope, payload, error)
			== TomCat::Save::SlotReadStatus::Corrupted,
			"envelope corruption was not detected");

		// A truncated file is corruption, not a crash.
		Require(WriteFileRaw(primary,
			std::span(goodBytes).first(goodBytes.size() / 2)), "truncation failed");
		Require(store.Read("slot", envelope, payload, error)
			== TomCat::Save::SlotReadStatus::Corrupted,
			"truncation was not detected");

		// A missing slot reads as Missing.
		Require(store.Read("absent", envelope, payload, error)
			== TomCat::Save::SlotReadStatus::Missing,
			"absent slot was not reported Missing");
	}

	void TestRecoveryPrefersBackup()
	{
		TemporaryDirectory directory;
		TomCat::Save::SaveDataStore store(directory.Path);
		std::string error;
		Require(store.Write("slot", 1, MakePayload("older"), error),
			"first write failed: " + error);
		Require(store.Write("slot", 2, MakePayload("newer"), error),
			"second write failed: " + error);
		const std::filesystem::path primary = directory.Path / "slot.tcsav";
		CorruptByte(primary, 24);

		TomCat::Save::SaveEnvelope envelope;
		std::vector<uint8_t> payload;
		Require(store.Read("slot", envelope, payload, error)
			== TomCat::Save::SlotReadStatus::RecoveredFromBackup,
			"corrupt primary did not fall back: " + error);
		Require(envelope.DataVersion == 1 && payload == MakePayload("older"),
			"backup did not carry the older version");
	}

	void TestListAndDelete()
	{
		TemporaryDirectory directory;
		TomCat::Save::SaveDataStore store(directory.Path);
		std::string error;
		Require(store.Write("alpha", 1, MakePayload("a"), error),
			"alpha write failed: " + error);
		Require(store.Write("beta", 2, MakePayload("b"), error),
			"beta write failed: " + error);
		Require(WriteFileRaw(directory.Path / "broken.tcsav", MakePayload("junk")),
			"could not plant a corrupt slot");
		Require(WriteFileRaw(directory.Path / "ignore.tcsav.bak", MakePayload("b")),
			"could not plant a backup file");

		const std::vector<TomCat::Save::SlotSummary> slots = store.List(error);
		Require(error.empty(), "list failed: " + error);
		Require(slots.size() == 3, "unexpected slot count");
		Require(slots[0].Slot == "alpha" && slots[1].Slot == "beta"
			&& slots[2].Slot == "broken", "unexpected slot order");
		Require(!slots[0].Corrupted && slots[0].DataVersion == 1
			&& slots[0].PayloadBytes == 1, "alpha summary mismatch");
		Require(slots[2].Corrupted, "corrupt slot was not flagged");
		Require(std::none_of(slots.begin(), slots.end(),
			[](const TomCat::Save::SlotSummary& summary)
			{
				return summary.Slot == "ignore";
			}), "backup file leaked into the slot listing");

		bool removed = false;
		Require(store.Delete("alpha", removed, error), "delete failed: " + error);
		Require(removed, "delete reported nothing removed");
		Require(!store.Exists("alpha"), "slot survived deletion");
		Require(!std::filesystem::exists(directory.Path / "alpha.tcsav.bak"),
			"backup survived deletion");
		Require(store.Delete("alpha", removed, error) && !removed,
			"deleting an absent slot must succeed without removal");
	}

	void TestLimitsAndRejections()
	{
		TemporaryDirectory directory;
		TomCat::Save::SaveDataStore store(directory.Path);
		std::string error;
		Require(!store.Write("../escape", 1, {}, error),
			"traversal slot name accepted");
		Require(!store.Exists("../escape"), "invalid slot created a file");

		std::vector<uint8_t> oversized(
			TomCat::Save::MaximumPayloadBytes + 1, 0xab);
		Require(!store.Write("big", 1, oversized, error),
			"oversized payload accepted");
	}

}

int main()
{
	try
	{
		TestSlotNameValidation();
		TestRoundTrip();
		TestBackupRotationAndRecovery();
		TestCorruptionDetection();
		TestRecoveryPrefersBackup();
		TestListAndDelete();
		TestLimitsAndRejections();
		std::cout << "PASS game save slots: validation, atomic writes, SHA-256 "
			"envelopes, backup rotation and recovery, corruption detection, "
			"listing and deletion\n";
		return 0;
	}
	catch (const std::exception& exception)
	{
		std::cerr << "FAIL save data regression: " << exception.what() << '\n';
		return 1;
	}
}
