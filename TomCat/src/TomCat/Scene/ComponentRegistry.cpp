#include "tcpch.h"
#include "ComponentRegistry.h"

#include "TomCat/Scene/BuiltInComponentDescriptors.h"
#include "TomCat/Scene/Components.h"
#include "TomCat/Scene/RuntimeUIComponentDescriptors.h"
#include "TomCat/Scene/Serialization/PrefabLink.h"

#include <algorithm>
#include <exception>
#include <unordered_map>
#include <unordered_set>

#include <yaml-cpp/yaml.h>

namespace TomCat {

	namespace {
		thread_local ComponentMutationPhase CurrentComponentMutationPhase =
			ComponentMutationPhase::Commit;
		bool RemapEntityValue(uint64_t& value,
			const std::unordered_map<UUID, UUID>& entityMap,
			MissingEntityReferencePolicy missingPolicy,
			std::string_view context, std::string& error)
		{
			if (value == 0)
				return true;
			const auto mapped = entityMap.find(UUID(value));
			if (mapped != entityMap.end())
			{
				value = static_cast<uint64_t>(mapped->second);
				return true;
			}
			if (missingPolicy == MissingEntityReferencePolicy::Preserve)
				return true;
			if (missingPolicy == MissingEntityReferencePolicy::Clear)
			{
				value = 0;
				return true;
			}
			error = std::string(context)
				+ " references an entity outside the instantiated archive";
			return false;
		}

		bool ContainsOnlyFields(const YAML::Node& node,
			std::initializer_list<const char*> fields, std::string& error,
			const std::string& context)
		{
			if (!node || !node.IsMap())
			{
				error = context + " must be a map";
				return false;
			}
			std::unordered_set<std::string> allowed;
			for (const char* field : fields)
				allowed.emplace(field);
			std::unordered_set<std::string> seen;
			for (const auto& pair : node)
			{
				if (!pair.first.IsScalar())
				{
					error = context + " contains a non-scalar field name";
					return false;
				}
				const std::string name = pair.first.as<std::string>();
				if (!allowed.contains(name) || !seen.emplace(name).second)
				{
					error = context + " contains unknown or duplicate field '" + name + "'";
					return false;
				}
			}
			for (const char* field : fields)
			{
				if (!seen.contains(field))
				{
					error = context + " is missing required field '" + field + "'";
					return false;
				}
			}
			return true;
		}

		void EmitPropertyValue(YAML::Emitter& output, PropertyKind kind,
			const PropertyValue& value)
		{
			switch (kind)
			{
				case PropertyKind::Bool: output << std::get<bool>(value); break;
				case PropertyKind::Int32: output << std::get<int32_t>(value); break;
				case PropertyKind::Int64: output << std::get<int64_t>(value); break;
				case PropertyKind::UInt32: output << std::get<uint32_t>(value); break;
				case PropertyKind::UInt64: output << std::get<uint64_t>(value); break;
				case PropertyKind::Float: output << std::get<float>(value); break;
				case PropertyKind::Double: output << std::get<double>(value); break;
				case PropertyKind::String: output << std::get<std::string>(value); break;
				case PropertyKind::Vector2:
				{
					const auto& item = std::get<glm::vec2>(value);
					output << YAML::Flow << YAML::BeginSeq << item.x << item.y << YAML::EndSeq;
					break;
				}
				case PropertyKind::Vector3:
				{
					const auto& item = std::get<glm::vec3>(value);
					output << YAML::Flow << YAML::BeginSeq << item.x << item.y << item.z
						<< YAML::EndSeq;
					break;
				}
				case PropertyKind::Vector4:
				{
					const auto& item = std::get<glm::vec4>(value);
					output << YAML::Flow << YAML::BeginSeq << item.x << item.y << item.z
						<< item.w << YAML::EndSeq;
					break;
				}
			}
		}

		PropertyValue ReadPropertyValue(const YAML::Node& node, PropertyKind kind)
		{
			switch (kind)
			{
				case PropertyKind::Bool: return node.as<bool>();
				case PropertyKind::Int32: return node.as<int32_t>();
				case PropertyKind::Int64: return node.as<int64_t>();
				case PropertyKind::UInt32: return node.as<uint32_t>();
				case PropertyKind::UInt64: return node.as<uint64_t>();
				case PropertyKind::Float: return node.as<float>();
				case PropertyKind::Double: return node.as<double>();
				case PropertyKind::String: return node.as<std::string>();
				case PropertyKind::Vector2:
					if (!node.IsSequence() || node.size() != 2)
						throw std::runtime_error("Vector2 value must contain two numbers");
					return glm::vec2(node[0].as<float>(), node[1].as<float>());
				case PropertyKind::Vector3:
					if (!node.IsSequence() || node.size() != 3)
						throw std::runtime_error("Vector3 value must contain three numbers");
					return glm::vec3(node[0].as<float>(), node[1].as<float>(),
						node[2].as<float>());
				case PropertyKind::Vector4:
					if (!node.IsSequence() || node.size() != 4)
						throw std::runtime_error("Vector4 value must contain four numbers");
					return glm::vec4(node[0].as<float>(), node[1].as<float>(),
						node[2].as<float>(), node[3].as<float>());
			}
			throw std::runtime_error("unsupported property kind");
		}

