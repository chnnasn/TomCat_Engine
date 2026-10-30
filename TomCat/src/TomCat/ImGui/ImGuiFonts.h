#pragma once

struct ImFont;
struct ImFontAtlas;

namespace TomCat {
    // CPU-only font setup shared by Hub/Editor and the glyph regression.
    // Replaces the atlas with bold at index 0 and regular at index 1, merging
    // CJK/symbol glyphs into both. Returns the regular default font.
    ImFont* BuildImGuiFonts(ImFontAtlas& fonts, float fontSize, bool editorStyling);
}
