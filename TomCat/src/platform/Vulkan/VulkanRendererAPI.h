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
};
}
