#pragma once

#include "Camera.h"
#include "TomCat/Core/Timestep.h"
#include "TomCat/Events/Event.h"
#include "TomCat/Events/MouseEvent.h"

#include <glm/glm.hpp>

namespace TomCat {

	class EditorCamera : public Camera
	{
	public:
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

		inline float GetDistance() const { return m_Distance; }
		inline void SetDistance(float distance) { m_Distance = distance; }
		// Overlays use an infinite far plane so authored camera bounds are never
		// clipped by the Scene camera's finite working range.
		glm::mat4 GetInfiniteFarViewProjection() const;

		inline void SetViewportSize(float width, float height) { m_ViewportWidth = width; m_ViewportHeight = height; UpdateProjection(); }

		const glm::mat4& GetViewMatrix() const { return m_ViewMatrix; }
		glm::mat4 GetViewProjection() const { return m_Projection * m_ViewMatrix; }

		glm::vec3 GetUpDirection() const;
		glm::vec3 GetRightDirection() const;
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
	};

}
