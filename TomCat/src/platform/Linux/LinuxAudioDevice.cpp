#include "tcpch.h"
#if defined(TC_PLATFORM_LINUX)
#include "LinuxAudioDevice.h"
#include <AL/al.h>
#include <AL/alc.h>
#include <AL/alext.h>
#include <array>
#include <cmath>
#include <cstring>
#include <deque>
#include <limits>
#include <unordered_map>

namespace TomCat {
namespace {
constexpr size_t ChunkFrames = 4096;
constexpr size_t BufferCount = 4;
bool ValidVolume(float value) { return std::isfinite(value) && value >= 0 && value <= 4; }
bool ValidPitch(float value) { return std::isfinite(value) && value >= MinimumAudioPitch && value <= MaximumAudioPitch; }

class OpenALAudioDevice final : public IAudioDevice {
    struct Voice {
        ALuint Source = 0;
        std::array<ALuint, BufferCount> Buffers{};
        Ref<AudioClip> Clip;
        std::unique_ptr<IAudioStream> Stream;
        AudioStreamFormat Format;
        AudioVoiceSettings Settings;
        AudioSpatialSettings Spatial;
        AudioPlaybackState State = AudioPlaybackState::Stopped;
        uint64_t Frames = 0, Consumed = 0;
        std::deque<size_t> QueuedFrames;
        std::vector<uint8_t> Scratch;
        std::vector<float> Samples;
    };
public:
    explicit OpenALAudioDevice(bool loopback) : m_Loopback(loopback) {}
    ~OpenALAudioDevice() override { Shutdown(); }
    bool Initialize(std::string& error) override {
        error.clear();
        if (m_Context) return IsOperational();
        if (m_Loopback) {
            auto open = reinterpret_cast<LPALCLOOPBACKOPENDEVICESOFT>(alcGetProcAddress(nullptr, "alcLoopbackOpenDeviceSOFT"));
            m_Render = reinterpret_cast<LPALCRENDERSAMPLESSOFT>(alcGetProcAddress(nullptr, "alcRenderSamplesSOFT"));
            if (open && m_Render) m_Device = open(nullptr);
        } else m_Device = alcOpenDevice(nullptr);
        if (!m_Device) { error = "OpenAL Soft could not open an audio device"; return false; }
        const ALCint attributes[] = { ALC_FREQUENCY, 48000,
            ALC_FORMAT_CHANNELS_SOFT, ALC_STEREO_SOFT, ALC_FORMAT_TYPE_SOFT, ALC_FLOAT_SOFT, 0 };
        m_Context = alcCreateContext(m_Device, m_Loopback ? attributes : nullptr);
        if (!m_Context || !alcMakeContextCurrent(m_Context) || !alIsExtensionPresent("AL_EXT_FLOAT32")) {
            error = "OpenAL Soft could not create a floating-point audio context";
            Shutdown(); return false;
        }
        m_Disconnect = alcIsExtensionPresent(m_Device, "ALC_EXT_disconnect");
        alDistanceModel(AL_NONE);
        alDopplerFactor(0);
        alListenerf(AL_GAIN, 4); // Preserve the engine's [0, 4] gain range.
        return true;
    }
    void Shutdown() override {
        if (m_Context) {
            alcMakeContextCurrent(m_Context);
            while (!m_Voices.empty()) DestroyVoice(m_Voices.begin()->first);
            alcMakeContextCurrent(nullptr);
            alcDestroyContext(m_Context);
            m_Context = nullptr;
        }
        if (m_Device) { alcCloseDevice(m_Device); m_Device = nullptr; }
    }
    bool IsHardwareAvailable() const override { return !m_Loopback && IsOperational(); }
    bool IsOperational() const override {
        if (!m_Context || !m_Device) return false;
        ALCint connected = ALC_TRUE;
        if (m_Disconnect) alcGetIntegerv(m_Device, ALC_CONNECTED, 1, &connected);
        return connected == ALC_TRUE;
    }
    const char* GetBackendName() const override { return m_Loopback ? "OpenAL Soft (loopback)" : "OpenAL Soft"; }
    AudioVoiceHandle CreateVoice(Ref<AudioClip> clip, const AudioVoiceSettings& settings, std::string& error) override {
        if (!clip) { error = "Audio clip is empty"; return 0; }
        Voice voice;
        voice.Format = {clip->GetChannels(), clip->GetSampleRate(), clip->GetBitsPerSample(),
            clip->GetBlockAlign(), clip->GetAverageBytesPerSecond(), clip->IsFloatingPoint()};
        voice.Frames = clip->GetFrameCount();
        voice.Clip = std::move(clip);
        return Create(std::move(voice), settings, error);
    }
    AudioVoiceHandle CreateStreamingVoice(std::unique_ptr<IAudioStream> stream,
        const AudioVoiceSettings& settings, std::string& error) override {
        if (!stream) { error = "Audio stream is empty"; return 0; }
        Voice voice;
        voice.Format = stream->GetFormat();
        voice.Frames = stream->GetFrameCount();
        voice.Stream = std::move(stream);
        return Create(std::move(voice), settings, error);
    }
    bool DestroyVoice(AudioVoiceHandle handle) override {
        auto* voice = Find(handle);
        if (!voice) return false;
        alDeleteSources(1, &voice->Source);
        alDeleteBuffers(BufferCount, voice->Buffers.data());
        m_Voices.erase(handle);
        return alGetError() == AL_NO_ERROR;
    }
    bool Play(AudioVoiceHandle handle) override {
        auto* voice = Find(handle);
        if (!voice) return false;
        if (GetState(handle) == AudioPlaybackState::Playing) return true;
        if (voice->State == AudioPlaybackState::Stopped && !Prepare(*voice, 0)) return false;
        alSourcePlay(voice->Source);
        if (alGetError() != AL_NO_ERROR) return false;
        voice->State = AudioPlaybackState::Playing;
        return true;
    }
    bool Pause(AudioVoiceHandle handle) override {
        auto* voice = Find(handle);
        if (!voice || GetState(handle) != AudioPlaybackState::Playing) return false;
        alSourcePause(voice->Source);
        voice->State = AudioPlaybackState::Paused;
        return alGetError() == AL_NO_ERROR;
    }
    bool Stop(AudioVoiceHandle handle) override {
        auto* voice = Find(handle);
        if (!voice) return false;
        alSourceStop(voice->Source);
        alSourceRewind(voice->Source);
        voice->State = AudioPlaybackState::Stopped;
        voice->Consumed = 0;
        return alGetError() == AL_NO_ERROR;
    }
    bool SetLoop(AudioVoiceHandle handle, bool loop) override {
        auto* voice = Find(handle);
        if (!voice) return false;
        if (voice->Settings.Loop == loop) return true;
        const uint64_t frame = static_cast<uint64_t>(GetPlaybackSeconds(handle) * voice->Format.SampleRate);
        voice->Settings.Loop = loop;
        if (!voice->Stream) alSourcei(voice->Source, AL_LOOPING, loop);
        else if (voice->State != AudioPlaybackState::Stopped) {
            if (!Prepare(*voice, frame % voice->Frames)) return false;
            if (voice->State == AudioPlaybackState::Playing) alSourcePlay(voice->Source);
        }
        return alGetError() == AL_NO_ERROR;
    }
    bool SetVolume(AudioVoiceHandle handle, float volume) override {
        auto* voice = Find(handle);
        if (!voice || !ValidVolume(volume)) return false;
        voice->Settings.Volume = volume;
        alSourcef(voice->Source, AL_GAIN, volume * voice->Spatial.DistanceGain / 4);
        return alGetError() == AL_NO_ERROR;
    }
    bool SetPitch(AudioVoiceHandle handle, float pitch) override {
        auto* voice = Find(handle);
        if (!voice || !ValidPitch(pitch)) return false;
        alSourcef(voice->Source, AL_PITCH, pitch);
        voice->Settings.Pitch = pitch;
        return alGetError() == AL_NO_ERROR;
    }
    bool SetSpatial(AudioVoiceHandle handle, const AudioSpatialSettings& settings) override {
        auto* voice = Find(handle);
        if (!voice || !std::isfinite(settings.Pan) || !std::isfinite(settings.DistanceGain) ||
            settings.Pan < -1 || settings.Pan > 1 || settings.DistanceGain < 0 || settings.DistanceGain > 1) return false;
        voice->Spatial = settings;
        // OpenAL spatializes mono only; stereo retains channel identity.
        alSource3f(voice->Source, AL_POSITION, settings.Pan, 0, -std::sqrt(1 - settings.Pan * settings.Pan));
        alSourcef(voice->Source, AL_GAIN, voice->Settings.Volume * settings.DistanceGain / 4);
        return alGetError() == AL_NO_ERROR;
    }
    AudioPlaybackState GetState(AudioVoiceHandle handle) const override {
        auto* voice = Find(handle);
        if (!voice) return AudioPlaybackState::Stopped;
        if (voice->State == AudioPlaybackState::Playing && !voice->Stream) {
            ALint state = 0;
            alGetSourcei(voice->Source, AL_SOURCE_STATE, &state);
            if (state == AL_STOPPED) {
                voice->State = AudioPlaybackState::Stopped;
                voice->Consumed = voice->Frames;
            }
        }
        return voice->State;
    }
    double GetPlaybackSeconds(AudioVoiceHandle handle) const override {
        auto* voice = Find(handle);
        if (!voice) return 0;
        GetState(handle);
        ALfloat offset = 0;
        if (voice->State != AudioPlaybackState::Stopped) alGetSourcef(voice->Source, AL_SEC_OFFSET, &offset);
        double cursor = static_cast<double>(voice->Consumed) / voice->Format.SampleRate + offset;
        double duration = static_cast<double>(voice->Frames) / voice->Format.SampleRate;
        return voice->Settings.Loop ? std::fmod(cursor, duration) : std::min(cursor, duration);
    }
    void Update(double) override {
        if (!Activate()) return;
        for (auto& [handle, voice] : m_Voices) {
            if (!voice.Stream || voice.State != AudioPlaybackState::Playing) continue;
            ALint processed = 0;
            alGetSourcei(voice.Source, AL_BUFFERS_PROCESSED, &processed);
            while (processed-- > 0) {
                ALuint buffer = 0;
                alSourceUnqueueBuffers(voice.Source, 1, &buffer);
                voice.Consumed += voice.QueuedFrames.front();
                voice.QueuedFrames.pop_front();
                if (!Fill(voice, buffer)) { voice.State = AudioPlaybackState::Stopped; break; }
            }
            ALint queued = 0, state = 0;
            alGetSourcei(voice.Source, AL_BUFFERS_QUEUED, &queued);
            alGetSourcei(voice.Source, AL_SOURCE_STATE, &state);
            if (!queued) voice.State = AudioPlaybackState::Stopped;
            else if (state != AL_PLAYING && voice.State == AudioPlaybackState::Playing)
                alSourcePlay(voice.Source); // Recover a scheduling underrun without losing queued audio.
        }
    }
    bool Render(std::span<float> samples) {
        if (!m_Loopback || !m_Render || !Activate() || samples.size() % 2) return false;
        // Service bounded streaming queues between mixer blocks, as the game loop does.
        for (size_t offset = 0; offset < samples.size(); offset += 1024) {
            Update(0);
            const auto count = std::min<size_t>(1024, samples.size() - offset);
            m_Render(m_Device, samples.data() + offset, static_cast<ALCsizei>(count / 2));
        }
        Update(0);
        return alcGetError(m_Device) == ALC_NO_ERROR;
    }
private:
    bool Activate() const { return m_Context && alcMakeContextCurrent(m_Context); }
    Voice* Find(AudioVoiceHandle handle) const {
        if (!Activate()) return nullptr;
        const auto it = m_Voices.find(handle);
        return it == m_Voices.end() ? nullptr : &it->second;
    }
    bool Upload(Voice& voice, ALuint buffer, std::span<const uint8_t> bytes) {
        const auto& f = voice.Format;
        const size_t frames = bytes.size() / f.BlockAlign;
        const size_t channels = f.Channels == 1 ? 1 : 2;
        if (frames > static_cast<size_t>(std::numeric_limits<ALsizei>::max()) / (channels * sizeof(float))) return false;
        voice.Samples.assign(frames * channels, 0);
        std::vector<float> matrix;
        if (channels == 2 && !BuildAudioOutputMatrix(f.Channels, 2, {}, matrix)) return false;
        for (size_t frame = 0; frame < frames; ++frame) {
            for (size_t channel = 0; channel < f.Channels; ++channel) {
                const auto* sample = bytes.data() + frame * f.BlockAlign + channel * (f.BitsPerSample / 8);
                float value = 0;
                if (f.FloatingPoint) std::memcpy(&value, sample, sizeof(float));
                else if (f.BitsPerSample == 8) value = (int(sample[0]) - 128) / 128.0f;
                else {
                    uint32_t packed = 0;
                    for (size_t i = 0; i < f.BitsPerSample / 8; ++i) packed |= uint32_t(sample[i]) << (i * 8);
                    const auto signedValue = static_cast<int32_t>(packed << (32 - f.BitsPerSample));
                    value = signedValue / 2147483648.0f;
                }
                if (!std::isfinite(value)) value = 0;
                if (channels == 1) voice.Samples[frame] = value;
                else for (size_t out = 0; out < 2; ++out)
                    voice.Samples[frame * 2 + out] += value * matrix[channel * 2 + out];
            }
        }
        alBufferData(buffer, channels == 1 ? AL_FORMAT_MONO_FLOAT32 : AL_FORMAT_STEREO_FLOAT32,
            voice.Samples.data(), static_cast<ALsizei>(voice.Samples.size() * sizeof(float)), f.SampleRate);
        return alGetError() == AL_NO_ERROR;
    }
    bool Fill(Voice& voice, ALuint buffer) {
        size_t frames = voice.Stream->ReadFrames(voice.Scratch);
        if (!frames && voice.Settings.Loop) {
            if (!voice.Stream->SeekFrame(0)) return false;
            frames = voice.Stream->ReadFrames(voice.Scratch);
        }
        if (!frames) return true; // EOF; no empty buffers enter the queue.
        if (frames > ChunkFrames || !Upload(voice, buffer,
            std::span<const uint8_t>(voice.Scratch.data(), frames * voice.Format.BlockAlign))) return false;
        alSourceQueueBuffers(voice.Source, 1, &buffer);
        voice.QueuedFrames.push_back(frames);
        return alGetError() == AL_NO_ERROR;
    }
    bool Prepare(Voice& voice, uint64_t frame) {
        alSourceStop(voice.Source);
        if (!voice.Stream) {
            alSourceRewind(voice.Source);
            voice.Consumed = 0;
            return alGetError() == AL_NO_ERROR;
        }
        alSourcei(voice.Source, AL_BUFFER, 0);
        voice.QueuedFrames.clear();
        voice.Consumed = frame;
        if (!voice.Stream->SeekFrame(frame)) return false;
        for (ALuint buffer : voice.Buffers) if (!Fill(voice, buffer)) return false;
        return !voice.QueuedFrames.empty();
    }
    AudioVoiceHandle Create(Voice voice, const AudioVoiceSettings& settings, std::string& error) {
        error.clear();
        const auto& f = voice.Format;
        if (!Activate() || !IsOperational() || !voice.Frames || !f.SampleRate || !f.Channels || f.Channels > 8 ||
            f.BlockAlign != f.Channels * (f.BitsPerSample / 8) ||
            (f.FloatingPoint ? f.BitsPerSample != 32 :
                (f.BitsPerSample != 8 && f.BitsPerSample != 16 && f.BitsPerSample != 24 && f.BitsPerSample != 32)) ||
            !ValidVolume(settings.Volume) || !ValidPitch(settings.Pitch)) {
            error = "OpenAL voice requires valid PCM and voice settings"; return 0;
        }
        alGetError();
        alGenSources(1, &voice.Source);
        alGenBuffers(BufferCount, voice.Buffers.data());
        voice.Settings = settings;
        alSourcei(voice.Source, AL_SOURCE_RELATIVE, AL_TRUE);
        alSource3f(voice.Source, AL_POSITION, 0, 0, -1);
        alSourcef(voice.Source, AL_ROLLOFF_FACTOR, 0);
        alSourcef(voice.Source, AL_MAX_GAIN, 1);
        alSourcef(voice.Source, AL_GAIN, settings.Volume / 4);
        alSourcef(voice.Source, AL_PITCH, settings.Pitch);
        bool ok = alGetError() == AL_NO_ERROR;
        if (voice.Stream) voice.Scratch.resize(ChunkFrames * f.BlockAlign);
        else {
            ok = ok && Upload(voice, voice.Buffers[0], voice.Clip->GetPcmBytes());
            alSourcei(voice.Source, AL_BUFFER, voice.Buffers[0]);
            alSourcei(voice.Source, AL_LOOPING, settings.Loop);
            voice.Samples.clear(); voice.Samples.shrink_to_fit();
        }
        if (!ok || alGetError() != AL_NO_ERROR) {
            alDeleteSources(1, &voice.Source);
            alDeleteBuffers(BufferCount, voice.Buffers.data());
            error = "OpenAL could not allocate/upload the audio voice"; return 0;
        }
        auto handle = m_NextVoice++;
        if (!handle) handle = m_NextVoice++;
        m_Voices.emplace(handle, std::move(voice));
        return handle;
    }
    bool m_Loopback = false, m_Disconnect = false;
    ALCdevice* m_Device = nullptr;
    ALCcontext* m_Context = nullptr;
    LPALCRENDERSAMPLESSOFT m_Render = nullptr;
    AudioVoiceHandle m_NextVoice = 1;
    mutable std::unordered_map<AudioVoiceHandle, Voice> m_Voices;
};
}
std::unique_ptr<IAudioDevice> CreateOpenALAudioDevice(bool loopback) {
    return std::make_unique<OpenALAudioDevice>(loopback);
}
bool RenderOpenALLoopback(IAudioDevice& device, std::span<float> samples) {
    auto* openal = dynamic_cast<OpenALAudioDevice*>(&device);
    return openal && openal->Render(samples);
}
}
#endif
