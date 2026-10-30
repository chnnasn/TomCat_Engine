#include "tcpch.h"
#include "WindowIcon.h"

#include <stb_image.h>
#include <fstream>

namespace TomCat {
namespace {
    constexpr size_t MaximumIconFileSize = 32 * 1024 * 1024;

    uint16_t Read16(std::span<const uint8_t> bytes, size_t offset) {
        return uint16_t(bytes[offset]) | (uint16_t(bytes[offset + 1]) << 8);
    }

    uint32_t Read32(std::span<const uint8_t> bytes, size_t offset) {
        return uint32_t(bytes[offset]) | (uint32_t(bytes[offset + 1]) << 8)
            | (uint32_t(bytes[offset + 2]) << 16) | (uint32_t(bytes[offset + 3]) << 24);
    }

    WindowIconImage DecodeRaster(std::span<const uint8_t> bytes, int expectedWidth = 0, int expectedHeight = 0) {
        WindowIconImage image;
        int width = 0, height = 0, channels = 0;
        if (bytes.empty() || bytes.size() > MaximumIconFileSize
            || !stbi_info_from_memory(bytes.data(), static_cast<int>(bytes.size()), &width, &height, &channels)
            || width <= 0 || height <= 0 || width > 4096 || height > 4096
            || (expectedWidth && (width != expectedWidth || height != expectedHeight)))
            return image;

        std::unique_ptr<stbi_uc, decltype(&stbi_image_free)> pixels(
            stbi_load_from_memory(bytes.data(), static_cast<int>(bytes.size()), &width, &height, &channels, 4),
            stbi_image_free);
        if (pixels) {
            image.Width = width;
            image.Height = height;
            image.Pixels.assign(pixels.get(), pixels.get() + size_t(width) * height * 4);
        }
        return image;
    }

    WindowIconImage DecodeDIB(std::span<const uint8_t> bytes, int width, int height) {
        // ICO DIB height includes the XOR image and the 1-bit AND mask. Unlike
        // a BMP file there is no BITMAPFILEHEADER. Only BI_RGB BGRA is used by
        // our legacy Editor icon; other DIB encodings are deliberately skipped.
        // https://devblogs.microsoft.com/oldnewthing/20101021-00/?p=12483
        if (bytes.size() < 40 || Read32(bytes, 0) != 40
            || Read32(bytes, 4) != uint32_t(width) || Read32(bytes, 8) != uint32_t(height * 2)
            || Read16(bytes, 12) != 1 || Read16(bytes, 14) != 32
            || Read32(bytes, 16) != 0 || Read32(bytes, 32) != 0)
            return {};

        const size_t pixelBytes = size_t(width) * height * 4;
        const size_t maskStride = ((size_t(width) + 31) / 32) * 4;
        if (bytes.size() - 40 < pixelBytes)
            return {};
        const auto pixels = bytes.subspan(40, pixelBytes);
        const auto mask = bytes.subspan(40 + pixelBytes);
        bool hasAlpha = false;
        for (size_t i = 3; i < pixelBytes; i += 4)
            hasAlpha |= pixels[i] != 0;
        if (!hasAlpha && mask.size() < maskStride * height)
            return {};

        WindowIconImage image{width, height, std::vector<uint8_t>(pixelBytes)};
        for (int y = 0; y < height; ++y) {
            const size_t sourceRow = size_t(height - 1 - y); // DIB is bottom-up.
            for (int x = 0; x < width; ++x) {
                const size_t src = (sourceRow * width + x) * 4;
                const size_t dst = (size_t(y) * width + x) * 4;
                image.Pixels[dst] = pixels[src + 2];
                image.Pixels[dst + 1] = pixels[src + 1];
                image.Pixels[dst + 2] = pixels[src];
                image.Pixels[dst + 3] = hasAlpha ? pixels[src + 3]
                    : (mask[sourceRow * maskStride + x / 8] & (0x80 >> (x % 8)) ? 0 : 255);
            }
        }
        return image;
    }
}

std::vector<WindowIconImage> DecodeWindowIcon(std::span<const uint8_t> encoded) {
    if (encoded.empty() || encoded.size() > MaximumIconFileSize)
        return {};
    std::vector<WindowIconImage> images;
    if (encoded.size() < 4 || Read16(encoded, 0) != 0 || Read16(encoded, 2) != 1) {
        auto image = DecodeRaster(encoded);
        if (!image.Pixels.empty()) images.push_back(std::move(image));
        return images;
    }

    if (encoded.size() < 6)
        return {};
    const auto count = Read16(encoded, 4);
    const size_t directorySize = 6 + size_t(count) * 16;
    if (count == 0 || count > 256 || directorySize > encoded.size())
        return {};
    for (size_t i = 0; i < count; ++i) {
        const size_t entry = 6 + i * 16;
        const int width = encoded[entry] ? encoded[entry] : 256;
        const int height = encoded[entry + 1] ? encoded[entry + 1] : 256;
        const size_t length = Read32(encoded, entry + 8);
        const size_t offset = Read32(encoded, entry + 12);
        if (encoded[entry + 3] != 0 || offset < directorySize || offset > encoded.size()
            || length == 0 || length > encoded.size() - offset)
            continue;
        const auto frame = encoded.subspan(offset, length);
        static constexpr uint8_t pngSignature[] = {137, 80, 78, 71, 13, 10, 26, 10};
        const bool png = frame.size() >= sizeof(pngSignature)
            && std::equal(std::begin(pngSignature), std::end(pngSignature), frame.begin());
        auto image = png ? DecodeRaster(frame, width, height) : DecodeDIB(frame, width, height);
        if (!image.Pixels.empty()) images.push_back(std::move(image));
    }
    return images;
}

std::vector<WindowIconImage> LoadWindowIcon(const std::filesystem::path& path) {
    std::ifstream input(path, std::ios::binary | std::ios::ate);
    const auto size = input ? static_cast<std::streamoff>(input.tellg()) : -1;
    if (size <= 0 || size > static_cast<std::streamoff>(MaximumIconFileSize))
        return {};
    std::vector<uint8_t> encoded(static_cast<size_t>(size));
    input.seekg(0, std::ios::beg);
    if (!input.read(reinterpret_cast<char*>(encoded.data()), static_cast<std::streamsize>(size)))
        return {};
    return DecodeWindowIcon(encoded);
}
}
