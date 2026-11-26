#pragma once

#include <filesystem>

namespace TomCat::CLI {

	struct PakInspectOptions
	{
		std::filesystem::path PackagePath;
		// When set, every texture entry is decoded and written as <handle>.png.
		// Sprite slices are cropped to their rect.
		std::filesystem::path DumpTexturesDirectory;
		// When set, every scene/prefab entry's cooked YAML is written as-is.
		std::filesystem::path DumpScenesDirectory;
	};

	// Mounts the package read-only, prints its manifest, entry table, scene
	// entities and texture/sprite descriptors, then unmounts. Returns a process
	// exit code: 0 on success, 4 when the package fails validation.
	int InspectPackage(const PakInspectOptions& options);

}
