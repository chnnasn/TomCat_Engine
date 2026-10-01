#include "tcpch.h"
#include "MobileTextureCodec.h"
#include "../../../vendor/astcenc/Source/astcenc.h"
#include "../../../vendor/etc2comp/EtcLib/Etc/EtcImage.h"
#pragma push_macro("OPAQUE")
#pragma push_macro("TRANSPARENT")
#undef OPAQUE
#undef TRANSPARENT
#include "../../../vendor/etc2comp/EtcLib/EtcCodec/EtcBlock4x4.h"
#pragma pop_macro("TRANSPARENT")
#pragma pop_macro("OPAQUE")
#include <memory>
namespace TomCat {
namespace {
using AstcContext = std::unique_ptr<astcenc_context, decltype(&astcenc_context_free)>;
bool Astc(bool decode, std::span<const uint8_t> input, uint32_t w, uint32_t h, bool srgb, std::vector<uint8_t>& output, std::string& error) {
    astcenc_config config{};
    auto status = astcenc_config_init(srgb ? ASTCENC_PRF_LDR_SRGB : ASTCENC_PRF_LDR,4,4,1,ASTCENC_PRE_MEDIUM,decode ? ASTCENC_FLG_DECOMPRESS_ONLY : 0,&config);
    astcenc_context* raw = nullptr;
    if(status == ASTCENC_SUCCESS) status = astcenc_context_alloc(&config,1,&raw);
    AstcContext context(raw,astcenc_context_free);
    if(status != ASTCENC_SUCCESS) { error=astcenc_get_error_string(status);return false; }
    output.resize(decode ? size_t(w)*h*4 : size_t((w+3)/4)*((h+3)/4)*16);
    void* slice = decode ? output.data() : const_cast<uint8_t*>(input.data());
    astcenc_image image{w,h,1,ASTCENC_TYPE_U8,&slice};
    const astcenc_swizzle swizzle{ASTCENC_SWZ_R,ASTCENC_SWZ_G,ASTCENC_SWZ_B,ASTCENC_SWZ_A};
    status = decode ? astcenc_decompress_image(context.get(),input.data(),input.size(),&image,&swizzle,0)
                    : astcenc_compress_image(context.get(),&image,&swizzle,output.data(),output.size(),0);
    if(status != ASTCENC_SUCCESS) { output.clear();error=astcenc_get_error_string(status);return false; }
    return true;
}
}
bool EncodeMobileTexture(std::span<const uint8_t> rgba,uint32_t w,uint32_t h,TextureArtifactFormat format,bool srgb,std::vector<uint8_t>& output,std::string& error) {
    if(!w || !h || size_t(w)*h*4 != rgba.size()) {error="invalid mobile texture dimensions";return false;}
    if(format==TextureArtifactFormat::ASTC4x4) return Astc(false,rgba,w,h,srgb,output,error);
    if(format!=TextureArtifactFormat::ETC2RGBA8) {error="unknown mobile texture format";return false;}
    std::vector<float> pixels(rgba.size());
    std::transform(rgba.begin(),rgba.end(),pixels.begin(),[](uint8_t value){return value/255.f;});
    Etc::Image image(pixels.data(),w,h,Etc::ErrorMetric::RGBA);
    const auto status=image.Encode(srgb ? Etc::Image::Format::SRGBA8 : Etc::Image::Format::RGBA8,Etc::ErrorMetric::RGBA,40.f,1,1);
    std::unique_ptr<unsigned char[]> encoded(image.GetEncodingBits());
    if(status >= Etc::Image::ERROR_THRESHOLD) {error="ETC2 encoding failed";return false;}
    output.assign(image.GetEncodingBits(),image.GetEncodingBits()+image.GetEncodingBitsBytes());
    return output.size()==size_t((w+3)/4)*((h+3)/4)*16;
}
bool DecodeMobileTexture(const TextureArtifactMip& mip,TextureArtifactFormat format,std::vector<uint8_t>& output,std::string& error) {
    if(!mip.Width || !mip.Height || mip.Width>16384 || mip.Height>16384 || mip.Bytes.size()!=size_t((mip.Width+3)/4)*((mip.Height+3)/4)*16) {error="invalid mobile texture payload";return false;}
    if(format==TextureArtifactFormat::ASTC4x4) return Astc(true,mip.Bytes,mip.Width,mip.Height,false,output,error);
    if(format!=TextureArtifactFormat::ETC2RGBA8) {error="unknown mobile texture format";return false;}
    // Decode independently bounded blocks; never allocate an image-wide encoder
    // search structure when the runtime only needs RGBA pixels.
    std::array<float,64> dummy{};Etc::Image source(dummy.data(),4,4,Etc::ErrorMetric::RGBA);
    output.resize(size_t(mip.Width)*mip.Height*4);
    for(uint32_t by=0;by<(mip.Height+3)/4;++by) for(uint32_t bx=0;bx<(mip.Width+3)/4;++bx) {
        std::array<unsigned char,16> bytes{};
        std::copy_n(mip.Bytes.data()+(size_t(by)*((mip.Width+3)/4)+bx)*16,16,bytes.data());
        Etc::Block4x4 block;block.InitFromEtcEncodingBits(Etc::Image::Format::RGBA8,0,0,bytes.data(),&source,Etc::ErrorMetric::RGBA);
        auto* colors=block.GetDecodedColors();auto* alpha=block.GetDecodedAlphas();
        for(unsigned y=0;y<4;++y) for(unsigned x=0;x<4;++x) {
            auto px=bx*4+x,py=by*4+y;if(px>=mip.Width || py>=mip.Height) continue;
            unsigned i=x*4+y; const float values[]={colors[i].fR,colors[i].fG,colors[i].fB,alpha[i]};
            for(unsigned c=0;c<4;++c) output[(size_t(py)*mip.Width+px)*4+c]=static_cast<uint8_t>(std::clamp(values[c],0.f,1.f)*255.f+.5f);
        }
    }
    return true;
}
}
