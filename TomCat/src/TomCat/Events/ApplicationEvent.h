#pragma once

#include "Event.h"


namespace TomCat {
	class WindowResizeEvent : public Event
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

	class WindowCloseEvent : public Event
	{
	public:
		WindowCloseEvent() {}

		Event_Class_Type(WindowClose)

		Event_Class_Category(EventCategoryApplication)
	};

}
