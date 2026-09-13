#include "tcpch.h"
#include "SceneSerializer.h"

#include "Components.h"
#include "ComponentRegistry.h"
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

		void ValidateSpriteAnimator(const SpriteAnimator& animator,
			const std::string& context)
		{
			if (!IsFinite(animator.Speed) || animator.Speed < 0.0f)
				throw std::runtime_error(context
					+ ".Speed must be finite and non-negative");
			if (animator.Clips.empty())
				throw std::runtime_error(context + ".Clips must not be empty");

			std::unordered_set<std::string> clipNames;
			bool foundInitialClip = animator.InitialClip.empty();
			for (size_t clipIndex = 0; clipIndex < animator.Clips.size(); ++clipIndex)
			{
				const SpriteAnimationClip& clip = animator.Clips[clipIndex];
				const std::string clipContext = context + ".Clips["
					+ std::to_string(clipIndex) + "]";
				if (clip.Name.empty() || !clipNames.emplace(clip.Name).second)
					throw std::runtime_error(clipContext
						+ ".Name must be nonempty and unique");
				foundInitialClip = foundInitialClip || clip.Name == animator.InitialClip;
				if (clip.Frames.empty())
					throw std::runtime_error(clipContext + ".Frames must not be empty");
				for (size_t frameIndex = 0; frameIndex < clip.Frames.size(); ++frameIndex)
				{
					const float duration = clip.Frames[frameIndex].DurationSeconds;
					if (!IsFinite(duration) || duration <= 0.0f)
						throw std::runtime_error(clipContext + ".Frames["
							+ std::to_string(frameIndex)
							+ "].DurationSeconds must be finite and greater than zero");
				}
			}
			if (!foundInitialClip)
				throw std::runtime_error(context
					+ ".InitialClip must name one of Clips or be empty");

			std::unordered_map<std::string, AnimatorParameterType> parameterTypes;
			for (size_t index = 0; index < animator.Parameters.size(); ++index)
			{
				const AnimatorParameter& parameter = animator.Parameters[index];
				const std::string parameterContext = context + ".Parameters["
					+ std::to_string(index) + "]";
				if (parameter.Name.empty()
					|| !parameterTypes.emplace(parameter.Name, parameter.Type).second)
					throw std::runtime_error(parameterContext
						+ ".Name must be nonempty and unique");
				if (parameter.Type < AnimatorParameterType::Bool
					|| parameter.Type > AnimatorParameterType::Trigger)
					throw std::runtime_error(parameterContext + ".Type is invalid");
				if (!IsFinite(parameter.FloatValue))
					throw std::runtime_error(parameterContext + ".FloatValue must be finite");
			}

			std::unordered_set<std::string> stateNames;
			bool foundInitialState = animator.InitialState.empty();
			for (size_t index = 0; index < animator.States.size(); ++index)
			{
				const AnimatorState& state = animator.States[index];
				const std::string stateContext = context + ".States["
					+ std::to_string(index) + "]";
				if (state.Name.empty() || !stateNames.emplace(state.Name).second)
					throw std::runtime_error(stateContext
						+ ".Name must be nonempty and unique");
				if (clipNames.find(state.Clip) == clipNames.end())
					throw std::runtime_error(stateContext
						+ ".Clip must name an existing clip");
				if (!IsFinite(state.Speed) || state.Speed <= 0.0f)
					throw std::runtime_error(stateContext
						+ ".Speed must be finite and greater than zero");
				foundInitialState = foundInitialState || state.Name == animator.InitialState;
			}
			if (!animator.InitialState.empty() && animator.States.empty())
				throw std::runtime_error(context
					+ ".InitialState requires at least one State");
			if (!foundInitialState)
				throw std::runtime_error(context
					+ ".InitialState must name an existing State or be empty");

			for (size_t transitionIndex = 0;
				transitionIndex < animator.Transitions.size(); ++transitionIndex)
			{
				const AnimatorTransition& transition =
					animator.Transitions[transitionIndex];
				const std::string transitionContext = context + ".Transitions["
					+ std::to_string(transitionIndex) + "]";
				if (animator.States.empty()
					|| stateNames.find(transition.ToState) == stateNames.end())
					throw std::runtime_error(transitionContext
						+ ".ToState must name an existing State");
				if (!transition.AnyState
					&& stateNames.find(transition.FromState) == stateNames.end())
					throw std::runtime_error(transitionContext
						+ ".FromState must name an existing State");
				if (transition.AnyState && !transition.FromState.empty())
					throw std::runtime_error(transitionContext
						+ ".FromState must be empty for AnyState");
				if (!IsFinite(transition.ExitTime) || transition.ExitTime < -1.0f
					|| transition.ExitTime > 1.0f)
					throw std::runtime_error(transitionContext
						+ ".ExitTime must be -1 or normalized in [0, 1]");
				if (transition.ExitTime < 0.0f && transition.Conditions.empty())
					throw std::runtime_error(transitionContext
						+ " requires an exit time or a condition");
				for (size_t conditionIndex = 0;
					conditionIndex < transition.Conditions.size(); ++conditionIndex)
				{
					const AnimatorCondition& condition =
						transition.Conditions[conditionIndex];
					const std::string conditionContext = transitionContext
						+ ".Conditions[" + std::to_string(conditionIndex) + "]";
					const auto parameter = parameterTypes.find(condition.Parameter);
					if (parameter == parameterTypes.end())
						throw std::runtime_error(conditionContext
							+ ".Parameter must name an existing parameter");
					if (!IsFinite(condition.Threshold))
						throw std::runtime_error(conditionContext
							+ ".Threshold must be finite");
					const bool boolean = parameter->second == AnimatorParameterType::Bool
						|| parameter->second == AnimatorParameterType::Trigger;
					const bool booleanMode = condition.Mode == AnimatorConditionMode::If
						|| condition.Mode == AnimatorConditionMode::IfNot;
					const bool numericMode = condition.Mode >= AnimatorConditionMode::Greater
						&& condition.Mode <= AnimatorConditionMode::NotEqual;
					if ((boolean && !booleanMode) || (!boolean && !numericMode))
						throw std::runtime_error(conditionContext
							+ ".Mode is incompatible with the parameter type");
				}
			}
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

		void ValidateAudioSource(const AudioSource& source,
			const std::string& context)
		{
			if (!IsFinite(source.Volume) || source.Volume < 0.0f
				|| source.Volume > 4.0f)
				throw std::runtime_error(context + ".Volume must be finite and in [0, 4]");
			if (!IsFinite(source.Pitch) || source.Pitch < 0.25f
				|| source.Pitch > 4.0f)
				throw std::runtime_error(context + ".Pitch must be finite and in [0.25, 4]");
			if (source.MixerGroup > 2)
				throw std::runtime_error(context + ".MixerGroup must be Master, Music, or SFX");
			if (!IsFinite(source.SpatialBlend) || source.SpatialBlend < 0.0f
				|| source.SpatialBlend > 1.0f)
				throw std::runtime_error(context
					+ ".SpatialBlend must be finite and in [0, 1]");
			if (!IsFinite(source.MinDistance) || source.MinDistance < 0.0f)
				throw std::runtime_error(context
					+ ".MinDistance must be finite and non-negative");
			if (!IsFinite(source.MaxDistance)
				|| source.MaxDistance <= source.MinDistance)
				throw std::runtime_error(context
					+ ".MaxDistance must be finite and greater than MinDistance");
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

		void SerializeEntity(YAML::Emitter& out, Scene* scene, Entity entity)
		{
			const std::string context = "Entity " + std::to_string(static_cast<uint64_t>(entity.GetUUID()));
			out << YAML::BeginMap;
			out << YAML::Key << "Entity" << YAML::Value << entity.GetUUID();

			auto& metadata = entity.GetComponent<EntityMetadata>();
			ValidateEntityMetadata(metadata, context + ".EntityMetadata");

			auto& transform = entity.GetComponent<Transform>();
			ValidateTransform(transform, context + ".Transform");

			if (entity.HasComponent<C_Camera>())
				ValidateCamera(entity.GetComponent<C_Camera>(), context + ".Camera");

			if (entity.HasComponent<SpriteRenderer>())
			{
				const auto& sprite = entity.GetComponent<SpriteRenderer>();
				ValidateSprite(sprite, context + ".SpriteRenderer");
				if (sprite.Sprite && static_cast<uint64_t>(sprite.SpriteHandle) == 0)
					throw std::runtime_error(context
						+ ".SpriteRenderer has a resolved sprite but no AssetHandle");
			}

			if (entity.HasComponent<SpriteAnimator>())
				ValidateSpriteAnimator(entity.GetComponent<SpriteAnimator>(),
					context + ".SpriteAnimator");

			if (entity.HasComponent<LineRenderer>())
				ValidateLine(entity.GetComponent<LineRenderer>(),
					context + ".LineRenderer");

			if (entity.HasComponent<AudioSource>())
				ValidateAudioSource(entity.GetComponent<AudioSource>(),
					context + ".AudioSource");

			if (entity.HasComponent<CSharpScripts>())
				ValidateCSharpScripts(entity.GetComponent<CSharpScripts>(),
					context + ".CSharpScripts");

			if (entity.HasComponent<BoxCollider2D>())
				ValidateCollider(entity.GetComponent<BoxCollider2D>(),
					context + ".BoxCollider2D");

			if (entity.HasComponent<CircleCollider2D>())
				ValidateCollider(entity.GetComponent<CircleCollider2D>(),
					context + ".CircleCollider2D");

			if (entity.HasComponent<DistanceJoint2D>())
			{
				const auto& joint = entity.GetComponent<DistanceJoint2D>();
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
			}

			std::string componentError;
			if (!ComponentRegistry::Get().EncodeLegacyComponents(entity, out,
				componentError))
				throw std::runtime_error(context
					+ ".LegacyComponents: " + componentError);
			componentError.clear();
			if (!ComponentRegistry::Get().EncodeComponents(entity, out,
				componentError))
				throw std::runtime_error(context + ".Components: " + componentError);

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
			if (schemaVersion < OldestSupportedSchemaVersion
				|| schemaVersion > CurrentSchemaVersion)
				throw std::runtime_error("Scene SchemaVersion must be in ["
					+ std::to_string(OldestSupportedSchemaVersion) + ", "
					+ std::to_string(CurrentSchemaVersion) + "], got "
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
						{ "Entity", "Tag", "EntityMetadata", "Transform", "LocalTransform",
							"Components", "Parent" },
						{ "Camera", "SpriteRenderer", "LineRenderer", "CSharpScripts",
							"Rigidbody2D", "BoxCollider2D", "CircleCollider2D",
							"DistanceJoint2D", "AudioSource", "AudioListener",
							"SpriteAnimator" });
				}
				else if (schemaVersion == 10)
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
				const std::string name = ReadRequired<std::string>(
					tagNode, "Tag", context + ".Tag");

				Entity entity = parsedScene->CreateEntityWithUUID(uuid, name);
				if (!entity)
					throw std::runtime_error(context + " could not be created");

				std::string componentError;
				if (!ComponentRegistry::Get().DecodeLegacyComponents(entity,
					entityNode, componentError))
					throw std::runtime_error(context
						+ ".LegacyComponents: " + componentError);

				if (resolveAssets && entity.HasComponent<SpriteRenderer>())
				{
					auto& sprite = entity.GetComponent<SpriteRenderer>();
					if (static_cast<uint64_t>(sprite.SpriteHandle) != 0)
						sprite.Sprite = AssetManager::Get().LoadTexture(
							sprite.SpriteHandle);
				}

				if (entity.HasComponent<CSharpScripts>())
				{
					for (const CSharpScriptEntry& script :
						entity.GetComponent<CSharpScripts>().Scripts)
					{
						if (static_cast<uint64_t>(script.AttachmentID) == 0
							|| !seenAttachmentIDs.emplace(script.AttachmentID).second)
							throw std::runtime_error(context
								+ ".CSharpScripts AttachmentID must be nonzero and "
								"unique across the scene");
					}
				}

				if (entity.HasComponent<DistanceJoint2D>())
				{
					const UUID connected = entity.GetComponent<DistanceJoint2D>()
						.ConnectedEntity;
					if (static_cast<uint64_t>(connected) != 0)
						pendingJointConnections.emplace_back(uuid, connected);
				}
				if (schemaVersion == CurrentSchemaVersion)
				{
					componentError.clear();
					if (!ComponentRegistry::Get().DecodeComponents(entity,
						entityNode["Components"], componentError))
						throw std::runtime_error(context + ".Components: " + componentError);
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
