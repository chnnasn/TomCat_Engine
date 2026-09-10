#include "tcpch.h"
#include "SceneCamera.h"

#include <glm/gtc/constants.hpp>
#include <glm/gtc/matrix_transform.hpp>

#include <cmath>
#include <limits>

namespace TomCat {

	SceneCamera::SceneCamera()
	{
		(void)RecalculateProjection();
	}

	bool SceneCamera::SetOrthographic(float size, float nearClip, float farClip)
	{
		if (!std::isfinite(size) || !std::isfinite(nearClip) || !std::isfinite(farClip)
			|| size <= 0.0f || farClip <= nearClip)
			return false;
		const ProjectionType previousType = m_ProjectionType;
		const float previousSize = m_OrthographicSize;
		const float previousNear = m_OrthographicNear;
		const float previousFar = m_OrthographicFar;
		m_ProjectionType = ProjectionType::Orthographic;
		m_OrthographicSize = size;
		m_OrthographicNear = nearClip;
		m_OrthographicFar = farClip;
		if (!RecalculateProjection())
		{
			m_ProjectionType = previousType;
			m_OrthographicSize = previousSize;
			m_OrthographicNear = previousNear;
			m_OrthographicFar = previousFar;
			return false;
		}
		return true;
	}

	bool SceneCamera::SetPerspective(float verticalFOV, float nearClip, float farClip)
	{
		if (!std::isfinite(verticalFOV) || !std::isfinite(nearClip) || !std::isfinite(farClip)
			|| verticalFOV <= 0.0f || verticalFOV >= glm::pi<float>()
			|| nearClip <= 0.0f || farClip <= nearClip)
			return false;
		const ProjectionType previousType = m_ProjectionType;
		const float previousFov = m_PerspectiveFOV;
		const float previousNear = m_PerspectiveNear;
		const float previousFar = m_PerspectiveFar;
		m_ProjectionType = ProjectionType::Perspective;
		m_PerspectiveFOV = verticalFOV;
		m_PerspectiveNear = nearClip;
		m_PerspectiveFar = farClip;
		if (!RecalculateProjection())
		{
			m_ProjectionType = previousType;
			m_PerspectiveFOV = previousFov;
			m_PerspectiveNear = previousNear;
			m_PerspectiveFar = previousFar;
			return false;
		}
		return true;
	}

	bool SceneCamera::SetViewportSize(uint32_t width, uint32_t height)
	{
		if (width == 0 || height == 0)
			return false;
		const float previousAspectRatio = m_AspectRatio;
		m_AspectRatio = (float)width / (float)height;
		glm::mat4 perspectiveProjection{};
		glm::mat4 orthographicProjection{};
		if (!TryCalculateProjection(ProjectionType::Perspective, perspectiveProjection)
			|| !TryCalculateProjection(ProjectionType::Orthographic, orthographicProjection))
		{
			m_AspectRatio = previousAspectRatio;
			return false;
		}
		m_Projection = m_ProjectionType == ProjectionType::Perspective
			? perspectiveProjection : orthographicProjection;
		return true;
	}

	bool SceneCamera::SetPerspectiveVerticalFOV(float verticalFOV)
	{
		if (!std::isfinite(verticalFOV) || verticalFOV <= 0.0f || verticalFOV >= glm::pi<float>())
			return false;
		const float previousValue = m_PerspectiveFOV;
		m_PerspectiveFOV = verticalFOV;
		glm::mat4 projection{};
		if (!TryCalculateProjection(ProjectionType::Perspective, projection))
		{
			m_PerspectiveFOV = previousValue;
			return false;
		}
		if (m_ProjectionType == ProjectionType::Perspective)
			m_Projection = projection;
		return true;
	}

	bool SceneCamera::SetPerspectiveNearClip(float nearClip)
	{
		if (!std::isfinite(nearClip) || nearClip <= 0.0f || nearClip >= m_PerspectiveFar)
			return false;
		const float previousValue = m_PerspectiveNear;
		m_PerspectiveNear = nearClip;
		glm::mat4 projection{};
		if (!TryCalculateProjection(ProjectionType::Perspective, projection))
		{
			m_PerspectiveNear = previousValue;
			return false;
		}
		if (m_ProjectionType == ProjectionType::Perspective)
			m_Projection = projection;
		return true;
	}

	bool SceneCamera::SetPerspectiveFarClip(float farClip)
	{
		if (!std::isfinite(farClip) || farClip <= m_PerspectiveNear)
			return false;
		const float previousValue = m_PerspectiveFar;
		m_PerspectiveFar = farClip;
		glm::mat4 projection{};
		if (!TryCalculateProjection(ProjectionType::Perspective, projection))
		{
			m_PerspectiveFar = previousValue;
			return false;
		}
		if (m_ProjectionType == ProjectionType::Perspective)
			m_Projection = projection;
		return true;
	}

