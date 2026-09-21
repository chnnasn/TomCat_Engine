#include "tcpch.h"
#include "EditorCamera.h"

#include "TomCat/Core/Input.h"
#include "TomCat/Core/KeyCodes.h"
#include "TomCat/Core/MouseCodes.h"

#include <glfw/glfw3.h>
#include <glm/ext/matrix_clip_space.hpp>
#include <glm/gtc/constants.hpp>

#define GLM_ENABLE_EXPERIMENTAL
#include <glm/gtx/quaternion.hpp>

namespace TomCat {

	EditorCamera::EditorCamera(float fov, float aspectRatio, float nearClip, float farClip)
		: m_FOV(fov), m_AspectRatio(aspectRatio), m_NearClip(nearClip),
		  m_FarClip(farClip), Camera(glm::perspectiveLH_NO(glm::radians(fov),
			  aspectRatio, nearClip, farClip))
	{
		// Preserve the requested aspect until the Scene panel publishes its real
		// viewport size. UpdateProjection derives aspect from these dimensions.
		m_ViewportHeight = 720.0f;
		m_ViewportWidth = std::max(aspectRatio, 0.01f) * m_ViewportHeight;
		UpdateView();
	}

	void EditorCamera::UpdateProjection()
	{
		m_AspectRatio = std::max(m_ViewportWidth, 1.0f)
			/ std::max(m_ViewportHeight, 1.0f);
		const float effectiveFarClip = std::max(m_FarClip,
			m_Distance * 2.0f + m_NearClip);
		if (m_IsOrthographic)
		{
			// Match the perspective scale at the focal plane. Toggling projection
			// therefore preserves the apparent size and the orbit focus.
			const float halfHeight = std::max(m_Distance
				* std::tan(glm::radians(m_FOV) * 0.5f), 0.001f);
			const float halfWidth = halfHeight * m_AspectRatio;
			m_Projection = glm::orthoLH_NO(-halfWidth, halfWidth,
				-halfHeight, halfHeight, m_NearClip, effectiveFarClip);
		}
		else
		{
			m_Projection = glm::perspectiveLH_NO(glm::radians(m_FOV),
				m_AspectRatio, m_NearClip, effectiveFarClip);
		}
	}

	void EditorCamera::UpdateView()
	{
		// m_Yaw = m_Pitch = 0.0f; // Lock the camera's rotation
		m_Position = CalculatePosition();

		glm::quat orientation = GetOrientation();
		m_ViewMatrix = glm::translate(glm::mat4(1.0f), m_Position) * glm::toMat4(orientation);
		m_ViewMatrix = glm::inverse(m_ViewMatrix);
		// Projection depth and orthographic scale both depend on orbit distance.
		// Refresh them after panning/zooming so a distant Scene view cannot be
		// clipped by a projection that was built for an earlier camera position.
		UpdateProjection();
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
			// A 2D Scene view faces XY with parallel projection, so identical XY
			// positions overlap regardless of depth (including near/far outlines).
			m_3DOrthographic = m_IsOrthographic;
			m_3DViewAngles = { m_Pitch, m_Yaw };
			m_IsOrthographic = true;
			m_Pitch = 0.0f;
			m_Yaw = 0.0f;
		}
		else
		{
			m_IsOrthographic = m_3DOrthographic;
			m_Pitch = m_3DViewAngles.x;
			m_Yaw = m_3DViewAngles.y;
		}
		UpdateView();
	}

	glm::vec3 EditorCamera::GetWorldAxis(AxisView view)
	{
		switch (view)
		{
			case AxisView::PositiveX: return { 1.0f, 0.0f, 0.0f };
			case AxisView::NegativeX: return { -1.0f, 0.0f, 0.0f };
			case AxisView::PositiveY: return { 0.0f, 1.0f, 0.0f };
			case AxisView::NegativeY: return { 0.0f, -1.0f, 0.0f };
			case AxisView::PositiveZ: return { 0.0f, 0.0f, 1.0f };
			case AxisView::NegativeZ: return { 0.0f, 0.0f, -1.0f };
			default: return { 0.0f, 0.0f, 1.0f };
		}
	}

	void EditorCamera::SnapToAxis(AxisView view)
	{
		if (m_Is2DMode)
			return;
		constexpr float halfPi = glm::pi<float>() * 0.5f;
		switch (view)
		{
			case AxisView::PositiveX:
				m_Pitch = 0.0f; m_Yaw = -halfPi; break;
			case AxisView::NegativeX:
				m_Pitch = 0.0f; m_Yaw = halfPi; break;
			case AxisView::PositiveY:
				m_Pitch = halfPi; m_Yaw = 0.0f; break;
			case AxisView::NegativeY:
				m_Pitch = -halfPi; m_Yaw = 0.0f; break;
			case AxisView::PositiveZ:
				m_Pitch = 0.0f; m_Yaw = glm::pi<float>(); break;
			case AxisView::NegativeZ:
				m_Pitch = 0.0f; m_Yaw = 0.0f; break;
		}
		m_IsOrthographic = true;
		UpdateProjection();
		UpdateView();
	}

	void EditorCamera::SetOrthographic(bool enabled)
	{
		if (m_Is2DMode)
			enabled = true;
		if (m_IsOrthographic == enabled)
			return;
		m_IsOrthographic = enabled;
		UpdateProjection();
	}

	void EditorCamera::SetDistance(float distance)
	{
		if (!std::isfinite(distance))
			return;
		m_Distance = std::max(distance, std::max(m_NearClip * 1.1f, 0.01f));
		UpdateView();
	}

	glm::mat4 EditorCamera::GetInfiniteFarViewProjection() const
	{
		if (m_IsOrthographic)
		{
			// Orthographic projection has no infinite-far form. Camera outlines are
			// rendered with depth testing disabled, so keep the authored X/Y screen
			// mapping and force clip-space depth to the center of the visible range.
			// This lets very large authored Far values remain visible in Scene view.
			glm::mat4 viewProjection = m_Projection * m_ViewMatrix;
			viewProjection[0][2] = 0.0f;
			viewProjection[1][2] = 0.0f;
			viewProjection[2][2] = 0.0f;
			viewProjection[3][2] = 0.0f;
			return viewProjection;
		}
		return glm::infinitePerspectiveLH_NO(glm::radians(m_FOV),
			m_AspectRatio, m_NearClip) * m_ViewMatrix;
	}

	void EditorCamera::GetRightHandedToolMatrices(glm::mat4& view,
		glm::mat4& projection) const
	{
		// ImGuizmo and similar tools interpret inverse(view)[2] as the camera's
		// backward vector. Reflect view-space Z on both sides of P*V so
		// (P*S)*(S*V) == P*V while presenting the convention those tools expect.
		glm::mat4 reflectViewZ(1.0f);
		reflectViewZ[2][2] = -1.0f;
		view = reflectViewZ * m_ViewMatrix;
		projection = m_Projection * reflectViewZ;
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
		return glm::rotate(GetOrientation(), glm::vec3(0.0f, 0.0f, 1.0f));
	}

	glm::vec3 EditorCamera::CalculatePosition() const
	{
		return m_FocalPoint - GetForwardDirection() * m_Distance;
	}

	glm::quat EditorCamera::GetOrientation() const
	{
		// The authoring basis is +X right, +Y up, +Z forward. Applying editor
		// angles directly makes positive yaw turn +Z toward +X and positive pitch
		// tilt +Z toward -Y, matching Unity-style Scene look/orbit controls.
		return glm::quat(glm::vec3(m_Pitch, m_Yaw, 0.0f));
	}

}
