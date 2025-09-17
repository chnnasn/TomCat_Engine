#include "tcpch.h"

#include "RenderCommand.h"
#include "platform/OpenGL/OpenGLRendererAPI.h"

namespace TomCat {

	RendererAPI* RenderCommand::s_RendererAPI = new OpenGLRendererAPI;



}