#include "tcpch.h"
#include "SceneManager.h"

#include "SceneSerializer.h"
#include "Entity.h"
#include "Components.h"
#include "Serialization/ComponentCodecs.h"
#include "Serialization/PrefabLink.h"
#include "TomCat/Asset/AssetManager.h"
#include "TomCat/Asset/ContentHash.h"
#include "TomCat/Utils/PathUtils.h"

#include <algorithm>
#include <chrono>
#include <fstream>
#include <limits>
#include <unordered_set>
#include <utility>

namespace TomCat {

	SceneManager* SceneManager::s_Runtime = nullptr;

	SceneManager::SceneManager()
		: m_OwnerThread(std::this_thread::get_id())
	{
	}

	SceneManager::~SceneManager()
	{
		Stop();
		DeactivateRuntime();
	}

	bool SceneManager::CheckOwnerThread(const char* operation)
	{
		if (IsOwnerThread())
			return true;
		return Fail(std::string(operation ? operation : "SceneManager operation")
			+ " must run on the SceneManager owner thread");
	}

	bool SceneManager::Fail(std::string message)
	{
		m_LastError = std::move(message);
		TC_Core_Error("SceneManager: {0}", m_LastError);
		return false;
	}

	bool SceneManager::ActivateRuntime()
	{
		if (!CheckOwnerThread("ActivateRuntime"))
			return false;
		if (s_Runtime && s_Runtime != this)
			return Fail("another SceneManager is already bound to the runtime");
		s_Runtime = this;
		return true;
	}

	void SceneManager::DeactivateRuntime()
	{
		if (s_Runtime == this)
			s_Runtime = nullptr;
	}

	bool SceneManager::ConfigureCookedPackage()
	{
		if (!CheckOwnerThread("ConfigureCookedPackage"))
			return false;
		AssetManager& assets = AssetManager::Get();
		if (!assets.IsCookedPackageMounted())
			return Fail("ConfigureCookedPackage requires a mounted tcpak v5 package");

		return Configure(assets.GetCookedEntrySceneHandle(),
			assets.GetCookedBuildSceneHandles());
	}

	bool SceneManager::ConfigureBuildSettings(const BuildSettings& settings)
	{
		if (!CheckOwnerThread("ConfigureBuildSettings"))
			return false;
		std::vector<AssetHandle> enabledScenes;
		enabledScenes.reserve(settings.Scenes.size());
		for (const BuildSceneSettings& scene : settings.Scenes)
		{
			if (scene.Enabled)
				enabledScenes.push_back(scene.Handle);
		}
		return Configure(settings.EntrySceneHandle, std::move(enabledScenes));
	}

	bool SceneManager::Configure(AssetHandle entryScene,
		std::vector<AssetHandle> buildScenes)
	{
		if (m_ActiveScene || HasPendingTransition())
			return Fail("build-scene configuration cannot change while a Scene is active or pending");
		if (static_cast<uint64_t>(entryScene) == 0)
			return Fail("the build-scene configuration has no Entry Scene Handle");
		if (buildScenes.empty())
			return Fail("the build-scene configuration contains no enabled Scenes");
		if (buildScenes.size() > static_cast<size_t>((std::numeric_limits<uint32_t>::max)()))
			return Fail("the build-scene configuration exceeds the supported index range");

		std::unordered_set<uint64_t> unique;
		unique.reserve(buildScenes.size());
		bool entryFound = false;
		for (const AssetHandle handle : buildScenes)
		{
			const uint64_t rawHandle = static_cast<uint64_t>(handle);
			if (rawHandle == 0)
				return Fail("the enabled build-scene list contains handle 0");
			if (!unique.insert(rawHandle).second)
				return Fail("the enabled build-scene list contains duplicate handle "
					+ std::to_string(rawHandle));
			entryFound = entryFound || handle == entryScene;
		}
		if (!entryFound)
			return Fail("the Entry Scene is absent from the enabled build-scene list");

		m_EntrySceneHandle = entryScene;
		m_BuildSceneHandles = std::move(buildScenes);
		m_Physics2DSettings = AssetManager::Get().GetPhysics2DSettings();
		m_LastError.clear();
		return true;
	}

	std::optional<uint32_t> SceneManager::FindBuildIndex(AssetHandle scene) const
	{
		const auto iterator = std::find(m_BuildSceneHandles.begin(),
			m_BuildSceneHandles.end(), scene);
		if (iterator == m_BuildSceneHandles.end())
			return std::nullopt;
		return static_cast<uint32_t>(std::distance(m_BuildSceneHandles.begin(), iterator));
	}

