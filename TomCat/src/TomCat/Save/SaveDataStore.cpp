#include "tcpch.h"
#include "TomCat/Save/SaveDataStore.h"

#include "TomCat/Asset/ContentHash.h"
#include "TomCat/Core/ApplicationPaths.h"
#include "TomCat/Core/Version.h"
#include "TomCat/Utils/FileSystemUtils.h"
#include "TomCat/Utils/PathUtils.h"

#include <chrono>
#include <ctime>
#include <fstream>
#include <yaml-cpp/yaml.h>

namespace TomCat::Save {
	namespace {

		constexpr char Magic[5] = { 'T', 'C', 'S', 'A', 'V' };
		constexpr size_t HeaderBytes = 8;
		constexpr size_t EnvelopeLengthBytes = 4;
		constexpr size_t PayloadLengthBytes = 8;
		constexpr size_t DigestBytes = 32;
		constexpr size_t MaximumEnvelopeBytes = 4096;
		constexpr size_t MaximumFileBytes = HeaderBytes + EnvelopeLengthBytes
			+ MaximumEnvelopeBytes + PayloadLengthBytes + MaximumPayloadBytes
			+ DigestBytes;

		void AppendLittleEndian(std::vector<uint8_t>& bytes, uint64_t value,
			size_t width)
		{
			for (size_t index = 0; index < width; ++index)
			{
				bytes.push_back(static_cast<uint8_t>(value & 0xffu));
				value >>= 8;
			}
		}

		bool ReadLittleEndian(const uint8_t*& cursor, const uint8_t* end,
			size_t width, uint64_t& value)
		{
			if (static_cast<size_t>(end - cursor) < width)
				return false;
			value = 0;
			for (size_t index = 0; index < width; ++index)
				value |= static_cast<uint64_t>(*cursor++) << (8 * index);
			return true;
		}

		std::string FormatUtcTimestamp(int64_t unixSeconds)
		{
			const std::time_t time = static_cast<std::time_t>(unixSeconds);
			std::tm utc{};
#ifdef _WIN32
			gmtime_s(&utc, &time);
#else
			gmtime_r(&time, &utc);
#endif
			std::ostringstream stream;
			stream << std::put_time(&utc, "%Y-%m-%dT%H:%M:%SZ");
			return stream.str();
		}

		std::optional<int64_t> ParseUtcTimestamp(const std::string& text)
		{
			std::tm utc{};
			std::istringstream stream(text);
			stream >> std::get_time(&utc, "%Y-%m-%dT%H:%M:%SZ");
			if (stream.fail())
				return std::nullopt;
#ifdef _WIN32
			const std::time_t time = _mkgmtime(&utc);
#else
			const std::time_t time = timegm(&utc);
#endif
			if (time < 0)
				return std::nullopt;
			return static_cast<int64_t>(time);
		}

		std::string SerializeEnvelope(const SaveEnvelope& envelope)
		{
			YAML::Emitter emitter;
			emitter << YAML::BeginMap;
			emitter << YAML::Key << "SaveFormatVersion" << YAML::Value
				<< envelope.FormatVersion;
			emitter << YAML::Key << "Slot" << YAML::Value << envelope.Slot;
			emitter << YAML::Key << "DataVersion" << YAML::Value
				<< envelope.DataVersion;
			emitter << YAML::Key << "SavedAtUtc" << YAML::Value
				<< FormatUtcTimestamp(envelope.SavedAtUtcUnixSeconds);
			emitter << YAML::Key << "PayloadLength" << YAML::Value
				<< static_cast<uint64_t>(envelope.PayloadBytes);
			emitter << YAML::EndMap;
			return std::string(emitter.c_str(), emitter.size());
		}

