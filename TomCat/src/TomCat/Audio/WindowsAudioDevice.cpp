#include "tcpch.h"
#include "AudioDevice.h"

#if defined(TC_PLATFORM_WINDOWS)
	#include <Windows.h>
	#include <xaudio2.h>
#endif

#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <deque>
#include <unordered_map>
#include <vector>

namespace TomCat {

	namespace {

#if defined(TC_PLATFORM_WINDOWS)

		using XAudio2CreateDynamic = HRESULT(WINAPI*)(IXAudio2**, UINT32,
			XAUDIO2_PROCESSOR);

		bool ValidVolume(float value)
		{
			return std::isfinite(value) && value >= 0.0f && value <= 4.0f;
		}

		bool ValidPitch(float value)
		{
			return std::isfinite(value) && value >= XAUDIO2_MIN_FREQ_RATIO
				&& value <= 4.0f;
		}

		class WindowsAudioDevice final : public IAudioDevice
		{
		public:
			class EngineCallback final : public IXAudio2EngineCallback
			{
			public:
				explicit EngineCallback(WindowsAudioDevice& owner) : m_Owner(owner) {}
				void STDMETHODCALLTYPE OnProcessingPassStart() override {}
				void STDMETHODCALLTYPE OnProcessingPassEnd() override {}
				void STDMETHODCALLTYPE OnCriticalError(HRESULT) override
				{
					m_Owner.m_DeviceLost.store(true, std::memory_order_release);
				}
			private:
				WindowsAudioDevice& m_Owner;
			};

			WindowsAudioDevice() : m_EngineCallback(*this) {}
			~WindowsAudioDevice() override { Shutdown(); }

			bool Initialize(std::string& error) override
			{
				error.clear();
				if (m_Engine)
					return true;
				m_Module = ::LoadLibraryW(L"xaudio2_9.dll");
				if (!m_Module)
					m_Module = ::LoadLibraryW(L"xaudio2_8.dll");
				if (!m_Module)
				{
					error = "XAudio2 runtime DLL is unavailable";
					return false;
				}
				const auto create = reinterpret_cast<XAudio2CreateDynamic>(
					::GetProcAddress(m_Module, "XAudio2Create"));
				if (!create)
				{
					error = "XAudio2Create entry point is unavailable";
					Shutdown();
					return false;
				}
				HRESULT result = create(&m_Engine, 0, XAUDIO2_DEFAULT_PROCESSOR);
				if (FAILED(result) || !m_Engine)
				{
					error = "XAudio2 engine initialization failed";
					Shutdown();
					return false;
				}
				result = m_Engine->RegisterForCallbacks(&m_EngineCallback);
				if (FAILED(result))
				{
					error = "XAudio2 could not register its device-loss callback";
					Shutdown();
					return false;
				}
				m_CallbackRegistered = true;
				result = m_Engine->CreateMasteringVoice(&m_MasteringVoice);
				if (FAILED(result) || !m_MasteringVoice)
				{
					error = "No usable Windows audio endpoint was found";
					Shutdown();
					return false;
				}
				XAUDIO2_VOICE_DETAILS details{};
				m_MasteringVoice->GetVoiceDetails(&details);
				m_OutputChannels = (std::max)(1U, details.InputChannels);
				m_DeviceLost.store(false, std::memory_order_release);
				return true;
			}

			void Shutdown() override
			{
				for (auto& [handle, voice] : m_Voices)
				{
					(void)handle;
					if (voice.Native)
						voice.Native->DestroyVoice();
				}
				m_Voices.clear();
				if (m_MasteringVoice)
				{
					m_MasteringVoice->DestroyVoice();
					m_MasteringVoice = nullptr;
				}
				if (m_Engine)
				{
					if (m_CallbackRegistered)
					{
						m_Engine->UnregisterForCallbacks(&m_EngineCallback);
						m_CallbackRegistered = false;
					}
					m_Engine->Release();
					m_Engine = nullptr;
				}
				if (m_Module)
				{
					::FreeLibrary(m_Module);
					m_Module = nullptr;
				}
				m_OutputChannels = 0;
				m_DeviceLost.store(false, std::memory_order_release);
			}

			bool IsHardwareAvailable() const override
			{
				return m_Engine != nullptr && m_MasteringVoice != nullptr
					&& !m_DeviceLost.load(std::memory_order_acquire);
			}

			bool IsOperational() const override { return IsHardwareAvailable(); }

			const char* GetBackendName() const override { return "Windows XAudio2"; }

