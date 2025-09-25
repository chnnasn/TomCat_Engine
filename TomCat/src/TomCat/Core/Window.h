#pragma once

#include "tcpch.h"

#include"TomCat/Core/Core.h"
#include "TomCat/Events/Event.h"

namespace TomCat {

	struct WindowProps
	{
		std::string Title;
		unsigned int Width;
		unsigned int Height;


		WindowProps(const std::string& title = "TomCat Engine",
			unsigned int width = 1920,
			unsigned int height = 1080)
			:Title(title), Width(width), Height(height)
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

		virtual unsigned int GetWidth() const = 0;
		virtual unsigned int GetHeight() const = 0;

		//窗口属性
		virtual void SetEventCallback(const EventCallbackFn& callback) = 0;
		virtual void SetVSync(bool enabled) = 0;
		virtual bool IsVSync() const = 0;

		virtual void* GetNativeWindow() const = 0;

		static Scope<Window> Create(const WindowProps& props = WindowProps());
	};

}
