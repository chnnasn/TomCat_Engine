#include "tcpch.h"

#include "RenderCommand.h"
#include "platform/OpenGL/OpenGLRendererAPI.h"

namespace TomCat {

	Scope<RendererAPI> RenderCommand::s_RendererAPI = RendererAPI::Create();

}