#pragma once

#include "Scene.h"
#include "TomCat/Project/BuildSettings.h"
#include "TomCat/Project/ProjectSettings.h"

#include <cstdint>
#include <atomic>
#include <future>
#include <optional>
#include <string>
#include <thread>
#include <vector>
#include <unordered_map>
#include <unordered_set>

namespace TomCat {

	enum class SceneLoadMode : uint32_t { Single, Additive };
	enum class SceneLoadState : uint32_t { Idle, Reading, Ready, Completed, Failed, Cancelled };

	// Loaded scene assets compose into one ECS/physics/script world. Asset ownership
	// is tracked separately so unloading a scene leaves other scenes and persistent
	// roots alive, including their managed instances. All mutation commits at frame end.
	class SceneManager final
	{
	public:
		SceneManager();
		~SceneManager();

		SceneManager(const SceneManager&) = delete;
		SceneManager& operator=(const SceneManager&) = delete;

		// Configures the manager from the immutable tcpak v5 build-scene manifest.
		bool ConfigureCookedPackage();
		// Authoring/Editor path. Only enabled entries are admitted to the runtime
		// list, preserving their authored order.
		bool ConfigureBuildSettings(const BuildSettings& settings);

		bool LoadEntryScene();
		// Editor Play can provide its already-copied authoring Scene instead of
		// re-reading the on-disk asset. The identity must still be an enabled build
		// Scene so C# transitions use exactly the same list as the Player.
		bool StartPreparedScene(const Ref<Scene>& scene, AssetHandle sceneHandle);
		bool RequestLoadScene(AssetHandle scene, SceneLoadMode mode = SceneLoadMode::Single);
		bool RequestLoadScene(uint32_t buildIndex, SceneLoadMode mode = SceneLoadMode::Single);
		bool RequestLoadSceneAsync(AssetHandle scene, SceneLoadMode mode = SceneLoadMode::Single);
		bool RequestLoadSceneAsync(uint32_t buildIndex, SceneLoadMode mode = SceneLoadMode::Single);
		bool CancelPendingLoad();
		void SetAllowSceneActivation(bool allow) { m_AllowSceneActivation = allow; }
		bool GetAllowSceneActivation() const { return m_AllowSceneActivation; }
		SceneLoadState GetLoadState() const { return m_LoadState; }
		float GetLoadProgress() const;
		bool RequestUnloadScene(AssetHandle scene);
		bool SetActiveScene(AssetHandle scene);
		bool SetEntityPersistent(Entity entity, bool persistent = true);
		bool IsEntityPersistent(Entity entity) const;
		const std::vector<AssetHandle>& GetLoadedSceneHandles() const { return m_LoadedScenes; }
		bool RequestReload();
		bool CommitPendingTransition();
		void Stop();

		void SetViewportSize(uint32_t width, uint32_t height);
		void SetRuntimeUIViewportMetrics(const glm::vec2& screenOrigin,
			float dpiScale,
			const glm::vec2& screenToFramebufferScale = glm::vec2(1.0f));
		void SetPhysics2DSettings(const Physics2DSettings& settings)
		{
			m_Physics2DSettings = settings;
		}

		Ref<Scene> GetActiveScene() const { return m_ActiveScene; }
		AssetHandle GetActiveSceneHandle() const { return m_ActiveSceneHandle; }
		int32_t GetActiveBuildIndex() const { return m_ActiveBuildIndex; }
		AssetHandle GetEntrySceneHandle() const { return m_EntrySceneHandle; }
		const std::vector<AssetHandle>& GetBuildSceneHandles() const
		{
			return m_BuildSceneHandles;
		}
		bool HasPendingTransition() const { return m_PendingScene || m_AsyncRead.valid() || !m_PendingUnloads.empty(); }
		const std::string& GetLastError() const { return m_LastError; }

		// The Player/Editor runtime owner binds one non-owning instance for native
		// script glue. A second live owner cannot silently replace it.
		bool ActivateRuntime();
		void DeactivateRuntime();
		static SceneManager* GetRuntime() { return s_Runtime; }
		bool IsOwnerThread() const
		{
			return std::this_thread::get_id() == m_OwnerThread;
		}

	private:
		bool Configure(AssetHandle entryScene,
			std::vector<AssetHandle> buildScenes);
		bool StageScene(AssetHandle scene, uint32_t buildIndex, SceneLoadMode mode);
		bool StageBytes(std::vector<uint8_t> bytes, AssetHandle scene, uint32_t buildIndex, SceneLoadMode mode);
		bool CommitComposedScene(const Ref<Scene>& source, AssetHandle handle, uint32_t buildIndex, SceneLoadMode mode);
		bool MergeScene(const Ref<Scene>& source, std::vector<UUID>& created, std::string& error);
		void ReconcileEntityOwnership();
		bool UnloadSceneNow(AssetHandle handle);
		void ResetSceneOwnership();
		std::optional<uint32_t> FindBuildIndex(AssetHandle scene) const;
		bool CheckOwnerThread(const char* operation);
		bool Fail(std::string message);
		void ApplyRuntimeConfiguration(const Ref<Scene>& scene) const;
		void ClearPendingTransition();

	private:
		static SceneManager* s_Runtime;

		std::thread::id m_OwnerThread;
		AssetHandle m_EntrySceneHandle = AssetHandle(0);
		std::vector<AssetHandle> m_BuildSceneHandles;
		Physics2DSettings m_Physics2DSettings;
		uint32_t m_ViewportWidth = 0;
		uint32_t m_ViewportHeight = 0;
		glm::vec2 m_RuntimeUIViewportOrigin{ 0.0f };
		float m_RuntimeUIDPIScale = 1.0f;
		glm::vec2 m_RuntimeUIScreenToFramebufferScale{ 1.0f };

		Ref<Scene> m_ActiveScene;
		AssetHandle m_ActiveSceneHandle = AssetHandle(0);
		int32_t m_ActiveBuildIndex = -1;

		Ref<Scene> m_PendingScene;
		AssetHandle m_PendingSceneHandle = AssetHandle(0);
		int32_t m_PendingBuildIndex = -1;
		SceneLoadMode m_PendingLoadMode = SceneLoadMode::Single;
		SceneLoadState m_LoadState = SceneLoadState::Idle;
		bool m_AllowSceneActivation = true;
		bool m_Committing = false;
		bool m_Stopping = false;
		struct AsyncReadState { std::atomic<bool> Cancelled{false}; std::atomic<float> Progress{0.0f}; };
		struct AsyncReadResult { std::vector<uint8_t> Bytes; std::string Error; };
		std::shared_ptr<AsyncReadState> m_AsyncState;
		std::future<AsyncReadResult> m_AsyncRead;
		std::vector<AssetHandle> m_LoadedScenes;
		std::vector<AssetHandle> m_PendingUnloads;
		std::unordered_map<UUID, AssetHandle> m_EntityOwners;
		std::unordered_set<UUID> m_PersistentRoots;
		std::string m_LastError;
	};

}
