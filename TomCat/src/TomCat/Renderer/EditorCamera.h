#pragma once

#include "Camera.h"
#include "TomCat/Core/Timestep.h"
#include "TomCat/Events/Event.h"
#include "TomCat/Events/MouseEvent.h"

#include <cstdint>
#include <glm/glm.hpp>

namespace TomCat {

	class EditorCamera : public Camera
	{
	public:
		enum class AxisView : uint8_t
		{
			PositiveX = 0,
			NegativeX,
			PositiveY,
			NegativeY,
			PositiveZ,
			NegativeZ
		};

		EditorCamera() = default;
		EditorCamera(float fov, float aspectRatio, float nearClip, float farClip);

		void OnUpdate(Timestep ts, bool inputEnabled = true);
		void OnEvent(Event& e);
		// Centers the Scene camera on a world-space bounding sphere and chooses a
		// distance that keeps the whole sphere inside the current viewport.
		void FrameBounds(const glm::vec3& center, float radius);
		void FrameBounds(const glm::vec3& minimum, const glm::vec3& maximum);
		void Set2DMode(bool enabled);
		bool Is2DMode() const { return m_Is2DMode; }
		// TomCat's world basis follows the authoring convention used by scene
		// objects: +X right, +Y up, +Z forward. Axis views place the editor camera
		// on the selected side of the focal point and look back toward it.
		static glm::vec3 GetWorldAxis(AxisView view);
		void SnapToAxis(AxisView view);
		void SetOrthographic(bool enabled);
		bool IsOrthographic() const { return m_IsOrthographic; }

		inline float GetDistance() const { return m_Distance; }
		void SetDistance(float distance);
		// Overlays use an infinite far plane so authored camera bounds are never
		// clipped by the Scene camera's finite working range.
		glm::mat4 GetInfiniteFarViewProjection() const;

		inline void SetViewportSize(float width, float height) { m_ViewportWidth = width; m_ViewportHeight = height; UpdateProjection(); }

		const glm::mat4& GetViewMatrix() const { return m_ViewMatrix; }
		glm::mat4 GetViewProjection() const { return m_Projection * m_ViewMatrix; }
		// Produces an equivalent P/V pair for editor tools that assume a
		// right-handed camera. The product remains identical to GetViewProjection.
		void GetRightHandedToolMatrices(glm::mat4& view,
			glm::mat4& projection) const;

		glm::vec3 GetUpDirection() const;
		glm::vec3 GetRightDirection() const;
		// The Scene camera shares the Unity-style authoring basis: local +Z is
		// forward. Its projection is explicitly left-handed.
		glm::vec3 GetForwardDirection() const;
		const glm::vec3& GetPosition() const { return m_Position; }
		const glm::vec3& GetFocalPoint() const { return m_FocalPoint; }
		glm::quat GetOrientation() const;

		float GetPitch() const { return m_Pitch; }
		float GetYaw() const { return m_Yaw; }
	private:
		void UpdateProjection();
		void UpdateView();

		bool OnMouseScroll(MouseScrolledEvent& e);

		void MousePan(const glm::vec2& delta);
		void MouseRotate(const glm::vec2& delta);
		void MouseZoom(float delta);

		glm::vec3 CalculatePosition() const;

		std::pair<float, float> PanSpeed() const;
		float RotationSpeed() const;
		float ZoomSpeed() const;
	private:
		float m_FOV = 45.0f, m_AspectRatio = 1.778f, m_NearClip = 0.1f, m_FarClip = 1000.0f;

		glm::mat4 m_ViewMatrix;
		glm::vec3 m_Position = { 0.0f, 0.0f, 0.0f };
		glm::vec3 m_FocalPoint = { 0.0f, 0.0f, 0.0f };

		glm::vec2 m_InitialMousePosition = { 0.0f, 0.0f };

		float m_Distance = 10.0f;
		float m_Pitch = 0.0f, m_Yaw = 0.0f;

		float m_ViewportWidth = 1280, m_ViewportHeight = 720;
		bool m_Is2DMode = false;
		bool m_IsOrthographic = false;
		bool m_3DOrthographic = false;
	};

}
