#include "tcpch.h"
#include "AudioDevice.h"

#include <algorithm>
#include <cmath>
#include <unordered_map>

namespace TomCat {

	namespace {

		bool ValidVolume(float value)
		{
			return std::isfinite(value) && value >= 0.0f && value <= 4.0f;
		}

		bool ValidPitch(float value)
		{
			return std::isfinite(value) && value >= 0.25f && value <= 4.0f;
		}

		class NullAudioDevice final : public IAudioDevice
		{
		public:
			bool Initialize(std::string& error) override
			{
				error.clear();
				m_Initialized = true;
				return true;
			}

			void Shutdown() override
			{
				m_Voices.clear();
				m_Initialized = false;
			}

			bool IsHardwareAvailable() const override { return false; }
			bool IsOperational() const override { return m_Initialized; }
			const char* GetBackendName() const override { return "NullAudioDevice"; }

			AudioVoiceHandle CreateVoice(Ref<AudioClip> clip,
				const AudioVoiceSettings& settings, std::string& error) override
			{
				error.clear();
				if (!m_Initialized || !clip || clip->GetDurationSeconds() <= 0.0)
				{
					error = "Null audio voice requires an initialized device and a nonempty clip";
					return 0;
				}
				if (!ValidVolume(settings.Volume) || !ValidPitch(settings.Pitch))
				{
					error = "Audio volume or pitch is outside the supported range";
					return 0;
				}
				AudioVoiceHandle handle = m_NextVoice++;
				if (handle == 0)
					handle = m_NextVoice++;
				Voice voice;
				voice.Clip = std::move(clip);
				voice.Duration = voice.Clip->GetDurationSeconds();
				voice.Settings = settings;
				m_Voices.emplace(handle, std::move(voice));
				return handle;
			}

			AudioVoiceHandle CreateStreamingVoice(
				std::unique_ptr<IAudioStream> stream,
				const AudioVoiceSettings& settings, std::string& error) override
			{
				error.clear();
				if (!m_Initialized || !stream || stream->GetDurationSeconds() <= 0.0)
				{
					error = "Null streaming voice requires an initialized device and a nonempty stream";
					return 0;
				}
				if (!ValidVolume(settings.Volume) || !ValidPitch(settings.Pitch))
				{
					error = "Audio volume or pitch is outside the supported range";
					return 0;
				}
				AudioVoiceHandle handle = m_NextVoice++;
				if (handle == 0)
					handle = m_NextVoice++;
				Voice voice;
				voice.Duration = stream->GetDurationSeconds();
				voice.Stream = std::move(stream);
				voice.Settings = settings;
				m_Voices.emplace(handle, std::move(voice));
				return handle;
			}

			bool DestroyVoice(AudioVoiceHandle voice) override
			{
				return m_Voices.erase(voice) != 0;
			}

			bool Play(AudioVoiceHandle voice) override
			{
				Voice* value = Find(voice);
				if (!value)
					return false;
				if (value->State == AudioPlaybackState::Stopped)
					value->Cursor = 0.0;
				value->State = AudioPlaybackState::Playing;
				return true;
			}

			bool Pause(AudioVoiceHandle voice) override
			{
				Voice* value = Find(voice);
				if (!value || value->State != AudioPlaybackState::Playing)
					return false;
				value->State = AudioPlaybackState::Paused;
				return true;
			}

			bool Stop(AudioVoiceHandle voice) override
			{
				Voice* value = Find(voice);
				if (!value)
					return false;
				value->State = AudioPlaybackState::Stopped;
				value->Cursor = 0.0;
				return true;
			}

			bool SetLoop(AudioVoiceHandle voice, bool loop) override
			{
				Voice* value = Find(voice);
				if (!value)
					return false;
				value->Settings.Loop = loop;
				return true;
			}

			bool SetVolume(AudioVoiceHandle voice, float volume) override
			{
				Voice* value = Find(voice);
				if (!value || !ValidVolume(volume))
					return false;
				value->Settings.Volume = volume;
				return true;
			}

			bool SetPitch(AudioVoiceHandle voice, float pitch) override
			{
				Voice* value = Find(voice);
				if (!value || !ValidPitch(pitch))
					return false;
				value->Settings.Pitch = pitch;
				return true;
			}

			bool SetSpatial(AudioVoiceHandle voice,
				const AudioSpatialSettings& settings) override
			{
				Voice* value = Find(voice);
				if (!value || !std::isfinite(settings.Pan)
					|| !std::isfinite(settings.DistanceGain)
					|| settings.Pan < -1.0f || settings.Pan > 1.0f
					|| settings.DistanceGain < 0.0f || settings.DistanceGain > 1.0f)
					return false;
				value->Spatial = settings;
				return true;
			}

			AudioPlaybackState GetState(AudioVoiceHandle voice) const override
			{
				const auto iterator = m_Voices.find(voice);
				return iterator == m_Voices.end()
					? AudioPlaybackState::Stopped : iterator->second.State;
			}

			double GetPlaybackSeconds(AudioVoiceHandle voice) const override
			{
				const auto iterator = m_Voices.find(voice);
				return iterator == m_Voices.end() ? 0.0 : iterator->second.Cursor;
			}

			void Update(double deltaSeconds) override
			{
				if (!m_Initialized || !std::isfinite(deltaSeconds) || deltaSeconds <= 0.0)
					return;
				for (auto& [handle, voice] : m_Voices)
				{
					(void)handle;
					if (voice.State != AudioPlaybackState::Playing)
						continue;
					const double duration = voice.Duration;
					voice.Cursor += deltaSeconds * voice.Settings.Pitch;
					if (voice.Cursor < duration)
						continue;
					if (voice.Settings.Loop)
						voice.Cursor = std::fmod(voice.Cursor, duration);
					else
					{
						voice.Cursor = duration;
						voice.State = AudioPlaybackState::Stopped;
					}
				}
			}

		private:
			struct Voice
			{
				Ref<AudioClip> Clip;
				std::unique_ptr<IAudioStream> Stream;
				AudioVoiceSettings Settings;
				AudioSpatialSettings Spatial;
				AudioPlaybackState State = AudioPlaybackState::Stopped;
				double Cursor = 0.0;
				double Duration = 0.0;
			};

			Voice* Find(AudioVoiceHandle voice)
			{
				const auto iterator = m_Voices.find(voice);
				return iterator == m_Voices.end() ? nullptr : &iterator->second;
			}

			bool m_Initialized = false;
			AudioVoiceHandle m_NextVoice = 1;
			std::unordered_map<AudioVoiceHandle, Voice> m_Voices;
		};

	}

	std::unique_ptr<IAudioDevice> CreateNullAudioDevice()
	{
		return std::make_unique<NullAudioDevice>();
	}

}
