#include "tcpch.h"
#include "TomCat/Core/Window.h"

#if defined(TC_PLATFORM_WINDOWS) || defined(TC_PLATFORM_LINUX)
#include "platform/Window/WindowsWindow.h"
#endif
#ifdef TC_PLATFORM_WEB
#include "platform/Web/WebWindow.h"
#endif

namespace TomCat
{

	Scope<Window> Window::Create(const WindowProps& props)
	{
#if defined(TC_PLATFORM_WINDOWS) || defined(TC_PLATFORM_LINUX)
		return CreateScope<WindowsWindow>(props);
#elif defined(TC_PLATFORM_WEB)
		return CreateScope<WebWindow>(props);
#else
		TC_Core_Assert(false, "Unknown platform!");
		return nullptr;
#endif
	}
}
