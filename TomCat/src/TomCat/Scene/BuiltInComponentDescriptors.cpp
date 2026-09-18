#include "tcpch.h"
#include "BuiltInComponentDescriptors.h"

#include "TomCat/Audio/AudioSceneRuntime.h"
#include "TomCat/Scene/Advanced2D.h"
#include "TomCat/Scene/Components.h"
#include "TomCat/Scene/SpriteAnimation.h"

#include <algorithm>
#include <cmath>
#include <limits>

#include <yaml-cpp/yaml.h>

namespace TomCat {

	namespace {

		bool IsFinite(float value) { return std::isfinite(value); }
		bool IsFinite(const glm::vec2& value)
		{
			return IsFinite(value.x) && IsFinite(value.y);
		}
		bool IsFinite(const glm::vec3& value)
		{
			return IsFinite(value.x) && IsFinite(value.y) && IsFinite(value.z);
		}
		bool IsFinite(const glm::vec4& value)
		{
			return IsFinite(value.x) && IsFinite(value.y)
				&& IsFinite(value.z) && IsFinite(value.w);
		}
		bool IsUnitColor(const glm::vec4& value)
		{
			return IsFinite(value) && value.x >= 0.0f && value.x <= 1.0f
				&& value.y >= 0.0f && value.y <= 1.0f
				&& value.z >= 0.0f && value.z <= 1.0f
				&& value.w >= 0.0f && value.w <= 1.0f;
		}

		PropertyDescriptor Property(uint64_t id, std::string stableName,
			PropertyKind kind, std::function<PropertyValue(Entity)> get,
			std::function<bool(Entity, const PropertyValue&, std::string&)> set,
			bool entityReference = false,
			std::optional<PropertyValue> defaultValue = std::nullopt)
		{
			PropertyDescriptor result;
			result.PropertyId = UUID(id);
			result.DisplayName = stableName;
			result.StableName = std::move(stableName);
			result.Kind = kind;
			result.Get = std::move(get);
			result.Set = std::move(set);
			result.DefaultValue = std::move(defaultValue);
			result.EntityReference = entityReference;
			return result;
		}

		void SetPropertyDefault(ComponentDescriptor& descriptor, uint64_t propertyId,
			PropertyValue value)
		{
			const auto property = std::find_if(descriptor.Properties.begin(),
				descriptor.Properties.end(), [propertyId](const PropertyDescriptor& item)
				{
					return static_cast<uint64_t>(item.PropertyId) == propertyId;
				});
			if (property != descriptor.Properties.end())
				property->DefaultValue = std::move(value);
		}

		template<typename Component>
		void ResetRuntime(Component&) {}
		void ResetRuntime(SpriteRenderer& component) { component.Sprite.reset(); }
		void ResetRuntime(SpriteAnimator& component) { SpriteAnimatorRuntime::Reset(component); }
		void ResetRuntime(AudioSource& component)
		{
			component.RuntimeVoice = 0;
			component.RuntimeClipHandle = AssetHandle(0);
			component.RuntimeAutoPlayEvaluated = false;
			component.RuntimeStreaming = false;
		}
		void ResetRuntime(Rigidbody2D& component) { component.RuntimeBody = nullptr; }
		void ResetRuntime(BoxCollider2D& component) { component.RuntimeFixture = nullptr; }
		void ResetRuntime(CircleCollider2D& component) { component.RuntimeFixture = nullptr; }
		void ResetRuntime(DistanceJoint2D& component) { component.RuntimeJoint = nullptr; }
		void ResetRuntime(ParticleSystem2D& component)
		{
			ParticleSystem2DRuntime::Reset(component);
		}

		template<typename Component>
		ComponentDescriptor BaseDescriptor(uint64_t typeId, const char* stableName,
			const char* displayName, bool removable = true)
		{
			ComponentDescriptor descriptor;
			descriptor.TypeId = UUID(typeId);
			descriptor.StableName = stableName;
			descriptor.DisplayName = displayName;
			descriptor.SchemaVersion = 1;
			descriptor.Removable = removable;
			descriptor.AddableInInspector = removable;
			descriptor.DecodeIntoExisting = true;
			descriptor.UseGenericInspector = false;
			descriptor.Has = [](Entity entity)
			{
				return entity && entity.HasComponent<Component>();
			};
			descriptor.Add = [display = std::string(displayName)](Entity entity,
				std::string& error)
			{
				if (!entity)
				{
					error = "Cannot add " + display + " to an invalid entity";
					return false;
				}
				if (!entity.HasComponent<Component>())
					entity.AddComponent<Component>();
				return true;
			};
			descriptor.Remove = [removable, display = std::string(displayName)](
				Entity entity, std::string& error)
			{
				if (!entity || !entity.HasComponent<Component>())
				{
					error = display + " is not present on the entity";
					return false;
				}
				if (!removable)
				{
					error = display + " is an intrinsic entity component";
					return false;
				}
				entity.RemoveComponent<Component>();
				return true;
			};
			descriptor.Copy = [display = std::string(displayName)](Entity source,
				Entity destination, std::string& error)
			{
				if (!source || !destination || !source.HasComponent<Component>())
				{
					error = "Cannot copy " + display + " from invalid entities";
					return false;
				}
				Component copy = source.GetComponent<Component>();
				ResetRuntime(copy);
				destination.AddOrReplaceComponent<Component>(std::move(copy));
				return true;
			};
		return descriptor;
		}

		template<typename Component>
		PropertyDescriptor BoolProperty(uint64_t id, const char* name,
			bool Component::* member)
		{
			PropertyDescriptor result = Property(id, name, PropertyKind::Bool,
				[member](Entity entity) -> PropertyValue
				{ return entity.GetComponent<Component>().*member; },
				[member](Entity entity, const PropertyValue& value, std::string&)
				{
					entity.GetComponent<Component>().*member = std::get<bool>(value);
					return true;
				});
			result.DefaultValue = Component{}.*member;
			return result;
		}

		template<typename Component>
		PropertyDescriptor IntProperty(uint64_t id, const char* name,
			int32_t Component::* member)
		{
			PropertyDescriptor result = Property(id, name, PropertyKind::Int32,
				[member](Entity entity) -> PropertyValue
				{ return entity.GetComponent<Component>().*member; },
				[member](Entity entity, const PropertyValue& value, std::string&)
				{
					entity.GetComponent<Component>().*member = std::get<int32_t>(value);
					return true;
				});
			result.DefaultValue = Component{}.*member;
			return result;
		}

		template<typename Component>
		PropertyDescriptor FloatProperty(uint64_t id, const char* name,
			float Component::* member, float minimum = -std::numeric_limits<float>::max(),
			float maximum = std::numeric_limits<float>::max(), bool minimumInclusive = true)
		{
			PropertyDescriptor result = Property(id, name, PropertyKind::Float,
				[member](Entity entity) -> PropertyValue
				{ return entity.GetComponent<Component>().*member; },
				[member, minimum, maximum, minimumInclusive, name](Entity entity,
					const PropertyValue& value, std::string& error)
				{
					const float decoded = std::get<float>(value);
					if (!IsFinite(decoded) || (minimumInclusive ? decoded < minimum
						: decoded <= minimum) || decoded > maximum)
					{
						error = std::string(name) + " is outside its valid range";
						return false;
					}
					entity.GetComponent<Component>().*member = decoded;
					return true;
				});
			result.DefaultValue = Component{}.*member;
			return result;
		}

		template<typename Component>
		PropertyDescriptor Vector2Property(uint64_t id, const char* name,
			glm::vec2 Component::* member, bool positive = false)
		{
			PropertyDescriptor result = Property(id, name, PropertyKind::Vector2,
				[member](Entity entity) -> PropertyValue
				{ return entity.GetComponent<Component>().*member; },
				[member, positive, name](Entity entity, const PropertyValue& value,
					std::string& error)
				{
					const glm::vec2 decoded = std::get<glm::vec2>(value);
					if (!IsFinite(decoded) || (positive
						&& (decoded.x <= 0.0f || decoded.y <= 0.0f)))
					{
						error = std::string(name) + " must contain valid finite values";
						return false;
					}
					entity.GetComponent<Component>().*member = decoded;
					return true;
				});
			result.DefaultValue = Component{}.*member;
			return result;
		}

		template<typename Component>
		PropertyDescriptor Vector3Property(uint64_t id, const char* name,
			glm::vec3 Component::* member)
		{
			PropertyDescriptor result = Property(id, name, PropertyKind::Vector3,
				[member](Entity entity) -> PropertyValue
				{ return entity.GetComponent<Component>().*member; },
				[member, name](Entity entity, const PropertyValue& value,
					std::string& error)
				{
					const glm::vec3 decoded = std::get<glm::vec3>(value);
					if (!IsFinite(decoded))
					{
						error = std::string(name) + " must contain finite values";
						return false;
					}
					entity.GetComponent<Component>().*member = decoded;
					return true;
				});
			result.DefaultValue = Component{}.*member;
			return result;
		}

		template<typename Component>
		PropertyDescriptor ColorProperty(uint64_t id, const char* name,
			glm::vec4 Component::* member)
		{
			PropertyDescriptor result = Property(id, name, PropertyKind::Vector4,
				[member](Entity entity) -> PropertyValue
				{ return entity.GetComponent<Component>().*member; },
				[member, name](Entity entity, const PropertyValue& value,
					std::string& error)
				{
					const glm::vec4 decoded = std::get<glm::vec4>(value);
					if (!IsUnitColor(decoded))
					{
						error = std::string(name) + " must contain RGBA values in [0, 1]";
						return false;
					}
					entity.GetComponent<Component>().*member = decoded;
					return true;
				});
			result.DefaultValue = Component{}.*member;
			return result;
		}

		PropertyDescriptor AssetProperty(uint64_t id, const char* name,
			AssetType type, std::function<AssetHandle&(Entity)> access)
		{
			PropertyDescriptor result = Property(id, name, PropertyKind::UInt64,
				[access](Entity entity) -> PropertyValue
				{ return static_cast<uint64_t>(access(entity)); },
				[access](Entity entity, const PropertyValue& value, std::string&)
				{
					access(entity) = AssetHandle(std::get<uint64_t>(value));
					return true;
				});
			result.DefaultValue = uint64_t(0);
		result.AssetReference = AssetPropertyMetadata{ { type }, true };
			return result;
		}

		void EmitVector(YAML::Emitter& output, const glm::vec2& value)
		{
			output << YAML::Flow << YAML::BeginSeq << value.x << value.y << YAML::EndSeq;
		}
		void EmitVector(YAML::Emitter& output, const glm::vec3& value)
		{
			output << YAML::Flow << YAML::BeginSeq << value.x << value.y << value.z
				<< YAML::EndSeq;
		}
		void EmitVector(YAML::Emitter& output, const glm::vec4& value)
		{
			output << YAML::Flow << YAML::BeginSeq << value.x << value.y << value.z
				<< value.w << YAML::EndSeq;
		}

		glm::vec2 ReadVector2(const YAML::Node& node)
		{
			if (!node.IsSequence() || node.size() != 2)
				throw std::runtime_error("expected Vector2");
			return { node[0].as<float>(), node[1].as<float>() };
		}
		glm::vec3 ReadVector3(const YAML::Node& node)
		{
			if (!node.IsSequence() || node.size() != 3)
				throw std::runtime_error("expected Vector3");
			return { node[0].as<float>(), node[1].as<float>(), node[2].as<float>() };
		}
		glm::vec4 ReadVector4(const YAML::Node& node)
		{
			if (!node.IsSequence() || node.size() != 4)
				throw std::runtime_error("expected Vector4");
			return { node[0].as<float>(), node[1].as<float>(),
				node[2].as<float>(), node[3].as<float>() };
		}

		void EmitScriptFieldValue(YAML::Emitter& output, const ScriptField& field)
		{
			if (!IsScriptFieldValueCompatible(field.Type, field.Value))
				throw std::runtime_error("script field value does not match its type");
			switch (field.Type)
			{
				case ScriptFieldType::Bool: output << std::get<bool>(field.Value); break;
				case ScriptFieldType::Int32: output << std::get<int32_t>(field.Value); break;
				case ScriptFieldType::Int64:
				case ScriptFieldType::Enum: output << std::get<int64_t>(field.Value); break;
				case ScriptFieldType::Float: output << std::get<float>(field.Value); break;
				case ScriptFieldType::Double: output << std::get<double>(field.Value); break;
				case ScriptFieldType::String: output << std::get<std::string>(field.Value); break;
				case ScriptFieldType::Vector2: EmitVector(output, std::get<glm::vec2>(field.Value)); break;
				case ScriptFieldType::Vector3: EmitVector(output, std::get<glm::vec3>(field.Value)); break;
				case ScriptFieldType::Vector4:
				case ScriptFieldType::Color: EmitVector(output, std::get<glm::vec4>(field.Value)); break;
				case ScriptFieldType::Entity:
				case ScriptFieldType::AssetRef: output << std::get<uint64_t>(field.Value); break;
			}
		}