	bool SceneManager::LoadEntryScene()
	{
		if (!CheckOwnerThread("LoadEntryScene"))
			return false;
		if (s_Runtime != this)
			return Fail("LoadEntryScene requires this SceneManager to be the active runtime owner");
		if (m_ActiveScene)
			return Fail("LoadEntryScene is only valid before the first runtime Scene starts");
		if (!RequestLoadScene(m_EntrySceneHandle))
			return false;
		return CommitPendingTransition();
	}

	bool SceneManager::StartPreparedScene(const Ref<Scene>& scene,
		AssetHandle sceneHandle)
	{
		if (!CheckOwnerThread("StartPreparedScene"))
			return false;
		if (s_Runtime != this)
			return Fail("StartPreparedScene requires this SceneManager to be the active runtime owner");
		if (m_ActiveScene || HasPendingTransition())
			return Fail("a prepared Scene can only start when no runtime Scene is active or pending");
		if (!scene)
			return Fail("cannot start a null prepared Scene");
		const std::optional<uint32_t> buildIndex = FindBuildIndex(sceneHandle);
		if (!buildIndex)
			return Fail("prepared Scene handle "
				+ std::to_string(static_cast<uint64_t>(sceneHandle))
				+ " is not in the enabled build-scene list");

		ApplyRuntimeConfiguration(scene);
		m_ActiveScene = scene;
		m_ActiveSceneHandle = sceneHandle;
		m_ActiveBuildIndex = static_cast<int32_t>(*buildIndex);
		ResetSceneOwnership();
		if (!scene->OnRuntimeStart())
		{
			ClearPendingTransition();
			m_ActiveScene.reset();
			m_ActiveSceneHandle = AssetHandle(0);
			m_ActiveBuildIndex = -1;
			ResetSceneOwnership();
			return Fail("failed to start prepared build Scene handle "
				+ std::to_string(static_cast<uint64_t>(sceneHandle)));
		}
		m_LastError.clear();
		TC_Core_Info("SceneManager started prepared build Scene [{0}] handle {1}",
			*buildIndex, static_cast<uint64_t>(sceneHandle));
		return true;
	}

	bool SceneManager::RequestLoadScene(AssetHandle scene, SceneLoadMode mode)
	{
		if (!CheckOwnerThread("RequestLoadScene"))
			return false;
		if (m_BuildSceneHandles.empty())
			return Fail("no build-scene configuration is loaded");
		const std::optional<uint32_t> buildIndex = FindBuildIndex(scene);
		if (!buildIndex)
			return Fail("Scene handle " + std::to_string(static_cast<uint64_t>(scene))
				+ " is not in the enabled build-scene list");
		return StageScene(scene, *buildIndex, mode);
	}

	bool SceneManager::RequestLoadScene(uint32_t buildIndex, SceneLoadMode mode)
	{
		if (!CheckOwnerThread("RequestLoadScene"))
			return false;
		if (buildIndex >= m_BuildSceneHandles.size())
			return Fail("build Scene index " + std::to_string(buildIndex)
				+ " is outside the enabled build-scene list");
		return StageScene(m_BuildSceneHandles[buildIndex], buildIndex, mode);
	}

	bool SceneManager::RequestReload()
	{
		if (!CheckOwnerThread("RequestReload"))
			return false;
		if (!m_ActiveScene || static_cast<uint64_t>(m_ActiveSceneHandle) == 0)
			return Fail("there is no active Scene to reload");
		if (m_ActiveBuildIndex < 0)
			return Fail("the active Scene has no enabled build index");
		return StageScene(m_ActiveSceneHandle,
			static_cast<uint32_t>(m_ActiveBuildIndex), SceneLoadMode::Single);
	}

	bool SceneManager::StageScene(AssetHandle scene, uint32_t buildIndex, SceneLoadMode mode)
	{
		if (m_Stopping || m_Committing)
			return Fail("a Scene transition cannot be requested during scene activation or teardown");
		if (mode != SceneLoadMode::Single && mode != SceneLoadMode::Additive)
			return Fail("unknown Scene load mode");
		if (mode == SceneLoadMode::Additive && std::find(m_LoadedScenes.begin(), m_LoadedScenes.end(), scene) != m_LoadedScenes.end())
			return Fail("the additive Scene is already loaded");
		AssetManager& assets = AssetManager::Get();
		AssetType type = AssetType::None;
		std::vector<uint8_t> bytes;
		if (!assets.ReadAssetBytes(scene, bytes, &type))
			return Fail("could not read build Scene handle "
				+ std::to_string(static_cast<uint64_t>(scene)));
		if (type != AssetType::Scene)
			return Fail("build Scene handle " + std::to_string(static_cast<uint64_t>(scene))
				+ " resolves to " + AssetTypeToString(type) + " instead of Scene");

		return StageBytes(std::move(bytes), scene, buildIndex, mode);
	}

