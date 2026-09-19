#include "tcpch.h"

#include "WindowsWindow.h"

#include "TomCat/Events/KeyEvent.h"
#include "TomCat/Events/MouseEvent.h"
#include "TomCat/Events/ApplicationEvent.h"
#include "TomCat/Core/Input.h"
#include "TomCat/Utils/PathUtils.h"

#include "Platform/OpenGL/OpenGLContext.h"

#include <stb_image.h>
#include <algorithm>
#include <cctype>
#include <cmath>
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

	float WindowsWindow::GetDPIScale() const
	{
		return m_Data.Metrics.GetDPIScale();
	}

	void WindowsWindow::RefreshNativeMetrics(bool dispatchEvent)
	{
		if (!m_Window)
			return;
		int logicalWidth = 0;
		int logicalHeight = 0;
		int framebufferWidth = 0;
		int framebufferHeight = 0;
		float contentScaleX = 1.0f;
		float contentScaleY = 1.0f;
		glfwGetWindowSize(m_Window, &logicalWidth, &logicalHeight);
		glfwGetFramebufferSize(m_Window, &framebufferWidth, &framebufferHeight);
		glfwGetWindowContentScale(m_Window, &contentScaleX, &contentScaleY);
		const WindowMetrics next = WindowMetrics::FromNative(logicalWidth,
			logicalHeight, framebufferWidth, framebufferHeight, contentScaleX,
			contentScaleY);
		const bool changed = next != m_Data.Metrics;
		m_Data.Metrics = next;
		m_Data.MetricsDirty = false;
		if (dispatchEvent && changed && m_Data.EventCallback)
		{
			WindowResizeEvent event(next);
			m_Data.EventCallback(event);
		}
	}

	void WindowsWindow::Init(const WindowProps& props)
	{
		TC_PROFILE_FUNCTION();

		m_Data.Title = props.Title;
		m_Data.Metrics = WindowMetrics::FromNative(static_cast<int>(props.Width),
			static_cast<int>(props.Height), static_cast<int>(props.Width),
			static_cast<int>(props.Height), 1.0f, 1.0f);

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
		glfwWindowHint(GLFW_RESIZABLE,
			props.DisplayMode == WindowDisplayMode::Borderless
				? GLFW_FALSE : (props.Resizable ? GLFW_TRUE : GLFW_FALSE));
		glfwWindowHint(GLFW_DECORATED,
			props.DisplayMode == WindowDisplayMode::Borderless
				? GLFW_FALSE : GLFW_TRUE);

		const bool fillsMonitor = props.DisplayMode == WindowDisplayMode::Borderless
			|| props.DisplayMode == WindowDisplayMode::ExclusiveFullscreen;
		GLFWmonitor* primaryMonitor = fillsMonitor ? glfwGetPrimaryMonitor() : nullptr;
		if (fillsMonitor && !primaryMonitor)
		{
			if (s_GLFWWindowCount == 0)
				glfwTerminate();
			throw std::runtime_error("No primary monitor is available for fullscreen display");
		}
		GLFWmonitor* monitor = props.DisplayMode
			== WindowDisplayMode::ExclusiveFullscreen ? primaryMonitor : nullptr;
		int windowWidth = static_cast<int>(props.Width);
		int windowHeight = static_cast<int>(props.Height);
		int windowX = 0;
		int windowY = 0;
		if (props.DisplayMode == WindowDisplayMode::Borderless)
		{
			const GLFWvidmode* videoMode = glfwGetVideoMode(primaryMonitor);
			if (!videoMode || videoMode->width <= 0 || videoMode->height <= 0)
			{
				if (s_GLFWWindowCount == 0)
					glfwTerminate();
				throw std::runtime_error("Primary monitor video mode is unavailable");
			}
			windowWidth = videoMode->width;
			windowHeight = videoMode->height;
			glfwGetMonitorPos(primaryMonitor, &windowX, &windowY);
		}

		{
			TC_PROFILE_SCOPE("glfwCreateWindow");
			m_Window = glfwCreateWindow(windowWidth, windowHeight,
				m_Data.Title.c_str(), monitor, nullptr);
			if (!m_Window)
			{
				if (s_GLFWWindowCount == 0)
					glfwTerminate();
				throw std::runtime_error("Failed to create GLFW window");
			}
			++s_GLFWWindowCount;
		}
		GLFWWindowInitializationGuard initializationGuard(m_Window, m_Context);
		if (props.DisplayMode == WindowDisplayMode::Borderless)
			glfwSetWindowPos(m_Window, windowX, windowY);

		m_Context = CreateScope<OpenGLContext>(m_Window);
		m_Context->Init();

		glfwSetWindowUserPointer(m_Window, &m_Data);
		RefreshNativeMetrics(false);
		SetVSync(props.VSync);
		Input::ClearState();
		Input::NotifyWindowFocus(
			glfwGetWindowAttrib(m_Window, GLFW_FOCUSED) == GLFW_TRUE,
			glfwGetTime());
		double initialCursorX = 0.0;
		double initialCursorY = 0.0;
		glfwGetCursorPos(m_Window, &initialCursorX, &initialCursorY);
		Input::NotifyMousePosition(static_cast<float>(initialCursorX),
			static_cast<float>(initialCursorY));
		glfwSetJoystickCallback([](int joystick, int event)
		{
			if (joystick < GLFW_JOYSTICK_1 || joystick > GLFW_JOYSTICK_LAST)
				return;
			// The managed API exposes GLFW's standard gamepad mapping. Ignore a
			// generic joystick connection; a later disconnect is naturally ignored
			// by Input when no matching gamepad connection was reported.
			if (event == GLFW_CONNECTED
				&& glfwJoystickIsGamepad(joystick) != GLFW_TRUE)
				return;
			Input::NotifyGamepadConnection(
				static_cast<uint32_t>(joystick - GLFW_JOYSTICK_1),
				event == GLFW_CONNECTED, glfwGetTime());
		});

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
			(void)Width;
			(void)Height;
			Data.MetricsDirty = true;
		});

		glfwSetFramebufferSizeCallback(m_Window,
			[](GLFWwindow* Window, int Width, int Height)
			{
				WindowData& Data = *(WindowData*)glfwGetWindowUserPointer(Window);
				(void)Width;
				(void)Height;
				Data.MetricsDirty = true;
			});

		glfwSetWindowContentScaleCallback(m_Window,
			[](GLFWwindow* Window, float XScale, float YScale)
			{
				WindowData& Data = *(WindowData*)glfwGetWindowUserPointer(Window);
				(void)XScale;
				(void)YScale;
				Data.MetricsDirty = true;
			});

		glfwSetWindowCloseCallback(m_Window, [](GLFWwindow* Window)
		{
			WindowData& Data = *(WindowData*)glfwGetWindowUserPointer(Window);

			WindowCloseEvent event;

			if (Data.EventCallback)
				Data.EventCallback(event);

		});

		glfwSetWindowFocusCallback(m_Window, [](GLFWwindow* Window, int focused)
		{
			WindowData& Data = *(WindowData*)glfwGetWindowUserPointer(Window);
			Input::NotifyWindowFocus(focused == GLFW_TRUE, glfwGetTime());
			if (focused == GLFW_TRUE)
			{
				WindowFocusEvent event;
				if (Data.EventCallback)
					Data.EventCallback(event);
			}
			else
			{
				WindowLostFocusEvent event;
				if (Data.EventCallback)
					Data.EventCallback(event);
			}
		});

		glfwSetKeyCallback(m_Window,[](GLFWwindow* Window, int key, int, int action, int mods)
		{
			WindowData& Data = *(WindowData*)glfwGetWindowUserPointer(Window);
			const InputModifiers modifiers = GetInputModifiers(mods);
			const double timestamp = glfwGetTime();

			switch (action)
			{
				case GLFW_PRESS:
				{
					Input::NotifyKey(static_cast<uint32_t>(key),
						InputEventQueue::Action::Pressed, timestamp);
					KeyPressedEvent event(key, 0, modifiers);
					if (Data.EventCallback)
						Data.EventCallback(event);
					break;
				}
				case GLFW_RELEASE:
				{
					Input::NotifyKey(static_cast<uint32_t>(key),
						InputEventQueue::Action::Released, timestamp);
					KeyReleasedEvent event(key, modifiers);
					if (Data.EventCallback)
						Data.EventCallback(event);
					break;
				}

				case GLFW_REPEAT:
				{
					Input::NotifyKey(static_cast<uint32_t>(key),
						InputEventQueue::Action::Repeated, timestamp);
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
			const double timestamp = glfwGetTime();

			switch (action)
			{
				case GLFW_PRESS:
				{
					Input::NotifyMouseButton(static_cast<uint32_t>(button),
						InputEventQueue::Action::Pressed, timestamp);
					MouseButtonPressedEvent event(button, modifiers);
					if (Data.EventCallback)
						Data.EventCallback(event);
					break;
				}
				case GLFW_RELEASE:
				{
					Input::NotifyMouseButton(static_cast<uint32_t>(button),
						InputEventQueue::Action::Released, timestamp);
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
			Input::NotifyScroll(static_cast<float>(xoffset), static_cast<float>(yoffset));
				
			MouseScrolledEvent event((float)xoffset,(float)yoffset);

			if (Data.EventCallback)
				Data.EventCallback(event);
		});

		glfwSetCursorPosCallback(m_Window, [](GLFWwindow* Window, double xpos, double ypos)
		{
			WindowData& Data = *(WindowData*)glfwGetWindowUserPointer(Window);
			Input::NotifyMousePosition(static_cast<float>(xpos),
				static_cast<float>(ypos));

			MouseMovedEvent event((float)xpos,(float)ypos);

			if (Data.EventCallback)
				Data.EventCallback(event);
		});


		glfwSetCharCallback(m_Window, [](GLFWwindow* Window, unsigned int KeyCode)
		{
				Input::NotifyCharacter(KeyCode);
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

		void WindowsWindow::PollEvents()
		{
			TC_PROFILE_FUNCTION();
			glfwPollEvents();
			// GLFW may deliver logical-size, framebuffer-size and content-scale
			// callbacks for one monitor/DPI transition. Query the complete native
			// snapshot once after polling and publish one coherent resize event.
			if (m_Data.MetricsDirty)
				RefreshNativeMetrics(true);
		}

		void WindowsWindow::Present()
		{
			TC_PROFILE_FUNCTION();
			m_Context->SwapBuffers();
		}

		void WindowsWindow::SetTitle(const std::string& title)
		{
			if (m_Data.Title == title)
				return;

			m_Data.Title = title;
			if (m_Window)
				glfwSetWindowTitle(m_Window, m_Data.Title.c_str());
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
