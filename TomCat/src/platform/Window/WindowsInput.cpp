#include "tcpch.h"
#include "TomCat/Core/Input.h"
#include <glfw/glfw3.h>
#include "TomCat/Core/Application.h"

#include <algorithm>
#include <mutex>

namespace TomCat {
	namespace {
		std::mutex s_TransientInputMutex;
		float s_ScrollX = 0.0f;
		float s_ScrollY = 0.0f;
		bool s_WindowFocused = true;
	}

	bool Input::IsKeyPressed(KeyCode KeyCode)
	{

		auto Window = static_cast<GLFWwindow*>(Application::Get().GetWindow().GetNativeWindow());

		auto state = glfwGetKey(Window, static_cast<int32_t>(KeyCode));

		return state == GLFW_PRESS || state == GLFW_REPEAT;

	}

	bool Input::IsMouseButtonPressed(MouseCode Button)
	{

		auto Window = static_cast<GLFWwindow*>(Application::Get().GetWindow().GetNativeWindow());

		auto state = glfwGetMouseButton(Window, static_cast<int32_t>(Button));

		return state == GLFW_PRESS;
	}

	std::pair<float, float> Input::GetMousePosition()
	{
		auto Window = static_cast<GLFWwindow*>(Application::Get().GetWindow().GetNativeWindow());

		double Xpos, Ypos;

		glfwGetCursorPos(Window, &Xpos, &Ypos);

		return { (float)Xpos ,(float)Ypos };
	}

	float Input::GetMouseX()
	{
		auto [x,y] = GetMousePosition();

		return x;
	}

	float Input::GetMouseY()
	{
		auto [x, y] = GetMousePosition();

		return y;
	}

	bool Input::IsWindowFocused()
	{
		auto window = static_cast<GLFWwindow*>(Application::Get().GetWindow().GetNativeWindow());
		if (!window)
			return false;
		std::lock_guard<std::mutex> lock(s_TransientInputMutex);
		return s_WindowFocused && glfwGetWindowAttrib(window, GLFW_FOCUSED) == GLFW_TRUE;
	}

	Input::GamepadSnapshot Input::GetGamepadSnapshot(uint32_t index)
	{
		GamepadSnapshot snapshot;
		if (index >= MaximumGamepads)
			return snapshot;
		const int joystick = GLFW_JOYSTICK_1 + static_cast<int>(index);
		if (glfwJoystickIsGamepad(joystick) != GLFW_TRUE)
			return snapshot;

		GLFWgamepadstate state{};
		if (glfwGetGamepadState(joystick, &state) != GLFW_TRUE)
			return snapshot;
		snapshot.Connected = true;
		for (uint32_t button = 0; button < GamepadButtonCount; ++button)
			snapshot.Buttons[button] = state.buttons[button] == GLFW_PRESS;
		for (uint32_t axis = 0; axis < GamepadAxisCount; ++axis)
			snapshot.Axes[axis] = std::clamp(state.axes[axis], -1.0f, 1.0f);
		if (const char* name = glfwGetGamepadName(joystick))
			snapshot.Name = name;
		return snapshot;
	}

	void Input::NotifyScroll(float xOffset, float yOffset)
	{
		std::lock_guard<std::mutex> lock(s_TransientInputMutex);
		s_ScrollX += xOffset;
		s_ScrollY += yOffset;
	}

	std::pair<float, float> Input::ConsumeScrollDelta()
	{
		std::lock_guard<std::mutex> lock(s_TransientInputMutex);
		const std::pair<float, float> value{ s_ScrollX, s_ScrollY };
		s_ScrollX = 0.0f;
		s_ScrollY = 0.0f;
		return value;
	}

	void Input::NotifyWindowFocus(bool focused)
	{
		std::lock_guard<std::mutex> lock(s_TransientInputMutex);
		s_WindowFocused = focused;
		if (!focused)
		{
			s_ScrollX = 0.0f;
			s_ScrollY = 0.0f;
		}
	}
	
}
