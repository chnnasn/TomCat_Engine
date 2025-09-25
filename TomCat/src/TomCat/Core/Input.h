#pragma once

#include "TomCat/Core/Core.h"

namespace TomCat {

	class Input
	{
	protected:
		Input() = default;

	public:
		Input(const Input&) = delete;
		Input& operator=(const Input&) = delete;

		inline static bool IsKeyPressed(KeyCode KeyCode) { return s_Instance->IsKeyPressedImpl(static_cast<int>(KeyCode));}
		inline static bool IsMouseButtonPressed(MouseButtonCode Button) { return s_Instance->IsKeyPressedImpl(static_cast<int>(Button));}
		inline static std::pair<float,float> GetMousePositon() { return s_Instance->GetMousePositonImpl();}
		inline static float GetMouseX() { return s_Instance->GetMouseXImpl();}
		inline static float GetMouseY() { return s_Instance->GetMouseYImpl();}

	protected:
		virtual bool IsKeyPressedImpl(int KeyCode) = 0;
		virtual bool IsMouseButtonPressedImpl(int Button) = 0;
		virtual std::pair<float, float> GetMousePositonImpl() = 0;
		virtual float GetMouseXImpl() = 0;
		virtual float GetMouseYImpl() = 0;

	private:
		static Scope<Input> s_Instance;

	};


}
