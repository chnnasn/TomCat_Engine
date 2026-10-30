#pragma once

#include <cstdint>
#include <filesystem>
#include <span>
#include <vector>

namespace TomCat {
    struct WindowIconImage {
        int Width = 0;
        int Height = 0;
        std::vector<uint8_t> Pixels; // RGBA, ready for GLFW
    };

    // ICO containers with PNG or uncompressed 32-bit DIB frames, plus the
    // ordinary image formats supported by stb_image. Invalid frames are skipped.
    std::vector<WindowIconImage> DecodeWindowIcon(std::span<const uint8_t> encoded);
    std::vector<WindowIconImage> LoadWindowIcon(const std::filesystem::path& path);
}
