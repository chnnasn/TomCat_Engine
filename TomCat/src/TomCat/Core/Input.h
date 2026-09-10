#pragma once

#include "TomCat/Core/Base.h"

namespace TomCat {

	class Input
	{
	public:
		static bool IsKeyPressed(KeyCode KeyCode);
		static bool IsMouseButtonPressed(MouseCode Button);
		static std::pair<float, float> GetMousePosition();
		static float GetMouseX();
		static float GetMouseY();
	};


}
