#pragma once

#include <glm/glm.hpp>

namespace TomCat {

	struct Transform
	{
		glm::mat4 _Transform{ 1.0f };

		Transform() = default;
		Transform(const Transform&) = default;
		Transform(const glm::mat4& transform)
			: _Transform(transform) {
		}

		operator glm::mat4& () { return _Transform; }
		operator const glm::mat4& () const { return _Transform; }
	};

	struct SpriteRenderer
	{
		glm::vec4 Color{ 1.0f, 1.0f, 1.0f, 1.0f };

		SpriteRenderer() = default;
		SpriteRenderer(const SpriteRenderer&) = default;
		SpriteRenderer(const glm::vec4& color)
			: Color(color) {
		}
	};

}