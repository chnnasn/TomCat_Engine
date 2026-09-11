#pragma once

#include "AssetRegistry.h"
#include "TomCat/Core/Base.h"
#include "TomCat/Renderer/Texture.h"

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <functional>
#include <mutex>
#include <unordered_map>
#include <utility>
#include <vector>

namespace TomCat {

	class Project;

	// Owns loaded project assets. Authoring builds resolve handles through the
	// registry; a mounted cooked package is deliberately path-free and takes
	// precedence over the registry.
	class AssetManager
	{
	public:
		static AssetManager& Get();

		bool Initialize(const std::filesystem::path& assetRoot,
			const std::filesystem::path& libraryRoot);
		bool SetProject(const Ref<Project>& project);
		void Shutdown();
		bool IsInitialized() const { return m_RegistryInitialized || IsCookedPackageMounted(); }

		bool Refresh();
		AssetHandle ImportAsset(const std::filesystem::path& path);
		bool SetImportSettings(AssetHandle handle, const AssetImportSettings& settings);

		Ref<Texture2D> LoadTexture(AssetHandle handle);
		Ref<Texture2D> GetMissingTexture();

		void Release(AssetHandle handle);
		void ReleaseAll();

		bool MoveAsset(const std::filesystem::path& source,
			const std::filesystem::path& destination);
		bool MoveAsset(AssetHandle handle, const std::filesystem::path& destination);
		bool DeleteAsset(const std::filesystem::path& path, bool force = false,
			std::vector<AssetReference>* references = nullptr);
		bool DeleteAsset(AssetHandle handle, bool force = false,
			std::vector<AssetReference>* references = nullptr);
		std::vector<AssetReference> FindReferences(AssetHandle handle) const;
		using LiveReferenceProvider = std::function<std::vector<AssetReference>(AssetHandle)>;
		void SetLiveReferenceProvider(LiveReferenceProvider provider)
		{
			m_LiveReferenceProvider = std::move(provider);
		}

		std::filesystem::path ResolvePath(AssetHandle handle) const;
		AssetRegistry& GetRegistry() { return m_Registry; }
		const AssetRegistry& GetRegistry() const { return m_Registry; }
		AssetRegistry& Registry() { return m_Registry; }
		const AssetRegistry& Registry() const { return m_Registry; }

		// The current cooked package contains a path-free start-scene manifest followed by a
		// fixed handle/type/offset/size index and cooked asset bytes. The explicit
		// overload can build an asset-only package (handle 0) only when the manager
		// was initialized without a Project.
		bool CookToPackage(const std::filesystem::path& packagePath);
		bool CookToPackage(const std::filesystem::path& packagePath,
			AssetHandle startSceneHandle);
		bool MountCookedPackage(const std::filesystem::path& packagePath);
		void UnmountCookedPackage();
		bool IsCookedPackageMounted() const { return !m_CookedPackagePath.empty(); }
		const std::filesystem::path& GetCookedPackagePath() const { return m_CookedPackagePath; }
		AssetHandle GetCookedStartSceneHandle() const
		{
			return IsCookedPackageMounted() ? m_CookedStartSceneHandle : AssetHandle(0);
		}
		bool ReadAssetBytes(AssetHandle handle, std::vector<uint8_t>& bytes,
			AssetType* type = nullptr) const;
		std::vector<uint8_t> ReadAssetBytes(AssetHandle handle) const;

	private:
		struct CookedEntry
		{
			AssetType Type = AssetType::None;
			uint64_t Offset = 0;
			uint64_t Size = 0;
		};

		AssetManager() = default;
		AssetManager(const AssetManager&) = delete;
		AssetManager& operator=(const AssetManager&) = delete;

		Ref<Texture2D> CacheMissingTexture(AssetHandle handle, const char* reason);
		void ReleaseHandles(const std::vector<AssetHandle>& handles);
		bool DeleteAssetInternal(const std::filesystem::path& path, bool force,
			std::vector<AssetReference>* references, AssetHandle expectedHandle);

	private:
		AssetRegistry m_Registry;
		bool m_RegistryInitialized = false;
		std::unordered_map<AssetHandle, Ref<Texture2D>> m_TextureCache;
		Ref<Texture2D> m_MissingTexture;
		LiveReferenceProvider m_LiveReferenceProvider;

		std::filesystem::path m_CookedPackagePath;
		uint64_t m_CookedPackageSize = 0;
		AssetHandle m_CookedStartSceneHandle = AssetHandle(0);
		std::unordered_map<AssetHandle, CookedEntry> m_CookedEntries;
		mutable std::ifstream m_CookedPackageStream;
		mutable std::mutex m_CookedPackageMutex;

		// Authoring-only input used by the convenience CookToPackage overload.
		// Project.StartSceneHandle is the stable source of truth; the cooked
		// package serializes its value, never the authoring path.
		std::weak_ptr<Project> m_AuthoringProject;
		bool m_UsesProjectConfiguration = false;
	};

}
