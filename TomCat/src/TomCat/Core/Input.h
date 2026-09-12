#pragma once

#include "TomCat/Core/Base.h"

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

		// Window callbacks accumulate transient state until the scripting snapshot
		// consumes it at the beginning of the next display frame.
		static void NotifyScroll(float xOffset, float yOffset);
		static std::pair<float, float> ConsumeScrollDelta();
		static void NotifyWindowFocus(bool focused);
	};


}
