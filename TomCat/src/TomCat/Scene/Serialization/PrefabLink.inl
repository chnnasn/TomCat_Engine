// Included by PrefabArchiveCodec.cpp; uses the same canonical archive helpers.
namespace TomCat {
namespace {
	constexpr uint64_t PrefabLinkType = 0x9f02000000000001ULL;

	YAML::Node LinkSceneDocument(const Ref<Scene>& scene)
	{
		std::string document, error;
		if (!SceneArchiveCodec::Encode(scene, document, error))
			throw std::runtime_error(error);
		return YAML::Load(document);
	}

	void RemoveLinkRecord(YAML::Node entity)
	{
		YAML::Node records(YAML::NodeType::Sequence);
		for (const auto& record : entity["Components"])
			if (record["TypeId"].as<uint64_t>() != PrefabLinkType)
				records.push_back(YAML::Clone(record));
		entity["Components"] = records;
	}

	using LinkNodes = std::map<std::string, YAML::Node>;
	LinkNodes IndexLinkNodes(const YAML::Node& nodes, const char* key)
	{
		LinkNodes result;
		for (const auto& node : nodes)
		{
			if (!node.IsMap() || !node[key] || !node[key].IsScalar()
				|| !result.emplace(node[key].Scalar(), YAML::Clone(node)).second)
				throw std::runtime_error(std::string("Invalid or duplicate Prefab identity: ") + key);
		}
		return result;
	}

	bool LinkEqual(const YAML::Node& a, const YAML::Node& b)
	{
		if (!a.IsDefined() || !b.IsDefined()) return a.IsDefined() == b.IsDefined();
		if (a.Type() != b.Type()) return false;
		if (a.IsScalar()) return a.Scalar() == b.Scalar();
		if (a.IsNull()) return true;
		if (a.size() != b.size()) return false;
		if (a.IsSequence())
		{
			for (size_t i = 0; i < a.size(); ++i) if (!LinkEqual(a[i], b[i])) return false;
		}
		else
			for (const auto& pair : a)
				if (!LinkEqual(pair.second, b[pair.first.as<std::string>()])) return false;
		return true;
	}

	// Stable-ID sequences merge per element; vectors and unkeyed arrays are atomic.
	YAML::Node MergeLink(const YAML::Node& base, const YAML::Node& local,
		const YAML::Node& incoming, const std::string& path)
	{
		if (LinkEqual(base, local)) return YAML::Clone(incoming);
		if (LinkEqual(base, incoming) || LinkEqual(local, incoming)) return YAML::Clone(local);
		if (!incoming.IsDefined() && local.IsDefined())
			throw std::runtime_error("Prefab update conflicts with a removed template field: " + path);
		if (base.IsMap() && local.IsMap() && incoming.IsMap())
		{
			YAML::Node result(YAML::NodeType::Map);
			std::set<std::string> keys;
			for (const auto& n : { base, local, incoming })
				for (const auto& p : n) keys.insert(p.first.as<std::string>());
			for (const auto& key : keys)
			{
				const auto value = MergeLink(base[key], local[key], incoming[key], path + "/" + key);
				if (value.IsDefined()) result[key] = value;
			}
			return result;
		}
		if (base.IsSequence() && local.IsSequence() && incoming.IsSequence())
		{
			for (const char* key : { "TypeId", "PropertyId", "AttachmentID", "FieldID" })
			{
				bool keyed = false, valid = true;
				for (const auto& nodes : { base, local, incoming })
					for (const auto& n : nodes)
					{
						keyed = true;
						if (!n.IsMap() || !n[key]) valid = false;
					}
				if (!keyed || !valid) continue;
				const auto b = IndexLinkNodes(base, key), l = IndexLinkNodes(local, key), r = IndexLinkNodes(incoming, key);
				std::vector<std::string> order;
				std::set<std::string> seen;
				for (const auto& nodes : { local, incoming, base })
					for (const auto& n : nodes)
						if (seen.insert(n[key].Scalar()).second) order.push_back(n[key].Scalar());
				auto get = [](const LinkNodes& map, const std::string& id) {
					const auto it = map.find(id);
					return it == map.end() ? YAML::Node(YAML::NodeType::Undefined) : it->second;
				};
				YAML::Node result(YAML::NodeType::Sequence);
				for (const auto& id : order)
				{
					const auto value = MergeLink(get(b, id), get(l, id), get(r, id), path + "/" + id);
					if (value.IsDefined()) result.push_back(value);
				}
				return result;
			}
		}
		return YAML::Clone(local);
	}

