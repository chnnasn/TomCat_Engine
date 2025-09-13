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

	}

	void OpenGLContext::SwapBuffers()
	{

		glfwSwapBuffers(m_WindowHandle);

	}

}