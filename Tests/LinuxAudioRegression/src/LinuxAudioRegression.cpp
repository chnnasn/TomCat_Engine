#include "platform/Linux/LinuxAudioDevice.h"
#include "TomCat/Core/Log.h"
#include <cmath>
#include <iostream>
#include <stdexcept>

namespace {
void Require(bool condition, const std::string& message) { if (!condition) throw std::runtime_error(message); }
std::vector<uint8_t> Wave(int channels = 1) {
    std::vector<uint8_t> bytes;
    auto word = [&](uint32_t value, int size) { for (int i = 0; i < size; ++i) bytes.push_back(value >> (i * 8)); };
    auto tag = [&](const char* value) { bytes.insert(bytes.end(), value, value + 4); };
    const int frames = 48000, dataSize = frames * channels * 2;
    tag("RIFF"); word(36 + dataSize, 4); tag("WAVE"); tag("fmt "); word(16, 4);
    word(1, 2); word(channels, 2); word(48000, 4); word(48000 * channels * 2, 4);
    word(channels * 2, 2); word(16, 2); tag("data"); word(dataSize, 4);
    for (int frame = 0; frame < frames; ++frame)
        for (int channel = 0; channel < channels; ++channel)
            word(channel == 0 ? static_cast<uint16_t>(int16_t(std::sin(frame * 0.06) * 12000)) : 0, 2);
    return bytes;
}
double Energy(const std::vector<float>& samples, int channel = -1) {
    double energy = 0;
    for (size_t i = channel < 0 ? 0 : channel; i < samples.size(); i += channel < 0 ? 1 : 2)
        energy += samples[i] * samples[i];
    return energy;
}
}
int main() {
    TomCat::Log::Init();
    try {
        std::string error;
        auto device = TomCat::CreateOpenALAudioDevice(true);
        Require(device->Initialize(error), error);
        Require(device->IsOperational() && !device->IsHardwareAvailable(), "loopback reported a hardware endpoint");
        auto wave = Wave();
        auto clip = TomCat::AudioClip::Decode(wave, error);
        auto voice = device->CreateVoice(clip, {}, error);
        Require(voice && device->Play(voice), "resident voice: " + error);
        std::vector<float> samples(9600);
        auto render = [&] { Require(TomCat::RenderOpenALLoopback(*device, samples), "loopback render failed"); };
        render(); Require(Energy(samples) > 1, "mixer produced silence");
        Require(device->GetPlaybackSeconds(voice) > 0.09, "playback clock did not advance");
        Require(device->Pause(voice), "pause failed");
        render(); render(); Require(Energy(samples) < 0.00001, "paused voice still emits audio");
        Require(device->Play(voice) && device->SetVolume(voice, 0), "resume/mute failed");
        render(); render(); Require(Energy(samples) < 0.00001, "muted voice still emits audio");
        Require(device->SetVolume(voice, 1) && device->SetPitch(voice, 2), "gain/pitch failed");
        Require(!device->SetPitch(voice, 0) && !device->SetVolume(voice, 5), "invalid settings accepted");
        Require(device->SetSpatial(voice, {-1, 1}), "pan failed");
        render(); render(); Require(Energy(samples, 0) > Energy(samples, 1) * 4, "mono pan did not route left");
        Require(device->Stop(voice) && device->GetPlaybackSeconds(voice) == 0, "stop did not rewind");
        Require(device->Play(voice), "restart failed");
        for (int i = 0; i < 6; ++i) render();
        Require(device->GetState(voice) == TomCat::AudioPlaybackState::Stopped, "resident EOF not stopped");
        Require(device->DestroyVoice(voice), "resident destruction failed");

        auto stereo = TomCat::AudioClip::Decode(Wave(2), error);
        voice = device->CreateVoice(stereo, {}, error);
        Require(voice && device->Play(voice), "stereo voice failed");
        render(); render(); Require(Energy(samples, 0) > 1 && Energy(samples, 1) < 0.00001, "stereo channels collapsed");
        device->DestroyVoice(voice);

        auto source = TomCat::AudioStreamSource::Open(wave, error);
        Require(source != nullptr, error);
        voice = device->CreateStreamingVoice(source->CreateReader(), {true, 1, 1}, error);
        Require(voice && device->Play(voice), "stream failed: " + error);
        for (int i = 0; i < 25; ++i) { render(); Require(Energy(samples) > 1, "stream underrun/loop emitted silence"); }
        Require(device->GetState(voice) == TomCat::AudioPlaybackState::Playing, "stream loop stopped");
        Require(device->SetLoop(voice, false), "disable stream loop failed");
        for (int i = 0; i < 12; ++i) render();
        Require(device->GetState(voice) == TomCat::AudioPlaybackState::Stopped, "stream EOF did not stop");
        Require(device->Play(voice), "stream restart failed"); render();
        Require(Energy(samples) > 1, "stream restart silent");
        device->Shutdown();
        std::cout << "PASS OpenAL resident/stereo/stream PCM, pause, gain, pitch, pan, loop, EOF and restart\n";
        return 0;
    } catch (const std::exception& e) { std::cerr << "FAIL " << e.what() << '\n'; return 1; }
}