	bool SceneManager::StageBytes(std::vector<uint8_t> bytes, AssetHandle scene,
		uint32_t buildIndex, SceneLoadMode mode)
	{
		const std::filesystem::path diagnosticPath = UTF8ToPath(
			"BuildScene-" + std::to_string(static_cast<uint64_t>(scene)));
		Ref<Scene> staged = CreateRef<Scene>();
		SceneSerializer serializer(staged);
		const bool deserialized = serializer.DeserializeDocument(bytes, diagnosticPath, true);
		if (!deserialized)
			return Fail("build Scene handle " + std::to_string(static_cast<uint64_t>(scene))
				+ " could not be fully deserialized");
		bool changed = false;
		std::string prefabError;
		if (!PrefabLinkedInstance::RefreshAll(staged, changed, prefabError))
			return Fail("could not update linked Scene Prefabs: " + prefabError);

		// Replacing an already-staged request is safe: neither Scene has entered
		// runtime and the current Scene remains alive throughout this function.
		m_PendingScene = std::move(staged);
		m_PendingSceneHandle = scene;
		m_PendingBuildIndex = static_cast<int32_t>(buildIndex);
		m_PendingLoadMode = mode;
		if (m_AsyncState) m_AsyncState->Cancelled = true;
		m_AsyncRead = {};
		m_AsyncState.reset();
		m_LoadState = SceneLoadState::Ready;
		m_LastError.clear();
		return true;
	}

	bool SceneManager::RequestLoadSceneAsync(uint32_t buildIndex, SceneLoadMode mode)
	{
		if (!CheckOwnerThread("RequestLoadSceneAsync")) return false;
		if (buildIndex >= m_BuildSceneHandles.size()) return Fail("build Scene index is outside the enabled build-scene list");
		return RequestLoadSceneAsync(m_BuildSceneHandles[buildIndex], mode);
	}

	bool SceneManager::RequestLoadSceneAsync(AssetHandle scene, SceneLoadMode mode)
	{
		if (!CheckOwnerThread("RequestLoadSceneAsync")) return false;
		if (m_Stopping || m_Committing || HasPendingTransition())
			return Fail("another Scene operation is pending; complete or cancel it first");
		const auto index = FindBuildIndex(scene);
		if (!index) return Fail("async Scene is not in the enabled build-scene list");
		if (mode != SceneLoadMode::Single && mode != SceneLoadMode::Additive) return Fail("unknown Scene load mode");
		if (mode == SceneLoadMode::Additive && std::find(m_LoadedScenes.begin(), m_LoadedScenes.end(), scene) != m_LoadedScenes.end())
			return Fail("the additive Scene is already loaded");

		// Capture immutable paths/ranges on the owner thread. Workers never touch
		// AssetManager, the ECS, the renderer, or a managed runtime.
		AssetManager& assets = AssetManager::Get();
		CookedAssetRange range;
		std::filesystem::path path;
		if (assets.IsCookedPackageMounted())
		{
			if (!assets.TryGetCookedAssetRange(scene, range) || range.Type != AssetType::Scene)
				return Fail("async Scene is missing from the mounted package");
			path = range.PackagePath;
		}
		else
		{
			const AssetMetadata* metadata = assets.GetRegistry().GetMetadata(scene);
			if (!metadata || metadata->Type != AssetType::Scene || metadata->IsMissing)
				return Fail("async Scene metadata is unavailable");
			path = assets.ResolvePath(scene);
			std::error_code error;
			range.Size = std::filesystem::file_size(path, error);
			if (error) return Fail("could not stat async Scene source");
		}
		if (range.Size > 512ULL * 1024ULL * 1024ULL)
			return Fail("async Scene exceeds the 512 MiB document limit");
		auto state = std::make_shared<AsyncReadState>();
		auto job = std::make_shared<std::packaged_task<AsyncReadResult()>>([path, range, state]()
		{
			AsyncReadResult result;
			try
			{
				std::ifstream input(path, std::ios::binary);
				input.seekg(static_cast<std::streamoff>(range.Offset));
				if (!input) { result.Error = "could not open async Scene source"; return result; }
				result.Bytes.resize(static_cast<size_t>(range.Size));
				for (size_t offset = 0; offset < result.Bytes.size();)
				{
					if (state->Cancelled) { result.Bytes.clear(); return result; }
					const size_t count = (std::min)(size_t{64 * 1024}, result.Bytes.size() - offset);
					if (!input.read(reinterpret_cast<char*>(result.Bytes.data() + offset), static_cast<std::streamsize>(count)))
					{ result.Error = "async Scene read was truncated"; result.Bytes.clear(); return result; }
					offset += count;
					state->Progress = 0.8f * static_cast<float>(offset) / static_cast<float>(range.Size);
				}
				if (range.HasSHA256Digest && !VerifyContentSHA256(result.Bytes, range.SHA256Digest))
				{ result.Error = "async Scene package digest mismatch"; result.Bytes.clear(); return result; }
				state->Progress = 0.9f;
			}
			catch (const std::exception& error) { result.Error = error.what(); result.Bytes.clear(); }
			return result;
		});
		auto future = job->get_future();
		if (!AssetJobSystem::Get().TrySchedule(range.Size, [job]() { (*job)(); }))
			return Fail("Scene loader queue is full; retry on a later frame");
		m_AsyncState = std::move(state);
		m_AsyncRead = std::move(future);
		m_PendingSceneHandle = scene;
		m_PendingBuildIndex = static_cast<int32_t>(*index);
		m_PendingLoadMode = mode;
		m_LoadState = SceneLoadState::Reading;
		m_LastError.clear();
		return true;
	}

