#pragma once
#include "tcpch.h"
#include "Event.h"


namespace TomCat {
	class  KeyEvent : public Event
	{
	public:

		inline int GetKeyCode()const { return m_KeyCode; }
		
		Event_Class_Category(EventCategoryKeyboard | EventCategoryInput)

	protected:
		KeyEvent(int KeyCode)
			: m_KeyCode(KeyCode) {}

		int m_KeyCode;

	};

	class KeyPressedEvent : public KeyEvent
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

	class KeyReleasedEvent : public KeyEvent
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

	class KeyTypedEvent : public KeyEvent
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
