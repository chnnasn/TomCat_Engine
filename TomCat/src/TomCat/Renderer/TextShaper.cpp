#include "tcpch.h"
#include "TextShaper.h"
#include "TomCat/Runtime/UnicodeText.h"
#include "../../../vendor/harfbuzz/src/hb.h"
#include "../../../vendor/harfbuzz/src/hb-ot.h"
namespace TomCat {
void TextShaper::AddGlyphClosure(const FontAtlasData& atlas,std::set<uint32_t>& codepoints) {
    for(uint32_t index=0;index<atlas.ShapingSources.size();++index) {
        const auto& source=atlas.ShapingSources[index];
        auto* blob=hb_blob_create(reinterpret_cast<const char*>(source.data()),static_cast<unsigned>(source.size()),HB_MEMORY_MODE_READONLY,nullptr,nullptr);
        auto* face=hb_face_create(blob,0);auto* font=hb_font_create(face);hb_ot_font_set_funcs(font);
        auto* glyphs=hb_set_create();
        for(auto cp:codepoints) {
            hb_codepoint_t glyph=0;
            if(cp<GlyphKeyBase && hb_font_get_nominal_glyph(font,cp,&glyph))hb_set_add(glyphs,glyph);
            else if(cp>=GlyphKeyBase && (cp-GlyphKeyBase)/65536==index)hb_set_add(glyphs,(cp-GlyphKeyBase)%65536);
        }
        hb_ot_layout_lookups_substitute_closure(face,nullptr,glyphs);
        hb_codepoint_t glyph=HB_SET_VALUE_INVALID;
        while(hb_set_next(glyphs,&glyph))if(glyph<=65535)codepoints.insert(GlyphKeyBase+index*65536+glyph);
        hb_set_destroy(glyphs);hb_font_destroy(font);hb_face_destroy(face);hb_blob_destroy(blob);
    }
}
std::vector<ShapedGlyph> TextShaper::Shape(const FontAtlasData& atlas,std::string_view text,std::set<uint32_t>* closure) {
    std::vector<ShapedGlyph> output;
    if(text.empty() || text.size()>65536 || atlas.ShapingSources.empty()) return output;
    struct Font { hb_font_t* Value; ~Font(){hb_font_destroy(Value);} };
    std::vector<std::unique_ptr<Font>> fonts;
    for(const auto& source:atlas.ShapingSources) {
        auto* blob=hb_blob_create(reinterpret_cast<const char*>(source.data()),static_cast<unsigned>(source.size()),HB_MEMORY_MODE_READONLY,nullptr,nullptr);
        auto* face=hb_face_create(blob,0);auto* font=hb_font_create(face);hb_ot_font_set_funcs(font);
        hb_face_destroy(face);hb_blob_destroy(blob);fonts.emplace_back(new Font{font});
    }
    const auto edges=UnicodeText::GraphemeBoundaries(text);
    const auto visual=UnicodeText::VisualClusters(text);
    std::map<size_t,std::pair<size_t,bool>> ordering;
    for(size_t i=0;i<visual.size();++i) ordering[visual[i].Begin]={i,visual[i].RTL};
    struct Run {size_t Begin,End,Order;uint32_t Font;hb_script_t Script;bool RTL;};
    std::vector<Run> runs;
    hb_script_t previous=HB_SCRIPT_LATIN;
    for(size_t i=1;i<edges.size();++i) {
        const auto codepoints=FontAtlasBuilder::DecodeUTF8(text.substr(edges[i-1],edges[i]-edges[i-1]));
        uint32_t selected=0;
        for(uint32_t f=0;f<fonts.size();++f) {
            bool supports=true;
            for(auto cp:codepoints) {hb_codepoint_t glyph=0;if(cp==0x200d || cp==0x200c || (cp>=0xfe00 && cp<=0xfe0f) || cp==10 || cp==13) continue;if(!hb_font_get_nominal_glyph(fonts[f]->Value,cp,&glyph)) {supports=false;break;}}
            if(supports) {selected=f;break;}
        }
        hb_script_t script=previous;
        for(auto cp:codepoints) {auto value=hb_unicode_script(hb_unicode_funcs_get_default(),cp);if(value!=HB_SCRIPT_COMMON && value!=HB_SCRIPT_INHERITED && value!=HB_SCRIPT_UNKNOWN) {script=value;break;}}
        previous=script;
        const auto [order,rtl]=ordering.contains(edges[i-1]) ? ordering.at(edges[i-1]) : std::pair<size_t,bool>{i,false};
        if(!runs.empty() && runs.back().Font==selected && runs.back().Script==script && runs.back().RTL==rtl) {runs.back().End=edges[i];runs.back().Order=std::min(order,runs.back().Order);}
        else runs.push_back({edges[i-1],edges[i],order,selected,script,rtl});
    }
    std::stable_sort(runs.begin(),runs.end(),[](const auto& a,const auto& b){return a.Order<b.Order;});
    for(const auto& run:runs) {
        auto* buffer=hb_buffer_create();
        hb_buffer_set_direction(buffer,run.RTL ? HB_DIRECTION_RTL : HB_DIRECTION_LTR);
        hb_buffer_set_script(buffer,run.Script);hb_buffer_set_language(buffer,hb_language_from_string("und",-1));
        hb_buffer_set_cluster_level(buffer,HB_BUFFER_CLUSTER_LEVEL_MONOTONE_GRAPHEMES);
        hb_buffer_add_utf8(buffer,text.data(),static_cast<int>(text.size()),static_cast<unsigned>(run.Begin),static_cast<int>(run.End-run.Begin));
        auto* font=fonts[run.Font]->Value;
        if(closure) {
            auto* glyphs=hb_set_create();hb_ot_shape_glyphs_closure(font,buffer,nullptr,0,glyphs);
            hb_codepoint_t glyph=HB_SET_VALUE_INVALID;
            while(hb_set_next(glyphs,&glyph)) if(glyph<=65535) closure->insert(GlyphKeyBase+run.Font*65536+glyph);
            hb_set_destroy(glyphs);
        }
        hb_shape(font,buffer,nullptr,0);
        unsigned count=0;auto* info=hb_buffer_get_glyph_infos(buffer,&count);auto* positions=hb_buffer_get_glyph_positions(buffer,nullptr);
        std::set<size_t> clusterEnds{run.End};for(unsigned i=0;i<count;++i) clusterEnds.insert(info[i].cluster);
        const float scale=run.Font<atlas.ShapingScales.size() ? atlas.ShapingScales[run.Font] : 1.f;
        for(unsigned i=0;i<count;++i) {
            const size_t start=info[i].cluster;const auto next=clusterEnds.upper_bound(start);
            if(info[i].codepoint==0) {
                const auto* fallback=atlas.Find(FontAtlasBuilder::ReplacementCodepoint);
                output.push_back({FontAtlasBuilder::ReplacementCodepoint,start,next==clusterEnds.end()?run.End:*next,fallback ? fallback->Advance : 0,0,0,run.RTL});
            } else output.push_back({GlyphKeyBase+run.Font*65536+info[i].codepoint,start,next==clusterEnds.end()?run.End:*next,
                positions[i].x_advance*scale,positions[i].x_offset*scale,positions[i].y_offset*scale,run.RTL});
        }
        hb_buffer_destroy(buffer);
    }
    return output;
}
}
