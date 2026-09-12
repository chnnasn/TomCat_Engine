#include "tcpch.h"
#include "SceneManager.h"

#include "SceneSerializer.h"
#include "TomCat/Asset/AssetManager.h"
#include "TomCat/Utils/PathUtils.h"

#include <algorithm>
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
		if (m_ActiveScene || m_PendingScene)
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
		if (m_ActiveScene || m_PendingScene)
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
		if (!scene->OnRuntimeStart())
		{
			ClearPendingTransition();
			m_ActiveScene.reset();
			m_ActiveSceneHandle = AssetHandle(0);
			m_ActiveBuildIndex = -1;
			return Fail("failed to start prepared build Scene handle "
				+ std::to_string(static_cast<uint64_t>(sceneHandle)));
		}
		m_LastError.clear();
		TC_Core_Info("SceneManager started prepared build Scene [{0}] handle {1}",
			*buildIndex, static_cast<uint64_t>(sceneHandle));
		return true;
	}

	bool SceneManager::RequestLoadScene(AssetHandle scene)
	{
		if (!CheckOwnerThread("RequestLoadScene"))
			return false;
		if (m_BuildSceneHandles.empty())
			return Fail("no build-scene configuration is loaded");
		const std::optional<uint32_t> buildIndex = FindBuildIndex(scene);
		if (!buildIndex)
			return Fail("Scene handle " + std::to_string(static_cast<uint64_t>(scene))
				+ " is not in the enabled build-scene list");
		return StageScene(scene, *buildIndex);
	}

	bool SceneManager::RequestLoadScene(uint32_t buildIndex)
	{
		if (!CheckOwnerThread("RequestLoadScene"))
			return false;
		if (buildIndex >= m_BuildSceneHandles.size())
			return Fail("build Scene index " + std::to_string(buildIndex)
				+ " is outside the enabled build-scene list");
		return StageScene(m_BuildSceneHandles[buildIndex], buildIndex);
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
			static_cast<uint32_t>(m_ActiveBuildIndex));
	}

	bool SceneManager::StageScene(AssetHandle scene, uint32_t buildIndex)
	{
		AssetManager& assets = AssetManager::Get();
		AssetType type = AssetType::None;
		std::vector<uint8_t> bytes;
		if (!assets.ReadAssetBytes(scene, bytes, &type))
			return Fail("could not read build Scene handle "
				+ std::to_string(static_cast<uint64_t>(scene)));
		if (type != AssetType::Scene)
			return Fail("build Scene handle " + std::to_string(static_cast<uint64_t>(scene))
				+ " resolves to " + AssetTypeToString(type) + " instead of Scene");

		const std::filesystem::path diagnosticPath = UTF8ToPath(
			"BuildScene-" + std::to_string(static_cast<uint64_t>(scene)));
		if (!SceneSerializer::ValidateCurrentFormat(bytes, diagnosticPath))
			return Fail("build Scene handle " + std::to_string(static_cast<uint64_t>(scene))
				+ " failed current-schema validation");

		Ref<Scene> staged = CreateRef<Scene>();
		SceneSerializer serializer(staged);
		bool deserialized = false;
		if (assets.IsCookedPackageMounted())
			deserialized = serializer.Deserialize(scene);
		else
		{
			const std::filesystem::path path = assets.ResolvePath(scene);
			deserialized = !path.empty() && serializer.Deserialize(path);
		}
		if (!deserialized)
			return Fail("build Scene handle " + std::to_string(static_cast<uint64_t>(scene))
				+ " could not be fully deserialized");

		// Replacing an already-staged request is safe: neither Scene has entered
		// runtime and the current Scene remains alive throughout this function.
		m_PendingScene = std::move(staged);
		m_PendingSceneHandle = scene;
		m_PendingBuildIndex = static_cast<int32_t>(buildIndex);
		m_LastError.clear();
		return true;
	}

	void SceneManager::ApplyRuntimeConfiguration(const Ref<Scene>& scene) const
	{
		if (!scene)
			return;
		scene->SetPhysics2DSettings(m_Physics2DSettings);
		if (m_ViewportWidth > 0 && m_ViewportHeight > 0)
			scene->OnViewportResize(m_ViewportWidth, m_ViewportHeight);
	}

	void SceneManager::ClearPendingTransition()
	{
		m_PendingScene.reset();
		m_PendingSceneHandle = AssetHandle(0);
		m_PendingBuildIndex = -1;
	}

	bool SceneManager::CommitPendingTransition()
	{
		if (!CheckOwnerThread("CommitPendingTransition"))
			return false;
		if (!m_PendingScene)
			return true;
		if (s_Runtime != this)
			return Fail("CommitPendingTransition requires this SceneManager to be the active runtime owner");

		Ref<Scene> nextScene = std::move(m_PendingScene);
		const AssetHandle nextHandle = m_PendingSceneHandle;
		const int32_t nextBuildIndex = m_PendingBuildIndex;
		m_PendingSceneHandle = AssetHandle(0);
		m_PendingBuildIndex = -1;

		Ref<Scene> previousScene = m_ActiveScene;
		const AssetHandle previousHandle = m_ActiveSceneHandle;
		const int32_t previousBuildIndex = m_ActiveBuildIndex;
		if (previousScene && previousScene->IsRuntimeRunning())
			previousScene->OnRuntimeStop();

		ApplyRuntimeConfiguration(nextScene);
		m_ActiveScene = nextScene;
		m_ActiveSceneHandle = nextHandle;
		m_ActiveBuildIndex = nextBuildIndex;
		if (!nextScene->OnRuntimeStart())
		{
			// Discard requests issued by lifecycle callbacks belonging to the failed
			// transition. A successfully restored Scene may enqueue a fresh request.
			ClearPendingTransition();
			m_ActiveScene.reset();
			m_ActiveSceneHandle = AssetHandle(0);
			m_ActiveBuildIndex = -1;
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
				if (previousScene->OnRuntimeStart())
					return Fail(failure + "; the previous Scene was restored");

				ClearPendingTransition();
				m_ActiveScene.reset();
				m_ActiveSceneHandle = AssetHandle(0);
				m_ActiveBuildIndex = -1;
				failure += "; the previous Scene also failed to restart";
			}
			return Fail(std::move(failure));
		}

		m_LastError.clear();
		TC_Core_Info("SceneManager committed build Scene [{0}] handle {1}",
			nextBuildIndex, static_cast<uint64_t>(nextHandle));
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

	void SceneManager::Stop()
	{
		ClearPendingTransition();
		if (m_ActiveScene && m_ActiveScene->IsRuntimeRunning())
			m_ActiveScene->OnRuntimeStop();
		// OnDisable/OnDestroy callbacks execute during OnRuntimeStop and may have
		// submitted a request. Stop is terminal for this runtime owner.
		ClearPendingTransition();
		m_ActiveScene.reset();
		m_ActiveSceneHandle = AssetHandle(0);
		m_ActiveBuildIndex = -1;
		DeactivateRuntime();
	}

}
