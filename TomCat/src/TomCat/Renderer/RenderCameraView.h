#pragma once

#include <glm/glm.hpp>

namespace TomCat {

	// What a renderer needs from a camera that is not a scene entity, such as
	// the Editor's Scene view camera. Keeps the renderers and Scene independent
	// of editor camera types.
	struct RenderCameraView
	{
		glm::mat4 ViewProjection{ 1.0f };
		glm::vec3 Position{ 0.0f };
	};

}
