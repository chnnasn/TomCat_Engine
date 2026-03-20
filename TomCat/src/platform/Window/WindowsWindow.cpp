#include "tcpch.h"

#include "WindowsWindow.h"

#include "TomCat/Events/KeyEvent.h"
#include "TomCat/Events/MouseEvent.h"
#include "TomCat/Events/ApplicationEvent.h"

#include "Platform/OpenGL/OpenGLContext.h"

#include <stb_image.h>

#ifdef TC_PLATFORM_WINDOWS
#include <windows.h>
#include <shellapi.h>
#endif

namespace TomCat {

	static uint8_t s_GLFWWindowCount = 0;

	static void GLFWErrorCallback(int error, const char* description)
	{
		TC_Core_Error("GLFW ERROE({0}) :{1}",error,description);
	}

	Scope<Window> Window::Create(const WindowProps& props)
	{
	
		return CreateScope<WindowsWindow>(props);
	}

	WindowsWindow::WindowsWindow(const WindowProps& props) 
	{
		TC_PROFILE_FUNCTION();
		Init(props);
		
	}

	WindowsWindow::~WindowsWindow()
	{
		Shutdown();

	}

	void WindowsWindow::Init(const WindowProps& props)
	{
		TC_PROFILE_FUNCTION();

		m_Data.Title = props.Title;
		m_Data.Width = props.Width;
		m_Data.Height = props.Height;

		TC_Core_Info("Create window {0} {1} {2}", props.Title, props.Width, props.Height);

		if (s_GLFWWindowCount == 0)
		{
			TC_PROFILE_SCOPE("glfwCreateWindow");
			//系统关闭时调用glfwTerminate 
			int success = glfwInit();
			TC_Core_Assert(success, "不能初始化GLFW");
			glfwSetErrorCallback(GLFWErrorCallback);
		}

		{
			TC_PROFILE_SCOPE("glfwCreateWindow");
			m_Window = glfwCreateWindow((int)props.Width, (int)props.Height, m_Data.Title.c_str(), nullptr, nullptr);
			++s_GLFWWindowCount;
		}


		m_Context = CreateScope<OpenGLContext>(m_Window);
		m_Context->Init();

		glfwSetWindowUserPointer(m_Window, &m_Data);
		SetVSync(true);

		if (!props.IconPath.empty())
		{
			bool iconLoaded = false;

			if (props.IconPath.size() >= 4 &&
				(props.IconPath.substr(props.IconPath.size() - 4) == ".ico" ||
				 props.IconPath.substr(props.IconPath.size() - 4) == ".ICO"))
			{
#ifdef TC_PLATFORM_WINDOWS
				HICON hIcon = (HICON)LoadImageA(
					GetModuleHandle(NULL),
					props.IconPath.c_str(),
					IMAGE_ICON,
					0, 0,
					LR_LOADFROMFILE | LR_DEFAULTSIZE
				);

				if (hIcon)
				{
					ICONINFO iconInfo;
					if (GetIconInfo(hIcon, &iconInfo))
					{
						BITMAP bm;
						GetObject(iconInfo.hbmColor ? iconInfo.hbmColor : iconInfo.hbmMask, sizeof(BITMAP), &bm);

						int width = bm.bmWidth;
						int height = iconInfo.hbmColor ? bm.bmHeight : bm.bmHeight / 2;

						HDC hdc = GetDC(NULL);
						HDC hdcMem = CreateCompatibleDC(hdc);
						HBITMAP hbmColor = iconInfo.hbmColor ? iconInfo.hbmColor : iconInfo.hbmMask;

						BITMAPINFO bmi = { 0 };
						bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
						bmi.bmiHeader.biWidth = width;
						bmi.bmiHeader.biHeight = -height;
						bmi.bmiHeader.biPlanes = 1;
						bmi.bmiHeader.biBitCount = 32;
						bmi.bmiHeader.biCompression = BI_RGB;

						unsigned char* pixels = new unsigned char[width * height * 4];
						GetDIBits(hdcMem, hbmColor, 0, height, pixels, &bmi, DIB_RGB_COLORS);

						if (!iconInfo.hbmColor)
						{
							for (int i = 0; i < width * height; i++)
							{
								unsigned char mask = pixels[i * 4];
								pixels[i * 4 + 0] = mask;
								pixels[i * 4 + 1] = mask;
								pixels[i * 4 + 2] = mask;
								pixels[i * 4 + 3] = 255;
							}
						}

						GLFWimage icon;
						icon.width = width;
						icon.height = height;
						icon.pixels = pixels;
						glfwSetWindowIcon(m_Window, 1, &icon);

						delete[] pixels;
						DeleteDC(hdcMem);
						ReleaseDC(NULL, hdc);

						if (iconInfo.hbmColor) DeleteObject(iconInfo.hbmColor);
						if (iconInfo.hbmMask) DeleteObject(iconInfo.hbmMask);

						iconLoaded = true;
					}
					DestroyIcon(hIcon);
				}
#endif
			}

			if (!iconLoaded)
			{
				int width, height, channels;
				stbi_uc* pixels = stbi_load(props.IconPath.c_str(), &width, &height, &channels, 4);
				if (pixels)
				{
					GLFWimage icon;
					icon.width = width;
					icon.height = height;
					icon.pixels = pixels;
					glfwSetWindowIcon(m_Window, 1, &icon);
					stbi_image_free(pixels);
					iconLoaded = true;
				}
			}

			if (!iconLoaded)
			{
				TC_Core_Warn("Failed to load window icon: {0}", props.IconPath);
			}
		}

		//用于GLFW回调
		glfwSetWindowSizeCallback(m_Window, [](GLFWwindow* Window, int Width , int Height)
		{
			WindowData& Data = *(WindowData*) glfwGetWindowUserPointer(Window);
			
			Data.Width = Width;
			Data.Height = Height;

			WindowResizeEvent event(Width,Height);

			Data.EventCallback(event);
		});

		glfwSetWindowCloseCallback(m_Window, [](GLFWwindow* Window)
		{
			WindowData& Data = *(WindowData*)glfwGetWindowUserPointer(Window);

			WindowCloseEvent event;

			Data.EventCallback(event);

		});

		glfwSetKeyCallback(m_Window,[](GLFWwindow* Window, int key, int scancode, int action, int mods)
		{
			WindowData& Data = *(WindowData*)glfwGetWindowUserPointer(Window);

			switch (action)
			{
				case GLFW_PRESS:
				{
					KeyPressedEvent event(key,0);
					Data.EventCallback(event);
					break;
				}
				case GLFW_RELEASE:
				{
					KeyReleasedEvent event(key);
					Data.EventCallback(event);
					break;
				}

				case GLFW_REPEAT:
				{
					KeyPressedEvent event(key, 1);
					Data.EventCallback(event);
					break;
				}

			}
		});


		glfwSetMouseButtonCallback(m_Window,[](GLFWwindow* Window, int button, int action, int mods)
		{
			WindowData& Data = *(WindowData*)glfwGetWindowUserPointer(Window);

			switch (action)
			{
				case GLFW_PRESS:
				{
					MouseButtonPressedEvent event(button);
					Data.EventCallback(event);
					break;
				}
				case GLFW_RELEASE:
				{
					MouseButtonReleasedEvent event(button);
					Data.EventCallback(event);
					break;
				}
			}
		});

		glfwSetScrollCallback(m_Window,[](GLFWwindow* Window, double xoffset, double yoffset)
		{
			WindowData& Data = *(WindowData*)glfwGetWindowUserPointer(Window);
				
			MouseScrolledEvent event((float)xoffset,(float)yoffset);

			Data.EventCallback(event);
		});

		glfwSetCursorPosCallback(m_Window, [](GLFWwindow* Window, double xpos, double ypos)
		{
			WindowData& Data = *(WindowData*)glfwGetWindowUserPointer(Window);

			MouseMovedEvent event((float)xpos,(float)ypos);

			Data.EventCallback(event);
		});


		glfwSetCharCallback(m_Window, [](GLFWwindow* Window, unsigned int KeyCode)
		{
				WindowData& Data = *(WindowData*)glfwGetWindowUserPointer(Window);

				KeyTypedEvent event(KeyCode);

				Data.EventCallback(event);
		});

	}

		void WindowsWindow::Shutdown()
		{
			TC_PROFILE_FUNCTION();
			glfwDestroyWindow(m_Window);

			--s_GLFWWindowCount;

			if (s_GLFWWindowCount == 0)
			{
				glfwTerminate();
			}
		}

		void WindowsWindow::OnUpdate()
		{
			TC_PROFILE_FUNCTION();
			glfwPollEvents();

			m_Context->SwapBuffers();
		}

		void WindowsWindow::SetVSync(bool enabled)
		{
			TC_PROFILE_FUNCTION();

			if (enabled)
				glfwSwapInterval(1);
			else
				glfwSwapInterval(0);

			m_Data.VSync = enabled;
		}

		bool WindowsWindow::IsVSync() const
		{
			return m_Data.VSync;
		}
}