	YAML::Node BuildLinkState(const Ref<Scene>& scene, const PrefabArchive& archive,
		const PrefabInstantiationResult& instance)
	{
		YAML::Node state(YAML::NodeType::Map);
		state["Version"] = 1;
		std::string templateDocument, encodeError;
		if (!PrefabArchiveCodec::Encode(archive, templateDocument, encodeError))
			throw std::runtime_error(encodeError);
		state["Template"] = templateDocument;
		state["Root"] = static_cast<uint64_t>(instance.Root.GetUUID());
		state["Mapping"] = YAML::Node(YAML::NodeType::Map);
		state["Attachments"] = YAML::Node(YAML::NodeType::Map);
		std::set<uint64_t> ids;
		for (const auto& [local, id] : instance.LocalToSceneUUID)
		{
			state["Mapping"][std::to_string(local)] = static_cast<uint64_t>(id);
			ids.insert(static_cast<uint64_t>(id));
			Entity a = archive.TemplateScene->FindEntityByUUID(UUID(local));
			Entity b = scene->FindEntityByUUID(id);
			if (a.HasComponent<CSharpScripts>() && b.HasComponent<CSharpScripts>())
			{
				const auto& from = a.GetComponent<CSharpScripts>().Scripts;
				const auto& to = b.GetComponent<CSharpScripts>().Scripts;
				if (from.size() != to.size()) throw std::runtime_error("Prefab attachment layout mismatch");
				for (size_t i = 0; i < from.size(); ++i)
					state["Attachments"][std::to_string(static_cast<uint64_t>(from[i].AttachmentID))]
						= static_cast<uint64_t>(to[i].AttachmentID);
			}
		}
		state["Baseline"] = YAML::Node(YAML::NodeType::Sequence);
		for (auto entity : LinkSceneDocument(scene)["Entities"])
			if (ids.contains(entity["Entity"].as<uint64_t>()))
			{
				if (entity["Entity"].as<uint64_t>() == static_cast<uint64_t>(instance.Root.GetUUID()))
					RemoveLinkRecord(entity);
				state["Baseline"].push_back(entity);
			}
		return state;
	}
}

ComponentDescriptor MakePrefabLinkDescriptor()
{
	ComponentDescriptor d;
	d.TypeId = UUID(PrefabLinkType);
	d.StableName = "TomCat.PrefabLink";
	d.DisplayName = "Prefab Instance";
	d.InspectorVisible = false;
	d.AddableInInspector = false;
	d.Has = [](Entity e) { return e && e.HasComponent<PrefabLink>(); };
	d.Add = [](Entity e, std::string&) { e.AddComponent<PrefabLink>(); return true; };
	d.Remove = [](Entity e, std::string&) { e.RemoveComponent<PrefabLink>(); return true; };
	d.Copy = [](Entity a, Entity b, std::string&) {
		b.AddOrReplaceComponent<PrefabLink>(a.GetComponent<PrefabLink>()); return true;
	};
	PropertyDescriptor source;
	source.PropertyId = UUID(PrefabLinkType + 1);
	source.StableName = "Source"; source.DisplayName = "Source"; source.Kind = PropertyKind::UInt64;
	source.AssetReference = AssetPropertyMetadata{ { AssetType::Prefab }, false };
	source.Get = [](Entity e) -> PropertyValue { return static_cast<uint64_t>(e.GetComponent<PrefabLink>().Source); };
	source.Set = [](Entity e, const PropertyValue& v, std::string& error) {
		if (std::get<uint64_t>(v) == 0) { error = "Prefab source cannot be zero"; return false; }
		e.GetComponent<PrefabLink>().Source = AssetHandle(std::get<uint64_t>(v)); return true;
	};
	PropertyDescriptor state;
	state.PropertyId = UUID(PrefabLinkType + 2);
	state.StableName = "State"; state.DisplayName = "State"; state.Kind = PropertyKind::String;
	state.Get = [](Entity e) -> PropertyValue { return e.GetComponent<PrefabLink>().State; };
	state.Set = [](Entity e, const PropertyValue& v, std::string& error) {
		try {
			const auto& text = std::get<std::string>(v);
			const auto n = YAML::Load(text);
			if (!n.IsMap() || n["Version"].as<int>() != 1 || n["Root"].as<uint64_t>() == 0
				|| !n["Mapping"].IsMap() || !n["Attachments"].IsMap() || !n["Baseline"].IsSequence())
				throw std::runtime_error("Malformed Prefab instance state");
			IndexLinkNodes(n["Baseline"], "Entity");
			e.GetComponent<PrefabLink>().State = text; return true;
		} catch (const std::exception& ex) { error = ex.what(); return false; }
	};
	d.Properties = { std::move(source), std::move(state) };
	// ComponentCodecs remaps the structured baseline after attachment identities
	// are available, using the same canonical component reference mapper.
	return d;
}

bool PrefabLinkedInstance::Attach(const Ref<Scene>& scene, AssetHandle source,
	const PrefabArchive& archive, const PrefabInstantiationResult& instance, std::string& error)
{
	try {
		if (!scene || scene->IsRuntimeRunning() || !instance.Root
			|| instance.Root.GetScene() != scene.get() || static_cast<uint64_t>(source) == 0)
			throw std::runtime_error("Prefab linking requires an authoring Scene and a source asset");
		const auto state = BuildLinkState(scene, archive, instance);
		Entity root = instance.Root;
		root.AddOrReplaceComponent<PrefabLink>(PrefabLink{ source, YAML::Dump(state) });
		error.clear(); return true;
	} catch (const std::exception& ex) { error = ex.what(); return false; }
}

bool PrefabLinkedInstance::Update(const Ref<Scene>& scene, UUID rootID,
	const PrefabArchive& latest, bool revert, std::string& error, bool resolveAssets)
{
	try {
		Entity root = scene ? scene->FindEntityByUUID(rootID) : Entity{};
		if (!root || !root.HasComponent<PrefabLink>() || scene->IsRuntimeRunning())
			throw std::runtime_error("Prefab update requires a linked root in Edit mode");
		const auto link = root.GetComponent<PrefabLink>();
		const YAML::Node state = YAML::Load(link.State);
		PrefabInstantiateOptions options;
		options.ResolveAssets = false;
		for (const auto& p : state["Mapping"])
			options.EntityIdentities.emplace(p.first.as<uint64_t>(), UUID(p.second.as<uint64_t>()));
		for (const auto& record : latest.Entities)
			if (!options.EntityIdentities.contains(record.LocalID))
			{
				// Stable composition identity: resolving a nested asset repeatedly must
				// not invent a different outer LocalID for the same newly added node.
				uint64_t id = 1469598103934665603ULL;
				for (uint64_t value : { static_cast<uint64_t>(rootID), record.LocalID })
					for (int byte = 0; byte < 8; ++byte) { id ^= (value >> (byte * 8)) & 255; id *= 1099511628211ULL; }
				if (id == 0 || scene->FindEntityByUUID(UUID(id)))
					throw std::runtime_error("Prefab composition identity collision");
				options.EntityIdentities.emplace(record.LocalID, UUID(id));
			}
		if (!options.EntityIdentities.contains(latest.RootLocalID)
			|| options.EntityIdentities.at(latest.RootLocalID) != rootID)
			throw std::runtime_error("Prefab root identity changed; relink the instance explicitly");
		for (const auto& p : state["Attachments"])
			options.AttachmentIdentities.emplace(UUID(p.first.as<uint64_t>()), UUID(p.second.as<uint64_t>()));
		// Composition resolves from the saved baseline on every asset load. Fresh
		// attachments therefore need stable identities too: random UUIDs would
		// change the resolved outer template each time and invalidate overrides.
		std::unordered_set<uint64_t> reservedAttachments;
		for (const auto& [sourceID, instanceID] : options.AttachmentIdentities)
			reservedAttachments.insert(static_cast<uint64_t>(instanceID));
		std::vector<UUID> attachmentOwners = scene->GetRootEntityUUIDs();
		for (size_t index = 0; index < attachmentOwners.size(); ++index)
		{
			Entity owner = scene->FindEntityByUUID(attachmentOwners[index]);
			if (owner.HasComponent<CSharpScripts>())
				for (const auto& script : owner.GetComponent<CSharpScripts>().Scripts)
					reservedAttachments.insert(static_cast<uint64_t>(script.AttachmentID));
			const auto children = scene->GetChildrenUUIDs(owner);
			attachmentOwners.insert(attachmentOwners.end(), children.begin(), children.end());
		}
		for (const auto& record : latest.Entities)
		{
			Entity sourceEntity = latest.TemplateScene->FindEntityByUUID(UUID(record.LocalID));
			if (!sourceEntity.HasComponent<CSharpScripts>()) continue;
			for (const auto& script : sourceEntity.GetComponent<CSharpScripts>().Scripts)
			{
				if (options.AttachmentIdentities.contains(script.AttachmentID)) continue;
				uint64_t id = 1469598103934665603ULL;
				for (uint64_t value : { uint64_t{ 0x4174746163686D74ULL },
					static_cast<uint64_t>(rootID), record.LocalID, static_cast<uint64_t>(script.AttachmentID) })
					for (int byte = 0; byte < 8; ++byte) { id ^= (value >> (byte * 8)) & 255; id *= 1099511628211ULL; }
				if (id == 0 || !reservedAttachments.insert(id).second)
					throw std::runtime_error("Prefab composition attachment identity collision");
				options.AttachmentIdentities.emplace(script.AttachmentID, UUID(id));
			}
		}
		auto staged = CreateRef<Scene>();
		PrefabInstantiationResult generated;
		if (!PrefabArchiveCodec::Instantiate(latest, *staged, options, generated, error)) return false;
		YAML::Node nextState = BuildLinkState(staged, latest, generated);
		// Retain tombstones so Apply never reuses a removed template LocalID.
		for (const auto& p : state["Mapping"])
			if (!nextState["Mapping"][p.first.as<std::string>()])
				nextState["Mapping"][p.first.as<std::string>()] = p.second.as<uint64_t>();
		for (const auto& p : state["Attachments"])
			if (!nextState["Attachments"][p.first.as<std::string>()])
				nextState["Attachments"][p.first.as<std::string>()] = p.second.as<uint64_t>();
		for (const auto& p : state["Attachments"])
			if (!nextState["Attachments"][p.first.as<std::string>()])
				nextState["Attachments"][p.first.as<std::string>()] = p.second.as<uint64_t>();
		const auto oldNodes = IndexLinkNodes(state["Baseline"], "Entity");
		auto newNodes = IndexLinkNodes(nextState["Baseline"], "Entity");
		YAML::Node document = LinkSceneDocument(scene);
		auto localNodes = IndexLinkNodes(document["Entities"], "Entity");
		const std::string rootKey = std::to_string(static_cast<uint64_t>(rootID));
		RemoveLinkRecord(localNodes.at(rootKey));
		// Placement belongs to the level. Preserve it even for Revert All.
		auto preservePlacement = [](YAML::Node target, const YAML::Node& placement) {
			for (const char* field : { "Parent", "Transform", "LocalTransform" })
				if (placement[field]) target[field] = YAML::Clone(placement[field]);
			for (auto record : target["Components"])
				if (record["TypeId"].as<uint64_t>() == ComponentIds::Transform)
					for (const auto& original : placement["Components"])
						if (original["TypeId"].as<uint64_t>() == ComponentIds::Transform)
							record["Properties"] = YAML::Clone(original["Properties"]);
		};
		preservePlacement(newNodes.at(rootKey), oldNodes.at(rootKey));
		std::set<std::string> ids;
		for (const auto& [id, node] : oldNodes) ids.insert(id);
		for (const auto& [id, node] : newNodes) ids.insert(id);
		if (revert)
		{
			// Locally added descendants are structural overrides too. Collect before
			// merging: mapped descendants may subsequently move back to their source parent.
			std::function<void(Entity)> collect = [&](Entity entity) {
				const auto id = std::to_string(static_cast<uint64_t>(entity.GetUUID()));
				if (!oldNodes.contains(id) && !newNodes.contains(id)) localNodes.erase(id);
				for (UUID child : scene->GetChildrenUUIDs(entity)) collect(scene->FindEntityByUUID(child));
			};
			collect(root);
		}
		const YAML::Node absent(YAML::NodeType::Undefined);
		for (const auto& id : ids)
		{
			const auto base = oldNodes.contains(id) ? oldNodes.at(id) : absent;
			const auto local = localNodes.contains(id) ? localNodes.at(id) : absent;
			const auto incoming = newNodes.contains(id) ? newNodes.at(id) : absent;
			if (!base.IsDefined() && local.IsDefined())
				throw std::runtime_error("Prefab generated identity collides with a Scene entity");
			const auto merged = revert ? YAML::Clone(incoming) : MergeLink(base, local, incoming, id);
			if (merged.IsDefined()) localNodes[id] = merged;
			else localNodes.erase(id);
		}
		if (!localNodes.contains(rootKey)) throw std::runtime_error("Prefab update removed its root");
		const auto originalNodes = IndexLinkNodes(document["Entities"], "Entity");
		preservePlacement(localNodes.at(rootKey), originalNodes.at(rootKey));
		nextState["Baseline"] = YAML::Node(YAML::NodeType::Sequence);
		for (const auto& [id, node] : newNodes) nextState["Baseline"].push_back(node);
		YAML::Node linkRecord;
		for (const auto& record : originalNodes.at(rootKey)["Components"])
			if (record["TypeId"].as<uint64_t>() == PrefabLinkType) linkRecord = YAML::Clone(record);
		for (auto property : linkRecord["Properties"])
			if (property["StableName"].as<std::string>() == "State") property["Value"] = YAML::Dump(nextState);
		localNodes.at(rootKey)["Components"].push_back(linkRecord);
		YAML::Node entities(YAML::NodeType::Sequence);
		// Retain existing entity/sibling order and append newly introduced entities.
		std::set<std::string> emitted;
		for (const auto& old : document["Entities"])
		{
			const auto id = old["Entity"].Scalar();
			if (localNodes.contains(id)) { entities.push_back(localNodes.at(id)); emitted.insert(id); }
		}
		for (const auto& [id, node] : localNodes) if (!emitted.contains(id)) entities.push_back(node);
		document["Entities"] = entities;
		const auto text = YAML::Dump(document);
		if (!SceneArchiveCodec::Decode(std::vector<uint8_t>(text.begin(), text.end()), scene, "Prefab update", resolveAssets))
			throw std::runtime_error("Prefab merged result failed Scene validation; original Scene retained");
		error.clear(); return true;
	} catch (const std::exception& ex) { error = ex.what(); return false; }
}

bool PrefabLinkedInstance::Capture(const Ref<Scene>& scene, UUID rootID,
	PrefabArchive& archive, std::string& error)
{
	try {
		Entity root = scene ? scene->FindEntityByUUID(rootID) : Entity{};
		if (!root || !root.HasComponent<PrefabLink>() || scene->IsRuntimeRunning())
			throw std::runtime_error("Apply requires a linked root in Edit mode");
		const YAML::Node state = YAML::Load(root.GetComponent<PrefabLink>().State);
		std::unordered_map<UUID, UUID> identities;
		for (const auto& p : state["Mapping"])
			identities.emplace(UUID(p.second.as<uint64_t>()), UUID(p.first.as<uint64_t>()));
		if (!PrefabArchiveCodec::CaptureSubtree(scene, root, archive, error, &identities)) return false;
		std::unordered_map<UUID, UUID> attachments, entityIdentity;
		for (const auto& p : state["Attachments"])
			attachments.emplace(UUID(p.second.as<uint64_t>()), UUID(p.first.as<uint64_t>()));
		for (const auto& record : archive.Entities)
		{
			entityIdentity.emplace(UUID(record.LocalID), UUID(record.LocalID));
			Entity e = archive.TemplateScene->FindEntityByUUID(UUID(record.LocalID));
			if (e.HasComponent<CSharpScripts>())
				for (const auto& script : e.GetComponent<CSharpScripts>().Scripts)
					if (!attachments.contains(script.AttachmentID)) attachments.emplace(script.AttachmentID, script.AttachmentID);
		}
		std::unordered_set<uint64_t> used;
		for (const auto& record : archive.Entities)
			if (!ComponentCodecs::RemapInstanceReferences(archive.TemplateScene->FindEntityByUUID(UUID(record.LocalID)),
				entityIdentity, MissingEntityReferencePolicy::Reject, used, true, error, &attachments)) return false;
		return true;
	} catch (const std::exception& ex) { error = ex.what(); return false; }
}

bool PrefabLinkedInstance::GetOverridePaths(const Ref<Scene>& scene, UUID rootID,
	std::vector<std::string>& paths, std::string& error)
{
	paths.clear();
	try {
		Entity root = scene ? scene->FindEntityByUUID(rootID) : Entity{};
		if (!root || !root.HasComponent<PrefabLink>()) throw std::runtime_error("Select a linked Prefab root");
		const YAML::Node state = YAML::Load(root.GetComponent<PrefabLink>().State);
		auto baseline = IndexLinkNodes(state["Baseline"], "Entity");
		auto current = IndexLinkNodes(LinkSceneDocument(scene)["Entities"], "Entity");
		std::function<void(const YAML::Node&, const YAML::Node&, const std::string&)> diff;
		diff = [&](const YAML::Node& a, const YAML::Node& b, const std::string& path) {
			if (LinkEqual(a, b)) return;
			if (a.IsMap() && b.IsMap())
			{
				std::set<std::string> keys;
				for (const auto& n : { a, b }) for (const auto& p : n) keys.insert(p.first.as<std::string>());
				for (const auto& key : keys) diff(a[key], b[key], path + "/" + key);
				return;
			}
			if (a.IsSequence() && b.IsSequence())
				for (const char* key : { "TypeId", "PropertyId", "AttachmentID", "FieldID" })
				{
					bool valid = a.size() + b.size() > 0;
					for (const auto& nodes : { a, b })
						for (const auto& n : nodes) if (!n.IsMap() || !n[key]) valid = false;
					if (!valid) continue;
					auto left = IndexLinkNodes(a, key), right = IndexLinkNodes(b, key);
					std::set<std::string> keys;
					for (const auto& map : { left, right }) for (const auto& [id, n] : map) keys.insert(id);
					for (const auto& id : keys)
					{
						const YAML::Node absent(YAML::NodeType::Undefined);
						const auto l = left.contains(id) ? left.at(id) : absent;
						const auto r = right.contains(id) ? right.at(id) : absent;
						const auto named = r.IsDefined() ? r : l;
						const std::string label = named["StableName"] ? named["StableName"].as<std::string>() : id;
						diff(l, r, path + "/" + label);
					}
					return;
				}
			paths.push_back(path + (!a.IsDefined() ? " (added)" : !b.IsDefined() ? " (removed)" : ""));
		};
		for (auto& [id, base] : baseline)
		{
			if (!current.contains(id)) { paths.push_back(id + " (removed entity)"); continue; }
			auto local = current.at(id);
			const std::string label = scene->FindEntityByUUID(UUID(std::stoull(id))).GetName();
			RemoveLinkRecord(base); RemoveLinkRecord(local);
			if (std::stoull(id) == static_cast<uint64_t>(rootID))
				for (auto n : { base, local })
				{
					YAML::Node components(YAML::NodeType::Sequence);
					for (const auto& c : n["Components"])
						if (c["TypeId"].as<uint64_t>() != ComponentIds::Transform) components.push_back(c);
					n["Components"] = components;
				}
			else diff(base["Parent"], local["Parent"], label + "/Parent");
			diff(base["Components"], local["Components"], label);
		}
		std::function<void(Entity)> added = [&](Entity e) {
			if (!baseline.contains(std::to_string(static_cast<uint64_t>(e.GetUUID()))))
				paths.push_back(e.GetName() + " (added entity)");
			for (UUID child : scene->GetChildrenUUIDs(e)) added(scene->FindEntityByUUID(child));
		};
		added(root);
		error.clear(); return true;
	} catch (const std::exception& ex) { error = ex.what(); return false; }
}

namespace {
	std::vector<UUID> LinkedRoots(const Ref<Scene>& scene)
	{
		std::vector<UUID> roots, pending = scene->GetRootEntityUUIDs();
		for (size_t index = 0; index < pending.size(); ++index)
		{
			Entity e = scene->FindEntityByUUID(pending[index]);
			if (e.HasComponent<PrefabLink>()) roots.push_back(e.GetUUID());
			const auto children = scene->GetChildrenUUIDs(e);
			pending.insert(pending.end(), children.begin(), children.end());
		}
		return roots;
	}

