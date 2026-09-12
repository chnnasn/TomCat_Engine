#pragma once

#include "TomCat/Scene/ComponentRegistry.h"
#include "TomCat/Scene/Components.h"
#include "TomCat/Scene/Scene.h"
#include "TomCat/Scene/Serialization/PrefabArchiveCodec.h"
#include "TomCat/Scene/Serialization/SceneArchiveCodec.h"

#include <algorithm>
#include <stdexcept>
#include <string>
#include <vector>

#include <yaml-cpp/yaml.h>

namespace ComponentRegistryRegression {

	inline void Check(bool condition, const std::string& message)
	{
		if (!condition)
			throw std::runtime_error(message);
	}

	inline std::vector<uint8_t> Bytes(const std::string& value)
	{
		return { value.begin(), value.end() };
	}

	inline void RequireHealth(TomCat::Entity entity, int32_t maximum,
		int32_t current, bool invulnerable, const char* context)
	{
		Check(entity && entity.HasComponent<TomCat::HealthComponent>(), context);
		const auto& health = entity.GetComponent<TomCat::HealthComponent>();
		Check(health.Maximum == maximum && health.Current == current
			&& health.Invulnerable == invulnerable, context);
	}

	inline void Run()
	{
		TomCat::ComponentRegistry& registry = TomCat::ComponentRegistry::Get();
		const TomCat::ComponentDescriptor* descriptor = registry.Find(
			TomCat::UUID(TomCat::ComponentIds::Health));
		Check(descriptor && descriptor->StableName == "TomCat.HealthComponent"
			&& descriptor->SchemaVersion == 1 && descriptor->Properties.size() == 3,
			"Health descriptor is absent or unstable");
		Check(static_cast<uint64_t>(descriptor->TypeId)
			== TomCat::ComponentIds::Health
			&& static_cast<uint64_t>(descriptor->Properties[0].PropertyId)
				== TomCat::ComponentIds::HealthProperties::Maximum
			&& static_cast<uint64_t>(descriptor->Properties[1].PropertyId)
				== TomCat::ComponentIds::HealthProperties::Current
			&& static_cast<uint64_t>(descriptor->Properties[2].PropertyId)
				== TomCat::ComponentIds::HealthProperties::Invulnerable,
			"Health descriptor did not expose its explicit persisted UUIDs");

		auto requireAssetProperty = [&](uint64_t typeId, uint64_t propertyId,
			const char* context) -> const TomCat::PropertyDescriptor&
		{
			const TomCat::ComponentDescriptor* component = registry.Find(
				TomCat::UUID(typeId));
			Check(component != nullptr, context);
			const auto property = std::find_if(component->Properties.begin(),
				component->Properties.end(), [propertyId](const auto& candidate)
				{
					return static_cast<uint64_t>(candidate.PropertyId) == propertyId;
				});
			Check(property != component->Properties.end(), context);
			Check(property->Kind == TomCat::PropertyKind::UInt64
				&& property->AssetReference.has_value(), context);
			return *property;
		};
		for (const uint64_t propertyId : {
			TomCat::ComponentIds::TextRendererProperties::Font,
			TomCat::ComponentIds::TextRendererProperties::FallbackFont,
			TomCat::ComponentIds::TextRendererProperties::EmojiFont })
		{
			const auto& property = requireAssetProperty(
				TomCat::ComponentIds::TextRenderer, propertyId,
				"TextRenderer font property lacks typed asset metadata");
			Check(property.AssetReference->Accepts(TomCat::AssetType::Font)
				&& !property.AssetReference->Accepts(TomCat::AssetType::Texture2D)
				&& !property.AssetReference->Accepts(TomCat::AssetType::Font, true),
				"TextRenderer font metadata accepts a wrong type or subasset");
		}
		for (const uint64_t propertyId : {
			TomCat::ComponentIds::UITextProperties::Font,
			TomCat::ComponentIds::UITextProperties::FallbackFont,
			TomCat::ComponentIds::UITextProperties::EmojiFont })
		{
			const auto& property = requireAssetProperty(
				TomCat::ComponentIds::UIText, propertyId,
				"UIText font property lacks typed asset metadata");
			Check(property.AssetReference->Accepts(TomCat::AssetType::Font)
				&& !property.AssetReference->Accepts(TomCat::AssetType::Texture2D)
				&& !property.AssetReference->Accepts(TomCat::AssetType::Font, true),
				"UIText font metadata accepts a wrong type or subasset");
		}
		const auto& imageProperty = requireAssetProperty(
			TomCat::ComponentIds::UIImage,
			TomCat::ComponentIds::UIImageProperties::Image,
			"UIImage property lacks typed asset metadata");
		Check(imageProperty.AssetReference->Accepts(TomCat::AssetType::Texture2D)
			&& imageProperty.AssetReference->Accepts(
				TomCat::AssetType::Texture2D, true)
			&& !imageProperty.AssetReference->Accepts(TomCat::AssetType::Font),
			"UIImage metadata does not constrain Texture2D/Sprite references");

		auto source = TomCat::CreateRef<TomCat::Scene>();
		source->SetSceneName("Component registry vertical slice");
		TomCat::Entity root = source->CreateEntity("Registered health");
		std::string error;
		Check(registry.Add(root, TomCat::UUID(TomCat::ComponentIds::Health), error), error);
		Check(descriptor->Properties[0].Set(root, int32_t{ 250 }, error), error);
		Check(descriptor->Properties[1].Set(root, int32_t{ 175 }, error), error);
		Check(descriptor->Properties[2].Set(root, true, error), error);
		RequireHealth(root, 250, 175, true, "descriptor setters changed Health incorrectly");
		Check(!descriptor->Properties[1].Set(root, int32_t{ 251 }, error)
			&& root.GetComponent<TomCat::HealthComponent>().Current == 175,
			"Health setter accepted an invalid value or partially wrote it");

		Check(registry.Remove(root, TomCat::UUID(TomCat::ComponentIds::Health), error)
			&& !root.HasComponent<TomCat::HealthComponent>(),
			"registry Remove did not remove Health");
		Check(registry.Add(root, TomCat::UUID(TomCat::ComponentIds::Health), error), error);
		Check(descriptor->Properties[0].Set(root, int32_t{ 250 }, error), error);
		Check(descriptor->Properties[1].Set(root, int32_t{ 175 }, error), error);
		Check(descriptor->Properties[2].Set(root, true, error), error);

		const TomCat::UUID rootId = root.GetUUID();
		auto copied = TomCat::Scene::Copy(source);
		Check(copied != nullptr, "Scene::Copy failed for a registered component");
		RequireHealth(copied->FindEntityByUUID(rootId), 250, 175, true,
			"Scene::Copy omitted or changed registered Health");
		TomCat::Entity duplicate = source->DuplicateEntity(root);
		RequireHealth(duplicate, 250, 175, true,
			"DuplicateEntity omitted or changed registered Health");

		std::string sceneDocument;
		Check(TomCat::SceneArchiveCodec::Encode(source, sceneDocument, error), error);
		YAML::Node sceneRoot = YAML::Load(sceneDocument);
		Check(sceneRoot["SchemaVersion"].as<uint32_t>() == 11,
			"registered component scene did not use Schema 11");
		const YAML::Node components = sceneRoot["Entities"][0]["Components"];
		Check(components && components.IsSequence() && components.size() == 1
			&& components[0]["TypeId"].as<uint64_t>()
				== TomCat::ComponentIds::Health
			&& components[0]["Properties"][1]["PropertyId"].as<uint64_t>()
				== TomCat::ComponentIds::HealthProperties::Current,
			"Schema 11 omitted stable component/property UUIDs");
		auto loaded = TomCat::CreateRef<TomCat::Scene>();
		Check(TomCat::SceneArchiveCodec::Decode(Bytes(sceneDocument), loaded,
			"ComponentRegistry.scene", false), "Schema 11 registry decode failed");
		RequireHealth(loaded->FindEntityByUUID(rootId), 250, 175, true,
			"Scene save/load omitted or changed registered Health");

		YAML::Node lowHealthRoot = YAML::Load(sceneDocument);
		lowHealthRoot["Entities"][0]["Components"][0]["Properties"][0]["Value"] = 50;
		lowHealthRoot["Entities"][0]["Components"][0]["Properties"][1]["Value"] = 25;
		YAML::Emitter lowHealthEmitter;
		lowHealthEmitter << lowHealthRoot;
		auto lowHealthScene = TomCat::CreateRef<TomCat::Scene>();
		Check(TomCat::SceneArchiveCodec::Decode(Bytes(lowHealthEmitter.c_str()),
			lowHealthScene, "LowHealth.scene", false),
			"Health decode rejected values below its constructor defaults");
		RequireHealth(lowHealthScene->FindEntityByUUID(rootId), 50, 25, true,
			"Health transactional decode changed low persisted values");

		TomCat::PrefabArchive prefab;
		Check(TomCat::PrefabArchiveCodec::CaptureSubtree(source, root, prefab, error), error);
		std::string prefabDocument;
		Check(TomCat::PrefabArchiveCodec::Encode(prefab, prefabDocument, error), error);
		TomCat::PrefabArchive decodedPrefab;
		Check(TomCat::PrefabArchiveCodec::Decode(Bytes(prefabDocument),
			"ComponentRegistry.tcprefab", decodedPrefab, error), error);
		RequireHealth(decodedPrefab.TemplateScene->FindEntityByUUID(TomCat::UUID(1)),
			250, 175, true, "Prefab encode/decode omitted registered Health");
		TomCat::Scene prefabDestination;
		TomCat::PrefabInstantiationResult prefabResult;
		TomCat::PrefabInstantiateOptions options;
		options.ResolveAssets = false;
		Check(TomCat::PrefabArchiveCodec::Instantiate(decodedPrefab,
			prefabDestination, options, prefabResult, error), error);
		RequireHealth(prefabResult.Root, 250, 175, true,
			"Prefab instantiation omitted registered Health");

		// Simulate opening the scene without its component provider. The wrapper is
		// understood, while the nested payload is intentionally provider-defined.
		YAML::Node missingRoot = YAML::Load(sceneDocument);
		YAML::Node missingRecord = missingRoot["Entities"][0]["Components"][0];
		constexpr uint64_t missingTypeId = 0xf1632f30522247bbULL;
		missingRecord["TypeId"] = missingTypeId;
		missingRecord["StableName"] = "Plugin.FutureHealth";
		missingRecord["SchemaVersion"] = 7;
		missingRecord["Properties"][0]["PluginPayload"]["Nested"] = "keep-me";
		YAML::Emitter missingEmitter;
		missingEmitter << missingRoot;
		auto missingScene = TomCat::CreateRef<TomCat::Scene>();
		Check(TomCat::SceneArchiveCodec::Decode(Bytes(missingEmitter.c_str()),
			missingScene, "MissingComponent.scene", false),
			"unknown registered component could not be loaded as opaque");
		TomCat::Entity missingEntity = missingScene->FindEntityByUUID(rootId);
		Check(missingEntity && !missingEntity.HasComponent<TomCat::HealthComponent>()
			&& missingEntity.HasComponent<TomCat::OpaqueComponents>()
			&& missingEntity.GetComponent<TomCat::OpaqueComponents>().Records.size() == 1,
			"unknown component did not become one Missing Component record");
		std::string resavedMissing;
		Check(TomCat::SceneArchiveCodec::Encode(missingScene, resavedMissing, error), error);
		YAML::Node preserved = YAML::Load(resavedMissing)["Entities"][0]["Components"][0];
		Check(preserved["TypeId"].as<uint64_t>() == missingTypeId
			&& preserved["SchemaVersion"].as<uint32_t>() == 7
			&& preserved["Properties"][0]["PluginPayload"]["Nested"].as<std::string>()
				== "keep-me",
			"Missing Component payload was lost while resaving");
		auto copiedMissing = TomCat::Scene::Copy(missingScene);
		Check(copiedMissing && copiedMissing->FindEntityByUUID(rootId)
			.HasComponent<TomCat::OpaqueComponents>(),
			"Scene::Copy omitted Missing Component payload");
	}

}
