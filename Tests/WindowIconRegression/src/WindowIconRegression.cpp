#include "platform/Window/WindowIcon.h"
#include <algorithm>
#include <array>
#include <fstream>
#include <iostream>
#include <iterator>
#include <memory>
#include <stdexcept>

#ifdef TC_PLATFORM_LINUX
#include "TomCat/Core/Log.h"
#include "TomCat/Core/Window.h"
#define GLFW_EXPOSE_NATIVE_X11
#include <GLFW/glfw3.h>
#include <GLFW/glfw3native.h>
#include <X11/Xatom.h>
#endif

namespace {
void Require(bool value, const char* message) {
    if (!value) throw std::runtime_error(message);
}
std::vector<uint8_t> Read(const std::filesystem::path& path) {
    std::ifstream input(path, std::ios::binary);
    Require(input.good(), "icon fixture is missing from the application Packages");
    return {std::istreambuf_iterator<char>(input), {}};
}
void Put32(std::vector<uint8_t>& bytes, size_t offset, uint32_t value) {
    for (size_t i = 0; i < 4; ++i) bytes[offset + i] = static_cast<uint8_t>(value >> (i * 8));
}
uint64_t PixelHash(const TomCat::WindowIconImage& image) {
    uint64_t hash = 14695981039346656037ull;
    for (uint8_t value : image.Pixels) hash = (hash ^ value) * 1099511628211ull;
    return hash;
}
void CheckDIBAndMalformedData(const std::vector<uint8_t>& hub) {
    // 2x2 BGRA DIB in an ICO container: bottom row first, followed by a
    // DWORD-aligned monochrome AND mask. Distinct colors expose channel/row bugs.
    std::vector<uint8_t> dib(86, 0);
    dib[2] = 1; dib[4] = 1;
    dib[6] = 2; dib[7] = 2; dib[10] = 1; dib[12] = 32;
    Put32(dib, 14, 64); Put32(dib, 18, 22);
    Put32(dib, 22, 40); Put32(dib, 26, 2); Put32(dib, 30, 4);
    dib[34] = 1; dib[36] = 32;
    const std::array<uint8_t, 16> bgra = {255,0,0,0, 255,255,255,64, 0,0,255,255, 0,255,0,128};
    std::copy(bgra.begin(), bgra.end(), dib.begin() + 62);
    const std::vector<uint8_t> rgba = {255,0,0,255, 0,255,0,128, 0,0,255,0, 255,255,255,64};
    auto decoded = TomCat::DecodeWindowIcon(dib);
    Require(decoded.size() == 1 && decoded[0].Pixels == rgba, "DIB orientation, RGBA conversion or alpha is incorrect");

    for (size_t i = 65; i < 78; i += 4) dib[i] = 0;
    dib[78] = 0x80; // Bottom-left transparent.
    dib[82] = 0x40; // Top-right transparent.
    decoded = TomCat::DecodeWindowIcon(dib);
    auto masked = rgba;
    masked[3] = 255; masked[7] = 0; masked[11] = 0; masked[15] = 255;
    Require(decoded.size() == 1 && decoded[0].Pixels == masked, "zero-alpha DIB did not use the AND mask");
    auto invalidDIB = dib;
    Put32(invalidDIB, 26, 0x7fffffff);
    Require(TomCat::DecodeWindowIcon(invalidDIB).empty(), "DIB dimension mismatch was accepted");
    invalidDIB = dib;
    invalidDIB.resize(78); Put32(invalidDIB, 14, 56);
    Require(TomCat::DecodeWindowIcon(invalidDIB).empty(), "zero-alpha DIB with no mask was accepted");

    for (size_t length = 0; length < 22; ++length)
        Require(TomCat::DecodeWindowIcon(std::span(hub).first(length)).empty(), "truncated ICO directory was accepted");
    auto invalid = hub;
    invalid[4] = 255; invalid[5] = 255;
    Require(TomCat::DecodeWindowIcon(invalid).empty(), "invalid ICO frame count was accepted");
    invalid = hub;
    Put32(invalid, 18, 0xfffffff0); // Overflowing first frame offset.
    Require(TomCat::DecodeWindowIcon(invalid).size() == 8, "bad ICO offset hid valid remaining frames");
    invalid = hub;
    Put32(invalid, 14, 0xffffffff); // Overflowing first frame length.
    Require(TomCat::DecodeWindowIcon(invalid).size() == 8, "bad ICO size hid valid remaining frames");
    invalid = hub; invalid[6] = 17;
    Require(TomCat::DecodeWindowIcon(invalid).size() == 8, "PNG dimensions did not match the ICO entry");
    Require(TomCat::DecodeWindowIcon(std::span(hub).first(hub.size() - 1)).size() == 8,
        "truncated final ICO frame hid valid smaller frames");
}

#ifdef TC_PLATFORM_LINUX
void CheckNativeWindow(const std::filesystem::path& path, const std::vector<TomCat::WindowIconImage>& expected) {
    TomCat::WindowProps props("Linux window icon regression", 160, 120, path);
    props.Visible = false; props.VSync = false;
    auto window = TomCat::Window::Create(props); // Exercise the application's actual icon loading path.
    Display* display = glfwGetX11Display();
    const auto native = glfwGetX11Window(static_cast<GLFWwindow*>(window->GetNativeWindow()));
    const Atom property = XInternAtom(display, "_NET_WM_ICON", True);
    Require(property != None, "window icon property was not created");
    size_t expectedCount = 0;
    for (const auto& image : expected) expectedCount += 2 + size_t(image.Width) * image.Height;
    Atom type = None;
    int format = 0;
    unsigned long count = 0, remaining = 0;
    unsigned char* data = nullptr;
    const int result = XGetWindowProperty(display, native, property, 0, static_cast<long>(expectedCount),
        False, XA_CARDINAL, &type, &format, &count, &remaining, &data);
    std::unique_ptr<unsigned char, decltype(&XFree)> storage(data, XFree);
    Require(result == Success && type == XA_CARDINAL && format == 32 && count == expectedCount
        && remaining == 0 && data, "GLFW did not publish all ICO frames to the Linux window");
    const auto* values = reinterpret_cast<const unsigned long*>(data);
    size_t cursor = 0;
    for (const auto& image : expected) {
        Require(values[cursor++] == static_cast<unsigned long>(image.Width)
            && values[cursor++] == static_cast<unsigned long>(image.Height), "native icon dimensions differ");
        for (size_t i = 0; i < image.Pixels.size(); i += 4) {
            const auto& p = image.Pixels;
            const uint32_t argb = (uint32_t(p[i + 3]) << 24) | (uint32_t(p[i]) << 16)
                | (uint32_t(p[i + 1]) << 8) | p[i + 2];
            Require(static_cast<uint32_t>(values[cursor++]) == argb, "native icon colors or transparency differ");
        }
    }
}
#endif
}

