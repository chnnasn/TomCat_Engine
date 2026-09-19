#include "tcpch.h"
#include "EditorCamera.h"

#include "TomCat/Core/Input.h"
#include "TomCat/Core/KeyCodes.h"
#include "TomCat/Core/MouseCodes.h"

#include <glfw/glfw3.h>

#define GLM_ENABLE_EXPERIMENTAL
#include <glm/gtx/quaternion.hpp>

namespace TomCat {

	EditorCamera::EditorCamera(float fov, float aspectRatio, float nearClip, float farClip)
		: m_FOV(fov), m_AspectRatio(aspectRatio), m_NearClip(nearClip), m_FarClip(farClip), Camera(glm::perspective(glm::radians(fov), aspectRatio, nearClip, farClip))
	{
		UpdateView();
	}

	void EditorCamera::UpdateProjection()
	{
		m_AspectRatio = m_ViewportWidth / m_ViewportHeight;
		m_Projection = glm::perspective(glm::radians(m_FOV), m_AspectRatio, m_NearClip, m_FarClip);
	}

	void EditorCamera::UpdateView()
	{
		// m_Yaw = m_Pitch = 0.0f; // Lock the camera's rotation
		m_Position = CalculatePosition();

		glm::quat orientation = GetOrientation();
		m_ViewMatrix = glm::translate(glm::mat4(1.0f), m_Position) * glm::toMat4(orientation);
		m_ViewMatrix = glm::inverse(m_ViewMatrix);
	}

	std::pair<float, float> EditorCamera::PanSpeed() const
	{
		float x = std::min(m_ViewportWidth / 1000.0f, 2.4f); // max = 2.4f
		float xFactor = 0.0366f * (x * x) - 0.1778f * x + 0.3021f;

		float y = std::min(m_ViewportHeight / 1000.0f, 2.4f); // max = 2.4f
		float yFactor = 0.0366f * (y * y) - 0.1778f * y + 0.3021f;

		// 增加速度倍数
		float speedMultiplier = 5.0f; // 调整为需要的倍数
		return { xFactor * speedMultiplier, yFactor * speedMultiplier };
	}

	float EditorCamera::RotationSpeed() const
	{
		return 0.8f;
	}

	float EditorCamera::ZoomSpeed() const
	{
		// Keep each wheel step proportional to the current distance.  The old
		// quadratic curve was capped at 100, which made a camera framed around a
		// large object take hundreds of wheel steps to approach, while its speed
		// collapsed almost to zero near small objects.
		return std::max(m_Distance * 0.5f, 0.5f);
	}

	void EditorCamera::OnUpdate(Timestep ts, bool inputEnabled)
	{
		const glm::vec2& mouse{ Input::GetMouseX(), Input::GetMouseY() };
		glm::vec2 delta = (mouse - m_InitialMousePosition) * 0.005f;
		m_InitialMousePosition = mouse;

		if (inputEnabled)
		{
			if (Input::IsMouseButtonPressed(Mouse::ButtonMiddle))
				MousePan(delta);
			else if (Input::IsMouseButtonPressed(Mouse::ButtonRight))
			{
				// In 2D projects the right mouse button is an alternate pan button;
				// camera rotation is reserved for 3D projects.
				if (m_Is2DMode)
					MousePan(delta);
				else
					MouseRotate(delta);
			}
			else if (Input::IsKeyPressed(Key::LeftAlt) && Input::IsMouseButtonPressed(Mouse::ButtonLeft))
				MouseZoom(delta.y);
		}

		UpdateView();
	}

	void EditorCamera::OnEvent(Event& e)
	{
		EventDispatcher dispatcher(e);
		dispatcher.Dispatch<MouseScrolledEvent>(TC_Bind_Event_Fn(EditorCamera::OnMouseScroll));
	}

	void EditorCamera::Set2DMode(bool enabled)
	{
		if (m_Is2DMode == enabled)
			return;
		m_Is2DMode = enabled;
		if (enabled)
		{
			// A 2D Scene view always faces the XY plane. Reset an angle inherited
			// from a 3D project once when entering 2D; panning and zooming remain.
			m_Pitch = 0.0f;
			m_Yaw = 0.0f;
			UpdateView();
		}
	}

	void EditorCamera::FrameBounds(const glm::vec3& center, float radius)
	{
		if (!std::isfinite(center.x) || !std::isfinite(center.y)
			|| !std::isfinite(center.z) || !std::isfinite(radius))
			return;
		const float safeRadius = std::max(std::abs(radius), 0.25f);
		FrameBounds(center - glm::vec3(safeRadius),
			center + glm::vec3(safeRadius));
	}

