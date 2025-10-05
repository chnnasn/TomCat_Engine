#pragma once

#include "TomCat/Core/Base.h"

namespace TomCat {

	class Input
	{
	public:
		static bool IsKeyPressed(KeyCode KeyCode);
		static bool IsMouseButtonPressed(MouseCode Button);
		static std::pair<float, float> GetMousePositon();
		static float GetMouseX();
		static float GetMouseY();
	};


}
