#include "tcpch.h"
#include "AudioStream.h"

#include <algorithm>
#include <array>
#include <cstring>
#include <fstream>
#include <functional>
#include <limits>

namespace TomCat {

	namespace {

		constexpr uint32_t MaximumSampleRate = 384000;
		constexpr uint16_t WaveFormatPcm = 0x0001;
		constexpr uint16_t WaveFormatIeeeFloat = 0x0003;
		constexpr uint16_t WaveFormatExtensible = 0xfffe;
		using RangeReader = std::function<bool(uint64_t, std::span<uint8_t>)>;

		struct ParsedWave
		{
			AudioStreamFormat Format;
			uint64_t DataOffset = 0;
			uint64_t DataSize = 0;
			uint64_t FrameCount = 0;
		};

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

		bool ExtensibleSubformat(const uint8_t* guid, uint16_t format)
		{
			static constexpr std::array<uint8_t, 14> tail = {
				0x00, 0x00, 0x00, 0x00, 0x10, 0x00, 0x80,
				0x00, 0x00, 0xaa, 0x00, 0x38, 0x9b, 0x71 };
			return ReadU16(guid) == format
				&& std::memcmp(guid + 2, tail.data(), tail.size()) == 0;
		}

		std::string WithSource(std::string message, std::string_view sourceName)
		{
			return sourceName.empty() ? message
				: std::string(sourceName) + ": " + message;
		}

		bool ParseWave(uint64_t sourceSize, const RangeReader& read,
			ParsedWave& parsed, std::string& error, std::string_view sourceName)
		{
			parsed = {};
			std::array<uint8_t, 4> signature{};
			if (sourceSize >= signature.size() && read(0, signature)
				&& FourCC(signature.data(), "OggS"))
			{
				error = WithSource(
					"OGG/Vorbis streaming is not available in this build", sourceName);
				return false;
			}
			std::array<uint8_t, 12> riff{};
			if (sourceSize < riff.size() || !read(0, riff))
			{
				error = WithSource("audio source is too small", sourceName);
				return false;
			}
			if (!FourCC(riff.data(), "RIFF") || !FourCC(riff.data() + 8, "WAVE"))
			{
				error = WithSource("only RIFF/WAVE streaming is supported", sourceName);
				return false;
			}
			const uint64_t declaredEnd = 8ULL + ReadU32(riff.data() + 4);
			if (declaredEnd < 12 || declaredEnd > sourceSize)
			{
				error = WithSource("RIFF size exceeds the available data", sourceName);
				return false;
			}

			std::array<uint8_t, 40> format{};
			uint32_t formatSize = 0;
			bool hasFormat = false;
			bool hasData = false;
			uint64_t cursor = 12;
			while (cursor + 8 <= declaredEnd)
			{
				std::array<uint8_t, 8> header{};
				if (!read(cursor, header))
				{
					error = WithSource("could not read a WAV chunk header", sourceName);
					return false;
				}
				const uint32_t chunkSize = ReadU32(header.data() + 4);
				const uint64_t payload = cursor + 8;
				const uint64_t chunkEnd = payload + chunkSize;
				if (chunkEnd < payload || chunkEnd > declaredEnd)
				{
					error = WithSource("WAV chunk exceeds the RIFF boundary", sourceName);
					return false;
				}
				if (FourCC(header.data(), "fmt ") && !hasFormat)
				{
					formatSize = chunkSize;
					const size_t bytes = (std::min)(format.size(),
						static_cast<size_t>(chunkSize));
					if (bytes == 0 || !read(payload,
						std::span<uint8_t>(format.data(), bytes)))
					{
						error = WithSource("could not read the WAV format chunk", sourceName);
						return false;
					}
					hasFormat = true;
				}
				else if (FourCC(header.data(), "data") && !hasData)
				{
					parsed.DataOffset = payload;
					parsed.DataSize = chunkSize;
					hasData = true;
				}
				cursor = chunkEnd + (chunkSize & 1U);
				if (cursor > declaredEnd)
				{
					error = WithSource("WAV chunk padding exceeds the RIFF boundary",
						sourceName);
					return false;
				}
			}
			if (!hasFormat || formatSize < 16 || !hasData || parsed.DataSize == 0)
			{
				error = WithSource("WAV requires nonempty fmt and data chunks", sourceName);
				return false;
			}

			uint16_t encoding = ReadU16(format.data());
			const uint16_t channels = ReadU16(format.data() + 2);
			const uint32_t sampleRate = ReadU32(format.data() + 4);
			const uint32_t byteRate = ReadU32(format.data() + 8);
			const uint16_t blockAlign = ReadU16(format.data() + 12);
			const uint16_t bitsPerSample = ReadU16(format.data() + 14);
			if (encoding == WaveFormatExtensible)
			{
				if (formatSize < 40 || ReadU16(format.data() + 16) < 22)
				{
					error = WithSource("WAVE_FORMAT_EXTENSIBLE header is incomplete",
						sourceName);
					return false;
				}
				const uint8_t* subformat = format.data() + 24;
				if (ExtensibleSubformat(subformat, WaveFormatPcm))
					encoding = WaveFormatPcm;
				else if (ExtensibleSubformat(subformat, WaveFormatIeeeFloat))
					encoding = WaveFormatIeeeFloat;
				else
				{
					error = WithSource("WAV extensible subformat is unsupported",
						sourceName);
					return false;
				}
			}
			if (encoding != WaveFormatPcm && encoding != WaveFormatIeeeFloat)
			{
				error = WithSource("compressed WAV encoding is unsupported", sourceName);
				return false;
			}
			if (channels == 0 || channels > 8 || sampleRate == 0
				|| sampleRate > MaximumSampleRate)
			{
				error = WithSource("WAV channel count or sample rate is unsupported",
					sourceName);
				return false;
			}
			const bool floatingPoint = encoding == WaveFormatIeeeFloat;
			const bool validBits = floatingPoint ? bitsPerSample == 32
				: (bitsPerSample == 8 || bitsPerSample == 16
					|| bitsPerSample == 24 || bitsPerSample == 32);
			const uint32_t expectedAlign = static_cast<uint32_t>(channels)
				* (bitsPerSample / 8U);
			const uint64_t expectedRate = static_cast<uint64_t>(sampleRate)
				* expectedAlign;
			if (!validBits || bitsPerSample % 8 != 0 || blockAlign != expectedAlign
				|| byteRate != expectedRate || parsed.DataSize % blockAlign != 0)
			{
				error = WithSource("WAV format alignment is inconsistent", sourceName);
				return false;
			}
			parsed.Format = { channels, sampleRate, bitsPerSample, blockAlign,
				byteRate, floatingPoint };
			parsed.FrameCount = parsed.DataSize / blockAlign;
			return true;
		}

