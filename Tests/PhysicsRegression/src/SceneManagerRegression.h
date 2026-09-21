#pragma once

#include "TomCat/Asset/AssetManager.h"
#include "TomCat/Project/Project.h"
#include "TomCat/Scene/Components.h"
#include "TomCat/Scene/Entity.h"
#include "TomCat/Scene/SceneManager.h"
#include "TomCat/Scene/SceneSerializer.h"
#include "TomCat/Scripting/IScriptRuntime.h"
#include "TomCat/Scripting/ScriptEngine.h"

#include <algorithm>
#include <chrono>
#include <fstream>
#include <filesystem>
#include <functional>
#include <initializer_list>
#include <memory>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace SceneManagerRegression {

	inline void Require(bool condition, const char* message)
	{
		if (!condition)
			throw std::runtime_error(message);
	}

	class TemporaryProject final
	{
	public:
		TemporaryProject()
		{
			TomCat::AssetManager::Get().Shutdown();
			Root = std::filesystem::temp_directory_path()
				/ ("tomcat_scene_manager_"
					+ std::to_string(static_cast<uint64_t>(TomCat::UUID())));
		}

		~TemporaryProject()
		{
			TomCat::AssetManager::Get().Shutdown();
			std::error_code error;
			std::filesystem::remove_all(Root, error);
		}

		std::filesystem::path Root;
	};

	class RuntimeProbe final : public TomCat::Scripting::IScriptRuntime
	{
	public:
		bool IsReady() const override { return true; }

		TomCat::Scripting::ScriptStatus CreateSceneRuntime(uint64_t sceneSessionId,
			uint64_t runtimeGeneration) override
		{
			Calls.emplace_back("CreateSceneRuntime");
			LastSceneSession = sceneSessionId;
			LastRuntimeGeneration = runtimeGeneration;
			return TomCat::Scripting::ScriptStatus::Success;
		}

		TomCat::Scripting::ScriptStatus InstantiateAll(
			std::span<const TomCat::Scripting::NativeScriptAttachmentV1> attachments) override
		{
			Calls.emplace_back("InstantiateAll");
			Attachments.assign(attachments.begin(), attachments.end());
			return TomCat::Scripting::ScriptStatus::Success;
		}

		TomCat::Scripting::ScriptStatus ApplySerializedFields(std::string_view) override
		{
			Calls.emplace_back("ApplySerializedFields");
			return TomCat::Scripting::ScriptStatus::Success;
		}

		TomCat::Scripting::ScriptStatus InvokeCreateAll() override
		{
			Calls.emplace_back("InvokeCreateAll");
			if (FailNextCreate)
			{
				FailNextCreate = false;
				return TomCat::Scripting::ScriptStatus::ManagedException;
			}
			return TomCat::Scripting::ScriptStatus::Success;
		}

		TomCat::Scripting::ScriptStatus SetEnabled(uint64_t, bool) override
		{
			return TomCat::Scripting::ScriptStatus::Success;
		}

		TomCat::Scripting::ScriptStatus UpdateAll(float) override
		{
			Calls.emplace_back("UpdateAll");
			if (OnNextUpdate)
			{
				auto callback = std::move(OnNextUpdate);
				callback();
			}
			return TomCat::Scripting::ScriptStatus::Success;
		}

		TomCat::Scripting::ScriptStatus FixedUpdateAll(float) override
		{
			Calls.emplace_back("FixedUpdateAll");
			return TomCat::Scripting::ScriptStatus::Success;
		}

		TomCat::Scripting::ScriptStatus DispatchPhysicsEvents(
			std::span<const TomCat::Scripting::NativePhysicsEventV1>) override
		{
			return TomCat::Scripting::ScriptStatus::Success;
		}

		TomCat::Scripting::ScriptStatus DestroyAll() override
		{
			Calls.emplace_back("DestroyAll");
			Attachments.clear();
			return TomCat::Scripting::ScriptStatus::Success;
		}

		TomCat::Scripting::ScriptStatus InstantiateAttachments(
			std::span<const TomCat::Scripting::NativeScriptAttachmentV1> attachments,
			std::string_view) override
		{
			Calls.emplace_back("InstantiateAttachments");
			if (OnNextBatch)
			{
				auto callback = std::move(OnNextBatch);
				callback();
			}
			if (FailNextBatch)
			{
				FailNextBatch = false;
				return TomCat::Scripting::ScriptStatus::ManagedException;
			}
			Attachments.insert(Attachments.end(), attachments.begin(), attachments.end());
			return TomCat::Scripting::ScriptStatus::Success;
		}

		TomCat::Scripting::ScriptStatus DestroyAttachments(std::span<const uint64_t> ids) override
		{
			Calls.emplace_back("DestroyAttachments");
			std::erase_if(Attachments, [&](const auto& attachment)
			{
				return std::find(ids.begin(), ids.end(), attachment.AttachmentId) != ids.end();
			});
			return TomCat::Scripting::ScriptStatus::Success;
		}

		bool PollUnload() override { return true; }

		bool FailNextCreate = false;
		bool FailNextBatch = false;
		uint64_t LastSceneSession = 0;
		uint64_t LastRuntimeGeneration = 0;
		std::function<void()> OnNextUpdate;
		std::function<void()> OnNextBatch;
		std::vector<TomCat::Scripting::NativeScriptAttachmentV1> Attachments;
		std::vector<std::string> Calls;
	};

	class RuntimeOverride final
	{
	public:
		explicit RuntimeOverride(std::shared_ptr<TomCat::Scripting::IScriptRuntime> runtime)
		{
			TomCat::Scripting::ScriptEngine::Get().SetRuntime(std::move(runtime));
		}

		~RuntimeOverride()
		{
			TomCat::Scripting::ScriptEngine::Get().SetRuntime({});
		}
	};

	inline TomCat::AssetHandle WriteSceneAsset(const TomCat::Ref<TomCat::Project>& project,
		const char* filename, const char* sceneName, uint64_t scriptAsset)
	{
		auto scene = TomCat::CreateRef<TomCat::Scene>();
		scene->SetSceneName(sceneName);
		TomCat::Entity entity = scene->CreateEntity(sceneName);
		TomCat::CSharpScriptEntry script;
		script.AttachmentID = TomCat::UUID();
		script.ScriptAsset = TomCat::AssetHandle(scriptAsset);
		script.LastKnownClassName = std::string(sceneName) + "Behaviour";
		script.Fields.emplace_back("ee000000000000000000000000000001", "Self",
			TomCat::ScriptFieldType::Entity, static_cast<uint64_t>(entity.GetUUID()));
		entity.AddComponent<TomCat::CSharpScripts>().Scripts.emplace_back(std::move(script));

		const std::filesystem::path path = project->GetAssetPath() / filename;
		TomCat::SceneSerializer serializer(scene);
		Require(serializer.Serialize(path), "could not serialize a SceneManager fixture Scene");
		const TomCat::AssetMetadata* metadata =
			TomCat::AssetManager::Get().GetRegistry().GetMetadata(path);
		Require(metadata && metadata->Type == TomCat::AssetType::Scene
			&& static_cast<uint64_t>(metadata->Handle) != 0,
			"SceneManager fixture Scene was not imported");
		return metadata->Handle;
	}

	inline void RequireTail(const std::vector<std::string>& calls,
		std::initializer_list<const char*> expected, const char* message)
	{
		Require(calls.size() >= expected.size(), message);
		auto actual = calls.end() - static_cast<std::ptrdiff_t>(expected.size());
		for (const char* value : expected)
		{
			if (*actual++ != value)
				throw std::runtime_error(message);
		}
	}

	inline void Run()
	{
		TemporaryProject environment;
		TomCat::ProjectConfig config;
		config.Name = "Scene Manager Regression";
		config.Template = "2D";
		config.AssetDirectory = "Assets";
		auto project = TomCat::Project::CreateNew(
			environment.Root / "Project.tcproj", config);
		Require(project != nullptr, "could not create the SceneManager fixture project");

		TomCat::ProjectSettings projectSettings;
		projectSettings.Physics2D.SetLayersCollide(1, 2, false);
		Require(project->SetSettings(projectSettings),
			"could not persist SceneManager fixture physics settings");

		TomCat::AssetManager& assets = TomCat::AssetManager::Get();
		Require(assets.SetProject(project),
			"could not initialize assets for the SceneManager fixture");
		const TomCat::AssetHandle first = WriteSceneAsset(project,
			"First.tomcat", "First", 70001);
		const TomCat::AssetHandle second = WriteSceneAsset(project,
			"Second.tomcat", "Second", 70002);

		TomCat::BuildSettings buildSettings;
		buildSettings.EntrySceneHandle = first;
		buildSettings.Scenes = {
			{ first, true, "First.tomcat" },
			{ TomCat::AssetHandle(79999), false, "Disabled.tomcat" },
			{ second, true, "Second.tomcat" }
		};
		Require(project->SetBuildSettings(buildSettings),
			"could not persist the SceneManager build list");

		auto runtime = std::make_shared<RuntimeProbe>();
		RuntimeOverride runtimeOverride(runtime);
		{
			TomCat::SceneManager manager;
			manager.SetViewportSize(1280, 720);
			Require(manager.ConfigureBuildSettings(project->GetBuildSettings()),
				"SceneManager rejected a valid authoring build list");
			Require(manager.GetBuildSceneHandles().size() == 2
				&& manager.GetBuildSceneHandles()[0] == first
				&& manager.GetBuildSceneHandles()[1] == second,
				"SceneManager did not preserve enabled build-scene order");
			Require(manager.ActivateRuntime()
				&& TomCat::SceneManager::GetRuntime() == &manager,
				"SceneManager did not bind its runtime instance");
			TomCat::SceneManager competingManager;
			Require(!competingManager.ActivateRuntime()
				&& TomCat::SceneManager::GetRuntime() == &manager,
				"a second SceneManager replaced the live runtime owner");

			Require(manager.LoadEntryScene(), "SceneManager could not load its Entry Scene");
			Require(manager.GetActiveSceneHandle() == first
				&& manager.GetActiveBuildIndex() == 0
				&& manager.GetActiveScene()
				&& manager.GetActiveScene()->GetSceneName() == "First"
				&& manager.GetActiveScene()->GetPhysics2DSettings()
					== projectSettings.Physics2D,
				"Entry Scene did not receive its handle, index, or project physics settings");
			Require(runtime->Attachments.size() == 1,
				"Entry Scene did not create its managed attachment");
			const TomCat::Scripting::EntityHandleV1 firstEntity =
				runtime->Attachments.front().Entity;

			const TomCat::Ref<TomCat::Scene> firstScene = manager.GetActiveScene();
			Require(!manager.RequestLoadScene(TomCat::AssetHandle(79999))
				&& !manager.RequestLoadScene(uint32_t{ 2 })
				&& manager.GetActiveScene() == firstScene
				&& !manager.HasPendingTransition(),
				"disabled/invalid build Scene requests mutated the active Scene");

			bool requestedDuringUpdate = false;
			runtime->OnNextUpdate = [&]()
			{
				requestedDuringUpdate = manager.RequestLoadScene(uint32_t{ 1 });
			};
			firstScene->OnUpdateRuntime(TomCat::Timestep(0.0f));
			Require(requestedDuringUpdate && manager.HasPendingTransition()
				&& manager.GetActiveScene() == firstScene
				&& firstScene->IsRuntimeRunning()
				&& std::count(runtime->Calls.begin(), runtime->Calls.end(), "DestroyAll") == 0,
				"a load request from Update destroyed the iterated Scene before frame end");

			Require(manager.CommitPendingTransition(),
				"frame-end Scene transition did not commit");
			Require(manager.GetActiveSceneHandle() == second
				&& manager.GetActiveBuildIndex() == 1
				&& manager.GetActiveScene()->GetSceneName() == "Second"
				&& !firstScene->IsRuntimeRunning(),
				"frame-end Scene transition did not replace the active Scene");
			RequireTail(runtime->Calls,
				{ "DestroyAll", "CreateSceneRuntime", "InstantiateAll",
					"ApplySerializedFields", "InvokeCreateAll" },
				"old script teardown did not precede new script creation");
			Require(!TomCat::Scripting::ScriptEngine::Get().ResolveEntity(firstEntity),
				"an Entity handle from the replaced Scene remained valid");

			const TomCat::Ref<TomCat::Scene> secondScene = manager.GetActiveScene();
			const TomCat::Scripting::EntityHandleV1 secondEntity =
				runtime->Attachments.front().Entity;
			Require(manager.RequestReload() && manager.GetActiveScene() == secondScene,
				"reload did not remain deferred until the safe point");
			Require(manager.CommitPendingTransition()
				&& manager.GetActiveSceneHandle() == second
				&& manager.GetActiveBuildIndex() == 1
				&& manager.GetActiveScene() != secondScene,
				"reload did not replace the Scene while preserving build identity");
			Require(!TomCat::Scripting::ScriptEngine::Get().ResolveEntity(secondEntity),
				"reload did not invalidate an Entity handle from the previous generation");

			const TomCat::Ref<TomCat::Scene> rollbackScene = manager.GetActiveScene();
			const TomCat::Scripting::EntityHandleV1 preRollbackEntity =
				runtime->Attachments.front().Entity;
			runtime->FailNextCreate = true;
			Require(manager.RequestLoadScene(first)
				&& !manager.CommitPendingTransition()
				&& manager.GetActiveScene() == rollbackScene
				&& manager.GetActiveSceneHandle() == second
				&& manager.GetLastError().find("previous Scene was restored")
					!= std::string::npos,
				"failed target startup did not report failure and restore the old Scene");
			Require(!TomCat::Scripting::ScriptEngine::Get().ResolveEntity(preRollbackEntity),
				"rollback did not advance the restored Scene generation");

			Require(manager.RequestLoadScene(first)
				&& manager.CommitPendingTransition()
				&& manager.GetActiveSceneHandle() == first
				&& manager.GetActiveBuildIndex() == 0,
				"handle-based Scene loading did not commit");
			manager.Stop();
			Require(!manager.GetActiveScene()
				&& TomCat::SceneManager::GetRuntime() == nullptr,
				"SceneManager Stop did not clear active and runtime state");

			auto prepared = TomCat::CreateRef<TomCat::Scene>();
			prepared->SetSceneName("Unsaved Editor Play Copy");
			TomCat::CSharpScriptEntry preparedScript;
			preparedScript.AttachmentID = TomCat::UUID();
			preparedScript.ScriptAsset = TomCat::AssetHandle(70003);
			preparedScript.LastKnownClassName = "PreparedBehaviour";
			prepared->CreateEntity("Prepared")
				.AddComponent<TomCat::CSharpScripts>().Scripts.push_back(preparedScript);
			Require(manager.ActivateRuntime()
				&& !manager.StartPreparedScene(prepared, TomCat::AssetHandle(79999))
				&& manager.StartPreparedScene(prepared, second)
				&& manager.GetActiveScene() == prepared
				&& manager.GetActiveSceneHandle() == second
				&& manager.GetActiveBuildIndex() == 1
				&& prepared->IsRuntimeRunning(),
				"Editor prepared-Scene startup bypassed build-list identity or runtime setup");
			manager.Stop();

			Require(manager.ActivateRuntime() && manager.LoadEntryScene(), "could not restart SceneManager for streaming tests");
			const auto world = manager.GetActiveScene();
			const auto persistentHandle = runtime->Attachments.front().Entity;
			TomCat::Entity persistent = TomCat::Scripting::ScriptEngine::Get().ResolveEntity(persistentHandle);
			const size_t teardownCount = std::count(runtime->Calls.begin(), runtime->Calls.end(), "DestroyAll");
			Require(manager.SetEntityPersistent(persistent), "could not mark the manager root persistent");
			TomCat::Entity child = world->CreateEntity("Persistent child");
			Require(world->SetParent(child, persistent), "could not parent a persistent child");
			const auto childID = child.GetUUID();
			Require(manager.RequestLoadScene(second, TomCat::SceneLoadMode::Additive)
				&& manager.CommitPendingTransition(), "additive Scene did not commit");
			Require(manager.GetActiveScene() == world && manager.GetActiveSceneHandle() == first
				&& manager.GetLoadedSceneHandles().size() == 2 && runtime->Attachments.size() == 2
				&& TomCat::Scripting::ScriptEngine::Get().ResolveEntity(persistentHandle),
				"additive Scene replaced the shared runtime or lost an existing script");
			Require(!manager.RequestLoadScene(second, TomCat::SceneLoadMode::Additive), "duplicate additive Scene was accepted");
			Require(manager.SetActiveScene(second) && manager.RequestUnloadScene(first)
				&& manager.GetLoadedSceneHandles().size() == 2 && manager.CommitPendingTransition(),
				"explicit Scene unload was not deferred to frame end");
			Require(manager.GetLoadedSceneHandles().size() == 1 && runtime->Attachments.size() == 2
				&& world->FindEntityByUUID(childID)
				&& TomCat::Scripting::ScriptEngine::Get().ResolveEntity(persistentHandle),
				"unload destroyed persistent script state or its child");
			Require(!manager.RequestUnloadScene(second), "last loaded Scene was allowed to unload");

			runtime->FailNextBatch = true;
			Require(manager.RequestLoadScene(first, TomCat::SceneLoadMode::Additive)
				&& !manager.CommitPendingTransition() && manager.GetLoadedSceneHandles().size() == 1
				&& runtime->Attachments.size() == 2 && world->IsRuntimeRunning(),
				"failed additive bootstrap damaged the previously loaded world");

			// Startup code can create objects outside the imported hierarchy, enqueue
			// another lifecycle batch, and reparent old content before throwing.
			TomCat::UUID failedOrphanID(0), failedChildID(0);
			const glm::vec3 persistentPosition = persistent.GetComponent<TomCat::Transform>()._Translation;
			runtime->OnNextBatch = [&]()
			{
				TomCat::Entity orphan = world->CreateEntity("Failed startup orphan");
				failedOrphanID = orphan.GetUUID();
				TomCat::CSharpScriptEntry orphanScript = persistent.GetComponent<TomCat::CSharpScripts>().Scripts.front();
				orphanScript.AttachmentID = TomCat::UUID();
				orphan.AddComponent<TomCat::CSharpScripts>().Scripts.push_back(orphanScript);
				TomCat::Entity spawnedChild = world->CreateEntity("Failed startup child of old root");
				failedChildID = spawnedChild.GetUUID();
				Require(world->SetParent(spawnedChild, persistent), "could not set up failed startup child");
				Require(manager.SetEntityPersistent(persistent, false)
					&& manager.SetEntityPersistent(orphan)
					&& world->SetParent(persistent, orphan), "could not set up failed startup ownership");
				world->QueueRuntimeEntityBatchCreated({failedOrphanID, failedChildID});
				throw std::runtime_error("intentional startup exception after creating external objects");
			};
			Require(manager.RequestLoadScene(first)
				&& !manager.CommitPendingTransition()
				&& manager.GetActiveScene() == world && world->IsRuntimeRunning()
				&& manager.GetActiveSceneHandle() == second && manager.GetActiveBuildIndex() == 1
				&& manager.GetLoadedSceneHandles() == std::vector<TomCat::AssetHandle>{second}
				&& static_cast<uint64_t>(failedOrphanID) != 0 && static_cast<uint64_t>(failedChildID) != 0
				&& !world->FindEntityByUUID(failedOrphanID) && !world->FindEntityByUUID(failedChildID)
				&& world->GetPendingRuntimeEntityCreateCount() == 0
				&& TomCat::Scripting::ScriptEngine::Get().ResolveEntity(persistentHandle)
				&& !world->GetParent(persistent) && manager.IsEntityPersistent(persistent)
				&& world->FindEntityByUUID(childID)
				&& glm::distance(persistent.GetComponent<TomCat::Transform>()._Translation, persistentPosition) < 0.0001f
				&& runtime->Attachments.size() == 2,
				"failed composed startup left callback-created objects or damaged existing ownership and handles");
			const auto batchCallsAfterRollback = std::count(runtime->Calls.begin(), runtime->Calls.end(), "InstantiateAttachments");
			world->OnUpdateRuntime(TomCat::Timestep(0.0f), false);
			Require(runtime->Attachments.size() == 2
				&& std::count(runtime->Calls.begin(), runtime->Calls.end(), "InstantiateAttachments") == batchCallsAfterRollback,
				"failed composed startup left a queued lifecycle batch for the next frame");

			manager.SetAllowSceneActivation(false);
			Require(manager.RequestLoadSceneAsync(first), "async Scene read was rejected");
			const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
			while (manager.GetLoadState() == TomCat::SceneLoadState::Reading
				&& std::chrono::steady_clock::now() < deadline)
			{
				world->OnUpdateRuntime(TomCat::Timestep(0.0f), false);
				Require(manager.CommitPendingTransition(), "async Scene preparation failed");
				std::this_thread::sleep_for(std::chrono::milliseconds(1));
			}
			Require(manager.GetLoadState() == TomCat::SceneLoadState::Ready
				&& manager.GetLoadProgress() == 0.9f && manager.GetActiveSceneHandle() == second,
				"async activation barrier did not retain the running Scene at 90 percent");
			manager.SetAllowSceneActivation(true);
			Require(manager.CommitPendingTransition() && manager.GetLoadState() == TomCat::SceneLoadState::Completed
				&& manager.GetLoadProgress() == 1.0f && manager.GetActiveSceneHandle() == first
				&& manager.GetLoadedSceneHandles().size() == 1 && runtime->Attachments.size() == 2
				&& world->FindEntityByUUID(childID)
				&& TomCat::Scripting::ScriptEngine::Get().ResolveEntity(persistentHandle)
				&& std::count(runtime->Calls.begin(), runtime->Calls.end(), "DestroyAll") == teardownCount,
				"async replacement restarted a persistent script or left unloaded content alive");
			Require(manager.RequestReload() && manager.CommitPendingTransition()
				&& runtime->Attachments.size() == 2
				&& TomCat::Scripting::ScriptEngine::Get().ResolveEntity(persistentHandle),
				"same-asset reload destroyed incoming or persistent objects");
			Require(manager.RequestLoadSceneAsync(second) && manager.CancelPendingLoad()
				&& manager.CommitPendingTransition() && manager.GetLoadState() == TomCat::SceneLoadState::Cancelled
				&& manager.GetActiveSceneHandle() == first, "cancelled Scene load activated later");
			Require(manager.SetEntityPersistent(persistent, false), "could not return persistent objects to the active Scene");
			Require(manager.RequestLoadScene(second) && manager.CommitPendingTransition()
				&& !TomCat::Scripting::ScriptEngine::Get().ResolveEntity(persistentHandle),
				"unmarked persistent objects survived a single Scene replacement");

			// A surviving scene's child may be parented under an unloading scene.
			// Ownership must not change with hierarchy, and detachment preserves its
			// world pose, managed handle, and serialized entity references.
			const auto crossWorld = manager.GetActiveScene();
			const auto survivorHandle = runtime->Attachments.front().Entity;
			TomCat::Entity survivor = TomCat::Scripting::ScriptEngine::Get().ResolveEntity(survivorHandle);
			survivor.GetComponent<TomCat::Transform>()._Translation = {5.0f, 7.0f, 0.0f};
			Require(manager.RequestLoadScene(first, TomCat::SceneLoadMode::Additive)
				&& manager.CommitPendingTransition(), "could not load cross-scene parent fixture");
			const auto parentHandle = runtime->Attachments.back().Entity;
			TomCat::Entity unloadingParent = TomCat::Scripting::ScriptEngine::Get().ResolveEntity(parentHandle);
			const auto& copiedSelf = unloadingParent.GetComponent<TomCat::CSharpScripts>().Scripts.front().Fields.front();
			Require(std::get<uint64_t>(copiedSelf.Value) == static_cast<uint64_t>(unloadingParent.GetUUID()),
				"additive Scene did not remap serialized entity references");
			Require(crossWorld->SetParent(survivor, unloadingParent), "cross-scene reparent failed");
			const glm::vec3 survivorPosition = survivor.GetComponent<TomCat::Transform>()._Translation;
			Require(manager.RequestUnloadScene(first) && manager.CommitPendingTransition()
				&& !TomCat::Scripting::ScriptEngine::Get().ResolveEntity(parentHandle)
				&& TomCat::Scripting::ScriptEngine::Get().ResolveEntity(survivorHandle)
				&& !crossWorld->GetParent(survivor)
				&& glm::distance(survivor.GetComponent<TomCat::Transform>()._Translation, survivorPosition) < 0.0001f
				&& std::get<uint64_t>(survivor.GetComponent<TomCat::CSharpScripts>().Scripts.front().Fields.front().Value)
					== static_cast<uint64_t>(survivor.GetUUID())
				&& runtime->Attachments.size() == 1,
				"unloading a cross-scene parent invalidated its surviving child, world pose or references");

			// The asynchronous I/O may succeed while schema validation fails on the
			// owner thread. That terminal error must not tear down the current world.
			{
				std::ofstream invalidScene(project->GetAssetPath() / "First.tomcat", std::ios::binary | std::ios::trunc);
				invalidScene << "Scene: Invalid asynchronous fixture\nSchemaVersion: 0\nEntities: []\n";
				Require(static_cast<bool>(invalidScene), "could not write malformed async Scene fixture");
			}
			Require(manager.RequestLoadSceneAsync(first), "malformed Scene fixture was not queued for asynchronous reading");
			bool asyncFailureReported = false;
			const auto failureDeadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
			while (manager.GetLoadState() == TomCat::SceneLoadState::Reading
				&& std::chrono::steady_clock::now() < failureDeadline)
			{
				asyncFailureReported |= !manager.CommitPendingTransition();
				std::this_thread::sleep_for(std::chrono::milliseconds(1));
			}
			crossWorld->OnUpdateRuntime(TomCat::Timestep(0.0f), false);
			Require(asyncFailureReported && manager.GetLoadState() == TomCat::SceneLoadState::Failed
				&& !manager.HasPendingTransition() && !manager.GetLastError().empty()
				&& manager.GetActiveScene() == crossWorld && manager.GetActiveSceneHandle() == second
				&& crossWorld->IsRuntimeRunning() && runtime->Attachments.size() == 1
				&& TomCat::Scripting::ScriptEngine::Get().ResolveEntity(survivorHandle),
				"malformed asynchronous Scene did not fail terminally while retaining the running world");
			manager.Stop();

			// A callback may stop just the world, or relinquish manager ownership.
			// Retain a local reference so rollback can clean the unpublished objects
			// without dereferencing a cleared manager or resurrecting stopped state.
			for (bool stopManager : {false, true})
			{
				Require(manager.ActivateRuntime() && manager.RequestLoadScene(second)
					&& manager.CommitPendingTransition(), "could not restart stopped-bootstrap fixture");
				const auto stoppedWorld = manager.GetActiveScene();
				const auto existingID = stoppedWorld->GetRootEntityUUIDs().front();
				TomCat::Entity existing = stoppedWorld->FindEntityByUUID(existingID);
				Require(manager.SetEntityPersistent(existing), "could not set up stopped-bootstrap persistence");
				TomCat::UUID stoppedOrphanID(0);
				runtime->OnNextBatch = [&]()
				{
					TomCat::Entity orphan = stoppedWorld->CreateEntity("Stopped startup orphan");
					stoppedOrphanID = orphan.GetUUID();
					stoppedWorld->QueueRuntimeEntityBatchCreated({stoppedOrphanID});
					Require(manager.SetEntityPersistent(orphan), "could not mark stopped startup orphan persistent");
					if (stopManager) manager.Stop();
					else stoppedWorld->OnRuntimeStop();
					throw std::runtime_error("intentional stop during composed startup");
				};
				Require(manager.RequestReload() && !manager.CommitPendingTransition()
					&& !stoppedWorld->IsRuntimeRunning() && static_cast<uint64_t>(stoppedOrphanID) != 0
					&& !stoppedWorld->FindEntityByUUID(stoppedOrphanID)
					&& stoppedWorld->FindEntityByUUID(existingID)
					&& stoppedWorld->GetRootEntityUUIDs() == std::vector<TomCat::UUID>{existingID}
					&& stoppedWorld->GetPendingRuntimeEntityCreateCount() == 0
					&& runtime->Attachments.empty() && !manager.HasPendingTransition(),
					"stopping during composed bootstrap crashed or leaked newly created objects");
				Require(stopManager
					? (!manager.GetActiveScene() && manager.GetLoadedSceneHandles().empty()
						&& manager.GetLoadState() == TomCat::SceneLoadState::Idle
						&& TomCat::SceneManager::GetRuntime() == nullptr)
					: (manager.GetActiveScene() == stoppedWorld && manager.GetActiveSceneHandle() == second
						&& manager.GetLoadedSceneHandles() == std::vector<TomCat::AssetHandle>{second}
						&& manager.IsEntityPersistent(existing)),
					"stopped bootstrap restored manager ownership after Stop or lost previous metadata");
				manager.Stop();
			}
		}
	}

}