		bool PropertyValueMatchesKind(const PropertyValue& value, PropertyKind kind)
		{
			switch (kind)
			{
				case PropertyKind::Bool: return std::holds_alternative<bool>(value);
				case PropertyKind::Int32: return std::holds_alternative<int32_t>(value);
				case PropertyKind::Int64: return std::holds_alternative<int64_t>(value);
				case PropertyKind::UInt32: return std::holds_alternative<uint32_t>(value);
				case PropertyKind::UInt64: return std::holds_alternative<uint64_t>(value);
				case PropertyKind::Float: return std::holds_alternative<float>(value);
				case PropertyKind::Double: return std::holds_alternative<double>(value);
				case PropertyKind::String: return std::holds_alternative<std::string>(value);
				case PropertyKind::Vector2: return std::holds_alternative<glm::vec2>(value);
				case PropertyKind::Vector3: return std::holds_alternative<glm::vec3>(value);
				case PropertyKind::Vector4: return std::holds_alternative<glm::vec4>(value);
			}
			return false;
		}

		bool EncodeDescriptor(const ComponentDescriptor& descriptor, Entity entity,
			YAML::Emitter& output, std::string& error)
		{
			try
			{
				output << YAML::BeginSeq;
				for (const PropertyDescriptor& property : descriptor.Properties)
				{
					if (!property.Get)
						throw std::runtime_error("property has no getter");
					output << YAML::BeginMap;
					output << YAML::Key << "PropertyId" << YAML::Value
						<< static_cast<uint64_t>(property.PropertyId);
					output << YAML::Key << "StableName" << YAML::Value << property.StableName;
					output << YAML::Key << "Value" << YAML::Value;
					EmitPropertyValue(output, property.Kind, property.Get(entity));
					output << YAML::EndMap;
				}
				output << YAML::EndSeq;
				return output.good();
			}
			catch (const std::exception& exception)
			{
				error = descriptor.StableName + ": " + exception.what();
				return false;
			}
		}

		bool DecodeDescriptor(const ComponentDescriptor& descriptor, Entity entity,
			const YAML::Node& properties, std::string& error)
		{
			if (!properties || !properties.IsSequence())
			{
				error = descriptor.StableName + ".Properties must be a sequence";
				return false;
			}
			std::unordered_map<uint64_t, YAML::Node> values;
			try
			{
				for (size_t index = 0; index < properties.size(); ++index)
				{
					const YAML::Node propertyNode = properties[index];
					const std::string context = descriptor.StableName + ".Properties["
						+ std::to_string(index) + "]";
					if (!ContainsOnlyFields(propertyNode,
						{ "PropertyId", "StableName", "Value" }, error, context))
						return false;
					const uint64_t id = propertyNode["PropertyId"].as<uint64_t>();
					if (id == 0 || !values.emplace(id, propertyNode).second)
					{
						error = context + ".PropertyId must be nonzero and unique";
						return false;
					}
					auto property = std::find_if(descriptor.Properties.begin(),
						descriptor.Properties.end(), [id](const PropertyDescriptor& candidate)
						{
							return static_cast<uint64_t>(candidate.PropertyId) == id;
						});
					if (property == descriptor.Properties.end())
					{
						error = context + " uses an unknown PropertyId";
						return false;
					}
					if (propertyNode["StableName"].as<std::string>() != property->StableName)
					{
						error = context + ".StableName does not match its PropertyId";
						return false;
					}
				}
				if (values.size() != descriptor.Properties.size())
				{
					error = descriptor.StableName + " is missing one or more properties";
					return false;
				}
				// Apply in descriptor order rather than document order. This lets a
				// component establish invariants (Health.Maximum before Current) while
				// keeping YAML maps/sequences semantically order-independent.
				for (const PropertyDescriptor& property : descriptor.Properties)
				{
					const YAML::Node& propertyNode = values.at(
						static_cast<uint64_t>(property.PropertyId));
					if (!property.Set || !property.Set(entity,
						ReadPropertyValue(propertyNode["Value"], property.Kind), error))
						return false;
				}
				return true;
			}
			catch (const std::exception& exception)
			{
				error = descriptor.StableName + ": " + exception.what();
				return false;
			}
		}

