#pragma once
#include "Scene.h"
#include "TomCat/Asset/Asset.h"

#include <cstdint>
#include <filesystem>
#include <iosfwd>
#include <string>
#include <vector>

namespace TomCat {

	class SceneSerializer
	{
	public:
		static constexpr uint32_t CurrentSchemaVersion = 10;
		static constexpr uint32_t OldestSupportedSchemaVersion = 9;

		SceneSerializer(const Ref<Scene>& scene);

		bool Serialize(const std::filesystem::path& filepath);
		// Internal document hooks used by SceneArchiveCodec. They preserve the
		// serializer's single canonical component schema while allowing Prefabs to
		// validate through an in-memory synthetic Scene document.
		bool SerializeDocument(std::string& document, std::string& error) const;
		bool DeserializeDocument(const std::vector<uint8_t>& bytes,
			const std::filesystem::path& diagnosticPath, bool resolveAssets);

		bool Deserialize(const std::filesystem::path& filepath);
		// Parses and validates the complete current scene schema without resolving
		// runtime resources. Cook uses this to reject malformed source scenes before
		// publishing a package.
		static bool ValidateCurrentFormat(const std::filesystem::path& filepath);
		static bool ValidateCurrentFormat(const std::vector<uint8_t>& bytes,
			const std::filesystem::path& diagnosticPath);
		// Runtime path: deserialize a Scene entry directly from the currently
		// mounted cooked package. No source path or extracted temporary file is used.
		bool Deserialize(AssetHandle handle);

	private:
		bool DeserializeStream(std::istream& input,
			const std::filesystem::path& diagnosticPath, bool resolveAssets);

		Ref<Scene> m_Scene;
	};

}
