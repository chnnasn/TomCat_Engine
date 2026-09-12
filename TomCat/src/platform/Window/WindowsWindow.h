#pragma once

#include "TomCat/Core/Window.h"
#include "TomCat/Renderer/GraphicsContext.h"

#include <GLFW/glfw3.h>

namespace TomCat {
	class WindowsWindow : public Window
	{
	public:
		WindowsWindow(const WindowProps& props);
		virtual ~WindowsWindow();

		void OnUpdate() override;

		inline unsigned int GetWidth() const override { return m_Data.Metrics.LogicalWidth; }
		inline unsigned int GetHeight() const override { return m_Data.Metrics.LogicalHeight; }
		inline unsigned int GetFramebufferWidth() const override
		{
			return m_Data.Metrics.FramebufferWidth;
		}
		inline unsigned int GetFramebufferHeight() const override
		{
			return m_Data.Metrics.FramebufferHeight;
		}
		inline float GetScreenToFramebufferScaleX() const override
		{
			return m_Data.Metrics.GetScreenToFramebufferScaleX();
		}
		inline float GetScreenToFramebufferScaleY() const override
		{
			return m_Data.Metrics.GetScreenToFramebufferScaleY();
		}
		float GetDPIScale() const override;

		// Window attributes
		void SetTitle(const std::string& title) override;
		inline void SetEventCallback(const EventCallbackFn& callback) override { m_Data.EventCallback = callback; }
		void SetVSync(bool enabled) override;
		bool IsVSync() const override;
		double GetTimeSeconds() const override;
		void CancelCloseRequest() override;

		inline void* GetNativeWindow() const override { return m_Window; }
	private:
		virtual void Init(const WindowProps& props);
		virtual void Shutdown();
		void RefreshNativeMetrics(bool dispatchEvent);
	private:
		GLFWwindow* m_Window = nullptr;
		Scope<GraphicsContext> m_Context;

		struct WindowData
		{
			std::string Title;
			WindowMetrics Metrics;
			bool MetricsDirty = false;
			bool VSync = false;

			EventCallbackFn EventCallback;
		};

		WindowData m_Data;
	};

}
