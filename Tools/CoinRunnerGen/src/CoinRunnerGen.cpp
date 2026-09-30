// One-shot generator for the CoinRunner sample project. It builds the scene
// through the engine's own scene/serializer/asset APIs so the checked-in
// sample is always a valid, registry-consistent project. Run from the
// repository root after building:
//   Tools\bin\Release-windows-x86_64\CoinRunnerGen\CoinRunnerGen.exe

#include <TomCat/Asset/AssetJobSystem.h>
#include <TomCat/Asset/AssetManager.h>
#include <TomCat/Core/Log.h>
#include <TomCat/Core/Version.h>
#include <TomCat/Project/Project.h>
#include <TomCat/Scene/Components.h>
#include <TomCat/Scene/Entity.h>
#include <TomCat/Scene/Scene.h>
#include <TomCat/Scene/SceneSerializer.h>
#include <TomCat/Utils/PathUtils.h>

#include <glm/glm.hpp>

#include <chrono>
#include <thread>

#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <string>

namespace {

	void Require(bool condition, const std::string& message)
	{
		if (!condition)
			throw std::runtime_error(message);
	}

	TomCat::AssetHandle ResolveScriptAsset(const std::filesystem::path& relative)
	{
		const TomCat::AssetMetadata* metadata =
			TomCat::AssetManager::Get().GetRegistry().GetMetadata(relative);
		Require(metadata != nullptr, "script asset was not imported: "
			+ TomCat::PathToUTF8(relative));
		return metadata->Handle;
	}

}

