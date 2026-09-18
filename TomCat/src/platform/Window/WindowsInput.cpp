#include "tcpch.h"
#include "TomCat/Core/Input.h"

#include "TomCat/Core/Application.h"

#include <algorithm>
#include <GLFW/glfw3.h>
#ifdef TC_PLATFORM_WEB
#include <emscripten/html5.h>
#endif

namespace TomCat {
	namespace {
		InputEventQueue s_EventQueue;
		float s_PendingScrollX = 0.0f;
		float s_PendingScrollY = 0.0f;
		float s_FrameScrollX = 0.0f;
		float s_FrameScrollY = 0.0f;
		float s_LiveMouseX = 0.0f;
		float s_LiveMouseY = 0.0f;
		float s_FrameMouseX = 0.0f;
		float s_FrameMouseY = 0.0f;
		bool s_LiveWindowFocused = true;
		bool s_FrameWindowFocused = true;
		std::array<Input::GamepadSnapshot, Input::MaximumGamepads> s_FrameGamepads{};
		std::array<bool, Input::MaximumGamepads> s_ReportedGamepadConnections{};

		Input::GamepadSnapshot PollGamepad(uint32_t index)
		{
			Input::GamepadSnapshot snapshot;
			Application* application = Application::TryGet();
			if (!application || !application->HasWindow()
				|| index >= Input::MaximumGamepads)
				return snapshot;

#ifdef TC_PLATFORM_WEB
			EmscriptenGamepadEvent state{};
			if (emscripten_get_gamepad_status(index, &state) != EMSCRIPTEN_RESULT_SUCCESS
				|| !state.connected || std::string_view(state.mapping) != "standard")
				return snapshot;
			snapshot.Connected = true;
			// Browser standard mapping has triggers at 6/7; GLFW exposes them as axes.
			constexpr int buttons[] = {0, 1, 2, 3, 4, 5, 8, 9, 16, 10, 11, 12, 13, 14, 15};
			for (uint32_t button = 0; button < Input::GamepadButtonCount; ++button)
				snapshot.Buttons[button] = buttons[button] < state.numButtons && state.digitalButton[buttons[button]];
			for (int axis = 0; axis < 4 && axis < state.numAxes; ++axis)
				snapshot.Axes[axis] = std::clamp(float(state.axis[axis]), -1.0f, 1.0f);
			snapshot.Axes[4] = state.numButtons > 6 ? float(state.analogButton[6] * 2 - 1) : -1.0f;
			snapshot.Axes[5] = state.numButtons > 7 ? float(state.analogButton[7] * 2 - 1) : -1.0f;
			snapshot.Name = state.id;
#else
			const int joystick = GLFW_JOYSTICK_1 + static_cast<int>(index);
			if (glfwJoystickIsGamepad(joystick) != GLFW_TRUE)
				return snapshot;
			GLFWgamepadstate state{};
			if (glfwGetGamepadState(joystick, &state) != GLFW_TRUE)
				return snapshot;
			snapshot.Connected = true;
			for (uint32_t button = 0; button < Input::GamepadButtonCount; ++button)
				snapshot.Buttons[button] = state.buttons[button] == GLFW_PRESS;
			for (uint32_t axis = 0; axis < Input::GamepadAxisCount; ++axis)
				snapshot.Axes[axis] = std::clamp(state.axes[axis], -1.0f, 1.0f);
			if (const char* name = glfwGetGamepadName(joystick))
				snapshot.Name = name;
#endif
			return snapshot;
		}
	}

	bool Input::IsKeyPressed(KeyCode keyCode)
	{
		return s_EventQueue.GetSnapshot().IsHeld(InputEventQueue::Device::Keyboard,
			static_cast<uint32_t>(keyCode));
	}

	bool Input::IsMouseButtonPressed(MouseCode button)
	{
		return s_EventQueue.GetSnapshot().IsHeld(InputEventQueue::Device::Mouse,
			static_cast<uint32_t>(button));
	}

	std::pair<float, float> Input::GetMousePosition()
	{
		return { s_FrameMouseX, s_FrameMouseY };
	}

	float Input::GetMouseX()
	{
		return GetMousePosition().first;
	}

	float Input::GetMouseY()
	{
		return GetMousePosition().second;
	}

	bool Input::IsWindowFocused()
	{
		return s_FrameWindowFocused;
	}

	Input::GamepadSnapshot Input::GetGamepadSnapshot(uint32_t index)
	{
		return index < MaximumGamepads ? s_FrameGamepads[index] : GamepadSnapshot{};
	}

