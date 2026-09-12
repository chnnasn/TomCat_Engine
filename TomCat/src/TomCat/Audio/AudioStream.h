#pragma once

#include "TomCat/Core/Base.h"

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace TomCat {

	struct AudioStreamFormat
	{
		uint16_t Channels = 0;
		uint32_t SampleRate = 0;
		uint16_t BitsPerSample = 0;
		uint16_t BlockAlign = 0;
		uint32_t AverageBytesPerSecond = 0;
		bool FloatingPoint = false;
	};

	// Per-voice decoder cursor. Implementations must return whole interleaved
	// sample frames and retain no unbounded decoded history.
	class IAudioStream
	{
	public:
		virtual ~IAudioStream() = default;
		virtual const AudioStreamFormat& GetFormat() const = 0;
		virtual uint64_t GetFrameCount() const = 0;
		virtual uint64_t GetFramePosition() const = 0;
		virtual size_t ReadFrames(std::span<uint8_t> destination) = 0;
		virtual bool SeekFrame(uint64_t frame) = 0;
		virtual std::unique_ptr<IAudioStream> Clone() const = 0;
		virtual size_t GetDecoderBufferedByteCount() const = 0;

		double GetDurationSeconds() const
		{
			return GetFormat().SampleRate == 0 ? 0.0
				: static_cast<double>(GetFrameCount()) / GetFormat().SampleRate;
		}
	};

	// Immutable encoded source shared by independent stream readers. PCM WAV is
	// read directly from its data chunk. OGG remains an explicit extension point
	// until a real Vorbis decoder is linked into the engine.
	class AudioStreamSource final
		: public std::enable_shared_from_this<AudioStreamSource>
	{
	public:
		static Ref<AudioStreamSource> Open(std::vector<uint8_t> bytes,
			std::string& error, std::string_view sourceName = {});
		static Ref<AudioStreamSource> OpenFileRange(
			const std::filesystem::path& path, uint64_t rangeOffset,
			uint64_t rangeSize, std::string& error,
			std::string_view sourceName = {});

		const AudioStreamFormat& GetFormat() const { return m_Format; }
		uint64_t GetFrameCount() const { return m_FrameCount; }
		double GetDurationSeconds() const
		{
			return m_Format.SampleRate == 0 ? 0.0
				: static_cast<double>(m_FrameCount) / m_Format.SampleRate;
		}
		uint64_t GetEncodedByteCount() const { return m_RangeSize; }
		size_t GetResidentByteCount() const { return m_Bytes.size(); }
		bool IsFileBacked() const { return !m_Path.empty(); }
		std::unique_ptr<IAudioStream> CreateReader() const;

		// Copies only complete frames and never allocates. Public for custom device
		// backends; callers normally consume it through IAudioStream.
		size_t CopyFrames(uint64_t firstFrame,
			std::span<uint8_t> destination) const;

	private:
		AudioStreamFormat m_Format;
		std::vector<uint8_t> m_Bytes;
		std::filesystem::path m_Path;
		uint64_t m_RangeOffset = 0;
		uint64_t m_RangeSize = 0;
		size_t m_DataOffset = 0;
		size_t m_DataSize = 0;
		uint64_t m_FrameCount = 0;
	};

}
