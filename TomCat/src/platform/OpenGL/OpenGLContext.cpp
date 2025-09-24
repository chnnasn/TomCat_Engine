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

	#ifdef TC_ENABLE_ASSERTS
			int versionMajor;
			int versionMinor;
			glGetIntegerv(GL_MAJOR_VERSION, &versionMajor);
			glGetIntegerv(GL_MINOR_VERSION, &versionMinor);

			TC_Core_Assert(versionMajor > 4 || (versionMajor == 4 && versionMinor >= 5), "Hazel requires at least OpenGL version 4.5!");
	#endif

	}

	void OpenGLContext::SwapBuffers()
	{

		glfwSwapBuffers(m_WindowHandle);

	}

}