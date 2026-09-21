#include "tcpch.h"
#include "AudioClip.h"

#include <algorithm>
#include <array>
#include <cstring>
#include <limits>

namespace TomCat {

	namespace {

		constexpr uint64_t MaximumDecodedAudioBytes = 512ULL * 1024ULL * 1024ULL;
		constexpr uint32_t MaximumSampleRate = 384000;
		constexpr uint16_t WaveFormatPcm = 0x0001;
		constexpr uint16_t WaveFormatIeeeFloat = 0x0003;
		constexpr uint16_t WaveFormatExtensible = 0xfffe;

		uint16_t ReadU16(const uint8_t* value)
		{
			return static_cast<uint16_t>(value[0])
				| static_cast<uint16_t>(value[1] << 8);
		}

		uint32_t ReadU32(const uint8_t* value)
		{
			return static_cast<uint32_t>(value[0])
				| (static_cast<uint32_t>(value[1]) << 8)
				| (static_cast<uint32_t>(value[2]) << 16)
				| (static_cast<uint32_t>(value[3]) << 24);
		}

		bool FourCC(const uint8_t* value, const char* expected)
		{
			return std::memcmp(value, expected, 4) == 0;
		}

		Ref<AudioClip> Fail(AudioDecodeStatus value, std::string message,
			std::string_view sourceName, std::string& error,
			AudioDecodeStatus* status)
		{
			if (!sourceName.empty())
				message = std::string(sourceName) + ": " + message;
			error = std::move(message);
			if (status)
				*status = value;
			return {};
		}

		bool IsWaveExtensibleSubformat(const uint8_t* guid, uint16_t format)
		{
			// KSDATAFORMAT_SUBTYPE_PCM / IEEE_FLOAT: first two bytes contain the
			// classic format tag and the remaining GUID bytes are identical.
			static constexpr std::array<uint8_t, 14> tail = {
				0x00, 0x00, 0x00, 0x00, 0x10, 0x00, 0x80,
				0x00, 0x00, 0xaa, 0x00, 0x38, 0x9b, 0x71 };
			if (ReadU16(guid) != format)
				return false;
			return std::memcmp(guid + 2, tail.data(), tail.size()) == 0;
		}

	}

