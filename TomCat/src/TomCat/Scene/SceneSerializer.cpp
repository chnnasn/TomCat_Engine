#include "tcpch.h"
#include "SceneSerializer.h"

#include "Components.h"
#include "Entity.h"
#include "TomCat/Utils/FileSystemUtils.h"
#include "TomCat/Utils/PathUtils.h"

#include <cmath>
#include <filesystem>
#include <fstream>
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

		constexpr uint32_t kCurrentSceneSchemaVersion = 2;
		constexpr float kPi = 3.14159265358979323846f;

		bool IsFinite(float value)
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

		void ValidateCollider(const BoxCollider2D& collider, const std::string& context)
		{
			RequireFinite(collider.Offset, context + ".Offset");
			RequireFinite(collider.Size, context + ".Size");
			if (collider.Size.x <= 0.0f || collider.Size.y <= 0.0f)
				throw std::runtime_error(context + ".Size components must be greater than zero");
			if (!IsFinite(collider.Density) || collider.Density < 0.0f)
				throw std::runtime_error(context + ".Density must be finite and non-negative");
			if (!IsFinite(collider.Friction) || collider.Friction < 0.0f || collider.Friction > 1.0f)
				throw std::runtime_error(context + ".Friction must be in [0, 1]");
			if (!IsFinite(collider.Restitution) || collider.Restitution < 0.0f || collider.Restitution > 1.0f)
				throw std::runtime_error(context + ".Restitution must be in [0, 1]");
			if (!IsFinite(collider.RestitutionThreshold) || collider.RestitutionThreshold < 0.0f)
				throw std::runtime_error(context + ".RestitutionThreshold must be finite and non-negative");
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

		void RequireMap(const YAML::Node& node, const std::string& context)
		{
			if (!node || !node.IsMap())
				throw std::runtime_error(context + " must be a map");
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

		template<typename T>
		T ReadOptional(const YAML::Node& node, const char* key, const T& fallback)
		{
			const YAML::Node value = node ? node[key] : YAML::Node{};
			return value ? value.as<T>() : fallback;
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

		std::string MakePortableTexturePath(const std::filesystem::path& texturePath,
			const std::filesystem::path& scenePath)
		{
			if (texturePath.empty())
				return {};

			std::error_code error;
			std::filesystem::path texture = std::filesystem::absolute(texturePath, error);
			if (error)
				return PathToUTF8(texturePath.lexically_normal());

			std::filesystem::path sceneDirectory = scenePath.parent_path();
			if (sceneDirectory.empty())
				sceneDirectory = std::filesystem::current_path(error);
			else
				sceneDirectory = std::filesystem::absolute(sceneDirectory, error);
			if (!error)
			{
				std::filesystem::path relative = std::filesystem::relative(texture, sceneDirectory, error);
				if (!error && !relative.empty())
					return PathToUTF8(relative.lexically_normal());
			}
			return PathToUTF8(texture.lexically_normal());
		}

		std::filesystem::path ResolveTexturePath(const std::string& storedPath,
			const std::filesystem::path& scenePath)
		{
			std::filesystem::path texturePath = UTF8ToPath(storedPath);
			if (texturePath.is_absolute())
				return texturePath.lexically_normal();
			std::filesystem::path parent = scenePath.parent_path();
			if (parent.empty())
				parent = ".";
			return (parent / texturePath).lexically_normal();
		}

		void SerializeEntity(YAML::Emitter& out, Scene* scene, Entity entity,
			const std::filesystem::path& scenePath)
		{
			const std::string context = "Entity " + std::to_string(static_cast<uint64_t>(entity.GetUUID()));
			out << YAML::BeginMap;
			out << YAML::Key << "Entity" << YAML::Value << entity.GetUUID();

			auto& tag = entity.GetComponent<Tag>();
			out << YAML::Key << "Tag" << YAML::Value << YAML::BeginMap;
			out << YAML::Key << "Tag" << YAML::Value << tag._Tag;
			out << YAML::Key << "Visible" << YAML::Value << tag.Visible;
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
				out << YAML::Key << "SpriteRenderer" << YAML::Value << YAML::BeginMap;
				out << YAML::Key << "Enabled" << YAML::Value << sprite.Enabled;
				out << YAML::Key << "Color" << YAML::Value << sprite._Color;
				out << YAML::Key << "TilingFactor" << YAML::Value << sprite.TilingFactor;
				if (sprite.Texture && !sprite.Texture->GetPath().empty())
					out << YAML::Key << "TexturePath" << YAML::Value << MakePortableTexturePath(sprite.Texture->GetPath(), scenePath);
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
				out << YAML::Key << "Offset" << YAML::Value << collider.Offset;
				out << YAML::Key << "Size" << YAML::Value << collider.Size;
				out << YAML::Key << "Density" << YAML::Value << collider.Density;
				out << YAML::Key << "Friction" << YAML::Value << collider.Friction;
				out << YAML::Key << "Restitution" << YAML::Value << collider.Restitution;
				out << YAML::Key << "RestitutionThreshold" << YAML::Value << collider.RestitutionThreshold;
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

	bool SceneSerializer::Serialize(const std::filesystem::path& filepath)
	{
		if (!m_Scene)
		{
			TC_Core_Error("Cannot serialize a null scene");
			return false;
		}

		try
		{
			if (m_Scene->m_EntityMap.size() != m_Scene->m_EntityOrder.size())
			{
				TC_Core_Error("Scene entity index and serialization order are inconsistent");
				return false;
			}

			std::unordered_set<UUID> serializedUUIDs;
			for (UUID uuid : m_Scene->m_EntityOrder)
			{
				Entity entity = m_Scene->FindEntityByUUID(uuid);
				if ((uint64_t)uuid == 0 || !entity || !entity.HasComponent<ID>()
					|| !entity.HasComponent<Tag>() || !entity.HasComponent<Transform>()
					|| entity.GetUUID() != uuid || !serializedUUIDs.emplace(uuid).second)
				{
					TC_Core_Error("Scene contains an invalid or duplicate UUID {0}", (uint64_t)uuid);
					return false;
				}
			}
			if (!m_Scene->ValidateTransformHierarchy())
			{
				TC_Core_Error("Scene transform hierarchy cannot be synchronized losslessly as TRS values");
				return false;
			}

			YAML::Emitter out;
			out << YAML::BeginMap;
			out << YAML::Key << "SchemaVersion" << YAML::Value << kCurrentSceneSchemaVersion;
			out << YAML::Key << "SceneName" << YAML::Value << m_Scene->GetSceneName();
			out << YAML::Key << "Entities" << YAML::Value << YAML::BeginSeq;
			for (UUID uuid : m_Scene->m_EntityOrder)
				SerializeEntity(out, m_Scene.get(), m_Scene->FindEntityByUUID(uuid), filepath);
			out << YAML::EndSeq << YAML::EndMap;

			if (!out.good())
			{
				TC_Core_Error("Failed to encode scene '{0}': {1}", PathToUTF8(filepath), out.GetLastError());
				return false;
			}
			std::string writeError;
			if (FileSystem::WriteFileAtomically(filepath, out.c_str(), writeError))
				return true;
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
		if (!m_Scene)
		{
			TC_Core_Error("Cannot deserialize into a null scene");
			return false;
		}

		try
		{
			std::ifstream input(filepath, std::ios::binary);
			if (!input)
				throw std::runtime_error("Could not open the scene file");
			YAML::Node data = YAML::Load(input);
			if (input.bad())
				throw std::runtime_error("Failed while reading the scene file");
			RequireMap(data, "scene document");

			const uint32_t schemaVersion = data["SchemaVersion"] ? data["SchemaVersion"].as<uint32_t>() : 1U;
			if (schemaVersion == 0 || schemaVersion > kCurrentSceneSchemaVersion)
				throw std::runtime_error("Unsupported scene SchemaVersion " + std::to_string(schemaVersion));

			YAML::Node sceneNameNode = data["SceneName"];
			if (!sceneNameNode && schemaVersion == 1)
				sceneNameNode = data["Scene"];
			if (!sceneNameNode)
				throw std::runtime_error("Scene document is missing required field 'SceneName'");
			const std::string sceneName = sceneNameNode.as<std::string>();
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
			std::unordered_set<UUID> entitiesWithoutLocalTransform;
			std::unordered_set<UUID> seenUUIDs;

			for (std::size_t index = 0; index < entities.size(); ++index)
			{
				const YAML::Node entityNode = entities[index];
				const std::string context = "Entities[" + std::to_string(index) + "]";
				RequireMap(entityNode, context);

				const uint64_t rawUUID = ReadRequired<uint64_t>(entityNode, "Entity", context);
				const UUID uuid(rawUUID);
				if (rawUUID == 0)
					throw std::runtime_error(context + " uses reserved UUID 0");
				if (!seenUUIDs.emplace(uuid).second)
					throw std::runtime_error(context + " duplicates UUID " + std::to_string(rawUUID));

				std::string name = "Entity";
				YAML::Node tagNode = entityNode["Tag"];
				if (tagNode)
				{
					RequireMap(tagNode, context + ".Tag");
					name = ReadRequired<std::string>(tagNode, "Tag", context + ".Tag");
				}
				else if (schemaVersion >= 2)
					throw std::runtime_error(context + " is missing required component 'Tag'");

				Entity entity = parsedScene->CreateEntityWithUUID(uuid, name);
				if (!entity)
					throw std::runtime_error(context + " could not be created");
				if (tagNode)
					entity.GetComponent<Tag>().Visible = ReadOptional<bool>(tagNode, "Visible", true);

				YAML::Node transformNode = entityNode["Transform"];
				if (transformNode)
				{
					RequireMap(transformNode, context + ".Transform");
					auto& transform = entity.GetComponent<Transform>();
					transform._Translation = ReadRequired<glm::vec3>(transformNode, "Translation", context + ".Transform");
					transform._Rotation = ReadRequired<glm::vec3>(transformNode, "Rotation", context + ".Transform");
					transform._Scale = ReadRequired<glm::vec3>(transformNode, "Scale", context + ".Transform");
				}
				else if (schemaVersion >= 2)
					throw std::runtime_error(context + " is missing required component 'Transform'");

				YAML::Node localTransformNode = entityNode["LocalTransform"];
				if (localTransformNode)
				{
					RequireMap(localTransformNode, context + ".LocalTransform");
					auto& transform = entity.GetComponent<Transform>();
					transform._LocalTranslation = ReadRequired<glm::vec3>(localTransformNode, "Translation", context + ".LocalTransform");
					transform._LocalRotation = ReadRequired<glm::vec3>(localTransformNode, "Rotation", context + ".LocalTransform");
					transform._LocalScale = ReadRequired<glm::vec3>(localTransformNode, "Scale", context + ".LocalTransform");
				}
				else if (schemaVersion >= 2)
					throw std::runtime_error(context + " is missing required component 'LocalTransform'");
				else
				{
					auto& transform = entity.GetComponent<Transform>();
					transform._LocalTranslation = transform._Translation;
					transform._LocalRotation = transform._Rotation;
					transform._LocalScale = transform._Scale;
					entitiesWithoutLocalTransform.emplace(uuid);
				}
				ValidateTransform(entity.GetComponent<Transform>(), context + ".Transform");

				YAML::Node cameraNode = entityNode["Camera"];
				if (cameraNode)
				{
					RequireMap(cameraNode, context + ".Camera");
					YAML::Node properties = cameraNode["Camera"];
					RequireMap(properties, context + ".Camera.Camera");
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
					const glm::vec4 backgroundColor = ReadOptional<glm::vec4>(
						cameraNode, "BackgroundColor", C_Camera{}.BackgroundColor);
					ValidateCameraValues(perspectiveFov, perspectiveNear, perspectiveFar,
						orthographicSize, orthographicNear, orthographicFar, backgroundColor, context + ".Camera");

					auto& camera = entity.AddComponent<C_Camera>();
					if (!camera._Camera.SetPerspective(perspectiveFov, perspectiveNear, perspectiveFar)
						|| !camera._Camera.SetOrthographic(orthographicSize, orthographicNear, orthographicFar)
						|| !camera._Camera.SetProjectionType(static_cast<SceneCamera::ProjectionType>(projectionType)))
						throw std::runtime_error(context + ".Camera produces a non-finite projection matrix");
					camera.Primary = ReadOptional<bool>(cameraNode, "Primary", true);
					camera.FixedAspectRatio = ReadOptional<bool>(cameraNode, "FixedAspectRatio", false);
					camera.BackgroundColor = backgroundColor;
				}

				YAML::Node spriteNode = entityNode["SpriteRenderer"];
				if (spriteNode)
				{
					RequireMap(spriteNode, context + ".SpriteRenderer");
					auto& sprite = entity.AddComponent<SpriteRenderer>();
					sprite.Enabled = ReadOptional<bool>(spriteNode, "Enabled", true);
					sprite._Color = ReadRequired<glm::vec4>(spriteNode, "Color", context + ".SpriteRenderer");
					sprite.TilingFactor = ReadOptional<float>(spriteNode, "TilingFactor", 1.0f);
					ValidateSprite(sprite, context + ".SpriteRenderer");

					const std::string texturePath = ReadOptional<std::string>(spriteNode, "TexturePath", std::string{});
					if (!texturePath.empty())
					{
						const std::filesystem::path resolved = ResolveTexturePath(texturePath, filepath);
						try
						{
							sprite.Texture = Texture2D::Create(resolved);
							if (!sprite.Texture || !sprite.Texture->IsLoaded())
								TC_Core_Warn("Scene texture is missing or unreadable: {0}", PathToUTF8(resolved));
						}
						catch (const std::exception& textureError)
						{
							TC_Core_Warn("Could not load scene texture '{0}': {1}", PathToUTF8(resolved), textureError.what());
						}
					}
				}

				YAML::Node rigidbodyNode = entityNode["Rigidbody2D"];
				if (rigidbodyNode)
				{
					RequireMap(rigidbodyNode, context + ".Rigidbody2D");
					auto& rigidbody = entity.AddComponent<Rigidbody2D>();
					rigidbody.Enabled = ReadOptional<bool>(rigidbodyNode, "Enabled", true);
					rigidbody.Type = Rigidbody2DBodyTypeFromString(ReadRequired<std::string>(rigidbodyNode, "BodyType", context + ".Rigidbody2D"));
					rigidbody.FixedRotation = ReadOptional<bool>(rigidbodyNode, "FixedRotation", false);
				}

				YAML::Node colliderNode = entityNode["BoxCollider2D"];
				if (colliderNode)
				{
					RequireMap(colliderNode, context + ".BoxCollider2D");
					auto& collider = entity.AddComponent<BoxCollider2D>();
					collider.Enabled = ReadOptional<bool>(colliderNode, "Enabled", true);
					collider.Offset = ReadRequired<glm::vec2>(colliderNode, "Offset", context + ".BoxCollider2D");
					collider.Size = ReadRequired<glm::vec2>(colliderNode, "Size", context + ".BoxCollider2D");
					collider.Density = ReadRequired<float>(colliderNode, "Density", context + ".BoxCollider2D");
					collider.Friction = ReadRequired<float>(colliderNode, "Friction", context + ".BoxCollider2D");
					collider.Restitution = ReadRequired<float>(colliderNode, "Restitution", context + ".BoxCollider2D");
					collider.RestitutionThreshold = ReadRequired<float>(colliderNode, "RestitutionThreshold", context + ".BoxCollider2D");
					ValidateCollider(collider, context + ".BoxCollider2D");
				}

				uint64_t parentUUID = 0;
				if (entityNode["Parent"])
					parentUUID = entityNode["Parent"].as<uint64_t>();
				else if (entityNode["m_Father"])
				{
					YAML::Node legacyParent = entityNode["m_Father"];
					RequireMap(legacyParent, context + ".m_Father");
					parentUUID = ReadOptional<uint64_t>(legacyParent, "fileID", 0ULL);
				}
				if (parentUUID != 0)
					pendingParents.emplace_back(uuid, UUID(parentUUID));
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
				if (entitiesWithoutLocalTransform.find(childUUID) != entitiesWithoutLocalTransform.end())
				{
					if (!parsedScene->SetParent(child, parent))
						throw std::runtime_error(
							"Cannot migrate a legacy child whose hierarchy transform is singular or contains shear");
					continue;
				}
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

			TC_Core_Trace("Deserialized scene '{0}' (schema {1})", sceneName, schemaVersion);
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
