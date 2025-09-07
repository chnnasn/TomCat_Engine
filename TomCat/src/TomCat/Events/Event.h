#pragma once
#include "tcpch.h"
#include "TomCat/Core.h"


namespace TomCat {

	// TomCat 中的事件目前是阻塞式的，意味着当事件发生时，
	// 它会立即被分发并且必须当场处理。
	// 对于未来，更好的策略可能是在事件总线中缓冲事件，
	// 并在更新阶段的"事件"部分处理它们。


	enum class EventType 
	{
		None = 0,
		WindowClose, WindowResize, WindowFocus, WindowLostFocus,
		AppTick,AppUpdate,AppRender,
		KeyPressed,KeyReleased,
		MouseButtonPressed, MouseButtonReleased,MouseMoved, MouseScrolled

	};

	enum EventCategory 
	{
		None = 0,
		EventCategoryApplication =  BIT(0),
		EventCategoryInput = BIT(1),
		EventCategoryKeyboard = BIT(2),
		EventCategoryMouse = BIT(3),
		EventCategoryMouseButton = BIT(4)

	};

#define Event_Class_Type(type) static EventType GetStaticType() { return EventType::##type; }\
								virtual EventType GetEventType() const override { return GetStaticType(); }\
								virtual const char* GetName() const override { return #type; }

#define Event_Class_Category(category) virtual int GetCategoryFlogs() const override {return category;}

	class TomCat_API Event
	{
		friend class EventDispatcher;

	public:
		virtual EventType GetEventType() const = 0;
		virtual const char* GetName() const = 0;
		virtual int GetCategoryFlogs() const = 0;
		virtual std::string ToString() const { return GetName(); }

		inline bool IsIncategory(EventCategory category)
		{
			return GetCategoryFlogs() & category;
		}

		bool m_Handled = false;

	protected:


	};

	class EventDispatcher
	{
		template<typename T>
		using EventFn = std::function<bool(T&)>;
	public:
		EventDispatcher(Event& event)
			: m_Event(event)
		{
		}

		template<typename T>
		bool Dispatch(EventFn<T> func)
		{
			if (m_Event.GetEventType() == T::GetStaticType())
			{
				m_Event.m_Handled = func(*(T*)&m_Event);
				return true;
			}
			return false;
		}
	private:
		Event& m_Event;
	};

	inline std::ostream& operator<<(std::ostream& os, const Event& e)
	{
		return os << e.ToString();
	}



}
