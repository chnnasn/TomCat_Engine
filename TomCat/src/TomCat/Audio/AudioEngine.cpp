#include "tcpch.h"
#include "AudioEngine.h"

#include "TomCat/Asset/AssetManager.h"
#include "TomCat/Core/Log.h"

#include <algorithm>
#include <cmath>
#include <utility>

namespace TomCat {

	AudioEngine::~AudioEngine()
	{
		Shutdown();
	}

	AudioEngine& AudioEngine::Get()
	{
		static AudioEngine instance;
		return instance;
	}

	bool AudioEngine::Initialize()
	{
		if (m_Device)
			return true;
		m_Device = CreateDefaultAudioDevice(m_FallbackReason);
		if (!m_Device)
			return false;
		m_UseDefaultRecovery = true;
		++m_DeviceGeneration;
		if (!m_FallbackReason.empty())
			TC_Core_Warn("Audio hardware is unavailable; using NullAudioDevice: {0}",
				m_FallbackReason);
		else
			TC_Core_Info("Audio backend initialized: {0}", m_Device->GetBackendName());
		return true;
	}

	bool AudioEngine::Initialize(std::unique_ptr<IAudioDevice> device,
		std::string& error)
	{
		error.clear();
		if (!device)
		{
			error = "Audio device is null";
			return false;
		}
		Shutdown();
		if (!device->Initialize(error))
			return false;
		m_Device = std::move(device);
		m_UseDefaultRecovery = false;
		m_FallbackReason.clear();
		++m_DeviceGeneration;
		return true;
	}

	void AudioEngine::Shutdown()
	{
		m_Voices.clear();
		m_ClipCache.clear();
		m_StreamCache.clear();
		if (m_Device)
			m_Device->Shutdown();
		m_Device.reset();
		m_FallbackReason.clear();
		m_MixerVolumes = { 1.0f, 1.0f, 1.0f };
		m_NextVoice = 1;
		m_UseDefaultRecovery = true;
	}

	Ref<AudioClip> AudioEngine::LoadClip(AssetHandle handle, std::string& error)
	{
		error.clear();
		if (static_cast<uint64_t>(handle) == 0)
		{
			error = "AudioClip AssetHandle must be nonzero";
			return {};
		}
		if (const auto iterator = m_ClipCache.find(handle);
			iterator != m_ClipCache.end())
			return iterator->second;

		AssetLoadResult imported = AssetManager::Get().LoadImportedArtifact(handle);
		if (!imported.Succeeded())
		{
			error = imported.Error.empty()
				? "Audio asset bytes could not be imported" : imported.Error;
			return {};
		}
		if (imported.Artifact.Type != AssetType::Audio)
		{
			error = "AssetHandle does not reference an Audio asset";
			return {};
		}
		Ref<AudioClip> clip = AudioClip::Decode(imported.Artifact.Bytes, error, nullptr,
			"Audio asset " + std::to_string(static_cast<uint64_t>(handle)));
		if (!clip)
			return {};
		m_ClipCache.emplace(handle, clip);
		return clip;
	}

	Ref<AudioStreamSource> AudioEngine::LoadStreamSource(AssetHandle handle,
		std::string& error)
	{
		error.clear();
		if (static_cast<uint64_t>(handle) == 0)
		{
			error = "Audio stream AssetHandle must be nonzero";
			return {};
		}
		if (const auto iterator = m_StreamCache.find(handle);
			iterator != m_StreamCache.end())
			return iterator->second;
		AssetManager& assets = AssetManager::Get();
		Ref<AudioStreamSource> source;
		if (assets.IsCookedPackageMounted())
		{
			CookedAssetRange range;
			if (!assets.TryGetCookedAssetRange(handle, range)
				|| range.Type != AssetType::Audio)
			{
				error = "AssetHandle does not reference a cooked Audio asset range";
				return {};
			}
			source = AudioStreamSource::OpenFileRange(range.PackagePath,
				range.Offset, range.Size, error,
				"Cooked audio asset "
					+ std::to_string(static_cast<uint64_t>(handle)));
		}
		else
		{
			const AssetMetadata* metadata = assets.GetRegistry().GetMetadata(handle);
			if (!metadata || metadata->IsMissing || metadata->Type != AssetType::Audio)
			{
				error = "AssetHandle does not reference an authoring Audio source";
				return {};
			}
			AssetLoadResult imported = assets.LoadImportedArtifact(handle);
			if (!imported.Succeeded() || imported.Artifact.Type != AssetType::Audio)
			{
				error = imported.Error.empty()
					? "Audio artifact could not be imported" : imported.Error;
				return {};
			}
			std::filesystem::path artifactPath;
			uint64_t artifactOffset = 0;
			uint64_t artifactSize = 0;
			if (assets.GetDatabase().GetCache().TryGetPayloadRange(
				imported.Artifact.ArtifactKey, artifactPath, artifactOffset, artifactSize))
			{
				source = AudioStreamSource::OpenFileRange(artifactPath,
					artifactOffset, artifactSize, error,
					"Audio artifact " + std::to_string(static_cast<uint64_t>(handle)));
			}
			else
			{
				source = AudioStreamSource::Open(std::move(imported.Artifact.Bytes),
					error, "Audio artifact "
						+ std::to_string(static_cast<uint64_t>(handle)));
			}
		}
		if (!source)
			return {};
		m_StreamCache.emplace(handle, source);
		return source;
	}

