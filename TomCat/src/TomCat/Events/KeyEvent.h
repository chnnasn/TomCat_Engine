#pragma once
#include "tcpch.h"
#include "Event.h"
#include "InputModifiers.h"


namespace TomCat {
	class  KeyEvent : public Event
	{
	public:

		inline int GetKeyCode()const { return m_KeyCode; }
		inline const InputModifiers& GetModifiers() const { return m_Modifiers; }
		inline bool IsControlDown() const { return m_Modifiers.Control; }
		inline bool IsShiftDown() const { return m_Modifiers.Shift; }
		inline bool IsAltDown() const { return m_Modifiers.Alt; }
		inline bool IsSuperDown() const { return m_Modifiers.Super; }
		
		Event_Class_Category(EventCategoryKeyboard | EventCategoryInput)

	protected:
		KeyEvent(int keyCode, InputModifiers modifiers)
			: m_KeyCode(keyCode), m_Modifiers(modifiers) {}

		int m_KeyCode;
		InputModifiers m_Modifiers;

	};

	class KeyPressedEvent : public KeyEvent
	{
	public:
		KeyPressedEvent(int keycode, int repeatCount, InputModifiers modifiers)
			: KeyEvent(keycode, modifiers), m_RepeatCount(repeatCount) {}

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
		KeyReleasedEvent(int keycode, InputModifiers modifiers)
			: KeyEvent(keycode, modifiers) {
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
			: KeyEvent(keycode, {}) {
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