		ScriptFieldValue ReadScriptFieldValue(const YAML::Node& node,
			ScriptFieldType type)
		{
			switch (type)
			{
				case ScriptFieldType::Bool: return node.as<bool>();
				case ScriptFieldType::Int32: return node.as<int32_t>();
				case ScriptFieldType::Int64:
				case ScriptFieldType::Enum: return node.as<int64_t>();
				case ScriptFieldType::Float: return node.as<float>();
				case ScriptFieldType::Double: return node.as<double>();
				case ScriptFieldType::String: return node.as<std::string>();
				case ScriptFieldType::Vector2: return ReadVector2(node);
				case ScriptFieldType::Vector3: return ReadVector3(node);
				case ScriptFieldType::Vector4:
				case ScriptFieldType::Color: return ReadVector4(node);
				case ScriptFieldType::Entity:
				case ScriptFieldType::AssetRef: return node.as<uint64_t>();
			}
			throw std::runtime_error("unknown script field type");
		}

		bool ReadEncodedPropertyMap(const ComponentDescriptor& descriptor,
			Entity entity, YAML::Node& propertyMap, std::string& error)
		{
			YAML::Emitter encoded;
			if (!descriptor.Encode(descriptor, entity, encoded, error))
				return false;
			if (!encoded.good())
			{
				error = descriptor.StableName + " property encoding failed";
				return false;
			}

			const YAML::Node payload = YAML::Load(encoded.c_str());
			if (payload.IsMap())
			{
				propertyMap = payload;
				return true;
			}
			if (!payload.IsSequence())
			{
				error = descriptor.StableName
					+ " encoded properties must be a map or sequence";
				return false;
			}

			propertyMap = YAML::Node(YAML::NodeType::Map);
			for (const YAML::Node& record : payload)
			{
				if (!record.IsMap() || !record["StableName"] || !record["Value"])
				{
					error = descriptor.StableName
						+ " encoded an invalid property record";
					return false;
				}
				const std::string stableName = record["StableName"].as<std::string>();
				if (stableName.empty() || propertyMap[stableName])
				{
					error = descriptor.StableName
						+ " encoded duplicate or empty property names";
					return false;
				}
				propertyMap[stableName] = record["Value"];
			}
			return true;
		}

		YAML::Node RequireEncodedProperty(const ComponentDescriptor& descriptor,
			const YAML::Node& propertyMap, const char* stableName)
		{
			const YAML::Node value = propertyMap[stableName];
			if (!value)
				throw std::runtime_error(descriptor.StableName
					+ " is missing encoded property '" + stableName + "'");
			return value;
		}

		ComponentDescriptor::LegacyEncodeFn LegacyFlatMap(std::string legacyKey,
			std::initializer_list<std::pair<std::string, std::string>> renames = {})
		{
			std::vector<std::pair<std::string, std::string>> ownedRenames(renames);
			return [legacyKey = std::move(legacyKey),
				ownedRenames = std::move(ownedRenames)](
					const ComponentDescriptor& descriptor, Entity entity,
					YAML::Emitter& output, std::string& error)
			{
				YAML::Node encodedProperties;
				if (!ReadEncodedPropertyMap(descriptor, entity, encodedProperties, error))
					return false;
				YAML::Node legacyProperties(YAML::NodeType::Map);
				for (const auto& property : encodedProperties)
				{
					const std::string stableName = property.first.as<std::string>();
					std::string legacyName = stableName;
					for (const auto& [source, destination] : ownedRenames)
					{
						if (stableName == source)
						{
							legacyName = destination;
							break;
						}
					}
					legacyProperties[legacyName] = property.second;
				}
				output << YAML::Key << legacyKey << YAML::Value << legacyProperties;
				return output.good();
			};
		}

		ComponentDescriptor::LegacyEncodeFn LegacyEntityMetadata()
		{
			return [](const ComponentDescriptor& descriptor, Entity entity,
				YAML::Emitter& output, std::string& error)
			{
				YAML::Node properties;
				if (!ReadEncodedPropertyMap(descriptor, entity, properties, error))
					return false;
				const int32_t icon = RequireEncodedProperty(descriptor, properties,
					"HierarchyIcon").as<int32_t>();
				const char* iconName = nullptr;
				switch (static_cast<EntityIconMode>(icon))
				{
					case EntityIconMode::Automatic: iconName = "Automatic"; break;
					case EntityIconMode::Entity: iconName = "Entity"; break;
					case EntityIconMode::Camera: iconName = "Camera"; break;
					case EntityIconMode::Sprite: iconName = "Sprite"; break;
					case EntityIconMode::Rigidbody2D: iconName = "Rigidbody2D"; break;
					case EntityIconMode::Collider2D: iconName = "Collider2D"; break;
					default: throw std::runtime_error("EntityMetadata.HierarchyIcon is invalid");
				}
				properties["HierarchyIcon"] = iconName;
				output << YAML::Key << "EntityMetadata" << YAML::Value << properties;
				return output.good();
			};
		}

		ComponentDescriptor::LegacyEncodeFn LegacyTransform()
		{
			return [](const ComponentDescriptor& descriptor, Entity entity,
				YAML::Emitter& output, std::string& error)
			{
				YAML::Node properties;
				if (!ReadEncodedPropertyMap(descriptor, entity, properties, error))
					return false;
				YAML::Node world(YAML::NodeType::Map);
				world["Translation"] = RequireEncodedProperty(descriptor, properties,
					"Translation");
				world["Rotation"] = RequireEncodedProperty(descriptor, properties,
					"Rotation");
				world["Scale"] = RequireEncodedProperty(descriptor, properties, "Scale");
				YAML::Node local(YAML::NodeType::Map);
				local["Translation"] = RequireEncodedProperty(descriptor, properties,
					"LocalTranslation");
				local["Rotation"] = RequireEncodedProperty(descriptor, properties,
					"LocalRotation");
				local["Scale"] = RequireEncodedProperty(descriptor, properties,
					"LocalScale");
				output << YAML::Key << "Transform" << YAML::Value << world;
				output << YAML::Key << "LocalTransform" << YAML::Value << local;
				return output.good();
			};
		}

		ComponentDescriptor::LegacyEncodeFn LegacyCamera()
		{
			return [](const ComponentDescriptor& descriptor, Entity entity,
				YAML::Emitter& output, std::string& error)
			{
				YAML::Node properties;
				if (!ReadEncodedPropertyMap(descriptor, entity, properties, error))
					return false;
				YAML::Node projection(YAML::NodeType::Map);
				projection["ProjectionType"] = RequireEncodedProperty(descriptor,
					properties, "ProjectionType");
				projection["PerspectiveFOV"] = RequireEncodedProperty(descriptor,
					properties, "PerspectiveVerticalFov");
				projection["PerspectiveNear"] = RequireEncodedProperty(descriptor,
					properties, "PerspectiveNearClip");
				projection["PerspectiveFar"] = RequireEncodedProperty(descriptor,
					properties, "PerspectiveFarClip");
				projection["OrthographicSize"] = RequireEncodedProperty(descriptor,
					properties, "OrthographicSize");
				projection["OrthographicNear"] = RequireEncodedProperty(descriptor,
					properties, "OrthographicNearClip");
				projection["OrthographicFar"] = RequireEncodedProperty(descriptor,
					properties, "OrthographicFarClip");
				YAML::Node legacy(YAML::NodeType::Map);
				legacy["Camera"] = projection;
				legacy["Primary"] = RequireEncodedProperty(descriptor, properties,
					"Primary");
				legacy["FixedAspectRatio"] = RequireEncodedProperty(descriptor,
					properties, "FixedAspectRatio");
				legacy["BackgroundColor"] = RequireEncodedProperty(descriptor,
					properties, "BackgroundColor");
				// Scene 9-11 Camera data had no independent Enabled field. Keep
				// ordinary enabled cameras readable by older readers, while retaining
				// a disabled value when this new state must be represented.
				if (!RequireEncodedProperty(descriptor, properties, "Enabled").as<bool>())
					legacy["Enabled"] = false;
				output << YAML::Key << "Camera" << YAML::Value << legacy;
				return output.good();
			};
		}

		ComponentDescriptor::LegacyEncodeFn LegacyRigidbody2D()
		{
			return [](const ComponentDescriptor& descriptor, Entity entity,
				YAML::Emitter& output, std::string& error)
			{
				YAML::Node properties;
				if (!ReadEncodedPropertyMap(descriptor, entity, properties, error))
					return false;
				const int32_t type = RequireEncodedProperty(descriptor, properties,
					"BodyType").as<int32_t>();
				const char* typeName = nullptr;
				switch (static_cast<Rigidbody2D::BodyType>(type))
				{
					case Rigidbody2D::BodyType::Static: typeName = "Static"; break;
					case Rigidbody2D::BodyType::Dynamic: typeName = "Dynamic"; break;
					case Rigidbody2D::BodyType::Kinematic: typeName = "Kinematic"; break;
					default: throw std::runtime_error("Rigidbody2D.BodyType is invalid");
				}
				properties["BodyType"] = typeName;
				output << YAML::Key << "Rigidbody2D" << YAML::Value << properties;
				return output.good();
			};
		}

