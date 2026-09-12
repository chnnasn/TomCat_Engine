#include "tcpch.h"
#include "SceneSerializer.h"

#include "Components.h"
#include "Entity.h"
#include "Serialization/SceneArchiveCodec.h"
#include "TomCat/Asset/AssetManager.h"
#include "TomCat/Utils/FileSystemUtils.h"
#include "TomCat/Utils/PathUtils.h"

#include <cmath>
#include <filesystem>
#include <fstream>
#include <initializer_list>
#include <limits>
#include <sstream>
#include <stdexcept>
#include <system_error>
#include <unordered_map>
#include <unordered_set>

#include <yaml-cpp/yaml.h>

namespace YAML {

	template<>
	struct convert<glm::vec2>
	{
		static Node encode(const glm::vec2& rhs)
		{
			Node node;
			node.push_back(rhs.x);
			node.push_back(rhs.y);
			node.SetStyle(EmitterStyle::Flow);
			return node;
		}

		static bool decode(const Node& node, glm::vec2& rhs)
		{
			if (!node.IsSequence() || node.size() != 2)
				return false;
			rhs.x = node[0].as<float>();
			rhs.y = node[1].as<float>();
			return true;
		}
	};

	template<>
	struct convert<glm::vec3>
	{
		static Node encode(const glm::vec3& rhs)
		{
			Node node;
			node.push_back(rhs.x);
			node.push_back(rhs.y);
			node.push_back(rhs.z);
			node.SetStyle(EmitterStyle::Flow);
			return node;
		}

		static bool decode(const Node& node, glm::vec3& rhs)
		{
			if (!node.IsSequence() || node.size() != 3)
				return false;
			rhs.x = node[0].as<float>();
			rhs.y = node[1].as<float>();
			rhs.z = node[2].as<float>();
			return true;
		}
	};

	template<>
	struct convert<glm::vec4>
	{
		static Node encode(const glm::vec4& rhs)
		{
			Node node;
			node.push_back(rhs.x);
			node.push_back(rhs.y);
			node.push_back(rhs.z);
			node.push_back(rhs.w);
			node.SetStyle(EmitterStyle::Flow);
			return node;
		}

		static bool decode(const Node& node, glm::vec4& rhs)
		{
			if (!node.IsSequence() || node.size() != 4)
				return false;
			rhs.x = node[0].as<float>();
			rhs.y = node[1].as<float>();
			rhs.z = node[2].as<float>();
			rhs.w = node[3].as<float>();
			return true;
		}
	};

}

namespace TomCat {

	namespace {

		constexpr float kPi = 3.14159265358979323846f;

		bool IsFinite(float value)
		{
			return std::isfinite(value);
		}

		bool IsFinite(double value)
		{
			return std::isfinite(value);
		}

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
			return IsFinite(value.x) && IsFinite(value.y) && IsFinite(value.z) && IsFinite(value.w);
		}

		bool IsFinite(const glm::mat4& value)
		{
			for (glm::length_t column = 0; column < 4; ++column)
			{
				for (glm::length_t row = 0; row < 4; ++row)
				{
					if (!IsFinite(value[column][row]))
						return false;
				}
			}
			return true;
		}

		void RequireFinite(const glm::vec2& value, const std::string& context)
		{
			if (!IsFinite(value))
				throw std::runtime_error(context + " must contain only finite numbers");
		}

		void RequireFinite(const glm::vec3& value, const std::string& context)
		{
			if (!IsFinite(value))
				throw std::runtime_error(context + " must contain only finite numbers");
		}

		void RequireUnitColor(const glm::vec4& value, const std::string& context)
		{
			if (!IsFinite(value)
				|| value.x < 0.0f || value.x > 1.0f
				|| value.y < 0.0f || value.y > 1.0f
				|| value.z < 0.0f || value.z > 1.0f
				|| value.w < 0.0f || value.w > 1.0f)
				throw std::runtime_error(context + " must contain RGBA values in [0, 1]");
		}

		void ValidateTransform(const Transform& transform, const std::string& context)
		{
			RequireFinite(transform._Translation, context + ".Translation");
			RequireFinite(transform._Rotation, context + ".Rotation");
			RequireFinite(transform._Scale, context + ".Scale");
			RequireFinite(transform._LocalTranslation, context + ".LocalTranslation");
			RequireFinite(transform._LocalRotation, context + ".LocalRotation");
			RequireFinite(transform._LocalScale, context + ".LocalScale");
		}

		void ValidateCameraValues(float perspectiveFov, float perspectiveNear, float perspectiveFar,
			float orthographicSize, float orthographicNear, float orthographicFar,
			const glm::vec4& backgroundColor, const std::string& context)
		{
			if (!IsFinite(perspectiveFov) || perspectiveFov <= 0.0f || perspectiveFov >= kPi)
				throw std::runtime_error(context + ".PerspectiveFOV must be in (0, pi)");
			if (!IsFinite(perspectiveNear) || !IsFinite(perspectiveFar)
				|| perspectiveNear <= 0.0f || perspectiveFar <= perspectiveNear)
				throw std::runtime_error(context + " has an invalid perspective clip range");
			if (!IsFinite(orthographicSize) || orthographicSize <= 0.0f)
				throw std::runtime_error(context + ".OrthographicSize must be greater than zero");
			if (!IsFinite(orthographicNear) || !IsFinite(orthographicFar)
				|| orthographicFar <= orthographicNear)
				throw std::runtime_error(context + " has an invalid orthographic clip range");
			RequireUnitColor(backgroundColor, context + ".BackgroundColor");
		}

		void ValidateCamera(const C_Camera& component, const std::string& context)
		{
			const SceneCamera& camera = component._Camera;
			if (camera.GetProjectionType() != SceneCamera::ProjectionType::Perspective
				&& camera.GetProjectionType() != SceneCamera::ProjectionType::Orthographic)
				throw std::runtime_error(context + " has an invalid ProjectionType");
			ValidateCameraValues(camera.GetPerspectiveVerticalFOV(), camera.GetPerspectiveNearClip(),
				camera.GetPerspectiveFarClip(), camera.GetOrthographicSize(), camera.GetOrthographicNearClip(),
				camera.GetOrthographicFarClip(), component.BackgroundColor, context);
			if (!IsFinite(camera.GetProjection()))
				throw std::runtime_error(context + " projection matrix must contain only finite numbers");
		}

		void ValidateSprite(const SpriteRenderer& sprite, const std::string& context)
		{
			RequireUnitColor(sprite._Color, context + ".Color");
			if (!IsFinite(sprite.TilingFactor) || sprite.TilingFactor < 0.0f)
				throw std::runtime_error(context + ".TilingFactor must be finite and non-negative");
		}

		void ValidateEntityMetadata(const EntityMetadata& metadata,
			const std::string& context)
		{
			if (metadata.GameplayTag.empty())
				throw std::runtime_error(context + ".GameplayTag cannot be empty");
			if (metadata.Layer >= Physics2DLayerCount)
				throw std::runtime_error(context + ".Layer must be in [0, 15]");
			switch (metadata.HierarchyIcon)
			{
				case EntityIconMode::Automatic:
				case EntityIconMode::Entity:
				case EntityIconMode::Camera:
				case EntityIconMode::Sprite:
				case EntityIconMode::Rigidbody2D:
				case EntityIconMode::Collider2D:
					break;
				default:
					throw std::runtime_error(context + ".HierarchyIcon is invalid");
			}
		}

		void ValidateLine(const LineRenderer& line, const std::string& context)
		{
			RequireUnitColor(line._Color, context + ".Color");
			RequireFinite(line.Start, context + ".Start");
			RequireFinite(line.End, context + ".End");
			if (!IsFinite(line.Width) || line.Width <= 0.0f)
				throw std::runtime_error(context + ".Width must be finite and greater than zero");
		}

		void ValidatePhysicsMaterial(float density, float friction, float restitution,
			const std::string& context)
		{
			if (!IsFinite(density) || density < 0.0f)
				throw std::runtime_error(context + ".Density must be finite and non-negative");
			if (!IsFinite(friction) || friction < 0.0f)
				throw std::runtime_error(context + ".Friction must be finite and non-negative");
			if (!IsFinite(restitution) || restitution < 0.0f || restitution > 1.0f)
				throw std::runtime_error(context + ".Restitution must be in [0, 1]");
		}

