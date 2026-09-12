#include "TomCat/Asset/AssetDatabase.h"
#include "TomCat/Asset/AssetImportCoordinator.h"
#include "TomCat/Asset/AssetRegistry.h"
#include "TomCat/Core/Log.h"
#include "TomCat/Core/UUID.h"
#include "TomCat/Scene/Components.h"
#include "TomCat/Scene/Scene.h"
#include "TomCat/Scene/SceneSerializer.h"
#include "TomCat/Scene/Serialization/PrefabArchiveCodec.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <future>
#include <iostream>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

namespace {

	void Require(bool condition, const char* message)
	{
		if (!condition)
			throw std::runtime_error(message);
	}

	void WriteBigEndian16(std::vector<uint8_t>& bytes, size_t offset,
		uint16_t value)
	{
		Require(offset <= bytes.size() && bytes.size() - offset >= 2,
			"test BE16 write is out of range");
		bytes[offset] = static_cast<uint8_t>(value >> 8);
		bytes[offset + 1] = static_cast<uint8_t>(value);
	}

	void WriteBigEndian32(std::vector<uint8_t>& bytes, size_t offset,
		uint32_t value)
	{
		Require(offset <= bytes.size() && bytes.size() - offset >= 4,
			"test BE32 write is out of range");
		bytes[offset] = static_cast<uint8_t>(value >> 24);
		bytes[offset + 1] = static_cast<uint8_t>(value >> 16);
		bytes[offset + 2] = static_cast<uint8_t>(value >> 8);
		bytes[offset + 3] = static_cast<uint8_t>(value);
	}

	void WriteBytes(const std::filesystem::path& path, std::string_view bytes)
	{
		std::filesystem::create_directories(path.parent_path());
		std::ofstream output(path, std::ios::binary | std::ios::trunc);
		Require(static_cast<bool>(output), "could not create test file");
		output.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
		Require(static_cast<bool>(output), "could not write complete test file");
	}

	std::string ReadText(const std::filesystem::path& path)
	{
		std::ifstream input(path, std::ios::binary | std::ios::ate);
		Require(static_cast<bool>(input), "could not open test file");
		const std::streamoff size = input.tellg();
		Require(size >= 0, "test file size is invalid");
		std::string result(static_cast<size_t>(size), '\0');
		input.seekg(0, std::ios::beg);
		if (!result.empty())
			input.read(result.data(), size);
		Require(static_cast<bool>(input) || result.empty(), "could not read test file");
		return result;
	}

	class TemporaryProject final
	{
	public:
		TemporaryProject()
		{
			Root = std::filesystem::temp_directory_path() /
				("tomcat_importers_" +
					std::to_string(static_cast<uint64_t>(TomCat::UUID())));
			Assets = Root / "Assets";
			Library = Root / "Library";
			std::filesystem::create_directories(Assets);
		}

		~TemporaryProject()
		{
			std::error_code ignored;
			std::filesystem::remove_all(Root, ignored);
		}

		std::filesystem::path Root;
		std::filesystem::path Assets;
		std::filesystem::path Library;
	};

	class CountingTextureImporter final : public TomCat::IAssetImporter
	{
	public:
		std::string_view GetID() const noexcept override
		{
			return "regression.texture.counting";
		}
		uint32_t GetVersion() const noexcept override { return 7; }
		TomCat::AssetType GetAssetType() const noexcept override
		{
			return TomCat::AssetType::Texture2D;
		}

		TomCat::AssetImportResult Import(
			const TomCat::AssetImportRequest& request) const override
		{
			++Invocations;
			const uint32_t active = ActiveInvocations.fetch_add(1) + 1;
			uint32_t maximum = MaxConcurrentInvocations.load();
			while (maximum < active && !MaxConcurrentInvocations.compare_exchange_weak(
				maximum, active)) {}
			std::this_thread::sleep_for(std::chrono::milliseconds(
				DelayMilliseconds.load()));
			TomCat::AssetImportResult result;
			if (request.IsCancellationRequested())
			{
				result.Error = "cancelled";
				ActiveInvocations.fetch_sub(1);
				return result;
			}
			result.Format = "regression-texture/v7";
			result.ArtifactBytes.assign(request.SourceBytes.begin(), request.SourceBytes.end());
			const auto setting = request.Settings.find("quality");
			if (setting != request.Settings.end())
				result.ArtifactBytes.insert(result.ArtifactBytes.end(),
					setting->second.begin(), setting->second.end());
			result.SubAssets.push_back({ "sprite:hero", "Hero", TomCat::AssetType::Texture2D });
			ActiveInvocations.fetch_sub(1);
			return result;
		}

		mutable std::atomic_uint32_t Invocations = 0;
		mutable std::atomic_uint32_t DelayMilliseconds = 15;
		mutable std::atomic_uint32_t ActiveInvocations = 0;
		mutable std::atomic_uint32_t MaxConcurrentInvocations = 0;
	};

