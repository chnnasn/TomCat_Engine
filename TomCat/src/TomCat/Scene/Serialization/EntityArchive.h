#pragma once

#include <cstdint>

namespace TomCat {

	// Prefab-local identity. Values are stable inside a .tcprefab document and
	// deliberately have no relationship to a Scene UUID.
	using EntityLocalID = uint64_t;

	struct EntityArchive
	{
		EntityLocalID LocalID = 0;
		EntityLocalID ParentLocalID = 0;
	};

}