		bool ParseEnvelope(std::span<const uint8_t> bytes, SaveEnvelope& outEnvelope)
		{
			const std::string text(reinterpret_cast<const char*>(bytes.data()),
				bytes.size());
			YAML::Node document;
			try
			{
				document = YAML::Load(text);
			}
			catch (const YAML::Exception&)
			{
				return false;
			}
			if (!document.IsMap())
				return false;
			static const char* RequiredKeys[] = { "SaveFormatVersion", "Slot",
				"DataVersion", "SavedAtUtc", "PayloadLength" };
			if (document.size() != std::size(RequiredKeys))
				return false;
			for (const char* key : RequiredKeys)
			{
				if (!document[key] || document[key].IsNull())
					return false;
			}

			uint64_t formatVersion = 0;
			uint64_t dataVersion = 0;
			uint64_t payloadBytes = 0;
			std::string slot;
			std::string savedAtUtc;
			try
			{
				formatVersion = document["SaveFormatVersion"].as<uint64_t>();
				slot = document["Slot"].as<std::string>();
				dataVersion = document["DataVersion"].as<uint64_t>();
				savedAtUtc = document["SavedAtUtc"].as<std::string>();
				payloadBytes = document["PayloadLength"].as<uint64_t>();
			}
			catch (const YAML::Exception&)
			{
				return false;
			}

			if (formatVersion == 0 || formatVersion > Version::SaveFormatCurrent
				|| dataVersion > std::numeric_limits<uint32_t>::max()
				|| payloadBytes > MaximumPayloadBytes)
				return false;
			const std::optional<int64_t> savedAt = ParseUtcTimestamp(savedAtUtc);
			if (!savedAt)
				return false;

			outEnvelope = {};
			outEnvelope.FormatVersion = static_cast<uint32_t>(formatVersion);
			outEnvelope.Slot = std::move(slot);
			outEnvelope.DataVersion = static_cast<uint32_t>(dataVersion);
			outEnvelope.SavedAtUtcUnixSeconds = *savedAt;
			outEnvelope.PayloadBytes = payloadBytes;
			return true;
		}

		std::vector<uint8_t> SerializeContainer(const SaveEnvelope& envelope,
			std::span<const uint8_t> payload)
		{
			std::string envelopeText = SerializeEnvelope(envelope);
			std::vector<uint8_t> bytes;
			bytes.reserve(HeaderBytes + EnvelopeLengthBytes + envelopeText.size()
				+ PayloadLengthBytes + payload.size() + DigestBytes);
			bytes.insert(bytes.end(), Magic, Magic + sizeof(Magic));
			bytes.push_back(static_cast<uint8_t>(Version::SaveFormatCurrent));
			bytes.push_back(0);
			bytes.push_back(0);
			AppendLittleEndian(bytes, envelopeText.size(), EnvelopeLengthBytes);
			bytes.insert(bytes.end(), envelopeText.begin(), envelopeText.end());
			AppendLittleEndian(bytes, payload.size(), PayloadLengthBytes);
			bytes.insert(bytes.end(), payload.begin(), payload.end());

			const ContentSHA256Digest digest = ComputeContentSHA256Digest(bytes);
			bytes.insert(bytes.end(), digest.begin(), digest.end());
			return bytes;
		}

		// Verifies the full container (digest, layout, envelope consistency) and
		// returns the payload. outputError distinguishes "absent" from "present
		// but invalid" only through present.
		bool ParseContainer(std::span<const uint8_t> bytes,
			SaveEnvelope& outEnvelope, std::vector<uint8_t>& outPayload)
		{
			if (bytes.size() < HeaderBytes + EnvelopeLengthBytes + DigestBytes)
				return false;
			if (std::memcmp(bytes.data(), Magic, sizeof(Magic)) != 0)
				return false;
			if (bytes[sizeof(Magic)] != Version::SaveFormatCurrent
				|| bytes[sizeof(Magic) + 1] != 0 || bytes[sizeof(Magic) + 2] != 0)
				return false;
			if (bytes.size() > MaximumFileBytes)
				return false;

			const ContentSHA256Digest digest = ComputeContentSHA256Digest(
				bytes.first(bytes.size() - DigestBytes));
			if (std::memcmp(digest.data(),
				bytes.data() + bytes.size() - DigestBytes, DigestBytes) != 0)
				return false;

			const uint8_t* cursor = bytes.data() + HeaderBytes;
			const uint8_t* end = bytes.data() + bytes.size() - DigestBytes;
			uint64_t envelopeLength = 0;
			if (!ReadLittleEndian(cursor, end, EnvelopeLengthBytes, envelopeLength)
				|| envelopeLength == 0 || envelopeLength > MaximumEnvelopeBytes
				|| static_cast<size_t>(end - cursor) < envelopeLength)
				return false;
			SaveEnvelope envelope;
			if (!ParseEnvelope({ cursor, static_cast<size_t>(envelopeLength) },
				envelope))
				return false;
			cursor += envelopeLength;

			uint64_t payloadLength = 0;
			if (!ReadLittleEndian(cursor, end, PayloadLengthBytes, payloadLength)
				|| payloadLength != envelope.PayloadBytes
				|| static_cast<uint64_t>(end - cursor) != payloadLength)
				return false;

			outEnvelope = std::move(envelope);
			outPayload.assign(cursor, end);
			return true;
		}