	TomCat::AssetHandle RequireHandle(TomCat::AssetRegistry& registry,
		const std::filesystem::path& path, TomCat::AssetType expected)
	{
		const TomCat::AssetHandle handle = registry.ImportAsset(path);
		const TomCat::AssetMetadata* metadata = registry.GetMetadata(handle);
		Require(static_cast<uint64_t>(handle) != 0 && metadata &&
			metadata->Type == expected && !metadata->IsMissing,
			"asset did not receive a live handle of the expected type");
		return handle;
	}

	TomCat::AssetHandle RequireHeroSubAsset(TomCat::AssetRegistry& registry,
		TomCat::AssetHandle hero)
	{
		const TomCat::AssetMetadata* metadata = registry.GetMetadata(hero);
		Require(metadata && metadata->SubAssets.size() == 1 &&
			metadata->SubAssets[0].PersistentID == "sprite:hero" &&
			static_cast<uint64_t>(metadata->SubAssets[0].Handle) != 0,
			"stable imported sub-asset was not committed to tcmeta v2");
		return metadata->SubAssets[0].Handle;
	}

	void TestMalformedFontPreflight()
	{
		TomCat::ImporterRegistry registry;
		registry.RegisterBuiltInImporters();
		const std::shared_ptr<const TomCat::IAssetImporter> importer =
			registry.Find(TomCat::AssetType::Font);
		Require(importer && importer->GetVersion() >= 2,
			"validated SFNT importer is not registered");
		auto rejects = [&](const std::vector<uint8_t>& bytes)
		{
			TomCat::AssetImportRequest request;
			request.Type = TomCat::AssetType::Font;
			request.SourceBytes = bytes;
			const TomCat::AssetImportResult result = importer->Import(request);
			return !result.Succeeded() && result.ArtifactBytes.empty()
				&& !result.Error.empty();
		};

		std::vector<uint8_t> truncatedDirectory(12, 0);
		WriteBigEndian32(truncatedDirectory, 0, 0x00010000u);
		WriteBigEndian16(truncatedDirectory, 4, 1);
		Require(rejects(truncatedDirectory),
			"SFNT with a truncated table directory was accepted");

		std::vector<uint8_t> overflowingTable(28, 0);
		WriteBigEndian32(overflowingTable, 0, 0x00010000u);
		WriteBigEndian16(overflowingTable, 4, 1);
		WriteBigEndian32(overflowingTable, 12, 0x636d6170u);
		WriteBigEndian32(overflowingTable, 20, 0xfffffff0u);
		WriteBigEndian32(overflowingTable, 24, 64);
		Require(rejects(overflowingTable),
			"SFNT with an out-of-range table was accepted");

		std::vector<uint8_t> overflowingCollectionFace(16, 0);
		WriteBigEndian32(overflowingCollectionFace, 0, 0x74746366u);
		WriteBigEndian32(overflowingCollectionFace, 4, 0x00010000u);
		WriteBigEndian32(overflowingCollectionFace, 8, 1);
		WriteBigEndian32(overflowingCollectionFace, 12, 0xfffffff0u);
		Require(rejects(overflowingCollectionFace),
			"TTC with an out-of-range face offset was accepted");

		std::vector<uint8_t> missingRequiredTables(12, 0);
		WriteBigEndian32(missingRequiredTables, 0, 0x00010000u);
		Require(rejects(missingRequiredTables),
			"SFNT without required rendering tables was accepted");
	}

