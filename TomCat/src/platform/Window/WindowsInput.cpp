#include "tcpch.h"
#include "WindowsInput.h"
#include <glfw/glfw3.h>
#include "TomCat/Application.h"

namespace TomCat {

	Input* Input::s_Instance = new WindowsInput();

	bool WindowsInput::IsKeyPressedImpl(int KeyCode)
	{

		auto Window = static_cast<GLFWwindow*>(Application::Get().GetWindow().GetNativeWindow());

		auto state = glfwGetKey(Window, KeyCode);

		return state == GLFW_PRESS || state == GLFW_REPEAT;

	}

	bool WindowsInput::IsMouseButtonPressedImpl(int Button)
	{

		auto Window = static_cast<GLFWwindow*>(Application::Get().GetWindow().GetNativeWindow());

		auto state = glfwGetMouseButton(Window, Button);

		return state == GLFW_PRESS;
	}

	std::pair<float, float> WindowsInput::GetMousePositonImpl()
	{
		auto Window = static_cast<GLFWwindow*>(Application::Get().GetWindow().GetNativeWindow());

		double Xpos, Ypos;

		glfwGetCursorPos(Window, &Xpos, &Ypos);

		return { (float)Xpos ,(float)Ypos };
	}

	float WindowsInput::GetMouseXImpl()
	{
		auto [x,y] = GetMousePositonImpl();

		return x;
	}

	float WindowsInput::GetMouseYImpl()
	{
		auto [x, y] = GetMousePositonImpl();

		return y;
	}
	
}