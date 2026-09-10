#include "tcpch.h"
#include "TomCat/Core/Input.h"
#include <glfw/glfw3.h>
#include "TomCat/Core/Application.h"

namespace TomCat {

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
	
}
