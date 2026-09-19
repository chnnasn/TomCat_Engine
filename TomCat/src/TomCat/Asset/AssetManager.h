#pragma once

#include "AssetRegistry.h"
#include "AssetDatabase.h"
#include "SpriteAsset.h"
#include "AssetImportCoordinator.h"
#include "MaterialArtifact.h"
#include "MeshArtifact.h"
#include "TomCat/Core/Base.h"
#include "TomCat/Project/ProjectSettings.h"
#include "TomCat/Project/PlayerSettings.h"
#include "TomCat/Renderer/Texture.h"

#include <array>
#include <cstdint>
#include <condition_variable>
#include <deque>
#include <filesystem>
#include <fstream>
#include <functional>
#include <mutex>
#include <optional>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

namespace TomCat {

	class Project;
	class Shader;

	template<AssetType ExpectedType>
	struct TypedAssetLoadResult
	{
		AssetLoadStatus Status = AssetLoadStatus::NotInitialized;
		ImportedArtifact Artifact;
		std::string Error;

		[[nodiscard]] bool Succeeded() const noexcept
		{
			return Status == AssetLoadStatus::Success
				&& Artifact.Type == ExpectedType;
		}
	};

	using ShaderLoadResult = TypedAssetLoadResult<AssetType::Shader>;
	using MaterialLoadResult = TypedAssetLoadResult<AssetType::Material>;
	using AudioLoadResult = TypedAssetLoadResult<AssetType::Audio>;
	using MeshLoadResult = TypedAssetLoadResult<AssetType::Mesh>;
	using FontLoadResult = TypedAssetLoadResult<AssetType::Font>;

	template<typename ArtifactData, AssetType ExpectedType>
	struct DecodedAssetLoadResult
	{
		AssetLoadStatus Status = AssetLoadStatus::NotInitialized;
		AssetHandle Handle = AssetHandle(0);
		AssetType Type = AssetType::None;
		std::string ArtifactKey;
		std::string Format;
		std::vector<AssetSubAsset> SubAssets;
		std::vector<std::string> DependencyKeys;
		bool FromCache = false;
		ArtifactData Asset;
		std::string Error;

		[[nodiscard]] bool Succeeded() const noexcept
		{
			return Status == AssetLoadStatus::Success && Type == ExpectedType;
		}
	};

	using DecodedMaterialLoadResult = DecodedAssetLoadResult<
		MaterialArtifact, AssetType::Material>;
	using DecodedMeshLoadResult = DecodedAssetLoadResult<
		MeshArtifact, AssetType::Mesh>;

	// Path-free managed payload embedded in tcpak v5. Host/runtime files remain
	// beside the Player on disk; only the collectible project assembly is stored
	// in the package.
	struct ManagedPackagePayload
	{
		uint32_t NativeApiVersion = 0;
		uint32_t ManagedApiVersion = 0;
		uint32_t ScriptManifestVersion = 0;
		std::string TargetFramework;
		std::string RuntimeIdentifier;
		std::string BuildID;
		std::string AssemblySHA256;
		std::string ScriptManifestJson;
		std::vector<uint8_t> Assembly;
		std::vector<uint8_t> Pdb;
	};

	// A validated, immutable byte range in a mounted package. Streaming systems
	// may open PackagePath independently and read only this range; source paths
	// are never exposed by a cooked Player.
	struct CookedAssetRange
	{
		std::filesystem::path PackagePath;
		uint64_t Offset = 0;
		uint64_t Size = 0;
		AssetType Type = AssetType::None;
		std::array<uint8_t, 32> SHA256Digest{};
		bool HasSHA256Digest = false;
	};

	struct TextureStreamingStats
	{
		size_t BacklogCount = 0;
		size_t PendingCount = 0;
		size_t PreparedCount = 0;
		uint64_t PreparedBytes = 0;
		size_t JobsInFlight = 0;
	};

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
		AssetLoadResult LoadImportedArtifact(AssetHandle handle,
			AssetLoadOptions options = {});
		std::future<AssetLoadResult> LoadImportedArtifactAsync(AssetHandle handle,
			AssetLoadOptions options = {});
		template<AssetType ExpectedType>
		TypedAssetLoadResult<ExpectedType> LoadTypedArtifact(AssetHandle handle,
			AssetLoadOptions options = {})
		{
			AssetLoadResult untyped = LoadImportedArtifact(handle, std::move(options));
			TypedAssetLoadResult<ExpectedType> result;
			result.Status = untyped.Status;
			result.Error = std::move(untyped.Error);
			result.Artifact = std::move(untyped.Artifact);
			if (result.Status == AssetLoadStatus::Success
				&& result.Artifact.Type != ExpectedType)
			{
				result.Status = AssetLoadStatus::UnsupportedType;
				result.Error = "asset type does not match the requested typed loader";
			}
			return result;
		}