	void TestDeterministicImportPipeline()
	{
		TemporaryProject project;
		const std::filesystem::path heroPath = project.Assets / "hero.png";
		const std::filesystem::path dependencyPath = project.Assets / "common.glsl";
		const std::filesystem::path futurePath = project.Assets / "future.png";
		const std::filesystem::path sharedOwnerPath = project.Assets / "shared-owner.png";
		const std::filesystem::path sharedWaiterPath = project.Assets / "shared-waiter.png";
		WriteBytes(heroPath, "source-one");
		WriteBytes(dependencyPath, "shader-one");
		WriteBytes(futurePath, "future-source");
		WriteBytes(sharedOwnerPath, "identical-shared-flight");
		WriteBytes(sharedWaiterPath, "identical-shared-flight");

		const std::filesystem::path heroMeta =
			TomCat::AssetRegistry::GetMetadataPath(heroPath);
		WriteBytes(heroMeta,
			"SchemaVersion: 1\n"
			"FutureRoot:\n  Keep: root-value\n"
			"Asset:\n"
			"  Handle: 1001\n"
			"  Type: Texture2D\n"
			"  ImportSettings:\n    quality: high\n"
			"  FutureAsset:\n    Keep: asset-value\n");
		const std::filesystem::path futureMeta =
			TomCat::AssetRegistry::GetMetadataPath(futurePath);
		const std::string futureDocument =
			"SchemaVersion: 99\nAsset:\n  Handle: 9901\n  Type: Texture2D\n"
			"  ImportSettings: {}\nFutureOnly: untouched\n";
		WriteBytes(futureMeta, futureDocument);

		TomCat::AssetRegistry registry;
		Require(registry.Initialize(project.Assets, project.Library),
			"asset registry initialization failed");
		const TomCat::AssetMetadata* migrated =
			registry.GetMetadata(TomCat::AssetHandle(1001));
		Require(migrated && migrated->ImportSettings.at("quality") == "high",
			"v1 tcmeta was not read during migration");
		const std::string migratedDocument = ReadText(heroMeta);
		Require(migratedDocument.find("SchemaVersion: 2") != std::string::npos &&
			migratedDocument.find("SubAssets:") != std::string::npos &&
			migratedDocument.find("FutureRoot") != std::string::npos &&
			migratedDocument.find("FutureAsset") != std::string::npos,
			"v1 migration did not preserve unknown fields or emit v2 SubAssets");
		Require(ReadText(futureMeta) == futureDocument &&
			registry.GetMetadata(futurePath) == nullptr,
			"future tcmeta schema was overwritten or registered ambiguously");

		const TomCat::AssetHandle hero(1001);
		const TomCat::AssetHandle dependency = RequireHandle(registry,
			dependencyPath, TomCat::AssetType::Shader);
		const TomCat::AssetHandle sharedOwner = RequireHandle(registry,
			sharedOwnerPath, TomCat::AssetType::Texture2D);
		const TomCat::AssetHandle sharedWaiter = RequireHandle(registry,
			sharedWaiterPath, TomCat::AssetType::Texture2D);
		TomCat::AssetDatabase database;
		Require(database.Initialize(registry, project.Library),
			"asset database initialization failed");
		const std::vector<TomCat::AssetType> registered =
			database.GetImporters().GetRegisteredTypes();
		for (TomCat::AssetType required : { TomCat::AssetType::Texture2D,
			TomCat::AssetType::Shader, TomCat::AssetType::Material,
			TomCat::AssetType::Font, TomCat::AssetType::Audio,
			TomCat::AssetType::Scene, TomCat::AssetType::Prefab,
			TomCat::AssetType::CSharpScript })
		{
			Require(std::find(registered.begin(), registered.end(), required) !=
				registered.end(), "a required built-in importer was not registered");
		}

		auto counting = std::make_shared<CountingTextureImporter>();
		Require(database.GetImporters().Register(counting, true),
			"custom importer replacement failed");

		// Artifact keys intentionally omit Handle, so these two assets share one
		// flight. Cancelling its owner must not leak Cancelled to the independent,
		// non-cancelled waiter; the waiter retries ownership without recursion.
		counting->DelayMilliseconds = 150;
		auto ownerCancellation = std::make_shared<TomCat::AssetLoadCancellation>();
		TomCat::AssetLoadOptions ownerOptions;
		ownerOptions.Cancellation = ownerCancellation;
		std::future<TomCat::AssetLoadResult> cancelledOwner =
			database.LoadArtifactAsync(sharedOwner, ownerOptions);
		const auto ownerStartedDeadline = std::chrono::steady_clock::now()
			+ std::chrono::seconds(2);
		while (counting->Invocations.load() == 0
			&& std::chrono::steady_clock::now() < ownerStartedDeadline)
			std::this_thread::yield();
		Require(counting->Invocations.load() == 1,
			"shared artifact-key owner did not enter its importer");
		std::future<TomCat::AssetLoadResult> independentWaiter =
			database.LoadArtifactAsync(sharedWaiter);
		std::this_thread::sleep_for(std::chrono::milliseconds(25));
		ownerCancellation->Cancel();
		const TomCat::AssetLoadResult ownerResult = cancelledOwner.get();
		const TomCat::AssetLoadResult waiterResult = independentWaiter.get();
		Require(ownerResult.Status == TomCat::AssetLoadStatus::Cancelled,
			"shared artifact-key owner did not observe its cancellation");
		Require(waiterResult.Succeeded()
			&& waiterResult.Artifact.Handle == sharedWaiter
			&& counting->Invocations.load() == 2,
			"cancelled shared-flight owner incorrectly cancelled its independent waiter");
		counting->Invocations = 0;
		counting->ActiveInvocations = 0;
		counting->MaxConcurrentInvocations = 0;
		counting->DelayMilliseconds = 15;

		TomCat::AssetLoadResult first = database.LoadArtifact(hero);
		Require(first.Succeeded() && !first.Artifact.FromCache &&
			counting->Invocations == 1, "first import did not publish a derived artifact");
		const std::string firstKey = first.Artifact.ArtifactKey;
		const TomCat::AssetHandle stableChild = RequireHeroSubAsset(registry, hero);

		TomCat::AssetLoadResult repeated = database.LoadArtifact(hero);
		Require(repeated.Succeeded() && repeated.Artifact.FromCache &&
			repeated.Artifact.ArtifactKey == firstKey && counting->Invocations == 1 &&
			RequireHeroSubAsset(registry, hero) == stableChild,
			"identical input did not hit cache or changed the sub-asset handle");

		Require(registry.SetImportSettings(hero, { { "quality", "low" } }),
			"could not change import settings");
		TomCat::AssetLoadResult settingsChanged = database.LoadArtifact(hero);
		Require(settingsChanged.Succeeded() && settingsChanged.Artifact.ArtifactKey != firstKey &&
			counting->Invocations == 2 && RequireHeroSubAsset(registry, hero) == stableChild,
			"settings did not invalidate the artifact key or preserve sub-asset identity");

		WriteBytes(heroPath, "source-two");
		TomCat::AssetLoadResult sourceChanged = database.LoadArtifact(hero);
		Require(sourceChanged.Succeeded() &&
			sourceChanged.Artifact.ArtifactKey != settingsChanged.Artifact.ArtifactKey &&
			counting->Invocations == 3 && RequireHeroSubAsset(registry, hero) == stableChild,
			"source content did not invalidate the artifact key");

		Require(database.SetDependencies(hero, { dependency }),
			"could not establish dependency graph edge");
		Require(database.GetDependents(dependency) ==
			std::vector<TomCat::AssetHandle>{ hero },
			"reverse dependency graph is inconsistent");
		TomCat::AssetLoadResult dependencyAdded = database.LoadArtifact(hero);
		Require(dependencyAdded.Succeeded() &&
			dependencyAdded.Artifact.ArtifactKey != sourceChanged.Artifact.ArtifactKey &&
			counting->Invocations == 4,
			"adding a dependency did not invalidate the artifact key");
		WriteBytes(dependencyPath, "shader-two");
		TomCat::AssetLoadResult dependencyChanged = database.LoadArtifact(hero);
		Require(dependencyChanged.Succeeded() &&
			dependencyChanged.Artifact.ArtifactKey != dependencyAdded.Artifact.ArtifactKey &&
			counting->Invocations == 5,
			"dependency content did not propagate into the artifact key");

		Require(registry.SetImportSettings(hero,
			{ { "batch", "single-flight" }, { "quality", "low" } }),
			"could not prepare single-flight settings");
		const uint32_t beforeConcurrent = counting->Invocations.load();
		std::vector<std::future<TomCat::AssetLoadResult>> futures;
		for (size_t index = 0; index < 12; ++index)
			futures.push_back(database.LoadArtifactAsync(hero));
		std::string concurrentKey;
		for (auto& future : futures)
		{
			TomCat::AssetLoadResult loaded = future.get();
			Require(loaded.Succeeded(), "concurrent import failed");
			if (concurrentKey.empty())
				concurrentKey = loaded.Artifact.ArtifactKey;
			Require(loaded.Artifact.ArtifactKey == concurrentKey,
				"concurrent callers observed different artifact keys");
		}
		Require(counting->Invocations == beforeConcurrent + 1,
			"concurrent identical loads executed the importer more than once");
		Require(RequireHeroSubAsset(registry, hero) == stableChild,
			"concurrent imports changed the stable sub-asset handle");

		const std::filesystem::path cacheEntry =
			database.GetCache().GetEntryPath(concurrentKey);
		WriteBytes(cacheEntry, "damaged-cache-entry");
		const uint32_t beforeRepair = counting->Invocations.load();
		TomCat::AssetLoadResult repaired = database.LoadArtifact(hero);
		Require(repaired.Succeeded() && !repaired.Artifact.FromCache &&
			counting->Invocations == beforeRepair + 1 &&
			database.GetCache().GetStats().CorruptEntries >= 1,
			"damaged cache entry was not detected and rebuilt");
		TomCat::AssetLoadResult repairedHit = database.LoadArtifact(hero);
		Require(repairedHit.Succeeded() && repairedHit.Artifact.FromCache &&
			counting->Invocations == beforeRepair + 1,
			"rebuilt cache entry was not reusable");

		auto cancellation = std::make_shared<TomCat::AssetLoadCancellation>();
		cancellation->Cancel();
		TomCat::AssetLoadOptions cancelledOptions;
		cancelledOptions.Cancellation = cancellation;
		Require(database.LoadArtifactAsync(hero, cancelledOptions).get().Status ==
			TomCat::AssetLoadStatus::Cancelled,
			"pre-cancelled asynchronous load did not stop");

		const std::optional<size_t> decoded = database.Load<size_t>(hero,
			[](const TomCat::ImportedArtifact& artifact) -> std::optional<size_t>
			{
				return artifact.Bytes.size();
			});
		Require(decoded && *decoded == repairedHit.Artifact.Bytes.size(),
			"typed synchronous Load<T> did not decode the imported artifact");
	}

