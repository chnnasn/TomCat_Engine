#pragma once

#include "glm/glm.hpp"
#include "VertexArray.h"
#include "Texture.h"

namespace TomCat {

	class RenderState
	{
	public:
		virtual ~RenderState() = default;
		virtual void RestoreTarget() = 0;
	};

	class RendererAPI
	{
	public:
		virtual ~RendererAPI() = default;

		enum class	API
		{
			None = 0,
			OpenGL = 1,
			Vulkan = 2
		};
	public:
		virtual void Init() = 0;
        // Full-screen post-process passes. Each one copies the current draw
        // target into a private texture, filters it with an embedded engine
        // shader, and draws the result back; UI rendered afterwards is never
        // affected. All passes early out on their neutral parameters.
        virtual void ApplyColorGrade(float exposure, float saturation, float vignette) = 0;
        // Exact four-pair palette swap; source sampling must stay NEAREST so
        // neighboring colors are not interpolated before the match.
        virtual void ApplyPaletteSwap(const glm::vec4 (&oldColors)[4],
            const glm::vec4 (&newColors)[4], float threshold) = 0;
        // Iris transition: pixels whose mask alpha is below progress are
        // covered with coverColor. A null mask covers the whole target.
        virtual void ApplyMaskTransition(const Ref<Texture2D>& mask, float progress,
            const glm::vec4& coverColor, bool rotatingIris = false) = 0;
        virtual void ReleaseColorGrade() = 0;

		virtual void SetViewport(uint32_t x, uint32_t y, uint32_t width, uint32_t height) = 0;
		virtual void SetClearColor(const glm::vec4& color) = 0;
		virtual void Clear() = 0;
		virtual void SetDepthTest(bool enabled) = 0;
		virtual void SetDepthWrite(bool enabled) = 0;
		virtual void SetDepthLessEqual(bool enabled) = 0;
		virtual void SetBlending(bool enabled) = 0;
		virtual void SetCullFaces(bool enabled) = 0;
		// Restores the target, pipeline and resource bindings when destroyed.
		virtual Scope<RenderState> CaptureState() = 0;

		virtual void DrawIndexed(const Ref<VertexArray>& vertexArray, uint32_t indexCount = 0) = 0;
		virtual void DrawLines(const Ref<VertexArray>& vertexArray, uint32_t vertexCount) = 0;
		virtual void SetLineWidth(float width) = 0;

		static API GetAPI();
		static Scope<RendererAPI> Create();
	private:
		friend class WindowsWindow;
		// Only the first native window may fall back, before renderer resources exist.
		static bool TryUseOpenGLFallback(const char* reason);
	};

}
