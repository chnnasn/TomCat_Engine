#include "TomCat/ImGui/ImGuiFonts.h"
#include "TomCat/Core/Log.h"
#include <imgui.h>
#include <imgui_internal.h>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <regex>
#include <set>
#include <stdexcept>

namespace {
void Require(bool value, const char* message) {
    if (!value) throw std::runtime_error(message);
}
}

int main(int argc, char** argv) {
    TomCat::Log::Init();
    try {
        Require(argc == 2, "expected the Hub UI source path");
        // Run from the staged application's directory, not from the source tree.
        Require(std::filesystem::is_regular_file("Packages/fonts/noto/LICENSE"), "font license was not staged");
        std::ifstream source(argv[1], std::ios::binary);
        Require(source.good(), "could not read the Hub's actual UI text");
        const std::string text{std::istreambuf_iterator<char>(source), {}};
        std::set<ImWchar> chinese;
        const char* cursor = text.data();
        const char* end = cursor + text.size();
        while (cursor < end) {
            unsigned int codepoint = 0;
            const int length = ImTextCharFromUtf8(&codepoint, cursor, end);
            Require(length > 0 && codepoint != 0xFFFD, "Hub source is not valid UTF-8");
            cursor += length;
            if (codepoint >= 0x4E00 && codepoint <= 0x9FFF)
                chinese.insert(static_cast<ImWchar>(codepoint));
        }
        // Some labels use C++ universal character names instead of UTF-8 bytes.
        const std::regex escapedUnicode(R"(\\u([0-9a-fA-F]{4}))");
        for (auto it = std::sregex_iterator(text.begin(), text.end(), escapedUnicode);
            it != std::sregex_iterator(); ++it) {
            const auto codepoint = std::stoul((*it)[1].str(), nullptr, 16);
            if (codepoint >= 0x4E00 && codepoint <= 0x9FFF)
                chinese.insert(static_cast<ImWchar>(codepoint));
        }
        Require(chinese.size() > 100, "Hub Chinese text fixture is incomplete");

        ImFontAtlas atlas;
        for (bool editorStyling : {false, true}) {
            // Reuse the same atlas to cover font replacement on a DPI change.
            for (float scale : {1.0f, 1.5f}) {
                const float size = (editorStyling ? 16.0f : 32.0f) * scale;
                ImFont* regular = TomCat::BuildImGuiFonts(atlas, size, editorStyling);
                Require(atlas.Fonts.Size == 2 && regular == atlas.Fonts[1], "bold/default font layout changed");
                Require(atlas.Build(), "font atlas build failed");
                unsigned char* pixels = nullptr;
                int width = 0, height = 0;
                atlas.GetTexDataAsAlpha8(&pixels, &width, &height);
                Require(pixels && width > 0 && height > 0, "font atlas has no rasterized pixels");
                for (ImFont* font : atlas.Fonts) {
                    Require(font->FindGlyphNoFallback('A'), "Latin font is missing");
                    for (ImWchar codepoint : chinese) {
                        const ImFontGlyph* glyph = font->FindGlyphNoFallback(codepoint);
                        if (!glyph || !glyph->Visible || glyph->U0 >= glyph->U1 || glyph->V0 >= glyph->V1) {
                            std::cerr << "Missing Chinese glyph U+" << std::hex << unsigned(codepoint) << '\n';
                            throw std::runtime_error("Hub text would use a missing-glyph placeholder");
                        }
                    }
                }
            }
        }
        std::cout << "PASS " << chinese.size() << " Hub Chinese characters in regular/bold fonts, Hub/Editor sizes and DPI rebuilds\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "FAIL ImGuiFontRegression: " << error.what() << '\n';
        return 1;
    }
}