	float SceneManager::GetLoadProgress() const
	{
		if (m_LoadState == SceneLoadState::Completed) return 1.0f;
		if (m_LoadState == SceneLoadState::Ready) return 0.9f;
		return m_AsyncState ? m_AsyncState->Progress.load() : 0.0f;
	}

	bool SceneManager::CancelPendingLoad()
	{
		if (!CheckOwnerThread("CancelPendingLoad") || m_Committing) return false;
		if (!m_PendingScene && !m_AsyncRead.valid()) return false;
		ClearPendingTransition();
		m_LoadState = SceneLoadState::Cancelled;
		return true;
	}

	void SceneManager::ApplyRuntimeConfiguration(const Ref<Scene>& scene) const
	{
		if (!scene)
			return;
		scene->SetPhysics2DSettings(m_Physics2DSettings);
		scene->SetRuntimeUIViewportMetrics(m_RuntimeUIViewportOrigin,
			m_RuntimeUIDPIScale, m_RuntimeUIScreenToFramebufferScale);
		if (m_ViewportWidth > 0 && m_ViewportHeight > 0)
			scene->OnViewportResize(m_ViewportWidth, m_ViewportHeight);
	}

	void SceneManager::ClearPendingTransition()
	{
		if (m_AsyncState) m_AsyncState->Cancelled = true;
		m_AsyncRead = {};
		m_AsyncState.reset();
		m_PendingScene.reset();
		m_PendingSceneHandle = AssetHandle(0);
		m_PendingBuildIndex = -1;
	}

