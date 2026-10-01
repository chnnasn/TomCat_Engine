#pragma once

#include "AudioDevice.h"
#include "AudioBusGraph.h"
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

	struct AudioMixerSnapshot {
        std::array<float, 3> Volumes{1.f, 1.f, 1.f};
        std::array<bool, 3> Muted{};
        std::array<bool, 3> Solo{};
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
        bool ConfigureBusGraph(std::string_view document,std::string& error);
        bool SetBus(uint32_t id,float volume,bool muted,bool solo);
        bool SetVoiceBus(AudioVoiceHandle voice,uint32_t bus);
        const AudioBusGraph& GetBusGraph() const {return m_BusGraph;}
		float GetMixerVolume(AudioMixerGroup group) const;
        AudioMixerSnapshot GetMixerSnapshot() const;
        bool ApplyMixerSnapshot(const AudioMixerSnapshot& snapshot, double fadeSeconds = 0);
        bool SetMixerMuted(AudioMixerGroup group, bool muted);
        bool SetMixerSolo(AudioMixerGroup group, bool solo);
        // Playing voices in trigger lower target to gain; attack/release are seconds.
        bool SetDucking(AudioMixerGroup trigger, AudioMixerGroup target, float gain,
            double attackSeconds = 0.05, double releaseSeconds = 0.3);
        void ClearDucking();
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
            uint32_t Bus=UINT32_MAX;
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
		AudioBusGraph m_BusGraph;
        std::array<float, 3> m_MixerVolumes{ 1.0f, 1.0f, 1.0f };
		std::array<bool, 3> m_MixerMuted{}, m_MixerSolo{};
        AudioMixerSnapshot m_FadeStart, m_FadeTarget;
        double m_FadeDuration = 0, m_FadeElapsed = 0;
        bool m_Ducking = false;
        AudioMixerGroup m_DuckTrigger = AudioMixerGroup::SFX, m_DuckTarget = AudioMixerGroup::Music;
        float m_DuckGain = 0.3f, m_DuckEnvelope = 1.f;
        double m_DuckAttack = 0.05, m_DuckRelease = 0.3;
        AudioVoiceHandle m_NextVoice = 1;
		uint64_t m_DeviceGeneration = 0;
		bool m_UseDefaultRecovery = true;
	};

}
