#include "tcpch.h"
#include "Math.h"

#define GLM_ENABLE_EXPERIMENTAL
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtx/quaternion.hpp>
#include <glm/gtx/matrix_decompose.hpp>

#include <algorithm>
#include <cmath>

namespace TomCat::Math {

	glm::mat4 ComposeTransform(const glm::vec3& translation, const glm::vec3& rotation, const glm::vec3& scale)
	{
		return glm::translate(glm::mat4(1.0f), translation)
			* glm::toMat4(glm::quat(rotation))
			* glm::scale(glm::mat4(1.0f), scale);
	}

	bool DecomposeTransform(const glm::mat4& transform, glm::vec3& translation, glm::vec3& rotation, glm::vec3& scale)
	{
		for (glm::length_t column = 0; column < 4; ++column)
		{
			for (glm::length_t row = 0; row < 4; ++row)
			{
				if (!std::isfinite(transform[column][row]))
					return false;
			}
		}

		glm::vec3 newTranslation{};
		glm::vec3 newScale{};
		glm::quat orientation{};
		glm::vec3 skew{};
		glm::vec4 perspective{};
		if (!glm::decompose(transform, newScale, orientation, newTranslation, skew, perspective))
			return false;

		const auto finite3 = [](const glm::vec3& value)
		{
			return std::isfinite(value.x) && std::isfinite(value.y) && std::isfinite(value.z);
		};
		const auto finite4 = [](const glm::vec4& value)
		{
			return std::isfinite(value.x) && std::isfinite(value.y)
				&& std::isfinite(value.z) && std::isfinite(value.w);
		};
		if (!finite3(newTranslation) || !finite3(newScale) || !finite3(skew)
			|| !finite4(perspective)
			|| !std::isfinite(orientation.x) || !std::isfinite(orientation.y)
			|| !std::isfinite(orientation.z) || !std::isfinite(orientation.w))
			return false;

		// Transform stores TRS only. Silently discarding perspective or shear would
		// make reparenting and gizmo operations drift, so reject matrices that this
		// component cannot represent exactly enough.
		constexpr float tolerance = 1.0e-4f;
		if (glm::length(skew) > tolerance
			|| std::abs(perspective.x) > tolerance
			|| std::abs(perspective.y) > tolerance
			|| std::abs(perspective.z) > tolerance
			|| std::abs(perspective.w - 1.0f) > tolerance)
			return false;

		const float orientationLength = glm::length(orientation);
		if (!std::isfinite(orientationLength) || orientationLength <= tolerance)
			return false;
		const glm::vec3 newRotation = glm::eulerAngles(glm::normalize(orientation));
		if (!finite3(newRotation))
			return false;

		const glm::mat4 reconstructed = ComposeTransform(newTranslation, newRotation, newScale);
		float largestValue = 1.0f;
		float largestError = 0.0f;
		for (glm::length_t column = 0; column < 4; ++column)
		{
			for (glm::length_t row = 0; row < 4; ++row)
			{
				if (!std::isfinite(reconstructed[column][row]))
					return false;
				largestValue = std::max(largestValue, std::abs(transform[column][row]));
				largestError = std::max(largestError,
					std::abs(reconstructed[column][row] - transform[column][row]));
			}
		}
		if (!std::isfinite(largestError) || largestError > tolerance * largestValue)
			return false;

		translation = newTranslation;
		rotation = newRotation;
		scale = newScale;
		return true;
	}

}
