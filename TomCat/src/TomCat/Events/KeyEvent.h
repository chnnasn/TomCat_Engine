#pragma once
#include "tcpch.h"
#include "Event.h"


namespace TomCat {
	class  TomCat_API KeyEvent : public Event
	{
	public:

		inline int GetKeyCode()const { return m_KeyCode; }
		
		Event_Class_Category(EventCategoryKeyboard | EventCategoryInput)

	protected:
		KeyEvent(int KeyCode)
			: m_KeyCode(KeyCode) {}

		int m_KeyCode;

	};

	class TomCat_API KeyPressedEvent : public KeyEvent
	{
	public:
		KeyPressedEvent(int keycode,int repeatCount)
			: KeyEvent(keycode),m_RepeatCount(repeatCount){}

		inline int GetRepeatCount()const { return m_RepeatCount; }

		std::string ToString() const override
		{
			std::stringstream ss;
			ss << "KeyPressedEvent:" << m_KeyCode << "(" << m_RepeatCount << "repeats)";
			return ss.str();
		}

		Event_Class_Type(KeyPressed)
	private:
		int m_RepeatCount;

	};

	class TomCat_API KeyReleasedEvent : public KeyEvent
	{
	public:
		KeyReleasedEvent(int keycode)
			: KeyEvent(keycode) {
		}

		std::string ToString() const override
		{
			std::stringstream ss;
			ss << "KeyReleasedEvent: " << m_KeyCode;
			return ss.str();
		}

		Event_Class_Type(KeyReleased)
	};

	class TomCat_API KeyTypedEvent : public KeyEvent
	{
	public:
		KeyTypedEvent(int keycode)
			: KeyEvent(keycode) {
		}

		std::string ToString() const override
		{
			std::stringstream ss;
			ss << "KeyTypedEvent: " << m_KeyCode;
			return ss.str();
		}

		Event_Class_Type(KeyTyped)
	};


}