	Ref<AudioClip> AudioClip::Decode(std::span<const uint8_t> bytes,
		std::string& error, AudioDecodeStatus* status, std::string_view sourceName)
	{
		error.clear();
		if (status)
			*status = AudioDecodeStatus::MalformedData;
		if (bytes.empty())
			return Fail(AudioDecodeStatus::EmptyData, "audio data is empty",
				sourceName, error, status);
		if (bytes.size() >= 4 && FourCC(bytes.data(), "OggS"))
			return Fail(AudioDecodeStatus::UnsupportedEncoding,
				"OGG/Vorbis decoding is not available in this build",
				sourceName, error, status);
		if (bytes.size() < 12 || !FourCC(bytes.data(), "RIFF")
			|| !FourCC(bytes.data() + 8, "WAVE"))
			return Fail(AudioDecodeStatus::UnsupportedContainer,
				"only RIFF/WAVE audio is supported", sourceName, error, status);

		const uint64_t declaredEnd = 8ULL + ReadU32(bytes.data() + 4);
		if (declaredEnd < 12 || declaredEnd > bytes.size())
			return Fail(AudioDecodeStatus::MalformedData,
				"RIFF size exceeds the available data", sourceName, error, status);

		const uint8_t* format = nullptr;
		uint32_t formatSize = 0;
		const uint8_t* pcm = nullptr;
		uint32_t pcmSize = 0;
		uint64_t cursor = 12;
		while (cursor + 8 <= declaredEnd)
		{
			const uint8_t* header = bytes.data() + cursor;
			const uint32_t chunkSize = ReadU32(header + 4);
			const uint64_t payload = cursor + 8;
			const uint64_t chunkEnd = payload + chunkSize;
			if (chunkEnd < payload || chunkEnd > declaredEnd)
				return Fail(AudioDecodeStatus::MalformedData,
					"WAV chunk exceeds the RIFF boundary", sourceName, error, status);
			if (FourCC(header, "fmt ") && !format)
			{
				format = bytes.data() + payload;
				formatSize = chunkSize;
			}
			else if (FourCC(header, "data") && !pcm)
			{
				pcm = bytes.data() + payload;
				pcmSize = chunkSize;
			}
			cursor = chunkEnd + (chunkSize & 1U);
			if (cursor > declaredEnd)
				return Fail(AudioDecodeStatus::MalformedData,
					"WAV chunk padding exceeds the RIFF boundary", sourceName, error, status);
		}

		if (!format || formatSize < 16 || !pcm)
			return Fail(AudioDecodeStatus::MalformedData,
				"WAV requires fmt and data chunks", sourceName, error, status);
		if (pcmSize == 0)
			return Fail(AudioDecodeStatus::EmptyData,
				"WAV data chunk is empty", sourceName, error, status);
		if (pcmSize > MaximumDecodedAudioBytes)
			return Fail(AudioDecodeStatus::LimitExceeded,
				"decoded audio exceeds the 512 MiB limit", sourceName, error, status);

		uint16_t encoding = ReadU16(format);
		const uint16_t channels = ReadU16(format + 2);
		const uint32_t sampleRate = ReadU32(format + 4);
		const uint32_t byteRate = ReadU32(format + 8);
		const uint16_t blockAlign = ReadU16(format + 12);
		const uint16_t bitsPerSample = ReadU16(format + 14);
		if (encoding == WaveFormatExtensible)
		{
			if (formatSize < 40 || ReadU16(format + 16) < 22)
				return Fail(AudioDecodeStatus::MalformedData,
					"WAVE_FORMAT_EXTENSIBLE header is incomplete",
					sourceName, error, status);
			const uint8_t* subformat = format + 24;
			if (IsWaveExtensibleSubformat(subformat, WaveFormatPcm))
				encoding = WaveFormatPcm;
			else if (IsWaveExtensibleSubformat(subformat, WaveFormatIeeeFloat))
				encoding = WaveFormatIeeeFloat;
			else
				return Fail(AudioDecodeStatus::UnsupportedEncoding,
					"WAV extensible subformat is unsupported", sourceName, error, status);
		}
		if (encoding != WaveFormatPcm && encoding != WaveFormatIeeeFloat)
			return Fail(AudioDecodeStatus::UnsupportedEncoding,
				"compressed WAV encoding is unsupported", sourceName, error, status);
		if (channels == 0 || channels > 8 || sampleRate == 0
			|| sampleRate > MaximumSampleRate)
			return Fail(AudioDecodeStatus::UnsupportedEncoding,
				"WAV channel count or sample rate is unsupported",
				sourceName, error, status);
		const bool floatingPoint = encoding == WaveFormatIeeeFloat;
		const bool validBits = floatingPoint ? bitsPerSample == 32
			: (bitsPerSample == 8 || bitsPerSample == 16
				|| bitsPerSample == 24 || bitsPerSample == 32);
		if (!validBits || bitsPerSample % 8 != 0)
			return Fail(AudioDecodeStatus::UnsupportedEncoding,
				"WAV sample bit depth is unsupported", sourceName, error, status);
		const uint32_t expectedBlockAlign = static_cast<uint32_t>(channels)
			* (bitsPerSample / 8U);
		const uint64_t expectedByteRate = static_cast<uint64_t>(sampleRate)
			* expectedBlockAlign;
		if (blockAlign != expectedBlockAlign || byteRate != expectedByteRate
			|| pcmSize % blockAlign != 0)
			return Fail(AudioDecodeStatus::MalformedData,
				"WAV format alignment is inconsistent", sourceName, error, status);

		Ref<AudioClip> clip = CreateRef<AudioClip>();
		clip->m_Channels = channels;
		clip->m_SampleRate = sampleRate;
		clip->m_BitsPerSample = bitsPerSample;
		clip->m_BlockAlign = blockAlign;
		clip->m_AverageBytesPerSecond = byteRate;
		clip->m_FloatingPoint = floatingPoint;
		clip->m_PcmBytes.assign(pcm, pcm + pcmSize);
		if (status)
			*status = AudioDecodeStatus::Success;
		return clip;
	}

}
