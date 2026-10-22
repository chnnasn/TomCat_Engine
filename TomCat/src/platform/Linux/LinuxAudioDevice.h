#pragma once
#include "TomCat/Audio/AudioDevice.h"
#include <span>

namespace TomCat {
// Loopback uses the same OpenAL mixer and voice implementation without an endpoint.
std::unique_ptr<IAudioDevice> CreateOpenALAudioDevice(bool loopback = false);
bool RenderOpenALLoopback(IAudioDevice& device, std::span<float> stereoFrames);
}
