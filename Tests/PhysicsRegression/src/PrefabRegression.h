#pragma once

#include "TomCat/Scene/Components.h"
#include "TomCat/Scene/Entity.h"
#include "TomCat/Scene/Scene.h"
#include "TomCat/Scene/Serialization/PrefabArchiveCodec.h"
#include "TomCat/Scene/Serialization/PrefabLink.h"
#include "TomCat/Scene/Serialization/SceneArchiveCodec.h"
#include "TomCat/Asset/AssetManager.h"

#include <cmath>
#include <stdexcept>
#include <string>
#include <vector>

#include <yaml-cpp/yaml.h>

namespace PrefabRegression {

	inline void Check(bool condition, const std::string& message)
	{
		if (!condition)
			throw std::runtime_error(message);
	}

	inline std::vector<uint8_t> Bytes(const std::string& value)
	{
		return std::vector<uint8_t>(value.begin(), value.end());
	}

	inline void RunNestedAttachmentIdentityRegression()
	{
		struct Fixture {
			std::filesystem::path Root = std::filesystem::temp_directory_path()
				/ ("tomcat_nested_attachments_" + std::to_string(static_cast<uint64_t>(TomCat::UUID())));
			~Fixture() {
				TomCat::AssetManager::Get().Shutdown();
				std::error_code ignored;
				std::filesystem::remove_all(Root, ignored);
			}
		} fixture;
		auto& assets = TomCat::AssetManager::Get();
		assets.Shutdown();
		std::filesystem::create_directories(fixture.Root / "Assets");
		Check(assets.Initialize(fixture.Root / "Assets", fixture.Root / "Library"), "nested attachment fixture failed");
		auto authoring = TomCat::CreateRef<TomCat::Scene>();
		auto baseRoot = authoring->CreateEntity("Base enemy");
		const auto basePath = fixture.Root / "Assets/Base.tcprefab";
		TomCat::AssetHandle baseHandle(0), outerHandle(0);
		Check(TomCat::PrefabArchiveCodec::SaveSubtree(authoring, baseRoot, basePath, &baseHandle), "base save failed");
		TomCat::PrefabArchive base;
		std::string error;
		Check(TomCat::PrefabArchiveCodec::Load(baseHandle, base, error), error);
		auto composition = TomCat::CreateRef<TomCat::Scene>();
		auto outerRoot = composition->CreateEntity("Enemy group");
		TomCat::PrefabInstantiateOptions options;
		options.ResolveAssets = false;
		options.Parent = outerRoot.GetUUID();
		TomCat::PrefabInstantiationResult nested;
		Check(TomCat::PrefabArchiveCodec::Instantiate(base, *composition, options, nested, error), error);
		Check(TomCat::PrefabLinkedInstance::Attach(composition, baseHandle, base, nested, error), error);
		Check(TomCat::PrefabArchiveCodec::SaveSubtree(composition, outerRoot,
			fixture.Root / "Assets/Outer.tcprefab", &outerHandle), "outer save failed");

		// Keep the outer file's saved baseline unchanged, then add a script to its
		// dependency. Every Load must resolve that addition to the same identity.
		TomCat::CSharpScriptEntry script;
		script.ScriptAsset = TomCat::AssetHandle(101);
		script.LastKnownClassName = "Game.NewNestedScript";
		base.TemplateScene->FindEntityByUUID(TomCat::UUID(base.RootLocalID))
			.AddComponent<TomCat::CSharpScripts>().Scripts.push_back(script);
		std::string baseDocument;
		Check(TomCat::PrefabArchiveCodec::Encode(base, baseDocument, error), error);
		{ std::ofstream output(basePath, std::ios::binary | std::ios::trunc); output << baseDocument; }
		TomCat::PrefabArchive first, second;
		std::string firstDocument, secondDocument;
		Check(TomCat::PrefabArchiveCodec::Load(outerHandle, first, error), error);
		Check(TomCat::PrefabArchiveCodec::Load(outerHandle, second, error), error);
		Check(TomCat::PrefabArchiveCodec::Encode(first, firstDocument, error), error);
		Check(TomCat::PrefabArchiveCodec::Encode(second, secondDocument, error), error);
		Check(firstDocument == secondDocument, "nested source script addition produces unstable attachment identities");
		size_t scriptCount = 0;
		for (const auto& record : first.Entities)
		{
			const auto entity = first.TemplateScene->FindEntityByUUID(TomCat::UUID(record.LocalID));
			if (entity.HasComponent<TomCat::CSharpScripts>())
				scriptCount += entity.GetComponent<TomCat::CSharpScripts>().Scripts.size();
		}
		Check(scriptCount == 1, "nested composition did not inherit the new script");

		// An original instance uses random attachment identities at instantiation.
		// Removing and restoring a template script must recover that exact identity,
		// rather than replace it with the deterministic ID used for new additions.
		auto identityScene = TomCat::CreateRef<TomCat::Scene>();
		TomCat::PrefabInstantiateOptions identityOptions;
		identityOptions.ResolveAssets = false;
		TomCat::PrefabInstantiationResult identityInstance;
		Check(TomCat::PrefabArchiveCodec::Instantiate(base, *identityScene, identityOptions,
			identityInstance, error), error);
		Check(TomCat::PrefabLinkedInstance::Attach(identityScene, baseHandle, base, identityInstance, error), error);
		const auto identityRoot = identityInstance.Root.GetUUID();
		const auto originalAttachment = identityInstance.Root.GetComponent<TomCat::CSharpScripts>().Scripts.front().AttachmentID;
		TomCat::PrefabArchive removed = base;
		removed.TemplateScene = TomCat::Scene::Copy(base.TemplateScene);
		removed.TemplateScene->FindEntityByUUID(TomCat::UUID(removed.RootLocalID)).RemoveComponent<TomCat::CSharpScripts>();
		Check(TomCat::PrefabLinkedInstance::Update(identityScene, identityRoot, removed, false, error, false), error);
		Check(!identityScene->FindEntityByUUID(identityRoot).HasComponent<TomCat::CSharpScripts>(), "template script removal failed");
		Check(TomCat::PrefabLinkedInstance::Update(identityScene, identityRoot, base, false, error, false), error);
		Check(identityScene->FindEntityByUUID(identityRoot).GetComponent<TomCat::CSharpScripts>().Scripts.front().AttachmentID
			== originalAttachment, "restored source script lost its attachment tombstone identity");
	}