		class PcmWaveStream final : public IAudioStream
		{
		public:
			explicit PcmWaveStream(Ref<const AudioStreamSource> source)
				: m_Source(std::move(source)) {}
			const AudioStreamFormat& GetFormat() const override
			{ return m_Source->GetFormat(); }
			uint64_t GetFrameCount() const override
			{ return m_Source->GetFrameCount(); }
			uint64_t GetFramePosition() const override { return m_FramePosition; }
			size_t ReadFrames(std::span<uint8_t> destination) override
			{
				const size_t copied = m_Source->CopyFrames(m_FramePosition, destination);
				m_FramePosition += copied / GetFormat().BlockAlign;
				return copied;
			}
			bool SeekFrame(uint64_t frame) override
			{
				if (frame > GetFrameCount()) return false;
				m_FramePosition = frame;
				return true;
			}
			std::unique_ptr<IAudioStream> Clone() const override
			{
				auto result = std::make_unique<PcmWaveStream>(m_Source);
				result->m_FramePosition = m_FramePosition;
				return result;
			}
			size_t GetDecoderBufferedByteCount() const override { return 0; }
		private:
			Ref<const AudioStreamSource> m_Source;
			uint64_t m_FramePosition = 0;
		};

	}

	Ref<AudioStreamSource> AudioStreamSource::Open(std::vector<uint8_t> bytes,
		std::string& error, std::string_view sourceName)
	{
		error.clear();
		ParsedWave parsed;
		const auto read = [&bytes](uint64_t offset, std::span<uint8_t> output)
		{
			if (offset > bytes.size() || output.size() > bytes.size() - offset)
				return false;
			if (!output.empty())
				std::memcpy(output.data(), bytes.data() + static_cast<size_t>(offset),
					output.size());
			return true;
		};
		if (!ParseWave(bytes.size(), read, parsed, error, sourceName))
			return {};
		Ref<AudioStreamSource> source = CreateRef<AudioStreamSource>();
		source->m_Format = parsed.Format;
		source->m_Bytes = std::move(bytes);
		source->m_RangeSize = source->m_Bytes.size();
		source->m_DataOffset = static_cast<size_t>(parsed.DataOffset);
		source->m_DataSize = static_cast<size_t>(parsed.DataSize);
		source->m_FrameCount = parsed.FrameCount;
		return source;
	}

