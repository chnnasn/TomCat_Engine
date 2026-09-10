#pragma once
#include "tcpch.h"
#include "Event.h"


namespace TomCat {
	struct KeyModifiers
	{
		bool Control = false;
		bool Shift = false;
		bool Alt = false;
		bool Super = false;
	};

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
		KeyPressedEvent(int keycode, int repeatCount, KeyModifiers modifiers = {})
			: KeyEvent(keycode), m_RepeatCount(repeatCount), m_Modifiers(modifiers) {}

		inline int GetRepeatCount()const { return m_RepeatCount; }
		inline const KeyModifiers& GetModifiers() const { return m_Modifiers; }
		inline bool IsControlDown() const { return m_Modifiers.Control; }
		inline bool IsShiftDown() const { return m_Modifiers.Shift; }
		inline bool IsAltDown() const { return m_Modifiers.Alt; }
		inline bool IsSuperDown() const { return m_Modifiers.Super; }

		std::string ToString() const override
		{
			std::stringstream ss;
			ss << "KeyPressedEvent:" << m_KeyCode << "(" << m_RepeatCount << "repeats)";
			return ss.str();
		}

		Event_Class_Type(KeyPressed)
	private:
		int m_RepeatCount;
		KeyModifiers m_Modifiers;

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