	void AudioEngine::ReleaseClip(AssetHandle handle)
	{
		m_ClipCache.erase(handle);
		m_StreamCache.erase(handle);
	}

	void AudioEngine::ReleaseAllClips()
	{
		m_ClipCache.clear();
		m_StreamCache.clear();
	}

	AudioVoiceHandle AudioEngine::AllocateVoiceHandle()
	{
		AudioVoiceHandle handle = m_NextVoice++;
		if (handle == 0)
			handle = m_NextVoice++;
		while (m_Voices.contains(handle))
		{
			handle = m_NextVoice++;
			if (handle == 0)
				handle = m_NextVoice++;
		}
		return handle;
	}

	AudioVoiceHandle AudioEngine::CreateVoice(Ref<AudioClip> clip,
		AudioMixerGroup group, const AudioVoiceSettings& settings,
		std::string& error)
	{
		error.clear();
		if (!Initialize())
		{
			error = "Audio engine initialization failed";
			return 0;
		}
		if (!clip || !IsValidMixerGroup(group) || !IsValidVolume(settings.Volume)
			|| !std::isfinite(settings.Pitch) || settings.Pitch < 0.25f
			|| settings.Pitch > 4.0f)
		{
			error = "Audio voice settings are invalid";
			return 0;
		}
		VoiceRecord record;
		record.Clip = std::move(clip);
		record.Settings = settings;
		record.Group = group;
		AudioVoiceSettings effective = settings;
		effective.Volume = EffectiveVolume(record);
		record.BackendVoice = m_Device->CreateVoice(record.Clip, effective, error);
		if (record.BackendVoice == 0)
			return 0;
		const AudioVoiceHandle handle = AllocateVoiceHandle();
		m_Voices.emplace(handle, std::move(record));
		return handle;
	}

	AudioVoiceHandle AudioEngine::CreateVoice(AssetHandle clipHandle,
		AudioMixerGroup group, const AudioVoiceSettings& settings,
		std::string& error)
	{
		Ref<AudioClip> clip = LoadClip(clipHandle, error);
		return clip ? CreateVoice(std::move(clip), group, settings, error) : 0;
	}

	AudioVoiceHandle AudioEngine::CreateStreamingVoice(
		Ref<AudioStreamSource> source, AudioMixerGroup group,
		const AudioVoiceSettings& settings, std::string& error)
	{
		error.clear();
		if (!Initialize())
		{
			error = "Audio engine initialization failed";
			return 0;
		}
		if (!source || !IsValidMixerGroup(group) || !IsValidVolume(settings.Volume)
			|| !std::isfinite(settings.Pitch) || settings.Pitch < 0.25f
			|| settings.Pitch > 4.0f)
		{
			error = "Audio streaming voice settings are invalid";
			return 0;
		}
		VoiceRecord record;
		record.StreamSource = std::move(source);
		record.Settings = settings;
		record.Group = group;
		AudioVoiceSettings effective = settings;
		effective.Volume = EffectiveVolume(record);
		record.BackendVoice = m_Device->CreateStreamingVoice(
			record.StreamSource->CreateReader(), effective, error);
		if (record.BackendVoice == 0)
			return 0;
		const AudioVoiceHandle handle = AllocateVoiceHandle();
		m_Voices.emplace(handle, std::move(record));
		return handle;
	}

	AudioVoiceHandle AudioEngine::CreateStreamingVoice(AssetHandle clipHandle,
		AudioMixerGroup group, const AudioVoiceSettings& settings,
		std::string& error)
	{
		Ref<AudioStreamSource> source = LoadStreamSource(clipHandle, error);
		return source ? CreateStreamingVoice(std::move(source), group, settings, error)
			: 0;
	}