			AudioVoiceHandle CreateVoice(Ref<AudioClip> clip,
				const AudioVoiceSettings& settings, std::string& error) override
			{
				error.clear();
				if (!IsHardwareAvailable() || !clip || clip->GetPcmBytes().empty())
				{
					error = "XAudio2 voice requires an initialized endpoint and a nonempty clip";
					return 0;
				}
				if (!ValidVolume(settings.Volume) || !ValidPitch(settings.Pitch))
				{
					error = "Audio volume or pitch is outside the supported range";
					return 0;
				}
				WAVEFORMATEX format{};
				format.wFormatTag = clip->IsFloatingPoint()
					? WAVE_FORMAT_IEEE_FLOAT : WAVE_FORMAT_PCM;
				format.nChannels = clip->GetChannels();
				format.nSamplesPerSec = clip->GetSampleRate();
				format.nAvgBytesPerSec = clip->GetAverageBytesPerSecond();
				format.nBlockAlign = clip->GetBlockAlign();
				format.wBitsPerSample = clip->GetBitsPerSample();

				IXAudio2SourceVoice* native = nullptr;
				const HRESULT result = m_Engine->CreateSourceVoice(&native, &format,
					0, 4.0f);
				if (FAILED(result) || !native)
				{
					error = "XAudio2 could not create a source voice";
					return 0;
				}
				if (FAILED(native->SetVolume(settings.Volume))
					|| FAILED(native->SetFrequencyRatio(settings.Pitch)))
				{
					native->DestroyVoice();
					error = "XAudio2 rejected initial voice parameters";
					return 0;
				}
				AudioVoiceHandle handle = m_NextVoice++;
				if (handle == 0)
					handle = m_NextVoice++;
				Voice voice;
				voice.Native = native;
				voice.Clip = std::move(clip);
				voice.SourceChannels = voice.Clip->GetChannels();
				voice.SampleRate = voice.Clip->GetSampleRate();
				voice.Settings = settings;
				m_Voices.emplace(handle, std::move(voice));
				return handle;
			}

			AudioVoiceHandle CreateStreamingVoice(
				std::unique_ptr<IAudioStream> stream,
				const AudioVoiceSettings& settings, std::string& error) override
			{
				error.clear();
				if (!IsHardwareAvailable() || !stream
					|| stream->GetFrameCount() == 0)
				{
					error = "XAudio2 streaming voice requires an initialized endpoint and a nonempty stream";
					return 0;
				}
				if (!ValidVolume(settings.Volume) || !ValidPitch(settings.Pitch))
				{
					error = "Audio volume or pitch is outside the supported range";
					return 0;
				}

				const AudioStreamFormat& source = stream->GetFormat();
				WAVEFORMATEX format{};
				format.wFormatTag = source.FloatingPoint
					? WAVE_FORMAT_IEEE_FLOAT : WAVE_FORMAT_PCM;
				format.nChannels = source.Channels;
				format.nSamplesPerSec = source.SampleRate;
				format.nAvgBytesPerSec = source.AverageBytesPerSecond;
				format.nBlockAlign = source.BlockAlign;
				format.wBitsPerSample = source.BitsPerSample;

				IXAudio2SourceVoice* native = nullptr;
				HRESULT result = m_Engine->CreateSourceVoice(&native, &format, 0, 4.0f);
				if (FAILED(result) || !native)
				{
					MarkFailure(result);
					error = "XAudio2 could not create a streaming source voice";
					return 0;
				}
				if (FAILED(result = native->SetVolume(settings.Volume))
					|| FAILED(result = native->SetFrequencyRatio(settings.Pitch)))
				{
					MarkFailure(result);
					native->DestroyVoice();
					error = "XAudio2 rejected initial streaming voice parameters";
					return 0;
				}

				AudioVoiceHandle handle = m_NextVoice++;
				if (handle == 0)
					handle = m_NextVoice++;
				Voice voice;
				voice.Native = native;
				voice.Stream = std::move(stream);
				voice.SourceChannels = source.Channels;
				voice.SampleRate = source.SampleRate;
				voice.Settings = settings;
				const size_t alignedChunk = StreamChunkBytes
					- StreamChunkBytes % source.BlockAlign;
				for (auto& buffer : voice.StreamBuffers)
					buffer.resize(alignedChunk);
				m_Voices.emplace(handle, std::move(voice));
				return handle;
			}

			bool DestroyVoice(AudioVoiceHandle voice) override
			{
				const auto iterator = m_Voices.find(voice);
				if (iterator == m_Voices.end())
					return false;
				iterator->second.Native->DestroyVoice();
				m_Voices.erase(iterator);
				return true;
			}

			bool Play(AudioVoiceHandle voice) override
			{
				Voice* value = Find(voice);
				if (!value)
					return false;
				if (value->State == AudioPlaybackState::Playing)
					return true;
				if (value->State == AudioPlaybackState::Stopped
					&& !(value->Stream ? PrepareStream(*value) : SubmitClip(*value)))
					return false;
				const HRESULT result = value->Native->Start();
				if (FAILED(result))
				{
					MarkFailure(result);
					return false;
				}
				value->State = AudioPlaybackState::Playing;
				return true;
			}

