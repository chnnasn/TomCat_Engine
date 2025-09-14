#include "tcpch.h"

#include "OpenGLContext.h"

#include <GLFW/glfw3.h>

#include <Glad/glad.h>

namespace TomCat {

	OpenGLContext::OpenGLContext(GLFWwindow* WindowHandle) : m_WindowHandle(WindowHandle)
	{

		TC_Core_Assert(WindowHandle, "m_WindowHandle为空");
	}

	void OpenGLContext::Init()
	{

		glfwMakeContextCurrent(m_WindowHandle);
		int status = gladLoadGLLoader((GLADloadproc)glfwGetProcAddress);
		TC_Core_Assert(status, "初始化Glad失败");

		TC_Core_Info("OpenGL Info");
		TC_Core_Info("Vendor: {0}", reinterpret_cast<const char*>(glGetString(GL_VENDOR)));
		TC_Core_Info("Renderer: {0}", reinterpret_cast<const char*>(glGetString(GL_RENDERER)));
		TC_Core_Info("Version: {0}", reinterpret_cast<const char*>(glGetString(GL_VERSION)));

	}

	void OpenGLContext::SwapBuffers()
	{

		glfwSwapBuffers(m_WindowHandle);

	}

}