	bool AudioEngine::DestroyVoice(AudioVoiceHandle voice)
	{
		const auto iterator = m_Voices.find(voice);
		if (iterator == m_Voices.end())
			return false;
		if (m_Device && iterator->second.BackendVoice != 0)
			m_Device->DestroyVoice(iterator->second.BackendVoice);
		m_Voices.erase(iterator);
		return true;
	}

	bool AudioEngine::RebuildVoice(VoiceRecord& voice, std::string& error)
	{
		error.clear();
		if (!m_Device)
		{
			error = "Audio device is unavailable";
			return false;
		}
		AudioVoiceSettings effective = voice.Settings;
		effective.Volume = EffectiveVolume(voice);
		voice.BackendVoice = voice.StreamSource
			? m_Device->CreateStreamingVoice(voice.StreamSource->CreateReader(),
				effective, error)
			: m_Device->CreateVoice(voice.Clip, effective, error);
		if (voice.BackendVoice == 0)
			return false;
		if (!m_Device->SetSpatial(voice.BackendVoice, voice.Spatial))
		{
			error = "Audio device rejected restored spatial settings";
			return false;
		}
		if (voice.DesiredState == AudioPlaybackState::Playing
			&& !m_Device->Play(voice.BackendVoice))
		{
			error = "Audio device rejected restored playback";
			return false;
		}
		if (voice.DesiredState == AudioPlaybackState::Paused
			&& (!m_Device->Play(voice.BackendVoice)
				|| !m_Device->Pause(voice.BackendVoice)))
		{
			error = "Audio device rejected restored paused playback";
			return false;
		}
		return true;
	}

	bool AudioEngine::RecoverDevice()
	{
		if (!m_Device)
			return false;
		const std::string failedBackend = m_Device->GetBackendName();
		m_Device->Shutdown();
		m_Device.reset();

		std::string recoveryReason;
		if (m_UseDefaultRecovery)
			m_Device = CreateDefaultAudioDevice(recoveryReason);
		else
		{
			m_Device = CreateNullAudioDevice();
			if (m_Device && !m_Device->Initialize(recoveryReason))
				m_Device.reset();
		}
		if (!m_Device)
		{
			m_FallbackReason = recoveryReason.empty()
				? "audio device recovery failed" : recoveryReason;
			return false;
		}
		++m_DeviceGeneration;
		m_FallbackReason = recoveryReason.empty()
			? "Recovered after " + failedBackend + " became unavailable"
			: recoveryReason;
		for (auto& [handle, voice] : m_Voices)
		{
			(void)handle;
			voice.BackendVoice = 0;
			std::string voiceError;
			if (!RebuildVoice(voice, voiceError))
				TC_Core_Warn("Could not rebuild an audio voice after device loss: {0}",
					voiceError);
		}
		TC_Core_Warn("Audio backend '{0}' was lost; active voices were rebuilt on '{1}'",
			failedBackend, m_Device->GetBackendName());
		return true;
	}

	bool AudioEngine::Play(AudioVoiceHandle voice)
	{
		auto iterator = m_Voices.find(voice);
		if (iterator == m_Voices.end() || !m_Device)
			return false;
		if (iterator->second.BackendVoice == 0)
		{
			std::string error;
			if (!RebuildVoice(iterator->second, error))
				return false;
		}
		if (!m_Device->Play(iterator->second.BackendVoice))
			return false;
		iterator->second.DesiredState = AudioPlaybackState::Playing;
		return true;
	}

	bool AudioEngine::Pause(AudioVoiceHandle voice)
	{
		auto iterator = m_Voices.find(voice);
		if (iterator == m_Voices.end() || !m_Device
			|| iterator->second.BackendVoice == 0
			|| !m_Device->Pause(iterator->second.BackendVoice))
			return false;
		iterator->second.DesiredState = AudioPlaybackState::Paused;
		return true;
	}

	bool AudioEngine::Stop(AudioVoiceHandle voice)
	{
		auto iterator = m_Voices.find(voice);
		if (iterator == m_Voices.end())
			return false;
		if (m_Device && iterator->second.BackendVoice != 0
			&& !m_Device->Stop(iterator->second.BackendVoice))
			return false;
		iterator->second.DesiredState = AudioPlaybackState::Stopped;
		return true;
	}