	bool SceneManager::CommitPendingTransition()
	{
		if (!CheckOwnerThread("CommitPendingTransition"))
			return false;
		if (m_Committing || m_Stopping) return Fail("recursive Scene transition commit is not allowed");
		if (s_Runtime != this && HasPendingTransition())
			return Fail("CommitPendingTransition requires this SceneManager to be the active runtime owner");
		if (m_AsyncRead.valid())
		{
			if (m_AsyncRead.wait_for(std::chrono::seconds(0)) != std::future_status::ready)
				return true;
			AsyncReadResult read = m_AsyncRead.get();
			if (!read.Error.empty())
			{
				ClearPendingTransition();
				m_LoadState = SceneLoadState::Failed;
				return Fail(std::move(read.Error));
			}
			if (!StageBytes(std::move(read.Bytes), m_PendingSceneHandle,
				static_cast<uint32_t>(m_PendingBuildIndex), m_PendingLoadMode))
			{
				ClearPendingTransition();
				m_LoadState = SceneLoadState::Failed;
				return false;
			}
		}
		if (!m_PendingUnloads.empty())
		{
			auto unloads = std::move(m_PendingUnloads);
			m_PendingUnloads.clear();
			for (AssetHandle handle : unloads)
				if (!UnloadSceneNow(handle)) return false;
		}
		if (!m_PendingScene)
			return true;
		if (!m_AllowSceneActivation) return true;

		Ref<Scene> nextScene = std::move(m_PendingScene);
		const AssetHandle nextHandle = m_PendingSceneHandle;
		const int32_t nextBuildIndex = m_PendingBuildIndex;
		const SceneLoadMode mode = m_PendingLoadMode;
		m_PendingSceneHandle = AssetHandle(0);
		m_PendingBuildIndex = -1;
		// Keep the live world when it contains persistent roots or another loaded
		// scene. This preserves managed fields, audio voices and physics state.
		ReconcileEntityOwnership();
		if (m_ActiveScene && (mode == SceneLoadMode::Additive || !m_PersistentRoots.empty()))
		{
			m_Committing = true;
			const bool result = CommitComposedScene(nextScene, nextHandle,
				static_cast<uint32_t>(nextBuildIndex), mode);
			m_Committing = false;
			if (s_Runtime == this)
				m_LoadState = result ? SceneLoadState::Completed : SceneLoadState::Failed;
			return result;
		}

		Ref<Scene> previousScene = m_ActiveScene;
		const AssetHandle previousHandle = m_ActiveSceneHandle;
		const int32_t previousBuildIndex = m_ActiveBuildIndex;
		const auto previousLoadedScenes = m_LoadedScenes;
		const auto previousOwners = m_EntityOwners;
		if (previousScene && previousScene->IsRuntimeRunning())
			previousScene->OnRuntimeStop();

		ApplyRuntimeConfiguration(nextScene);
		m_ActiveScene = nextScene;
		m_ActiveSceneHandle = nextHandle;
		m_ActiveBuildIndex = nextBuildIndex;
		ResetSceneOwnership();
		if (!nextScene->OnRuntimeStart())
		{
			// Discard requests issued by lifecycle callbacks belonging to the failed
			// transition. A successfully restored Scene may enqueue a fresh request.
			ClearPendingTransition();
			m_ActiveScene.reset();
			m_ActiveSceneHandle = AssetHandle(0);
			m_ActiveBuildIndex = -1;
			m_LoadState = SceneLoadState::Failed;
			ResetSceneOwnership();
			std::string failure = "failed to start build Scene handle "
				+ std::to_string(static_cast<uint64_t>(nextHandle));

			// Startup is the only step that can fail after the old runtime stops.
			// Keep the old Scene object until this point and make one deterministic
			// rollback attempt so a bad target script need not terminate the Player.
			if (previousScene)
			{
				ApplyRuntimeConfiguration(previousScene);
				m_ActiveScene = previousScene;
				m_ActiveSceneHandle = previousHandle;
				m_ActiveBuildIndex = previousBuildIndex;
				m_LoadedScenes = previousLoadedScenes;
				m_EntityOwners = previousOwners;
				if (previousScene->OnRuntimeStart())
					return Fail(failure + "; the previous Scene was restored");

				ClearPendingTransition();
				m_ActiveScene.reset();
				m_ActiveSceneHandle = AssetHandle(0);
				m_ActiveBuildIndex = -1;
				ResetSceneOwnership();
				failure += "; the previous Scene also failed to restart";
			}
			return Fail(std::move(failure));
		}

		m_LastError.clear();
		m_LoadState = SceneLoadState::Completed;
		TC_Core_Info("SceneManager committed build Scene [{0}] handle {1}",
			nextBuildIndex, static_cast<uint64_t>(nextHandle));
		return true;
	}

	void SceneManager::ResetSceneOwnership()
	{
		m_LoadedScenes.clear();
		m_EntityOwners.clear();
		m_PersistentRoots.clear();
		if (!m_ActiveScene) return;
		m_LoadedScenes.push_back(m_ActiveSceneHandle);
		for (UUID id : m_ActiveScene->m_EntityOrder) m_EntityOwners[id] = m_ActiveSceneHandle;
	}

	void SceneManager::ReconcileEntityOwnership()
	{
		if (!m_ActiveScene) return;
		std::erase_if(m_EntityOwners, [&](const auto& entry) { return !m_ActiveScene->FindEntityByUUID(entry.first); });
		std::erase_if(m_PersistentRoots, [&](UUID id) { return !m_ActiveScene->FindEntityByUUID(id); });
		for (UUID id : m_ActiveScene->m_EntityOrder)
		{
			if (m_EntityOwners.contains(id)) continue;
			Entity parent = m_ActiveScene->GetParent(m_ActiveScene->FindEntityByUUID(id));
			while (parent && !m_EntityOwners.contains(parent.GetUUID())) parent = m_ActiveScene->GetParent(parent);
			m_EntityOwners[id] = parent ? m_EntityOwners.at(parent.GetUUID()) : m_ActiveSceneHandle;
		}
	}

	bool SceneManager::SetEntityPersistent(Entity entity, bool persistent)
	{
		if (!CheckOwnerThread("SetEntityPersistent")) return false;
		if (!entity || entity.GetScene() != m_ActiveScene.get()) return Fail("persistent Entity must belong to the runtime world");
		if (persistent && m_ActiveScene->GetParent(entity)) return Fail("only a root Entity can be marked persistent");
		if (persistent) m_PersistentRoots.emplace(entity.GetUUID());
		else
		{
			m_PersistentRoots.erase(entity.GetUUID());
			std::vector<UUID> descendants{entity.GetUUID()};
			for (size_t index = 0; index < descendants.size(); ++index)
			{
				m_EntityOwners[descendants[index]] = m_ActiveSceneHandle;
				for (UUID child : m_ActiveScene->GetChildrenUUIDs(m_ActiveScene->FindEntityByUUID(descendants[index]))) descendants.push_back(child);
			}
		}
		return true;
	}

