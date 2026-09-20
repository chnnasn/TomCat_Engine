#pragma once
#include "TomCat/Renderer/RendererAPI.h"
#include "VulkanShader.h"
#include <cmath>

namespace TomCat {
class VulkanRendererAPI final : public RendererAPI {
public:
    void Init() override { m_Device = VulkanDevice::Current(); }
    void SetViewport(uint32_t x, uint32_t y, uint32_t width, uint32_t height) override {
        if (width && height && m_Device->DefaultTarget) m_Device->DefaultTarget->Resize(width, height);
        m_Device->Viewport = {float(x), float(y), float(width), float(height), 0, 1};
    }
    void SetClearColor(const glm::vec4& color) override { m_Device->ClearColor = {color.r, color.g, color.b, color.a}; }
    void Clear() override { if (auto* target = m_Device->Target ? m_Device->Target : m_Device->DefaultTarget) target->Clear(m_Device->ClearColor); }
    void SetDepthTest(bool enabled) override { m_Device->DepthTest = enabled; }
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
};
}