	bool AudioEngine::SetLoop(AudioVoiceHandle voice, bool loop)
	{
		auto iterator = m_Voices.find(voice);
		if (iterator == m_Voices.end())
			return false;
		if (m_Device && iterator->second.BackendVoice != 0
			&& !m_Device->SetLoop(iterator->second.BackendVoice, loop))
			return false;
		iterator->second.Settings.Loop = loop;
		return true;
	}

	bool AudioEngine::SetVolume(AudioVoiceHandle voice, float volume)
	{
		if (!IsValidVolume(volume))
			return false;
		auto iterator = m_Voices.find(voice);
		if (iterator == m_Voices.end())
			return false;
		VoiceRecord updated = iterator->second;
		updated.Settings.Volume = volume;
		if (!ApplyVolume(voice, updated))
			return false;
		iterator->second.Settings.Volume = volume;
		return true;
	}

	bool AudioEngine::SetPitch(AudioVoiceHandle voice, float pitch)
	{
		if (!std::isfinite(pitch) || pitch < 0.25f || pitch > 4.0f)
			return false;
		auto iterator = m_Voices.find(voice);
		if (iterator == m_Voices.end())
			return false;
		if (m_Device && iterator->second.BackendVoice != 0
			&& !m_Device->SetPitch(iterator->second.BackendVoice, pitch))
			return false;
		iterator->second.Settings.Pitch = pitch;
		return true;
	}

	bool AudioEngine::SetSpatial(AudioVoiceHandle voice,
		const AudioSpatialSettings& settings)
	{
		if (!std::isfinite(settings.Pan) || !std::isfinite(settings.DistanceGain)
			|| settings.Pan < -1.0f || settings.Pan > 1.0f
			|| settings.DistanceGain < 0.0f || settings.DistanceGain > 1.0f)
			return false;
		auto iterator = m_Voices.find(voice);
		if (iterator == m_Voices.end())
			return false;
		if (m_Device && iterator->second.BackendVoice != 0
			&& !m_Device->SetSpatial(iterator->second.BackendVoice, settings))
			return false;
		iterator->second.Spatial = settings;
		return true;
	}

	bool AudioEngine::SetMixerGroup(AudioVoiceHandle voice, AudioMixerGroup group)
	{
		if (!IsValidMixerGroup(group))
			return false;
		auto iterator = m_Voices.find(voice);
		if (iterator == m_Voices.end())
			return false;
		VoiceRecord updated = iterator->second;
		updated.Group = group;
		if (!ApplyVolume(voice, updated))
			return false;
		iterator->second.Group = group;
		return true;
	}

	AudioPlaybackState AudioEngine::GetState(AudioVoiceHandle voice) const
	{
		const auto iterator = m_Voices.find(voice);
		return iterator == m_Voices.end()
			? AudioPlaybackState::Stopped : iterator->second.DesiredState;
	}

	double AudioEngine::GetPlaybackSeconds(AudioVoiceHandle voice) const
	{
		const auto iterator = m_Voices.find(voice);
		return iterator != m_Voices.end() && m_Device
			&& iterator->second.BackendVoice != 0
			? m_Device->GetPlaybackSeconds(iterator->second.BackendVoice) : 0.0;
	}

	bool AudioEngine::SetMixerVolume(AudioMixerGroup group, float volume)
	{
		if (!IsValidMixerGroup(group) || !IsValidVolume(volume))
			return false;
		const size_t index = static_cast<size_t>(group);
		const float previous = m_MixerVolumes[index];
		m_MixerVolumes[index] = volume;
		for (const auto& [handle, voice] : m_Voices)
		{
			if (group != AudioMixerGroup::Master && voice.Group != group)
				continue;
			if (!ApplyVolume(handle, voice))
			{
				m_MixerVolumes[index] = previous;
				for (const auto& [rollbackHandle, rollbackVoice] : m_Voices)
					ApplyVolume(rollbackHandle, rollbackVoice);
				return false;
			}
		}
		return true;
	}

	float AudioEngine::GetMixerVolume(AudioMixerGroup group) const
	{
		return IsValidMixerGroup(group)
			? m_MixerVolumes[static_cast<size_t>(group)] : 0.0f;
	}

	void AudioEngine::Update(double deltaSeconds)
	{
		if (!m_Device)
			return;
		m_Device->Update(deltaSeconds);
		if (!m_Device->IsOperational())
		{
			RecoverDevice();
			return;
		}
		for (auto& [handle, voice] : m_Voices)
		{
			(void)handle;
			if (voice.BackendVoice != 0
				&& voice.DesiredState == AudioPlaybackState::Playing
				&& m_Device->GetState(voice.BackendVoice)
					== AudioPlaybackState::Stopped)
				voice.DesiredState = AudioPlaybackState::Stopped;
		}
	}