		bool PrepareComponentRecord(const ComponentDescriptor& descriptor,
			const YAML::Node& source, YAML::Node& prepared, bool& compatible,
			std::string& error)
		{
			compatible = false;
			try
			{
				uint32_t version = source["SchemaVersion"].as<uint32_t>();
				if (version > descriptor.SchemaVersion)
					return true;
				prepared = YAML::Load(YAML::Dump(source));
				while (version < descriptor.SchemaVersion)
				{
					const auto migration = std::find_if(descriptor.Migrations.begin(),
						descriptor.Migrations.end(), [version](const auto& candidate)
						{
							return candidate.FromVersion == version;
						});
					if (migration == descriptor.Migrations.end())
						return true;
					if (!migration->Migrate(prepared, error))
					{
						if (error.empty())
							error = descriptor.StableName + " schema migration failed";
						return false;
					}
					version = migration->ToVersion;
					prepared["SchemaVersion"] = version;
					std::string validationError;
					if (!ContainsOnlyFields(prepared,
						{ "TypeId", "StableName", "SchemaVersion", "Properties" },
						validationError, descriptor.StableName)
						|| prepared["TypeId"].as<uint64_t>()
							!= static_cast<uint64_t>(descriptor.TypeId)
						|| prepared["StableName"].as<std::string>()
							!= descriptor.StableName)
					{
						error = validationError.empty()
							? descriptor.StableName + " migration changed component identity"
							: validationError;
						return false;
					}
				}
				compatible = version == descriptor.SchemaVersion;
				return true;
			}
			catch (const std::exception& exception)
			{
				error = descriptor.StableName + " migration: " + exception.what();
				return false;
			}
		}

		bool EncodeComponentRecord(const ComponentDescriptor& descriptor,
			Entity entity, OpaqueComponentRecord& record, std::string& error)
		{
			try
			{
				YAML::Emitter output;
				output << YAML::BeginMap;
				output << YAML::Key << "TypeId" << YAML::Value
					<< static_cast<uint64_t>(descriptor.TypeId);
				output << YAML::Key << "StableName" << YAML::Value
					<< descriptor.StableName;
				output << YAML::Key << "SchemaVersion" << YAML::Value
					<< descriptor.SchemaVersion;
				output << YAML::Key << "Properties" << YAML::Value;
				if (!descriptor.Encode(descriptor, entity, output, error))
					return false;
				output << YAML::EndMap;
				if (!output.good())
				{
					error = "Could not serialize " + descriptor.StableName
						+ " before provider unload";
					return false;
				}
				record = { descriptor.TypeId, descriptor.StableName,
					descriptor.SchemaVersion, output.c_str() };
				return true;
			}
			catch (const std::exception& exception)
			{
				error = descriptor.StableName + ": " + exception.what();
				return false;
			}
		}

		bool DecodeComponentTransactionally(const ComponentDescriptor& descriptor,
			Entity entity, const YAML::Node& record, std::string& error)
		{
			if (descriptor.Has(entity))
			{
				error = descriptor.StableName + " already exists on the entity";
				return false;
			}
			if (!descriptor.Add(entity, error))
				return false;
			if (!descriptor.Has(entity))
			{
				error = descriptor.StableName
					+ " provider reported success without adding its component";
				return false;
			}
			if (descriptor.Decode(descriptor, entity, record["Properties"], error))
				return true;

			std::string rollbackError;
			if (!descriptor.Remove(entity, rollbackError) && !rollbackError.empty())
				error += "; rollback failed: " + rollbackError;
			return false;
		}

		bool MirroredPayloadMatches(const ComponentDescriptor& descriptor,
			Entity entity, const YAML::Node& properties, std::string& error)
		{
			try
			{
				YAML::Emitter current;
				if (!descriptor.Encode(descriptor, entity, current, error)
					|| !current.good())
					return false;
				const YAML::Node expected = YAML::Load(current.c_str());
				if (YAML::Dump(expected) != YAML::Dump(properties))
				{
					error = descriptor.StableName
						+ " registry payload disagrees with its legacy Scene 11 field";
					return false;
				}
				return true;
			}
			catch (const std::exception& exception)
			{
				error = descriptor.StableName + ": " + exception.what();
				return false;
			}
		}

