#pragma once

#include "AudioDevice.h"
#include "TomCat/Asset/Asset.h"

#include <array>
#include <cstdint>
#include <memory>
#include <string>
#include <unordered_map>

namespace TomCat {

	enum class AudioMixerGroup : int32_t
	{
		Master = 0,
		Music = 1,
		SFX = 2
	};

	class AudioEngine
	{
	public:
		AudioEngine() = default;
		~AudioEngine();

		static AudioEngine& Get();

		bool Initialize();
		bool Initialize(std::unique_ptr<IAudioDevice> device,
			std::string& error);
		void Shutdown();
		bool IsInitialized() const { return m_Device != nullptr; }
		bool IsHardwareAvailable() const
		{
			return m_Device && m_Device->IsHardwareAvailable();
		}
		const char* GetBackendName() const
		{
			return m_Device ? m_Device->GetBackendName() : "Uninitialized";
		}
		const std::string& GetFallbackReason() const { return m_FallbackReason; }

		Ref<AudioClip> LoadClip(AssetHandle handle, std::string& error);
		Ref<AudioStreamSource> LoadStreamSource(AssetHandle handle,
			std::string& error);
		void ReleaseClip(AssetHandle handle);
		void ReleaseAllClips();

		AudioVoiceHandle CreateVoice(Ref<AudioClip> clip,
			AudioMixerGroup group, const AudioVoiceSettings& settings,
			std::string& error);
		AudioVoiceHandle CreateVoice(AssetHandle clipHandle,
			AudioMixerGroup group, const AudioVoiceSettings& settings,
			std::string& error);
		AudioVoiceHandle CreateStreamingVoice(Ref<AudioStreamSource> source,
			AudioMixerGroup group, const AudioVoiceSettings& settings,
			std::string& error);
		AudioVoiceHandle CreateStreamingVoice(AssetHandle clipHandle,
			AudioMixerGroup group, const AudioVoiceSettings& settings,
			std::string& error);
		bool DestroyVoice(AudioVoiceHandle voice);
		bool HasVoice(AudioVoiceHandle voice) const { return m_Voices.contains(voice); }
		bool Play(AudioVoiceHandle voice);
		bool Pause(AudioVoiceHandle voice);
		bool Stop(AudioVoiceHandle voice);
		bool SetLoop(AudioVoiceHandle voice, bool loop);
		bool SetVolume(AudioVoiceHandle voice, float volume);
		bool SetPitch(AudioVoiceHandle voice, float pitch);
		bool SetSpatial(AudioVoiceHandle voice,
			const AudioSpatialSettings& settings);
		bool SetMixerGroup(AudioVoiceHandle voice, AudioMixerGroup group);
		AudioPlaybackState GetState(AudioVoiceHandle voice) const;
		double GetPlaybackSeconds(AudioVoiceHandle voice) const;

		bool SetMixerVolume(AudioMixerGroup group, float volume);
		float GetMixerVolume(AudioMixerGroup group) const;
		void Update(double deltaSeconds);
		uint64_t GetDeviceGeneration() const { return m_DeviceGeneration; }

	private:
		struct VoiceRecord
		{
			AudioVoiceHandle BackendVoice = 0;
			Ref<AudioClip> Clip;
			Ref<AudioStreamSource> StreamSource;
			AudioVoiceSettings Settings;
			AudioSpatialSettings Spatial;
			AudioPlaybackState DesiredState = AudioPlaybackState::Stopped;
			AudioMixerGroup Group = AudioMixerGroup::SFX;
		};

		static bool IsValidMixerGroup(AudioMixerGroup group);
		static bool IsValidVolume(float volume);
		float EffectiveVolume(const VoiceRecord& voice) const;
		bool ApplyVolume(AudioVoiceHandle handle, const VoiceRecord& voice);
		bool RebuildVoice(VoiceRecord& voice, std::string& error);
		bool RecoverDevice();
		AudioVoiceHandle AllocateVoiceHandle();

		std::unique_ptr<IAudioDevice> m_Device;
		std::string m_FallbackReason;
		std::unordered_map<AssetHandle, Ref<AudioClip>> m_ClipCache;
		std::unordered_map<AssetHandle, Ref<AudioStreamSource>> m_StreamCache;
		std::unordered_map<AudioVoiceHandle, VoiceRecord> m_Voices;
		std::array<float, 3> m_MixerVolumes{ 1.0f, 1.0f, 1.0f };
		AudioVoiceHandle m_NextVoice = 1;
		uint64_t m_DeviceGeneration = 0;
		bool m_UseDefaultRecovery = true;
	};

}