		void ValidateCollisionFilter(uint16_t collisionLayer, const std::string& context)
		{
			if (collisionLayer == 0)
				throw std::runtime_error(context + ".CollisionLayer must contain at least one layer bit");
		}

		void ValidateCollider(const BoxCollider2D& collider, const std::string& context)
		{
			ValidateCollisionFilter(collider.CollisionLayer, context);
			RequireFinite(collider.Offset, context + ".Offset");
			RequireFinite(collider.Size, context + ".Size");
			if (collider.Size.x <= 0.0f || collider.Size.y <= 0.0f)
				throw std::runtime_error(context + ".Size components must be greater than zero");
			ValidatePhysicsMaterial(collider.Density, collider.Friction,
				collider.Restitution, context);
			if (!IsFinite(collider.RestitutionThreshold) || collider.RestitutionThreshold < 0.0f)
				throw std::runtime_error(context + ".RestitutionThreshold must be finite and non-negative");
		}

		void ValidateCollider(const CircleCollider2D& collider, const std::string& context)
		{
			ValidateCollisionFilter(collider.CollisionLayer, context);
			RequireFinite(collider.Offset, context + ".Offset");
			if (!IsFinite(collider.Radius) || collider.Radius <= 0.0f)
				throw std::runtime_error(context + ".Radius must be finite and greater than zero");
			ValidatePhysicsMaterial(collider.Density, collider.Friction,
				collider.Restitution, context);
		}

		void ValidateJoint(const DistanceJoint2D& joint, const std::string& context)
		{
			RequireFinite(joint.Anchor, context + ".Anchor");
			RequireFinite(joint.ConnectedAnchor, context + ".ConnectedAnchor");
			if (!IsFinite(joint.Distance) || joint.Distance <= 0.0f)
				throw std::runtime_error(context + ".Distance must be finite and greater than zero");
			if (!IsFinite(joint.Frequency) || joint.Frequency < 0.0f)
				throw std::runtime_error(context + ".Frequency must be finite and non-negative");
			if (!IsFinite(joint.Damping) || joint.Damping < 0.0f || joint.Damping > 1.0f)
				throw std::runtime_error(context + ".Damping must be in [0, 1]");
		}

		bool IsFieldID(const std::string& value)
		{
			if (value.size() != 32)
				return false;
			for (const unsigned char character : value)
			{
				if (!((character >= '0' && character <= '9')
					|| (character >= 'a' && character <= 'f')))
					return false;
			}
			return true;
		}

		void ValidateScriptField(const ScriptField& field, const std::string& context)
		{
			if (!IsFieldID(field.FieldID))
				throw std::runtime_error(context
					+ ".FieldID must contain exactly 32 lowercase hexadecimal characters");
			if (field.Name.empty())
				throw std::runtime_error(context + ".Name cannot be empty");
			if (!IsScriptFieldValueCompatible(field.Type, field.Value))
				throw std::runtime_error(context + ".Value does not match Type "
					+ ScriptFieldTypeToString(field.Type));

			switch (field.Type)
			{
				case ScriptFieldType::Float:
					if (!IsFinite(std::get<float>(field.Value)))
						throw std::runtime_error(context + ".Value must be finite");
					break;
				case ScriptFieldType::Double:
					if (!IsFinite(std::get<double>(field.Value)))
						throw std::runtime_error(context + ".Value must be finite");
					break;
				case ScriptFieldType::Vector2:
					RequireFinite(std::get<glm::vec2>(field.Value), context + ".Value");
					break;
				case ScriptFieldType::Vector3:
					RequireFinite(std::get<glm::vec3>(field.Value), context + ".Value");
					break;
				case ScriptFieldType::Vector4:
					if (!IsFinite(std::get<glm::vec4>(field.Value)))
						throw std::runtime_error(context + ".Value must contain only finite numbers");
					break;
				case ScriptFieldType::Color:
					RequireUnitColor(std::get<glm::vec4>(field.Value), context + ".Value");
					break;
				default:
					break;
			}
		}

		void ValidateCSharpScripts(const CSharpScripts& scripts, const std::string& context)
		{
			std::unordered_set<UUID> attachmentIDs;
			for (size_t scriptIndex = 0; scriptIndex < scripts.Scripts.size(); ++scriptIndex)
			{
				const CSharpScriptEntry& script = scripts.Scripts[scriptIndex];
				const std::string scriptContext = context + ".Scripts["
					+ std::to_string(scriptIndex) + "]";
				if (static_cast<uint64_t>(script.AttachmentID) == 0)
					throw std::runtime_error(scriptContext + ".AttachmentID cannot be zero");
				if (!attachmentIDs.emplace(script.AttachmentID).second)
					throw std::runtime_error(scriptContext + ".AttachmentID is duplicated");

				std::unordered_set<std::string> fieldIDs;
				for (size_t fieldIndex = 0; fieldIndex < script.Fields.size(); ++fieldIndex)
				{
					const ScriptField& field = script.Fields[fieldIndex];
					const std::string fieldContext = scriptContext + ".Fields["
						+ std::to_string(fieldIndex) + "]";
					ValidateScriptField(field, fieldContext);
					if (!fieldIDs.emplace(field.FieldID).second)
						throw std::runtime_error(fieldContext + ".FieldID is duplicated");
				}
			}
		}

		YAML::Emitter& operator<<(YAML::Emitter& out, const glm::vec2& value)
		{
			out << YAML::Flow << YAML::BeginSeq << value.x << value.y << YAML::EndSeq;
			return out;
		}

		YAML::Emitter& operator<<(YAML::Emitter& out, const glm::vec3& value)
		{
			out << YAML::Flow << YAML::BeginSeq << value.x << value.y << value.z << YAML::EndSeq;
			return out;
		}

		YAML::Emitter& operator<<(YAML::Emitter& out, const glm::vec4& value)
		{
			out << YAML::Flow << YAML::BeginSeq << value.x << value.y << value.z << value.w << YAML::EndSeq;
			return out;
		}

		void SerializeScriptFieldValue(YAML::Emitter& out, const ScriptField& field)
		{
			if (!IsScriptFieldValueCompatible(field.Type, field.Value))
				throw std::runtime_error("C# script field '" + field.Name
					+ "' has a value incompatible with its declared type");

			switch (field.Type)
			{
				case ScriptFieldType::Bool: out << std::get<bool>(field.Value); break;
				case ScriptFieldType::Int32: out << std::get<int32_t>(field.Value); break;
				case ScriptFieldType::Int64:
				case ScriptFieldType::Enum: out << std::get<int64_t>(field.Value); break;
				case ScriptFieldType::Float: out << std::get<float>(field.Value); break;
				case ScriptFieldType::Double: out << std::get<double>(field.Value); break;
				case ScriptFieldType::String: out << std::get<std::string>(field.Value); break;
				case ScriptFieldType::Vector2: out << std::get<glm::vec2>(field.Value); break;
				case ScriptFieldType::Vector3: out << std::get<glm::vec3>(field.Value); break;
				case ScriptFieldType::Vector4:
				case ScriptFieldType::Color: out << std::get<glm::vec4>(field.Value); break;
				case ScriptFieldType::Entity:
				case ScriptFieldType::AssetRef: out << std::get<uint64_t>(field.Value); break;
			}
		}

		void RequireMap(const YAML::Node& node, const std::string& context)
		{
			if (!node || !node.IsMap())
				throw std::runtime_error(context + " must be a map");
		}

		bool ContainsField(std::initializer_list<const char*> fields, const std::string& candidate)
		{
			for (const char* field : fields)
			{
				if (candidate == field)
					return true;
			}
			return false;
		}

