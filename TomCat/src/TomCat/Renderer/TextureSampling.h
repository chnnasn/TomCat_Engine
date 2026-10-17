#pragma once

namespace TomCat {

    enum class TextureFilterMode { Linear, Nearest };
    enum class TextureWrapMode { Repeat, Clamp };

    // Keep legacy texture behavior unless the asset explicitly opts in.
    struct TextureSampling {
        TextureFilterMode Filter = TextureFilterMode::Linear;
        TextureWrapMode WrapU = TextureWrapMode::Repeat;
        TextureWrapMode WrapV = TextureWrapMode::Repeat;
    };

}
