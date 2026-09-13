#include "tcpch.h"
#include "AudioArtifact.h"

#include "TomCat/Audio/AudioClip.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <limits>

namespace TomCat {

	namespace {
		void AppendU16(std::vector<uint8_t>& output, uint16_t value)
		{
			output.push_back(static_cast<uint8_t>(value));
			output.push_back(static_cast<uint8_t>(value >> 8));
		}

		void AppendU32(std::vector<uint8_t>& output, uint32_t value)
		{
			for (uint32_t shift = 0; shift < 32; shift += 8)
				output.push_back(static_cast<uint8_t>(value >> shift));
		}

		void AppendFourCC(std::vector<uint8_t>& output, const char* value)
		{
			output.insert(output.end(), value, value + 4);
		}

		int16_t ConvertIntegerSample(const uint8_t* source, uint16_t bits)
		{
			switch (bits)
			{
				case 8:
					return static_cast<int16_t>((static_cast<int32_t>(source[0]) - 128) << 8);
				case 16:
					return static_cast<int16_t>(static_cast<uint16_t>(source[0])
						| (static_cast<uint16_t>(source[1]) << 8));
				case 24:
				{
					int32_t value = static_cast<int32_t>(source[0])
						| (static_cast<int32_t>(source[1]) << 8)
						| (static_cast<int32_t>(source[2]) << 16);
					if ((value & 0x00800000) != 0)
						value |= static_cast<int32_t>(0xff000000);
					return static_cast<int16_t>(value >> 8);
				}
				case 32:
				{
					const uint32_t raw = static_cast<uint32_t>(source[0])
						| (static_cast<uint32_t>(source[1]) << 8)
						| (static_cast<uint32_t>(source[2]) << 16)
						| (static_cast<uint32_t>(source[3]) << 24);
					return static_cast<int16_t>(static_cast<int32_t>(raw) >> 16);
				}
			}
			return 0;
		}
	}

	bool BuildAudioArtifact(std::span<const uint8_t> source,
		const AssetImportSettings&, std::vector<uint8_t>& artifact,
		std::string& error)
	{
		artifact.clear();
		Ref<AudioClip> clip = AudioClip::Decode(source, error, nullptr,
			"audio import");
		if (!clip)
			return false;
		const std::vector<uint8_t>& pcm = clip->GetPcmBytes();
		const uint16_t sourceBytesPerSample = clip->GetBitsPerSample() / 8;
		if (sourceBytesPerSample == 0 || pcm.size() % sourceBytesPerSample != 0)
		{
			error = "audio import: decoded sample alignment is invalid";
			return false;
		}
		const uint64_t sampleCount = pcm.size() / sourceBytesPerSample;
		const uint64_t outputBytes = sampleCount * sizeof(int16_t);
		if (outputBytes > static_cast<uint64_t>((std::numeric_limits<uint32_t>::max)())
			- 36ULL || outputBytes > (std::numeric_limits<size_t>::max)() - 44ULL)
		{
			error = "audio import: canonical PCM payload exceeds the WAV limit";
			return false;
		}

		artifact.reserve(44 + static_cast<size_t>(outputBytes));
		AppendFourCC(artifact, "RIFF");
		AppendU32(artifact, 36 + static_cast<uint32_t>(outputBytes));
		AppendFourCC(artifact, "WAVE");
		AppendFourCC(artifact, "fmt ");
		AppendU32(artifact, 16);
		AppendU16(artifact, 1);
		AppendU16(artifact, clip->GetChannels());
		AppendU32(artifact, clip->GetSampleRate());
		const uint16_t blockAlign = clip->GetChannels() * 2;
		AppendU32(artifact, clip->GetSampleRate() * blockAlign);
		AppendU16(artifact, blockAlign);
		AppendU16(artifact, 16);
		AppendFourCC(artifact, "data");
		AppendU32(artifact, static_cast<uint32_t>(outputBytes));

		for (uint64_t index = 0; index < sampleCount; ++index)
		{
			const uint8_t* input = pcm.data()
				+ static_cast<size_t>(index) * sourceBytesPerSample;
			int16_t value = 0;
			if (clip->IsFloatingPoint())
			{
				float sample = 0.0f;
				std::memcpy(&sample, input, sizeof(sample));
				if (!std::isfinite(sample)) sample = 0.0f;
				value = static_cast<int16_t>(std::lround(
					std::clamp(sample, -1.0f, 1.0f) * 32767.0f));
			}
			else
				value = ConvertIntegerSample(input, clip->GetBitsPerSample());
			AppendU16(artifact, static_cast<uint16_t>(value));
		}
		return true;
	}

}