		bool ReadFileBytes(const std::filesystem::path& path,
			std::vector<uint8_t>& bytes, bool& present, std::string& error)
		{
			present = false;
			std::error_code code;
			if (!std::filesystem::exists(path, code))
				return true;
			present = true;
			std::ifstream input(path, std::ios::binary);
			if (!input)
			{
				error = "could not open " + PathToUTF8(path.filename());
				return false;
			}
			input.seekg(0, std::ios::end);
			const std::streamoff size = input.tellg();
			if (size < 0 || static_cast<uint64_t>(size) > MaximumFileBytes)
			{
				error = "save file has an invalid size: "
					+ PathToUTF8(path.filename());
				return false;
			}
			input.seekg(0, std::ios::beg);
			bytes.resize(static_cast<size_t>(size));
			if (!bytes.empty())
				input.read(reinterpret_cast<char*>(bytes.data()), size);
			if (!input)
			{
				error = "could not read " + PathToUTF8(path.filename());
				return false;
			}
			return true;
		}

		bool WriteBytesAtomically(const std::filesystem::path& destination,
			std::span<const uint8_t> bytes, std::string& error)
		{
			return FileSystem::WriteFileAtomically(destination,
				std::string_view(reinterpret_cast<const char*>(bytes.data()),
					bytes.size()),
				error);
		}

	}

	bool IsValidSlotName(std::string_view slot)
	{
		if (slot.empty() || slot.size() > MaximumSlotNameBytes)
			return false;
		for (const char character : slot)
		{
			const bool allowed = (character >= 'a' && character <= 'z')
				|| (character >= 'A' && character <= 'Z')
				|| (character >= '0' && character <= '9')
				|| character == '_' || character == '-';
			if (!allowed)
				return false;
		}
		return true;
	}

	std::string SlotFileName(std::string_view slot)
	{
		return std::string(slot) + ".tcsav";
	}

	SaveDataStore::SaveDataStore(std::filesystem::path directory)
		: m_Directory(std::move(directory))
	{
	}

	std::optional<SaveDataStore> SaveDataStore::ResolveForRunningGame(
		std::string& error)
	{
		const std::optional<std::filesystem::path> directory =
			ApplicationPaths::GetRuntimeSaveDirectory();
		if (!directory)
		{
			error = "game save directory is unavailable: no running game data "
				"paths were published";
			return std::nullopt;
		}
		return std::optional<SaveDataStore>(std::in_place, *directory);
	}

	const std::filesystem::path& SaveDataStore::GetDirectory() const
	{
		return m_Directory;
	}

	std::filesystem::path SaveDataStore::PrimaryPath(std::string_view slot) const
	{
		return m_Directory / UTF8ToPath(SlotFileName(slot));
	}

	std::filesystem::path SaveDataStore::BackupPath(std::string_view slot) const
	{
		return m_Directory / UTF8ToPath(SlotFileName(slot) + ".bak");
	}