		void RequireExactFields(const YAML::Node& node, const std::string& context,
			std::initializer_list<const char*> requiredFields,
			std::initializer_list<const char*> optionalFields = {})
		{
			RequireMap(node, context);
			std::unordered_set<std::string> seenFields;
			for (const auto& entry : node)
			{
				if (!entry.first.IsScalar())
					throw std::runtime_error(context + " contains a non-scalar field name");
				const std::string field = entry.first.as<std::string>();
				if (!seenFields.emplace(field).second)
					throw std::runtime_error(context + " contains duplicate field '" + field + "'");
				if (!ContainsField(requiredFields, field) && !ContainsField(optionalFields, field))
					throw std::runtime_error(context + " contains unknown field '" + field + "'");
			}

			for (const char* field : requiredFields)
			{
				if (!node[field])
					throw std::runtime_error(context + " is missing required field '" + field + "'");
			}
		}

		template<typename T>
		T ReadRequired(const YAML::Node& node, const char* key, const std::string& context)
		{
			RequireMap(node, context);
			const YAML::Node value = node[key];
			if (!value)
				throw std::runtime_error(context + " is missing required field '" + key + "'");
			return value.as<T>();
		}

		ScriptFieldValue ReadScriptFieldValue(const YAML::Node& fieldNode,
			ScriptFieldType type, const std::string& context)
		{
			switch (type)
			{
				case ScriptFieldType::Bool:
					return ReadRequired<bool>(fieldNode, "Value", context);
				case ScriptFieldType::Int32:
					return ReadRequired<int32_t>(fieldNode, "Value", context);
				case ScriptFieldType::Int64:
				case ScriptFieldType::Enum:
					return ReadRequired<int64_t>(fieldNode, "Value", context);
				case ScriptFieldType::Float:
					return ReadRequired<float>(fieldNode, "Value", context);
				case ScriptFieldType::Double:
					return ReadRequired<double>(fieldNode, "Value", context);
				case ScriptFieldType::String:
					return ReadRequired<std::string>(fieldNode, "Value", context);
				case ScriptFieldType::Vector2:
					return ReadRequired<glm::vec2>(fieldNode, "Value", context);
				case ScriptFieldType::Vector3:
					return ReadRequired<glm::vec3>(fieldNode, "Value", context);
				case ScriptFieldType::Vector4:
				case ScriptFieldType::Color:
					return ReadRequired<glm::vec4>(fieldNode, "Value", context);
				case ScriptFieldType::Entity:
				case ScriptFieldType::AssetRef:
					return ReadRequired<uint64_t>(fieldNode, "Value", context);
			}
			throw std::runtime_error(context + ".Type is invalid");
		}

		std::string Rigidbody2DBodyTypeToString(Rigidbody2D::BodyType bodyType)
		{
			switch (bodyType)
			{
				case Rigidbody2D::BodyType::Static: return "Static";
				case Rigidbody2D::BodyType::Dynamic: return "Dynamic";
				case Rigidbody2D::BodyType::Kinematic: return "Kinematic";
			}
			throw std::runtime_error("Cannot serialize an unknown Rigidbody2D body type");
		}

		Rigidbody2D::BodyType Rigidbody2DBodyTypeFromString(const std::string& value)
		{
			if (value == "Static") return Rigidbody2D::BodyType::Static;
			if (value == "Dynamic") return Rigidbody2D::BodyType::Dynamic;
			if (value == "Kinematic") return Rigidbody2D::BodyType::Kinematic;
			throw std::runtime_error("Unknown Rigidbody2D body type '" + value + "'");
		}

		const char* EntityIconModeToString(EntityIconMode value)
		{
			switch (value)
			{
				case EntityIconMode::Automatic: return "Automatic";
				case EntityIconMode::Entity: return "Entity";
				case EntityIconMode::Camera: return "Camera";
				case EntityIconMode::Sprite: return "Sprite";
				case EntityIconMode::Rigidbody2D: return "Rigidbody2D";
				case EntityIconMode::Collider2D: return "Collider2D";
			}
			throw std::runtime_error("Cannot serialize an unknown Entity icon mode");
		}

		EntityIconMode EntityIconModeFromString(const std::string& value)
		{
			if (value == "Automatic") return EntityIconMode::Automatic;
			if (value == "Entity") return EntityIconMode::Entity;
			if (value == "Camera") return EntityIconMode::Camera;
			if (value == "Sprite") return EntityIconMode::Sprite;
			if (value == "Rigidbody2D") return EntityIconMode::Rigidbody2D;
			if (value == "Collider2D") return EntityIconMode::Collider2D;
			throw std::runtime_error("Unknown Entity icon mode '" + value + "'");
		}

