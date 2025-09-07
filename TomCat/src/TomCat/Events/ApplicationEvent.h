#pragma once

#include "Event.h"


namespace TomCat {
	class  TomCat_API WindowResizeEvent : public Event
	{
	public:
		WindowResizeEvent(unsigned int width,unsigned int height)
			:m_Width(width),m_Height(height) {};

		inline unsigned int GetWidth()const { return m_Width; }
		inline unsigned int GetHeight()const { return m_Height; }

		std::string ToString() const override 
		{
			std::stringstream ss;
			ss << "WindowResizeEvent :" << m_Width << "," << m_Height;
			return ss.str();
		}

		Event_Class_Type(WindowResize)

		Event_Class_Category(EventCategoryApplication)

	private:
		unsigned int m_Width, m_Height;

	};

	class TomCat_API WindowCloseEvent : public Event
	{
	public:
		WindowCloseEvent() {}

		Event_Class_Type(WindowClose)

		Event_Class_Category(EventCategoryApplication)
	};

	class TomCat_API AppTickEvent : public Event
	{
	public:
		AppTickEvent() {}

		Event_Class_Type(AppTick)

		Event_Class_Category(EventCategoryApplication)
	};


	class TomCat_API AppUpdateEvent : public Event
	{
	public:
		AppUpdateEvent() {}

		Event_Class_Type(AppUpdate)

		Event_Class_Category(EventCategoryApplication)
	};

	class TomCat_API AppRenderEvent : public Event
	{
	public:
		AppRenderEvent() {}

		Event_Class_Type(AppRender)

		Event_Class_Category(EventCategoryApplication)
	};
}