		ComponentDescriptor MakeHealthDescriptor()
		{
			ComponentDescriptor descriptor;
			descriptor.TypeId = UUID(ComponentIds::Health);
			descriptor.StableName = "TomCat.HealthComponent";
			descriptor.DisplayName = "Health";
			descriptor.SchemaVersion = 1;
			descriptor.ScriptAccessible = true;
			descriptor.Has = [](Entity entity)
			{
				return entity && entity.HasComponent<HealthComponent>();
			};
			descriptor.Add = [](Entity entity, std::string& error)
			{
				if (!entity)
				{
					error = "Cannot add HealthComponent to an invalid entity";
					return false;
				}
				if (!entity.HasComponent<HealthComponent>())
					entity.AddComponent<HealthComponent>();
				return true;
			};
			descriptor.Remove = [](Entity entity, std::string& error)
			{
				if (!entity)
				{
					error = "Cannot remove HealthComponent from an invalid entity";
					return false;
				}
				entity.RemoveComponent<HealthComponent>();
				return true;
			};
			descriptor.Copy = [](Entity source, Entity destination, std::string& error)
			{
				if (!source || !destination || !source.HasComponent<HealthComponent>())
				{
					error = "Cannot copy HealthComponent from invalid entities";
					return false;
				}
				destination.AddOrReplaceComponent<HealthComponent>(
					source.GetComponent<HealthComponent>());
				return true;
			};
			descriptor.Encode = &EncodeDescriptor;
			descriptor.Decode = [](const ComponentDescriptor& component, Entity entity,
				const YAML::Node& properties, std::string& error)
			{
				// Decode transactionally from a neutral Current value. Otherwise a valid
				// persisted Maximum below the constructor default (100) would be rejected
				// before its matching Current value has been applied.
				auto& health = entity.GetComponent<HealthComponent>();
				const HealthComponent original = health;
				health.Current = 0;
				if (DecodeDescriptor(component, entity, properties, error))
					return true;
				health = original;
				return false;
			};

			PropertyDescriptor maximum;
			maximum.PropertyId = UUID(ComponentIds::HealthProperties::Maximum);
			maximum.StableName = "Maximum";
			maximum.DisplayName = "Maximum";
			maximum.Kind = PropertyKind::Int32;
			maximum.DefaultValue = HealthComponent{}.Maximum;
			maximum.Get = [](Entity entity) -> PropertyValue
			{
				return entity.GetComponent<HealthComponent>().Maximum;
			};
			maximum.Set = [](Entity entity, const PropertyValue& value, std::string& error)
			{
				const int32_t maximumValue = std::get<int32_t>(value);
				auto& health = entity.GetComponent<HealthComponent>();
				if (maximumValue <= 0 || maximumValue < health.Current)
				{
					error = "Health.Maximum must be positive and at least Current";
					return false;
				}
				health.Maximum = maximumValue;
				return true;
			};

			PropertyDescriptor current;
			current.PropertyId = UUID(ComponentIds::HealthProperties::Current);
			current.StableName = "Current";
			current.DisplayName = "Current";
			current.Kind = PropertyKind::Int32;
			current.DefaultValue = HealthComponent{}.Current;
			current.Get = [](Entity entity) -> PropertyValue
			{
				return entity.GetComponent<HealthComponent>().Current;
			};
			current.Set = [](Entity entity, const PropertyValue& value, std::string& error)
			{
				const int32_t currentValue = std::get<int32_t>(value);
				auto& health = entity.GetComponent<HealthComponent>();
				if (currentValue < 0 || currentValue > health.Maximum)
				{
					error = "Health.Current must be between zero and Maximum";
					return false;
				}
				health.Current = currentValue;
				return true;
			};

			PropertyDescriptor invulnerable;
			invulnerable.PropertyId = UUID(ComponentIds::HealthProperties::Invulnerable);
			invulnerable.StableName = "Invulnerable";
			invulnerable.DisplayName = "Invulnerable";
			invulnerable.Kind = PropertyKind::Bool;
			invulnerable.DefaultValue = HealthComponent{}.Invulnerable;
			invulnerable.Get = [](Entity entity) -> PropertyValue
			{
				return entity.GetComponent<HealthComponent>().Invulnerable;
			};
			invulnerable.Set = [](Entity entity, const PropertyValue& value, std::string&)
			{
				entity.GetComponent<HealthComponent>().Invulnerable = std::get<bool>(value);
				return true;
			};

			descriptor.Properties = { std::move(maximum), std::move(current),
				std::move(invulnerable) };
			return descriptor;
		}

	}

	ComponentMutationPhase GetComponentMutationPhase() noexcept
	{
		return CurrentComponentMutationPhase;
	}

	ComponentMutationPhaseScope::ComponentMutationPhaseScope(
		ComponentMutationPhase phase) noexcept
		: m_Previous(CurrentComponentMutationPhase)
	{
		CurrentComponentMutationPhase = phase;
	}

	ComponentMutationPhaseScope::~ComponentMutationPhaseScope() noexcept
	{
		CurrentComponentMutationPhase = m_Previous;
	}

	ComponentRegistry& ComponentRegistry::Get()
	{
		static ComponentRegistry registry;
		return registry;
	}

	ComponentRegistry::ComponentRegistry()
	{
		std::string error;
		for (ComponentDescriptor descriptor : MakeBuiltInComponentDescriptors())
		{
			const std::string stableName = descriptor.StableName;
			if (!Register(std::move(descriptor), error))
				throw std::runtime_error("Could not register built-in "
					+ stableName + ": " + error);
		}
		if (!Register(MakeHealthDescriptor(), error))
			throw std::runtime_error("Could not register built-in HealthComponent: " + error);
		if (!Register(MakePrefabLinkDescriptor(), error))
			throw std::runtime_error("Could not register PrefabLink: " + error);
		for (ComponentDescriptor descriptor : MakeRuntimeUIComponentDescriptors())
		{
			const std::string stableName = descriptor.StableName;
			if (!Register(std::move(descriptor), error))
				throw std::runtime_error("Could not register built-in "
					+ stableName + ": " + error);
		}
	}