		PropertyValue ReadLegacyPropertyValue(const YAML::Node& node,
			PropertyKind kind)
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
				case PropertyKind::Vector2: return ReadVector2(node);
				case PropertyKind::Vector3: return ReadVector3(node);
				case PropertyKind::Vector4: return ReadVector4(node);
			}
			throw std::runtime_error("unsupported property kind");
		}

		bool ValidateLegacyMap(const YAML::Node& node, const std::string& context,
			const std::vector<std::string>& required,
			const std::vector<std::string>& optional, std::string& error)
		{
			if (!node || !node.IsMap())
			{
				error = context + " must be a map";
				return false;
			}
			std::vector<std::string> seen;
			for (const auto& item : node)
			{
				if (!item.first.IsScalar())
				{
					error = context + " contains a non-scalar field name";
					return false;
				}
				const std::string name = item.first.as<std::string>();
				const bool known = std::find(required.begin(), required.end(), name)
					!= required.end() || std::find(optional.begin(), optional.end(), name)
					!= optional.end();
				if (!known || std::find(seen.begin(), seen.end(), name) != seen.end())
				{
					error = context + " contains unknown or duplicate field '" + name + "'";
					return false;
				}
				seen.push_back(name);
			}
			for (const std::string& name : required)
			{
				if (std::find(seen.begin(), seen.end(), name) == seen.end())
				{
					error = context + " is missing required field '" + name + "'";
					return false;
				}
			}
			return true;
		}

		bool ApplyLegacyPropertyMap(const ComponentDescriptor& descriptor,
			Entity entity, const YAML::Node& properties, std::string& error)
		{
			bool added = false;
			if (!descriptor.Has(entity))
			{
				if (!descriptor.Add(entity, error) || !descriptor.Has(entity))
				{
					if (error.empty())
						error = descriptor.StableName
							+ " provider did not add its component";
					return false;
				}
				added = true;
			}
			try
			{
				for (const PropertyDescriptor& property : descriptor.Properties)
				{
					const YAML::Node value = properties[property.StableName];
					if (!value)
						continue;
					if (!property.Set(entity,
						ReadLegacyPropertyValue(value, property.Kind), error))
					{
						if (error.empty())
							error = descriptor.StableName + "." + property.StableName
								+ " rejected its legacy value";
						throw std::runtime_error(error);
					}
				}
				return true;
			}
			catch (const std::exception& exception)
			{
				if (error.empty())
					error = descriptor.StableName + ": " + exception.what();
				if (added)
				{
					std::string rollbackError;
					descriptor.Remove(entity, rollbackError);
				}
				return false;
			}
		}

		std::string LegacyPropertyName(const std::string& stableName,
			const std::vector<std::pair<std::string, std::string>>& renames)
		{
			for (const auto& [source, destination] : renames)
				if (source == stableName)
					return destination;
			return stableName;
		}

		ComponentDescriptor::LegacyDecodeFn LegacyFlatDecode(std::string legacyKey,
			bool componentRequired = false,
			std::initializer_list<std::pair<std::string, std::string>> renames = {},
			std::initializer_list<std::string> optionalProperties = {})
		{
			std::vector<std::pair<std::string, std::string>> ownedRenames(renames);
			std::vector<std::string> ownedOptional(optionalProperties);
			return [legacyKey = std::move(legacyKey), componentRequired,
				ownedRenames = std::move(ownedRenames),
				ownedOptional = std::move(ownedOptional)](
					const ComponentDescriptor& descriptor, Entity entity,
					const YAML::Node& entityNode, std::string& error)
			{
				const YAML::Node legacy = entityNode[legacyKey];
				if (!legacy)
				{
					if (componentRequired)
					{
						error = descriptor.StableName
							+ " is missing required legacy field '" + legacyKey + "'";
						return false;
					}
					return true;
				}

				std::vector<std::string> required;
				std::vector<std::string> optional;
				for (const PropertyDescriptor& property : descriptor.Properties)
				{
					const std::string name = LegacyPropertyName(property.StableName,
						ownedRenames);
					if (std::find(ownedOptional.begin(), ownedOptional.end(),
						property.StableName) != ownedOptional.end())
						optional.push_back(name);
					else
						required.push_back(name);
				}
				if (!ValidateLegacyMap(legacy, descriptor.StableName, required,
					optional, error))
					return false;

				YAML::Node canonical(YAML::NodeType::Map);
				for (const PropertyDescriptor& property : descriptor.Properties)
				{
					const YAML::Node value = legacy[LegacyPropertyName(
						property.StableName, ownedRenames)];
					if (value)
						canonical[property.StableName] = value;
				}
				return ApplyLegacyPropertyMap(descriptor, entity, canonical, error);
			};
		}

		ComponentDescriptor::LegacyDecodeFn LegacyEntityMetadataDecode()
		{
			return [](const ComponentDescriptor& descriptor, Entity entity,
				const YAML::Node& entityNode, std::string& error)
			{
				const YAML::Node legacy = entityNode["EntityMetadata"];
				if (!ValidateLegacyMap(legacy, "EntityMetadata",
					{ "GameplayTag", "Layer", "HierarchyIcon" }, {}, error))
					return false;
				YAML::Node canonical(YAML::NodeType::Map);
				canonical["GameplayTag"] = legacy["GameplayTag"];
				canonical["Layer"] = legacy["Layer"];
				const std::string icon = legacy["HierarchyIcon"].as<std::string>();
				int32_t iconValue = -1;
				if (icon == "Automatic") iconValue = static_cast<int32_t>(EntityIconMode::Automatic);
				else if (icon == "Entity") iconValue = static_cast<int32_t>(EntityIconMode::Entity);
				else if (icon == "Camera") iconValue = static_cast<int32_t>(EntityIconMode::Camera);
				else if (icon == "Sprite") iconValue = static_cast<int32_t>(EntityIconMode::Sprite);
				else if (icon == "Rigidbody2D") iconValue = static_cast<int32_t>(EntityIconMode::Rigidbody2D);
				else if (icon == "Collider2D") iconValue = static_cast<int32_t>(EntityIconMode::Collider2D);
				else
				{
					error = "Unknown Entity icon mode '" + icon + "'";
					return false;
				}
				canonical["HierarchyIcon"] = iconValue;
				return ApplyLegacyPropertyMap(descriptor, entity, canonical, error);
			};
		}

		ComponentDescriptor::LegacyDecodeFn LegacyTransformDecode()
		{
			return [](const ComponentDescriptor& descriptor, Entity entity,
				const YAML::Node& entityNode, std::string& error)
			{
				const YAML::Node world = entityNode["Transform"];
				const YAML::Node local = entityNode["LocalTransform"];
				const std::vector<std::string> fields{
					"Translation", "Rotation", "Scale" };
				if (!ValidateLegacyMap(world, "Transform", fields, {}, error)
					|| !ValidateLegacyMap(local, "LocalTransform", fields, {}, error))
					return false;
				YAML::Node canonical(YAML::NodeType::Map);
				canonical["Translation"] = world["Translation"];
				canonical["Rotation"] = world["Rotation"];
				canonical["Scale"] = world["Scale"];
				canonical["LocalTranslation"] = local["Translation"];
				canonical["LocalRotation"] = local["Rotation"];
				canonical["LocalScale"] = local["Scale"];
				return ApplyLegacyPropertyMap(descriptor, entity, canonical, error);
			};
		}

		ComponentDescriptor::LegacyDecodeFn LegacyCameraDecode()
		{
			return [](const ComponentDescriptor& descriptor, Entity entity,
				const YAML::Node& entityNode, std::string& error)
			{
				const YAML::Node legacy = entityNode["Camera"];
				if (!legacy)
					return true;
				if (!ValidateLegacyMap(legacy, "Camera",
					{ "Camera", "Primary", "FixedAspectRatio", "BackgroundColor" },
					{ "Enabled" }, error))
					return false;
				const YAML::Node projection = legacy["Camera"];
				if (!ValidateLegacyMap(projection, "Camera.Camera",
					{ "ProjectionType", "PerspectiveFOV", "PerspectiveNear",
						"PerspectiveFar", "OrthographicSize", "OrthographicNear",
						"OrthographicFar" }, {}, error))
					return false;
				YAML::Node canonical(YAML::NodeType::Map);
				canonical["Primary"] = legacy["Primary"];
				canonical["FixedAspectRatio"] = legacy["FixedAspectRatio"];
				canonical["BackgroundColor"] = legacy["BackgroundColor"];
				if (legacy["Enabled"])
					canonical["Enabled"] = legacy["Enabled"];
				canonical["ProjectionType"] = projection["ProjectionType"];
				canonical["PerspectiveVerticalFov"] = projection["PerspectiveFOV"];
				canonical["PerspectiveNearClip"] = projection["PerspectiveNear"];
				canonical["PerspectiveFarClip"] = projection["PerspectiveFar"];
				canonical["OrthographicSize"] = projection["OrthographicSize"];
				canonical["OrthographicNearClip"] = projection["OrthographicNear"];
				canonical["OrthographicFarClip"] = projection["OrthographicFar"];
				return ApplyLegacyPropertyMap(descriptor, entity, canonical, error);
			};
		}

		ComponentDescriptor::LegacyDecodeFn LegacyStructuredDecode(
			std::string legacyKey, std::vector<std::string> required,
			std::vector<std::string> optional = {})
		{
			return [legacyKey = std::move(legacyKey), required = std::move(required),
				optional = std::move(optional)](const ComponentDescriptor& descriptor,
				Entity entity, const YAML::Node& entityNode, std::string& error)
			{
				const YAML::Node legacy = entityNode[legacyKey];
				if (!legacy)
					return true;
				if (!ValidateLegacyMap(legacy, descriptor.StableName, required,
					optional, error))
					return false;
				bool added = false;
				if (!descriptor.Has(entity))
				{
					if (!descriptor.Add(entity, error) || !descriptor.Has(entity))
						return false;
					added = true;
				}
				if (descriptor.Decode(descriptor, entity, legacy, error))
					return true;
				if (added)
				{
					std::string rollbackError;
					descriptor.Remove(entity, rollbackError);
				}
				return false;
			};
		}

		ComponentDescriptor::LegacyDecodeFn LegacySpriteAnimatorDecode()
		{
			auto structured = LegacyStructuredDecode("SpriteAnimator",
				{ "Enabled", "PlayOnStart", "InitialClip", "Speed", "Clips" },
				{ "InitialState", "Parameters", "States", "Transitions" });
			return [structured = std::move(structured)](
				const ComponentDescriptor& descriptor, Entity entity,
				const YAML::Node& entityNode, std::string& error)
			{
				if (!entityNode["SpriteAnimator"])
					return true;
				YAML::Node normalized = YAML::Load(YAML::Dump(entityNode));
				YAML::Node animator = normalized["SpriteAnimator"];
				if (!animator["InitialState"]) animator["InitialState"] = "";
				if (!animator["Parameters"])
					animator["Parameters"] = YAML::Node(YAML::NodeType::Sequence);
				if (!animator["States"])
					animator["States"] = YAML::Node(YAML::NodeType::Sequence);
				if (!animator["Transitions"])
					animator["Transitions"] = YAML::Node(YAML::NodeType::Sequence);
				return structured(descriptor, entity, normalized, error);
			};
		}

		ComponentDescriptor::LegacyDecodeFn LegacyRigidbody2DDecode()
		{
			return [](const ComponentDescriptor& descriptor, Entity entity,
				const YAML::Node& entityNode, std::string& error)
			{
				const YAML::Node legacy = entityNode["Rigidbody2D"];
				if (!legacy)
					return true;
				if (!ValidateLegacyMap(legacy, "Rigidbody2D",
					{ "Enabled", "BodyType", "FixedRotation" }, {}, error))
					return false;
				const std::string type = legacy["BodyType"].as<std::string>();
				int32_t typeValue = -1;
				if (type == "Static") typeValue = static_cast<int32_t>(Rigidbody2D::BodyType::Static);
				else if (type == "Dynamic") typeValue = static_cast<int32_t>(Rigidbody2D::BodyType::Dynamic);
				else if (type == "Kinematic") typeValue = static_cast<int32_t>(Rigidbody2D::BodyType::Kinematic);
				else
				{
					error = "Unknown Rigidbody2D body type '" + type + "'";
					return false;
				}
				YAML::Node canonical(YAML::NodeType::Map);
				canonical["Enabled"] = legacy["Enabled"];
				canonical["BodyType"] = typeValue;
				canonical["FixedRotation"] = legacy["FixedRotation"];
				return ApplyLegacyPropertyMap(descriptor, entity, canonical, error);
			};
		}

		ComponentDescriptor MakeIDDescriptor()
		{
			auto descriptor = BaseDescriptor<ID>(ComponentIds::ID,
				"TomCat.ID", "ID", false);
			descriptor.InspectorVisible = false;
			descriptor.Copy = [](Entity source, Entity destination, std::string& error)
			{
				if (!source || !destination || !source.HasComponent<ID>()
					|| !destination.HasComponent<ID>())
				{
					error = "Cannot preserve entity identity while copying invalid entities";
					return false;
				}
				return true;
			};
			descriptor.Properties.push_back(Property(ComponentIds::IDProperties::Value,
				"Value", PropertyKind::UInt64,
				[](Entity entity) -> PropertyValue
				{ return static_cast<uint64_t>(entity.GetComponent<ID>().id); },
				[](Entity entity, const PropertyValue& value, std::string& error)
				{
					if (static_cast<uint64_t>(entity.GetComponent<ID>().id)
						!= std::get<uint64_t>(value))
					{
						error = "ID.Value does not match the enclosing entity";
						return false;
					}
					return true;
				}));
			return descriptor;
		}

		ComponentDescriptor MakeTagDescriptor()
		{
			auto descriptor = BaseDescriptor<Tag>(ComponentIds::Tag,
				"TomCat.Tag", "Tag", false);
			descriptor.InspectorVisible = false;
			descriptor.EncodeLegacyFields = LegacyFlatMap("Tag", { { "Name", "Tag" } });
			descriptor.DecodeLegacyFields = LegacyFlatDecode("Tag", true,
				{ { "Name", "Tag" } });
			descriptor.Copy = [](Entity source, Entity destination, std::string& error)
			{
				if (!source || !destination || !source.HasComponent<Tag>()
					|| !destination.HasComponent<Tag>())
				{
					error = "Cannot copy Tag from invalid entities";
					return false;
				}
				destination.GetComponent<Tag>().ActiveSelf =
					source.GetComponent<Tag>().ActiveSelf;
				return true;
			};
			auto activeSelf = BoolProperty<Tag>(
				ComponentIds::TagProperties::ActiveSelf, "Visible", &Tag::ActiveSelf);
			activeSelf.DisplayName = "Active Self";
			 descriptor.Properties = {
				Property(ComponentIds::TagProperties::Name, "Name", PropertyKind::String,
					[](Entity entity) -> PropertyValue { return entity.GetComponent<Tag>()._Tag; },
					[](Entity entity, const PropertyValue& value, std::string& error)
					{
						const std::string& name = std::get<std::string>(value);
						if (name.empty()) { error = "Tag.Name cannot be empty"; return false; }
						entity.GetComponent<Tag>()._Tag = name;
						return true;
					}),
				std::move(activeSelf)
			};
			return descriptor;
		}

		ComponentDescriptor MakeEditorVisibilityDescriptor()
		{
			auto descriptor = BaseDescriptor<EditorVisibility>(
				ComponentIds::EditorVisibility, "TomCat.EditorVisibility",
				"Editor Visibility");
			descriptor.InspectorVisible = false;
			descriptor.UseGenericInspector = false;
			descriptor.AddableInInspector = false;
			descriptor.Properties = {
				BoolProperty<EditorVisibility>(
					ComponentIds::EditorVisibilityProperties::Hidden,
					"Hidden", &EditorVisibility::Hidden)
			};
			return descriptor;
		}

		ComponentDescriptor MakeEntityMetadataDescriptor()
		{
			auto descriptor = BaseDescriptor<EntityMetadata>(ComponentIds::EntityMetadata,
				"TomCat.EntityMetadata", "Entity Metadata", false);
			descriptor.InspectorVisible = false;
			descriptor.EncodeLegacyFields = LegacyEntityMetadata();
			descriptor.DecodeLegacyFields = LegacyEntityMetadataDecode();
			descriptor.Properties = {
				Property(ComponentIds::EntityMetadataProperties::GameplayTag,
					"GameplayTag", PropertyKind::String,
					[](Entity entity) -> PropertyValue
					{ return entity.GetComponent<EntityMetadata>().GameplayTag; },
					[](Entity entity, const PropertyValue& value, std::string& error)
					{
						const std::string& tag = std::get<std::string>(value);
						if (tag.empty()) { error = "GameplayTag cannot be empty"; return false; }
						entity.GetComponent<EntityMetadata>().GameplayTag = tag;
						return true;
					}),
				Property(ComponentIds::EntityMetadataProperties::Layer, "Layer",
					PropertyKind::UInt32,
					[](Entity entity) -> PropertyValue
					{ return static_cast<uint32_t>(entity.GetComponent<EntityMetadata>().Layer); },
					[](Entity entity, const PropertyValue& value, std::string& error)
					{
						const uint32_t layer = std::get<uint32_t>(value);
						if (layer >= Physics2DLayerCount) { error = "Layer must be in [0, 15]"; return false; }
						entity.GetComponent<EntityMetadata>().Layer = static_cast<uint8_t>(layer);
						return true;
					}),
				Property(ComponentIds::EntityMetadataProperties::HierarchyIcon,
					"HierarchyIcon", PropertyKind::Int32,
					[](Entity entity) -> PropertyValue
					{ return static_cast<int32_t>(entity.GetComponent<EntityMetadata>().HierarchyIcon); },
					[](Entity entity, const PropertyValue& value, std::string& error)
					{
						const int32_t icon = std::get<int32_t>(value);
						if (icon < 0 || icon > static_cast<int32_t>(EntityIconMode::Collider2D))
						{ error = "HierarchyIcon is invalid"; return false; }
						entity.GetComponent<EntityMetadata>().HierarchyIcon = static_cast<EntityIconMode>(icon);
						return true;
					})
			};
			return descriptor;
		}

		ComponentDescriptor MakeTransformDescriptor()
		{
			auto descriptor = BaseDescriptor<Transform>(ComponentIds::Transform,
				"TomCat.Transform", "Transform", false);
			descriptor.ScriptAccessible = true;
			descriptor.EncodeLegacyFields = LegacyTransform();
			descriptor.DecodeLegacyFields = LegacyTransformDecode();
			descriptor.Properties = {
				Vector3Property<Transform>(ComponentIds::TransformProperties::Translation,
					"Translation", &Transform::_Translation),
				Vector3Property<Transform>(ComponentIds::TransformProperties::Rotation,
					"Rotation", &Transform::_Rotation),
				Vector3Property<Transform>(ComponentIds::TransformProperties::Scale,
					"Scale", &Transform::_Scale),
				Vector3Property<Transform>(ComponentIds::TransformProperties::LocalTranslation,
					"LocalTranslation", &Transform::_LocalTranslation),
				Vector3Property<Transform>(ComponentIds::TransformProperties::LocalRotation,
					"LocalRotation", &Transform::_LocalRotation),
				Vector3Property<Transform>(ComponentIds::TransformProperties::LocalScale,
					"LocalScale", &Transform::_LocalScale)
			};
			return descriptor;
		}

		ComponentDescriptor MakeCameraDescriptor()
		{
			auto descriptor = BaseDescriptor<C_Camera>(ComponentIds::Camera,
				"TomCat.Camera", "Camera");
			descriptor.ScriptAccessible = true;
			descriptor.SchemaVersion = 2;
			descriptor.Migrations.push_back({ 1, 2,
				[](YAML::Node& record, std::string& error)
				{
					YAML::Node properties = record["Properties"];
					if (!properties || !properties.IsSequence())
					{
						error = "TomCat.Camera v1 properties must be a sequence";
						return false;
					}
					for (const YAML::Node& property : properties)
					{
						if (!property["PropertyId"] || !property["StableName"])
						{
							error = "TomCat.Camera v1 property identity is invalid";
							return false;
						}
						if (property["PropertyId"].as<uint64_t>()
								== ComponentIds::CameraProperties::Enabled
							|| property["StableName"].as<std::string>() == "Enabled")
						{
							error = "TomCat.Camera v1 unexpectedly contains Enabled";
							return false;
						}
					}
					YAML::Node enabled(YAML::NodeType::Map);
					enabled["PropertyId"] = ComponentIds::CameraProperties::Enabled;
					enabled["StableName"] = "Enabled";
					enabled["Value"] = true;
					properties.push_back(enabled);
					return true;
				} });
			descriptor.EncodeLegacyFields = LegacyCamera();
			descriptor.DecodeLegacyFields = LegacyCameraDecode();
			descriptor.Add = [](Entity entity, std::string& error)
			{
				if (!entity)
				{
					error = "Cannot add Camera to an invalid entity";
					return false;
				}
				if (entity.HasComponent<C_Camera>())
					return true;
				Scene* scene = entity.GetScene();
				const bool alreadyHasPrimary = scene
					&& scene->HasAuthoredPrimaryCamera();
				auto& camera = entity.AddComponent<C_Camera>();
				camera.Primary = !alreadyHasPrimary;
				return true;
			};
			descriptor.Properties = {
				Property(ComponentIds::CameraProperties::Primary, "Primary",
					PropertyKind::Bool,
					[](Entity entity) -> PropertyValue
					{ return entity.GetComponent<C_Camera>().Primary; },
					[](Entity entity, const PropertyValue& value, std::string& error)
					{
						Scene* scene = entity.GetScene();
						if (!scene || !scene->SetCameraPrimary(entity,
							std::get<bool>(value)))
						{
							error = "Camera.Primary target is invalid";
							return false;
						}
						return true;
					}),
				BoolProperty<C_Camera>(ComponentIds::CameraProperties::FixedAspectRatio,
					"FixedAspectRatio", &C_Camera::FixedAspectRatio),
				ColorProperty<C_Camera>(ComponentIds::CameraProperties::BackgroundColor,
					"BackgroundColor", &C_Camera::BackgroundColor),
				Property(ComponentIds::CameraProperties::ProjectionType, "ProjectionType",
					PropertyKind::Int32,
					[](Entity entity) -> PropertyValue
					{ return static_cast<int32_t>(entity.GetComponent<C_Camera>()._Camera.GetProjectionType()); },
					[](Entity entity, const PropertyValue& value, std::string& error)
					{
						const int32_t type = std::get<int32_t>(value);
						if (type < 0 || type > 1 || !entity.GetComponent<C_Camera>()._Camera.SetProjectionType(
							static_cast<SceneCamera::ProjectionType>(type)))
						{ error = "ProjectionType is invalid"; return false; }
						return true;
					}),
				Property(ComponentIds::CameraProperties::OrthographicSize, "OrthographicSize", PropertyKind::Float,
					[](Entity e) -> PropertyValue { return e.GetComponent<C_Camera>()._Camera.GetOrthographicSize(); },
					[](Entity e, const PropertyValue& v, std::string& error) { if (!e.GetComponent<C_Camera>()._Camera.SetOrthographicSize(std::get<float>(v))) { error = "OrthographicSize is invalid"; return false; } return true; }),
				Property(ComponentIds::CameraProperties::OrthographicNear, "OrthographicNearClip", PropertyKind::Float,
					[](Entity e) -> PropertyValue { return e.GetComponent<C_Camera>()._Camera.GetOrthographicNearClip(); },
					[](Entity e, const PropertyValue& v, std::string& error) { if (!e.GetComponent<C_Camera>()._Camera.SetOrthographicNearClip(std::get<float>(v))) { error = "OrthographicNearClip is invalid"; return false; } return true; }),
				Property(ComponentIds::CameraProperties::OrthographicFar, "OrthographicFarClip", PropertyKind::Float,
					[](Entity e) -> PropertyValue { return e.GetComponent<C_Camera>()._Camera.GetOrthographicFarClip(); },
					[](Entity e, const PropertyValue& v, std::string& error) { if (!e.GetComponent<C_Camera>()._Camera.SetOrthographicFarClip(std::get<float>(v))) { error = "OrthographicFarClip is invalid"; return false; } return true; }),
				Property(ComponentIds::CameraProperties::PerspectiveFov, "PerspectiveVerticalFov", PropertyKind::Float,
					[](Entity e) -> PropertyValue { return e.GetComponent<C_Camera>()._Camera.GetPerspectiveVerticalFOV(); },
					[](Entity e, const PropertyValue& v, std::string& error) { if (!e.GetComponent<C_Camera>()._Camera.SetPerspectiveVerticalFOV(std::get<float>(v))) { error = "PerspectiveVerticalFov is invalid"; return false; } return true; }),
				Property(ComponentIds::CameraProperties::PerspectiveNear, "PerspectiveNearClip", PropertyKind::Float,
					[](Entity e) -> PropertyValue { return e.GetComponent<C_Camera>()._Camera.GetPerspectiveNearClip(); },
					[](Entity e, const PropertyValue& v, std::string& error) { if (!e.GetComponent<C_Camera>()._Camera.SetPerspectiveNearClip(std::get<float>(v))) { error = "PerspectiveNearClip is invalid"; return false; } return true; }),
				Property(ComponentIds::CameraProperties::PerspectiveFar, "PerspectiveFarClip", PropertyKind::Float,
					[](Entity e) -> PropertyValue { return e.GetComponent<C_Camera>()._Camera.GetPerspectiveFarClip(); },
					[](Entity e, const PropertyValue& v, std::string& error) { if (!e.GetComponent<C_Camera>()._Camera.SetPerspectiveFarClip(std::get<float>(v))) { error = "PerspectiveFarClip is invalid"; return false; } return true; }),
				BoolProperty<C_Camera>(ComponentIds::CameraProperties::Enabled,
					"Enabled", &C_Camera::Enabled)
			};
			const C_Camera cameraDefaults;
			SetPropertyDefault(descriptor, ComponentIds::CameraProperties::Primary,
				cameraDefaults.Primary);
			SetPropertyDefault(descriptor, ComponentIds::CameraProperties::ProjectionType,
				static_cast<int32_t>(cameraDefaults._Camera.GetProjectionType()));
			SetPropertyDefault(descriptor, ComponentIds::CameraProperties::OrthographicSize,
				cameraDefaults._Camera.GetOrthographicSize());
			SetPropertyDefault(descriptor, ComponentIds::CameraProperties::OrthographicNear,
				cameraDefaults._Camera.GetOrthographicNearClip());
			SetPropertyDefault(descriptor, ComponentIds::CameraProperties::OrthographicFar,
				cameraDefaults._Camera.GetOrthographicFarClip());
			SetPropertyDefault(descriptor, ComponentIds::CameraProperties::PerspectiveFov,
				cameraDefaults._Camera.GetPerspectiveVerticalFOV());
			SetPropertyDefault(descriptor, ComponentIds::CameraProperties::PerspectiveNear,
				cameraDefaults._Camera.GetPerspectiveNearClip());
			SetPropertyDefault(descriptor, ComponentIds::CameraProperties::PerspectiveFar,
				cameraDefaults._Camera.GetPerspectiveFarClip());
			return descriptor;
		}

		ComponentDescriptor MakeSpriteRendererDescriptor()
		{
			auto descriptor = BaseDescriptor<SpriteRenderer>(ComponentIds::SpriteRenderer,
				"TomCat.SpriteRenderer", "Sprite Renderer");
			descriptor.ScriptAccessible = true;
			descriptor.EncodeLegacyFields = LegacyFlatMap("SpriteRenderer",
				{ { "Sprite", "SpriteHandle" } });
			descriptor.DecodeLegacyFields = LegacyFlatDecode("SpriteRenderer", false,
				{ { "Sprite", "SpriteHandle" } },
				{ "SortingLayer", "OrderInLayer" });
			auto sprite = AssetProperty(ComponentIds::SpriteRendererProperties::Sprite,
				"Sprite", AssetType::Texture2D,
				[](Entity entity) -> AssetHandle& { return entity.GetComponent<SpriteRenderer>().SpriteHandle; });
			auto originalSet = sprite.Set;
			sprite.Set = [originalSet](Entity entity, const PropertyValue& value, std::string& error)
			{
				const AssetHandle before = entity.GetComponent<SpriteRenderer>().SpriteHandle;
				if (!originalSet(entity, value, error)) return false;
				if (before != entity.GetComponent<SpriteRenderer>().SpriteHandle)
					entity.GetComponent<SpriteRenderer>().Sprite.reset();
				return true;
			};
			descriptor.Properties = {
				BoolProperty<SpriteRenderer>(ComponentIds::SpriteRendererProperties::Enabled,
					"Enabled", &SpriteRenderer::Enabled),
				ColorProperty<SpriteRenderer>(ComponentIds::SpriteRendererProperties::Color,
					"Color", &SpriteRenderer::_Color),
				std::move(sprite),
				FloatProperty<SpriteRenderer>(ComponentIds::SpriteRendererProperties::TilingFactor,
					"TilingFactor", &SpriteRenderer::TilingFactor, 0.0f),
				IntProperty<SpriteRenderer>(ComponentIds::SpriteRendererProperties::SortingLayer,
					"SortingLayer", &SpriteRenderer::SortingLayer),
				IntProperty<SpriteRenderer>(ComponentIds::SpriteRendererProperties::OrderInLayer,
					"OrderInLayer", &SpriteRenderer::OrderInLayer)
			};
			return descriptor;
		}

		bool EncodeSpriteAnimator(const ComponentDescriptor&, Entity entity,
			YAML::Emitter& output, std::string& error)
		{
			try
			{
				const SpriteAnimator& animator = entity.GetComponent<SpriteAnimator>();
				output << YAML::BeginMap;
				output << YAML::Key << "Enabled" << YAML::Value << animator.Enabled;
				output << YAML::Key << "PlayOnStart" << YAML::Value << animator.PlayOnStart;
				output << YAML::Key << "InitialClip" << YAML::Value << animator.InitialClip;
				output << YAML::Key << "Speed" << YAML::Value << animator.Speed;
				output << YAML::Key << "Clips" << YAML::Value << YAML::BeginSeq;
				for (const SpriteAnimationClip& clip : animator.Clips)
				{
					output << YAML::BeginMap << YAML::Key << "Name" << YAML::Value << clip.Name
						<< YAML::Key << "Loop" << YAML::Value << clip.Loop
						<< YAML::Key << "Frames" << YAML::Value << YAML::BeginSeq;
					for (const SpriteAnimationFrame& frame : clip.Frames)
						output << YAML::BeginMap << YAML::Key << "SpriteHandle" << YAML::Value
							<< static_cast<uint64_t>(frame.SpriteHandle)
							<< YAML::Key << "DurationSeconds" << YAML::Value
							<< frame.DurationSeconds << YAML::EndMap;
					output << YAML::EndSeq << YAML::EndMap;
				}
				output << YAML::EndSeq;
				output << YAML::Key << "InitialState" << YAML::Value << animator.InitialState;
				output << YAML::Key << "Parameters" << YAML::Value << YAML::BeginSeq;
				for (const AnimatorParameter& parameter : animator.Parameters)
					output << YAML::BeginMap << YAML::Key << "Name" << YAML::Value << parameter.Name
						<< YAML::Key << "Type" << YAML::Value << static_cast<uint32_t>(parameter.Type)
						<< YAML::Key << "BoolValue" << YAML::Value << parameter.BoolValue
						<< YAML::Key << "IntValue" << YAML::Value << parameter.IntValue
						<< YAML::Key << "FloatValue" << YAML::Value << parameter.FloatValue
						<< YAML::EndMap;
				output << YAML::EndSeq;
				output << YAML::Key << "States" << YAML::Value << YAML::BeginSeq;
				for (const AnimatorState& state : animator.States)
					output << YAML::BeginMap << YAML::Key << "Name" << YAML::Value << state.Name
						<< YAML::Key << "Clip" << YAML::Value << state.Clip
						<< YAML::Key << "Speed" << YAML::Value << state.Speed << YAML::EndMap;
				output << YAML::EndSeq;
				output << YAML::Key << "Transitions" << YAML::Value << YAML::BeginSeq;
				for (const AnimatorTransition& transition : animator.Transitions)
				{
					output << YAML::BeginMap
						<< YAML::Key << "FromState" << YAML::Value << transition.FromState
						<< YAML::Key << "ToState" << YAML::Value << transition.ToState
						<< YAML::Key << "AnyState" << YAML::Value << transition.AnyState
						<< YAML::Key << "ExitTime" << YAML::Value << transition.ExitTime
						<< YAML::Key << "Conditions" << YAML::Value << YAML::BeginSeq;
					for (const AnimatorCondition& condition : transition.Conditions)
						output << YAML::BeginMap
							<< YAML::Key << "Parameter" << YAML::Value << condition.Parameter
							<< YAML::Key << "Mode" << YAML::Value << static_cast<uint32_t>(condition.Mode)
							<< YAML::Key << "Threshold" << YAML::Value << condition.Threshold
							<< YAML::EndMap;
					output << YAML::EndSeq << YAML::EndMap;
				}
				output << YAML::EndSeq << YAML::EndMap;
				return output.good();
			}
			catch (const std::exception& exception)
			{
				error = exception.what();
				return false;
			}
		}

		bool DecodeSpriteAnimator(const ComponentDescriptor&, Entity entity,
			const YAML::Node& node, std::string& error)
		{
			try
			{
				if (!ValidateLegacyMap(node, "SpriteAnimator.Properties",
					{ "Enabled", "PlayOnStart", "InitialClip", "Speed", "Clips",
						"InitialState", "Parameters", "States", "Transitions" },
					{}, error))
					return false;
				SpriteAnimator animator;
				animator.Enabled = node["Enabled"].as<bool>();
				animator.PlayOnStart = node["PlayOnStart"].as<bool>();
				animator.InitialClip = node["InitialClip"].as<std::string>();
				animator.Speed = node["Speed"].as<float>();
				if (!IsFinite(animator.Speed) || animator.Speed < 0.0f)
					throw std::runtime_error("SpriteAnimator.Speed is invalid");
				const YAML::Node clips = node["Clips"];
				if (!clips.IsSequence() || clips.size() == 0)
					throw std::runtime_error("SpriteAnimator.Clips must be a nonempty sequence");
				std::vector<std::string> clipNames;
				for (const YAML::Node& clipNode : clips)
				{
					if (!ValidateLegacyMap(clipNode, "SpriteAnimator clip",
						{ "Name", "Loop", "Frames" }, {}, error))
						return false;
					SpriteAnimationClip clip;
					clip.Name = clipNode["Name"].as<std::string>();
					clip.Loop = clipNode["Loop"].as<bool>();
					if (clip.Name.empty() || std::find(clipNames.begin(), clipNames.end(), clip.Name) != clipNames.end())
						throw std::runtime_error("SpriteAnimator clip names must be nonempty and unique");
					clipNames.push_back(clip.Name);
					const YAML::Node frames = clipNode["Frames"];
					if (!frames.IsSequence() || frames.size() == 0)
						throw std::runtime_error("SpriteAnimator clip frames must be nonempty");
					for (const YAML::Node& frameNode : frames)
					{
						if (!ValidateLegacyMap(frameNode, "SpriteAnimator frame",
							{ "SpriteHandle", "DurationSeconds" }, {}, error))
							return false;
						SpriteAnimationFrame frame;
						frame.SpriteHandle = AssetHandle(frameNode["SpriteHandle"].as<uint64_t>());
						frame.DurationSeconds = frameNode["DurationSeconds"].as<float>();
						if (!IsFinite(frame.DurationSeconds) || frame.DurationSeconds <= 0.0f)
							throw std::runtime_error("SpriteAnimator frame duration is invalid");
						clip.Frames.push_back(frame);
					}
					animator.Clips.push_back(std::move(clip));
				}
				if (!animator.InitialClip.empty()
					&& std::find(clipNames.begin(), clipNames.end(), animator.InitialClip) == clipNames.end())
					throw std::runtime_error("SpriteAnimator.InitialClip is missing");
				animator.InitialState = node["InitialState"].as<std::string>();
				const YAML::Node parameters = node["Parameters"];
				if (!parameters.IsSequence()) throw std::runtime_error("SpriteAnimator.Parameters must be a sequence");
				std::vector<std::string> parameterNames;
				for (const YAML::Node& parameterNode : parameters)
				{
					if (!ValidateLegacyMap(parameterNode, "SpriteAnimator parameter",
						{ "Name", "Type", "BoolValue", "IntValue", "FloatValue" },
						{}, error))
						return false;
					AnimatorParameter parameter;
					parameter.Name = parameterNode["Name"].as<std::string>();
					const uint32_t type = parameterNode["Type"].as<uint32_t>();
					if (parameter.Name.empty() || type > static_cast<uint32_t>(AnimatorParameterType::Trigger)
						|| std::find(parameterNames.begin(), parameterNames.end(), parameter.Name) != parameterNames.end())
						throw std::runtime_error("SpriteAnimator parameter is invalid");
					parameterNames.push_back(parameter.Name);
					parameter.Type = static_cast<AnimatorParameterType>(type);
					parameter.BoolValue = parameterNode["BoolValue"].as<bool>();
					parameter.IntValue = parameterNode["IntValue"].as<int32_t>();
					parameter.FloatValue = parameterNode["FloatValue"].as<float>();
					if (!IsFinite(parameter.FloatValue)) throw std::runtime_error("Animator parameter value is invalid");
					animator.Parameters.push_back(std::move(parameter));
				}
				const YAML::Node states = node["States"];
				if (!states.IsSequence()) throw std::runtime_error("SpriteAnimator.States must be a sequence");
				std::vector<std::string> stateNames;
				for (const YAML::Node& stateNode : states)
				{
					if (!ValidateLegacyMap(stateNode, "SpriteAnimator state",
						{ "Name", "Clip", "Speed" }, {}, error))
						return false;
					AnimatorState state{ stateNode["Name"].as<std::string>(),
						stateNode["Clip"].as<std::string>(), stateNode["Speed"].as<float>() };
					if (state.Name.empty() || !IsFinite(state.Speed) || state.Speed <= 0.0f
						|| std::find(stateNames.begin(), stateNames.end(), state.Name) != stateNames.end()
						|| std::find(clipNames.begin(), clipNames.end(), state.Clip) == clipNames.end())
						throw std::runtime_error("SpriteAnimator state is invalid");
					stateNames.push_back(state.Name);
					animator.States.push_back(std::move(state));
				}
				if (!animator.InitialState.empty()
					&& std::find(stateNames.begin(), stateNames.end(), animator.InitialState) == stateNames.end())
					throw std::runtime_error("SpriteAnimator.InitialState is missing");
				const YAML::Node transitions = node["Transitions"];
				if (!transitions.IsSequence()) throw std::runtime_error("SpriteAnimator.Transitions must be a sequence");
				for (const YAML::Node& transitionNode : transitions)
				{
					if (!ValidateLegacyMap(transitionNode, "SpriteAnimator transition",
						{ "FromState", "ToState", "AnyState", "ExitTime",
							"Conditions" }, {}, error))
						return false;
					AnimatorTransition transition;
					transition.FromState = transitionNode["FromState"].as<std::string>();
					transition.ToState = transitionNode["ToState"].as<std::string>();
					transition.AnyState = transitionNode["AnyState"].as<bool>();
					transition.ExitTime = transitionNode["ExitTime"].as<float>();
					if (!IsFinite(transition.ExitTime) || transition.ExitTime < -1.0f || transition.ExitTime > 1.0f)
						throw std::runtime_error("SpriteAnimator transition exit time is invalid");
					const YAML::Node conditions = transitionNode["Conditions"];
					if (!conditions.IsSequence()) throw std::runtime_error("Animator conditions must be a sequence");
					for (const YAML::Node& conditionNode : conditions)
					{
						if (!ValidateLegacyMap(conditionNode,
							"SpriteAnimator condition",
							{ "Parameter", "Mode", "Threshold" }, {}, error))
							return false;
						AnimatorCondition condition;
						condition.Parameter = conditionNode["Parameter"].as<std::string>();
						const uint32_t mode = conditionNode["Mode"].as<uint32_t>();
						condition.Threshold = conditionNode["Threshold"].as<float>();
						if (mode > static_cast<uint32_t>(AnimatorConditionMode::NotEqual)
							|| !IsFinite(condition.Threshold)) throw std::runtime_error("Animator condition is invalid");
						condition.Mode = static_cast<AnimatorConditionMode>(mode);
						transition.Conditions.push_back(std::move(condition));
					}
					animator.Transitions.push_back(std::move(transition));
				}
				ResetRuntime(animator);
				entity.AddOrReplaceComponent<SpriteAnimator>(std::move(animator));
				return true;
			}
			catch (const std::exception& exception)
			{
				error = exception.what();
				return false;
			}
		}

		ComponentDescriptor MakeSpriteAnimatorDescriptor()
		{
			auto descriptor = BaseDescriptor<SpriteAnimator>(ComponentIds::SpriteAnimator,
				"TomCat.SpriteAnimator", "Sprite Animator");
			descriptor.EncodeLegacyFields = LegacyFlatMap("SpriteAnimator");
			descriptor.DecodeLegacyFields = LegacySpriteAnimatorDecode();
			descriptor.ScriptAccessible = true;
			descriptor.Encode = &EncodeSpriteAnimator;
			descriptor.Decode = &DecodeSpriteAnimator;
			descriptor.Add = [](Entity entity, std::string& error)
			{
				if (!entity) { error = "Cannot add Sprite Animator to an invalid entity"; return false; }
				if (!entity.HasComponent<SpriteRenderer>()) entity.AddComponent<SpriteRenderer>();
				if (!entity.HasComponent<SpriteAnimator>())
				{
					SpriteAnimator animator;
					SpriteAnimationClip clip;
					clip.Name = "Default";
					clip.Frames.push_back({ entity.GetComponent<SpriteRenderer>().SpriteHandle, 1.0f / 12.0f });
					animator.Clips.push_back(std::move(clip));
					entity.AddComponent<SpriteAnimator>(std::move(animator));
				}
				return true;
			};
			descriptor.Properties = {
				BoolProperty<SpriteAnimator>(ComponentIds::SpriteAnimatorProperties::Enabled,
					"Enabled", &SpriteAnimator::Enabled),
				FloatProperty<SpriteAnimator>(ComponentIds::SpriteAnimatorProperties::Speed,
					"Speed", &SpriteAnimator::Speed, 0.0f),
				BoolProperty<SpriteAnimator>(ComponentIds::SpriteAnimatorProperties::PlayOnStart,
					"PlayOnStart", &SpriteAnimator::PlayOnStart),
				Property(ComponentIds::SpriteAnimatorProperties::InitialClip, "InitialClip",
					PropertyKind::String,
					[](Entity entity) -> PropertyValue { return entity.GetComponent<SpriteAnimator>().InitialClip; },
					[](Entity entity, const PropertyValue& value, std::string& error)
					{
						const std::string& name = std::get<std::string>(value);
						const auto& clips = entity.GetComponent<SpriteAnimator>().Clips;
						if (!name.empty() && std::none_of(clips.begin(), clips.end(),
							[&name](const SpriteAnimationClip& clip) { return clip.Name == name; }))
						{ error = "InitialClip must name an existing clip"; return false; }
						entity.GetComponent<SpriteAnimator>().InitialClip = name;
						return true;
					})
			};
			SetPropertyDefault(descriptor,
				ComponentIds::SpriteAnimatorProperties::InitialClip, std::string{});
			return descriptor;
		}

		ComponentDescriptor MakeLineRendererDescriptor()
		{
			auto descriptor = BaseDescriptor<LineRenderer>(ComponentIds::LineRenderer,
				"TomCat.LineRenderer", "Line Renderer");
			descriptor.EncodeLegacyFields = LegacyFlatMap("LineRenderer");
			descriptor.DecodeLegacyFields = LegacyFlatDecode("LineRenderer");
			descriptor.Properties = {
				BoolProperty<LineRenderer>(ComponentIds::LineRendererProperties::Enabled,
					"Enabled", &LineRenderer::Enabled),
				ColorProperty<LineRenderer>(ComponentIds::LineRendererProperties::Color,
					"Color", &LineRenderer::_Color),
				Vector3Property<LineRenderer>(ComponentIds::LineRendererProperties::Start,
					"Start", &LineRenderer::Start),
				Vector3Property<LineRenderer>(ComponentIds::LineRendererProperties::End,
					"End", &LineRenderer::End),
				FloatProperty<LineRenderer>(ComponentIds::LineRendererProperties::Width,
					"Width", &LineRenderer::Width, 0.0f,
					std::numeric_limits<float>::max(), false)
			};
			return descriptor;
		}

		bool IsFieldId(std::string_view value)
		{
			if (value.size() != 32) return false;
			return std::all_of(value.begin(), value.end(), [](unsigned char character)
			{
				return (character >= '0' && character <= '9')
					|| (character >= 'a' && character <= 'f');
			});
		}

		bool EncodeCSharpScripts(const ComponentDescriptor&, Entity entity,
			YAML::Emitter& output, std::string& error)
		{
			try
			{
				const CSharpScripts& scripts = entity.GetComponent<CSharpScripts>();
				output << YAML::BeginMap << YAML::Key << "Scripts" << YAML::Value
					<< YAML::BeginSeq;
				for (const CSharpScriptEntry& script : scripts.Scripts)
				{
					if (static_cast<uint64_t>(script.AttachmentID) == 0)
						throw std::runtime_error("C# AttachmentID must be nonzero");
					output << YAML::BeginMap
						<< YAML::Key << "AttachmentID" << YAML::Value
						<< static_cast<uint64_t>(script.AttachmentID)
						<< YAML::Key << "Enabled" << YAML::Value << script.Enabled
						<< YAML::Key << "ScriptHandle" << YAML::Value
						<< static_cast<uint64_t>(script.ScriptAsset)
						<< YAML::Key << "ClassName" << YAML::Value
						<< script.LastKnownClassName
						<< YAML::Key << "Fields" << YAML::Value << YAML::BeginSeq;
					for (const ScriptField& field : script.Fields)
					{
						if (!IsFieldId(field.FieldID) || field.Name.empty())
							throw std::runtime_error("C# script field identity is invalid");
						output << YAML::BeginMap
							<< YAML::Key << "FieldID" << YAML::Value << field.FieldID
							<< YAML::Key << "Name" << YAML::Value << field.Name
							<< YAML::Key << "Type" << YAML::Value
							<< ScriptFieldTypeToString(field.Type);
						if (!field.TypeName.empty())
							output << YAML::Key << "TypeName" << YAML::Value << field.TypeName;
						output << YAML::Key << "Value" << YAML::Value;
						EmitScriptFieldValue(output, field);
						output << YAML::EndMap;
					}
					output << YAML::EndSeq << YAML::EndMap;
				}
				output << YAML::EndSeq << YAML::EndMap;
				return output.good();
			}
			catch (const std::exception& exception)
			{
				error = exception.what();
				return false;
			}
		}

		bool DecodeCSharpScripts(const ComponentDescriptor&, Entity entity,
			const YAML::Node& node, std::string& error)
		{
			try
			{
				if (!ValidateLegacyMap(node, "CSharpScripts.Properties",
					{ "Scripts" }, {}, error))
					return false;
				if (!node["Scripts"].IsSequence())
					throw std::runtime_error("CSharpScripts.Properties.Scripts must be a sequence");
				CSharpScripts decoded;
				std::vector<uint64_t> attachmentIds;
				for (const YAML::Node& scriptNode : node["Scripts"])
				{
					if (!ValidateLegacyMap(scriptNode, "CSharpScripts script",
						{ "AttachmentID", "Enabled", "ScriptHandle", "ClassName",
							"Fields" }, {}, error))
						return false;
					CSharpScriptEntry script;
					const uint64_t attachmentId = scriptNode["AttachmentID"].as<uint64_t>();
					if (attachmentId == 0 || std::find(attachmentIds.begin(),
						attachmentIds.end(), attachmentId) != attachmentIds.end())
						throw std::runtime_error("C# AttachmentID must be nonzero and unique");
					attachmentIds.push_back(attachmentId);
					script.AttachmentID = UUID(attachmentId);
					script.Enabled = scriptNode["Enabled"].as<bool>();
					script.ScriptAsset = AssetHandle(scriptNode["ScriptHandle"].as<uint64_t>());
					script.LastKnownClassName = scriptNode["ClassName"].as<std::string>();
					const YAML::Node fields = scriptNode["Fields"];
					if (!fields.IsSequence()) throw std::runtime_error("C# Fields must be a sequence");
					std::vector<std::string> fieldIds;
					for (const YAML::Node& fieldNode : fields)
					{
						if (!ValidateLegacyMap(fieldNode, "CSharpScripts field",
							{ "FieldID", "Name", "Type", "Value" },
							{ "TypeName" }, error))
							return false;
						ScriptField field;
						field.FieldID = fieldNode["FieldID"].as<std::string>();
						field.Name = fieldNode["Name"].as<std::string>();
						if (!IsFieldId(field.FieldID) || field.Name.empty()
							|| std::find(fieldIds.begin(), fieldIds.end(), field.FieldID) != fieldIds.end())
							throw std::runtime_error("C# field identity must be valid and unique per attachment");
						fieldIds.push_back(field.FieldID);
						const std::string typeName = fieldNode["Type"].as<std::string>();
						if (!TryParseScriptFieldType(typeName, field.Type))
							throw std::runtime_error("C# field Type is invalid");
						if (fieldNode["TypeName"])
							field.TypeName = fieldNode["TypeName"].as<std::string>();
						field.Value = ReadScriptFieldValue(fieldNode["Value"], field.Type);
						if (!IsScriptFieldValueCompatible(field.Type, field.Value))
							throw std::runtime_error("C# field Value does not match Type");
						script.Fields.push_back(std::move(field));
					}
					decoded.Scripts.push_back(std::move(script));
				}
				entity.AddOrReplaceComponent<CSharpScripts>(std::move(decoded));
				return true;
			}
			catch (const std::exception& exception)
			{
				error = exception.what();
				return false;
			}
		}

		ComponentDescriptor MakeCSharpScriptsDescriptor()
		{
			auto descriptor = BaseDescriptor<CSharpScripts>(ComponentIds::CSharpScripts,
				"TomCat.CSharpScripts", "C# Scripts");
			descriptor.AddableInInspector = false;
			descriptor.EncodeLegacyFields = LegacyFlatMap("CSharpScripts");
			descriptor.DecodeLegacyFields = LegacyStructuredDecode("CSharpScripts",
				{ "Scripts" });
			descriptor.Encode = &EncodeCSharpScripts;
			descriptor.Decode = &DecodeCSharpScripts;
			descriptor.RemapEntityReferences = [](Entity entity,
				const EntityReferenceMapper& remap, std::string& error)
			{
				for (CSharpScriptEntry& script :
					entity.GetComponent<CSharpScripts>().Scripts)
				{
					for (ScriptField& field : script.Fields)
					{
						if (field.Type != ScriptFieldType::Entity)
							continue;
						if (!std::holds_alternative<uint64_t>(field.Value))
						{
							error = "C# Entity field '" + field.Name
								+ "' has an incompatible value";
							return false;
						}
						uint64_t value = std::get<uint64_t>(field.Value);
						const std::string context = "C# Entity field '" + field.Name + "'";
						if (!remap(value, context, error))
							return false;
						field.Value = value;
					}
				}
				return true;
			};
			return descriptor;
		}

		ComponentDescriptor MakeAudioSourceDescriptor()
		{
			auto descriptor = BaseDescriptor<AudioSource>(ComponentIds::AudioSource,
				"TomCat.AudioSource", "Audio Source");
			descriptor.ScriptAccessible = true;
			descriptor.EncodeLegacyFields = LegacyFlatMap("AudioSource");
			descriptor.DecodeLegacyFields = LegacyFlatDecode("AudioSource", false, {},
				{ "Streaming", "SpatialBlend", "MinDistance", "MaxDistance" });
			descriptor.Remove = [](Entity entity, std::string& error)
			{
				if (!entity || !entity.HasComponent<AudioSource>())
				{ error = "Audio Source is not present on the entity"; return false; }
				AudioSceneRuntime::DeferDestroySource(entity);
				entity.RemoveComponent<AudioSource>();
				return true;
			};
			auto clip = AssetProperty(ComponentIds::AudioSourceProperties::Clip, "Clip",
				AssetType::Audio, [](Entity entity) -> AssetHandle&
				{ return entity.GetComponent<AudioSource>().Clip; });
			auto enabled = BoolProperty<AudioSource>(
				ComponentIds::AudioSourceProperties::Enabled, "Enabled",
				&AudioSource::Enabled);
			auto loop = BoolProperty<AudioSource>(
				ComponentIds::AudioSourceProperties::Loop, "Loop", &AudioSource::Loop);
			auto streaming = BoolProperty<AudioSource>(
				ComponentIds::AudioSourceProperties::Streaming, "Streaming",
				&AudioSource::Streaming);
			auto minDistance = Property(
				ComponentIds::AudioSourceProperties::MinDistance, "MinDistance",
				PropertyKind::Float,
				[](Entity entity) -> PropertyValue
				{ return entity.GetComponent<AudioSource>().MinDistance; },
				[](Entity entity, const PropertyValue& value, std::string& error)
				{
					const float distance = std::get<float>(value);
					auto& source = entity.GetComponent<AudioSource>();
					if (!IsFinite(distance) || distance < 0.0f
						|| distance >= source.MaxDistance)
					{
						error = "MinDistance must be finite, non-negative, and less than MaxDistance";
						return false;
					}
					source.MinDistance = distance;
					return true;
				});
			auto maxDistance = Property(
				ComponentIds::AudioSourceProperties::MaxDistance, "MaxDistance",
				PropertyKind::Float,
				[](Entity entity) -> PropertyValue
				{ return entity.GetComponent<AudioSource>().MaxDistance; },
				[](Entity entity, const PropertyValue& value, std::string& error)
				{
					const float distance = std::get<float>(value);
					auto& source = entity.GetComponent<AudioSource>();
					if (!IsFinite(distance) || distance <= source.MinDistance)
					{
						error = "MaxDistance must be finite and greater than MinDistance";
						return false;
					}
					source.MaxDistance = distance;
					return true;
				});
			auto mixerGroup = Property(
				ComponentIds::AudioSourceProperties::MixerGroup, "MixerGroup",
				PropertyKind::UInt32,
				[](Entity entity) -> PropertyValue
				{ return static_cast<uint32_t>(entity.GetComponent<AudioSource>().MixerGroup); },
				[](Entity entity, const PropertyValue& value, std::string& error)
				{
					const uint32_t group = std::get<uint32_t>(value);
					if (group > 2)
					{
						error = "MixerGroup is invalid";
						return false;
					}
					entity.GetComponent<AudioSource>().MixerGroup =
						static_cast<uint8_t>(group);
					return true;
				});
			descriptor.Properties = {
				std::move(enabled), std::move(clip),
				BoolProperty<AudioSource>(ComponentIds::AudioSourceProperties::PlayOnStart,
					"PlayOnStart", &AudioSource::PlayOnStart),
				std::move(loop), std::move(streaming),
				FloatProperty<AudioSource>(
					ComponentIds::AudioSourceProperties::Volume,
					"Volume", &AudioSource::Volume, 0.0f, 4.0f),
				FloatProperty<AudioSource>(
					ComponentIds::AudioSourceProperties::Pitch,
					"Pitch", &AudioSource::Pitch, 0.25f, 4.0f),
				FloatProperty<AudioSource>(ComponentIds::AudioSourceProperties::SpatialBlend,
					"SpatialBlend", &AudioSource::SpatialBlend, 0.0f, 1.0f),
				std::move(minDistance), std::move(maxDistance),
				std::move(mixerGroup)
			};
			const AudioSource audioDefaults;
			SetPropertyDefault(descriptor, ComponentIds::AudioSourceProperties::MinDistance,
				audioDefaults.MinDistance);
			SetPropertyDefault(descriptor, ComponentIds::AudioSourceProperties::MaxDistance,
				audioDefaults.MaxDistance);
			SetPropertyDefault(descriptor, ComponentIds::AudioSourceProperties::MixerGroup,
				static_cast<uint32_t>(audioDefaults.MixerGroup));
			return descriptor;
		}

		ComponentDescriptor MakeAudioListenerDescriptor()
		{
			auto descriptor = BaseDescriptor<AudioListener>(ComponentIds::AudioListener,
				"TomCat.AudioListener", "Audio Listener");
			descriptor.ScriptAccessible = true;
			descriptor.EncodeLegacyFields = LegacyFlatMap("AudioListener");
			descriptor.DecodeLegacyFields = LegacyFlatDecode("AudioListener");
			descriptor.Properties = {
				BoolProperty<AudioListener>(ComponentIds::AudioListenerProperties::Enabled,
					"Enabled", &AudioListener::Enabled),
				BoolProperty<AudioListener>(ComponentIds::AudioListenerProperties::Primary,
					"Primary", &AudioListener::Primary)
			};
			return descriptor;
		}

		ComponentDescriptor MakeRigidbodyDescriptor()
		{
			auto descriptor = BaseDescriptor<Rigidbody2D>(ComponentIds::Rigidbody2D,
				"TomCat.Rigidbody2D", "Rigidbody 2D");
			descriptor.ScriptAccessible = true;
			descriptor.EncodeLegacyFields = LegacyRigidbody2D();
			descriptor.DecodeLegacyFields = LegacyRigidbody2DDecode();
			descriptor.Properties = {
				BoolProperty<Rigidbody2D>(ComponentIds::Rigidbody2DProperties::Enabled,
					"Enabled", &Rigidbody2D::Enabled),
				Property(ComponentIds::Rigidbody2DProperties::BodyType, "BodyType",
					PropertyKind::Int32,
					[](Entity entity) -> PropertyValue { return static_cast<int32_t>(entity.GetComponent<Rigidbody2D>().Type); },
					[](Entity entity, const PropertyValue& value, std::string& error)
					{
						const int32_t type = std::get<int32_t>(value);
						if (type < 0 || type > 2) { error = "BodyType is invalid"; return false; }
						entity.GetComponent<Rigidbody2D>().Type = static_cast<Rigidbody2D::BodyType>(type);
						return true;
					}),
				BoolProperty<Rigidbody2D>(ComponentIds::Rigidbody2DProperties::FixedRotation,
					"FixedRotation", &Rigidbody2D::FixedRotation)
			};
			SetPropertyDefault(descriptor, ComponentIds::Rigidbody2DProperties::BodyType,
				static_cast<int32_t>(Rigidbody2D{}.Type));
			return descriptor;
		}

		PropertyDescriptor CollisionBitsProperty(uint64_t id, const char* name,
			std::function<uint16_t&(Entity)> access, bool allowZero)
		{
			PropertyDescriptor property = Property(id, name, PropertyKind::UInt32,
				[access](Entity entity) -> PropertyValue { return static_cast<uint32_t>(access(entity)); },
				[access, allowZero, name](Entity entity, const PropertyValue& value, std::string& error)
				{
					const uint32_t bits = std::get<uint32_t>(value);
					if (bits > 0xffff || (!allowZero && bits == 0))
					{ error = std::string(name) + " is invalid"; return false; }
					access(entity) = static_cast<uint16_t>(bits);
					return true;
				});
			property.DefaultValue = allowZero ? uint32_t(0xffff) : uint32_t(1);
			return property;
		}

		ComponentDescriptor MakeBoxColliderDescriptor()
		{
			auto descriptor = BaseDescriptor<BoxCollider2D>(ComponentIds::BoxCollider2D,
				"TomCat.BoxCollider2D", "Box Collider 2D");
			descriptor.ScriptAccessible = true;
			descriptor.EncodeLegacyFields = LegacyFlatMap("BoxCollider2D");
			descriptor.DecodeLegacyFields = LegacyFlatDecode("BoxCollider2D");
			descriptor.Properties = {
				BoolProperty<BoxCollider2D>(ComponentIds::BoxCollider2DProperties::Enabled, "Enabled", &BoxCollider2D::Enabled),
				BoolProperty<BoxCollider2D>(ComponentIds::BoxCollider2DProperties::IsTrigger, "IsTrigger", &BoxCollider2D::IsTrigger),
				CollisionBitsProperty(ComponentIds::BoxCollider2DProperties::CollisionLayer, "CollisionLayer", [](Entity e) -> uint16_t& { return e.GetComponent<BoxCollider2D>().CollisionLayer; }, false),
				CollisionBitsProperty(ComponentIds::BoxCollider2DProperties::CollisionMask, "CollisionMask", [](Entity e) -> uint16_t& { return e.GetComponent<BoxCollider2D>().CollisionMask; }, true),
				Vector2Property<BoxCollider2D>(ComponentIds::BoxCollider2DProperties::Offset, "Offset", &BoxCollider2D::Offset),
				Vector2Property<BoxCollider2D>(ComponentIds::BoxCollider2DProperties::Size, "Size", &BoxCollider2D::Size, true),
				FloatProperty<BoxCollider2D>(ComponentIds::BoxCollider2DProperties::Density, "Density", &BoxCollider2D::Density, 0.0f),
				FloatProperty<BoxCollider2D>(ComponentIds::BoxCollider2DProperties::Friction, "Friction", &BoxCollider2D::Friction, 0.0f),
				FloatProperty<BoxCollider2D>(ComponentIds::BoxCollider2DProperties::Restitution, "Restitution", &BoxCollider2D::Restitution, 0.0f, 1.0f),
				FloatProperty<BoxCollider2D>(ComponentIds::BoxCollider2DProperties::RestitutionThreshold, "RestitutionThreshold", &BoxCollider2D::RestitutionThreshold, 0.0f)
			};
			return descriptor;
		}

		ComponentDescriptor MakeCircleColliderDescriptor()
		{
			auto descriptor = BaseDescriptor<CircleCollider2D>(ComponentIds::CircleCollider2D,
				"TomCat.CircleCollider2D", "Circle Collider 2D");
			descriptor.ScriptAccessible = true;
			descriptor.EncodeLegacyFields = LegacyFlatMap("CircleCollider2D");
			descriptor.DecodeLegacyFields = LegacyFlatDecode("CircleCollider2D");
			descriptor.Properties = {
				BoolProperty<CircleCollider2D>(ComponentIds::CircleCollider2DProperties::Enabled, "Enabled", &CircleCollider2D::Enabled),
				BoolProperty<CircleCollider2D>(ComponentIds::CircleCollider2DProperties::IsTrigger, "IsTrigger", &CircleCollider2D::IsTrigger),
				CollisionBitsProperty(ComponentIds::CircleCollider2DProperties::CollisionLayer, "CollisionLayer", [](Entity e) -> uint16_t& { return e.GetComponent<CircleCollider2D>().CollisionLayer; }, false),
				CollisionBitsProperty(ComponentIds::CircleCollider2DProperties::CollisionMask, "CollisionMask", [](Entity e) -> uint16_t& { return e.GetComponent<CircleCollider2D>().CollisionMask; }, true),
				Vector2Property<CircleCollider2D>(ComponentIds::CircleCollider2DProperties::Offset, "Offset", &CircleCollider2D::Offset),
				FloatProperty<CircleCollider2D>(ComponentIds::CircleCollider2DProperties::Radius, "Radius", &CircleCollider2D::Radius, 0.0f, std::numeric_limits<float>::max(), false),
				FloatProperty<CircleCollider2D>(ComponentIds::CircleCollider2DProperties::Density, "Density", &CircleCollider2D::Density, 0.0f),
				FloatProperty<CircleCollider2D>(ComponentIds::CircleCollider2DProperties::Friction, "Friction", &CircleCollider2D::Friction, 0.0f),
				FloatProperty<CircleCollider2D>(ComponentIds::CircleCollider2DProperties::Restitution, "Restitution", &CircleCollider2D::Restitution, 0.0f, 1.0f)
			};
			return descriptor;
		}

		ComponentDescriptor MakeDistanceJointDescriptor()
		{
			auto descriptor = BaseDescriptor<DistanceJoint2D>(ComponentIds::DistanceJoint2D,
				"TomCat.DistanceJoint2D", "Distance Joint 2D");
			descriptor.ScriptAccessible = true;
			descriptor.EncodeLegacyFields = LegacyFlatMap("DistanceJoint2D");
			descriptor.DecodeLegacyFields = LegacyFlatDecode("DistanceJoint2D");
			descriptor.Add = [](Entity entity, std::string& error)
			{
				if (!entity) { error = "Cannot add Distance Joint 2D to an invalid entity"; return false; }
				if (!entity.HasComponent<Rigidbody2D>()) entity.AddComponent<Rigidbody2D>();
				if (!entity.HasComponent<DistanceJoint2D>()) entity.AddComponent<DistanceJoint2D>();
				return true;
			};
			descriptor.Properties = {
				BoolProperty<DistanceJoint2D>(ComponentIds::DistanceJoint2DProperties::Enabled, "Enabled", &DistanceJoint2D::Enabled),
				Property(ComponentIds::DistanceJoint2DProperties::ConnectedEntity, "ConnectedEntity", PropertyKind::UInt64,
					[](Entity entity) -> PropertyValue { return static_cast<uint64_t>(entity.GetComponent<DistanceJoint2D>().ConnectedEntity); },
					[](Entity entity, const PropertyValue& value, std::string& error)
					{
						const uint64_t connected = std::get<uint64_t>(value);
						if (connected == static_cast<uint64_t>(entity.GetUUID())) { error = "ConnectedEntity cannot reference itself"; return false; }
						entity.GetComponent<DistanceJoint2D>().ConnectedEntity = UUID(connected);
						return true;
					}, true),
				Vector2Property<DistanceJoint2D>(ComponentIds::DistanceJoint2DProperties::Anchor, "Anchor", &DistanceJoint2D::Anchor),
				Vector2Property<DistanceJoint2D>(ComponentIds::DistanceJoint2DProperties::ConnectedAnchor, "ConnectedAnchor", &DistanceJoint2D::ConnectedAnchor),
				FloatProperty<DistanceJoint2D>(ComponentIds::DistanceJoint2DProperties::Distance, "Distance", &DistanceJoint2D::Distance, 0.0f, std::numeric_limits<float>::max(), false),
				FloatProperty<DistanceJoint2D>(ComponentIds::DistanceJoint2DProperties::Frequency, "Frequency", &DistanceJoint2D::Frequency, 0.0f),
				FloatProperty<DistanceJoint2D>(ComponentIds::DistanceJoint2DProperties::Damping, "Damping", &DistanceJoint2D::Damping, 0.0f, 1.0f),
				BoolProperty<DistanceJoint2D>(ComponentIds::DistanceJoint2DProperties::CollideConnected, "CollideConnected", &DistanceJoint2D::CollideConnected)
			};
			SetPropertyDefault(descriptor,
				ComponentIds::DistanceJoint2DProperties::ConnectedEntity, uint64_t(0));
			return descriptor;
		}

		bool EncodeTilemap2D(const ComponentDescriptor&, Entity entity,
			YAML::Emitter& output, std::string& error)
		{
			try
			{
				Tilemap2D tilemap = entity.GetComponent<Tilemap2D>();
				Tilemap2DRuntime::Normalize(tilemap);
				output << YAML::BeginMap
					<< YAML::Key << "Enabled" << YAML::Value << tilemap.Enabled
					<< YAML::Key << "CellSize" << YAML::Value;
				EmitVector(output, tilemap.CellSize);
				output << YAML::Key << "CellGap" << YAML::Value;
				EmitVector(output, tilemap.CellGap);
				output << YAML::Key << "SortingLayer" << YAML::Value << tilemap.SortingLayer
					<< YAML::Key << "OrderInLayer" << YAML::Value << tilemap.OrderInLayer
					<< YAML::Key << "Cells" << YAML::Value << YAML::BeginSeq;
				for (const TilemapCell& cell : tilemap.Cells)
				{
					output << YAML::BeginMap
						<< YAML::Key << "Coordinate" << YAML::Value << YAML::Flow
						<< YAML::BeginSeq << cell.Coordinate.x << cell.Coordinate.y
						<< YAML::EndSeq
						<< YAML::Key << "SpriteHandle" << YAML::Value
						<< static_cast<uint64_t>(cell.SpriteHandle)
						<< YAML::Key << "Tint" << YAML::Value;
					EmitVector(output, cell.Tint);
					output << YAML::Key << "FlipX" << YAML::Value << cell.FlipX
						<< YAML::Key << "FlipY" << YAML::Value << cell.FlipY
						<< YAML::Key << "RotationQuarterTurns" << YAML::Value
						<< cell.RotationQuarterTurns << YAML::EndMap;
				}
				output << YAML::EndSeq << YAML::EndMap;
				return output.good();
			}
			catch (const std::exception& exception)
			{
				error = exception.what();
				return false;
			}
		}

		bool DecodeTilemap2D(const ComponentDescriptor&, Entity entity,
			const YAML::Node& node, std::string& error)
		{
			try
			{
				if (!ValidateLegacyMap(node, "Tilemap2D.Properties",
					{ "Enabled", "CellSize", "CellGap", "SortingLayer",
						"OrderInLayer", "Cells" }, {}, error))
					return false;
				Tilemap2D tilemap;
				tilemap.Enabled = node["Enabled"].as<bool>();
				tilemap.CellSize = ReadVector2(node["CellSize"]);
				tilemap.CellGap = ReadVector2(node["CellGap"]);
				tilemap.SortingLayer = node["SortingLayer"].as<int32_t>();
				tilemap.OrderInLayer = node["OrderInLayer"].as<int32_t>();
				if (!IsFinite(tilemap.CellSize) || tilemap.CellSize.x <= 0.0f
					|| tilemap.CellSize.y <= 0.0f || !IsFinite(tilemap.CellGap)
					|| tilemap.CellSize.x + tilemap.CellGap.x <= 0.0f
					|| tilemap.CellSize.y + tilemap.CellGap.y <= 0.0f)
					throw std::runtime_error("Tilemap2D cell size/gap is invalid");
				const YAML::Node cells = node["Cells"];
				if (!cells.IsSequence() || cells.size() > 1000000)
					throw std::runtime_error("Tilemap2D.Cells must be a bounded sequence");
				for (const YAML::Node& cellNode : cells)
				{
					if (!ValidateLegacyMap(cellNode, "Tilemap2D cell",
						{ "Coordinate", "SpriteHandle", "Tint", "FlipX", "FlipY",
							"RotationQuarterTurns" }, {}, error))
						return false;
					const YAML::Node coordinate = cellNode["Coordinate"];
					if (!coordinate.IsSequence() || coordinate.size() != 2)
						throw std::runtime_error("Tilemap2D cell coordinate is invalid");
					TilemapCell cell;
					cell.Coordinate = { coordinate[0].as<int32_t>(),
						coordinate[1].as<int32_t>() };
					cell.SpriteHandle = AssetHandle(cellNode["SpriteHandle"].as<uint64_t>());
					cell.Tint = ReadVector4(cellNode["Tint"]);
					cell.FlipX = cellNode["FlipX"].as<bool>();
					cell.FlipY = cellNode["FlipY"].as<bool>();
					cell.RotationQuarterTurns = cellNode["RotationQuarterTurns"].as<int32_t>();
					if (!IsUnitColor(cell.Tint))
						throw std::runtime_error("Tilemap2D cell tint is invalid");
					tilemap.Cells.push_back(std::move(cell));
				}
				Tilemap2DRuntime::Normalize(tilemap);
				entity.AddOrReplaceComponent<Tilemap2D>(std::move(tilemap));
				return true;
			}
			catch (const std::exception& exception)
			{
				error = exception.what();
				return false;
			}
		}

		ComponentDescriptor MakeTilemap2DDescriptor()
		{
			auto descriptor = BaseDescriptor<Tilemap2D>(ComponentIds::Tilemap2D,
				"TomCat.Tilemap2D", "Tilemap 2D");
			descriptor.Encode = &EncodeTilemap2D;
			descriptor.Decode = &DecodeTilemap2D;
			descriptor.UseGenericInspector = true;
			descriptor.Properties = {
				BoolProperty<Tilemap2D>(ComponentIds::Tilemap2DProperties::Enabled,
					"Enabled", &Tilemap2D::Enabled),
				Vector2Property<Tilemap2D>(ComponentIds::Tilemap2DProperties::CellSize,
					"CellSize", &Tilemap2D::CellSize, true),
				Vector2Property<Tilemap2D>(ComponentIds::Tilemap2DProperties::CellGap,
					"CellGap", &Tilemap2D::CellGap),
				IntProperty<Tilemap2D>(ComponentIds::Tilemap2DProperties::SortingLayer,
					"SortingLayer", &Tilemap2D::SortingLayer),
				IntProperty<Tilemap2D>(ComponentIds::Tilemap2DProperties::OrderInLayer,
					"OrderInLayer", &Tilemap2D::OrderInLayer)
			};
			return descriptor;
		}

		ComponentDescriptor MakeParticleSystem2DDescriptor()
		{
			auto descriptor = BaseDescriptor<ParticleSystem2D>(
				ComponentIds::ParticleSystem2D, "TomCat.ParticleSystem2D",
				"Particle System 2D");
			descriptor.UseGenericInspector = true;
			auto sprite = AssetProperty(ComponentIds::ParticleSystem2DProperties::Sprite,
				"Sprite", AssetType::Texture2D, [](Entity entity) -> AssetHandle&
				{ return entity.GetComponent<ParticleSystem2D>().SpriteHandle; });
			descriptor.Properties = {
				BoolProperty<ParticleSystem2D>(ComponentIds::ParticleSystem2DProperties::Enabled,
					"Enabled", &ParticleSystem2D::Enabled),
				BoolProperty<ParticleSystem2D>(ComponentIds::ParticleSystem2DProperties::PlayOnStart,
					"PlayOnStart", &ParticleSystem2D::PlayOnStart),
				BoolProperty<ParticleSystem2D>(ComponentIds::ParticleSystem2DProperties::Loop,
					"Loop", &ParticleSystem2D::Loop),
				FloatProperty<ParticleSystem2D>(ComponentIds::ParticleSystem2DProperties::Duration,
					"Duration", &ParticleSystem2D::Duration, 0.0f),
				FloatProperty<ParticleSystem2D>(ComponentIds::ParticleSystem2DProperties::EmissionRate,
					"EmissionRate", &ParticleSystem2D::EmissionRate, 0.0f),
				Property(ComponentIds::ParticleSystem2DProperties::MaxParticles,
					"MaxParticles", PropertyKind::Int32,
					[](Entity entity) -> PropertyValue
					{ return entity.GetComponent<ParticleSystem2D>().MaxParticles; },
					[](Entity entity, const PropertyValue& value, std::string& error)
					{
						const int32_t count = std::get<int32_t>(value);
						if (count < 0 || count > 100000)
						{
							error = "MaxParticles must be between 0 and 100000";
							return false;
						}
						entity.GetComponent<ParticleSystem2D>().MaxParticles = count;
						return true;
					}, false, int32_t(256)),
				FloatProperty<ParticleSystem2D>(ComponentIds::ParticleSystem2DProperties::StartLifetime,
					"StartLifetime", &ParticleSystem2D::StartLifetime, 0.0f,
					std::numeric_limits<float>::max(), false),
				FloatProperty<ParticleSystem2D>(ComponentIds::ParticleSystem2DProperties::StartSpeed,
					"StartSpeed", &ParticleSystem2D::StartSpeed, 0.0f),
				FloatProperty<ParticleSystem2D>(ComponentIds::ParticleSystem2DProperties::StartSize,
					"StartSize", &ParticleSystem2D::StartSize, 0.0f),
				FloatProperty<ParticleSystem2D>(ComponentIds::ParticleSystem2DProperties::EndSize,
					"EndSize", &ParticleSystem2D::EndSize, 0.0f),
				FloatProperty<ParticleSystem2D>(ComponentIds::ParticleSystem2DProperties::GravityScale,
					"GravityScale", &ParticleSystem2D::GravityScale),
				Vector2Property<ParticleSystem2D>(ComponentIds::ParticleSystem2DProperties::Direction,
					"Direction", &ParticleSystem2D::Direction),
				FloatProperty<ParticleSystem2D>(ComponentIds::ParticleSystem2DProperties::SpreadDegrees,
					"SpreadDegrees", &ParticleSystem2D::SpreadDegrees, 0.0f, 360.0f),
				ColorProperty<ParticleSystem2D>(ComponentIds::ParticleSystem2DProperties::StartColor,
					"StartColor", &ParticleSystem2D::StartColor),
				ColorProperty<ParticleSystem2D>(ComponentIds::ParticleSystem2DProperties::EndColor,
					"EndColor", &ParticleSystem2D::EndColor),
				std::move(sprite),
				IntProperty<ParticleSystem2D>(ComponentIds::ParticleSystem2DProperties::SortingLayer,
					"SortingLayer", &ParticleSystem2D::SortingLayer),
				IntProperty<ParticleSystem2D>(ComponentIds::ParticleSystem2DProperties::OrderInLayer,
					"OrderInLayer", &ParticleSystem2D::OrderInLayer),
				Property(ComponentIds::ParticleSystem2DProperties::Seed, "Seed",
					PropertyKind::UInt32,
					[](Entity entity) -> PropertyValue
					{ return entity.GetComponent<ParticleSystem2D>().Seed; },
					[](Entity entity, const PropertyValue& value, std::string&)
					{
						entity.GetComponent<ParticleSystem2D>().Seed = std::get<uint32_t>(value);
						return true;
					}, false, uint32_t(1))
			};
			return descriptor;
		}

		ComponentDescriptor MakeLight2DDescriptor()
		{
			auto descriptor = BaseDescriptor<Light2D>(ComponentIds::Light2D,
				"TomCat.Light2D", "Light 2D");
			descriptor.UseGenericInspector = true;
			descriptor.Properties = {
				BoolProperty<Light2D>(ComponentIds::Light2DProperties::Enabled,
					"Enabled", &Light2D::Enabled),
				Property(ComponentIds::Light2DProperties::Type, "Type", PropertyKind::Int32,
					[](Entity entity) -> PropertyValue
					{ return static_cast<int32_t>(entity.GetComponent<Light2D>().Type); },
					[](Entity entity, const PropertyValue& value, std::string& error)
					{
						const int32_t type = std::get<int32_t>(value);
						if (type < 0 || type > 1)
						{
							error = "Light2D.Type must be Global (0) or Point (1)";
							return false;
						}
						entity.GetComponent<Light2D>().Type = static_cast<Light2DType>(type);
						return true;
					}, false, int32_t(1)),
				ColorProperty<Light2D>(ComponentIds::Light2DProperties::Color,
					"Color", &Light2D::Color),
				FloatProperty<Light2D>(ComponentIds::Light2DProperties::Intensity,
					"Intensity", &Light2D::Intensity, 0.0f),
				FloatProperty<Light2D>(ComponentIds::Light2DProperties::Radius,
					"Radius", &Light2D::Radius, 0.0f,
					std::numeric_limits<float>::max(), false),
				FloatProperty<Light2D>(ComponentIds::Light2DProperties::Falloff,
					"Falloff", &Light2D::Falloff, 0.0f,
					std::numeric_limits<float>::max(), false)
			};
			return descriptor;
		}

	}

	std::vector<ComponentDescriptor> MakeBuiltInComponentDescriptors()
	{
		std::vector<ComponentDescriptor> result;
		result.reserve(19);
		result.emplace_back(MakeIDDescriptor());
		result.emplace_back(MakeTagDescriptor());
		result.emplace_back(MakeEntityMetadataDescriptor());
		result.emplace_back(MakeTransformDescriptor());
		result.emplace_back(MakeCameraDescriptor());
		result.emplace_back(MakeSpriteRendererDescriptor());
		result.emplace_back(MakeSpriteAnimatorDescriptor());
		result.emplace_back(MakeLineRendererDescriptor());
		result.emplace_back(MakeCSharpScriptsDescriptor());
		result.emplace_back(MakeAudioSourceDescriptor());
		result.emplace_back(MakeAudioListenerDescriptor());
		result.emplace_back(MakeRigidbodyDescriptor());
		result.emplace_back(MakeBoxColliderDescriptor());
		result.emplace_back(MakeCircleColliderDescriptor());
		result.emplace_back(MakeDistanceJointDescriptor());
		result.emplace_back(MakeEditorVisibilityDescriptor());
		result.emplace_back(MakeTilemap2DDescriptor());
		result.emplace_back(MakeParticleSystem2DDescriptor());
		result.emplace_back(MakeLight2DDescriptor());
		return result;
	}

}
