#pragma once

#include "TomCat/Renderer/RendererAPI.h"
namespace TomCat{

	class OpenGLRendererAPI : public RendererAPI {

	public:

		virtual void SetClearColor(const glm::vec4& color)override;
		virtual void Clear() override;

		virtual void DrawIndexed(const Ref<VertexArray>& vertexArray) override;

	};
}
