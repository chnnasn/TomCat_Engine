#pragma once

#include <cstdint>
#include <filesystem>
#include <mutex>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace TomCat::Save {

	// Persisted game save slots. The on-disk container is owned by the engine;
	// the payload is opaque game data produced by the scripting layer or a
	// native game module. The envelope (YAML) records the identity and version
	// metadata games need to migrate older payloads on load.
	//
	// Container layout (little-endian):
	//   [0..4]  ASCII "TCSAV"
	//   [5]     format version byte (Version::SaveFormatCurrent)
	//   [6..7]  reserved, zero
	//   [8..11] uint32 envelope length
	//   [..]    envelope YAML (SaveFormatVersion, Slot, DataVersion,
	//           SavedAtUtc, PayloadLength)
	//   [..]    uint64 payload length
	//   [..]    payload bytes
	//   [..]    SHA-256 digest of every preceding byte
	//
	// Writes are atomic (sibling temporary + replace) and rotate the previous
	// verified slot file to "<slot>.tcsav.bak". Reads verify the digest and
	// fall back to the backup when the primary file is missing or corrupt,
	// healing the primary file from the backup on success.

	inline constexpr size_t MaximumSlotNameBytes = 64;
	inline constexpr size_t MaximumPayloadBytes = 16u * 1024u * 1024u;

	enum class SlotReadStatus : int32_t
	{
		Ok = 0,
		RecoveredFromBackup = 1,
		Missing = 2,
		Corrupted = 3
	};

	struct SaveEnvelope
	{
		uint32_t FormatVersion = 0;
		std::string Slot;
		uint32_t DataVersion = 0;
		int64_t SavedAtUtcUnixSeconds = 0;
		uint64_t PayloadBytes = 0;

		bool operator==(const SaveEnvelope&) const = default;
	};

	struct SlotSummary
	{
		std::string Slot;
		uint32_t FormatVersion = 0;
		uint32_t DataVersion = 0;
		int64_t SavedAtUtcUnixSeconds = 0;
		uint64_t PayloadBytes = 0;
		bool Corrupted = false;
	};

	[[nodiscard]] bool IsValidSlotName(std::string_view slot);
	[[nodiscard]] std::string SlotFileName(std::string_view slot);

	class SaveDataStore
	{
	public:
		// The directory is created lazily on first write; reads never create it.
		explicit SaveDataStore(std::filesystem::path directory);

		// Resolves the save directory published by the running Player (or an
		// editor Play session). Fails when game data paths were never published.
		[[nodiscard]] static std::optional<SaveDataStore> ResolveForRunningGame(
			std::string& error);

		[[nodiscard]] const std::filesystem::path& GetDirectory() const;

		// Atomically installs a new slot file, rotating the previous verified
		// file to the backup. dataVersion is opaque to the store and travels
		// with the slot so games can migrate payloads across versions.
		[[nodiscard]] bool Write(std::string_view slot, uint32_t dataVersion,
			std::span<const uint8_t> payload, std::string& error);

		// Reads and verifies the slot, falling back to the rotated backup and
		// healing the primary file when the backup wins.
		[[nodiscard]] SlotReadStatus Read(std::string_view slot,
			SaveEnvelope& outEnvelope, std::vector<uint8_t>& outPayload,
			std::string& error);

		// Deletes the slot and its backup. removed is true when either file
		// was deleted; deleting an absent slot is a successful no-op.
		[[nodiscard]] bool Delete(std::string_view slot, bool& removed,
			std::string& error);

		[[nodiscard]] bool Exists(std::string_view slot) const;

		// Every *.tcsav file in the directory. Entries whose container fails
		// validation are reported with Corrupted set and zeroed metadata.
		[[nodiscard]] std::vector<SlotSummary> List(std::string& error) const;

		// Reads and verifies the slot file without backup fallback. Exposed for
		// diagnostics and regression coverage of the recovery path.
		[[nodiscard]] SlotReadStatus ReadPrimary(std::string_view slot,
			SaveEnvelope& outEnvelope, std::vector<uint8_t>& outPayload,
			std::string& error);

	private:
		[[nodiscard]] std::filesystem::path PrimaryPath(std::string_view slot) const;
		[[nodiscard]] std::filesystem::path BackupPath(std::string_view slot) const;
		[[nodiscard]] bool EnsureDirectory(std::string& error);
		[[nodiscard]] bool ReadFileIfVerified(const std::filesystem::path& path,
			SaveEnvelope& outEnvelope, std::vector<uint8_t>& outPayload,
			bool& present, std::string& error) const;

		std::filesystem::path m_Directory;
		mutable std::mutex m_Mutex;
	};

}