		template<AssetType ExpectedType>
		std::future<TypedAssetLoadResult<ExpectedType>> LoadTypedArtifactAsync(
			AssetHandle handle, AssetLoadOptions options = {})
		{
			std::future<AssetLoadResult> untyped = LoadImportedArtifactAsync(handle,
				std::move(options));
			return AssetJobSystem::Get().Submit(0,
				[untyped = std::move(untyped)]() mutable
				{
					AssetLoadResult loaded = untyped.get();
					TypedAssetLoadResult<ExpectedType> result;
					result.Status = loaded.Status;
					result.Error = std::move(loaded.Error);
					result.Artifact = std::move(loaded.Artifact);
					if (result.Status == AssetLoadStatus::Success
						&& result.Artifact.Type != ExpectedType)
					{
						result.Status = AssetLoadStatus::UnsupportedType;
						result.Error = "asset type does not match the requested typed loader";
					}
					return result;
				});
		}
		DecodedMaterialLoadResult LoadMaterial(AssetHandle handle,
			AssetLoadOptions options = {});
		std::future<DecodedMaterialLoadResult> LoadMaterialAsync(AssetHandle handle,
			AssetLoadOptions options = {});
		DecodedMeshLoadResult LoadMesh(AssetHandle handle,
			AssetLoadOptions options = {});
		std::future<DecodedMeshLoadResult> LoadMeshAsync(AssetHandle handle,
			AssetLoadOptions options = {});
		size_t PumpImportCoordinator(
			const AssetImportCoordinator::Callback& callback = {});
		void RequestAssetScan() { m_ImportCoordinator.RequestScan(); }
		uint64_t GetImportRevision() const { return m_ImportRevision; }
		AssetImportCoordinator& GetImportCoordinator() { return m_ImportCoordinator; }
		const AssetImportCoordinator& GetImportCoordinator() const
		{
			return m_ImportCoordinator;
		}
		AssetDatabase& GetDatabase() { return m_Database; }
		const AssetDatabase& GetDatabase() const { return m_Database; }

		Ref<Texture2D> LoadTexture(AssetHandle handle);
		// Loads and publishes an offline shader artifact on the calling graphics
		// thread. A current renderer context is required for the first load.
		Ref<Shader> LoadShader(AssetHandle handle);
		// Player startup uses this after mounting a package so every Shader in the
		// cooked dependency closure is validated and linked before scene execution.
		bool PreloadCookedShaders(std::string& error);
		bool ResolveSpriteAsset(AssetHandle handle, ResolvedSpriteAsset& sprite) const;
		Ref<Texture2D> GetMissingTexture();
		// Player startup adds all package textures to a bounded, non-blocking
		// backlog. Workers only read and validate immutable artifact bytes. Call
		// PumpTexturePublishes from the application thread after the graphics
		// context is current to create a small number of GPU resources per frame.
		// The byte limit is the worst-case RGBA size of every mip, including the
		// fallback cost when a platform cannot upload BC3 directly.
		size_t BeginCookedTexturePreload();
		size_t PumpTexturePublishes(uint32_t maximumUploads = 2,
			uint64_t maximumUploadBytes = 32ULL * 1024ULL * 1024ULL);
		TextureStreamingStats GetTextureStreamingStats() const;

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

		// The current cooked package contains a path-free start-scene manifest and
		// project collision matrix, followed by a fixed handle/type/offset/size index
		// and cooked asset bytes. The explicit
		// overload can build an asset-only package (handle 0) only when the manager
		// was initialized without a Project.
		bool CookToPackage(const std::filesystem::path& packagePath);
		bool CookToPackage(const std::filesystem::path& packagePath,
			AssetHandle startSceneHandle);
		// Tests and non-Editor authoring tools may provide an already validated
		// Release build explicitly. Normal project cooks discover last-good.json.
		bool SetManagedCookPayload(std::vector<uint8_t> assembly,
			std::string scriptManifestJson, std::string buildID,
			std::vector<uint8_t> pdb = {});
		void ClearManagedCookPayload() { m_ManagedCookPayloadOverride.reset(); }
		bool MountCookedPackage(const std::filesystem::path& packagePath);
		void UnmountCookedPackage();
		bool IsCookedPackageMounted() const { return !m_CookedPackagePath.empty(); }
		const std::filesystem::path& GetCookedPackagePath() const { return m_CookedPackagePath; }
		AssetHandle GetCookedEntrySceneHandle() const
		{
			return IsCookedPackageMounted() ? m_CookedEntrySceneHandle : AssetHandle(0);
		}
		AssetHandle GetCookedStartSceneHandle() const { return GetCookedEntrySceneHandle(); }
		const std::vector<AssetHandle>& GetCookedBuildSceneHandles() const
		{
			return m_CookedBuildSceneHandles;
		}
		uint32_t GetCookedPackageVersion() const
		{
			return IsCookedPackageMounted() ? m_CookedPackageVersion : 0;
		}
		const PlayerSettings& GetCookedPlayerSettings() const
		{
			return m_CookedPlayerSettings;
		}
		std::optional<uint32_t> GetCookedBuildSceneIndex(AssetHandle handle) const;
		AssetHandle GetCookedBuildSceneHandle(uint32_t index) const;
		// Authoring reads the active Project; a cooked runtime reads the immutable
		// matrix embedded in the v5 package header.
		Physics2DSettings GetPhysics2DSettings() const;
		const ManagedPackagePayload* GetCookedManagedPayload() const
		{
			return IsCookedPackageMounted() && m_CookedManagedPayload
				? &*m_CookedManagedPayload : nullptr;
		}
		bool ReadAssetBytes(AssetHandle handle, std::vector<uint8_t>& bytes,
			AssetType* type = nullptr) const;
		std::vector<uint8_t> ReadAssetBytes(AssetHandle handle) const;
		[[nodiscard]] bool TryGetCookedAssetRange(AssetHandle handle,
			CookedAssetRange& range) const;

