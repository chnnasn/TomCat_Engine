#include "tcpch.h"

#include "OrthographicCameraController.h"
#include "TomCat/Core/Input.h"

namespace TomCat {

	OrthographicCameraController::OrthographicCameraController(float aspectRatio, bool rotation)
		:m_AspectRatio(aspectRatio), m_Camera(-m_AspectRatio * m_ZoomLevel, m_AspectRatio* m_ZoomLevel, -m_ZoomLevel, m_ZoomLevel), m_Rotation(rotation)
	{

	}

	void OrthographicCameraController::OnUpdate(Timestep ts)
	{
		TC_PROFILE_FUNCTION();

		// 鼠标中键拖动控制
		if (Input::IsMouseButtonPressed(Mouse::ButtonMiddle))
		{
			auto [currentMouseX, currentMouseY] = Input::GetMousePositon();

			if (!m_IsDragging)
			{
				// 第一次按下中键，记录初始位置
				m_IsDragging = true;
				m_LastMouseX = currentMouseX;
				m_LastMouseY = currentMouseY;
			}
			else
			{
				// 计算鼠标移动增量
				float deltaX = currentMouseX - m_LastMouseX;
				float deltaY = currentMouseY - m_LastMouseY;

				// 根据移动增量更新摄像机位置
				// 注意：这里使用世界坐标，所以移动方向与鼠标方向相反
				m_CameraPosition.x -= deltaX * m_CameraTranslationSpeed * m_MouseDragSensitivity;
				m_CameraPosition.y += deltaY * m_CameraTranslationSpeed * m_MouseDragSensitivity;

				// 更新上一帧鼠标位置
				m_LastMouseX = currentMouseX;
				m_LastMouseY = currentMouseY;
			}
		}
		else
		{
			// 鼠标中键释放时重置拖动状态
			m_IsDragging = false;
		}

		if (m_Rotation)
		{
			if (Input::IsKeyPressed(Key::E))
				m_CameraRotation -= m_CameraRotationSpeed * ts;

			if (m_CameraRotation > 180.0f)
				m_CameraRotation -= 360.0f;
			else if (m_CameraRotation <= -180.0f)
				m_CameraRotation += 360.0f;

			m_Camera.SetRotation(m_CameraRotation);
		}

		m_Camera.SetPosition(m_CameraPosition);
		m_CameraTranslationSpeed = m_ZoomLevel;
	}

	void OrthographicCameraController::OnEvent(Event& e)
	{
		TC_PROFILE_FUNCTION();

		EventDispatcher dispatcher(e);
		dispatcher.Dispatch<MouseScrolledEvent>(TC_Bind_Event_Fn(OrthographicCameraController::OnMouseScrolled));
		dispatcher.Dispatch<WindowResizeEvent>(TC_Bind_Event_Fn(OrthographicCameraController::OnWindowResized));
	}

	void OrthographicCameraController::OnResize(float width, float height)
	{
		m_AspectRatio = width / height;
		m_Camera.SetProjection(-m_AspectRatio * m_ZoomLevel, m_AspectRatio * m_ZoomLevel, -m_ZoomLevel, m_ZoomLevel);
	}

	bool OrthographicCameraController::OnMouseScrolled(MouseScrolledEvent& e)
	{
		TC_PROFILE_FUNCTION();

		m_ZoomLevel -= e.GetYOffset() * 0.25f;
		m_ZoomLevel = std::max(m_ZoomLevel, 0.25f);
		m_Camera.SetProjection(-m_AspectRatio * m_ZoomLevel, m_AspectRatio * m_ZoomLevel, -m_ZoomLevel, m_ZoomLevel);
		return false;

	}

	bool OrthographicCameraController::OnWindowResized(WindowResizeEvent& e)
	{
		TC_PROFILE_FUNCTION();

		OnResize((float)e.GetWidth(), (float)e.GetHeight());
		return false;
	}

}