	bool ComponentRegistry::Register(ComponentDescriptor descriptor, std::string& error)
	{
		error.clear();
		// Property-only descriptors use the registry's canonical YAML codec. A
		// provider supplies custom callbacks only for structured component data.
		if (!descriptor.Encode)
			descriptor.Encode = &EncodeDescriptor;
		if (!descriptor.Decode)
			descriptor.Decode = &DecodeDescriptor;
		const uint64_t typeId = static_cast<uint64_t>(descriptor.TypeId);
		if (typeId == 0 || descriptor.StableName.empty() || descriptor.DisplayName.empty()
			|| descriptor.SchemaVersion == 0 || !descriptor.Has || !descriptor.Add
			|| !descriptor.Remove || !descriptor.Copy || !descriptor.Encode
			|| !descriptor.Decode)
		{
			error = "Component descriptor is incomplete";
			return false;
		}
		if (descriptor.ScriptAccessible
			&& static_cast<uint64_t>(descriptor.ProviderId) != 0
			&& !descriptor.SupportsTransactionalValidation)
		{
			error = "Third-party script-accessible components must support "
				"transactional validation phase";
			return false;
		}
		for (const ComponentDescriptor& existing : m_Descriptors)
		{
			if (existing.TypeId == descriptor.TypeId
				|| existing.StableName == descriptor.StableName)
			{
				error = "Component TypeId and StableName must be unique";
				return false;
			}
		}
		std::unordered_set<uint64_t> propertyIds;
		std::unordered_set<std::string> propertyNames;
		for (const PropertyDescriptor& property : descriptor.Properties)
		{
			const uint64_t propertyId = static_cast<uint64_t>(property.PropertyId);
			if (propertyId == 0 || property.StableName.empty()
				|| property.DisplayName.empty() || !property.Get || !property.Set
				|| !propertyIds.emplace(propertyId).second
				|| !propertyNames.emplace(property.StableName).second)
			{
				error = "Property IDs and stable names must be nonzero and unique";
				return false;
			}
			if (property.AssetReference)
			{
				if (property.Kind != PropertyKind::UInt64
					|| property.AssetReference->AcceptedTypes.empty())
				{
					error = "Asset-reference metadata requires a UInt64 property and at least one type";
					return false;
				}
				std::unordered_set<uint16_t> acceptedTypes;
				for (const AssetType type : property.AssetReference->AcceptedTypes)
				{
					if (type == AssetType::None
						|| !acceptedTypes.emplace(static_cast<uint16_t>(type)).second)
					{
						error = "Asset-reference types must be non-None and unique";
						return false;
					}
				}
			}
			if (descriptor.ScriptAccessible
				&& (!property.DefaultValue
					|| !PropertyValueMatchesKind(*property.DefaultValue, property.Kind)))
			{
				error = "Script-accessible properties require a type-correct DefaultValue";
				return false;
			}
			if (property.EntityReference
				&& (property.Kind != PropertyKind::UInt64
					|| property.AssetReference.has_value()))
			{
				error = "Entity-reference metadata requires a non-asset UInt64 property";
				return false;
			}
		}
		std::sort(descriptor.Migrations.begin(), descriptor.Migrations.end(),
			[](const ComponentSchemaMigration& left,
				const ComponentSchemaMigration& right)
			{
				return left.FromVersion < right.FromVersion;
			});
		uint32_t previousFromVersion = 0;
		for (const ComponentSchemaMigration& migration : descriptor.Migrations)
		{
			if (migration.FromVersion == 0
				|| migration.FromVersion >= migration.ToVersion
				|| migration.ToVersion > descriptor.SchemaVersion
				|| !migration.Migrate
				|| migration.FromVersion == previousFromVersion)
			{
				error = "Component schema migrations must be unique, forward-only, and bounded by the current schema";
				return false;
			}
			previousFromVersion = migration.FromVersion;
		}
		// Providers can be registered after scenes exist. Apply their explicit
		// storage declaration at every descriptor entry point that creates data.
		if (descriptor.RegisterStorage)
		{
			const auto registerStorage = descriptor.RegisterStorage;
			descriptor.Add = [registerStorage, add = std::move(descriptor.Add)]
				(Entity entity, std::string& error) {
				if (entity) registerStorage(*entity.GetScene());
				return add(entity, error);
			};
			descriptor.Copy = [registerStorage, copy = std::move(descriptor.Copy)]
				(Entity source, Entity destination, std::string& error) {
				if (destination) registerStorage(*destination.GetScene());
				return copy(source, destination, error);
			};
		}
		m_Descriptors.emplace_back(std::move(descriptor));
		std::sort(m_Descriptors.begin(), m_Descriptors.end(),
			[](const ComponentDescriptor& left, const ComponentDescriptor& right)
			{
				return static_cast<uint64_t>(left.TypeId)
					< static_cast<uint64_t>(right.TypeId);
			});
		return true;
	}

	const ComponentDescriptor* ComponentRegistry::Find(UUID typeId) const
	{
		for (const ComponentDescriptor& descriptor : m_Descriptors)
		{
			if (descriptor.TypeId == typeId)
				return &descriptor;
		}
		return nullptr;
	}