	bool SceneManager::IsEntityPersistent(Entity entity) const
	{
		if (!entity || entity.GetScene() != m_ActiveScene.get()) return false;
		while (entity)
		{
			if (m_PersistentRoots.contains(entity.GetUUID())) return true;
			entity = m_ActiveScene->GetParent(entity);
		}
		return false;
	}

	bool SceneManager::SetActiveScene(AssetHandle scene)
	{
		if (!CheckOwnerThread("SetActiveScene") || m_Committing || m_Stopping) return false;
		if (std::find(m_LoadedScenes.begin(), m_LoadedScenes.end(), scene) == m_LoadedScenes.end())
			return Fail("active Scene must already be loaded");
		ReconcileEntityOwnership();
		m_ActiveSceneHandle = scene;
		m_ActiveBuildIndex = static_cast<int32_t>(*FindBuildIndex(scene));
		return true;
	}

	bool SceneManager::RequestUnloadScene(AssetHandle scene)
	{
		if (!CheckOwnerThread("RequestUnloadScene") || m_Committing || m_Stopping) return false;
		if (HasPendingTransition()) return Fail("complete the pending Scene operation before unloading");
		if (m_LoadedScenes.size() <= 1) return Fail("cannot unload the last loaded Scene; load a replacement or stop the runtime");
		if (std::find(m_LoadedScenes.begin(), m_LoadedScenes.end(), scene) == m_LoadedScenes.end())
			return Fail("cannot unload a Scene that is not loaded");
		m_PendingUnloads.push_back(scene);
		return true;
	}

	bool SceneManager::UnloadSceneNow(AssetHandle handle)
	{
		ReconcileEntityOwnership();
		std::unordered_set<UUID> removed;
		for (const auto& [id, owner] : m_EntityOwners)
			if (owner == handle && !IsEntityPersistent(m_ActiveScene->FindEntityByUUID(id))) removed.emplace(id);
		// Reparented cross-scene objects and persistent roots must not be deleted
		// recursively with an unloading parent. Preserve their world transforms.
		for (UUID id : m_ActiveScene->m_EntityOrder)
		{
			if (removed.contains(id)) continue;
			Entity entity = m_ActiveScene->FindEntityByUUID(id);
			Entity parent = m_ActiveScene->GetParent(entity);
			if (parent && removed.contains(parent.GetUUID()) && !m_ActiveScene->SetParent(entity, {}))
				return Fail("could not detach a surviving Entity before unloading its parent Scene");
		}
		const bool wasCommitting = m_Committing;
		m_Committing = true;
		for (UUID id : removed)
			if (Entity entity = m_ActiveScene->FindEntityByUUID(id)) m_ActiveScene->DestroyEntity(entity);
		m_Committing = wasCommitting;
		std::erase(m_LoadedScenes, handle);
		if (m_ActiveSceneHandle == handle && !m_LoadedScenes.empty())
		{
			m_ActiveSceneHandle = m_LoadedScenes.front();
			m_ActiveBuildIndex = static_cast<int32_t>(*FindBuildIndex(m_ActiveSceneHandle));
		}
		ReconcileEntityOwnership();
		// Survivors belong to the persistent domain (handle zero), so later loading
		// the same asset cannot accidentally adopt and destroy them.
		for (auto& [id, owner] : m_EntityOwners) if (owner == handle) owner = AssetHandle(0);
		return true;
	}

