#pragma once

#include "TomCat/Renderer/RendererAPI.h"
#include "TomCat/Renderer/Shader.h"
#include "TomCat/Renderer/UniformBuffer.h"
namespace TomCat{

	class OpenGLRendererAPI : public RendererAPI {

	public:
		virtual void Init() override;
        void ApplyColorGrade(float exposure, float saturation, float vignette) override;
        void ApplyPaletteSwap(const glm::vec4 (&oldColors)[4],
            const glm::vec4 (&newColors)[4], float threshold) override;
        void ApplyMaskTransition(const Ref<Texture2D>& mask, float progress,
            const glm::vec4& coverColor, bool rotatingIris = false) override;
        void ReleaseColorGrade() override;

		virtual void SetViewport(uint32_t x, uint32_t y, uint32_t width, uint32_t height) override;

		virtual void SetClearColor(const glm::vec4& color)override;
		virtual void Clear() override;
		virtual void SetDepthTest(bool enabled) override;
		void SetDepthWrite(bool enabled) override;
		void SetDepthLessEqual(bool enabled) override;
		void SetBlending(bool enabled) override;
		void SetCullFaces(bool enabled) override;
		Scope<RenderState> CaptureState() override;

		virtual void DrawIndexed(const Ref<VertexArray>& vertexArray, uint32_t indexCount = 0) override;
		virtual void DrawLines(const Ref<VertexArray>& vertexArray, uint32_t vertexCount) override;
		virtual void SetLineWidth(float width) override;

    private:
        // Prepares this renderer's offscreen copy target for one full-screen
        // pass: (re)allocates the RGBA8 texture at the viewport size and binds
        // the private framebuffer with the texture attached. After a successful
        // call the private framebuffer is GL_DRAW_FRAMEBUFFER-bound and the
        // caller blits the scene target into it.
        bool PrepareFullscreenCopyTarget(uint32_t& texture, uint32_t& framebuffer,
            bool& framebufferComplete, int& width, int& height,
            const int32_t viewport[4], bool nearest);

	private:
        Ref<Shader> m_GradeShader;
        Ref<UniformBuffer> m_GradeUniform;
        uint32_t m_GradeTexture = 0, m_GradeFramebuffer = 0, m_GradeVAO = 0;
        int m_GradeWidth = 0, m_GradeHeight = 0;
        bool m_GradeFramebufferComplete = false;
        // PaletteSwap and MaskTransition each keep their own copy target so
        // their sampler filters (NEAREST vs LINEAR) never thrash between calls.
        Ref<Shader> m_PaletteShader;
        Ref<Shader> m_MaskShader;
        Ref<UniformBuffer> m_PaletteUniform;
        Ref<UniformBuffer> m_MaskUniform;
        uint32_t m_PaletteTexture = 0, m_PaletteFramebuffer = 0;
        int m_PaletteWidth = 0, m_PaletteHeight = 0;
        bool m_PaletteFramebufferComplete = false;
        uint32_t m_MaskTexture = 0, m_MaskFramebuffer = 0;
        int m_MaskWidth = 0, m_MaskHeight = 0;
        bool m_MaskFramebufferComplete = false;
        // Renderer instances belong to one initialized context, never a global GL cache.
        int m_MaxDrawBuffers = 1;
#ifdef TC_PLATFORM_WEB
        // Web clearBufferfv needs the color supplied through this renderer API.
        // Native Clear deliberately continues to honor direct glClearColor calls.
        glm::vec4 m_ClearColor{0.0f};
#endif
	};
}