	bool SaveDataStore::EnsureDirectory(std::string& error)
	{
		std::error_code code;
		if (std::filesystem::is_directory(m_Directory, code))
			return true;
		if (!std::filesystem::create_directories(m_Directory, code) || code)
		{
			error = "could not create save directory "
				+ PathToUTF8(m_Directory) + ": " + code.message();
			return false;
		}
		return true;
	}

	bool SaveDataStore::ReadFileIfVerified(const std::filesystem::path& path,
		SaveEnvelope& outEnvelope, std::vector<uint8_t>& outPayload,
		bool& present, std::string& error) const
	{
		std::vector<uint8_t> bytes;
		if (!ReadFileBytes(path, bytes, present, error))
			return false;
		if (!present)
			return false;
		if (!ParseContainer(bytes, outEnvelope, outPayload))
		{
			error = "save file failed validation: " + PathToUTF8(path.filename());
			return false;
		}
		return true;
	}

	bool SaveDataStore::Write(std::string_view slot, uint32_t dataVersion,
		std::span<const uint8_t> payload, std::string& error)
	{
		if (!IsValidSlotName(slot))
		{
			error = "invalid slot name";
			return false;
		}
		if (payload.size() > MaximumPayloadBytes)
		{
			error = "save payload exceeds the "
				+ std::to_string(MaximumPayloadBytes) + " byte limit";
			return false;
		}

		std::lock_guard lock(m_Mutex);
		if (!EnsureDirectory(error))
			return false;

		SaveEnvelope envelope;
		envelope.FormatVersion = Version::SaveFormatCurrent;
		envelope.Slot = std::string(slot);
		envelope.DataVersion = dataVersion;
		envelope.SavedAtUtcUnixSeconds = std::chrono::duration_cast<
			std::chrono::seconds>(std::chrono::system_clock::now()
				.time_since_epoch()).count();
		envelope.PayloadBytes = payload.size();

		const std::vector<uint8_t> container = SerializeContainer(envelope,
			payload);

		// Rotate the previous slot to the backup only when it is fully valid;
		// backups of corrupt files have no recovery value.
		{
			SaveEnvelope previousEnvelope;
			std::vector<uint8_t> previousPayload;
			std::vector<uint8_t> previousBytes;
			bool present = false;
			std::string rotateError;
			if (!ReadFileBytes(PrimaryPath(slot), previousBytes, present,
				rotateError))
				return false;
			if (present && ParseContainer(previousBytes, previousEnvelope,
				previousPayload))
			{
				if (!WriteBytesAtomically(BackupPath(slot), previousBytes,
					rotateError))
				{
					error = "could not rotate save backup: " + rotateError;
					return false;
				}
			}
		}

		return WriteBytesAtomically(PrimaryPath(slot), container, error);
	}

	SlotReadStatus SaveDataStore::ReadPrimary(std::string_view slot,
		SaveEnvelope& outEnvelope, std::vector<uint8_t>& outPayload,
		std::string& error)
	{
		if (!IsValidSlotName(slot))
		{
			error = "invalid slot name";
			return SlotReadStatus::Corrupted;
		}
		std::lock_guard lock(m_Mutex);
		bool present = false;
		return ReadFileIfVerified(PrimaryPath(slot), outEnvelope, outPayload,
			present, error)
			? SlotReadStatus::Ok
			: (present ? SlotReadStatus::Corrupted : SlotReadStatus::Missing);
	}

