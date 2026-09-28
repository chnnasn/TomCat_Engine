#pragma once

#include "tcpch.h"

#include"TomCat/Core/Base.h"
#include "TomCat/Core/WindowMetrics.h"
#include "TomCat/Events/Event.h"

#include <filesystem>

namespace TomCat {

	enum class WindowDisplayMode
	{
		Windowed,
		Borderless,
		ExclusiveFullscreen
	};

	struct WindowProps
	{
		std::string Title;
		uint32_t Width;
		uint32_t Height;
		std::filesystem::path IconPath;
		WindowDisplayMode DisplayMode = WindowDisplayMode::Windowed;
		bool Resizable = true;
		bool VSync = true;
        // Authoring windows use logical startup dimensions bounded by the monitor.
        bool FitToWorkArea = false;
        bool EditorStyling = false;


		WindowProps(const std::string& title = "TomCat Engine",
			uint32_t  width = 1920,
			uint32_t  height = 1080,
			std::filesystem::path iconPath = {})
			:Title(title), Width(width), Height(height), IconPath(std::move(iconPath))
		{
		}
	};

	//基于窗口的桌面系统的接口
	class Window 
	{
	public:
		using EventCallbackFn = std::function<void(Event&)>;

		virtual ~Window(){}

		// Application owns the frame boundary: native events are polled first,
		// input is frozen, gameplay updates run, and only then is the back buffer
		// presented.
		virtual void PollEvents() = 0;
		virtual void Present() = 0;

		virtual uint32_t GetWidth() const = 0;
		virtual uint32_t GetHeight() const = 0;
		// Physical default-framebuffer extent used by OpenGL and runtime cameras.
		// Defaults preserve compatibility with headless/test Window backends.
		virtual uint32_t GetFramebufferWidth() const { return GetWidth(); }
		virtual uint32_t GetFramebufferHeight() const { return GetHeight(); }
		virtual float GetScreenToFramebufferScaleX() const
		{
			return GetWidth() > 0 && GetFramebufferWidth() > 0
				? static_cast<float>(GetFramebufferWidth()) / GetWidth() : 1.0f;
		}
		virtual float GetScreenToFramebufferScaleY() const
		{
			return GetHeight() > 0 && GetFramebufferHeight() > 0
				? static_cast<float>(GetFramebufferHeight()) / GetHeight() : 1.0f;
		}
		// UI content scale (1.0 at 96 DPI). It is deliberately independent of
		// the framebuffer ratio: on Windows it can be 1.5 while coordinates and
		// framebuffer pixels remain 1:1.
		virtual float GetDPIScale() const { return 1.0f; }

		//窗口属性
		virtual void SetTitle(const std::string& title) = 0;
		virtual void SetEventCallback(const EventCallbackFn& callback) = 0;
		virtual void SetVSync(bool enabled) = 0;
		virtual bool IsVSync() const = 0;
		virtual double GetTimeSeconds() const = 0;

		// Reject a native close request and make the window visible again so the
		// application can present its own confirmation UI.
		virtual void CancelCloseRequest() = 0;

		virtual void* GetNativeWindow() const = 0;

		static Scope<Window> Create(const WindowProps& props = WindowProps());
	};

}
