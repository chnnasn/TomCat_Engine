#pragma once

#include "TomCat/Renderer/Camera.h"

namespace TomCat {

	class SceneCamera : public Camera
	{

	public:
		enum class ProjectionType { Perspective = 0, Orthographic = 1 };

	public:
		SceneCamera();
		virtual ~SceneCamera() = default;

		bool SetOrthographic(float size, float nearClip, float farClip);
		bool SetPerspective(float verticalFOV, float nearClip, float farClip);

		bool SetViewportSize(uint32_t width, uint32_t height);

		float GetPerspectiveVerticalFOV() const { return m_PerspectiveFOV; }
		bool SetPerspectiveVerticalFOV(float verticalFov);
		float GetPerspectiveNearClip() const { return m_PerspectiveNear; }
		bool SetPerspectiveNearClip(float nearClip);
		float GetPerspectiveFarClip() const { return m_PerspectiveFar; }
		bool SetPerspectiveFarClip(float farClip);

		float GetOrthographicSize() const { return m_OrthographicSize; }
		bool SetOrthographicSize(float size);
		float GetOrthographicNearClip() const { return m_OrthographicNear; }
		bool SetOrthographicNearClip(float nearClip);
		float GetOrthographicFarClip() const { return m_OrthographicFar; }
		bool SetOrthographicFarClip(float farClip);

		ProjectionType GetProjectionType() const { return m_ProjectionType; }
		bool SetProjectionType(ProjectionType type);

	private:
		bool TryCalculateProjection(ProjectionType type, glm::mat4& projection) const;
		bool RecalculateProjection();
	private:
	ProjectionType m_ProjectionType = ProjectionType::Orthographic;

		float m_PerspectiveFOV = glm::radians(45.0f);
		float m_PerspectiveNear = 0.01f, m_PerspectiveFar = 1000.0f;

		float m_OrthographicSize = 10.0f;
		float m_OrthographicNear = -1.0f, m_OrthographicFar = 1.0f;

		float m_AspectRatio = 1.0f;
	};

}