	bool ComponentRegistry::Has(Entity entity, UUID typeId) const
	{
		const ComponentDescriptor* descriptor = Find(typeId);
		return descriptor && descriptor->Has(entity);
	}

	bool ComponentRegistry::Add(Entity entity, UUID typeId, std::string& error) const
	{
		const ComponentDescriptor* descriptor = Find(typeId);
		if (!descriptor)
		{
			error = "Unknown component TypeId " + std::to_string(static_cast<uint64_t>(typeId));
			return false;
		}
		return descriptor->Add(entity, error);
	}

	bool ComponentRegistry::Remove(Entity entity, UUID typeId, std::string& error) const
	{
		const ComponentDescriptor* descriptor = Find(typeId);
		if (!descriptor)
		{
			error = "Unknown component TypeId " + std::to_string(static_cast<uint64_t>(typeId));
			return false;
		}
		return descriptor->Remove(entity, error);
	}

	bool ComponentRegistry::CopyRegisteredComponents(Entity source, Entity destination,
		std::string& error) const
	{
		for (const ComponentDescriptor& descriptor : m_Descriptors)
		{
			if (descriptor.Has(source) && !descriptor.Copy(source, destination, error))
				return false;
		}
		if (source.HasComponent<OpaqueComponents>())
			destination.AddOrReplaceComponent<OpaqueComponents>(
				source.GetComponent<OpaqueComponents>());
		return true;
	}

	bool ComponentRegistry::RemapEntityReferences(Entity entity,
		const std::unordered_map<UUID, UUID>& entityMap,
		MissingEntityReferencePolicy missingPolicy, std::string& error) const
	{
		error.clear();
		if (!entity)
		{
			error = "Cannot remap references on an invalid entity";
			return false;
		}

		const EntityReferenceMapper mapper = [&](uint64_t& value,
			std::string_view context, std::string& mapperError)
		{
			return RemapEntityValue(value, entityMap, missingPolicy, context,
				mapperError);
		};
		try
		{
			for (const ComponentDescriptor& descriptor : m_Descriptors)
			{
				if (!descriptor.Has(entity))
					continue;
				for (const PropertyDescriptor& property : descriptor.Properties)
				{
					if (!property.EntityReference)
						continue;
					const std::string context = descriptor.StableName + "."
						+ property.StableName;
					const PropertyValue current = property.Get(entity);
					if (!std::holds_alternative<uint64_t>(current))
					{
						error = context + " returned an incompatible value";
						return false;
					}
					const uint64_t original = std::get<uint64_t>(current);
					uint64_t remapped = original;
					if (!mapper(remapped, context, error))
						return false;
					if (remapped != original
						&& !property.Set(entity, PropertyValue(remapped), error))
					{
						if (error.empty())
							error = context + " rejected the remapped entity";
						return false;
					}
				}
				if (descriptor.RemapEntityReferences
					&& !descriptor.RemapEntityReferences(entity, mapper, error))
				{
					if (error.empty())
						error = descriptor.StableName
							+ " failed to remap structured entity references";
					return false;
				}
			}
			return true;
		}
		catch (const std::exception& exception)
		{
			error = exception.what();
			return false;
		}
	}

	bool ComponentRegistry::EncodeLegacyComponents(Entity entity,
		YAML::Emitter& output, std::string& error) const
	{
		error.clear();
		try
		{
			for (const ComponentDescriptor& descriptor : m_Descriptors)
			{
				if (!descriptor.EncodeLegacyFields || !descriptor.Has(entity))
					continue;
				if (!descriptor.EncodeLegacyFields(descriptor, entity, output, error))
				{
					if (error.empty())
						error = descriptor.StableName
							+ " legacy compatibility encoding failed";
					return false;
				}
			}
			return output.good();
		}
		catch (const std::exception& exception)
		{
			error = exception.what();
			return false;
		}
	}

	bool ComponentRegistry::DecodeLegacyComponents(Entity entity,
		const YAML::Node& entityNode, std::string& error) const
	{
		error.clear();
		try
		{
			if (!entityNode || !entityNode.IsMap())
			{
				error = "Legacy entity fields must be a map";
				return false;
			}
			for (const ComponentDescriptor& descriptor : m_Descriptors)
			{
				if (!descriptor.DecodeLegacyFields)
					continue;
				if (!descriptor.DecodeLegacyFields(descriptor, entity,
					entityNode, error))
				{
					if (error.empty())
						error = descriptor.StableName
							+ " legacy compatibility decoding failed";
					return false;
				}
			}
			return true;
		}
		catch (const std::exception& exception)
		{
			error = exception.what();
			return false;
		}
	}

