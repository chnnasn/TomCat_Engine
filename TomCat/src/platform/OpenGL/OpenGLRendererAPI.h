#pragma once

#include "TomCat/Renderer/RendererAPI.h"
#include "TomCat/Renderer/Shader.h"
#include "TomCat/Renderer/UniformBuffer.h"
namespace TomCat{

	class OpenGLRendererAPI : public RendererAPI {

	public:
		virtual void Init() override;
        void ApplyColorGrade(float exposure, float saturation, float vignette) override;
        void ReleaseColorGrade() override;

		virtual void SetViewport(uint32_t x, uint32_t y, uint32_t width, uint32_t height) override;

		virtual void SetClearColor(const glm::vec4& color)override;
		virtual void Clear() override;
		virtual void SetDepthTest(bool enabled) override;

		virtual void DrawIndexed(const Ref<VertexArray>& vertexArray, uint32_t indexCount = 0) override;
		virtual void DrawLines(const Ref<VertexArray>& vertexArray, uint32_t vertexCount) override;
		virtual void SetLineWidth(float width) override;

    private:
        Ref<Shader> m_GradeShader;
        Ref<UniformBuffer> m_GradeUniform;
        uint32_t m_GradeTexture = 0, m_GradeFramebuffer = 0, m_GradeVAO = 0;
        int m_GradeWidth = 0, m_GradeHeight = 0;
	};
}
