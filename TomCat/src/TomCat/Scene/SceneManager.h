#pragma once

#include "Scene.h"
#include "TomCat/Project/BuildSettings.h"
#include "TomCat/Project/ProjectSettings.h"

#include <cstdint>
#include <optional>
#include <string>
#include <thread>
#include <vector>

namespace TomCat {

	// Owns the single runtime Scene and performs synchronous, replacement-only
	// transitions. Requests stage and validate a fresh Scene while the current
	// Scene remains alive; CommitPendingTransition is intentionally separate so
	// callers can invoke it only at a frame-end safe point.
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
		bool RequestLoadScene(AssetHandle scene);
		bool RequestLoadScene(uint32_t buildIndex);
		bool RequestReload();
		bool CommitPendingTransition();
		void Stop();

		void SetViewportSize(uint32_t width, uint32_t height);
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
		bool HasPendingTransition() const { return static_cast<bool>(m_PendingScene); }
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
		bool StageScene(AssetHandle scene, uint32_t buildIndex);
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

		Ref<Scene> m_ActiveScene;
		AssetHandle m_ActiveSceneHandle = AssetHandle(0);
		int32_t m_ActiveBuildIndex = -1;

		Ref<Scene> m_PendingScene;
		AssetHandle m_PendingSceneHandle = AssetHandle(0);
		int32_t m_PendingBuildIndex = -1;
		std::string m_LastError;
	};

}
