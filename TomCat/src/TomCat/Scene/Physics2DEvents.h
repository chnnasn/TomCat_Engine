#pragma once

#include "TomCat/Core/UUID.h"

namespace TomCat {

	// Physics callbacks deliberately carry stable scene identities instead of
	// Entity handles or pointers. Consumers can resolve them against the scene at
	// dispatch time, and no object owned by entt or Box2D escapes its lifetime.
	struct CollisionEnter2D
	{
		// Canonical order (the smaller UUID is always EntityA). A single event is
		// emitted for an entity pair even when several fixtures touch.
		UUID EntityA{ 0 };
		UUID EntityB{ 0 };
	};

	struct CollisionExit2D
	{
		UUID EntityA{ 0 };
		UUID EntityB{ 0 };
	};

	struct TriggerEnter2D
	{
		UUID EntityA{ 0 };
		UUID EntityB{ 0 };
	};

	struct TriggerExit2D
	{
		UUID EntityA{ 0 };
		UUID EntityB{ 0 };
	};

}
