#include "tcpch.h"

#include "OpenGLContext.h"

#include <GLFW/glfw3.h>

#include <Glad/glad.h>
#include <stdexcept>

namespace TomCat {

	OpenGLContext::OpenGLContext(GLFWwindow* WindowHandle) : m_WindowHandle(WindowHandle)
	{

		if (!WindowHandle)
			throw std::invalid_argument("OpenGLContext requires a valid window handle");
	}

	void OpenGLContext::Init()
	{
		TC_PROFILE_FUNCTION();

		glfwMakeContextCurrent(m_WindowHandle);
		int status = gladLoadGLLoader((GLADloadproc)glfwGetProcAddress);
		if (!status)
			throw std::runtime_error("Failed to initialize Glad");
		if (!GLAD_GL_VERSION_4_6)
			throw std::runtime_error("TomCat requires OpenGL 4.6 for SPIR-V shader specialization");

		TC_Core_Info("OpenGL Info");
		TC_Core_Info("Vendor: {0}", reinterpret_cast<const char*>(glGetString(GL_VENDOR)));
		TC_Core_Info("Renderer: {0}", reinterpret_cast<const char*>(glGetString(GL_RENDERER)));
		TC_Core_Info("Version: {0}", reinterpret_cast<const char*>(glGetString(GL_VERSION)));

	}

	void OpenGLContext::SwapBuffers()
	{
		TC_PROFILE_FUNCTION();

		glfwSwapBuffers(m_WindowHandle);

	}

}