	bool CommitLinkScene(const Ref<Scene>& from, const Ref<Scene>& to, std::string& error, bool resolveAssets = true)
	{
		std::string document;
		if (!SceneArchiveCodec::Encode(from, document, error)) return false;
		if (!SceneArchiveCodec::Decode(std::vector<uint8_t>(document.begin(), document.end()), to,
			"Prefab transaction", resolveAssets))
		{ error = "Prefab transaction failed Scene validation"; return false; }
		return true;
	}
}

bool PrefabLinkedInstance::RefreshAll(const Ref<Scene>& scene, bool& changed, std::string& error, bool resolveAssets)
{
	changed = false;
	try {
		if (!scene || scene->IsRuntimeRunning()) throw std::runtime_error("Prefab refresh requires Edit mode");
		Ref<Scene> candidate;
		std::map<uint64_t, PrefabArchive> templates;
		std::map<uint64_t, std::string> documents;
		for (UUID id : LinkedRoots(scene))
		{
			if (candidate && !candidate->FindEntityByUUID(id)) continue;
			const auto link = scene->FindEntityByUUID(id).GetComponent<PrefabLink>();
			const auto source = static_cast<uint64_t>(link.Source);
			if (!templates.contains(source))
			{
				PrefabArchive archive;
				if (!PrefabArchiveCodec::Load(link.Source, archive, error)
					|| !PrefabArchiveCodec::Encode(archive, documents[source], error)) return false;
				templates.emplace(source, std::move(archive));
			}
			const YAML::Node state = YAML::Load(link.State);
			if (state["Template"] && state["Template"].as<std::string>() == documents.at(source)) continue;
			if (!candidate) candidate = Scene::Copy(scene);
			if (!candidate || !Update(candidate, id, templates.at(source), false, error, resolveAssets)) return false;
		}
		if (candidate)
		{
			if (!CommitLinkScene(candidate, scene, error, resolveAssets)) return false;
			changed = true;
		}
		error.clear(); return true;
	} catch (const std::exception& ex) { error = ex.what(); return false; }
}

bool PrefabLinkedInstance::Remap(Entity entity, const std::unordered_map<UUID, UUID>& entities,
	const std::unordered_map<UUID, UUID>* attachments, bool regenerate, std::string& error)
{
	try {
		auto& link = entity.GetComponent<PrefabLink>();
		YAML::Node state = YAML::Load(link.State);
		const auto oldRoot = state["Root"].as<uint64_t>();
		if (oldRoot == static_cast<uint64_t>(entity.GetUUID()) && !regenerate) return true;
		auto map = entities;
		for (auto p : state["Mapping"])
		{
			const UUID oldID(p.second.as<uint64_t>());
			if (!map.contains(oldID)) map.emplace(oldID, UUID()); // removed-node tombstone
			p.second = static_cast<uint64_t>(map.at(oldID));
		}
		if (!map.contains(UUID(oldRoot))) throw std::runtime_error("Copied Prefab root is missing from its identity map");
		state["Root"] = static_cast<uint64_t>(map.at(UUID(oldRoot)));
		auto attachmentMap = attachments ? *attachments : std::unordered_map<UUID, UUID>{};
		for (auto p : state["Attachments"])
		{
			const UUID oldID(p.second.as<uint64_t>());
			if (!attachmentMap.contains(oldID)) attachmentMap.emplace(oldID, regenerate ? UUID() : oldID);
			p.second = static_cast<uint64_t>(attachmentMap.at(oldID));
		}
		YAML::Node baseline = YAML::Clone(state["Baseline"]);
		YAML::Node rootPlacement;
		for (auto n : baseline)
			if (n["Entity"].as<uint64_t>() == oldRoot)
			{
				rootPlacement = YAML::Clone(n);
				n["Parent"] = uint64_t(0);
			}
		YAML::Node document;
		document["SchemaVersion"] = SceneSerializer::CurrentSchemaVersion;
		document["SceneName"] = "Prefab baseline";
		document["Entities"] = baseline;
		const auto text = YAML::Dump(document);
		auto old = CreateRef<Scene>(), remapped = CreateRef<Scene>();
		if (!SceneArchiveCodec::Decode(std::vector<uint8_t>(text.begin(), text.end()), old, "Prefab baseline", false))
			throw std::runtime_error("Copied Prefab baseline failed validation");
		for (const auto& n : baseline)
		{
			Entity a = old->FindEntityByUUID(UUID(n["Entity"].as<uint64_t>()));
			Entity b = remapped->CreateEntityWithUUID(map.at(a.GetUUID()), a.GetName());
			if (!ComponentCodecs::CopyAuthoringComponents(a, b, false, error)) return false;
			if (a.HasComponent<CSharpScripts>())
				for (const auto& script : a.GetComponent<CSharpScripts>().Scripts)
					if (!attachmentMap.contains(script.AttachmentID))
						attachmentMap.emplace(script.AttachmentID, regenerate ? UUID() : script.AttachmentID);
		}
		std::unordered_set<uint64_t> used;
		for (const auto& n : baseline)
		{
			const UUID oldID(n["Entity"].as<uint64_t>());
			Entity b = remapped->FindEntityByUUID(map.at(oldID));
			if (!ComponentCodecs::RemapInstanceReferences(b, map, MissingEntityReferencePolicy::Preserve,
				used, true, error, &attachmentMap)) return false;
			const UUID parent(n["Parent"].as<uint64_t>());
			if (static_cast<uint64_t>(parent) != 0
				&& !remapped->SetParent(b, remapped->FindEntityByUUID(map.at(parent))))
				throw std::runtime_error("Copied Prefab baseline has an invalid hierarchy");
		}
		state["Baseline"] = LinkSceneDocument(remapped)["Entities"];
		for (auto n : state["Baseline"])
			if (n["Entity"].as<uint64_t>() == state["Root"].as<uint64_t>())
			{
				const UUID oldParent(rootPlacement["Parent"].as<uint64_t>());
				n["Parent"] = static_cast<uint64_t>(map.contains(oldParent) ? map.at(oldParent) : oldParent);
				for (const char* field : { "Transform", "LocalTransform" }) n[field] = YAML::Clone(rootPlacement[field]);
				for (auto record : n["Components"])
					if (record["TypeId"].as<uint64_t>() == ComponentIds::Transform)
						for (const auto& original : rootPlacement["Components"])
							if (original["TypeId"].as<uint64_t>() == ComponentIds::Transform)
								record["Properties"] = YAML::Clone(original["Properties"]);
			}
		link.State = YAML::Dump(state);
		return true;
	} catch (const std::exception& ex) { error = ex.what(); return false; }
}

bool PrefabLinkedInstance::ResolveComposition(PrefabArchive& archive, std::string& error)
{
	bool changed = false;
	if (!RefreshAll(archive.TemplateScene, changed, error, false)) return false;
	if (!changed) return true;
	archive.Entities.clear();
	std::function<void(Entity, uint64_t)> visit = [&](Entity e, uint64_t parent) {
		archive.Entities.push_back({ static_cast<uint64_t>(e.GetUUID()), parent });
		for (UUID child : archive.TemplateScene->GetChildrenUUIDs(e))
			visit(archive.TemplateScene->FindEntityByUUID(child), static_cast<uint64_t>(e.GetUUID()));
	};
	visit(archive.TemplateScene->FindEntityByUUID(UUID(archive.RootLocalID)), 0);
	return true;
}

namespace {
	bool ValidateApplyDependencies(const PrefabArchive& candidate, AssetHandle target, std::string& error)
	{
		std::set<uint64_t> visited;
		std::function<bool(const PrefabArchive&, size_t)> visit = [&](const PrefabArchive& archive, size_t depth) {
			if (depth >= 32) { error = "Prefab composition depth limit"; return false; }
			for (const auto& record : archive.Entities)
			{
				Entity entity = archive.TemplateScene->FindEntityByUUID(UUID(record.LocalID));
				if (!entity.HasComponent<PrefabLink>()) continue;
				const auto dependency = entity.GetComponent<PrefabLink>().Source;
				if (dependency == target) { error = "Apply would create a Prefab composition cycle"; return false; }
				if (!visited.insert(static_cast<uint64_t>(dependency)).second) continue;
				PrefabArchive nested;
				if (!PrefabArchiveCodec::Load(dependency, nested, error) || !visit(nested, depth + 1)) return false;
			}
			return true;
		};
		return visit(candidate, 0);
	}