		void SerializeEntity(YAML::Emitter& out, Scene* scene, Entity entity)
		{
			const std::string context = "Entity " + std::to_string(static_cast<uint64_t>(entity.GetUUID()));
			out << YAML::BeginMap;
			out << YAML::Key << "Entity" << YAML::Value << entity.GetUUID();

			auto& tag = entity.GetComponent<Tag>();
			out << YAML::Key << "Tag" << YAML::Value << YAML::BeginMap;
			out << YAML::Key << "Tag" << YAML::Value << tag._Tag;
			out << YAML::Key << "Visible" << YAML::Value << tag.Visible;
			out << YAML::EndMap;

			auto& metadata = entity.GetComponent<EntityMetadata>();
			ValidateEntityMetadata(metadata, context + ".EntityMetadata");
			out << YAML::Key << "EntityMetadata" << YAML::Value << YAML::BeginMap;
			out << YAML::Key << "GameplayTag" << YAML::Value << metadata.GameplayTag;
			out << YAML::Key << "Layer" << YAML::Value
				<< static_cast<uint32_t>(metadata.Layer);
			out << YAML::Key << "HierarchyIcon" << YAML::Value
				<< EntityIconModeToString(metadata.HierarchyIcon);
			out << YAML::EndMap;

			auto& transform = entity.GetComponent<Transform>();
			ValidateTransform(transform, context + ".Transform");
			out << YAML::Key << "Transform" << YAML::Value << YAML::BeginMap;
			out << YAML::Key << "Translation" << YAML::Value << transform._Translation;
			out << YAML::Key << "Rotation" << YAML::Value << transform._Rotation;
			out << YAML::Key << "Scale" << YAML::Value << transform._Scale;
			out << YAML::EndMap;

			out << YAML::Key << "LocalTransform" << YAML::Value << YAML::BeginMap;
			out << YAML::Key << "Translation" << YAML::Value << transform._LocalTranslation;
			out << YAML::Key << "Rotation" << YAML::Value << transform._LocalRotation;
			out << YAML::Key << "Scale" << YAML::Value << transform._LocalScale;
			out << YAML::EndMap;

			if (entity.HasComponent<C_Camera>())
			{
				auto& cameraComponent = entity.GetComponent<C_Camera>();
				ValidateCamera(cameraComponent, context + ".Camera");
				auto& camera = cameraComponent._Camera;
				out << YAML::Key << "Camera" << YAML::Value << YAML::BeginMap;
				out << YAML::Key << "Camera" << YAML::Value << YAML::BeginMap;
				out << YAML::Key << "ProjectionType" << YAML::Value << static_cast<int>(camera.GetProjectionType());
				out << YAML::Key << "PerspectiveFOV" << YAML::Value << camera.GetPerspectiveVerticalFOV();
				out << YAML::Key << "PerspectiveNear" << YAML::Value << camera.GetPerspectiveNearClip();
				out << YAML::Key << "PerspectiveFar" << YAML::Value << camera.GetPerspectiveFarClip();
				out << YAML::Key << "OrthographicSize" << YAML::Value << camera.GetOrthographicSize();
				out << YAML::Key << "OrthographicNear" << YAML::Value << camera.GetOrthographicNearClip();
				out << YAML::Key << "OrthographicFar" << YAML::Value << camera.GetOrthographicFarClip();
				out << YAML::EndMap;
				out << YAML::Key << "Primary" << YAML::Value << cameraComponent.Primary;
				out << YAML::Key << "FixedAspectRatio" << YAML::Value << cameraComponent.FixedAspectRatio;
				out << YAML::Key << "BackgroundColor" << YAML::Value << cameraComponent.BackgroundColor;
				out << YAML::EndMap;
			}

			if (entity.HasComponent<SpriteRenderer>())
			{
				auto& sprite = entity.GetComponent<SpriteRenderer>();
				ValidateSprite(sprite, context + ".SpriteRenderer");
				if (sprite.Sprite && static_cast<uint64_t>(sprite.SpriteHandle) == 0)
					throw std::runtime_error(context +
						".SpriteRenderer has a resolved sprite but no AssetHandle");
				out << YAML::Key << "SpriteRenderer" << YAML::Value << YAML::BeginMap;
				out << YAML::Key << "Enabled" << YAML::Value << sprite.Enabled;
				out << YAML::Key << "SpriteHandle" << YAML::Value
					<< static_cast<uint64_t>(sprite.SpriteHandle);
				out << YAML::Key << "Color" << YAML::Value << sprite._Color;
				out << YAML::Key << "TilingFactor" << YAML::Value << sprite.TilingFactor;
				out << YAML::EndMap;
			}

			if (entity.HasComponent<LineRenderer>())
			{
				auto& line = entity.GetComponent<LineRenderer>();
				ValidateLine(line, context + ".LineRenderer");
				out << YAML::Key << "LineRenderer" << YAML::Value << YAML::BeginMap;
				out << YAML::Key << "Enabled" << YAML::Value << line.Enabled;
				out << YAML::Key << "Color" << YAML::Value << line._Color;
				out << YAML::Key << "Start" << YAML::Value << line.Start;
				out << YAML::Key << "End" << YAML::Value << line.End;
				out << YAML::Key << "Width" << YAML::Value << line.Width;
				out << YAML::EndMap;
			}

			if (entity.HasComponent<CSharpScripts>())
			{
				const auto& scripts = entity.GetComponent<CSharpScripts>();
				ValidateCSharpScripts(scripts, context + ".CSharpScripts");
				out << YAML::Key << "CSharpScripts" << YAML::Value << YAML::BeginMap;
				out << YAML::Key << "Scripts" << YAML::Value << YAML::BeginSeq;
				for (const CSharpScriptEntry& script : scripts.Scripts)
				{
					out << YAML::BeginMap;
					out << YAML::Key << "AttachmentID" << YAML::Value
						<< static_cast<uint64_t>(script.AttachmentID);
					out << YAML::Key << "Enabled" << YAML::Value << script.Enabled;
					out << YAML::Key << "ScriptHandle" << YAML::Value
						<< static_cast<uint64_t>(script.ScriptAsset);
					out << YAML::Key << "ClassName" << YAML::Value
						<< script.LastKnownClassName;
					out << YAML::Key << "Fields" << YAML::Value << YAML::BeginSeq;
					for (const ScriptField& field : script.Fields)
					{
						out << YAML::BeginMap;
						out << YAML::Key << "FieldID" << YAML::Value << field.FieldID;
						out << YAML::Key << "Name" << YAML::Value << field.Name;
						out << YAML::Key << "Type" << YAML::Value
							<< ScriptFieldTypeToString(field.Type);
						out << YAML::Key << "TypeName" << YAML::Value << field.TypeName;
						out << YAML::Key << "Value" << YAML::Value;
						SerializeScriptFieldValue(out, field);
						out << YAML::EndMap;
					}
					out << YAML::EndSeq;
					out << YAML::EndMap;
				}
				out << YAML::EndSeq;
				out << YAML::EndMap;
			}

			if (entity.HasComponent<Rigidbody2D>())
			{
				auto& rigidbody = entity.GetComponent<Rigidbody2D>();
				out << YAML::Key << "Rigidbody2D" << YAML::Value << YAML::BeginMap;
				out << YAML::Key << "Enabled" << YAML::Value << rigidbody.Enabled;
				out << YAML::Key << "BodyType" << YAML::Value << Rigidbody2DBodyTypeToString(rigidbody.Type);
				out << YAML::Key << "FixedRotation" << YAML::Value << rigidbody.FixedRotation;
				out << YAML::EndMap;
			}

			if (entity.HasComponent<BoxCollider2D>())
			{
				auto& collider = entity.GetComponent<BoxCollider2D>();
				ValidateCollider(collider, context + ".BoxCollider2D");
				out << YAML::Key << "BoxCollider2D" << YAML::Value << YAML::BeginMap;
				out << YAML::Key << "Enabled" << YAML::Value << collider.Enabled;
				out << YAML::Key << "IsTrigger" << YAML::Value << collider.IsTrigger;
				out << YAML::Key << "CollisionLayer" << YAML::Value << collider.CollisionLayer;
				out << YAML::Key << "CollisionMask" << YAML::Value << collider.CollisionMask;
				out << YAML::Key << "Offset" << YAML::Value << collider.Offset;
				out << YAML::Key << "Size" << YAML::Value << collider.Size;
				out << YAML::Key << "Density" << YAML::Value << collider.Density;
				out << YAML::Key << "Friction" << YAML::Value << collider.Friction;
				out << YAML::Key << "Restitution" << YAML::Value << collider.Restitution;
				out << YAML::Key << "RestitutionThreshold" << YAML::Value << collider.RestitutionThreshold;
				out << YAML::EndMap;
			}

			if (entity.HasComponent<CircleCollider2D>())
			{
				auto& collider = entity.GetComponent<CircleCollider2D>();
				ValidateCollider(collider, context + ".CircleCollider2D");
				out << YAML::Key << "CircleCollider2D" << YAML::Value << YAML::BeginMap;
				out << YAML::Key << "Enabled" << YAML::Value << collider.Enabled;
				out << YAML::Key << "IsTrigger" << YAML::Value << collider.IsTrigger;
				out << YAML::Key << "CollisionLayer" << YAML::Value << collider.CollisionLayer;
				out << YAML::Key << "CollisionMask" << YAML::Value << collider.CollisionMask;
				out << YAML::Key << "Offset" << YAML::Value << collider.Offset;
				out << YAML::Key << "Radius" << YAML::Value << collider.Radius;
				out << YAML::Key << "Density" << YAML::Value << collider.Density;
				out << YAML::Key << "Friction" << YAML::Value << collider.Friction;
				out << YAML::Key << "Restitution" << YAML::Value << collider.Restitution;
				out << YAML::EndMap;
			}

			if (entity.HasComponent<DistanceJoint2D>())
			{
				auto& joint = entity.GetComponent<DistanceJoint2D>();
				ValidateJoint(joint, context + ".DistanceJoint2D");
				if (static_cast<uint64_t>(joint.ConnectedEntity) != 0)
				{
					if (joint.ConnectedEntity == entity.GetUUID())
						throw std::runtime_error(context
							+ ".DistanceJoint2D cannot connect the entity to itself");
					if (!scene->FindEntityByUUID(joint.ConnectedEntity))
						throw std::runtime_error(context
							+ ".DistanceJoint2D references missing ConnectedEntity "
							+ std::to_string(static_cast<uint64_t>(joint.ConnectedEntity)));
				}
				out << YAML::Key << "DistanceJoint2D" << YAML::Value << YAML::BeginMap;
				out << YAML::Key << "Enabled" << YAML::Value << joint.Enabled;
				out << YAML::Key << "ConnectedEntity" << YAML::Value
					<< static_cast<uint64_t>(joint.ConnectedEntity);
				out << YAML::Key << "Anchor" << YAML::Value << joint.Anchor;
				out << YAML::Key << "ConnectedAnchor" << YAML::Value << joint.ConnectedAnchor;
				out << YAML::Key << "Distance" << YAML::Value << joint.Distance;
				out << YAML::Key << "Frequency" << YAML::Value << joint.Frequency;
				out << YAML::Key << "Damping" << YAML::Value << joint.Damping;
				out << YAML::Key << "CollideConnected" << YAML::Value << joint.CollideConnected;
				out << YAML::EndMap;
			}

			Entity parent = scene->GetParent(entity);
			out << YAML::Key << "Parent" << YAML::Value << (parent ? static_cast<uint64_t>(parent.GetUUID()) : 0ULL);
			out << YAML::EndMap;
		}

	}

	SceneSerializer::SceneSerializer(const Ref<Scene>& scene)
		: m_Scene(scene)
	{
	}

	bool SceneSerializer::SerializeDocument(std::string& document,
		std::string& error) const
	{
		document.clear();
		error.clear();
		if (!m_Scene)
		{
			error = "Cannot serialize a null scene";
			return false;
		}

		try
		{
			if (m_Scene->m_EntityMap.size() != m_Scene->m_EntityOrder.size())
				throw std::runtime_error(
					"Scene entity index and serialization order are inconsistent");

			std::unordered_set<UUID> serializedUUIDs;
			std::unordered_set<UUID> serializedAttachmentIDs;
			for (UUID uuid : m_Scene->m_EntityOrder)
			{
				Entity entity = m_Scene->FindEntityByUUID(uuid);
				if ((uint64_t)uuid == 0 || !entity || !entity.HasComponent<ID>()
					|| !entity.HasComponent<Tag>() || !entity.HasComponent<EntityMetadata>()
					|| !entity.HasComponent<Transform>() || entity.GetUUID() != uuid
					|| !serializedUUIDs.emplace(uuid).second)
					throw std::runtime_error("Scene contains an invalid or duplicate UUID "
						+ std::to_string(static_cast<uint64_t>(uuid)));

				if (!entity.HasComponent<CSharpScripts>())
					continue;
				for (const CSharpScriptEntry& script :
					entity.GetComponent<CSharpScripts>().Scripts)
				{
					if (static_cast<uint64_t>(script.AttachmentID) != 0
						&& !serializedAttachmentIDs.emplace(script.AttachmentID).second)
						throw std::runtime_error("Scene contains duplicate C# AttachmentID "
							+ std::to_string(static_cast<uint64_t>(script.AttachmentID)));
				}
			}
			if (!m_Scene->ValidateTransformHierarchy())
				throw std::runtime_error(
					"Scene transform hierarchy cannot be synchronized losslessly as TRS values");

			YAML::Emitter out;
			out << YAML::BeginMap;
			out << YAML::Key << "SchemaVersion" << YAML::Value
				<< SceneSerializer::CurrentSchemaVersion;
			out << YAML::Key << "SceneName" << YAML::Value << m_Scene->GetSceneName();
			out << YAML::Key << "Entities" << YAML::Value << YAML::BeginSeq;
			for (UUID uuid : m_Scene->m_EntityOrder)
				SerializeEntity(out, m_Scene.get(), m_Scene->FindEntityByUUID(uuid));
			out << YAML::EndSeq << YAML::EndMap;
			if (!out.good())
				throw std::runtime_error(out.GetLastError());
			document.assign(out.c_str());
			return true;
		}
		catch (const std::exception& exception)
		{
			error = exception.what();
			return false;
		}
	}

	bool SceneSerializer::Serialize(const std::filesystem::path& filepath)
	{
		if (!m_Scene)
		{
			TC_Core_Error("Cannot serialize a null scene");
			return false;
		}
		if (AssetTypeFromPath(filepath) != AssetType::Scene)
		{
			TC_Core_Error("Scene files must use the .tomcat extension: {0}",
				PathToUTF8(filepath));
			return false;
		}
		std::error_code pathError;
		const bool pathExists = std::filesystem::exists(filepath, pathError);
		if (pathError)
		{
			TC_Core_Error("Scene destination is not a regular file path: {0}",
				PathToUTF8(filepath));
			return false;
		}
		if (pathExists)
		{
			pathError.clear();
			const bool isRegularFile = std::filesystem::is_regular_file(filepath, pathError);
			if (pathError || !isRegularFile)
			{
				TC_Core_Error("Scene destination is not a regular file path: {0}",
					PathToUTF8(filepath));
				return false;
			}
		}
		AssetManager& assetManager = AssetManager::Get();
		if (assetManager.GetRegistry().IsInitialized() &&
			!assetManager.GetRegistry().IsManagedPath(filepath, true))
		{
			TC_Core_Error("Scenes in an active project must be saved inside Assets: {0}",
				PathToUTF8(filepath));
			return false;
		}
		std::string canonicalDocument;
		std::string canonicalError;
		if (!SceneArchiveCodec::Encode(m_Scene, canonicalDocument, canonicalError))
		{
			TC_Core_Error("Failed to encode scene '{0}': {1}",
				PathToUTF8(filepath), canonicalError);
			return false;
		}

		try
		{
			std::string writeError;
			if (FileSystem::WriteFileAtomically(filepath, canonicalDocument, writeError))
			{
				if (assetManager.GetRegistry().IsInitialized())
				{
					const AssetHandle sceneHandle = assetManager.ImportAsset(filepath);
					const AssetMetadata* metadata = assetManager.GetRegistry().GetMetadata(sceneHandle);
					if (static_cast<uint64_t>(sceneHandle) == 0 || !metadata ||
						metadata->IsMissing || metadata->Type != AssetType::Scene)
					{
						TC_Core_Error("Scene was written but could not be registered as an asset: {0}",
							PathToUTF8(filepath));
						return false;
					}
				}
				return true;
			}
			TC_Core_Error("Could not atomically replace scene '{0}': {1}", PathToUTF8(filepath), writeError);
			return false;
		}
		catch (const std::exception& error)
		{
			TC_Core_Error("Failed to serialize scene '{0}': {1}", PathToUTF8(filepath), error.what());
			return false;
		}
	}

	bool SceneSerializer::Deserialize(const std::filesystem::path& filepath)
	{
		std::ifstream input(filepath, std::ios::binary);
		if (!input)
		{
			TC_Core_Error("Failed to deserialize scene '{0}': Could not open the scene file",
				PathToUTF8(filepath));
			return false;
		}
		const bool deserialized = DeserializeStream(input, filepath, true);
		if (deserialized && AssetManager::Get().IsInitialized())
			m_Scene->SetPhysics2DSettings(AssetManager::Get().GetPhysics2DSettings());
		return deserialized;
	}

	bool SceneSerializer::DeserializeDocument(const std::vector<uint8_t>& bytes,
		const std::filesystem::path& diagnosticPath, bool resolveAssets)
	{
		std::string serialized(bytes.begin(), bytes.end());
		std::istringstream input(std::move(serialized));
		return DeserializeStream(input, diagnosticPath, resolveAssets);
	}

	bool SceneSerializer::ValidateCurrentFormat(const std::filesystem::path& filepath)
	{
		std::ifstream input(filepath, std::ios::binary | std::ios::ate);
		if (!input)
		{
			TC_Core_Error("Failed to validate scene '{0}': Could not open the scene file",
				PathToUTF8(filepath));
			return false;
		}
		const std::streamoff end = input.tellg();
		if (end < 0 || static_cast<uint64_t>(end) >
			static_cast<uint64_t>((std::numeric_limits<size_t>::max)()))
		{
			TC_Core_Error("Failed to validate scene '{0}': Scene file is too large",
				PathToUTF8(filepath));
			return false;
		}
		std::vector<uint8_t> bytes(static_cast<size_t>(end));
		input.seekg(0, std::ios::beg);
		if (!bytes.empty() && !input.read(reinterpret_cast<char*>(bytes.data()),
			static_cast<std::streamsize>(bytes.size())))
		{
			TC_Core_Error("Failed to validate scene '{0}': Could not read the scene file",
				PathToUTF8(filepath));
			return false;
		}
		return ValidateCurrentFormat(bytes, filepath);
	}

	bool SceneSerializer::ValidateCurrentFormat(const std::vector<uint8_t>& bytes,
		const std::filesystem::path& diagnosticPath)
	{
		return SceneArchiveCodec::Decode(bytes, CreateRef<Scene>(), diagnosticPath,
			false);
	}

	bool SceneSerializer::Deserialize(AssetHandle handle)
	{
		AssetManager& assetManager = AssetManager::Get();
		if (!assetManager.IsCookedPackageMounted() || static_cast<uint64_t>(handle) == 0)
		{
			TC_Core_Error("A nonzero Scene AssetHandle from a mounted cooked package is required");
			return false;
		}

		AssetType type = AssetType::None;
		std::vector<uint8_t> bytes;
		if (!assetManager.ReadAssetBytes(handle, bytes, &type) || type != AssetType::Scene)
		{
			TC_Core_Error("Cooked asset {0} is unavailable or is not a Scene",
				static_cast<uint64_t>(handle));
			return false;
		}

		try
		{
			const bool deserialized = SceneArchiveCodec::Decode(bytes, m_Scene,
				UTF8ToPath("CookedScene-" + std::to_string(
					static_cast<uint64_t>(handle))), true);
			if (deserialized)
				m_Scene->SetPhysics2DSettings(assetManager.GetPhysics2DSettings());
			return deserialized;
		}
		catch (const std::exception& error)
		{
			TC_Core_Error("Failed to stage cooked scene {0} for deserialization: {1}",
				static_cast<uint64_t>(handle), error.what());
			return false;
		}
	}

	bool SceneSerializer::DeserializeStream(std::istream& input,
		const std::filesystem::path& filepath, bool resolveAssets)
	{
		if (!m_Scene)
		{
			TC_Core_Error("Cannot deserialize into a null scene");
			return false;
		}

		try
		{
			YAML::Node data = YAML::Load(input);
			if (input.bad())
				throw std::runtime_error("Failed while reading the scene file");
			RequireExactFields(data, "scene document",
				{ "SchemaVersion", "SceneName", "Entities" });

			const uint32_t schemaVersion = ReadRequired<uint32_t>(
				data, "SchemaVersion", "scene document");
			if (schemaVersion != OldestSupportedSchemaVersion
				&& schemaVersion != CurrentSchemaVersion)
				throw std::runtime_error("Scene SchemaVersion must be "
					+ std::to_string(OldestSupportedSchemaVersion) + " or "
					+ std::to_string(CurrentSchemaVersion) + ", got "
					+ std::to_string(schemaVersion));

			const std::string sceneName = ReadRequired<std::string>(
				data, "SceneName", "scene document");
			if (sceneName.empty())
				throw std::runtime_error("SceneName cannot be empty");

			YAML::Node entities = data["Entities"];
			if (!entities || !entities.IsSequence())
				throw std::runtime_error("Scene field 'Entities' must be a sequence");

			Ref<Scene> parsedScene = CreateRef<Scene>();
			parsedScene->SetSceneName(sceneName);
			parsedScene->m_ViewportWidth = m_Scene->m_ViewportWidth;
			parsedScene->m_ViewportHeight = m_Scene->m_ViewportHeight;

			std::vector<std::pair<UUID, UUID>> pendingParents;
			std::vector<std::pair<UUID, UUID>> pendingJointConnections;
			std::unordered_set<UUID> seenUUIDs;
			std::unordered_set<UUID> seenAttachmentIDs;

			for (std::size_t index = 0; index < entities.size(); ++index)
			{
				const YAML::Node entityNode = entities[index];
				const std::string context = "Entities[" + std::to_string(index) + "]";
				if (schemaVersion == CurrentSchemaVersion)
				{
					RequireExactFields(entityNode, context,
						{ "Entity", "Tag", "EntityMetadata", "Transform", "LocalTransform", "Parent" },
						{ "Camera", "SpriteRenderer", "LineRenderer", "CSharpScripts",
							"Rigidbody2D", "BoxCollider2D", "CircleCollider2D",
							"DistanceJoint2D" });
				}
				else
				{
					// Schema 9 is accepted only as a migration input and did not
					// define CSharpScripts. Saving the loaded scene always emits 10.
					RequireExactFields(entityNode, context,
						{ "Entity", "Tag", "EntityMetadata", "Transform", "LocalTransform", "Parent" },
						{ "Camera", "SpriteRenderer", "LineRenderer", "Rigidbody2D",
							"BoxCollider2D", "CircleCollider2D", "DistanceJoint2D" });
				}

				const uint64_t rawUUID = ReadRequired<uint64_t>(entityNode, "Entity", context);
				const UUID uuid(rawUUID);
				if (rawUUID == 0)
					throw std::runtime_error(context + " uses reserved UUID 0");
				if (!seenUUIDs.emplace(uuid).second)
					throw std::runtime_error(context + " duplicates UUID " + std::to_string(rawUUID));

				YAML::Node tagNode = entityNode["Tag"];
				RequireExactFields(tagNode, context + ".Tag", { "Tag", "Visible" });
				const std::string name = ReadRequired<std::string>(tagNode, "Tag", context + ".Tag");
				const bool visible = ReadRequired<bool>(tagNode, "Visible", context + ".Tag");

				Entity entity = parsedScene->CreateEntityWithUUID(uuid, name);
				if (!entity)
					throw std::runtime_error(context + " could not be created");
				entity.GetComponent<Tag>().Visible = visible;

				YAML::Node metadataNode = entityNode["EntityMetadata"];
				RequireExactFields(metadataNode, context + ".EntityMetadata",
					{ "GameplayTag", "Layer", "HierarchyIcon" });
				auto& metadata = entity.GetComponent<EntityMetadata>();
				metadata.GameplayTag = ReadRequired<std::string>(metadataNode,
					"GameplayTag", context + ".EntityMetadata");
				const uint32_t rawLayer = ReadRequired<uint32_t>(metadataNode,
					"Layer", context + ".EntityMetadata");
				if (rawLayer >= Physics2DLayerCount)
					throw std::runtime_error(context + ".EntityMetadata.Layer must be in [0, 15]");
				metadata.Layer = static_cast<uint8_t>(rawLayer);
				metadata.HierarchyIcon = EntityIconModeFromString(ReadRequired<std::string>(
					metadataNode, "HierarchyIcon", context + ".EntityMetadata"));
				ValidateEntityMetadata(metadata, context + ".EntityMetadata");

				YAML::Node transformNode = entityNode["Transform"];
				RequireExactFields(transformNode, context + ".Transform",
					{ "Translation", "Rotation", "Scale" });
				auto& transform = entity.GetComponent<Transform>();
				transform._Translation = ReadRequired<glm::vec3>(transformNode, "Translation", context + ".Transform");
				transform._Rotation = ReadRequired<glm::vec3>(transformNode, "Rotation", context + ".Transform");
				transform._Scale = ReadRequired<glm::vec3>(transformNode, "Scale", context + ".Transform");

				YAML::Node localTransformNode = entityNode["LocalTransform"];
				RequireExactFields(localTransformNode, context + ".LocalTransform",
					{ "Translation", "Rotation", "Scale" });
				transform._LocalTranslation = ReadRequired<glm::vec3>(localTransformNode, "Translation", context + ".LocalTransform");
				transform._LocalRotation = ReadRequired<glm::vec3>(localTransformNode, "Rotation", context + ".LocalTransform");
				transform._LocalScale = ReadRequired<glm::vec3>(localTransformNode, "Scale", context + ".LocalTransform");
				ValidateTransform(entity.GetComponent<Transform>(), context + ".Transform");

				YAML::Node cameraNode = entityNode["Camera"];
				if (cameraNode)
				{
					RequireExactFields(cameraNode, context + ".Camera",
						{ "Camera", "Primary", "FixedAspectRatio", "BackgroundColor" });
					YAML::Node properties = cameraNode["Camera"];
					RequireExactFields(properties, context + ".Camera.Camera",
						{ "ProjectionType", "PerspectiveFOV", "PerspectiveNear", "PerspectiveFar",
							"OrthographicSize", "OrthographicNear", "OrthographicFar" });
					const int projectionType = ReadRequired<int>(properties, "ProjectionType", context + ".Camera.Camera");
					if (projectionType < static_cast<int>(SceneCamera::ProjectionType::Perspective)
						|| projectionType > static_cast<int>(SceneCamera::ProjectionType::Orthographic))
						throw std::runtime_error(context + ".Camera has an invalid ProjectionType");

					const float perspectiveFov = ReadRequired<float>(properties, "PerspectiveFOV", context + ".Camera.Camera");
					const float perspectiveNear = ReadRequired<float>(properties, "PerspectiveNear", context + ".Camera.Camera");
					const float perspectiveFar = ReadRequired<float>(properties, "PerspectiveFar", context + ".Camera.Camera");
					const float orthographicSize = ReadRequired<float>(properties, "OrthographicSize", context + ".Camera.Camera");
					const float orthographicNear = ReadRequired<float>(properties, "OrthographicNear", context + ".Camera.Camera");
					const float orthographicFar = ReadRequired<float>(properties, "OrthographicFar", context + ".Camera.Camera");
					const bool primary = ReadRequired<bool>(cameraNode, "Primary", context + ".Camera");
					const bool fixedAspectRatio = ReadRequired<bool>(
						cameraNode, "FixedAspectRatio", context + ".Camera");
					const glm::vec4 backgroundColor = ReadRequired<glm::vec4>(
						cameraNode, "BackgroundColor", context + ".Camera");
					ValidateCameraValues(perspectiveFov, perspectiveNear, perspectiveFar,
						orthographicSize, orthographicNear, orthographicFar, backgroundColor, context + ".Camera");

					auto& camera = entity.AddComponent<C_Camera>();
					if (!camera._Camera.SetPerspective(perspectiveFov, perspectiveNear, perspectiveFar)
						|| !camera._Camera.SetOrthographic(orthographicSize, orthographicNear, orthographicFar)
						|| !camera._Camera.SetProjectionType(static_cast<SceneCamera::ProjectionType>(projectionType)))
						throw std::runtime_error(context + ".Camera produces a non-finite projection matrix");
					camera.Primary = primary;
					camera.FixedAspectRatio = fixedAspectRatio;
					camera.BackgroundColor = backgroundColor;
				}

				YAML::Node spriteNode = entityNode["SpriteRenderer"];
				if (spriteNode)
				{
					RequireExactFields(spriteNode, context + ".SpriteRenderer",
						{ "Enabled", "SpriteHandle", "Color", "TilingFactor" });
					auto& sprite = entity.AddComponent<SpriteRenderer>();
					sprite.Enabled = ReadRequired<bool>(spriteNode, "Enabled", context + ".SpriteRenderer");
					const uint64_t rawSpriteHandle = ReadRequired<uint64_t>(
						spriteNode, "SpriteHandle", context + ".SpriteRenderer");
					sprite.SpriteHandle = AssetHandle(rawSpriteHandle);
					sprite._Color = ReadRequired<glm::vec4>(spriteNode, "Color", context + ".SpriteRenderer");
					sprite.TilingFactor = ReadRequired<float>(spriteNode, "TilingFactor", context + ".SpriteRenderer");
					ValidateSprite(sprite, context + ".SpriteRenderer");
					if (resolveAssets && rawSpriteHandle != 0)
						sprite.Sprite = AssetManager::Get().LoadTexture(sprite.SpriteHandle);
				}

				YAML::Node lineNode = entityNode["LineRenderer"];
				if (lineNode)
				{
					RequireExactFields(lineNode, context + ".LineRenderer",
						{ "Enabled", "Color", "Start", "End", "Width" });
					auto& line = entity.AddComponent<LineRenderer>();
					line.Enabled = ReadRequired<bool>(lineNode, "Enabled", context + ".LineRenderer");
					line._Color = ReadRequired<glm::vec4>(lineNode, "Color", context + ".LineRenderer");
					line.Start = ReadRequired<glm::vec3>(lineNode, "Start", context + ".LineRenderer");
					line.End = ReadRequired<glm::vec3>(lineNode, "End", context + ".LineRenderer");
					line.Width = ReadRequired<float>(lineNode, "Width", context + ".LineRenderer");
					ValidateLine(line, context + ".LineRenderer");
				}

				YAML::Node csharpScriptsNode = entityNode["CSharpScripts"];
				if (csharpScriptsNode)
				{
					RequireExactFields(csharpScriptsNode, context + ".CSharpScripts",
						{ "Scripts" });
					const YAML::Node scriptsNode = csharpScriptsNode["Scripts"];
					if (!scriptsNode.IsSequence())
						throw std::runtime_error(context
							+ ".CSharpScripts.Scripts must be a sequence");

					auto& scripts = entity.AddComponent<CSharpScripts>();
					scripts.Scripts.reserve(scriptsNode.size());
					for (size_t scriptIndex = 0; scriptIndex < scriptsNode.size();
						++scriptIndex)
					{
						const YAML::Node scriptNode = scriptsNode[scriptIndex];
						const std::string scriptContext = context + ".CSharpScripts.Scripts["
							+ std::to_string(scriptIndex) + "]";
						RequireExactFields(scriptNode, scriptContext,
							{ "AttachmentID", "Enabled", "ScriptHandle", "ClassName",
								"Fields" });

						CSharpScriptEntry script;
						script.AttachmentID = UUID(ReadRequired<uint64_t>(scriptNode,
							"AttachmentID", scriptContext));
						if (static_cast<uint64_t>(script.AttachmentID) == 0
							|| !seenAttachmentIDs.emplace(script.AttachmentID).second)
							throw std::runtime_error(scriptContext
								+ ".AttachmentID must be nonzero and unique across the scene");
						script.Enabled = ReadRequired<bool>(scriptNode, "Enabled",
							scriptContext);
						script.ScriptAsset = AssetHandle(ReadRequired<uint64_t>(scriptNode,
							"ScriptHandle", scriptContext));
						script.LastKnownClassName = ReadRequired<std::string>(scriptNode,
							"ClassName", scriptContext);

						const YAML::Node fieldsNode = scriptNode["Fields"];
						if (!fieldsNode.IsSequence())
							throw std::runtime_error(scriptContext
								+ ".Fields must be a sequence");
						script.Fields.reserve(fieldsNode.size());
						for (size_t fieldIndex = 0; fieldIndex < fieldsNode.size();
							++fieldIndex)
						{
							const YAML::Node fieldNode = fieldsNode[fieldIndex];
							const std::string fieldContext = scriptContext + ".Fields["
								+ std::to_string(fieldIndex) + "]";
							RequireExactFields(fieldNode, fieldContext,
								{ "FieldID", "Name", "Type", "Value" },
								{ "TypeName" });

							ScriptField field;
							field.FieldID = ReadRequired<std::string>(fieldNode,
								"FieldID", fieldContext);
							field.Name = ReadRequired<std::string>(fieldNode, "Name",
								fieldContext);
							const std::string typeName = ReadRequired<std::string>(
								fieldNode, "Type", fieldContext);
							if (!TryParseScriptFieldType(typeName, field.Type))
								throw std::runtime_error(fieldContext
									+ ".Type contains unknown C# script field type '"
									+ typeName + "'");
							if (const YAML::Node managedType = fieldNode["TypeName"])
								field.TypeName = ReadRequired<std::string>(fieldNode,
									"TypeName", fieldContext);
							field.Value = ReadScriptFieldValue(fieldNode, field.Type,
								fieldContext);
							ValidateScriptField(field, fieldContext);
							script.Fields.emplace_back(std::move(field));
						}
						scripts.Scripts.emplace_back(std::move(script));
					}
					ValidateCSharpScripts(scripts, context + ".CSharpScripts");
				}

				YAML::Node rigidbodyNode = entityNode["Rigidbody2D"];
				if (rigidbodyNode)
				{
					RequireExactFields(rigidbodyNode, context + ".Rigidbody2D",
						{ "Enabled", "BodyType", "FixedRotation" });
					auto& rigidbody = entity.AddComponent<Rigidbody2D>();
					rigidbody.Enabled = ReadRequired<bool>(rigidbodyNode, "Enabled", context + ".Rigidbody2D");
					rigidbody.Type = Rigidbody2DBodyTypeFromString(ReadRequired<std::string>(rigidbodyNode, "BodyType", context + ".Rigidbody2D"));
					rigidbody.FixedRotation = ReadRequired<bool>(rigidbodyNode, "FixedRotation", context + ".Rigidbody2D");
				}

				YAML::Node colliderNode = entityNode["BoxCollider2D"];
				if (colliderNode)
				{
					RequireExactFields(colliderNode, context + ".BoxCollider2D",
						{ "Enabled", "IsTrigger", "CollisionLayer", "CollisionMask",
							"Offset", "Size", "Density", "Friction", "Restitution",
							"RestitutionThreshold" });
					auto& collider = entity.AddComponent<BoxCollider2D>();
					collider.Enabled = ReadRequired<bool>(colliderNode, "Enabled", context + ".BoxCollider2D");
					collider.IsTrigger = ReadRequired<bool>(colliderNode, "IsTrigger", context + ".BoxCollider2D");
					collider.CollisionLayer = ReadRequired<uint16_t>(colliderNode, "CollisionLayer", context + ".BoxCollider2D");
					collider.CollisionMask = ReadRequired<uint16_t>(colliderNode, "CollisionMask", context + ".BoxCollider2D");
					collider.Offset = ReadRequired<glm::vec2>(colliderNode, "Offset", context + ".BoxCollider2D");
					collider.Size = ReadRequired<glm::vec2>(colliderNode, "Size", context + ".BoxCollider2D");
					collider.Density = ReadRequired<float>(colliderNode, "Density", context + ".BoxCollider2D");
					collider.Friction = ReadRequired<float>(colliderNode, "Friction", context + ".BoxCollider2D");
					collider.Restitution = ReadRequired<float>(colliderNode, "Restitution", context + ".BoxCollider2D");
					collider.RestitutionThreshold = ReadRequired<float>(colliderNode, "RestitutionThreshold", context + ".BoxCollider2D");
					ValidateCollider(collider, context + ".BoxCollider2D");
				}

				YAML::Node circleColliderNode = entityNode["CircleCollider2D"];
				if (circleColliderNode)
				{
					RequireExactFields(circleColliderNode, context + ".CircleCollider2D",
						{ "Enabled", "IsTrigger", "CollisionLayer", "CollisionMask",
							"Offset", "Radius", "Density", "Friction", "Restitution" });
					auto& collider = entity.AddComponent<CircleCollider2D>();
					collider.Enabled = ReadRequired<bool>(circleColliderNode, "Enabled",
						context + ".CircleCollider2D");
					collider.IsTrigger = ReadRequired<bool>(circleColliderNode, "IsTrigger",
						context + ".CircleCollider2D");
					collider.CollisionLayer = ReadRequired<uint16_t>(circleColliderNode, "CollisionLayer",
						context + ".CircleCollider2D");
					collider.CollisionMask = ReadRequired<uint16_t>(circleColliderNode, "CollisionMask",
						context + ".CircleCollider2D");
					collider.Offset = ReadRequired<glm::vec2>(circleColliderNode, "Offset",
						context + ".CircleCollider2D");
					collider.Radius = ReadRequired<float>(circleColliderNode, "Radius",
						context + ".CircleCollider2D");
					collider.Density = ReadRequired<float>(circleColliderNode, "Density",
						context + ".CircleCollider2D");
					collider.Friction = ReadRequired<float>(circleColliderNode, "Friction",
						context + ".CircleCollider2D");
					collider.Restitution = ReadRequired<float>(circleColliderNode, "Restitution",
						context + ".CircleCollider2D");
					ValidateCollider(collider, context + ".CircleCollider2D");
				}

				YAML::Node distanceJointNode = entityNode["DistanceJoint2D"];
				if (distanceJointNode)
				{
					RequireExactFields(distanceJointNode, context + ".DistanceJoint2D",
						{ "Enabled", "ConnectedEntity", "Anchor", "ConnectedAnchor", "Distance",
							"Frequency", "Damping", "CollideConnected" });
					auto& joint = entity.AddComponent<DistanceJoint2D>();
					joint.Enabled = ReadRequired<bool>(distanceJointNode, "Enabled", context + ".DistanceJoint2D");
					joint.ConnectedEntity = UUID(ReadRequired<uint64_t>(distanceJointNode,
						"ConnectedEntity", context + ".DistanceJoint2D"));
					joint.Anchor = ReadRequired<glm::vec2>(distanceJointNode, "Anchor", context + ".DistanceJoint2D");
					joint.ConnectedAnchor = ReadRequired<glm::vec2>(distanceJointNode,
						"ConnectedAnchor", context + ".DistanceJoint2D");
					joint.Distance = ReadRequired<float>(distanceJointNode, "Distance", context + ".DistanceJoint2D");
					joint.Frequency = ReadRequired<float>(distanceJointNode, "Frequency", context + ".DistanceJoint2D");
					joint.Damping = ReadRequired<float>(distanceJointNode, "Damping", context + ".DistanceJoint2D");
					joint.CollideConnected = ReadRequired<bool>(distanceJointNode,
						"CollideConnected", context + ".DistanceJoint2D");
					ValidateJoint(joint, context + ".DistanceJoint2D");
					if (static_cast<uint64_t>(joint.ConnectedEntity) != 0)
						pendingJointConnections.emplace_back(uuid, joint.ConnectedEntity);
				}

				const uint64_t parentUUID = ReadRequired<uint64_t>(entityNode, "Parent", context);
				if (parentUUID != 0)
					pendingParents.emplace_back(uuid, UUID(parentUUID));
			}

			for (const auto& [ownerUUID, connectedUUID] : pendingJointConnections)
			{
				if (ownerUUID == connectedUUID)
					throw std::runtime_error("Entity " + std::to_string(static_cast<uint64_t>(ownerUUID))
						+ " cannot connect DistanceJoint2D to itself");
				if (!parsedScene->FindEntityByUUID(connectedUUID))
					throw std::runtime_error("Entity " + std::to_string(static_cast<uint64_t>(ownerUUID))
						+ " references missing DistanceJoint2D entity "
						+ std::to_string(static_cast<uint64_t>(connectedUUID)));
			}

			std::unordered_map<UUID, UUID> parentLookup;
			for (const auto& [childUUID, parentUUID] : pendingParents)
			{
				if (childUUID == parentUUID)
					throw std::runtime_error("Entity " + std::to_string((uint64_t)childUUID) + " cannot parent itself");
				if (!parsedScene->FindEntityByUUID(parentUUID))
					throw std::runtime_error("Entity " + std::to_string((uint64_t)childUUID)
						+ " references missing parent " + std::to_string((uint64_t)parentUUID));
				if (!parentLookup.emplace(childUUID, parentUUID).second)
					throw std::runtime_error("Entity " + std::to_string((uint64_t)childUUID) + " has multiple parents");
			}

			for (const auto& [childUUID, ignoredParent] : parentLookup)
			{
				(void)ignoredParent;
				std::unordered_set<UUID> chain;
				UUID cursor = childUUID;
				while (true)
				{
					if (!chain.emplace(cursor).second)
						throw std::runtime_error("Scene hierarchy contains a parent cycle");
					auto parentIt = parentLookup.find(cursor);
					if (parentIt == parentLookup.end())
						break;
					cursor = parentIt->second;
				}
			}

			for (const auto& [childUUID, parentUUID] : pendingParents)
			{
				Entity child = parsedScene->FindEntityByUUID(childUUID);
				Entity parent = parsedScene->FindEntityByUUID(parentUUID);
				parsedScene->m_ParentMap[childUUID] = parentUUID;
				parsedScene->m_ChildrenMap[parentUUID].push_back(childUUID);
			}
			if (!parsedScene->SyncTransformHierarchy())
				throw std::runtime_error("Scene hierarchy contains a transform that cannot be synchronized losslessly as TRS");

			m_Scene->OnRuntimeStop();
			m_Scene->m_Registry = std::move(parsedScene->m_Registry);
			m_Scene->m_SceneName = std::move(parsedScene->m_SceneName);
			m_Scene->m_EntityMap = std::move(parsedScene->m_EntityMap);
			m_Scene->m_ParentMap = std::move(parsedScene->m_ParentMap);
			m_Scene->m_ChildrenMap = std::move(parsedScene->m_ChildrenMap);
			m_Scene->m_EntityOrder = std::move(parsedScene->m_EntityOrder);
			m_Scene->m_RuntimeRunning = false;
			m_Scene->m_PhysicsWorld = nullptr;
			if (m_Scene->m_ViewportWidth > 0 && m_Scene->m_ViewportHeight > 0)
				m_Scene->OnViewportResize(m_Scene->m_ViewportWidth, m_Scene->m_ViewportHeight);

			TC_Core_Trace("{0} scene '{1}' (schema {2})",
				resolveAssets ? "Deserialized" : "Validated", sceneName, schemaVersion);
			return true;
		}
		catch (const YAML::Exception& error)
		{
			TC_Core_Error("Failed to parse scene '{0}': {1}", PathToUTF8(filepath), error.what());
		}
		catch (const std::exception& error)
		{
			TC_Core_Error("Failed to deserialize scene '{0}': {1}", PathToUTF8(filepath), error.what());
		}
		return false;
	}

}