	bool AudioEngine::IsValidMixerGroup(AudioMixerGroup group)
	{
		return group == AudioMixerGroup::Master || group == AudioMixerGroup::Music
			|| group == AudioMixerGroup::SFX;
	}

	bool AudioEngine::IsValidVolume(float volume)
	{
		return std::isfinite(volume) && volume >= 0.0f && volume <= 4.0f;
	}

	float AudioEngine::EffectiveVolume(const VoiceRecord& voice) const
	{
		const float master = m_MixerVolumes[static_cast<size_t>(AudioMixerGroup::Master)];
		const float group = voice.Group == AudioMixerGroup::Master ? 1.0f
			: m_MixerVolumes[static_cast<size_t>(voice.Group)];
		return std::clamp(voice.Settings.Volume * master * group, 0.0f, 4.0f);
	}

	bool AudioEngine::ApplyVolume(AudioVoiceHandle,
		const VoiceRecord& voice)
	{
		return !m_Device || voice.BackendVoice == 0
			|| m_Device->SetVolume(voice.BackendVoice, EffectiveVolume(voice));
	}

	bool BuildAudioOutputMatrix(uint32_t sourceChannels, uint32_t outputChannels,
		const AudioSpatialSettings& settings, std::vector<float>& matrix)
	{
		matrix.clear();
		if (sourceChannels == 0 || outputChannels == 0
			|| sourceChannels > 64 || outputChannels > 64
			|| !std::isfinite(settings.Pan)
			|| !std::isfinite(settings.DistanceGain)
			|| settings.Pan < -1.0f || settings.Pan > 1.0f
			|| settings.DistanceGain < 0.0f || settings.DistanceGain > 1.0f)
			return false;

		std::vector<float> candidate(static_cast<size_t>(sourceChannels)
			* outputChannels, 0.0f);
		if (sourceChannels == 1)
		{
			if (outputChannels == 1)
				candidate[0] = settings.DistanceGain;
			else
			{
				const float angle = (settings.Pan + 1.0f)
					* 0.7853981633974483f;
				candidate[0] = std::cos(angle) * settings.DistanceGain;
				candidate[1] = std::sin(angle) * settings.DistanceGain;
			}
		}
		else
		{
			// A pan pair represents one point emitter. Applying it independently to
			// each source channel sums stereo into identical L/R output. Keep each
			// authored channel discrete and apply only distance attenuation.
			for (uint32_t source = 0; source < sourceChannels; ++source)
			{
				const uint32_t destination = source % outputChannels;
				candidate[static_cast<size_t>(source) * outputChannels
					+ destination] = settings.DistanceGain;
			}
		}
		matrix = std::move(candidate);
		return true;
	}

	AudioSpatialSettings CalculateAudioSpatial2D(float sourceX, float sourceY,
		float listenerX, float listenerY, float listenerRightX,
		float listenerRightY, float spatialBlend, float minDistance,
		float maxDistance)
	{
		AudioSpatialSettings result;
		if (!std::isfinite(sourceX) || !std::isfinite(sourceY)
			|| !std::isfinite(listenerX) || !std::isfinite(listenerY)
			|| !std::isfinite(listenerRightX) || !std::isfinite(listenerRightY)
			|| !std::isfinite(spatialBlend) || !std::isfinite(minDistance)
			|| !std::isfinite(maxDistance) || minDistance < 0.0f
			|| maxDistance <= minDistance)
			return result;
		const float blend = std::clamp(spatialBlend, 0.0f, 1.0f);
		const float dx = sourceX - listenerX;
		const float dy = sourceY - listenerY;
		const float distance = std::sqrt(dx * dx + dy * dy);
		const float rightLength = std::sqrt(listenerRightX * listenerRightX
			+ listenerRightY * listenerRightY);
		if (distance > 1.0e-6f && rightLength > 1.0e-6f)
			result.Pan = std::clamp((dx * listenerRightX + dy * listenerRightY)
				/ (distance * rightLength), -1.0f, 1.0f) * blend;
		float distanceGain = 1.0f;
		if (distance >= maxDistance)
			distanceGain = 0.0f;
		else if (distance > minDistance)
			distanceGain = 1.0f - (distance - minDistance)
				/ (maxDistance - minDistance);
		result.DistanceGain = 1.0f + (distanceGain - 1.0f) * blend;
		return result;
	}

}
