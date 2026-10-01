#pragma once
#include "TextureArtifact.h"
namespace TomCat {
bool EncodeMobileTexture(std::span<const uint8_t> rgba, uint32_t width, uint32_t height, TextureArtifactFormat format, bool srgb, std::vector<uint8_t>& output, std::string& error);
bool DecodeMobileTexture(const TextureArtifactMip& mip, TextureArtifactFormat format, std::vector<uint8_t>& output, std::string& error);
}
