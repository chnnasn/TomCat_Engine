#include "tcpch.h"
#include "AudioDevice.h"

#if defined(TC_PLATFORM_WEB)
#include <emscripten.h>
#include <cmath>
#include <limits>

namespace TomCat {

    namespace {

        // JavaScript receives only 32-bit backend handles, never a truncated
        // public uint64 voice handle or a BigInt in an EM_ASM argument.
        bool ValidHandle(AudioVoiceHandle handle)
        {
            return handle != 0 && handle <= static_cast<uint64_t>(std::numeric_limits<int32_t>::max());
        }

        class WebAudioDevice final : public IAudioDevice
        {
        public:
            ~WebAudioDevice() override { Shutdown(); }

            bool Initialize(std::string& error) override
            {
                error.clear();
                if (IsOperational()) return true;
                Shutdown();
                m_Device = EM_ASM_INT({ return Module['tomcatAudio'] ? Module['tomcatAudio'].initialize() : 0; });
                if (!m_Device) error = "WebAudio could not create an AudioContext";
                return m_Device != 0;
            }

            void Shutdown() override
            {
                if (m_Device) EM_ASM({ Module['tomcatAudio'].shutdown($0); }, m_Device);
                m_Device = 0;
            }

            bool IsHardwareAvailable() const override { return IsOperational(); }
            bool IsOperational() const override
            {
                return m_Device && EM_ASM_INT({ return Module['tomcatAudio'].operational($0); }, m_Device);
            }
            const char* GetBackendName() const override { return "WebAudio"; }

            AudioVoiceHandle CreateVoice(Ref<AudioClip> clip,
                const AudioVoiceSettings& settings, std::string& error) override
            {
                error.clear();
                if (!IsOperational() || !clip || clip->GetPcmBytes().empty()
                    || !std::isfinite(settings.Volume) || settings.Volume < 0 || settings.Volume > 4
                    || !std::isfinite(settings.Pitch) || settings.Pitch < MinimumAudioPitch || settings.Pitch > MaximumAudioPitch)
                {
                    error = "WebAudio voice requires an initialized device, nonempty clip and valid settings";
                    return 0;
                }
                const auto& pcm = clip->GetPcmBytes();
                const int handle = EM_ASM_INT({
                    return Module['tomcatAudio'].create($0, HEAPU8, $1 >>> 0, $2 >>> 0, $3, $4, $5, $6, $7, $8, $9);
                }, m_Device, pcm.data(), pcm.size(), clip->GetChannels(), clip->GetSampleRate(),
                    clip->GetBitsPerSample(), clip->IsFloatingPoint(), settings.Loop, settings.Volume, settings.Pitch);
                if (!handle) error = "WebAudio could not create a PCM voice (format, sample rate or allocation unsupported)";
                return static_cast<AudioVoiceHandle>(handle);
            }

            AudioVoiceHandle CreateStreamingVoice(std::unique_ptr<IAudioStream>,
                const AudioVoiceSettings&, std::string& error) override
            {
                // Never turn a bounded stream into an unbounded whole-file read.
                error = "WebAudio streaming is not supported; use a non-streaming AudioSource for resident clips";
                return 0;
            }

            bool DestroyVoice(AudioVoiceHandle voice) override
            { return ValidHandle(voice) && EM_ASM_INT({ return Module['tomcatAudio'].destroy($0, $1); }, m_Device, static_cast<uint32_t>(voice)); }
            bool Play(AudioVoiceHandle voice) override
            { return ValidHandle(voice) && EM_ASM_INT({ return Module['tomcatAudio'].play($0, $1); }, m_Device, static_cast<uint32_t>(voice)); }
            bool Pause(AudioVoiceHandle voice) override
            { return ValidHandle(voice) && EM_ASM_INT({ return Module['tomcatAudio'].pause($0, $1); }, m_Device, static_cast<uint32_t>(voice)); }
            bool Stop(AudioVoiceHandle voice) override
            { return ValidHandle(voice) && EM_ASM_INT({ return Module['tomcatAudio'].stop($0, $1); }, m_Device, static_cast<uint32_t>(voice)); }
            bool SetLoop(AudioVoiceHandle voice, bool loop) override
            { return ValidHandle(voice) && EM_ASM_INT({ return Module['tomcatAudio'].loop($0, $1, $2); }, m_Device, static_cast<uint32_t>(voice), loop); }
            bool SetVolume(AudioVoiceHandle voice, float volume) override
            { return ValidHandle(voice) && EM_ASM_INT({ return Module['tomcatAudio'].volume($0, $1, $2); }, m_Device, static_cast<uint32_t>(voice), volume); }
            bool SetPitch(AudioVoiceHandle voice, float pitch) override
            { return ValidHandle(voice) && EM_ASM_INT({ return Module['tomcatAudio'].pitch($0, $1, $2); }, m_Device, static_cast<uint32_t>(voice), pitch); }
            bool SetSpatial(AudioVoiceHandle voice, const AudioSpatialSettings& settings) override
            { return ValidHandle(voice) && EM_ASM_INT({ return Module['tomcatAudio'].spatial($0, $1, $2, $3); }, m_Device, static_cast<uint32_t>(voice), settings.Pan, settings.DistanceGain); }
            AudioPlaybackState GetState(AudioVoiceHandle voice) const override
            {
                return ValidHandle(voice) ? static_cast<AudioPlaybackState>(EM_ASM_INT({
                    return Module['tomcatAudio'].state($0, $1);
                }, m_Device, static_cast<uint32_t>(voice))) : AudioPlaybackState::Stopped;
            }
            double GetPlaybackSeconds(AudioVoiceHandle voice) const override
            {
                return ValidHandle(voice) ? EM_ASM_DOUBLE({
                    return Module['tomcatAudio'].position($0, $1);
                }, m_Device, static_cast<uint32_t>(voice)) : 0;
            }
            // AudioContext's clock owns playback, including browser suspension.
            void Update(double) override {}

        private:
            int m_Device = 0;
        };
    }

    std::unique_ptr<IAudioDevice> CreateWebAudioDevice()
    {
        return std::make_unique<WebAudioDevice>();
    }
}
#endif