	private:
		struct CookedEntry
		{
			AssetType Type = AssetType::None;
			uint64_t Offset = 0;
			uint64_t Size = 0;
			std::array<uint8_t, 32> SHA256Digest{};
			bool HasSHA256Digest = false;
		};

		struct PreparedTexture
		{
			AssetHandle Handle = AssetHandle(0);
			uint64_t Generation = 0;
			std::vector<uint8_t> Bytes;
			std::filesystem::path SourcePath;
			ResolvedSpriteAsset Sprite;
			bool HasSpriteDescriptor = false;
			uint64_t EstimatedUploadBytes = 0;
			std::string Error;
		};

		AssetManager() = default;
		AssetManager(const AssetManager&) = delete;
		AssetManager& operator=(const AssetManager&) = delete;

		Ref<Texture2D> CacheMissingTexture(AssetHandle handle, const char* reason);
		bool RequestCookedTexture(AssetHandle handle, bool prioritize);
		void ScheduleTextureBacklog();
		void PrepareCookedTexture(AssetHandle handle, uint64_t generation,
			std::filesystem::path packagePath, uint64_t offset, uint64_t size,
			const std::array<uint8_t, 32>& expectedDigest, bool verifyDigest,
			bool requireArtifact);
		void CancelTextureStreaming(bool waitForJobs);
		void ReleaseHandles(const std::vector<AssetHandle>& handles);
		bool DeleteAssetInternal(const std::filesystem::path& path, bool force,
			std::vector<AssetReference>* references, AssetHandle expectedHandle);
		bool BeginAsyncLoad();
		void FinishAsyncLoad() noexcept;
		void EnableAsyncLoads();
		bool StopAndWaitForAsyncLoads();

	private:
		AssetRegistry m_Registry;
		AssetDatabase m_Database;
		AssetImportCoordinator m_ImportCoordinator;
		uint64_t m_ImportRevision = 0;
		bool m_RegistryInitialized = false;
		std::mutex m_AsyncLoadMutex;
		std::condition_variable m_AsyncLoadsIdle;
		size_t m_AsyncLoadsInFlight = 0;
		bool m_AcceptingAsyncLoads = false;
		std::unordered_map<AssetHandle, Ref<Texture2D>> m_TextureCache;
		std::unordered_map<AssetHandle, Ref<Shader>> m_ShaderCache;
		mutable std::unordered_map<AssetHandle, ResolvedSpriteAsset> m_SpriteDescriptorCache;
		Ref<Texture2D> m_MissingTexture;
		mutable std::mutex m_TextureStreamingMutex;
		std::condition_variable m_TextureStreamingCapacity;
		std::condition_variable m_TextureStreamingIdle;
		std::deque<AssetHandle> m_TexturePreloadBacklog;
		std::unordered_set<AssetHandle> m_TextureBacklogSet;
		std::unordered_set<AssetHandle> m_TexturePending;
		std::deque<PreparedTexture> m_PreparedTextures;
		std::unordered_map<AssetHandle, std::string> m_TextureFailures;
		uint64_t m_PreparedTextureBytes = 0;
		uint64_t m_TextureStreamingGeneration = 1;
		size_t m_TextureJobsInFlight = 0;
		LiveReferenceProvider m_LiveReferenceProvider;

		std::filesystem::path m_CookedPackagePath;
		uint64_t m_CookedPackageSize = 0;
		uint32_t m_CookedPackageVersion = 0;
		AssetHandle m_CookedEntrySceneHandle = AssetHandle(0);
		std::vector<AssetHandle> m_CookedBuildSceneHandles;
		Physics2DSettings m_CookedPhysics2DSettings;
		PlayerSettings m_CookedPlayerSettings;
		std::optional<ManagedPackagePayload> m_CookedManagedPayload;
		std::unordered_map<AssetHandle, CookedEntry> m_CookedEntries;
		mutable std::ifstream m_CookedPackageStream;
		mutable std::mutex m_CookedPackageMutex;

		// Authoring-only input used by the convenience CookToPackage overload.
		// ProjectSettings/BuildSettings.json is the stable source of truth; the
		// cooked package serializes its entry and ordered enabled-scene handles.
		std::weak_ptr<Project> m_AuthoringProject;
		bool m_UsesProjectConfiguration = false;
		std::optional<ManagedPackagePayload> m_ManagedCookPayloadOverride;
	};

}
