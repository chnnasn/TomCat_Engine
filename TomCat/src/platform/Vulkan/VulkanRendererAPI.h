#pragma once
#include "TomCat/Renderer/RendererAPI.h"
#include "VulkanShader.h"
#include <cmath>

namespace TomCat {
class VulkanRenderState final : public RenderState {
public:
    explicit VulkanRenderState(const std::shared_ptr<VulkanDevice>& device)
        : m_Device(device), m_Target(device->Target), m_Viewport(device->Viewport),
          m_Shader(device->Shader), m_Textures(device->Textures), m_Attachments(device->SampledAttachments),
          m_Uniforms(device->Uniforms), m_Depth(device->DepthTest), m_DepthWrite(device->DepthWrite),
          m_LessEqual(device->DepthLessEqual), m_Blending(device->Blending), m_CullFaces(device->CullFaces) {}
    void RestoreTarget() override {
        m_Device->Target = m_Target;
        m_Device->Viewport = m_Viewport;
    }
    ~VulkanRenderState() override {
        RestoreTarget();
        m_Device->Shader = m_Shader;
        m_Device->Textures = m_Textures;
        m_Device->SampledAttachments = m_Attachments;
        m_Device->Uniforms = std::move(m_Uniforms);
        m_Device->DepthTest = m_Depth;
        m_Device->DepthWrite = m_DepthWrite;
        m_Device->DepthLessEqual = m_LessEqual;
        m_Device->Blending = m_Blending;
        m_Device->CullFaces = m_CullFaces;
    }
private:
    std::shared_ptr<VulkanDevice> m_Device;
    VulkanFramebuffer* m_Target;
    VkViewport m_Viewport;
    const VulkanShader* m_Shader;
    std::array<const VulkanTexture2D*, 32> m_Textures;
    std::array<std::shared_ptr<VulkanImageAllocation>, 32> m_Attachments;
    std::array<std::vector<uint8_t>, 16> m_Uniforms;
    bool m_Depth, m_DepthWrite, m_LessEqual, m_Blending, m_CullFaces;
};
class VulkanRendererAPI final : public RendererAPI {
public:
    void Init() override { m_Device = VulkanDevice::Current(); }
    void ReleaseColorGrade() override {
        m_GradeShader.reset();
        m_GradeTexture.reset();
        m_GradeArray.reset();
        // Releases every embedded full-screen pass resource, palette swap and
        // mask transition included.
        m_PaletteShader.reset();
        m_PaletteTexture.reset();
        m_MaskShader.reset();
        m_MaskTexture.reset();
    }
    void ApplyColorGrade(float exposure, float saturation, float vignette) override {
        if (!std::isfinite(exposure) || !std::isfinite(saturation) || !std::isfinite(vignette)) return;
        if (exposure == 0 && saturation == 1 && vignette == 0) return;
        auto* target = m_Device->Target ? m_Device->Target : m_Device->DefaultTarget;
        if (!target || target->Colors().empty()) return;
        const auto& viewport = m_Device->Viewport;
        if (viewport.width <= 0 || viewport.height <= 0 || viewport.x < 0 || viewport.y < 0) return;
        const auto source = target->Colors().front();
        const auto width = static_cast<uint32_t>(viewport.width), height = static_cast<uint32_t>(viewport.height);
        const auto x = static_cast<uint32_t>(viewport.x), y = static_cast<uint32_t>(viewport.y);
        if (!width || !height || x > source->Width || y > source->Height
            || width > source->Width - x || height > source->Height - y) return;
        if (!m_GradeShader) {
            m_GradeShader = CreateRef<VulkanShader>("TomCat.ColorGrade", R"(#version 450 core
layout(location=0) out vec2 v_UV;
void main(){vec2 p=vec2((gl_VertexIndex<<1)&2,gl_VertexIndex&2);v_UV=p;gl_Position=vec4(p*2.0-1.0,0,1);}
)", R"(#version 450 core
layout(location=0) in vec2 v_UV;
layout(location=0) out vec4 o_Color;
layout(binding=0) uniform sampler2D u_Source;
uniform vec4 u_Grade;
void main(){vec4 color=texture(u_Source,v_UV);vec3 rgb=color.rgb*exp2(u_Grade.x);float luma=dot(rgb,vec3(.2126,.7152,.0722));rgb=mix(vec3(luma),rgb,u_Grade.y);vec2 d=v_UV*2.0-1.0;rgb*=1.0-u_Grade.z*smoothstep(.2,1.4,dot(d,d));o_Color=vec4(rgb,color.a);}
)");
        }
        if (!m_GradeArray) {
            m_GradeArray = CreateRef<VulkanVertexArray>();
            uint32_t indices[] = {0, 1, 2};
            m_GradeArray->SetIndexBuffer(CreateRef<VulkanIndexBuffer>(indices, 3));
        }
        if (!m_GradeTexture || m_GradeTexture->GetWidth() != width || m_GradeTexture->GetHeight() != height)
            m_GradeTexture = CreateRef<VulkanTexture2D>(width, height);

        // Read from a separate image so the postprocess never samples its own
        // color attachment. Integer picking and depth attachments are untouched.
        const auto destination = m_GradeTexture->NativeImage();
        m_Device->Barrier();
        VkImageCopy copy{};
        copy.srcSubresource = copy.dstSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
        copy.srcOffset = {static_cast<int32_t>(x), static_cast<int32_t>(y), 0};
        copy.extent = {width, height, 1};
        vkCmdCopyImage(m_Device->Commands(), source->Image, VK_IMAGE_LAYOUT_GENERAL,
            destination->Image, VK_IMAGE_LAYOUT_GENERAL, 1, &copy);
        m_Device->Keep(source); m_Device->Keep(destination);
        m_Device->Barrier();

        const auto* previousTexture = m_Device->Textures[0];
        struct RestoreTexture {
            VulkanDevice& Device;
            const VulkanTexture2D* Texture;
            std::shared_ptr<VulkanImageAllocation> Attachment;
            ~RestoreTexture() {
                Device.Textures[0] = Texture;
                Device.SampledAttachments[0] = Attachment;
            }
        } restore{*m_Device, previousTexture, m_Device->SampledAttachments[0]};
        m_GradeTexture->Bind(0);
        m_GradeShader->SetFloat4("u_Grade", {std::clamp(exposure, -10.f, 10.f),
            std::clamp(saturation, 0.f, 2.f), std::clamp(vignette, 0.f, 1.f), 0});
        m_GradeShader->Draw(m_GradeArray, 3, true, true);
    }
    // Shared first half of the palette/mask passes: same target, viewport, and
    // bounds verdicts as the color grade, with the pass' embedded shader and
    // the fullscreen triangle created lazily once.
    struct FullscreenPassTarget {
        std::shared_ptr<VulkanImageAllocation> Source;
        uint32_t Width = 0, Height = 0, X = 0, Y = 0;
    };
    bool ResolveFullscreenPassTarget(Ref<VulkanShader>& shader, const char* name,
        const char* vertexSource, const char* fragmentSource,
        FullscreenPassTarget& target) {
        // The embedded pass shaders compile before target resolution so a pass
        // only ever reports target-boundary conditions after its program is
        // valid; Shader creation throws on a compile failure.
        if (!shader)
            shader = CreateRef<VulkanShader>(name, vertexSource, fragmentSource);
        if (!m_GradeArray) {
            m_GradeArray = CreateRef<VulkanVertexArray>();
            uint32_t indices[] = {0, 1, 2};
            m_GradeArray->SetIndexBuffer(CreateRef<VulkanIndexBuffer>(indices, 3));
        }
        if (!shader || !m_GradeArray) return false;
        auto* frame = m_Device->Target ? m_Device->Target : m_Device->DefaultTarget;
        if (!frame || frame->Colors().empty()) return false;
        const auto& viewport = m_Device->Viewport;
        if (viewport.width <= 0 || viewport.height <= 0 || viewport.x < 0 || viewport.y < 0) return false;
        target.Source = frame->Colors().front();
        if (!target.Source) return false;
        const auto width = static_cast<uint32_t>(viewport.width);
        const auto height = static_cast<uint32_t>(viewport.height);
        const auto x = static_cast<uint32_t>(viewport.x);
        const auto y = static_cast<uint32_t>(viewport.y);
        if (!width || !height || x > target.Source->Width || y > target.Source->Height
            || width > target.Source->Width - x || height > target.Source->Height - y)
            return false;
        target.Width = width;
        target.Height = height;
        target.X = x;
        target.Y = y;
        return shader && m_GradeArray;
    }
    // Copies the scene color attachment into the pass' private texture so the
    // pass never samples the target it writes to (mirrors the color grade).
    bool CopySceneTargetInto(const FullscreenPassTarget& target,
        Ref<VulkanTexture2D>& passTexture,
        Ref<VulkanShader>& shader) {
        if (!passTexture || passTexture->GetWidth() != target.Width
            || passTexture->GetHeight() != target.Height)
            passTexture = CreateRef<VulkanTexture2D>(target.Width, target.Height,
                shader == m_PaletteShader);
        const auto destination = passTexture->NativeImage();
        if (!destination) return false;
        m_Device->Barrier();
        VkImageCopy copy{};
        copy.srcSubresource = copy.dstSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
        copy.srcOffset = {static_cast<int32_t>(target.X), static_cast<int32_t>(target.Y), 0};
        copy.extent = {target.Width, target.Height, 1};
        vkCmdCopyImage(m_Device->Commands(), target.Source->Image, VK_IMAGE_LAYOUT_GENERAL,
            destination->Image, VK_IMAGE_LAYOUT_GENERAL, 1, &copy);
        m_Device->Keep(target.Source); m_Device->Keep(destination);
        m_Device->Barrier();
        return true;
    }
    void ApplyPaletteSwap(const glm::vec4 (&oldColors)[4],
        const glm::vec4 (&newColors)[4], float threshold) override {
        // Resolving first ensures the embedded program exists even when the
        // caller's parameters are rejected; creation throws on compile errors.
        FullscreenPassTarget pass;
        if (!ResolveFullscreenPassTarget(m_PaletteShader, "TomCat.PaletteSwap",
            R"(#version 450 core
layout(location=0) out vec2 v_UV;
void main(){vec2 p=vec2((gl_VertexIndex<<1)&2,gl_VertexIndex&2);v_UV=p;gl_Position=vec4(p*2.0-1.0,0,1);}
)", R"(#version 450 core
layout(location=0) in vec2 v_UV;
layout(location=0) out vec4 o_Color;
layout(binding=0) uniform sampler2D u_Source;
// Keep these as one declaration per line: the legacy-uniform rewriter that
// lifts bare GLSL uniforms into TomCatNamedUniforms only matches single
// declarations, and the Vulkan profile rejects anything it leaves behind.
uniform vec4 u_OldDark;
uniform vec4 u_OldLight;
uniform vec4 u_OldPrimary;
uniform vec4 u_OldSecondary;
uniform vec4 u_NewDark;
uniform vec4 u_NewLight;
uniform vec4 u_NewPrimary;
uniform vec4 u_NewSecondary;
uniform float u_Threshold;
void main(){
    vec4 color=texture(u_Source,v_UV);
    vec4 replaced=color;
    vec4 tolerance=vec4(max(u_Threshold,0.0));
    if (all(lessThanEqual(abs(color-u_OldDark),tolerance))) replaced=u_NewDark;
    if (all(lessThanEqual(abs(color-u_OldLight),tolerance))) replaced=u_NewLight;
    if (all(lessThanEqual(abs(color-u_OldPrimary),tolerance))) replaced=u_NewPrimary;
    if (all(lessThanEqual(abs(color-u_OldSecondary),tolerance))) replaced=u_NewSecondary;
    o_Color=replaced;
}
)", pass))
            return;
        for (int index = 0; index < 4; ++index)
            if (!std::isfinite(oldColors[index].x) || !std::isfinite(oldColors[index].y)
                || !std::isfinite(oldColors[index].z) || !std::isfinite(oldColors[index].w)
                || !std::isfinite(newColors[index].x) || !std::isfinite(newColors[index].y)
                || !std::isfinite(newColors[index].z) || !std::isfinite(newColors[index].w))
                return;
        if (!std::isfinite(threshold)) return;
        // Exact palette matching requires the undeformed source pixel.
        if (!CopySceneTargetInto(pass, m_PaletteTexture, m_PaletteShader))
            return;
        const auto* previousTexture = m_Device->Textures[0];
        struct RestoreTexture {
            VulkanDevice& Device;
            const VulkanTexture2D* Texture;
            std::shared_ptr<VulkanImageAllocation> Attachment;
            ~RestoreTexture() {
                Device.Textures[0] = Texture;
                Device.SampledAttachments[0] = Attachment;
            }
        } restore{*m_Device, previousTexture, m_Device->SampledAttachments[0]};
        m_PaletteTexture->Bind(0);
        m_PaletteShader->SetFloat4("u_OldDark", oldColors[0]);
        m_PaletteShader->SetFloat4("u_OldLight", oldColors[1]);
        m_PaletteShader->SetFloat4("u_OldPrimary", oldColors[2]);
        m_PaletteShader->SetFloat4("u_OldSecondary", oldColors[3]);
        m_PaletteShader->SetFloat4("u_NewDark", newColors[0]);
        m_PaletteShader->SetFloat4("u_NewLight", newColors[1]);
        m_PaletteShader->SetFloat4("u_NewPrimary", newColors[2]);
        m_PaletteShader->SetFloat4("u_NewSecondary", newColors[3]);
        m_PaletteShader->SetFloat("u_Threshold", std::clamp(threshold, 0.f, 1.f));
        m_PaletteShader->Draw(m_GradeArray, 3, true, true);
    }
    void ApplyMaskTransition(const Ref<Texture2D>& mask, float progress,
        const glm::vec4& coverColor, bool rotatingIris = false) override {
        if (!std::isfinite(progress) || !std::isfinite(coverColor.x)
            || !std::isfinite(coverColor.y) || !std::isfinite(coverColor.z)
            || !std::isfinite(coverColor.w))
            return;
        // Progress 0 leaves the target untouched, matching the neutral grade.
        const float clampedProgress = std::clamp(progress, 0.f, 1.f);
        FullscreenPassTarget pass;
        if (!ResolveFullscreenPassTarget(m_MaskShader, "TomCat.MaskTransition",
            R"(#version 450 core
layout(location=0) out vec2 v_UV;
void main(){vec2 p=vec2((gl_VertexIndex<<1)&2,gl_VertexIndex&2);v_UV=p;gl_Position=vec4(p*2.0-1.0,0,1);}
)", R"(#version 450 core
layout(location=0) in vec2 v_UV;
layout(location=0) out vec4 o_Color;
layout(binding=0) uniform sampler2D u_Source;
layout(binding=1) uniform sampler2D u_Mask;
uniform float u_Progress;
uniform float u_RotatingIris;
uniform vec4 u_Cover;
void main(){
    vec4 source=texture(u_Source,v_UV);
    float masked=texture(u_Mask,v_UV).a;
    float cover=u_Progress<=0.0 ? 0.0 : step(masked,u_Progress);
    float progress = u_Progress;
    if (u_RotatingIris > 0.5) {
        vec2 size = vec2(textureSize(u_Source, 0));
        vec2 p = (v_UV - 0.5) * vec2(size.x / size.y, 1.0);
        float angle = progress * 6.2831853;
        float c = cos(angle), s = sin(angle);
        vec2 uv = mat2(c,-s,s,c) * p / max(0.0001,2.0*(1.0-progress)) + 0.5;
        cover = progress >= 1.0 || any(lessThan(uv,vec2(0))) || any(greaterThan(uv,vec2(1))) ? 1.0 : 1.0-texture(u_Mask,uv).a;
    }
    o_Color=mix(source,u_Cover,cover);
}
)", pass))
            return;
        const auto* maskTexture = dynamic_cast<const VulkanTexture2D*>(mask.get());
        if (!maskTexture || !mask->IsLoaded()) return;
        if (clampedProgress <= 0.f) return;
        if (!CopySceneTargetInto(pass, m_MaskTexture, m_MaskShader))
            return;
        const auto* previousSourceTexture = m_Device->Textures[0];
        const auto* previousMaskTexture = m_Device->Textures[1];
        struct RestoreTextures {
            VulkanDevice& Device;
            const VulkanTexture2D* Source;
            const VulkanTexture2D* Mask;
            std::shared_ptr<VulkanImageAllocation> SourceAttachment;
            std::shared_ptr<VulkanImageAllocation> MaskAttachment;
            ~RestoreTextures() {
                Device.Textures[0] = Source;
                Device.Textures[1] = Mask;
                Device.SampledAttachments[0] = SourceAttachment;
                Device.SampledAttachments[1] = MaskAttachment;
            }
        } restore{*m_Device, previousSourceTexture, previousMaskTexture,
            m_Device->SampledAttachments[0], m_Device->SampledAttachments[1]};
        m_MaskTexture->Bind(0);
        mask->Bind(1);
        m_Device->Keep(maskTexture->NativeImage());
        m_MaskShader->SetFloat("u_RotatingIris", rotatingIris ? 1.f : 0.f);
        m_MaskShader->SetFloat("u_Progress", clampedProgress);
        m_MaskShader->SetFloat4("u_Cover", coverColor);
        m_MaskShader->Draw(m_GradeArray, 3, true, true);
    }
    void SetViewport(uint32_t x, uint32_t y, uint32_t width, uint32_t height) override {
        if (width && height && m_Device->DefaultTarget) m_Device->DefaultTarget->Resize(width, height);
        m_Device->Viewport = {float(x), float(y), float(width), float(height), 0, 1};
    }
    void SetClearColor(const glm::vec4& color) override { m_Device->ClearColor = {color.r, color.g, color.b, color.a}; }
    void Clear() override { if (auto* target = m_Device->Target ? m_Device->Target : m_Device->DefaultTarget) target->Clear(m_Device->ClearColor); }
    void SetDepthTest(bool enabled) override { m_Device->DepthTest = enabled; }
    void SetDepthWrite(bool enabled) override { m_Device->DepthWrite = enabled; }
    void SetDepthLessEqual(bool enabled) override { m_Device->DepthLessEqual = enabled; }
    void SetBlending(bool enabled) override { m_Device->Blending = enabled; }
    void SetCullFaces(bool enabled) override { m_Device->CullFaces = enabled; }
    Scope<RenderState> CaptureState() override { return CreateScope<VulkanRenderState>(m_Device); }
    void DrawIndexed(const Ref<VertexArray>& array, uint32_t count) override {
        if (!array || !array->GetIndexBuffer()) return;
        if (!m_Device->Shader) throw std::logic_error("Draw requires a bound shader");
        m_Device->Shader->Draw(array, count ? count : array->GetIndexBuffer()->GetCount(), true);
    }
    void DrawLines(const Ref<VertexArray>& array, uint32_t count) override {
        if (!m_Device->Shader) throw std::logic_error("Draw requires a bound shader");
        m_Device->Shader->Draw(array, count, false);
    }
    void SetLineWidth(float width) override { if (std::isfinite(width) && width > 0) m_Device->LineWidth = width; }
private:
    std::shared_ptr<VulkanDevice> m_Device;
    Ref<VulkanShader> m_GradeShader;
    Ref<VulkanTexture2D> m_GradeTexture;
    Ref<VulkanVertexArray> m_GradeArray;
    Ref<VulkanShader> m_PaletteShader;
    Ref<VulkanTexture2D> m_PaletteTexture;
    Ref<VulkanShader> m_MaskShader;
    Ref<VulkanTexture2D> m_MaskTexture;
};
}
