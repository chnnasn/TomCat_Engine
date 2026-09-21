#pragma once
#include "tcpch.h"
#include "Event.h"
#include "InputModifiers.h"

namespace TomCat{

	class MouseMovedEvent : public Event
	{
	public:
		MouseMovedEvent(float x, float y)
			: m_MouseX(x), m_MouseY(y) {
		}

		inline float GetX() const { return m_MouseX; }
		inline float GetY() const { return m_MouseY; }

		std::string ToString() const override
		{
			std::stringstream ss;
			ss << "MouseMovedEvent: " << m_MouseX << ", " << m_MouseY;
			return ss.str();
		}

		Event_Class_Type(MouseMoved)
		Event_Class_Category(EventCategoryMouse | EventCategoryInput)
	private:
		float m_MouseX, m_MouseY;
	};

	class MouseScrolledEvent : public Event
	{
	public:
		MouseScrolledEvent(float xOffset, float yOffset)
			: m_XOffset(xOffset), m_YOffset(yOffset) {
		}

		inline float GetXOffset() const { return m_XOffset; }
		inline float GetYOffset() const { return m_YOffset; }

		std::string ToString() const override
		{
			std::stringstream ss;
			ss << "MouseScrolledEvent: " << GetXOffset() << ", " << GetYOffset();
			return ss.str();
		}

		Event_Class_Type(MouseScrolled)
		Event_Class_Category(EventCategoryMouse | EventCategoryInput)
	private:
		float m_XOffset, m_YOffset;
	};

	class MouseButtonEvent : public Event
	{
	public:
		inline int GetMouseButton() const { return m_Button; }
		inline const InputModifiers& GetModifiers() const { return m_Modifiers; }
		inline bool IsControlDown() const { return m_Modifiers.Control; }
		inline bool IsShiftDown() const { return m_Modifiers.Shift; }
		inline bool IsAltDown() const { return m_Modifiers.Alt; }
		inline bool IsSuperDown() const { return m_Modifiers.Super; }

		Event_Class_Category(EventCategoryMouse | EventCategoryInput | EventCategoryMouseButton)
	protected:
		MouseButtonEvent(int button, InputModifiers modifiers)
			: m_Button(button), m_Modifiers(modifiers) {
		}

		int m_Button;
		InputModifiers m_Modifiers;
	};

	class MouseButtonPressedEvent : public MouseButtonEvent
	{
	public:
		MouseButtonPressedEvent(int button, InputModifiers modifiers)
			: MouseButtonEvent(button, modifiers) {
		}

		std::string ToString() const override
		{
			std::stringstream ss;
			ss << "MouseButtonPressedEvent: " << m_Button;
			return ss.str();
		}

		Event_Class_Type(MouseButtonPressed)
	};

	class MouseButtonReleasedEvent : public MouseButtonEvent
	{
	public:
		MouseButtonReleasedEvent(int button, InputModifiers modifiers)
			: MouseButtonEvent(button, modifiers) {
		}

		std::string ToString() const override
		{
			std::stringstream ss;
			ss << "MouseButtonReleasedEvent: " << m_Button;
			return ss.str();
		}

		Event_Class_Type(MouseButtonReleased)
	};

}
