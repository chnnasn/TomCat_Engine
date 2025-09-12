#pragma once
#include "TomCat/Input.h"

namespace TomCat {

	class WindowsInput : public Input
	{
	public:

	protected:
		virtual bool IsKeyPressedImpl(int KeyCode) override;
		virtual bool IsMouseButtonPressedImpl(int Button) override;
		virtual std::pair<float, float> GetMousePositonImpl() override;
		virtual float GetMouseXImpl() override;
		virtual float GetMouseYImpl()  override;

	private:

	};

}