	void Input::BeginFrame()
	{
#ifdef TC_PLATFORM_WEB
		emscripten_sample_gamepad_data();
#endif
		std::array<GamepadSnapshot, MaximumGamepads> nextGamepads{};
		Application* application = Application::TryGet();
		const double timestamp = application && application->HasWindow()
			? glfwGetTime() : 0.0;
		for (uint32_t index = 0; index < MaximumGamepads; ++index)
		{
			nextGamepads[index] = PollGamepad(index);
			if (!s_LiveWindowFocused)
			{
				nextGamepads[index].Buttons.fill(false);
				nextGamepads[index].Axes.fill(0.0f);
			}
			const GamepadSnapshot& before = s_FrameGamepads[index];
			const GamepadSnapshot& after = nextGamepads[index];
			if (before.Connected != after.Connected
				&& s_ReportedGamepadConnections[index] != after.Connected)
			{
				(void)s_EventQueue.Push(InputEventQueue::Device::GamepadConnection,
					0, after.Connected ? InputEventQueue::Action::Pressed
						: InputEventQueue::Action::Released, timestamp, index);
				s_ReportedGamepadConnections[index] = after.Connected;
			}
			for (uint32_t button = 0; button < GamepadButtonCount; ++button)
			{
				if (before.Buttons[button] == after.Buttons[button])
					continue;
				(void)s_EventQueue.Push(InputEventQueue::Device::GamepadButton,
					button, after.Buttons[button] ? InputEventQueue::Action::Pressed
						: InputEventQueue::Action::Released, timestamp, index);
			}
		}
		s_EventQueue.Freeze();
		s_FrameGamepads = std::move(nextGamepads);
		s_FrameScrollX = s_PendingScrollX;
		s_FrameScrollY = s_PendingScrollY;
		s_PendingScrollX = 0.0f;
		s_PendingScrollY = 0.0f;
		s_FrameMouseX = s_LiveMouseX;
		s_FrameMouseY = s_LiveMouseY;
		s_FrameWindowFocused = s_LiveWindowFocused;
	}

	const InputEventQueue::FrameSnapshot& Input::GetFrameSnapshot()
	{
		return s_EventQueue.GetSnapshot();
	}

	void Input::ClearState()
	{
		s_EventQueue.ClearState();
		s_PendingScrollX = 0.0f;
		s_PendingScrollY = 0.0f;
		s_FrameScrollX = 0.0f;
		s_FrameScrollY = 0.0f;
		s_LiveMouseX = 0.0f;
		s_LiveMouseY = 0.0f;
		s_FrameMouseX = 0.0f;
		s_FrameMouseY = 0.0f;
		s_LiveWindowFocused = true;
		s_FrameWindowFocused = true;
		s_FrameGamepads = {};
		s_ReportedGamepadConnections.fill(false);
	}

	void Input::NotifyKey(uint32_t key, InputEventQueue::Action action,
		double timestamp)
	{
		if (!s_LiveWindowFocused && action != InputEventQueue::Action::Released)
			return;
		(void)s_EventQueue.Push(InputEventQueue::Device::Keyboard, key, action,
			timestamp);
	}

	void Input::NotifyMouseButton(uint32_t button, InputEventQueue::Action action,
		double timestamp)
	{
		if (!s_LiveWindowFocused && action != InputEventQueue::Action::Released)
			return;
		(void)s_EventQueue.Push(InputEventQueue::Device::Mouse, button, action,
			timestamp);
	}

	void Input::NotifyMousePosition(float x, float y)
	{
		s_LiveMouseX = x;
		s_LiveMouseY = y;
	}

	void Input::NotifyScroll(float xOffset, float yOffset)
	{
		if (!s_LiveWindowFocused)
			return;
		s_PendingScrollX += xOffset;
		s_PendingScrollY += yOffset;
	}

	void Input::NotifyGamepadConnection(uint32_t gamepad, bool connected,
		double timestamp)
	{
		if (gamepad >= MaximumGamepads
			|| s_ReportedGamepadConnections[gamepad] == connected)
			return;
		(void)s_EventQueue.Push(InputEventQueue::Device::GamepadConnection, 0,
			connected ? InputEventQueue::Action::Pressed
				: InputEventQueue::Action::Released, timestamp, gamepad);
		s_ReportedGamepadConnections[gamepad] = connected;
	}

	std::pair<float, float> Input::ConsumeScrollDelta()
	{
		return { s_FrameScrollX, s_FrameScrollY };
	}

	void Input::NotifyWindowFocus(bool focused, double timestamp)
	{
		if (s_LiveWindowFocused == focused)
			return;
		if (!focused)
		{
			s_EventQueue.ReleaseAll(timestamp);
			s_PendingScrollX = 0.0f;
			s_PendingScrollY = 0.0f;
		}
		s_LiveWindowFocused = focused;
	}

}
