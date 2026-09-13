#pragma once

#include "TomCat/Scene/Components.h"
#include "TomCat/Scene/Entity.h"
#include "TomCat/Scene/Scene.h"
#include "TomCat/Scene/Serialization/PrefabArchiveCodec.h"

#include <cmath>
#include <stdexcept>
#include <string>
#include <vector>

#include <yaml-cpp/yaml.h>

namespace PrefabRegression {

	inline void Check(bool condition, const char* message)
	{
		if (!condition)
			throw std::runtime_error(message);
	}

	inline std::vector<uint8_t> Bytes(const std::string& value)
	{
		return std::vector<uint8_t>(value.begin(), value.end());
	}

	inline void Run()
	{
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
			error.c_str());
		std::string document;
		Check(TomCat::PrefabArchiveCodec::Encode(captured, document, error),
			error.c_str());
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
			decoded, error), error.c_str());
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
			first, error), error.c_str());
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
			second, error), error.c_str());
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
			physicsArchive, error), error.c_str());
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
			runtimeOptions, runtimeResult, error), error.c_str());
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