			bool Pause(AudioVoiceHandle voice) override
			{
				Voice* value = Find(voice);
				if (!value || value->State != AudioPlaybackState::Playing)
					return false;
				const HRESULT result = value->Native->Stop();
				if (FAILED(result))
				{
					MarkFailure(result);
					return false;
				}
				value->State = AudioPlaybackState::Paused;
				return true;
			}

			bool Stop(AudioVoiceHandle voice) override
			{
				Voice* value = Find(voice);
				if (!value)
					return false;
				HRESULT result = value->Native->Stop();
				const bool success = SUCCEEDED(result)
					&& SUCCEEDED(result = value->Native->FlushSourceBuffers());
				if (!success)
					MarkFailure(result);
				if (success)
				{
					value->State = AudioPlaybackState::Stopped;
					value->InFlight.clear();
					value->NextStreamBuffer = 0;
					value->StreamEndSubmitted = false;
					if (value->Stream)
						value->Stream->SeekFrame(0);
				}
				return success;
			}

			bool SetLoop(AudioVoiceHandle voice, bool loop) override
			{
				Voice* value = Find(voice);
				if (!value)
					return false;
				if (value->Settings.Loop == loop)
					return true;
				const AudioPlaybackState previous = value->State;
				if (previous != AudioPlaybackState::Stopped)
				{
					HRESULT result = value->Native->Stop();
					if (FAILED(result)
						|| FAILED(result = value->Native->FlushSourceBuffers()))
					{
						MarkFailure(result);
						return false;
					}
					value->State = AudioPlaybackState::Stopped;
				}
				value->Settings.Loop = loop;
				if (previous == AudioPlaybackState::Stopped)
					return true;
				if (!(value->Stream ? PrepareStream(*value) : SubmitClip(*value)))
					return false;
				if (previous == AudioPlaybackState::Playing)
				{
					const HRESULT result = value->Native->Start();
					if (FAILED(result))
					{
						MarkFailure(result);
						return false;
					}
					value->State = AudioPlaybackState::Playing;
				}
				else
					value->State = AudioPlaybackState::Paused;
				return true;
			}

			bool SetVolume(AudioVoiceHandle voice, float volume) override
			{
				Voice* value = Find(voice);
				if (!value || !ValidVolume(volume))
					return false;
				const HRESULT result = value->Native->SetVolume(volume);
				if (FAILED(result))
				{
					MarkFailure(result);
					return false;
				}
				value->Settings.Volume = volume;
				return true;
			}

			bool SetPitch(AudioVoiceHandle voice, float pitch) override
			{
				Voice* value = Find(voice);
				if (!value || !ValidPitch(pitch))
					return false;
				const HRESULT result = value->Native->SetFrequencyRatio(pitch);
				if (FAILED(result))
				{
					MarkFailure(result);
					return false;
				}
				value->Settings.Pitch = pitch;
				return true;
			}

			bool SetSpatial(AudioVoiceHandle voice,
				const AudioSpatialSettings& settings) override
			{
				Voice* value = Find(voice);
				if (!value)
					return false;
				std::vector<float> matrix;
				if (!BuildAudioOutputMatrix(value->SourceChannels, m_OutputChannels,
					settings, matrix))
					return false;
				const HRESULT result = value->Native->SetOutputMatrix(m_MasteringVoice,
					value->SourceChannels, m_OutputChannels, matrix.data());
				if (FAILED(result))
				{
					MarkFailure(result);
					return false;
				}
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
				if (iterator == m_Voices.end())
					return 0.0;
				XAUDIO2_VOICE_STATE state{};
				iterator->second.Native->GetState(&state);
				return iterator->second.SampleRate == 0 ? 0.0
					: static_cast<double>(state.SamplesPlayed)
						/ iterator->second.SampleRate;
			}

			void Update(double) override
			{
				for (auto& [handle, voice] : m_Voices)
				{
					(void)handle;
					if (voice.State != AudioPlaybackState::Playing)
						continue;
					XAUDIO2_VOICE_STATE state{};
					voice.Native->GetState(&state, XAUDIO2_VOICE_NOSAMPLESPLAYED);
					if (voice.Stream)
					{
						ReconcileCompletedBuffers(voice, state.BuffersQueued);
						if (voice.StreamEndSubmitted && state.BuffersQueued == 0)
							voice.State = AudioPlaybackState::Stopped;
						else
							RefillStream(voice);
					}
					else if (state.BuffersQueued == 0 && !voice.Settings.Loop)
						voice.State = AudioPlaybackState::Stopped;
				}
			}

