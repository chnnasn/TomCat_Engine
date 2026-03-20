#pragma once

#include "tcpch.h"

#include"TomCat/Core/Base.h"
#include "TomCat/Events/Event.h"

namespace TomCat {

	struct WindowProps
	{
		std::string Title;
		uint32_t Width;
		uint32_t Height;
		std::string IconPath;


		WindowProps(const std::string& title = "TomCat Engine",
			uint32_t  width = 1920,
			uint32_t  height = 1080,
			const std::string& iconPath = "")
			:Title(title), Width(width), Height(height), IconPath(iconPath)
		{
		}
	};

	//基于窗口的桌面系统的接口
	class Window 
	{
	public:
		using EventCallbackFn = std::function<void(Event&)>;

		virtual ~Window(){}

		virtual void OnUpdate() = 0;

		virtual uint32_t GetWidth() const = 0;
		virtual uint32_t GetHeight() const = 0;

		//窗口属性
		virtual void SetEventCallback(const EventCallbackFn& callback) = 0;
		virtual void SetVSync(bool enabled) = 0;
		virtual bool IsVSync() const = 0;

		virtual void* GetNativeWindow() const = 0;

		static Scope<Window> Create(const WindowProps& props = WindowProps());
	};

}
