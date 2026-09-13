#pragma once

#include "TomCat/Scene/ComponentRegistry.h"
#include "TomCat/Scene/Components.h"
#include "TomCat/Scene/Scene.h"
#include "TomCat/Scene/Serialization/ComponentCodecs.h"
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

	struct PluginCounter
	{
		int32_t Value = 0;
	};

	struct PluginEntityLinks
	{
		uint64_t Target = 0;
		std::vector<uint64_t> RelatedTargets;
	};

	inline TomCat::ComponentDescriptor MakePluginEntityLinksDescriptor()
	{
		constexpr uint64_t providerId = 0xfed76d3b148c48a1ULL;
		constexpr uint64_t typeId = 0xd7a041f93f9d43d3ULL;
		constexpr uint64_t targetPropertyId = 0x935d7f1f95af48c2ULL;
		TomCat::ComponentDescriptor descriptor;
		descriptor.ProviderId = TomCat::UUID(providerId);
		descriptor.TypeId = TomCat::UUID(typeId);
		descriptor.StableName = "Regression.PluginEntityLinks";
		descriptor.DisplayName = "Plugin Entity Links";
		descriptor.Has = [](TomCat::Entity entity)
		{
			return entity && entity.HasComponent<PluginEntityLinks>();
		};
		descriptor.Add = [](TomCat::Entity entity, std::string& error)
		{
			if (!entity) { error = "invalid entity"; return false; }
			if (!entity.HasComponent<PluginEntityLinks>())
				entity.AddComponent<PluginEntityLinks>();
			return true;
		};
		descriptor.Remove = [](TomCat::Entity entity, std::string& error)
		{
			if (!entity || !entity.HasComponent<PluginEntityLinks>())
			{
				error = "PluginEntityLinks is absent";
				return false;
			}
			entity.RemoveComponent<PluginEntityLinks>();
			return true;
		};
		descriptor.Copy = [](TomCat::Entity source, TomCat::Entity destination,
			std::string& error)
		{
			if (!source || !destination
				|| !source.HasComponent<PluginEntityLinks>())
			{
				error = "invalid PluginEntityLinks copy";
				return false;
			}
			destination.AddOrReplaceComponent<PluginEntityLinks>(
				source.GetComponent<PluginEntityLinks>());
			return true;
		};
		TomCat::PropertyDescriptor target;
		target.PropertyId = TomCat::UUID(targetPropertyId);
		target.StableName = "Target";
		target.DisplayName = "Target";
		target.Kind = TomCat::PropertyKind::UInt64;
		target.EntityReference = true;
		target.Get = [](TomCat::Entity entity) -> TomCat::PropertyValue
		{
			return entity.GetComponent<PluginEntityLinks>().Target;
		};
		target.Set = [](TomCat::Entity entity, const TomCat::PropertyValue& value,
			std::string&)
		{
			entity.GetComponent<PluginEntityLinks>().Target =
				std::get<uint64_t>(value);
			return true;
		};
		descriptor.Properties.push_back(std::move(target));
		descriptor.RemapEntityReferences = [](TomCat::Entity entity,
			const TomCat::EntityReferenceMapper& remap, std::string& error)
		{
			auto& links = entity.GetComponent<PluginEntityLinks>();
			for (size_t index = 0; index < links.RelatedTargets.size(); ++index)
			{
				const std::string context = "Regression.PluginEntityLinks.RelatedTargets["
					+ std::to_string(index) + "]";
				if (!remap(links.RelatedTargets[index], context, error))
					return false;
			}
			return true;
		};
		return descriptor;
	}

	inline void TestPluginEntityReferenceRemap(TomCat::ComponentRegistry& registry)
	{
		constexpr uint64_t providerId = 0xfed76d3b148c48a1ULL;
		constexpr uint64_t typeId = 0xd7a041f93f9d43d3ULL;
		std::string error;
		Check(registry.Register(MakePluginEntityLinksDescriptor(), error), error);

		auto source = TomCat::CreateRef<TomCat::Scene>();
		TomCat::Entity root = source->CreateEntity("Plugin reference root");
		TomCat::Entity child = source->CreateEntity("Plugin reference child");
		TomCat::Entity outside = source->CreateEntity("Plugin reference outside");
		Check(source->SetParent(child, root), "could not parent plugin reference child");
		Check(registry.Add(root, TomCat::UUID(typeId), error), error);
		auto& sourceLinks = root.GetComponent<PluginEntityLinks>();
		sourceLinks.Target = static_cast<uint64_t>(child.GetUUID());
		sourceLinks.RelatedTargets = {
			static_cast<uint64_t>(child.GetUUID()), 0 };

		TomCat::PrefabArchive prefab;
		Check(TomCat::PrefabArchiveCodec::CaptureSubtree(source, root, prefab, error),
			error);
		TomCat::Entity templateRoot =
			prefab.TemplateScene->FindEntityByUUID(TomCat::UUID(1));
		Check(templateRoot && templateRoot.HasComponent<PluginEntityLinks>()
			&& templateRoot.GetComponent<PluginEntityLinks>().Target == 2
			&& templateRoot.GetComponent<PluginEntityLinks>().RelatedTargets[0] == 2,
			"plugin entity references were not remapped to Prefab LocalIDs");

		TomCat::Scene destination;
		TomCat::PrefabInstantiationResult result;
		TomCat::PrefabInstantiateOptions options;
		options.ResolveAssets = false;
		Check(TomCat::PrefabArchiveCodec::Instantiate(prefab, destination, options,
			result, error), error);
		Check(result.Root.HasComponent<PluginEntityLinks>()
			&& result.Root.GetComponent<PluginEntityLinks>().Target
				== static_cast<uint64_t>(result.LocalToSceneUUID.at(2))
			&& result.Root.GetComponent<PluginEntityLinks>().RelatedTargets[0]
				== static_cast<uint64_t>(result.LocalToSceneUUID.at(2)),
			"plugin entity references were not remapped during Prefab instantiate");

		auto direct = TomCat::CreateRef<TomCat::Scene>();
		TomCat::Entity directOwner = direct->CreateEntity("Direct remap owner");
		Check(registry.Add(directOwner, TomCat::UUID(typeId), error), error);
		auto& directLinks = directOwner.GetComponent<PluginEntityLinks>();
		directLinks.Target = static_cast<uint64_t>(child.GetUUID());
		directLinks.RelatedTargets = {
			static_cast<uint64_t>(child.GetUUID()),
			static_cast<uint64_t>(outside.GetUUID()), 0 };
		const TomCat::UUID mappedTarget(0x715ad36e4b8c419eULL);
		const std::unordered_map<TomCat::UUID, TomCat::UUID> remap = {
			{ child.GetUUID(), mappedTarget }
		};
		std::unordered_set<uint64_t> attachmentIds;
		Check(TomCat::ComponentCodecs::RemapInstanceReferences(directOwner, remap,
			TomCat::ComponentCodecs::MissingEntityReferencePolicy::Clear,
			attachmentIds, false, error), error);
		Check(directLinks.Target == static_cast<uint64_t>(mappedTarget)
			&& directLinks.RelatedTargets[0] == static_cast<uint64_t>(mappedTarget)
			&& directLinks.RelatedTargets[1] == 0
			&& directLinks.RelatedTargets[2] == 0,
			"plugin property/custom remap hook did not share missing-reference policy");

		std::vector<TomCat::Entity> liveEntities = {
			root, templateRoot, result.Root, directOwner
		};
		Check(registry.UnregisterProvider(TomCat::UUID(providerId), liveEntities,
			error), error);
	}

	inline TomCat::ComponentDescriptor MakePluginCounterDescriptor()
	{
		constexpr uint64_t providerId = 0xe193ec6e26f74428ULL;
		constexpr uint64_t typeId = 0xd4d267f3831c4b49ULL;
		constexpr uint64_t propertyId = 0xa0811dbbaf7d4ca9ULL;
		constexpr uint64_t oldPropertyId = 0x90e70be22ef64848ULL;
		TomCat::ComponentDescriptor descriptor;
		descriptor.ProviderId = TomCat::UUID(providerId);
		descriptor.TypeId = TomCat::UUID(typeId);
		descriptor.StableName = "Regression.PluginCounter";
		descriptor.DisplayName = "Plugin Counter";
		descriptor.SchemaVersion = 2;
		descriptor.Has = [](TomCat::Entity entity)
		{
			return entity && entity.HasComponent<PluginCounter>();
		};
		descriptor.Add = [](TomCat::Entity entity, std::string& error)
		{
			if (!entity) { error = "invalid entity"; return false; }
			if (!entity.HasComponent<PluginCounter>())
				entity.AddComponent<PluginCounter>();
			return true;
		};
		descriptor.Remove = [](TomCat::Entity entity, std::string& error)
		{
			if (!entity) { error = "invalid entity"; return false; }
			entity.RemoveComponent<PluginCounter>();
			return true;
		};
		descriptor.Copy = [](TomCat::Entity source, TomCat::Entity destination,
			std::string& error)
		{
			if (!source || !destination || !source.HasComponent<PluginCounter>())
			{
				error = "invalid PluginCounter copy";
				return false;
			}
			destination.AddOrReplaceComponent<PluginCounter>(
				source.GetComponent<PluginCounter>());
			return true;
		};
		descriptor.Encode = [](const TomCat::ComponentDescriptor&, TomCat::Entity entity,
			YAML::Emitter& output, std::string&)
		{
			output << YAML::BeginSeq << YAML::BeginMap
				<< YAML::Key << "PropertyId" << YAML::Value << propertyId
				<< YAML::Key << "StableName" << YAML::Value << "Value"
				<< YAML::Key << "Value" << YAML::Value
				<< entity.GetComponent<PluginCounter>().Value
				<< YAML::EndMap << YAML::EndSeq;
			return output.good();
		};
		descriptor.Decode = [](const TomCat::ComponentDescriptor&, TomCat::Entity entity,
			const YAML::Node& properties, std::string& error)
		{
			if (!properties || !properties.IsSequence() || properties.size() != 1
				|| properties[0]["PropertyId"].as<uint64_t>() != propertyId
				|| properties[0]["StableName"].as<std::string>() != "Value")
			{
				error = "PluginCounter properties are invalid";
				return false;
			}
			entity.GetComponent<PluginCounter>().Value =
				properties[0]["Value"].as<int32_t>();
			return true;
		};
		TomCat::PropertyDescriptor property;
		property.PropertyId = TomCat::UUID(propertyId);
		property.StableName = "Value";
		property.DisplayName = "Value";
		property.Kind = TomCat::PropertyKind::Int32;
		property.Get = [](TomCat::Entity entity) -> TomCat::PropertyValue
		{
			return entity.GetComponent<PluginCounter>().Value;
		};
		property.Set = [](TomCat::Entity entity, const TomCat::PropertyValue& value,
			std::string&)
		{
			entity.GetComponent<PluginCounter>().Value = std::get<int32_t>(value);
			return true;
		};
		descriptor.Properties.push_back(std::move(property));
		descriptor.Migrations.push_back({ 1, 2,
			[](YAML::Node& record, std::string& error)
			{
				YAML::Node properties = record["Properties"];
				if (!properties || !properties.IsSequence() || properties.size() != 1
					|| properties[0]["PropertyId"].as<uint64_t>() != oldPropertyId)
				{
					error = "PluginCounter v1 payload is invalid";
					return false;
				}
				properties[0]["PropertyId"] = propertyId;
				properties[0]["StableName"] = "Value";
				return true;
			} });
		return descriptor;
	}

	inline void TestProviderLifecycle(TomCat::ComponentRegistry& registry)
	{
		constexpr uint64_t providerId = 0xe193ec6e26f74428ULL;
		constexpr uint64_t typeId = 0xd4d267f3831c4b49ULL;
		constexpr uint64_t propertyId = 0xa0811dbbaf7d4ca9ULL;
		constexpr uint64_t oldPropertyId = 0x90e70be22ef64848ULL;
		std::string error;
		Check(registry.Register(MakePluginCounterDescriptor(), error), error);
		auto scene = TomCat::CreateRef<TomCat::Scene>();
		TomCat::Entity entity = scene->CreateEntity("Provider lifecycle");
		Check(registry.Add(entity, TomCat::UUID(typeId), error), error);
		entity.GetComponent<PluginCounter>().Value = 73;
		const std::vector<TomCat::Entity> liveEntities = { entity };
		Check(registry.UnregisterProvider(TomCat::UUID(providerId), liveEntities,
			error), error);
		Check(!registry.Find(TomCat::UUID(typeId))
			&& !entity.HasComponent<PluginCounter>()
			&& entity.HasComponent<TomCat::OpaqueComponents>()
			&& entity.GetComponent<TomCat::OpaqueComponents>().Records.size() == 1,
			"provider unload did not detach its live component into an opaque record");

		Check(registry.Register(MakePluginCounterDescriptor(), error), error);
		Check(registry.RehydrateOpaqueComponents(liveEntities,
			TomCat::UUID(providerId), error), error);
		Check(entity.HasComponent<PluginCounter>()
			&& entity.GetComponent<PluginCounter>().Value == 73
			&& !entity.HasComponent<TomCat::OpaqueComponents>(),
			"provider reload did not restore its exact live value");

		Check(registry.UnregisterProvider(TomCat::UUID(providerId), liveEntities,
			error), error);
		auto& oldRecord = entity.GetComponent<TomCat::OpaqueComponents>().Records[0];
		YAML::Node oldNode = YAML::Load(oldRecord.SerializedRecord);
		oldNode["SchemaVersion"] = 1;
		oldNode["Properties"][0]["PropertyId"] = oldPropertyId;
		oldNode["Properties"][0]["StableName"] = "LegacyValue";
		oldNode["Properties"][0]["Value"] = 41;
		oldRecord.SchemaVersion = 1;
		oldRecord.SerializedRecord = YAML::Dump(oldNode);
		Check(registry.Register(MakePluginCounterDescriptor(), error), error);
		Check(registry.RehydrateOpaqueComponents(liveEntities,
			TomCat::UUID(providerId), error), error);
		Check(entity.HasComponent<PluginCounter>()
			&& entity.GetComponent<PluginCounter>().Value == 41,
			"v1 opaque component did not migrate to the v2 schema");

		Check(registry.UnregisterProvider(TomCat::UUID(providerId), liveEntities,
			error), error);
		auto& futureRecord = entity.GetComponent<TomCat::OpaqueComponents>().Records[0];
		YAML::Node futureNode = YAML::Load(futureRecord.SerializedRecord);
		futureNode["SchemaVersion"] = 99;
		futureRecord.SchemaVersion = 99;
		futureRecord.SerializedRecord = YAML::Dump(futureNode);
		Check(registry.Register(MakePluginCounterDescriptor(), error), error);
		Check(registry.RehydrateOpaqueComponents(liveEntities,
			TomCat::UUID(providerId), error), error);
		Check(!entity.HasComponent<PluginCounter>()
			&& entity.HasComponent<TomCat::OpaqueComponents>(),
			"future component schema was not preserved as opaque");
		Check(registry.UnregisterProvider(TomCat::UUID(providerId), {}, error), error);
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
		for (const uint64_t builtInTypeId : {
			TomCat::ComponentIds::ID, TomCat::ComponentIds::Tag,
			TomCat::ComponentIds::EntityMetadata, TomCat::ComponentIds::Transform,
			TomCat::ComponentIds::Camera, TomCat::ComponentIds::SpriteRenderer,
			TomCat::ComponentIds::SpriteAnimator, TomCat::ComponentIds::LineRenderer,
			TomCat::ComponentIds::CSharpScripts, TomCat::ComponentIds::AudioSource,
			TomCat::ComponentIds::AudioListener, TomCat::ComponentIds::Rigidbody2D,
			TomCat::ComponentIds::BoxCollider2D,
			TomCat::ComponentIds::CircleCollider2D,
			TomCat::ComponentIds::DistanceJoint2D })
		{
			const TomCat::ComponentDescriptor* builtIn = registry.Find(
				TomCat::UUID(builtInTypeId));
			Check(builtIn != nullptr,
				"a traditional built-in component is absent from ComponentRegistry");
			Check(builtIn->Copy && builtIn->Encode && builtIn->Decode,
				"a traditional built-in component lacks canonical registry callbacks");
			if (builtInTypeId != TomCat::ComponentIds::ID)
				Check(builtIn->EncodeLegacyFields && builtIn->DecodeLegacyFields,
					"a traditional built-in component lacks its Scene 9-11 compatibility adapter");
		}

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
		Check(components && components.IsSequence() && components.size() >= 5
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
		const YAML::Node preservedComponents =
			YAML::Load(resavedMissing)["Entities"][0]["Components"];
		YAML::Node preserved;
		for (const YAML::Node& candidate : preservedComponents)
		{
			if (candidate["TypeId"].as<uint64_t>() == missingTypeId)
			{
				preserved = candidate;
				break;
			}
		}
		Check(preserved["TypeId"].as<uint64_t>() == missingTypeId
			&& preserved["SchemaVersion"].as<uint32_t>() == 7
			&& preserved["Properties"][0]["PluginPayload"]["Nested"].as<std::string>()
				== "keep-me",
			"Missing Component payload was lost while resaving");
		auto copiedMissing = TomCat::Scene::Copy(missingScene);
		Check(copiedMissing && copiedMissing->FindEntityByUUID(rootId)
			.HasComponent<TomCat::OpaqueComponents>(),
			"Scene::Copy omitted Missing Component payload");

		TestPluginEntityReferenceRemap(registry);
		TestProviderLifecycle(registry);
	}

}