	void TestFileMonitorImportCoordinator()
	{
		TemporaryProject project;
		const std::filesystem::path heroPath = project.Assets / "hero.png";
		const std::filesystem::path secondaryPath =
			project.Assets / "secondary.png";
		const std::filesystem::path dependencyPath = project.Assets / "common.glsl";
		WriteBytes(heroPath, "watch-baseline");
		WriteBytes(secondaryPath, "secondary-baseline");
		WriteBytes(dependencyPath, "shader-baseline");

		TomCat::AssetRegistry registry;
		Require(registry.Initialize(project.Assets, project.Library),
			"monitor registry initialization failed");
		const TomCat::AssetHandle hero = RequireHandle(registry, heroPath,
			TomCat::AssetType::Texture2D);
		const TomCat::AssetHandle dependency = RequireHandle(registry, dependencyPath,
			TomCat::AssetType::Shader);
		const TomCat::AssetHandle secondary = RequireHandle(registry, secondaryPath,
			TomCat::AssetType::Texture2D);
		Require(registry.SetImportSettings(hero, { { "quality", "watch-before" } }),
			"monitor test could not seed import settings");

		TomCat::AssetDatabase database;
		Require(database.Initialize(registry, project.Library),
			"monitor database initialization failed");
		auto counting = std::make_shared<CountingTextureImporter>();
		Require(database.GetImporters().Register(counting, true),
			"monitor counting importer registration failed");
		Require(database.SetDependencies(hero, { dependency }),
			"monitor dependency graph setup failed");
		Require(database.LoadArtifact(hero).Succeeded() && counting->Invocations == 1,
			"monitor baseline import failed");
		const TomCat::AssetHandle heroSprite = RequireHeroSubAsset(registry, hero);

		// Build a real Prefab -> sliced Sprite edge and a real Scene -> Prefab edge.
		// The dependency scanner must normalize the child Sprite handle to its atlas
		// parent because file monitor events are emitted for the parent source file.
		auto prefabSource = TomCat::CreateRef<TomCat::Scene>();
		TomCat::Entity prefabRoot = prefabSource->CreateEntity("Watched prefab");
		prefabRoot.AddComponent<TomCat::SpriteRenderer>().SpriteHandle = heroSprite;
		TomCat::PrefabArchive prefabArchive;
		std::string archiveError;
		Require(TomCat::PrefabArchiveCodec::CaptureSubtree(prefabSource, prefabRoot,
			prefabArchive, archiveError), "could not capture dependency Prefab");
		std::string prefabDocument;
		Require(TomCat::PrefabArchiveCodec::Encode(prefabArchive, prefabDocument,
			archiveError), "could not encode dependency Prefab");
		const std::filesystem::path prefabPath = project.Assets / "watched.tcprefab";
		WriteBytes(prefabPath, prefabDocument);
		const TomCat::AssetHandle prefab = RequireHandle(registry, prefabPath,
			TomCat::AssetType::Prefab);

		auto sceneSource = TomCat::CreateRef<TomCat::Scene>();
		TomCat::Entity sceneEntity = sceneSource->CreateEntity("Watched scene");
		TomCat::CSharpScriptEntry attachment;
		attachment.LastKnownClassName = "DependencyProbe";
		attachment.Fields.emplace_back("11111111111111111111111111111111",
			"Template", TomCat::ScriptFieldType::AssetRef,
			static_cast<uint64_t>(prefab), "TomCat.PrefabAsset");
		sceneEntity.AddComponent<TomCat::CSharpScripts>().Scripts.push_back(
			std::move(attachment));
		std::string sceneDocument;
		Require(TomCat::SceneSerializer(sceneSource).SerializeDocument(sceneDocument,
			archiveError), "could not encode dependency Scene");
		const std::filesystem::path scenePath = project.Assets / "watched.tomcat";
		WriteBytes(scenePath, sceneDocument);
		const TomCat::AssetHandle scene = RequireHandle(registry, scenePath,
			TomCat::AssetType::Scene);
		Require(database.RefreshRegistry(),
			"automatic dependency discovery failed to refresh the registry");
		Require(database.GetDependencies(prefab) ==
			std::vector<TomCat::AssetHandle>{ hero },
			"Prefab Sprite child dependency was not normalized to the atlas parent");
		Require(database.GetDependencies(scene) ==
			std::vector<TomCat::AssetHandle>{ prefab },
			"Scene Prefab dependency was not discovered by AssetReferenceVisitor");
		const std::vector<TomCat::AssetHandle> transitive =
			database.GetDependents(hero, true);
		Require(std::find(transitive.begin(), transitive.end(), prefab) != transitive.end()
			&& std::find(transitive.begin(), transitive.end(), scene) != transitive.end(),
			"transitive reverse dependencies were not built automatically");
		const std::filesystem::path dependencyCache =
			project.Library / "AssetDependencies.yaml";
		Require(std::filesystem::is_regular_file(dependencyCache)
			&& ReadText(dependencyCache).find(std::to_string(
				static_cast<uint64_t>(scene))) != std::string::npos,
			"dependency graph was not persisted as a rebuildable Library cache");

		// Recreate both registry and database. The graph must be immediately usable
		// without the test manually calling SetDependencies for archive references.
		database.Shutdown();
		registry.Shutdown();
		Require(registry.Initialize(project.Assets, project.Library),
			"monitor registry restart failed");
		Require(database.Initialize(registry, project.Library),
			"monitor database restart failed");
		Require(database.GetImporters().Register(counting, true),
			"monitor counting importer restart registration failed");
		Require(RequireHeroSubAsset(registry, hero) == heroSprite
			&& database.GetDependencies(prefab) ==
				std::vector<TomCat::AssetHandle>{ hero }
			&& database.GetDependencies(scene) ==
				std::vector<TomCat::AssetHandle>{ prefab },
			"restart did not restore and rebuild the automatic dependency graph");

		// The Library cache is disposable. A clean database must recover the same
		// graph from source archives and stable .tcmeta child identities alone.
		database.Shutdown();
		registry.Shutdown();
		std::error_code libraryError;
		std::filesystem::remove_all(project.Library, libraryError);
		Require(!libraryError && registry.Initialize(project.Assets, project.Library),
			"registry did not rebuild after deleting Library");
		Require(database.Initialize(registry, project.Library),
			"database did not rebuild after deleting Library");
		Require(database.GetImporters().Register(counting, true),
			"counting importer registration after Library rebuild failed");
		Require(database.GetDependencies(prefab) ==
				std::vector<TomCat::AssetHandle>{ hero }
			&& database.GetDependencies(scene) ==
				std::vector<TomCat::AssetHandle>{ prefab },
			"source archives did not rebuild dependencies after deleting Library");
		Require(database.SetDependencies(hero, { dependency }),
			"monitor dependency graph could not restore its explicit importer edge");

		TomCat::AssetImportCoordinatorOptions options;
		options.PollInterval = std::chrono::milliseconds(12);
		options.Debounce = std::chrono::milliseconds(55);
		TomCat::AssetImportCoordinator coordinator;
		Require(coordinator.Initialize(registry, database, project.Assets, options) &&
			coordinator.Start(), "asset import coordinator did not start");

		const std::thread::id mainThread = std::this_thread::get_id();
		std::vector<TomCat::AssetImportEvent> events;
		auto receive = [&](const TomCat::AssetImportEvent& event)
		{
			Require(std::this_thread::get_id() == mainThread,
				"import completion callback escaped the main thread");
			events.push_back(event);
		};
		auto waitFor = [&](const std::function<bool()>& predicate,
			const char* failure)
		{
			const auto deadline = std::chrono::steady_clock::now() +
				std::chrono::seconds(4);
			while (std::chrono::steady_clock::now() < deadline)
			{
				(void)coordinator.PumpMainThread(receive);
				if (predicate())
					return;
				std::this_thread::sleep_for(std::chrono::milliseconds(5));
			}
			throw std::runtime_error(failure);
		};
		auto matchingEvent = [&](size_t first, TomCat::AssetHandle handle,
			bool dependencyEvent, TomCat::AssetFileChangeKind kind,
			bool requireSuccess)
		{
			for (size_t index = first; index < events.size(); ++index)
			{
				const TomCat::AssetImportEvent& event = events[index];
				if (event.Handle == handle && event.IsDependency == dependencyEvent &&
					event.Change == kind &&
					(!requireSuccess || event.Result.Succeeded()))
					return true;
			}
			return false;
		};
		auto waitForIdle = [&]()
		{
			waitFor([&]() { return coordinator.GetPendingImportCount() == 0; },
				"asset import coordinator did not become idle");
		};

		size_t first = events.size();
		uint32_t invocations = counting->Invocations.load();
		WriteBytes(heroPath, "watch-content-one");
		coordinator.RequestScan();
		waitFor([&]()
		{
			return matchingEvent(first, hero, false,
					TomCat::AssetFileChangeKind::Modified, true)
				&& matchingEvent(first, prefab, true,
					TomCat::AssetFileChangeKind::Modified, true)
				&& matchingEvent(first, scene, true,
					TomCat::AssetFileChangeKind::Modified, true);
		}, "content change was not imported by the monitor");
		Require(counting->Invocations == invocations + 1,
			"atlas plus automatic archive dependents imported the atlas more than once");
		waitForIdle();

		// A second file can change while the first file's import is still running.
		// The newer batch must neither block the main-thread pump nor discard the
		// unrelated first completion.
		first = events.size();
		invocations = counting->Invocations.load();
		counting->DelayMilliseconds = 250;
		WriteBytes(heroPath, "overlap-hero");
		coordinator.RequestScan();
		const auto startedDeadline = std::chrono::steady_clock::now()
			+ std::chrono::seconds(4);
		while (counting->Invocations.load() < invocations + 1
			&& std::chrono::steady_clock::now() < startedDeadline)
		{
			(void)coordinator.PumpMainThread(receive);
			std::this_thread::sleep_for(std::chrono::milliseconds(5));
		}
		Require(counting->Invocations.load() >= invocations + 1,
			"first overlapping import did not start");
		WriteBytes(secondaryPath, "overlap-secondary");
		coordinator.RequestScan();
		waitFor([&]()
		{
			return matchingEvent(first, hero, false,
					TomCat::AssetFileChangeKind::Modified, true)
				&& matchingEvent(first, secondary, false,
					TomCat::AssetFileChangeKind::Modified, true);
		}, "overlapping changes did not publish both independent imports");
		Require(counting->Invocations == invocations + 2,
			"overlapping changes did not execute exactly two imports");
		waitForIdle();

		// Repeated saves of the same file while its importer is blocked retain
		// only the newest replacement. The first worker is cancelled and allowed
		// to unwind before the final worker starts, bounding per-asset concurrency.
		first = events.size();
		invocations = counting->Invocations.load();
		counting->DelayMilliseconds = 350;
		counting->MaxConcurrentInvocations = 0;
		WriteBytes(heroPath, "running-first");
		coordinator.RequestScan();
		const auto runningStartedDeadline = std::chrono::steady_clock::now()
			+ std::chrono::seconds(4);
		while (counting->Invocations.load() < invocations + 1
			&& std::chrono::steady_clock::now() < runningStartedDeadline)
		{
			(void)coordinator.PumpMainThread(receive);
			std::this_thread::sleep_for(std::chrono::milliseconds(5));
		}
		Require(counting->Invocations.load() == invocations + 1,
			"same-handle import did not start exactly one worker");
		auto pumpFor = [&](std::chrono::milliseconds duration)
		{
			const auto deadline = std::chrono::steady_clock::now() + duration;
			while (std::chrono::steady_clock::now() < deadline)
			{
				(void)coordinator.PumpMainThread(receive);
				std::this_thread::sleep_for(std::chrono::milliseconds(5));
			}
		};
		WriteBytes(heroPath, "running-second");
		coordinator.RequestScan();
		pumpFor(std::chrono::milliseconds(80));
		WriteBytes(heroPath, "running-final");
		coordinator.RequestScan();
		pumpFor(std::chrono::milliseconds(80));
		Require(counting->Invocations.load() == invocations + 1,
			"same-handle saves started concurrent obsolete import workers");
		Require(counting->MaxConcurrentInvocations.load() == 1,
			"dependency recursion bypassed the per-handle import concurrency bound");
		waitFor([&]()
		{
			return matchingEvent(first, hero, false,
				TomCat::AssetFileChangeKind::Modified, true);
		}, "latest same-handle replacement import was not published");
		Require(counting->Invocations.load() == invocations + 2,
			"same-handle saves were not reduced to first plus latest import");
		Require(counting->MaxConcurrentInvocations.load() == 1,
			"first and replacement imports overlapped for one asset handle");
		const auto replacementEvent = std::find_if(events.rbegin(), events.rend(),
			[hero](const TomCat::AssetImportEvent& event)
			{
				return event.Handle == hero && event.Result.Succeeded();
			});
		Require(replacementEvent != events.rend()
			&& std::string(replacementEvent->Result.Artifact.Bytes.begin(),
				replacementEvent->Result.Artifact.Bytes.end()).find("running-final") == 0,
			"same-handle replacement did not import the latest source bytes");
		waitForIdle();
		counting->DelayMilliseconds = 15;

		first = events.size();
		invocations = counting->Invocations.load();
		WriteBytes(heroPath, "rapid-one");
		coordinator.RequestScan();
		std::this_thread::sleep_for(std::chrono::milliseconds(22));
		WriteBytes(heroPath, "rapid-two");
		coordinator.RequestScan();
		std::this_thread::sleep_for(std::chrono::milliseconds(22));
		WriteBytes(heroPath, "rapid-final");
		coordinator.RequestScan();
		waitFor([&]()
		{
			return matchingEvent(first, hero, false,
				TomCat::AssetFileChangeKind::Modified, true);
		}, "debounced rapid write was not imported");
		Require(counting->Invocations == invocations + 1,
			"rapid writes were not coalesced into one import");
		const auto rapidEvent = std::find_if(
			events.begin() + static_cast<std::ptrdiff_t>(first), events.end(),
			[hero](const TomCat::AssetImportEvent& event)
			{
				return event.Handle == hero && !event.IsDependency
					&& event.Result.Succeeded();
			});
		Require(rapidEvent != events.end()
			&& std::string(rapidEvent->Result.Artifact.Bytes.begin(),
				rapidEvent->Result.Artifact.Bytes.end()).find("rapid-final") == 0,
			"coalesced import did not use the latest bytes");
		waitForIdle();

		first = events.size();
		invocations = counting->Invocations.load();
		std::error_code timeError;
		const auto oldWriteTime = std::filesystem::last_write_time(heroPath, timeError);
		Require(!timeError, "could not read source mtime");
		std::filesystem::last_write_time(heroPath,
			oldWriteTime + std::chrono::seconds(2), timeError);
		Require(!timeError, "could not touch source mtime");
		coordinator.RequestScan();
		const auto quietDeadline = std::chrono::steady_clock::now() +
			std::chrono::milliseconds(180);
		while (std::chrono::steady_clock::now() < quietDeadline)
		{
			(void)coordinator.PumpMainThread(receive);
			std::this_thread::sleep_for(std::chrono::milliseconds(5));
		}
		Require(events.size() == first && counting->Invocations == invocations,
			"mtime-only change incorrectly triggered an import");

		first = events.size();
		invocations = counting->Invocations.load();
		const std::filesystem::path heroMeta =
			TomCat::AssetRegistry::GetMetadataPath(heroPath);
		std::string metadata = ReadText(heroMeta);
		const size_t settingOffset = metadata.find("watch-before");
		Require(settingOffset != std::string::npos,
			"could not locate test setting in tcmeta");
		metadata.replace(settingOffset, std::string("watch-before").size(),
			"watch-after");
		WriteBytes(heroMeta, metadata);
		coordinator.RequestScan();
		waitFor([&]()
		{
			return matchingEvent(first, hero, false,
				TomCat::AssetFileChangeKind::Modified, true);
		}, "external tcmeta settings change was not imported");
		Require(counting->Invocations == invocations + 1,
			"tcmeta settings change did not invalidate exactly one artifact");
		waitForIdle();

		first = events.size();
		invocations = counting->Invocations.load();
		WriteBytes(dependencyPath, "shader-watched-change");
		coordinator.RequestScan();
		waitFor([&]()
		{
			return matchingEvent(first, hero, true,
				TomCat::AssetFileChangeKind::Modified, true);
		}, "dependency change did not reimport its transitive dependent");
		Require(counting->Invocations == invocations + 1,
			"dependency propagation did not execute the dependent importer once");
		waitForIdle();

		first = events.size();
		const std::filesystem::path addedPath = project.Assets / "added.png";
		WriteBytes(addedPath, "new-watched-asset");
		coordinator.RequestScan();
		waitFor([&]()
		{
			return std::any_of(events.begin() + static_cast<std::ptrdiff_t>(first),
				events.end(), [&](const TomCat::AssetImportEvent& event)
				{
					return event.FilePath == std::filesystem::path("added.png") &&
						event.Result.Succeeded() &&
						static_cast<uint64_t>(event.Handle) != 0;
				});
		}, "new file was not registered and imported");
		TomCat::AssetHandle addedHandle(0);
		for (size_t index = first; index < events.size(); ++index)
		{
			if (events[index].FilePath == std::filesystem::path("added.png") &&
				events[index].Result.Succeeded())
				addedHandle = events[index].Handle;
		}
		Require(static_cast<uint64_t>(addedHandle) != 0,
			"new file completion did not expose its stable handle");

		first = events.size();
		std::error_code removeError;
		Require(std::filesystem::remove(addedPath, removeError) && !removeError,
			"could not remove monitored source");
		coordinator.RequestScan();
		waitFor([&]()
		{
			return matchingEvent(first, addedHandle, false,
				TomCat::AssetFileChangeKind::Removed, false);
		}, "deleted source did not publish a tombstone event");
		const TomCat::AssetMetadata* missing = registry.GetMetadata(addedHandle);
		Require(missing && missing->IsMissing,
			"deleted source did not leave its sidecar identity as missing");

		first = events.size();
		const std::filesystem::path renamedPath = project.Assets / "renamed.png";
		const std::filesystem::path renamedMeta =
			TomCat::AssetRegistry::GetMetadataPath(renamedPath);
		std::filesystem::rename(heroPath, renamedPath);
		std::filesystem::rename(heroMeta, renamedMeta);
		coordinator.RequestScan();
		waitFor([&]()
		{
			return matchingEvent(first, hero, false,
				TomCat::AssetFileChangeKind::Added, true);
		}, "source plus sidecar rename was not reimported");
		const TomCat::AssetMetadata* moved = registry.GetMetadata(hero);
		Require(moved && !moved->IsMissing &&
			moved->FilePath == std::filesystem::path("renamed.png"),
			"source plus sidecar rename did not preserve its handle");
		Require(!matchingEvent(first, hero, false,
			TomCat::AssetFileChangeKind::Removed, false),
			"handle-preserving rename emitted a false deletion");

		const size_t stoppedEventCount = events.size();
		coordinator.Stop();
		WriteBytes(renamedPath, "change-while-stopped");
		coordinator.RequestScan();
		std::this_thread::sleep_for(std::chrono::milliseconds(80));
		Require(coordinator.PumpMainThread(receive) == 0 &&
			events.size() == stoppedEventCount,
			"stopped coordinator published a late callback");
		std::atomic_bool raceRequestScan = true;
		std::thread scanObserver([&]()
		{
			while (raceRequestScan.load())
			{
				coordinator.RequestScan();
				std::this_thread::yield();
			}
		});
		coordinator.Shutdown();
		raceRequestScan = false;
		scanObserver.join();
		Require(!coordinator.IsInitialized()
			&& coordinator.GetPendingImportCount() == 0,
			"RequestScan/Shutdown race retained a stale request");
		Require(coordinator.Initialize(registry, database, project.Assets, options)
			&& coordinator.Start(),
			"coordinator did not restart after RequestScan/Shutdown race");
		std::this_thread::sleep_for(std::chrono::milliseconds(80));
		Require(coordinator.PumpMainThread(receive) == 0,
			"stale RequestScan escaped into a later coordinator initialization");
		coordinator.Shutdown();
		database.Shutdown();
		registry.Shutdown();
	}

}

int main()
{
	TomCat::Log::Init();
	try
	{
		TestMalformedFontPreflight();
		TestDeterministicImportPipeline();
		TestFileMonitorImportCoordinator();
		std::cout << "PASS deterministic importer, DDC, tcmeta v2, and monitored reimport\n";
		return 0;
	}
	catch (const std::exception& exception)
	{
		std::cerr << "FAIL importer regression: " << exception.what() << '\n';
		return 1;
	}
}