	bool SceneManager::MergeScene(const Ref<Scene>& source, std::vector<UUID>& created, std::string& error)
	{
		auto staged = Scene::Copy(source);
		if (!staged) { error = "could not copy the staged Scene"; return false; }
		std::unordered_map<UUID, UUID> identities, attachments;
		std::unordered_set<uint64_t> usedAttachments;
		for (UUID id : m_ActiveScene->m_EntityOrder)
		{
			Entity entity = m_ActiveScene->FindEntityByUUID(id);
			if (entity.HasComponent<CSharpScripts>())
				for (const auto& script : entity.GetComponent<CSharpScripts>().Scripts) usedAttachments.emplace(static_cast<uint64_t>(script.AttachmentID));
		}
		std::unordered_set<UUID> generated;
		for (UUID id : staged->m_EntityOrder)
		{
			UUID mapped;
			while (static_cast<uint64_t>(mapped) == 0 || m_ActiveScene->FindEntityByUUID(mapped) || !generated.emplace(mapped).second) mapped = UUID();
			identities.emplace(id, mapped);
			Entity entity = staged->FindEntityByUUID(id);
			if (entity.HasComponent<CSharpScripts>())
				for (const auto& script : entity.GetComponent<CSharpScripts>().Scripts)
				{
					UUID attachment;
					while (static_cast<uint64_t>(attachment) == 0 || !usedAttachments.emplace(static_cast<uint64_t>(attachment)).second) attachment = UUID();
					attachments.emplace(script.AttachmentID, attachment);
				}
		}
		for (UUID id : staged->m_EntityOrder)
			if (!ComponentCodecs::RemapInstanceReferences(staged->FindEntityByUUID(id), identities,
				ComponentCodecs::MissingEntityReferencePolicy::Reject, usedAttachments, true, error, &attachments)) return false;
		auto rollback = [&]()
		{
			for (UUID id : created) if (Entity entity = m_ActiveScene->FindEntityByUUID(id)) m_ActiveScene->DestroyEntity(entity);
			created.clear();
		};
		for (UUID id : staged->m_EntityOrder)
		{
			Entity from = staged->FindEntityByUUID(id);
			Entity to = m_ActiveScene->CreateEntityWithUUID(identities.at(id), from.GetName());
			if (!to) { rollback(); error = "could not create Scene entity"; return false; }
			created.push_back(to.GetUUID());
			if (!ComponentCodecs::CopyAuthoringComponents(from, to, true, error)) { rollback(); return false; }
		}
		for (const auto& [child, parent] : staged->m_ParentMap)
			if (!m_ActiveScene->SetParent(m_ActiveScene->FindEntityByUUID(identities.at(child)), m_ActiveScene->FindEntityByUUID(identities.at(parent))))
			{ rollback(); error = "could not attach additive Scene hierarchy"; return false; }
		if (!m_ActiveScene->SyncTransformHierarchy()) { rollback(); error = "invalid additive Scene hierarchy"; return false; }
		return true;
	}

