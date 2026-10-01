#pragma once
#include "Font.h"
namespace TomCat {
struct ShapedGlyph { uint32_t Key=0; size_t Begin=0,End=0; float Advance=0,X=0,Y=0; bool RTL=false; };
class TextShaper {
public:
    static constexpr uint32_t GlyphKeyBase=0x110000;
    static void AddGlyphClosure(const FontAtlasData& atlas,std::set<uint32_t>& codepoints);
    static std::vector<ShapedGlyph> Shape(const FontAtlasData& atlas,std::string_view text,std::set<uint32_t>* closure=nullptr);
};
}