	bool ComponentRegistry::EncodeComponents(Entity entity, YAML::Emitter& output,
		std::string& error) const
	{
		error.clear();
		try
		{
			std::unordered_set<uint64_t> emitted;
			output << YAML::Key << "Components" << YAML::Value << YAML::BeginSeq;
			for (const ComponentDescriptor& descriptor : m_Descriptors)
			{
				if (!descriptor.PersistInComponentSequence || !descriptor.Has(entity))
					continue;
				const uint64_t typeId = static_cast<uint64_t>(descriptor.TypeId);
				emitted.emplace(typeId);
				output << YAML::BeginMap;
				output << YAML::Key << "TypeId" << YAML::Value << typeId;
				output << YAML::Key << "StableName" << YAML::Value << descriptor.StableName;
				output << YAML::Key << "SchemaVersion" << YAML::Value
					<< descriptor.SchemaVersion;
				output << YAML::Key << "Properties" << YAML::Value;
				if (!descriptor.Encode(descriptor, entity, output, error))
					return false;
				output << YAML::EndMap;
			}
			if (entity.HasComponent<OpaqueComponents>())
			{
				for (const OpaqueComponentRecord& record :
					entity.GetComponent<OpaqueComponents>().Records)
				{
					const uint64_t typeId = static_cast<uint64_t>(record.TypeId);
					if (typeId == 0 || !emitted.emplace(typeId).second)
					{
						error = "Opaque component TypeId must be nonzero and unique";
						return false;
					}
					const YAML::Node node = YAML::Load(record.SerializedRecord);
					std::string validationError;
					if (!ContainsOnlyFields(node,
						{ "TypeId", "StableName", "SchemaVersion", "Properties" },
						validationError, "Opaque component"))
					{
						error = validationError;
						return false;
					}
					if (node["TypeId"].as<uint64_t>() != typeId
						|| node["StableName"].as<std::string>() != record.StableName
						|| node["SchemaVersion"].as<uint32_t>() != record.SchemaVersion)
					{
						error = "Opaque component cached identity does not match its payload";
						return false;
					}
					output << node;
				}
			}
			output << YAML::EndSeq;
			return output.good();
		}
		catch (const std::exception& exception)
		{
			error = exception.what();
			return false;
		}
	}

	bool ComponentRegistry::DecodeComponents(Entity entity,
		const YAML::Node& components, std::string& error) const
	{
		error.clear();
		if (!components || !components.IsSequence())
		{
			error = "Components must be a sequence";
			return false;
		}
		std::unordered_set<uint64_t> seen;
		for (size_t index = 0; index < components.size(); ++index)
		{
			try
			{
				const YAML::Node node = components[index];
				const std::string context = "Components[" + std::to_string(index) + "]";
				if (!ContainsOnlyFields(node,
					{ "TypeId", "StableName", "SchemaVersion", "Properties" }, error,
					context))
					return false;
				const uint64_t typeId = node["TypeId"].as<uint64_t>();
				const std::string stableName = node["StableName"].as<std::string>();
				const uint32_t schemaVersion = node["SchemaVersion"].as<uint32_t>();
				if (typeId == 0 || stableName.empty() || schemaVersion == 0
					|| !seen.emplace(typeId).second)
				{
					error = context + " identity and schema must be nonzero and unique";
					return false;
				}

				const ComponentDescriptor* descriptor = Find(UUID(typeId));
				if (descriptor && descriptor->StableName != stableName)
				{
					error = context + ".StableName does not match registered TypeId";
					return false;
				}
				YAML::Node prepared;
				bool compatible = false;
				if (descriptor && !PrepareComponentRecord(*descriptor, node,
					prepared, compatible, error))
					return false;
				if (!descriptor || !compatible)
				{
					if (!entity.HasComponent<OpaqueComponents>())
						entity.AddComponent<OpaqueComponents>();
					entity.GetComponent<OpaqueComponents>().Records.push_back(
						{ UUID(typeId), stableName, schemaVersion, YAML::Dump(node) });
					continue;
				}
				if (descriptor->Has(entity) && descriptor->DecodeIntoExisting)
				{
					// Scene 11 writes both representations for compatibility. Reject
					// disagreement instead of silently choosing one, which also keeps
					// Prefab reference validation and transaction rollback deterministic.
					if (!MirroredPayloadMatches(*descriptor, entity,
						prepared["Properties"], error))
					{
						error = context + ": " + error;
						return false;
					}
				}
				else if (!DecodeComponentTransactionally(*descriptor, entity,
					prepared, error))
				{
					error = context + ": " + error;
					return false;
				}
			}
			catch (const std::exception& exception)
			{
				error = "Components[" + std::to_string(index) + "]: " + exception.what();
				return false;
			}
		}
		return true;
	}