	bool SceneCamera::SetOrthographicSize(float size)
	{
		if (!std::isfinite(size) || size <= 0.0f)
			return false;
		const float previousValue = m_OrthographicSize;
		m_OrthographicSize = size;
		glm::mat4 projection{};
		if (!TryCalculateProjection(ProjectionType::Orthographic, projection))
		{
			m_OrthographicSize = previousValue;
			return false;
		}
		if (m_ProjectionType == ProjectionType::Orthographic)
			m_Projection = projection;
		return true;
	}

	bool SceneCamera::SetOrthographicNearClip(float nearClip)
	{
		if (!std::isfinite(nearClip) || nearClip >= m_OrthographicFar)
			return false;
		const float previousValue = m_OrthographicNear;
		m_OrthographicNear = nearClip;
		glm::mat4 projection{};
		if (!TryCalculateProjection(ProjectionType::Orthographic, projection))
		{
			m_OrthographicNear = previousValue;
			return false;
		}
		if (m_ProjectionType == ProjectionType::Orthographic)
			m_Projection = projection;
		return true;
	}

	bool SceneCamera::SetOrthographicFarClip(float farClip)
	{
		if (!std::isfinite(farClip) || farClip <= m_OrthographicNear)
			return false;
		const float previousValue = m_OrthographicFar;
		m_OrthographicFar = farClip;
		glm::mat4 projection{};
		if (!TryCalculateProjection(ProjectionType::Orthographic, projection))
		{
			m_OrthographicFar = previousValue;
			return false;
		}
		if (m_ProjectionType == ProjectionType::Orthographic)
			m_Projection = projection;
		return true;
	}

	bool SceneCamera::SetProjectionType(ProjectionType type)
	{
		if (type != ProjectionType::Perspective && type != ProjectionType::Orthographic)
			return false;
		const ProjectionType previousType = m_ProjectionType;
		m_ProjectionType = type;
		if (!RecalculateProjection())
		{
			m_ProjectionType = previousType;
			return false;
		}
		return true;
	}

	bool SceneCamera::TryCalculateProjection(ProjectionType type, glm::mat4& projection) const
	{
		if (type == ProjectionType::Perspective)
		{
			// GLM asserts when aspect is exactly float epsilon, and divisions in
			// perspective() can otherwise produce a finite but singular matrix.
			const float tanHalfFov = std::tan(m_PerspectiveFOV * 0.5f);
			const float horizontalScaleDenominator = m_AspectRatio * tanHalfFov;
			const float depthRange = m_PerspectiveFar - m_PerspectiveNear;
			if (!std::isfinite(m_AspectRatio)
				|| m_AspectRatio <= std::numeric_limits<float>::epsilon()
				|| !std::isfinite(tanHalfFov) || tanHalfFov == 0.0f
				|| !std::isfinite(horizontalScaleDenominator) || horizontalScaleDenominator == 0.0f
				|| !std::isfinite(depthRange) || depthRange <= 0.0f)
				return false;

			projection = glm::perspective(m_PerspectiveFOV, m_AspectRatio, m_PerspectiveNear, m_PerspectiveFar);
		}
		else if (type == ProjectionType::Orthographic)
		{
			float orthoLeft = -m_OrthographicSize * m_AspectRatio * 0.5f;
			float orthoRight = m_OrthographicSize * m_AspectRatio * 0.5f;
			float orthoBottom = -m_OrthographicSize * 0.5f;
			float orthoTop = m_OrthographicSize * 0.5f;
			const float horizontalRange = orthoRight - orthoLeft;
			const float verticalRange = orthoTop - orthoBottom;
			const float depthRange = m_OrthographicFar - m_OrthographicNear;
			if (!std::isfinite(orthoLeft) || !std::isfinite(orthoRight)
				|| !std::isfinite(orthoBottom) || !std::isfinite(orthoTop)
				|| !std::isfinite(horizontalRange) || horizontalRange <= 0.0f
				|| !std::isfinite(verticalRange) || verticalRange <= 0.0f
				|| !std::isfinite(depthRange) || depthRange <= 0.0f)
				return false;

			projection = glm::ortho(orthoLeft, orthoRight,
				orthoBottom, orthoTop, m_OrthographicNear, m_OrthographicFar);
		}
		else
		{
			return false;
		}

		for (glm::length_t column = 0; column < 4; ++column)
		{
			for (glm::length_t row = 0; row < 4; ++row)
			{
				if (!std::isfinite(projection[column][row]))
					return false;
			}
		}

		// A zero scale term collapses one clip-space axis while still passing the
		// finite-number check above (for example, after a floating-point range
		// subtraction overflows). Such a projection cannot be inverted or rendered
		// meaningfully, so treat it as a failed calculation.
		if (projection[0][0] == 0.0f || projection[1][1] == 0.0f
			|| (type == ProjectionType::Perspective && projection[3][2] == 0.0f)
			|| (type == ProjectionType::Orthographic && projection[2][2] == 0.0f))
			return false;
		return true;
	}

	bool SceneCamera::RecalculateProjection()
	{
		glm::mat4 projection{};
		if (!TryCalculateProjection(m_ProjectionType, projection))
			return false;
		m_Projection = projection;
		return true;
	}

}
