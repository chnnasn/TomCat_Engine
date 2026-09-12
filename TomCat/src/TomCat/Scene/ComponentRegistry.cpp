#include "tcpch.h"
#include "ComponentRegistry.h"

#include "TomCat/Scene/Components.h"
#include "TomCat/Scene/RuntimeUIComponentDescriptors.h"

#include <algorithm>
#include <exception>
#include <unordered_map>
#include <unordered_set>

#include <yaml-cpp/yaml.h>

namespace TomCat {

	namespace {

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

	ComponentRegistry& ComponentRegistry::Get()
	{
		static ComponentRegistry registry;
		return registry;
	}

	ComponentRegistry::ComponentRegistry()
	{
		std::string error;
		if (!Register(MakeHealthDescriptor(), error))
			throw std::runtime_error("Could not register built-in HealthComponent: " + error);
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
		const uint64_t typeId = static_cast<uint64_t>(descriptor.TypeId);
		if (typeId == 0 || descriptor.StableName.empty() || descriptor.DisplayName.empty()
			|| descriptor.SchemaVersion == 0 || !descriptor.Has || !descriptor.Add
			|| !descriptor.Remove || !descriptor.Copy || !descriptor.Encode
			|| !descriptor.Decode)
		{
			error = "Component descriptor is incomplete";
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
				if (!descriptor.Has(entity))
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
				if (!descriptor || descriptor->SchemaVersion != schemaVersion)
				{
					if (!entity.HasComponent<OpaqueComponents>())
						entity.AddComponent<OpaqueComponents>();
					entity.GetComponent<OpaqueComponents>().Records.push_back(
						{ UUID(typeId), stableName, schemaVersion, YAML::Dump(node) });
					continue;
				}
				if (descriptor->Has(entity))
				{
					error = context + " duplicates an existing component";
					return false;
				}
				if (!descriptor->Add(entity, error)
					|| !descriptor->Decode(*descriptor, entity, node["Properties"], error))
					return false;
			}
			catch (const std::exception& exception)
			{
				error = "Components[" + std::to_string(index) + "]: " + exception.what();
				return false;
			}
		}
		return true;
	}

}