	Ref<AudioStreamSource> AudioStreamSource::OpenFileRange(
		const std::filesystem::path& path, uint64_t rangeOffset,
		uint64_t rangeSize, std::string& error, std::string_view sourceName,
		const ContentSHA256Digest* expectedDigest)
	{
		error.clear();
		std::error_code fileError;
		const uint64_t fileSize = std::filesystem::file_size(path, fileError);
		if (fileError || rangeOffset > fileSize || rangeSize > fileSize - rangeOffset)
		{
			error = WithSource("stream file range is unavailable or out of bounds",
				sourceName);
			return {};
		}
		std::ifstream input(path, std::ios::binary);
		if (!input)
		{
			error = WithSource("stream file could not be opened", sourceName);
			return {};
		}
		if (expectedDigest && !VerifyStreamRangeContentSHA256(input,
			rangeOffset, rangeSize, *expectedDigest))
		{
			error = WithSource(
				"stream file range SHA-256 does not match its tcpak index",
				sourceName);
			return {};
		}
		const auto read = [&input, rangeOffset, rangeSize](uint64_t offset,
			std::span<uint8_t> output)
		{
			const uint64_t maximumStreamOffset = static_cast<uint64_t>(
				(std::numeric_limits<std::streamoff>::max)());
			if (offset > rangeSize || output.size() > rangeSize - offset
				|| rangeOffset > maximumStreamOffset
				|| offset > maximumStreamOffset - rangeOffset)
				return false;
			const uint64_t absoluteOffset = rangeOffset + offset;
			input.clear();
			input.seekg(static_cast<std::streamoff>(absoluteOffset));
			if (!input) return false;
			input.read(reinterpret_cast<char*>(output.data()),
				static_cast<std::streamsize>(output.size()));
			return input.good() || static_cast<size_t>(input.gcount()) == output.size();
		};
		ParsedWave parsed;
		if (!ParseWave(rangeSize, read, parsed, error, sourceName))
			return {};
		Ref<AudioStreamSource> source = CreateRef<AudioStreamSource>();
		source->m_Format = parsed.Format;
		source->m_Path = std::filesystem::absolute(path, fileError).lexically_normal();
		if (fileError) source->m_Path = path.lexically_normal();
		source->m_RangeOffset = rangeOffset;
		source->m_RangeSize = rangeSize;
		source->m_DataOffset = static_cast<size_t>(parsed.DataOffset);
		source->m_DataSize = static_cast<size_t>(parsed.DataSize);
		source->m_FrameCount = parsed.FrameCount;
		return source;
	}

	std::unique_ptr<IAudioStream> AudioStreamSource::CreateReader() const
	{
		return std::make_unique<PcmWaveStream>(shared_from_this());
	}

	size_t AudioStreamSource::CopyFrames(uint64_t firstFrame,
		std::span<uint8_t> destination) const
	{
		if (m_Format.BlockAlign == 0 || firstFrame >= m_FrameCount)
			return 0;
		const size_t capacity = destination.size()
			- destination.size() % m_Format.BlockAlign;
		const uint64_t remainingFrames = m_FrameCount - firstFrame;
		const size_t remainingBytes = static_cast<size_t>((std::min)(
			remainingFrames * m_Format.BlockAlign,
			static_cast<uint64_t>((std::numeric_limits<size_t>::max)())));
		const size_t copied = (std::min)(capacity, remainingBytes);
		if (copied == 0) return 0;
		const uint64_t relativeOffset = static_cast<uint64_t>(m_DataOffset)
			+ firstFrame * m_Format.BlockAlign;
		if (!m_Path.empty())
		{
			std::ifstream input(m_Path, std::ios::binary);
			const uint64_t maximumStreamOffset = static_cast<uint64_t>(
				(std::numeric_limits<std::streamoff>::max)());
			if (!input || relativeOffset > m_RangeSize
				|| copied > m_RangeSize - relativeOffset
				|| m_RangeOffset > maximumStreamOffset
				|| relativeOffset > maximumStreamOffset - m_RangeOffset)
				return 0;
			const uint64_t absoluteOffset = m_RangeOffset + relativeOffset;
			input.seekg(static_cast<std::streamoff>(absoluteOffset));
			if (!input) return 0;
			input.read(reinterpret_cast<char*>(destination.data()),
				static_cast<std::streamsize>(copied));
			return static_cast<size_t>(input.gcount())
				- static_cast<size_t>(input.gcount()) % m_Format.BlockAlign;
		}
		std::memcpy(destination.data(), m_Bytes.data()
			+ static_cast<size_t>(relativeOffset), copied);
		return copied;
	}

}
