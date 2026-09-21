#pragma once

#include "Importer.h"

#include <memory>
#include <shared_mutex>
#include <string>
#include <unordered_map>
#include <vector>

namespace TomCat {

	class ImporterRegistry final
	{
	public:
		[[nodiscard]] bool Register(std::shared_ptr<const IAssetImporter> importer,
			bool replaceExisting = false);
		[[nodiscard]] bool Unregister(AssetType type);
		[[nodiscard]] std::shared_ptr<const IAssetImporter> Find(AssetType type) const;
		[[nodiscard]] std::vector<AssetType> GetRegisteredTypes() const;
		void Clear();

		// Registers deterministic production importers for render/audio data and
		// validated archive/source importers for the remaining authoring types.
		void RegisterBuiltInImporters();

	private:
		mutable std::shared_mutex m_Mutex;
		std::unordered_map<AssetType, std::shared_ptr<const IAssetImporter>> m_ByType;
		std::unordered_map<std::string, AssetType> m_ByID;
	};

}