		private:
			struct Voice
			{
				IXAudio2SourceVoice* Native = nullptr;
				Ref<AudioClip> Clip;
				std::unique_ptr<IAudioStream> Stream;
				AudioVoiceSettings Settings;
				AudioSpatialSettings Spatial;
				AudioPlaybackState State = AudioPlaybackState::Stopped;
				uint32_t SourceChannels = 0;
				uint32_t SampleRate = 0;
				std::array<std::vector<uint8_t>, 4> StreamBuffers;
				std::deque<size_t> InFlight;
				size_t NextStreamBuffer = 0;
				bool StreamEndSubmitted = false;
			};

			Voice* Find(AudioVoiceHandle voice)
			{
				const auto iterator = m_Voices.find(voice);
				return iterator == m_Voices.end() ? nullptr : &iterator->second;
			}

			bool SubmitClip(Voice& voice)
			{
				XAUDIO2_BUFFER buffer{};
				buffer.AudioBytes = static_cast<UINT32>(voice.Clip->GetPcmBytes().size());
				buffer.pAudioData = voice.Clip->GetPcmBytes().data();
				buffer.Flags = XAUDIO2_END_OF_STREAM;
				buffer.LoopCount = voice.Settings.Loop ? XAUDIO2_LOOP_INFINITE : 0;
				const HRESULT result = voice.Native->SubmitSourceBuffer(&buffer);
				if (FAILED(result))
					MarkFailure(result);
				return SUCCEEDED(result);
			}

			void ReconcileCompletedBuffers(Voice& voice, uint32_t queued)
			{
				while (voice.InFlight.size() > queued)
					voice.InFlight.pop_front();
			}

			bool RefillStream(Voice& voice)
			{
				if (!voice.Stream || voice.StreamEndSubmitted)
					return true;
				while (voice.InFlight.size() < voice.StreamBuffers.size())
				{
					const size_t index = voice.NextStreamBuffer;
					auto& storage = voice.StreamBuffers[index];
					size_t bytes = voice.Stream->ReadFrames(storage);
					if (bytes == 0 && voice.Settings.Loop)
					{
						if (!voice.Stream->SeekFrame(0))
							return false;
						bytes = voice.Stream->ReadFrames(storage);
					}
					if (bytes == 0)
					{
						voice.StreamEndSubmitted = true;
						break;
					}

					const bool atEnd = voice.Stream->GetFramePosition()
						>= voice.Stream->GetFrameCount();
					XAUDIO2_BUFFER buffer{};
					buffer.AudioBytes = static_cast<UINT32>(bytes);
					buffer.pAudioData = storage.data();
					if (atEnd && !voice.Settings.Loop)
					{
						buffer.Flags = XAUDIO2_END_OF_STREAM;
						voice.StreamEndSubmitted = true;
					}
					const HRESULT result = voice.Native->SubmitSourceBuffer(&buffer);
					if (FAILED(result))
					{
						MarkFailure(result);
						return false;
					}
					voice.InFlight.push_back(index);
					voice.NextStreamBuffer = (index + 1) % voice.StreamBuffers.size();
					if (atEnd && voice.Settings.Loop
						&& !voice.Stream->SeekFrame(0))
						return false;
					if (voice.StreamEndSubmitted)
						break;
				}
				return true;
			}

			bool PrepareStream(Voice& voice)
			{
				voice.InFlight.clear();
				voice.NextStreamBuffer = 0;
				voice.StreamEndSubmitted = false;
				return voice.Stream && voice.Stream->SeekFrame(0)
					&& RefillStream(voice);
			}

			void MarkFailure(HRESULT result)
			{
				if (FAILED(result))
					m_DeviceLost.store(true, std::memory_order_release);
			}

			static constexpr size_t StreamChunkBytes = 64 * 1024;
			HMODULE m_Module = nullptr;
			IXAudio2* m_Engine = nullptr;
			IXAudio2MasteringVoice* m_MasteringVoice = nullptr;
			EngineCallback m_EngineCallback;
			bool m_CallbackRegistered = false;
			uint32_t m_OutputChannels = 0;
			std::atomic<bool> m_DeviceLost{ false };
			AudioVoiceHandle m_NextVoice = 1;
			std::unordered_map<AudioVoiceHandle, Voice> m_Voices;
		};

#endif

	}

	std::unique_ptr<IAudioDevice> CreateDefaultAudioDevice(
		std::string& fallbackReason)
	{
		fallbackReason.clear();
#if defined(TC_PLATFORM_WINDOWS)
		auto device = std::make_unique<WindowsAudioDevice>();
		if (device->Initialize(fallbackReason))
			return device;
#else
		fallbackReason = "No platform audio backend is compiled for this target";
#endif
		auto fallback = CreateNullAudioDevice();
		std::string nullError;
		if (!fallback->Initialize(nullError) && fallbackReason.empty())
			fallbackReason = std::move(nullError);
		return fallback;
	}

}