	void EditorCamera::FrameBounds(const glm::vec3& minimum,
		const glm::vec3& maximum)
	{
		if (!std::isfinite(minimum.x) || !std::isfinite(minimum.y)
			|| !std::isfinite(minimum.z) || !std::isfinite(maximum.x)
			|| !std::isfinite(maximum.y) || !std::isfinite(maximum.z))
			return;
		const glm::vec3 lower = glm::min(minimum, maximum);
		const glm::vec3 upper = glm::max(minimum, maximum);
		m_FocalPoint = (lower + upper) * 0.5f;
		const glm::vec3 halfExtents = glm::max((upper - lower) * 0.5f,
			glm::vec3(0.25f));
		float halfWidth = 0.0f;
		float halfHeight = 0.0f;
		float halfDepth = 0.0f;
		const glm::vec3 right = GetRightDirection();
		const glm::vec3 up = GetUpDirection();
		const glm::vec3 forward = GetForwardDirection();
		for (int x = -1; x <= 1; x += 2)
		{
			for (int y = -1; y <= 1; y += 2)
			{
				for (int z = -1; z <= 1; z += 2)
				{
					const glm::vec3 offset = halfExtents * glm::vec3(
						static_cast<float>(x), static_cast<float>(y),
						static_cast<float>(z));
					halfWidth = std::max(halfWidth, std::abs(glm::dot(offset, right)));
					halfHeight = std::max(halfHeight, std::abs(glm::dot(offset, up)));
					halfDepth = std::max(halfDepth, std::abs(glm::dot(offset, forward)));
				}
			}
		}

		const float verticalHalfFov = glm::radians(m_FOV) * 0.5f;
		const float tangent = std::max(std::tan(verticalHalfFov), 0.01f);
		const float fitDistance = std::max(halfHeight / tangent,
			halfWidth / (tangent * std::max(m_AspectRatio, 0.01f)));
		// A small margin matches the Scene view's framing behavior and leaves the
		// selection outline visible instead of touching the viewport edges.
		m_Distance = std::max({ 1.0f, halfDepth + fitDistance * 1.15f,
			halfDepth + m_NearClip + 0.01f });
		const float requiredFarClip = m_Distance + halfDepth + 1.0f;
		if (requiredFarClip > m_FarClip)
		{
			m_FarClip = requiredFarClip * 1.1f;
			UpdateProjection();
		}
		UpdateView();
	}

	bool EditorCamera::OnMouseScroll(MouseScrolledEvent& e)
	{
		float delta = e.GetYOffset() * 0.1f;
		MouseZoom(delta);
		UpdateView();
		return false;
	}

	void EditorCamera::MousePan(const glm::vec2& delta)
	{
		auto [xSpeed, ySpeed] = PanSpeed();
		m_FocalPoint += -GetRightDirection() * delta.x * xSpeed * m_Distance;
		m_FocalPoint += GetUpDirection() * delta.y * ySpeed * m_Distance;
	}

	void EditorCamera::MouseRotate(const glm::vec2& delta)
	{
		float yawSign = GetUpDirection().y < 0 ? -1.0f : 1.0f;
		m_Yaw += yawSign * delta.x * RotationSpeed();
		m_Pitch += delta.y * RotationSpeed();
	}

	void EditorCamera::MouseZoom(float delta)
	{
		if (!std::isfinite(delta) || delta == 0.0f)
			return;

		m_Distance -= delta * ZoomSpeed();
		const float minimumDistance = std::max(m_NearClip * 1.1f, 0.01f);
		if (m_Distance < minimumDistance)
		{
			// Preserve the remaining dolly movement instead of getting stuck at the
			// orbit distance floor.  This also lets the user move through a focus
			// point without a discontinuous one-unit jump.
			m_FocalPoint += GetForwardDirection()
				* (minimumDistance - m_Distance);
			m_Distance = minimumDistance;
		}
	}

	glm::vec3 EditorCamera::GetUpDirection() const
	{
		return glm::rotate(GetOrientation(), glm::vec3(0.0f, 1.0f, 0.0f));
	}

	glm::vec3 EditorCamera::GetRightDirection() const
	{
		return glm::rotate(GetOrientation(), glm::vec3(1.0f, 0.0f, 0.0f));
	}

	glm::vec3 EditorCamera::GetForwardDirection() const
	{
		return glm::rotate(GetOrientation(), glm::vec3(0.0f, 0.0f, -1.0f));
	}

	glm::vec3 EditorCamera::CalculatePosition() const
	{
		return m_FocalPoint - GetForwardDirection() * m_Distance;
	}

	glm::quat EditorCamera::GetOrientation() const
	{
		return glm::quat(glm::vec3(-m_Pitch, -m_Yaw, 0.0f));
	}

}
