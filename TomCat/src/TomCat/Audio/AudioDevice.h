#pragma once

#include "AudioClip.h"
#include "AudioStream.h"

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace TomCat {

	using AudioVoiceHandle = uint64_t;

	enum class AudioPlaybackState : int32_t
	{
		Stopped = 0,
		Playing = 1,
		Paused = 2
	};

	struct AudioVoiceSettings
	{
		bool Loop = false;
		float Volume = 1.0f;
		float Pitch = 1.0f;
	};

	struct AudioSpatialSettings
	{
		// Equal-power pan in [-1, 1] and a separate distance gain in [0, 1].
		float Pan = 0.0f;
		float DistanceGain = 1.0f;
	};

	// A deliberately small backend boundary. NullAudioDevice implements the same
	// voice lifecycle as a hardware device, making headless CI and machines with
	// no endpoint deterministic instead of treating audio as a startup failure.
	class IAudioDevice
	{
	public:
		virtual ~IAudioDevice() = default;
		virtual bool Initialize(std::string& error) = 0;
		virtual void Shutdown() = 0;
		virtual bool IsHardwareAvailable() const = 0;
		virtual bool IsOperational() const = 0;
		virtual const char* GetBackendName() const = 0;

		virtual AudioVoiceHandle CreateVoice(Ref<AudioClip> clip,
			const AudioVoiceSettings& settings, std::string& error) = 0;
		virtual AudioVoiceHandle CreateStreamingVoice(
			std::unique_ptr<IAudioStream> stream,
			const AudioVoiceSettings& settings, std::string& error) = 0;
		virtual bool DestroyVoice(AudioVoiceHandle voice) = 0;
		virtual bool Play(AudioVoiceHandle voice) = 0;
		virtual bool Pause(AudioVoiceHandle voice) = 0;
		virtual bool Stop(AudioVoiceHandle voice) = 0;
		virtual bool SetLoop(AudioVoiceHandle voice, bool loop) = 0;
		virtual bool SetVolume(AudioVoiceHandle voice, float volume) = 0;
		virtual bool SetPitch(AudioVoiceHandle voice, float pitch) = 0;
		virtual bool SetSpatial(AudioVoiceHandle voice,
			const AudioSpatialSettings& settings) = 0;
		virtual AudioPlaybackState GetState(AudioVoiceHandle voice) const = 0;
		virtual double GetPlaybackSeconds(AudioVoiceHandle voice) const = 0;
		virtual void Update(double deltaSeconds) = 0;
	};

	// Builds the source-to-output matrix shared by the hardware backend and
	// regression tests. Mono sources use equal-power panning. Multi-channel
	// sources retain discrete channel identity and receive distance gain only;
	// applying one mono pan pair to every input channel would collapse stereo.
	[[nodiscard]] bool BuildAudioOutputMatrix(uint32_t sourceChannels,
		uint32_t outputChannels, const AudioSpatialSettings& settings,
		std::vector<float>& matrix);

	// Pure, backend-independent 2D spatialization used by AudioSceneRuntime and
	// native regression tests. listenerRight must be normalized.
	AudioSpatialSettings CalculateAudioSpatial2D(float sourceX, float sourceY,
		float listenerX, float listenerY, float listenerRightX,
		float listenerRightY, float spatialBlend, float minDistance,
		float maxDistance);

	std::unique_ptr<IAudioDevice> CreateNullAudioDevice();
	// Returns an initialized Windows XAudio2 device when possible. Initialization
	// failures are reported in fallbackReason and yield an initialized Null device.
	std::unique_ptr<IAudioDevice> CreateDefaultAudioDevice(
		std::string& fallbackReason);

}
