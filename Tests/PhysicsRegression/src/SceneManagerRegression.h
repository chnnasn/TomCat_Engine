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

		bool PollUnload() override { return true; }

		bool FailNextCreate = false;
		uint64_t LastSceneSession = 0;
		uint64_t LastRuntimeGeneration = 0;
		std::function<void()> OnNextUpdate;
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
		}
	}

}
