#pragma once

#include "TomCat/Asset/TextureArtifact.h"
#include "TomCat/Renderer/Texture.h"

#include <fstream>

namespace TomCat {

	// Editor-owned previews use an uncompressed mip chain. They never change the
	// source artwork, its import settings, or the texture used by the game.
	inline Ref<Texture2D> LoadEditorPreview(const std::filesystem::path& path)
	{
		std::ifstream input(path, std::ios::binary | std::ios::ate);
		if (!input)
			return {};
		const auto size = input.tellg();
		if (size <= 0 || size > 64 * 1024 * 1024)
			return {};
		std::vector<uint8_t> encoded(static_cast<size_t>(size));
		input.seekg(0);
		if (!input.read(reinterpret_cast<char*>(encoded.data()), static_cast<std::streamsize>(size)))
			return {};
		uint64_t reservation = 0;
		std::string error;
		if (!EstimateTextureBuildMemory(encoded, reservation, error) || reservation > 256ull * 1024 * 1024)
			return {};
		std::vector<uint8_t> artifact;
		// Match the editor's existing non-sRGB UI texture path.
		const AssetImportSettings settings = {
			{ "sRGB", "false" }, { "generateMipmaps", "true" }, { "compression", "none" }
		};
		if (!BuildTextureArtifact(encoded, settings, "editor-preview", artifact, error))
			return {};
		auto texture = Texture2D::Create(artifact.data(), artifact.size(), path);
		return texture && texture->IsLoaded() ? texture : Ref<Texture2D>{};
	}

}
