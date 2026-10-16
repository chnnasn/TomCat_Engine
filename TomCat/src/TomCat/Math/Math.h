#pragma once

#include <glm/glm.hpp>

namespace TomCat::Math {

	glm::mat4 ComposeTransform(const glm::vec3& translation, const glm::vec3& rotation, const glm::vec3& scale);
	// Returns false for non-finite, singular, perspective, or sheared matrices.
	// Output values are committed only on success and remain unchanged on failure.
	bool DecomposeTransform(const glm::mat4& transform, glm::vec3& outTranslation, glm::vec3& outRotation, glm::vec3& outScale);
	// For a derived TRS whose scale signs are known from its source components.
	// Does not change the canonical matrix-only decomposition contract above.
	bool DecomposeTransformWithScaleSigns(const glm::mat4& transform, const glm::vec3& scaleSigns,
		glm::vec3& outTranslation, glm::vec3& outRotation, glm::vec3& outScale);

}
