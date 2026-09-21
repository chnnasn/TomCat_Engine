#pragma once

#include "TomCat/Core/Base.h"

#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace TomCat {

	enum class AudioDecodeStatus : uint8_t
	{
		Success = 0,
		EmptyData,
		UnsupportedContainer,
		MalformedData,
		UnsupportedEncoding,
		LimitExceeded
	};

	// Immutable, interleaved PCM data ready for a platform audio voice. WAV is
	// deliberately decoded without a third-party runtime dependency. OGG is
	// reported as unsupported until a real decoder is shipped.
	class AudioClip final
	{
	public:
		static Ref<AudioClip> Decode(std::span<const uint8_t> bytes,
			std::string& error, AudioDecodeStatus* status = nullptr,
			std::string_view sourceName = {});

		uint16_t GetChannels() const { return m_Channels; }
		uint32_t GetSampleRate() const { return m_SampleRate; }
		uint16_t GetBitsPerSample() const { return m_BitsPerSample; }
		uint16_t GetBlockAlign() const { return m_BlockAlign; }
		uint32_t GetAverageBytesPerSecond() const { return m_AverageBytesPerSecond; }
		bool IsFloatingPoint() const { return m_FloatingPoint; }
		uint64_t GetFrameCount() const
		{
			return m_BlockAlign == 0 ? 0 : m_PcmBytes.size() / m_BlockAlign;
		}
		double GetDurationSeconds() const
		{
			return m_SampleRate == 0 ? 0.0
				: static_cast<double>(GetFrameCount()) / m_SampleRate;
		}
		const std::vector<uint8_t>& GetPcmBytes() const { return m_PcmBytes; }

	private:
		uint16_t m_Channels = 0;
		uint32_t m_SampleRate = 0;
		uint16_t m_BitsPerSample = 0;
		uint16_t m_BlockAlign = 0;
		uint32_t m_AverageBytesPerSecond = 0;
		bool m_FloatingPoint = false;
		std::vector<uint8_t> m_PcmBytes;
	};

}
