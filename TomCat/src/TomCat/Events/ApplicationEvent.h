#pragma once

#include "Event.h"
#include "TomCat/Core/WindowMetrics.h"


namespace TomCat {
	class WindowFocusEvent : public Event
	{
	public:
		WindowFocusEvent() = default;
		Event_Class_Type(WindowFocus)
		Event_Class_Category(EventCategoryApplication)
	};

	class WindowLostFocusEvent : public Event
	{
	public:
		WindowLostFocusEvent() = default;
		Event_Class_Type(WindowLostFocus)
		Event_Class_Category(EventCategoryApplication)
	};

	class WindowResizeEvent : public Event
	{
	public:
		WindowResizeEvent(unsigned int width,unsigned int height)
			: WindowResizeEvent(WindowMetrics::FromNative(
				static_cast<int>(width), static_cast<int>(height),
				static_cast<int>(width), static_cast<int>(height), 1.0f, 1.0f)) {};
		explicit WindowResizeEvent(const WindowMetrics& metrics)
			: m_Metrics(metrics) {}

		// Rendering-facing compatibility accessors intentionally return pixels.
		inline unsigned int GetWidth()const { return m_Metrics.FramebufferWidth; }
		inline unsigned int GetHeight()const { return m_Metrics.FramebufferHeight; }
		inline unsigned int GetFramebufferWidth() const { return m_Metrics.FramebufferWidth; }
		inline unsigned int GetFramebufferHeight() const { return m_Metrics.FramebufferHeight; }
		inline unsigned int GetLogicalWidth() const { return m_Metrics.LogicalWidth; }
		inline unsigned int GetLogicalHeight() const { return m_Metrics.LogicalHeight; }
		inline float GetDPIScale() const { return m_Metrics.GetDPIScale(); }
		inline float GetScreenToFramebufferScaleX() const
		{
			return m_Metrics.GetScreenToFramebufferScaleX();
		}
		inline float GetScreenToFramebufferScaleY() const
		{
			return m_Metrics.GetScreenToFramebufferScaleY();
		}
		const WindowMetrics& GetMetrics() const { return m_Metrics; }

		std::string ToString() const override 
		{
			std::stringstream ss;
			ss << "WindowResizeEvent framebuffer=" << m_Metrics.FramebufferWidth
				<< "," << m_Metrics.FramebufferHeight << " logical="
				<< m_Metrics.LogicalWidth << "," << m_Metrics.LogicalHeight
				<< " dpi=" << m_Metrics.GetDPIScale();
			return ss.str();
		}

		Event_Class_Type(WindowResize)

		Event_Class_Category(EventCategoryApplication)

	private:
		WindowMetrics m_Metrics;

	};

	class WindowCloseEvent : public Event
	{
	public:
		WindowCloseEvent() {}

		Event_Class_Type(WindowClose)

		Event_Class_Category(EventCategoryApplication)
	};

}