	bool SceneManager::CommitComposedScene(const Ref<Scene>& source, AssetHandle handle,
		uint32_t buildIndex, SceneLoadMode mode)
	{
		// Lifecycle callbacks can Stop the manager. Keep the world alive locally,
		// and never republish it if the callback explicitly relinquished ownership.
		const Ref<Scene> world = m_ActiveScene;
		const std::unordered_set<UUID> previousEntities(world->m_EntityOrder.begin(), world->m_EntityOrder.end());
		const auto previousOwners = m_EntityOwners;
		const auto previousPersistentRoots = m_PersistentRoots;
		std::vector<UUID> created;
		std::string error;
		if (!MergeScene(source, created, error)) return Fail(std::move(error));
		const auto previousLoaded = m_LoadedScenes;
		const AssetHandle previousActive = m_ActiveSceneHandle;
		const int32_t previousIndex = m_ActiveBuildIndex;
		// Publish ownership before OnCreate so persistent marking and new children
		// have a well-defined scene. Only tear down old content after bootstrap.
		if (mode == SceneLoadMode::Single)
		{
			m_ActiveSceneHandle = handle;
			m_ActiveBuildIndex = static_cast<int32_t>(buildIndex);
		}
		for (UUID id : created) m_EntityOwners[id] = handle;
		const uint64_t failureSerial = world->m_RuntimeEntityBatchFailureSerial;
		world->QueueRuntimeEntityBatchCreated(created);
		world->FlushPendingRuntimeEntityCreates();
		const bool originalBatchPending = std::any_of(created.begin(), created.end(), [&](UUID id)
		{
			return std::find(world->m_PendingRuntimeEntityCreates.begin(),
				world->m_PendingRuntimeEntityCreates.end(), id) != world->m_PendingRuntimeEntityCreates.end();
		});
		if (m_ActiveScene != world || !world->IsRuntimeRunning()
			|| failureSerial != world->m_RuntimeEntityBatchFailureSerial || originalBatchPending)
		{
			// Bootstrap may create unparented entities or attach children to existing
			// roots before it fails. Roll back the complete identity delta, including
			// identities produced by teardown callbacks, rather than only the asset's
			// original batch. Existing components/managed instances are not restored
			// from a snapshot; mutations to existing objects remain callback effects.
			bool rollbackComplete = false;
			for (uint32_t attempt = 0; attempt < 16; ++attempt)
			{
				std::vector<UUID> rollbackIDs;
				for (UUID id : world->m_EntityOrder)
					if (!previousEntities.contains(id)) rollbackIDs.push_back(id);
				if (rollbackIDs.empty()) { rollbackComplete = true; break; }
				// Do not recursively destroy an old entity reparented underneath a
				// newly created object by a startup callback.
				for (UUID id : previousEntities)
				{
					Entity entity = world->FindEntityByUUID(id);
					if (!entity) continue;
					Entity parent = world->GetParent(entity);
					if (parent && !previousEntities.contains(parent.GetUUID()))
						world->SetParent(entity, {});
				}
				for (auto it = rollbackIDs.rbegin(); it != rollbackIDs.rend(); ++it)
					if (Entity entity = world->FindEntityByUUID(*it)) world->DestroyEntity(entity);
			}
			rollbackComplete |= std::none_of(world->m_EntityOrder.begin(), world->m_EntityOrder.end(),
				[&](UUID id) { return !previousEntities.contains(id); });
			std::erase_if(world->m_PendingRuntimeEntityCreates, [&](UUID id)
			{
				return !previousEntities.contains(id) || !world->FindEntityByUUID(id);
			});
			if (!rollbackComplete)
			{
				// A teardown callback that endlessly creates new entities cannot be
				// rolled back safely while the runtime keeps executing user code.
				world->OnRuntimeStop();
				const auto remaining = world->m_EntityOrder;
				for (UUID id : remaining)
					if (!previousEntities.contains(id))
						if (Entity entity = world->FindEntityByUUID(id)) world->DestroyEntity(entity);
			}
			if (world->IsRuntimeRunning()) world->SynchronizeRuntimePhysicsDefinitions();
			if (m_ActiveScene == world)
			{
				m_ActiveSceneHandle = previousActive;
				m_ActiveBuildIndex = previousIndex;
				m_LoadedScenes = previousLoaded;
				m_EntityOwners = previousOwners;
				m_PersistentRoots = previousPersistentRoots;
				ReconcileEntityOwnership();
			}
			return Fail(world->IsRuntimeRunning()
				? "could not initialize the additive Scene entity batch; newly created entities were rolled back"
				: "runtime stopped while starting additive Scene scripts; newly created entities were rolled back");
		}
		created.clear();
		for (UUID id : m_ActiveScene->m_EntityOrder) if (!previousEntities.contains(id)) created.push_back(id);
		if (mode == SceneLoadMode::Single)
		{
			// Reloading the same asset needs old-vs-new identities, not just handles.
			for (UUID id : created) m_EntityOwners[id] = AssetHandle(0);
			for (AssetHandle old : previousLoaded) if (!UnloadSceneNow(old)) return false;
			for (UUID id : created) if (m_ActiveScene->FindEntityByUUID(id)) m_EntityOwners[id] = handle;
			m_ActiveSceneHandle = handle;
			m_ActiveBuildIndex = static_cast<int32_t>(buildIndex);
			m_ActiveScene->SetSceneName(source->GetSceneName());
		}
		m_LoadedScenes.push_back(handle);
		ApplyRuntimeConfiguration(m_ActiveScene);
		m_LastError.clear();
		return true;
	}

	void SceneManager::SetViewportSize(uint32_t width, uint32_t height)
	{
		if (!CheckOwnerThread("SetViewportSize"))
			return;
		m_ViewportWidth = width;
		m_ViewportHeight = height;
		if (m_ActiveScene && width > 0 && height > 0)
			m_ActiveScene->OnViewportResize(width, height);
	}

	void SceneManager::SetRuntimeUIViewportMetrics(const glm::vec2& screenOrigin,
		float dpiScale, const glm::vec2& screenToFramebufferScale)
	{
		if (!CheckOwnerThread("SetRuntimeUIViewportMetrics"))
			return;
		m_RuntimeUIViewportOrigin = screenOrigin;
		m_RuntimeUIDPIScale = dpiScale;
		m_RuntimeUIScreenToFramebufferScale = screenToFramebufferScale;
		if (m_ActiveScene)
			m_ActiveScene->SetRuntimeUIViewportMetrics(screenOrigin, dpiScale,
				screenToFramebufferScale);
	}

	void SceneManager::Stop()
	{
		m_Stopping = true;
		ClearPendingTransition();
		m_PendingUnloads.clear();
		if (m_ActiveScene && m_ActiveScene->IsRuntimeRunning())
			m_ActiveScene->OnRuntimeStop();
		// OnDisable/OnDestroy callbacks execute during OnRuntimeStop and may have
		// submitted a request. Stop is terminal for this runtime owner.
		ClearPendingTransition();
		m_ActiveScene.reset();
		m_ActiveSceneHandle = AssetHandle(0);
		m_ActiveBuildIndex = -1;
		ResetSceneOwnership();
		m_LoadState = SceneLoadState::Idle;
		m_AllowSceneActivation = true;
		m_Stopping = false;
		DeactivateRuntime();
	}

}