	bool ComponentRegistry::UnregisterProvider(UUID providerId,
		std::span<const Entity> liveEntities, std::string& error)
	{
		error.clear();
		if (static_cast<uint64_t>(providerId) == 0)
		{
			error = "ProviderId zero is reserved for built-in components";
			return false;
		}
		std::vector<const ComponentDescriptor*> descriptors;
		for (const ComponentDescriptor& descriptor : m_Descriptors)
			if (descriptor.ProviderId == providerId)
				descriptors.push_back(&descriptor);
		if (descriptors.empty())
		{
			error = "Component provider is not registered";
			return false;
		}

		struct PendingDetach
		{
			Entity Target;
			const ComponentDescriptor* Descriptor = nullptr;
			OpaqueComponentRecord Record;
		};
		std::vector<PendingDetach> pending;
		for (Entity entity : liveEntities)
		{
			if (!entity)
			{
				error = "Provider unload received an invalid live entity";
				return false;
			}
			for (const ComponentDescriptor* descriptor : descriptors)
			{
				if (!descriptor->Has(entity))
					continue;
				if (entity.HasComponent<OpaqueComponents>())
				{
					const auto& records = entity.GetComponent<OpaqueComponents>().Records;
					if (std::any_of(records.begin(), records.end(), [&](const auto& record)
						{ return record.TypeId == descriptor->TypeId; }))
					{
						error = descriptor->StableName
							+ " already has an opaque record on the entity";
						return false;
					}
				}
				PendingDetach item;
				item.Target = entity;
				item.Descriptor = descriptor;
				if (!EncodeComponentRecord(*descriptor, entity, item.Record, error))
					return false;
				pending.push_back(std::move(item));
			}
		}

		auto eraseOpaqueRecord = [](Entity entity, UUID typeId)
		{
			if (!entity.HasComponent<OpaqueComponents>())
				return;
			auto& records = entity.GetComponent<OpaqueComponents>().Records;
			records.erase(std::remove_if(records.begin(), records.end(),
				[typeId](const OpaqueComponentRecord& record)
				{ return record.TypeId == typeId; }), records.end());
			if (records.empty())
				entity.RemoveComponent<OpaqueComponents>();
		};

		size_t applied = 0;
		for (; applied < pending.size(); ++applied)
		{
			PendingDetach& item = pending[applied];
			if (!item.Target.HasComponent<OpaqueComponents>())
				item.Target.AddComponent<OpaqueComponents>();
			item.Target.GetComponent<OpaqueComponents>().Records.push_back(item.Record);
			if (!item.Descriptor->Remove(item.Target, error))
				break;
		}
		if (applied != pending.size())
		{
			const std::string failure = error.empty()
				? "Provider component removal failed" : error;
			const size_t rollbackCount = std::min(applied + 1, pending.size());
			for (size_t index = rollbackCount; index-- > 0;)
			{
				PendingDetach& item = pending[index];
				eraseOpaqueRecord(item.Target, item.Descriptor->TypeId);
				if (!item.Descriptor->Has(item.Target))
				{
					try
					{
						const YAML::Node record = YAML::Load(item.Record.SerializedRecord);
						std::string rollbackError;
						if (!DecodeComponentTransactionally(*item.Descriptor,
							item.Target, record, rollbackError))
							error = failure + "; rollback failed: " + rollbackError;
					}
					catch (const std::exception& exception)
					{
						error = failure + "; rollback failed: " + exception.what();
					}
				}
			}
			if (error.empty()) error = failure;
			return false;
		}

		m_Descriptors.erase(std::remove_if(m_Descriptors.begin(), m_Descriptors.end(),
			[providerId](const ComponentDescriptor& descriptor)
			{ return descriptor.ProviderId == providerId; }), m_Descriptors.end());
		return true;
	}

	bool ComponentRegistry::RehydrateOpaqueComponents(
		std::span<const Entity> liveEntities, UUID providerId,
		std::string& error) const
	{
		error.clear();
		if (static_cast<uint64_t>(providerId) == 0)
		{
			error = "ProviderId zero is reserved for built-in components";
			return false;
		}
		for (Entity entity : liveEntities)
		{
			if (!entity)
			{
				error = "Component rehydration received an invalid live entity";
				return false;
			}
			if (!entity.HasComponent<OpaqueComponents>())
				continue;
			auto& records = entity.GetComponent<OpaqueComponents>().Records;
			for (size_t index = 0; index < records.size();)
			{
				const ComponentDescriptor* descriptor = Find(records[index].TypeId);
				if (!descriptor || descriptor->ProviderId != providerId)
				{
					++index;
					continue;
				}
				if (descriptor->StableName != records[index].StableName)
				{
					error = "Opaque component StableName does not match its registered TypeId";
					return false;
				}
				try
				{
					const YAML::Node source = YAML::Load(records[index].SerializedRecord);
					YAML::Node prepared;
					bool compatible = false;
					if (!PrepareComponentRecord(*descriptor, source, prepared,
						compatible, error))
						return false;
					if (!compatible)
					{
						++index;
						continue;
					}
					if (!DecodeComponentTransactionally(*descriptor, entity,
						prepared, error))
						return false;
					records.erase(records.begin() + static_cast<std::ptrdiff_t>(index));
				}
				catch (const std::exception& exception)
				{
					error = descriptor->StableName + ": " + exception.what();
					return false;
				}
			}
			if (records.empty())
				entity.RemoveComponent<OpaqueComponents>();
		}
		return true;
	}

}