	inline void Run()
	{
		RunNestedAttachmentIdentityRegression();
		{
			auto authoring = TomCat::CreateRef<TomCat::Scene>();
			auto enemy = authoring->CreateEntity("Linked enemy");
			enemy.AddComponent<TomCat::HealthComponent>();
			auto child = authoring->CreateEntity("Weapon");
			Check(authoring->SetParent(child, enemy), "linked fixture parent failed");
			TomCat::PrefabArchive original;
			std::string error;
			Check(TomCat::PrefabArchiveCodec::CaptureSubtree(authoring, enemy, original, error), error);
			auto level = TomCat::CreateRef<TomCat::Scene>();
			TomCat::PrefabInstantiateOptions options;
			options.ResolveAssets = false;
			TomCat::PrefabInstantiationResult first, second;
			Check(TomCat::PrefabArchiveCodec::Instantiate(original, *level, options, first, error), error);
			Check(TomCat::PrefabArchiveCodec::Instantiate(original, *level, options, second, error), error);
			Check(TomCat::PrefabLinkedInstance::Attach(level, TomCat::AssetHandle(900), original, first, error), error);
			Check(TomCat::PrefabLinkedInstance::Attach(level, TomCat::AssetHandle(900), original, second, error), error);
			const auto firstID = first.Root.GetUUID(), secondID = second.Root.GetUUID();
			first.Root.GetComponent<TomCat::HealthComponent>().Current = 50;
			std::vector<std::string> overridePaths;
			Check(TomCat::PrefabLinkedInstance::GetOverridePaths(level, firstID, overridePaths, error), error);
			Check(overridePaths.size() == 1 && overridePaths.front().find("Current") != std::string::npos,
				"Prefab override inspection did not identify the changed property");
			TomCat::PrefabArchive latest;
			Check(TomCat::PrefabArchiveCodec::CaptureSubtree(authoring, enemy, latest, error), error);
			latest.TemplateScene->FindEntityByUUID(TomCat::UUID(latest.RootLocalID))
				.GetComponent<TomCat::HealthComponent>().Maximum = 200;
			Check(TomCat::PrefabLinkedInstance::Update(level, firstID, latest, false, error), error);
			Check(TomCat::PrefabLinkedInstance::Update(level, secondID, latest, false, error), error);
			Check(level->FindEntityByUUID(firstID).GetComponent<TomCat::HealthComponent>().Current == 50
				&& level->FindEntityByUUID(firstID).GetComponent<TomCat::HealthComponent>().Maximum == 200
				&& level->FindEntityByUUID(secondID).GetComponent<TomCat::HealthComponent>().Current == 100,
				"linked update lost an override or modified another instance");
			std::string saved;
			Check(TomCat::SceneArchiveCodec::Encode(level, saved, error), error);
			auto restored = TomCat::CreateRef<TomCat::Scene>();
			Check(TomCat::SceneArchiveCodec::Decode(Bytes(saved), restored, "Linked.scene", false), "linked persistence failed");
			auto localChild = restored->CreateEntity("Local child");
			auto localGrandchild = restored->CreateEntity("Local grandchild");
			const auto localChildID = localChild.GetUUID(), localGrandchildID = localGrandchild.GetUUID();
			Check(restored->SetParent(localChild, restored->FindEntityByUUID(firstID))
				&& restored->SetParent(localGrandchild, localChild), "Revert local subtree fixture failed");
			restored->DestroyEntity(restored->FindEntityByUUID(first.LocalToSceneUUID.at(2)));
			Check(TomCat::PrefabLinkedInstance::Update(restored, firstID, latest, true, error), error);
			Check(restored->FindEntityByUUID(firstID).GetComponent<TomCat::HealthComponent>().Current == 100,
				"Revert did not restore template values");
			Check(!restored->FindEntityByUUID(localChildID) && !restored->FindEntityByUUID(localGrandchildID)
				&& restored->FindEntityByUUID(first.LocalToSceneUUID.at(2)),
				"Revert did not remove added descendants and restore a deleted template node");
			localChild = restored->CreateEntity("Referenced local child");
			Check(restored->SetParent(localChild, restored->FindEntityByUUID(firstID)), "Revert reference fixture failed");
			restored->CreateEntity("External joint").AddComponent<TomCat::DistanceJoint2D>().ConnectedEntity = localChild.GetUUID();
			std::string beforeRejectedRevert, afterRejectedRevert;
			Check(TomCat::SceneArchiveCodec::Encode(restored, beforeRejectedRevert, error), error);
			Check(!TomCat::PrefabLinkedInstance::Update(restored, firstID, latest, true, error),
				"Revert accepted a dangling external joint reference");
			Check(TomCat::SceneArchiveCodec::Encode(restored, afterRejectedRevert, error)
				&& beforeRejectedRevert == afterRejectedRevert, "rejected structural Revert changed the scene");
			TomCat::PrefabArchive applied;
			Check(TomCat::PrefabLinkedInstance::Capture(level, firstID, applied, error), error);
			Check(applied.RootLocalID == original.RootLocalID && applied.Entities.size() == 2
				&& applied.TemplateScene->FindEntityByUUID(TomCat::UUID(applied.RootLocalID))
				.GetComponent<TomCat::HealthComponent>().Current == 50, "Apply capture lost stable identities or overrides");
			const auto weaponID = first.LocalToSceneUUID.at(2);
			level->RenameEntity(level->FindEntityByUUID(weaponID), "Overridden weapon");
			latest.TemplateScene->DestroyEntity(latest.TemplateScene->FindEntityByUUID(TomCat::UUID(2)));
			latest.Entities.erase(latest.Entities.begin() + 1);
			Check(TomCat::SceneArchiveCodec::Encode(level, saved, error), error);
			Check(!TomCat::PrefabLinkedInstance::Update(level, firstID, latest, false, error),
				"template deletion silently discarded an overridden node");
			std::string after;
			Check(TomCat::SceneArchiveCodec::Encode(level, after, error) && saved == after,
				"failed linked update changed the scene");
		}
		{
			struct Fixture {
				std::filesystem::path Root = std::filesystem::temp_directory_path()
					/ ("tomcat_linked_prefab_" + std::to_string(static_cast<uint64_t>(TomCat::UUID())));
				~Fixture() {
					TomCat::AssetManager::Get().Shutdown();
					std::error_code ignored;
					std::filesystem::remove_all(Root, ignored);
				}
			} fixture;
			auto& assets = TomCat::AssetManager::Get();
			assets.Shutdown();
			std::filesystem::create_directories(fixture.Root / "Assets");
			Check(assets.Initialize(fixture.Root / "Assets", fixture.Root / "Library"), "linked asset fixture failed");
			auto authoring = TomCat::CreateRef<TomCat::Scene>();
			auto root = authoring->CreateEntity("Enemy");
			root.AddComponent<TomCat::HealthComponent>();
			TomCat::AssetHandle handle(0);
			const auto path = fixture.Root / "Assets/Enemy.tcprefab";
			Check(TomCat::PrefabArchiveCodec::SaveSubtree(authoring, root, path, &handle), "linked template save failed");
			std::string error;
			TomCat::PrefabArchive original;
			Check(TomCat::PrefabArchiveCodec::Load(handle, original, error), error);
			auto level = TomCat::CreateRef<TomCat::Scene>();
			TomCat::PrefabInstantiationResult first, second;
			TomCat::PrefabInstantiateOptions options;
			options.ResolveAssets = false;
			options.RootWorldPosition = glm::vec3(37.0f, 12.0f, 0.0f);
			Check(TomCat::PrefabArchiveCodec::Instantiate(original, *level, options, first, error), error);
			options.RootWorldPosition = glm::vec3(-5.0f, 0.0f, 0.0f);
			Check(TomCat::PrefabArchiveCodec::Instantiate(original, *level, options, second, error), error);
			Check(TomCat::PrefabLinkedInstance::Attach(level, handle, original, first, error), error);
			Check(TomCat::PrefabLinkedInstance::Attach(level, handle, original, second, error), error);
			const auto firstID = first.Root.GetUUID(), secondID = second.Root.GetUUID();
			first.Root.GetComponent<TomCat::HealthComponent>().Maximum = 300;
			second.Root.GetComponent<TomCat::HealthComponent>().Current = 20;
			auto added = level->CreateEntity("Added weapon");
			const auto addedID = added.GetUUID();
			Check(level->SetParent(added, first.Root), "Apply child parent failed");
			Check(TomCat::PrefabLinkedInstance::Apply(level, firstID, error), error);
			Check(level->FindEntityByUUID(addedID)
				&& level->FindEntityByUUID(secondID).GetComponent<TomCat::HealthComponent>().Maximum == 300
				&& level->FindEntityByUUID(secondID).GetComponent<TomCat::HealthComponent>().Current == 20,
				"Apply failed to preserve identities or propagate while retaining other overrides");
			Check(level->FindEntityByUUID(firstID).GetComponent<TomCat::Transform>()._Translation.x == 37.0f
				&& level->FindEntityByUUID(secondID).GetComponent<TomCat::Transform>()._Translation.x == -5.0f,
				"Apply changed level placement");
			TomCat::PrefabArchive applied;
			Check(TomCat::PrefabArchiveCodec::Load(handle, applied, error), error);
			Check(applied.TemplateScene->FindEntityByUUID(TomCat::UUID(applied.RootLocalID))
				.GetComponent<TomCat::Transform>()._Translation.x == 0.0f,
				"Apply copied level placement into the template");
			bool changed = true;
			Check(TomCat::PrefabLinkedInstance::RefreshAll(level, changed, error) && !changed,
				"unchanged templates caused a spurious Scene replacement");
			applied.TemplateScene->FindEntityByUUID(TomCat::UUID(applied.RootLocalID))
				.GetComponent<TomCat::HealthComponent>().Maximum = 400;
			std::string document;
			Check(TomCat::PrefabArchiveCodec::Encode(applied, document, error), error);
			std::ofstream output(path, std::ios::binary | std::ios::trunc);
			output << document;
			output.close();
			Check(TomCat::PrefabLinkedInstance::RefreshAll(level, changed, error) && changed, error);
			Check(level->FindEntityByUUID(secondID).GetComponent<TomCat::HealthComponent>().Maximum == 400
				&& level->FindEntityByUUID(secondID).GetComponent<TomCat::HealthComponent>().Current == 20,
				"external template refresh lost overrides");
			auto duplicate = level->DuplicateEntity(level->FindEntityByUUID(secondID));
			Check(duplicate && duplicate.HasComponent<TomCat::PrefabLink>(), "duplicate lost its Prefab link");
			const auto duplicateID = duplicate.GetUUID();
			Check(TomCat::PrefabLinkedInstance::Update(level, duplicateID, applied, true, error), error);
			Check(level->FindEntityByUUID(duplicateID).GetComponent<TomCat::HealthComponent>().Current == 100
				&& level->FindEntityByUUID(secondID).GetComponent<TomCat::HealthComponent>().Current == 20,
				"duplicate Prefab baseline still targets the original instance");
			level->FindEntityByUUID(firstID).GetComponent<TomCat::HealthComponent>().Current = 60;
			const auto variantPath = fixture.Root / "Assets/EnemyVariant.tcprefab";
			TomCat::AssetHandle variantHandle(0);
			Check(TomCat::PrefabArchiveCodec::SaveSubtree(level, level->FindEntityByUUID(firstID),
				variantPath, &variantHandle), "variant save failed");
			applied.TemplateScene->FindEntityByUUID(TomCat::UUID(applied.RootLocalID))
				.GetComponent<TomCat::HealthComponent>().Maximum = 500;
			Check(TomCat::PrefabArchiveCodec::Encode(applied, document, error), error);
			{ std::ofstream file(path, std::ios::binary | std::ios::trunc); file << document; }
			TomCat::PrefabArchive variant;
			Check(TomCat::PrefabArchiveCodec::Load(variantHandle, variant, error), error);
			const auto variantHealth = variant.TemplateScene->FindEntityByUUID(TomCat::UUID(variant.RootLocalID))
				.GetComponent<TomCat::HealthComponent>();
			Check(variantHealth.Maximum == 500 && variantHealth.Current == 60,
				"variant did not inherit base changes while retaining its override");
			auto group = level->CreateEntity("Enemy group");
			Check(level->SetParent(level->FindEntityByUUID(firstID), group), "nested parent failed");
			TomCat::AssetHandle groupHandle(0);
			Check(TomCat::PrefabArchiveCodec::SaveSubtree(level, group,
				fixture.Root / "Assets/Group.tcprefab", &groupHandle), "nested Prefab save failed");
			TomCat::PrefabArchive nested;
			Check(TomCat::PrefabArchiveCodec::Load(groupHandle, nested, error), error);
			bool nestedFound = false;
			for (const auto& record : nested.Entities)
			{
				auto e = nested.TemplateScene->FindEntityByUUID(TomCat::UUID(record.LocalID));
				if (e.HasComponent<TomCat::HealthComponent>())
					nestedFound = e.GetComponent<TomCat::HealthComponent>().Maximum == 500;
			}
			Check(nestedFound, "nested Prefab source did not resolve");
			// Applying A with nested A, or nested Group -> A, must fail before writing A.
			for (const auto dependency : { handle, groupHandle })
			{
				auto cyclicScene = TomCat::CreateRef<TomCat::Scene>();
				TomCat::PrefabArchive dependencyArchive;
				Check(TomCat::PrefabArchiveCodec::Load(dependency, dependencyArchive, error), error);
				TomCat::PrefabInstantiationResult outer, inner;
				Check(TomCat::PrefabArchiveCodec::Instantiate(applied, *cyclicScene, options, outer, error), error);
				Check(TomCat::PrefabLinkedInstance::Attach(cyclicScene, handle, applied, outer, error), error);
				Check(TomCat::PrefabArchiveCodec::Instantiate(dependencyArchive, *cyclicScene, options, inner, error), error);
				Check(TomCat::PrefabLinkedInstance::Attach(cyclicScene, dependency, dependencyArchive, inner, error), error);
				Check(cyclicScene->SetParent(inner.Root, outer.Root), "cyclic Apply fixture failed");
				std::string sceneBefore, sceneAfter;
				Check(TomCat::SceneArchiveCodec::Encode(cyclicScene, sceneBefore, error), error);
				Check(!TomCat::PrefabLinkedInstance::Apply(cyclicScene, outer.Root.GetUUID(), error)
					&& error.find("cycle") != std::string::npos, "Apply accepted a composition cycle");
				std::ifstream input(path, std::ios::binary);
				const std::string persisted{ std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>() };
				Check(persisted == document && TomCat::SceneArchiveCodec::Encode(cyclicScene, sceneAfter, error)
					&& sceneBefore == sceneAfter, "rejected cyclic Apply changed the template or scene");
			}
			// A malformed dependency cycle is rejected without replacing the caller's archive.
			variant.TemplateScene->FindEntityByUUID(TomCat::UUID(variant.RootLocalID))
				.GetComponent<TomCat::PrefabLink>().Source = variantHandle;
			Check(TomCat::PrefabArchiveCodec::Encode(variant, document, error), error);
			{ std::ofstream file(variantPath, std::ios::binary | std::ios::trunc); file << document; }
			Check(!TomCat::PrefabArchiveCodec::Load(variantHandle, variant, error)
				&& error.find("cycle") != std::string::npos, "variant cycle was accepted");
		}
		Check(static_cast<uint16_t>(TomCat::AssetType::Prefab) == 10
			&& TomCat::AssetTypeFromPath("Fixture.tcprefab")
				== TomCat::AssetType::Prefab,
			"Prefab AssetType changed value or extension mapping");

		auto source = TomCat::CreateRef<TomCat::Scene>();
		TomCat::Entity outside = source->CreateEntity("Outside parent");
		outside.GetComponent<TomCat::Transform>()._Translation = { 3.0f, 4.0f, 0.0f };
		outside.GetComponent<TomCat::Transform>()._LocalTranslation = { 3.0f, 4.0f, 0.0f };
		TomCat::Entity root = source->CreateEntity("Prefab root");
		root.GetComponent<TomCat::Transform>()._Translation = { 10.0f, 20.0f, 0.0f };
		root.GetComponent<TomCat::Transform>()._LocalTranslation = { 10.0f, 20.0f, 0.0f };
		Check(source->SetParent(root, outside), "could not parent source Prefab root");
		TomCat::Entity child = source->CreateEntity("Prefab child");
		child.GetComponent<TomCat::Transform>()._Translation = { 12.0f, 20.0f, 0.0f };
		child.GetComponent<TomCat::Transform>()._LocalTranslation = { 12.0f, 20.0f, 0.0f };
		Check(source->SetParent(child, root), "could not parent source Prefab child");

		auto& joint = child.AddComponent<TomCat::DistanceJoint2D>();
		joint.ConnectedEntity = root.GetUUID();
		TomCat::CSharpScriptEntry script;
		const TomCat::UUID originalAttachment = script.AttachmentID;
		script.ScriptAsset = TomCat::AssetHandle(101);
		script.LastKnownClassName = "Game.PrefabProbe";
		script.Fields.emplace_back("aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa", "Target",
			TomCat::ScriptFieldType::Entity,
			static_cast<uint64_t>(child.GetUUID()));
		script.Fields.emplace_back("bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb", "Nested",
			TomCat::ScriptFieldType::AssetRef, uint64_t{ 0 },
			"TomCat.PrefabAsset");
		root.AddComponent<TomCat::CSharpScripts>().Scripts.push_back(script);

		TomCat::PrefabArchive captured;
		std::string error;
		Check(TomCat::PrefabArchiveCodec::CaptureSubtree(source, root, captured, error),
			error);
		std::string document;
		Check(TomCat::PrefabArchiveCodec::Encode(captured, document, error),
			error);
		const YAML::Node prefabDocument = YAML::Load(document);
		Check(prefabDocument["SchemaVersion"].as<uint32_t>() == 1
			&& prefabDocument["RootLocalID"].as<uint64_t>() == 1
			&& prefabDocument["Entities"].size() == 2,
			"Prefab header or stable LocalID set was not emitted");
		Check(prefabDocument["Entities"][0]["LocalID"].as<uint64_t>() == 1
			&& !prefabDocument["Entities"][0]["Entity"]
			&& prefabDocument["Entities"][0]["Parent"].as<uint64_t>() == 0,
			"Prefab leaked a Scene UUID or external root parent");
		Check(document.find(std::to_string(static_cast<uint64_t>(root.GetUUID())))
			== std::string::npos
			&& document.find(std::to_string(static_cast<uint64_t>(child.GetUUID())))
				== std::string::npos,
			"Prefab document contains source Scene UUID text");

		TomCat::PrefabArchive decoded;
		Check(TomCat::PrefabArchiveCodec::Decode(Bytes(document), "Memory.tcprefab",
			decoded, error), error);
		TomCat::Entity decodedRoot = decoded.TemplateScene->FindEntityByUUID(TomCat::UUID(1));
		TomCat::Entity decodedChild = decoded.TemplateScene->FindEntityByUUID(TomCat::UUID(2));
		Check(decodedRoot && decodedChild
			&& decodedChild.GetComponent<TomCat::DistanceJoint2D>().ConnectedEntity
				== TomCat::UUID(1),
			"Prefab LocalID joint target did not round-trip");
		const auto& decodedFields = decodedRoot.GetComponent<TomCat::CSharpScripts>()
			.Scripts.front().Fields;
		Check(std::get<uint64_t>(decodedFields[0].Value) == 2
			&& decodedFields[1].TypeName == "TomCat.PrefabAsset",
			"Prefab C# Entity reference or managed TypeName did not round-trip");

		YAML::Node invalidDocument = YAML::Load(document);
		invalidDocument["Entities"][0]["CSharpScripts"]["Scripts"][0]
			["Fields"][0]["Value"] = 9999;
		YAML::Emitter invalidOutput;
		invalidOutput << invalidDocument;
		TomCat::PrefabArchive untouched;
		untouched.RootLocalID = 77;
		Check(!TomCat::PrefabArchiveCodec::Decode(Bytes(invalidOutput.c_str()),
			"Invalid.tcprefab", untouched, error)
			&& untouched.RootLocalID == 77,
			"invalid Prefab Entity reference partially committed decoded state");

		TomCat::Scene destination;
		TomCat::Entity destinationParent = destination.CreateEntity("Destination parent");
		destinationParent.GetComponent<TomCat::Transform>()._Translation = { 5.0f, 5.0f, 0.0f };
		destinationParent.GetComponent<TomCat::Transform>()._LocalTranslation = { 5.0f, 5.0f, 0.0f };
		TomCat::PrefabInstantiateOptions options;
		options.RootWorldPosition = glm::vec3(42.0f, 43.0f, 0.0f);
		options.Parent = destinationParent.GetUUID();
		options.ResolveAssets = false;
		TomCat::PrefabInstantiationResult first;
		Check(TomCat::PrefabArchiveCodec::Instantiate(decoded, destination, options,
			first, error), error);
		Check(first.Root && first.Entities.size() == 2
			&& destination.GetParent(first.Root) == destinationParent,
			"Prefab instance result or optional parent is invalid");
		const auto& firstTransform = first.Root.GetComponent<TomCat::Transform>();
		Check(std::abs(firstTransform._Translation.x - 42.0f) < 0.001f
			&& std::abs(firstTransform._Translation.y - 43.0f) < 0.001f,
			"Prefab root world-position override was not preserved through parenting");
		const TomCat::UUID firstRootID = first.LocalToSceneUUID.at(1);
		const TomCat::UUID firstChildID = first.LocalToSceneUUID.at(2);
		Check(firstRootID != root.GetUUID() && firstChildID != child.GetUUID()
			&& destination.GetParent(first.Entities[1]) == first.Root
			&& first.Entities[1].GetComponent<TomCat::DistanceJoint2D>().ConnectedEntity
				== firstRootID,
			"Prefab UUID/hierarchy/joint remap failed");
		const auto& firstScript = first.Root.GetComponent<TomCat::CSharpScripts>()
			.Scripts.front();
		const TomCat::UUID firstAttachment = firstScript.AttachmentID;
		Check(firstScript.AttachmentID != originalAttachment
			&& std::get<uint64_t>(firstScript.Fields[0].Value)
				== static_cast<uint64_t>(firstChildID),
			"Prefab C# AttachmentID or Entity field was not remapped");

		TomCat::PrefabInstantiationResult second;
		options.Parent.reset();
		Check(TomCat::PrefabArchiveCodec::Instantiate(decoded, destination, options,
			second, error), error);
		Check(second.Root.GetUUID() != first.Root.GetUUID()
			&& second.Root.GetComponent<TomCat::CSharpScripts>().Scripts.front()
				.AttachmentID != firstAttachment,
			"separate Prefab instances reused UUID or AttachmentID identity");

		auto physicsSource = TomCat::CreateRef<TomCat::Scene>();
		TomCat::Entity physicsRoot = physicsSource->CreateEntity("Runtime Prefab");
		physicsRoot.AddComponent<TomCat::Rigidbody2D>().Type =
			TomCat::Rigidbody2D::BodyType::Dynamic;
		physicsRoot.AddComponent<TomCat::BoxCollider2D>();
		TomCat::PrefabArchive physicsArchive;
		Check(TomCat::PrefabArchiveCodec::CaptureSubtree(physicsSource, physicsRoot,
			physicsArchive, error), error);
		TomCat::Scene runtimeScene;
		Check(runtimeScene.OnRuntimeStart(), "could not start script-free runtime Scene");
		std::vector<TomCat::UUID> delivered;
		bool physicsReadyAtDelivery = false;
		runtimeScene.SetRuntimeEntityBatchCreatedCallback(
			[&](std::span<const TomCat::UUID> ids)
			{
				delivered.assign(ids.begin(), ids.end());
				TomCat::Entity created = ids.empty()
					? TomCat::Entity{} : runtimeScene.FindEntityByUUID(ids.front());
				physicsReadyAtDelivery = created
					&& created.GetComponent<TomCat::Rigidbody2D>().RuntimeBody != nullptr;
			});
		TomCat::PrefabInstantiationResult runtimeResult;
		TomCat::PrefabInstantiateOptions runtimeOptions;
		runtimeOptions.ResolveAssets = false;
		Check(TomCat::PrefabArchiveCodec::Instantiate(physicsArchive, runtimeScene,
			runtimeOptions, runtimeResult, error), error);
		Check(delivered.empty()
			&& runtimeScene.GetPendingRuntimeEntityCreateCount() == 1
			&& runtimeResult.Root.GetComponent<TomCat::Rigidbody2D>().RuntimeBody == nullptr,
			"runtime Prefab creation bypassed the pending physics-safe seam");
		runtimeScene.OnRuntimeStep();
		Check(delivered.size() == 1 && delivered.front() == runtimeResult.Root.GetUUID()
			&& physicsReadyAtDelivery
			&& runtimeScene.GetPendingRuntimeEntityCreateCount() == 0,
			"runtime Prefab UUID batch was not delivered at the physics safe point");
		runtimeScene.OnRuntimeStop();
	}

}