int main()
{
	TomCat::Log::Init();
	try
	{
		const std::filesystem::path projectPath = std::filesystem::path(
			"Samples/CoinRunner") / "Project.tcproj";
		if (std::filesystem::exists(projectPath))
		{
			std::cout << "CoinRunner project already exists; delete it first to "
				"regenerate.\n";
			return 0;
		}

		TomCat::ProjectConfig config;
		config.Name = "CoinRunner";
		config.Version = "1.0.0";
		config.Description = "Complete-game verification sample: a small 2D "
			"coin-collecting platformer exercising scripts, physics triggers, "
			"the save system and the packaging pipeline.";
		config.EditorVersion = std::string(TomCat::Version::ProductVersion);
		config.Template = "2D";
		config.AssetDirectory = "Assets";
		TomCat::Ref<TomCat::Project> project = TomCat::Project::CreateNew(
			projectPath, config);
		Require(project != nullptr, "project creation failed");

		auto scene = TomCat::CreateRef<TomCat::Scene>();
		scene->SetSceneName("Level");

		// Camera
		TomCat::Entity camera = scene->CreateEntityWithUUID(
			TomCat::UUID(0xC01A000000000001ULL), "MainCamera");
		auto& cameraComponent = camera.AddComponent<TomCat::C_Camera>();
		cameraComponent._Camera.SetOrthographic(10.0f, 0.0f, 1000.0f);
		camera.GetComponent<TomCat::Transform>()._Translation = glm::vec3(4.0f,
			2.0f, 0.0f);

		// Ground strip: one static box from x = -2 to x = 22.
		{
			TomCat::Entity ground = scene->CreateEntityWithUUID(
				TomCat::UUID(0xC01A000000000002ULL), "Ground");
			ground.GetComponent<TomCat::Transform>()._Translation = glm::vec3(
				10.0f, -0.5f, 0.0f);
			ground.GetComponent<TomCat::Transform>()._Scale = glm::vec3(24.0f,
				1.0f, 1.0f);
			auto& sprite = ground.AddComponent<TomCat::SpriteRenderer>();
			sprite._Color = glm::vec4(0.24f, 0.55f, 0.30f, 1.0f);
			auto& body = ground.AddComponent<TomCat::Rigidbody2D>();
			body.Type = TomCat::Rigidbody2D::BodyType::Static;
			auto& collider = ground.AddComponent<TomCat::BoxCollider2D>();
			collider.Size = glm::vec2(0.5f, 0.5f);
		}

		// Walls so the auto-demo player cannot run off the strip.
		for (const auto& [name, uuid, x] : std::initializer_list<std::tuple<
			const char*, uint64_t, float>>
		{
			{ "LeftWall", 0xC01A000000000003ULL, -2.5f },
			{ "RightWall", 0xC01A000000000004ULL, 22.5f }
		})
		{
			TomCat::Entity wall = scene->CreateEntityWithUUID(TomCat::UUID(uuid),
				name);
			wall.GetComponent<TomCat::Transform>()._Translation = glm::vec3(x,
				2.0f, 0.0f);
			wall.GetComponent<TomCat::Transform>()._Scale = glm::vec3(1.0f, 6.0f,
				1.0f);
			auto& body = wall.AddComponent<TomCat::Rigidbody2D>();
			body.Type = TomCat::Rigidbody2D::BodyType::Static;
			auto& collider = wall.AddComponent<TomCat::BoxCollider2D>();
			collider.Size = glm::vec2(0.5f, 0.5f);
		}

		// Player
		{
			TomCat::Entity player = scene->CreateEntityWithUUID(
				TomCat::UUID(0xC01A000000000005ULL), "Player");
			player.GetComponent<TomCat::Transform>()._Translation = glm::vec3(
				0.0f, 0.6f, 0.0f);
			player.GetComponent<TomCat::Transform>()._Scale = glm::vec3(0.8f,
				0.8f, 1.0f);
			auto& sprite = player.AddComponent<TomCat::SpriteRenderer>();
			sprite._Color = glm::vec4(0.9f, 0.35f, 0.25f, 1.0f);
			auto& body = player.AddComponent<TomCat::Rigidbody2D>();
			body.Type = TomCat::Rigidbody2D::BodyType::Dynamic;
			body.FixedRotation = true;
			auto& collider = player.AddComponent<TomCat::BoxCollider2D>();
			collider.Size = glm::vec2(0.5f, 0.5f);
			collider.Friction = 0.0f;

			TomCat::CSharpScriptEntry script;
			script.AttachmentID = TomCat::UUID(0xC01A00000000A001ULL);
			script.Enabled = true;
			script.LastKnownClassName = "PlayerController";
			player.AddComponent<TomCat::CSharpScripts>().Scripts.push_back(
				std::move(script));
		}

		// Coins: four triggers along the strip, slightly above the ground.
		for (int index = 0; index < 4; ++index)
		{
			TomCat::Entity coin = scene->CreateEntityWithUUID(
				TomCat::UUID(0xC01A000000000100ULL + static_cast<uint64_t>(index)),
				"Coin " + std::to_string(index + 1));
			coin.GetComponent<TomCat::Transform>()._Translation = glm::vec3(
				5.0f + 4.0f * index, 0.75f, 0.0f);
			coin.GetComponent<TomCat::Transform>()._Scale = glm::vec3(0.6f, 0.6f,
				1.0f);
			auto& sprite = coin.AddComponent<TomCat::SpriteRenderer>();
			sprite._Color = glm::vec4(1.0f, 0.84f, 0.0f, 1.0f);
			auto& body = coin.AddComponent<TomCat::Rigidbody2D>();
			body.Type = TomCat::Rigidbody2D::BodyType::Static;
			auto& collider = coin.AddComponent<TomCat::CircleCollider2D>();
			collider.IsTrigger = true;
			collider.Radius = 0.5f;

			TomCat::CSharpScriptEntry script;
			script.AttachmentID = TomCat::UUID(0xC01A00000000B000ULL
				+ static_cast<uint64_t>(index));
			script.Enabled = true;
			script.LastKnownClassName = "Coin";
			coin.AddComponent<TomCat::CSharpScripts>().Scripts.push_back(
				std::move(script));
		}

		// Score text
		{
			TomCat::Entity text = scene->CreateEntityWithUUID(
				TomCat::UUID(0xC01A000000000200ULL), "ScoreText");
			text.AddComponent<TomCat::RectTransform>();
			text.AddComponent<TomCat::UIText>();
		}

		// Game manager
		{
			TomCat::Entity manager = scene->CreateEntityWithUUID(
				TomCat::UUID(0xC01A000000000201ULL), "GameManager");
			TomCat::CSharpScriptEntry script;
			script.AttachmentID = TomCat::UUID(0xC01A00000000A002ULL);
			script.Enabled = true;
			script.LastKnownClassName = "GameManager";
			manager.AddComponent<TomCat::CSharpScripts>().Scripts.push_back(
				std::move(script));
		}

		const std::filesystem::path assetsDirectory = project->GetAssetPath();
		std::filesystem::create_directories(assetsDirectory / "Scene");

		// The canonical script sources live outside the project directory
		// (CreateNew requires it empty); the generator installs them into the
		// asset tree so the registry can assign their handles below.
		const std::filesystem::path scriptsDirectory = assetsDirectory / "Scripts";
		std::filesystem::create_directories(scriptsDirectory);
		const std::filesystem::path scriptSources = projectPath.parent_path()
			.parent_path() / "CoinRunnerScripts";
		for (const char* name : { "PlayerController.cs", "Coin.cs",
			"GameManager.cs" })
		{
			std::filesystem::copy_file(scriptSources / name,
				scriptsDirectory / name,
				std::filesystem::copy_options::overwrite_existing);
		}

		std::filesystem::path scenePath = std::filesystem::absolute(
			assetsDirectory / "Scene" / "level.tomcat");
		TomCat::SceneSerializer serializer(scene);
		Require(serializer.Serialize(scenePath), "scene serialization failed");

		// Import everything so the scene and script assets receive stable
		// handles, then bind the script references.
		TomCat::AssetManager& assets = TomCat::AssetManager::Get();
		Require(assets.SetProject(project), "asset manager setup failed");
		Require(assets.Refresh(), "asset registry refresh failed");
		const auto importDeadline = std::chrono::steady_clock::now()
			+ std::chrono::seconds(30);
		while (assets.GetImportCoordinator().GetPendingImportCount() > 0
			&& std::chrono::steady_clock::now() < importDeadline)
		{
			(void)assets.PumpImportCoordinator();
			std::this_thread::sleep_for(std::chrono::milliseconds(5));
		}
		Require(assets.GetImportCoordinator().GetPendingImportCount() == 0,
			"asset imports did not settle");

		TomCat::Ref<TomCat::Scene> rebound = TomCat::Scene::Copy(scene);
		Require(rebound != nullptr, "scene copy failed");
		for (TomCat::UUID uuid : rebound->GetEntityOrder())
		{
			TomCat::Entity entity = rebound->FindEntityByUUID(uuid);
			if (!entity || !entity.HasComponent<TomCat::CSharpScripts>())
				continue;
			for (TomCat::CSharpScriptEntry& script :
				entity.GetComponent<TomCat::CSharpScripts>().Scripts)
			{
				script.ScriptAsset = ResolveScriptAsset(
					std::filesystem::path("Scripts") / (script.LastKnownClassName
						+ ".cs"));
			}
		}
		TomCat::SceneSerializer rebinder(rebound);
		Require(rebinder.Serialize(scenePath), "scene rebinding failed");

		// Build settings: the level is the single enabled entry scene.
		const TomCat::AssetMetadata* sceneMetadata = assets.GetRegistry()
			.GetMetadata(std::filesystem::path("Scene") / "level.tomcat");
		Require(sceneMetadata != nullptr, "scene asset was not imported");
		TomCat::BuildSettings buildSettings;
		buildSettings.EntrySceneHandle = sceneMetadata->Handle;
		TomCat::BuildSceneSettings entryScene;
		entryScene.Handle = sceneMetadata->Handle;
		entryScene.Enabled = true;
		entryScene.PathHint = "Scene/level.tomcat";
		buildSettings.Scenes.push_back(entryScene);
		Require(project->SetBuildSettings(buildSettings),
			"build settings write failed");

		TomCat::PlayerSettings playerSettings = project->GetPlayerSettings();
		playerSettings.CompanyName = "TomCatSamples";
		playerSettings.ProductName = "CoinRunner";
		playerSettings.Width = 1280;
		playerSettings.Height = 720;
		Require(project->SetPlayerSettings(playerSettings),
			"player settings write failed");

		assets.Shutdown();
		TomCat::AssetJobSystem::Get().Shutdown();
		std::cout << "CoinRunner sample generated at Samples/CoinRunner\n";
		TomCat::Log::Shutdown();
		return 0;
	}
	catch (const std::exception& exception)
	{
		std::cerr << "FAIL CoinRunner generation: " << exception.what() << '\n';
		TomCat::Log::Shutdown();
		return 1;
	}
}
