#include "tcpch.h"

#include "WindowsWindow.h"

#include "TomCat/Events/KeyEvent.h"
#include "TomCat/Events/MouseEvent.h"
#include "TomCat/Events/ApplicationEvent.h"
#include "TomCat/Utils/PathUtils.h"

#include "Platform/OpenGL/OpenGLContext.h"

#include <stb_image.h>
#include <algorithm>
#include <cctype>
#include <fstream>
#include <limits>
#include <stdexcept>
#include <vector>

#ifdef TC_PLATFORM_WINDOWS
#include <windows.h>
#include <shellapi.h>
#endif

namespace TomCat {

	static uint8_t s_GLFWWindowCount = 0;

	class GLFWWindowInitializationGuard
	{
	public:
		GLFWWindowInitializationGuard(GLFWwindow*& window, Scope<GraphicsContext>& context) noexcept
			: m_Window(window), m_Context(context)
		{
		}

		GLFWWindowInitializationGuard(const GLFWWindowInitializationGuard&) = delete;
		GLFWWindowInitializationGuard& operator=(const GLFWWindowInitializationGuard&) = delete;

		~GLFWWindowInitializationGuard() noexcept
		{
			if (!m_Active)
				return;

			m_Context.reset();
			if (m_Window)
			{
				glfwDestroyWindow(m_Window);
				m_Window = nullptr;
			}

			if (s_GLFWWindowCount > 0)
				--s_GLFWWindowCount;
			if (s_GLFWWindowCount == 0)
				glfwTerminate();
		}

		void Release() noexcept { m_Active = false; }

	private:
		GLFWwindow*& m_Window;
		Scope<GraphicsContext>& m_Context;
		bool m_Active = true;
	};

	static InputModifiers GetInputModifiers(int mods)
	{
		InputModifiers modifiers;
		modifiers.Control = (mods & GLFW_MOD_CONTROL) != 0;
		modifiers.Shift = (mods & GLFW_MOD_SHIFT) != 0;
		modifiers.Alt = (mods & GLFW_MOD_ALT) != 0;
		modifiers.Super = (mods & GLFW_MOD_SUPER) != 0;
		return modifiers;
	}

	static void GLFWErrorCallback(int error, const char* description)
	{
		TC_Core_Error("GLFW error ({0}): {1}", error, description);
	}

	static stbi_uc* LoadImageFile(const std::filesystem::path& path, int* width, int* height,
		int* channels, int desiredChannels)
	{
		std::ifstream input(path, std::ios::binary | std::ios::ate);
		std::streamoff size = -1;
		if (input)
			size = static_cast<std::streamoff>(input.tellg());
		if (size <= 0 || size > std::numeric_limits<int>::max())
			return nullptr;
		std::vector<stbi_uc> encoded(static_cast<size_t>(size));
		input.seekg(0, std::ios::beg);
		if (!input.read(reinterpret_cast<char*>(encoded.data()), static_cast<std::streamsize>(size)))
			return nullptr;
		return stbi_load_from_memory(encoded.data(), static_cast<int>(encoded.size()),
			width, height, channels, desiredChannels);
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
			glfwSetErrorCallback(GLFWErrorCallback);
			int success = glfwInit();
			if (!success)
				throw std::runtime_error("Failed to initialize GLFW");
		}

		glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 4);
		glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 6);
		glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);

		{
			TC_PROFILE_SCOPE("glfwCreateWindow");
			m_Window = glfwCreateWindow((int)props.Width, (int)props.Height, m_Data.Title.c_str(), nullptr, nullptr);
			if (!m_Window)
			{
				if (s_GLFWWindowCount == 0)
					glfwTerminate();
				throw std::runtime_error("Failed to create GLFW window");
			}
			++s_GLFWWindowCount;
		}
		GLFWWindowInitializationGuard initializationGuard(m_Window, m_Context);

		m_Context = CreateScope<OpenGLContext>(m_Window);
		m_Context->Init();

		glfwSetWindowUserPointer(m_Window, &m_Data);
		SetVSync(true);

		if (!props.IconPath.empty())
		{
			bool iconLoaded = false;

			std::string extension = PathToUTF8(props.IconPath.extension());
			std::transform(extension.begin(), extension.end(), extension.begin(),
				[](unsigned char character) { return static_cast<char>(std::tolower(character)); });
			if (extension == ".ico")
			{
#ifdef TC_PLATFORM_WINDOWS
				HICON hIcon = (HICON)LoadImageW(
					GetModuleHandleW(nullptr),
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
				stbi_uc* pixels = LoadImageFile(props.IconPath, &width, &height, &channels, 4);
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
				TC_Core_Warn("Failed to load window icon: {0}", PathToUTF8(props.IconPath));
			}
		}

		//用于GLFW回调
		glfwSetWindowSizeCallback(m_Window, [](GLFWwindow* Window, int Width , int Height)
		{
			WindowData& Data = *(WindowData*) glfwGetWindowUserPointer(Window);
			
			Data.Width = Width;
			Data.Height = Height;

			WindowResizeEvent event(Width,Height);

			if (Data.EventCallback)
				Data.EventCallback(event);
		});

		glfwSetWindowCloseCallback(m_Window, [](GLFWwindow* Window)
		{
			WindowData& Data = *(WindowData*)glfwGetWindowUserPointer(Window);

			WindowCloseEvent event;

			if (Data.EventCallback)
				Data.EventCallback(event);

		});

		glfwSetKeyCallback(m_Window,[](GLFWwindow* Window, int key, int, int action, int mods)
		{
			WindowData& Data = *(WindowData*)glfwGetWindowUserPointer(Window);
			const InputModifiers modifiers = GetInputModifiers(mods);

			switch (action)
			{
				case GLFW_PRESS:
				{
					KeyPressedEvent event(key, 0, modifiers);
					if (Data.EventCallback)
						Data.EventCallback(event);
					break;
				}
				case GLFW_RELEASE:
				{
					KeyReleasedEvent event(key, modifiers);
					if (Data.EventCallback)
						Data.EventCallback(event);
					break;
				}

				case GLFW_REPEAT:
				{
					KeyPressedEvent event(key, 1, modifiers);
					if (Data.EventCallback)
						Data.EventCallback(event);
					break;
				}

			}
		});


		glfwSetMouseButtonCallback(m_Window,[](GLFWwindow* Window, int button, int action, int mods)
		{
			WindowData& Data = *(WindowData*)glfwGetWindowUserPointer(Window);
			const InputModifiers modifiers = GetInputModifiers(mods);

			switch (action)
			{
				case GLFW_PRESS:
				{
					MouseButtonPressedEvent event(button, modifiers);
					if (Data.EventCallback)
						Data.EventCallback(event);
					break;
				}
				case GLFW_RELEASE:
				{
					MouseButtonReleasedEvent event(button, modifiers);
					if (Data.EventCallback)
						Data.EventCallback(event);
					break;
				}
			}
		});

		glfwSetScrollCallback(m_Window,[](GLFWwindow* Window, double xoffset, double yoffset)
		{
			WindowData& Data = *(WindowData*)glfwGetWindowUserPointer(Window);
				
			MouseScrolledEvent event((float)xoffset,(float)yoffset);

			if (Data.EventCallback)
				Data.EventCallback(event);
		});

		glfwSetCursorPosCallback(m_Window, [](GLFWwindow* Window, double xpos, double ypos)
		{
			WindowData& Data = *(WindowData*)glfwGetWindowUserPointer(Window);

			MouseMovedEvent event((float)xpos,(float)ypos);

			if (Data.EventCallback)
				Data.EventCallback(event);
		});


		glfwSetCharCallback(m_Window, [](GLFWwindow* Window, unsigned int KeyCode)
		{
				WindowData& Data = *(WindowData*)glfwGetWindowUserPointer(Window);

				KeyTypedEvent event(KeyCode);

			if (Data.EventCallback)
				Data.EventCallback(event);
		});

		initializationGuard.Release();

	}

		void WindowsWindow::Shutdown()
		{
			TC_PROFILE_FUNCTION();
			m_Context.reset();
			if (!m_Window)
				return;

			glfwDestroyWindow(m_Window);
			m_Window = nullptr;

			if (s_GLFWWindowCount > 0)
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

		double WindowsWindow::GetTimeSeconds() const
		{
			return glfwGetTime();
		}

		void WindowsWindow::CancelCloseRequest()
		{
			if (!m_Window)
				return;

			glfwSetWindowShouldClose(m_Window, GLFW_FALSE);
			if (glfwGetWindowAttrib(m_Window, GLFW_ICONIFIED) == GLFW_TRUE)
				glfwRestoreWindow(m_Window);
			glfwShowWindow(m_Window);
			glfwFocusWindow(m_Window);
		}
}