	SlotReadStatus SaveDataStore::Read(std::string_view slot,
		SaveEnvelope& outEnvelope, std::vector<uint8_t>& outPayload,
		std::string& error)
	{
		if (!IsValidSlotName(slot))
		{
			error = "invalid slot name";
			return SlotReadStatus::Corrupted;
		}
		std::lock_guard lock(m_Mutex);

		bool primaryPresent = false;
		std::string primaryError;
		if (ReadFileIfVerified(PrimaryPath(slot), outEnvelope, outPayload,
			primaryPresent, primaryError))
			return SlotReadStatus::Ok;

		bool backupPresent = false;
		std::string backupError;
		SaveEnvelope backupEnvelope;
		std::vector<uint8_t> backupPayload;
		std::vector<uint8_t> backupBytes;
		if (ReadFileBytes(BackupPath(slot), backupBytes, backupPresent,
			backupError))
		{
			if (backupPresent && ParseContainer(backupBytes, backupEnvelope,
				backupPayload))
			{
				outEnvelope = std::move(backupEnvelope);
				outPayload = std::move(backupPayload);
				// Heal the primary file from the verified backup so the next
				// read observes a consistent state; failure is non-fatal.
				std::string healError;
				(void)WriteBytesAtomically(PrimaryPath(slot), backupBytes,
					healError);
				error = "primary save was unreadable (" + primaryError
					+ "); recovered from backup";
				return SlotReadStatus::RecoveredFromBackup;
			}
		}

		if (!primaryPresent && !backupPresent)
		{
			error = "save slot does not exist";
			return SlotReadStatus::Missing;
		}
		error = primaryError;
		return SlotReadStatus::Corrupted;
	}

	bool SaveDataStore::Delete(std::string_view slot, bool& removed,
		std::string& error)
	{
		removed = false;
		if (!IsValidSlotName(slot))
		{
			error = "invalid slot name";
			return false;
		}
		std::lock_guard lock(m_Mutex);
		bool primaryRemoved = false;
		bool backupRemoved = false;
		if (!FileSystem::RemovePathSafely(PrimaryPath(slot), primaryRemoved, error))
			return false;
		if (!FileSystem::RemovePathSafely(BackupPath(slot), backupRemoved, error))
			return false;
		removed = primaryRemoved || backupRemoved;
		return true;
	}

	bool SaveDataStore::Exists(std::string_view slot) const
	{
		if (!IsValidSlotName(slot))
			return false;
		std::lock_guard lock(m_Mutex);
		std::error_code code;
		return std::filesystem::exists(PrimaryPath(slot), code)
			|| std::filesystem::exists(BackupPath(slot), code);
	}

	std::vector<SlotSummary> SaveDataStore::List(std::string& error) const
	{
		std::lock_guard lock(m_Mutex);
		std::vector<SlotSummary> slots;
		std::error_code code;
		if (!std::filesystem::is_directory(m_Directory, code))
			return slots;

		std::vector<std::filesystem::path> entries;
		std::filesystem::directory_iterator iterator(m_Directory, code);
		const std::filesystem::directory_iterator iteratorEnd;
		for (; !code && iterator != iteratorEnd; iterator.increment(code))
		{
			const std::filesystem::directory_entry& entry = *iterator;
			if (!entry.is_regular_file(code) || code)
				continue;
			const std::string name = PathToUTF8(entry.path().filename());
			if (name.size() <= std::string_view(".tcsav").size())
				continue;
			if (name.substr(name.size() - std::string_view(".tcsav").size())
				!= ".tcsav")
				continue;
			entries.push_back(entry.path());
		}
		if (code)
		{
			error = "could not enumerate save directory "
				+ PathToUTF8(m_Directory) + ": " + code.message();
			return slots;
		}

		std::sort(entries.begin(), entries.end());
		for (const std::filesystem::path& path : entries)
		{
			std::string name = PathToUTF8(path.filename());
			SlotSummary summary;
			summary.Slot = name.substr(0, name.size()
				- std::string_view(".tcsav").size());

			SaveEnvelope envelope;
			std::vector<uint8_t> payload;
			bool present = false;
			std::string readError;
			if (ReadFileIfVerified(path, envelope, payload, present, readError))
			{
				summary.FormatVersion = envelope.FormatVersion;
				summary.DataVersion = envelope.DataVersion;
				summary.SavedAtUtcUnixSeconds = envelope.SavedAtUtcUnixSeconds;
				summary.PayloadBytes = envelope.PayloadBytes;
			}
			else
			{
				summary.Corrupted = true;
			}
			slots.push_back(std::move(summary));
		}
		return slots;
	}

}
