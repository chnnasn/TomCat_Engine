#include "tcpch.h"
#include "TomCat/Core/Window.h"

#ifdef TC_PLATFORM_WINDOWS
#include "platform/Window/WindowsWindow.h"
#endif

namespace TomCat
{

	Scope<Window> Window::Create(const WindowProps& props)
	{
#ifdef TC_PLATFORM_WINDOWS
		return CreateScope<WindowsWindow>(props);
#else
		TC_Core_Assert(false, "Unknown platform!");
		return nullptr;
#endif
	}
}