	bool LoadLinkedArchive(const std::vector<uint8_t>& bytes, const std::filesystem::path& path,
		const std::string& key, PrefabArchive& archive, std::string& error)
	{
		static thread_local std::vector<std::string> chain;
		if (chain.size() >= 32 || std::find(chain.begin(), chain.end(), key) != chain.end())
		{
			error = "Prefab composition cycle or depth limit: ";
			for (const auto& item : chain) error += item + " -> ";
			error += key;
			return false;
		}
		chain.push_back(key);
		struct Pop { std::vector<std::string>& Chain; ~Pop() { Chain.pop_back(); } } pop{ chain };
		PrefabArchive candidate;
		if (!PrefabArchiveCodec::Decode(bytes, path, candidate, error)
			|| !PrefabLinkedInstance::ResolveComposition(candidate, error)) return false;
		archive = std::move(candidate);
		return true;
	}
}

bool PrefabLinkedInstance::Apply(const Ref<Scene>& scene, UUID rootID, std::string& error)
{
	try {
		Entity root = scene ? scene->FindEntityByUUID(rootID) : Entity{};
		if (!root || !root.HasComponent<PrefabLink>() || scene->IsRuntimeRunning())
			throw std::runtime_error("Apply requires a linked root in Edit mode");
		const auto source = root.GetComponent<PrefabLink>().Source;
		auto& assets = AssetManager::Get();
		const auto path = assets.GetRegistry().GetFileSystemPath(source);
		if (path.empty() || !assets.GetRegistry().IsManagedPath(path))
			throw std::runtime_error("Apply target must be an existing project Prefab");
		auto readTarget = [&]() {
			std::ifstream input(path, std::ios::binary);
			if (!input) throw std::runtime_error("Cannot read Apply target");
			return std::string(std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>());
		};
		const std::string previous = readTarget();
		PrefabArchive latest, applied;
		if (!LoadLinkedArchive(std::vector<uint8_t>(previous.begin(), previous.end()), path,
			PathToUTF8(path), latest, error)) return false;
		auto candidate = Scene::Copy(scene);
		if (!candidate || !Update(candidate, rootID, latest, false, error)
			|| !Capture(candidate, rootID, applied, error)) return false;
		Entity templateRoot = latest.TemplateScene->FindEntityByUUID(UUID(latest.RootLocalID));
		if (templateRoot.HasComponent<PrefabLink>())
			applied.TemplateScene->FindEntityByUUID(UUID(applied.RootLocalID))
				.AddOrReplaceComponent<PrefabLink>(templateRoot.GetComponent<PrefabLink>());
		applied.TemplateScene->FindEntityByUUID(UUID(applied.RootLocalID)).GetComponent<Transform>() =
			latest.TemplateScene->FindEntityByUUID(UUID(latest.RootLocalID)).GetComponent<Transform>();
		if (!applied.TemplateScene->SyncTransformHierarchy()) throw std::runtime_error("Invalid applied hierarchy");
		if (!ValidateApplyDependencies(applied, source, error)) return false;
		// Capture uses depth-first subtree order. Reattach with every current UUID,
		// including newly added children, before propagating to other instances.
		std::vector<UUID> order;
		std::function<void(Entity)> visit = [&](Entity e) {
			order.push_back(e.GetUUID());
			for (UUID child : candidate->GetChildrenUUIDs(e)) visit(candidate->FindEntityByUUID(child));
		};
		visit(candidate->FindEntityByUUID(rootID));
		PrefabInstantiationResult instance;
		instance.Root = candidate->FindEntityByUUID(rootID);
		if (order.size() != applied.Entities.size()) throw std::runtime_error("Apply entity map mismatch");
		for (size_t i = 0; i < order.size(); ++i)
			instance.LocalToSceneUUID.emplace(applied.Entities[i].LocalID, order[i]);
		if (!Attach(candidate, source, applied, instance, error)) return false;
		for (UUID id : LinkedRoots(candidate))
			if (id != rootID && candidate->FindEntityByUUID(id).GetComponent<PrefabLink>().Source == source)
				if (!Update(candidate, id, applied, false, error)) return false;
		std::string document, sceneDocument;
		if (!PrefabArchiveCodec::Encode(applied, document, error)
			|| !SceneArchiveCodec::Encode(candidate, sceneDocument, error)) return false;
		if (readTarget() != previous)
			throw std::runtime_error("Apply target changed during validation; retry after reviewing the template");
		if (!FileSystem::WriteFileAtomically(path, document, error)) return false;
		if (assets.ImportAsset(path) != source || !CommitLinkScene(candidate, scene, error))
		{
			std::string rollbackError;
			if (!FileSystem::WriteFileAtomically(path, previous, rollbackError)) error += "; rollback: " + rollbackError;
			else (void)assets.ImportAsset(path);
			if (error.empty()) error = "Could not register the applied template; original Scene retained";
			return false;
		}
		error.clear(); return true;
	} catch (const std::exception& ex) { error = ex.what(); return false; }
}

bool PrefabLinkedInstance::GetPropertyOverrides(const Ref<Scene>& scene, UUID rootID,
    std::vector<PrefabPropertyOverride>& values, std::string& error)
{
    values.clear();
    try {
        Entity root=scene?scene->FindEntityByUUID(rootID):Entity{};
        if(!root || !root.HasComponent<PrefabLink>()) throw std::runtime_error("Select a linked root");
        const YAML::Node state=YAML::Load(root.GetComponent<PrefabLink>().State);
        const std::string saved=state["Template"].as<std::string>();
        PrefabArchive archive;
        if(!PrefabArchiveCodec::Decode(std::vector<uint8_t>(saved.begin(),saved.end()),"Prefab baseline",archive,error)) return false;
        PrefabInstantiateOptions options; options.ResolveAssets=false;
        for(const auto& p:state["Mapping"]) options.EntityIdentities.emplace(p.first.as<uint64_t>(),UUID(p.second.as<uint64_t>()));
        for(const auto& p:state["Attachments"]) options.AttachmentIdentities.emplace(UUID(p.first.as<uint64_t>()),UUID(p.second.as<uint64_t>()));
        auto baseline=CreateRef<Scene>(); PrefabInstantiationResult instance;
        if(!PrefabArchiveCodec::Instantiate(archive,*baseline,options,instance,error)) return false;
        for(const auto& [localID,id]:instance.LocalToSceneUUID)
        {
            Entity before=baseline->FindEntityByUUID(id), after=scene->FindEntityByUUID(id);
            if(!before || !after) continue;
            for(const auto& component:ComponentRegistry::Get().GetDescriptors())
            {
                if(!component.InspectorVisible || !component.Has(before) || !component.Has(after)
                    || (id==rootID && static_cast<uint64_t>(component.TypeId)==ComponentIds::Transform)) continue;
                for(const auto& property:component.Properties)
                {
                    auto a=property.Get(before), b=property.Get(after);
                    if(a!=b) values.push_back({id,component.TypeId,property.PropertyId,
                        after.GetName()+" / "+component.DisplayName+" / "+property.DisplayName,a,b});
                }
            }
        }
        error.clear(); return true;
    } catch(const std::exception& ex) { error=ex.what(); return false; }
}

bool PrefabLinkedInstance::RevertProperty(const Ref<Scene>& scene, UUID root, UUID entityID,
    UUID componentID, UUID propertyID, std::string& error)
{
    if(!scene || scene->IsRuntimeRunning()) {error="Property revert requires Edit mode"; return false;}
    std::vector<PrefabPropertyOverride> values;
    if(!GetPropertyOverrides(scene,root,values,error)) return false;
    for(const auto& value:values)
        if(value.EntityID==entityID && value.ComponentID==componentID && value.PropertyID==propertyID)
        {
            const auto* descriptor=ComponentRegistry::Get().Find(componentID);
            if(!descriptor) break;
            for(const auto& property:descriptor->Properties) if(property.PropertyId==propertyID)
                return property.Set(scene->FindEntityByUUID(entityID),value.Before,error);
        }
    error="The selected override no longer exists"; return false;
}

bool PrefabLinkedInstance::ApplyProperty(const Ref<Scene>& scene, UUID rootID, UUID entityID,
    UUID componentID, UUID propertyID, std::string& error)
{
    try {
        Entity root=scene?scene->FindEntityByUUID(rootID):Entity{};
        Entity entity=scene?scene->FindEntityByUUID(entityID):Entity{};
        if(!root || !entity || !root.HasComponent<PrefabLink>() || scene->IsRuntimeRunning())
            throw std::runtime_error("Property Apply requires a linked instance in Edit mode");
        if(rootID==entityID && static_cast<uint64_t>(componentID)==ComponentIds::Transform)
            throw std::runtime_error("Root placement is instance-owned");
        const auto source=root.GetComponent<PrefabLink>().Source;
        const YAML::Node state=YAML::Load(root.GetComponent<PrefabLink>().State);
        std::unordered_map<uint64_t,uint64_t> reverse;
        for(const auto& p:state["Mapping"]) reverse.emplace(p.second.as<uint64_t>(),p.first.as<uint64_t>());
        if(!reverse.contains(static_cast<uint64_t>(entityID))) throw std::runtime_error("Added entities require Apply All");
        const auto* component=ComponentRegistry::Get().Find(componentID);
        if(!component || !component->InspectorVisible || !component->Has(entity)) throw std::runtime_error("Invalid component");
        const PropertyDescriptor* property=nullptr;
        for(const auto& p:component->Properties) if(p.PropertyId==propertyID) property=&p;
        if(!property) throw std::runtime_error("Invalid property");
        auto& assets=AssetManager::Get();
        const auto path=assets.GetRegistry().GetFileSystemPath(source);
        if(path.empty() || !assets.GetRegistry().IsManagedPath(path)) throw std::runtime_error("Apply requires a project prefab");
        auto read=[&](){std::ifstream f(path,std::ios::binary); if(!f) throw std::runtime_error("Cannot read prefab"); return std::string(std::istreambuf_iterator<char>(f),std::istreambuf_iterator<char>());};
        const std::string previous=read();
        PrefabArchive applied;
        if(!LoadLinkedArchive(std::vector<uint8_t>(previous.begin(),previous.end()),path,PathToUTF8(path),applied,error)) return false;
        Entity target=applied.TemplateScene->FindEntityByUUID(UUID(reverse.at(static_cast<uint64_t>(entityID))));
        if(!target || !component->Has(target)) throw std::runtime_error("The source entity/component changed; refresh this instance first");
        PropertyValue value=property->Get(entity);
        if(property->EntityReference)
        {
            uint64_t id=std::get<uint64_t>(value);
            if(id && !reverse.contains(id)) throw std::runtime_error("A prefab cannot reference an external scene entity");
            value=id?reverse.at(id):uint64_t(0);
        }
        if(!property->Set(target,value,error) || !applied.TemplateScene->SyncTransformHierarchy()) return false;
        auto candidate=Scene::Copy(scene);
        if(!candidate) throw std::runtime_error("Cannot stage property Apply");
        for(UUID id:LinkedRoots(candidate))
            if(candidate->FindEntityByUUID(id).GetComponent<PrefabLink>().Source==source)
                if(!Update(candidate,id,applied,false,error)) return false;
        std::string document, sceneDocument;
        if(!PrefabArchiveCodec::Encode(applied,document,error) || !SceneArchiveCodec::Encode(candidate,sceneDocument,error)) return false;
        if(read()!=previous) throw std::runtime_error("Source changed during Apply; retry after reviewing changes");
        if(!FileSystem::WriteFileAtomically(path,document,error)) return false;
        if(assets.ImportAsset(path)!=source || !CommitLinkScene(candidate,scene,error))
        {
            std::string rollback;
            if(!FileSystem::WriteFileAtomically(path,previous,rollback)) error+="; rollback: "+rollback;
            else (void)assets.ImportAsset(path);
            return false;
        }
        error.clear(); return true;
    } catch(const std::exception& ex) {error=ex.what();return false;}
}

}