int main(int argc, char** argv) {
    try {
        Require(argc == 3, "expected the packaged Hub and Editor ICO paths");
        const auto hubBytes = Read(argv[1]);
        const auto hub = TomCat::LoadWindowIcon(argv[1]);
        const auto editor = TomCat::LoadWindowIcon(argv[2]);
        const std::array<int, 9> sizes = {16,20,24,32,40,48,64,128,256};
        Require(hub.size() == sizes.size(), "Hub ICO PNG frames failed to decode");
        for (size_t i = 0; i < sizes.size(); ++i)
            Require(hub[i].Width == sizes[i] && hub[i].Height == sizes[i]
                && hub[i].Pixels.size() == size_t(sizes[i]) * sizes[i] * 4, "Hub icon dimensions differ");
        Require(editor.size() == 1 && editor[0].Width == 64 && editor[0].Height == 64, "Editor ICO DIB failed to decode");
        // Independently decoded from the checked-in assets with Pillow.
        Require(PixelHash(hub.back()) == 0xf5b21434c7ba6fedull, "Hub icon pixel/alpha checksum differs");
        Require(PixelHash(editor[0]) == 0x02d68d73c9cf9d20ull, "Editor icon pixel/alpha checksum differs");
        auto png = std::filesystem::path(argv[1]); png.replace_extension(".png");
        const auto ordinary = TomCat::LoadWindowIcon(png);
        Require(ordinary.size() == 1 && ordinary[0].Width == 1254 && ordinary[0].Height == 1254,
            "ordinary PNG icon loading regressed");
        CheckDIBAndMalformedData(hubBytes);
#ifdef TC_PLATFORM_LINUX
        TomCat::Log::Init();
        CheckNativeWindow(argv[1], hub);
        CheckNativeWindow(argv[2], editor);
        std::cout << "PASS native X11 window icon pixels\n";
#endif
        std::cout << "PASS ICO PNG/DIB pixels, alpha/mask and malformed input\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "FAIL WindowIconRegression: " << error.what() << '\n';
        return 1;
    }
}
