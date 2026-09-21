#pragma once

#include "TomCat/Core/Base.h"
#include "TomCat/Core/InputEventQueue.h"

#include <array>
#include <string>

namespace TomCat {

	class Input
	{
	public:
		static constexpr uint32_t MaximumGamepads = 16;
		static constexpr uint32_t GamepadButtonCount = 15;
		static constexpr uint32_t GamepadAxisCount = 6;

		struct GamepadSnapshot
		{
			bool Connected = false;
			std::array<bool, GamepadButtonCount> Buttons{};
			std::array<float, GamepadAxisCount> Axes{};
			std::string Name;
		};

		static bool IsKeyPressed(KeyCode KeyCode);
		static bool IsMouseButtonPressed(MouseCode Button);
		static std::pair<float, float> GetMousePosition();
		static float GetMouseX();
		static float GetMouseY();
		static bool IsWindowFocused();
		static GamepadSnapshot GetGamepadSnapshot(uint32_t index);

		// Application calls BeginFrame immediately after the display-frame event
		// poll. The returned snapshot is immutable until the next BeginFrame. GLFW
		// exposes gamepad buttons as sampled state (not callbacks), so their event
		// boundary is one BeginFrame sample.
		static void BeginFrame();
		static const InputEventQueue::FrameSnapshot& GetFrameSnapshot();
		static void ClearState();

		// Platform callbacks append ordered transitions while PollEvents runs.
		static void NotifyKey(uint32_t key, InputEventQueue::Action action,
			double timestamp);
		static void NotifyMouseButton(uint32_t button,
			InputEventQueue::Action action, double timestamp);
		static void NotifyMousePosition(float x, float y);
		static void NotifyScroll(float xOffset, float yOffset);
		// Committed Unicode from the platform text/IME callback, frozen per display
		// frame. Key codes must never be used to synthesize user text.
		static void NotifyCharacter(uint32_t codepoint);
		static const std::string& GetTextInput();
		static std::string GetClipboardText();
		static bool SetClipboardText(const std::string& text);
		// GLFW reports joystick hot-plug as ordered callbacks during PollEvents.
		// Keep those edges in the same frame queue as keyboard and mouse input so
		// a connect+disconnect pair between frames is still observable.
		static void NotifyGamepadConnection(uint32_t gamepad, bool connected,
			double timestamp);
		// Kept for source compatibility. The display-frame scroll snapshot is
		// repeatable and is cleared only by the next BeginFrame.
		static std::pair<float, float> ConsumeScrollDelta();
		static void NotifyWindowFocus(bool focused, double timestamp);
	};


}
