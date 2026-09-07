#include "tcpch.h"
#include "SceneSerializer.h"

#include "Entity.h"
#include "Components.h"

#include <fstream>

#define YAML_CPP_STATIC_DEFINE
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

	YAML::Emitter& operator<<(YAML::Emitter& out, const glm::vec2& v)
	{
		out << YAML::Flow;
		out << YAML::BeginSeq << v.x << v.y << YAML::EndSeq;
		return out;
	}


	YAML::Emitter& operator<<(YAML::Emitter& out, const glm::vec3& v)
	{
		out << YAML::Flow;
		out << YAML::BeginSeq << v.x << v.y << v.z << YAML::EndSeq;
		return out;
	}

	YAML::Emitter& operator<<(YAML::Emitter& out, const glm::vec4& v)
	{
		out << YAML::Flow;
		out << YAML::BeginSeq << v.x << v.y << v.z << v.w << YAML::EndSeq;
		return out;
	}

	static std::string RigidBody2DBodyTypeToString(Rigidbody2D::BodyType bodyType)
	{
		switch (bodyType)
		{
		case Rigidbody2D::BodyType::Static:    return "Static";
		case Rigidbody2D::BodyType::Dynamic:   return "Dynamic";
		case Rigidbody2D::BodyType::Kinematic: return "Kinematic";
		}

		TC_Core_Assert(false, "Unknown body type");
		return {};
	}

	static Rigidbody2D::BodyType RigidBody2DBodyTypeFromString(const std::string& bodyTypeString)
	{
		if (bodyTypeString == "Static")    return Rigidbody2D::BodyType::Static;
		if (bodyTypeString == "Dynamic")   return Rigidbody2D::BodyType::Dynamic;
		if (bodyTypeString == "Kinematic") return Rigidbody2D::BodyType::Kinematic;

		TC_Core_Assert(false, "Unknown body type");
		return Rigidbody2D::BodyType::Static;
	}


	SceneSerializer::SceneSerializer(const Ref<Scene>& scene)
		: m_Scene(scene)
	{
	}

	static void SerializeEntity(YAML::Emitter& out, Entity entity)
	{
		TC_Core_Assert(entity.HasComponent<ID>());

		out << YAML::BeginMap; // Entity
		out << YAML::Key << "Entity" << YAML::Value << entity.GetUUID();

		if (entity.HasComponent<Tag>())
		{
			out << YAML::Key << "Tag";
			out << YAML::BeginMap; // TagComponent

			auto& tag = entity.GetComponent<Tag>()._Tag;
			out << YAML::Key << "Tag" << YAML::Value << tag;
			out << YAML::Key << "Visible" << YAML::Value << entity.GetComponent<Tag>().Visible;

			out << YAML::EndMap; // TagComponent
		}

		if (entity.HasComponent<Transform>())
		{
			out << YAML::Key << "Transform";
			out << YAML::BeginMap; // TransformComponent

			auto& tc = entity.GetComponent<Transform>();
			out << YAML::Key << "Translation" << YAML::Value << tc._Translation;
			out << YAML::Key << "Rotation" << YAML::Value << tc._Rotation;
			out << YAML::Key << "Scale" << YAML::Value << tc._Scale;

			out << YAML::EndMap; // TransformComponent
		}

		if (entity.HasComponent<C_Camera>())
		{
			out << YAML::Key << "Camera";
			out << YAML::BeginMap; // CameraComponent

			auto& cameraComponent = entity.GetComponent<C_Camera>();
			auto& camera = cameraComponent._Camera;

			out << YAML::Key << "Camera" << YAML::Value;
			out << YAML::BeginMap; // Camera
			out << YAML::Key << "ProjectionType" << YAML::Value << (int)camera.GetProjectionType();
			out << YAML::Key << "PerspectiveFOV" << YAML::Value << camera.GetPerspectiveVerticalFOV();
			out << YAML::Key << "PerspectiveNear" << YAML::Value << camera.GetPerspectiveNearClip();
			out << YAML::Key << "PerspectiveFar" << YAML::Value << camera.GetPerspectiveFarClip();
			out << YAML::Key << "OrthographicSize" << YAML::Value << camera.GetOrthographicSize();
			out << YAML::Key << "OrthographicNear" << YAML::Value << camera.GetOrthographicNearClip();
			out << YAML::Key << "OrthographicFar" << YAML::Value << camera.GetOrthographicFarClip();
			out << YAML::EndMap; // Camera

			out << YAML::Key << "Primary" << YAML::Value << cameraComponent.Primary;
			out << YAML::Key << "FixedAspectRatio" << YAML::Value << cameraComponent.FixedAspectRatio;
			out << YAML::Key << "BackgroundColor" << YAML::Value << cameraComponent.BackgroundColor;

			out << YAML::EndMap; // CameraComponent
		}

		if (entity.HasComponent<SpriteRenderer>())
		{
			out << YAML::Key << "SpriteRenderer";
			out << YAML::BeginMap; // SpriteRendererComponent

			auto& spriteRenderer = entity.GetComponent<SpriteRenderer>();
			out << YAML::Key << "Enabled" << YAML::Value << spriteRenderer.Enabled;
			out << YAML::Key << "Color" << YAML::Value << spriteRenderer._Color;

			out << YAML::EndMap; // SpriteRendererComponent
		}

		if (entity.HasComponent<Rigidbody2D>())
		{
			out << YAML::Key << "Rigidbody2D";
			out << YAML::BeginMap; // Rigidbody2DComponent

			auto& rb2dComponent = entity.GetComponent<Rigidbody2D>();
			out << YAML::Key << "Enabled" << YAML::Value << rb2dComponent.Enabled;
			out << YAML::Key << "BodyType" << YAML::Value << RigidBody2DBodyTypeToString(rb2dComponent.Type);
			out << YAML::Key << "FixedRotation" << YAML::Value << rb2dComponent.FixedRotation;

			out << YAML::EndMap; // Rigidbody2DComponent
		}

		if (entity.HasComponent<BoxCollider2D>())
		{
			out << YAML::Key << "BoxCollider2D";
			out << YAML::BeginMap; // BoxCollider2DComponent

			auto& bc2dComponent = entity.GetComponent<BoxCollider2D>();
			out << YAML::Key << "Enabled" << YAML::Value << bc2dComponent.Enabled;
			out << YAML::Key << "Offset" << YAML::Value << bc2dComponent.Offset;
			out << YAML::Key << "Size" << YAML::Value << bc2dComponent.Size;
			out << YAML::Key << "Density" << YAML::Value << bc2dComponent.Density;
			out << YAML::Key << "Friction" << YAML::Value << bc2dComponent.Friction;
			out << YAML::Key << "Restitution" << YAML::Value << bc2dComponent.Restitution;
			out << YAML::Key << "RestitutionThreshold" << YAML::Value << bc2dComponent.RestitutionThreshold;

			out << YAML::EndMap; // BoxCollider2DComponent
		}

		out << YAML::EndMap; // Entity
	}

	void SceneSerializer::Serialize(const std::string& filepath)
	{
		YAML::Emitter out;
		out << YAML::BeginMap;
		out << YAML::Key << "Scene" << YAML::Value << "Untitled";
		out << YAML::Key << "Entities" << YAML::Value << YAML::BeginSeq;
		m_Scene->m_Registry.view<entt::entity>().each([&](auto entityID)
			{
				Entity entity = { entityID, m_Scene.get() };
				if (!entity)
					return;

				SerializeEntity(out, entity);
			});
		out << YAML::EndSeq;
		out << YAML::EndMap;

		std::ofstream fout(filepath);
		fout << out.c_str();
	}

	void SceneSerializer::SerializeRuntime(const std::string& filepath)
	{
		// Not implemented
		TC_Core_Assert(false);
	}

	bool SceneSerializer::Deserialize(const std::string& filepath)
	{
		YAML::Node data;
		try
		{
			data = YAML::LoadFile(filepath);
		}
		catch (YAML::ParserException e)
		{
			return false;
		}

		if (!data["Scene"])
			return false;

		std::string sceneName = data["Scene"].as<std::string>();
		TC_Core_Trace("Deserializing scene '{0}'", sceneName);

		auto entities = data["Entities"];
		if (entities)
		{
			for (auto entity : entities)
			{
				uint64_t uuid = entity["Entity"].as<uint64_t>();

				std::string name;
				auto tagComponent = entity["Tag"];
				if (tagComponent)
				{
					name = tagComponent["Tag"].as<std::string>();
				}

				TC_Core_Trace("Deserialized entity with ID = {0}, name = {1}", uuid, name);

				Entity deserializedEntity = m_Scene->CreateEntityWithUUID(uuid, name);
				if (tagComponent && tagComponent["Visible"])
					deserializedEntity.GetComponent<Tag>().Visible = tagComponent["Visible"].as<bool>();

				auto transformComponent = entity["Transform"];
				if (transformComponent)
				{
					// Entities always have transforms
					auto& tc = deserializedEntity.GetComponent<Transform>();
					tc._Translation = transformComponent["Translation"].as<glm::vec3>();
					tc._Rotation = transformComponent["Rotation"].as<glm::vec3>();
					tc._Scale = transformComponent["Scale"].as<glm::vec3>();
				}

				auto camera = entity["Camera"];
				if (camera)
				{
					auto& cc = deserializedEntity.AddComponent<C_Camera>();

					const  auto& cameraProps = camera["Camera"];
					cc._Camera.SetProjectionType((SceneCamera::ProjectionType)cameraProps["ProjectionType"].as<int>());

					cc._Camera.SetPerspectiveVerticalFOV(cameraProps["PerspectiveFOV"].as<float>());
					cc._Camera.SetPerspectiveNearClip(cameraProps["PerspectiveNear"].as<float>());
					cc._Camera.SetPerspectiveFarClip(cameraProps["PerspectiveFar"].as<float>());

					cc._Camera.SetOrthographicSize(cameraProps["OrthographicSize"].as<float>());
					cc._Camera.SetOrthographicNearClip(cameraProps["OrthographicNear"].as<float>());
					cc._Camera.SetOrthographicFarClip(cameraProps["OrthographicFar"].as<float>());

					cc.Primary = camera["Primary"].as<bool>();
				cc.FixedAspectRatio = camera["FixedAspectRatio"].as<bool>();
				if (camera["BackgroundColor"])
					cc.BackgroundColor = camera["BackgroundColor"].as<glm::vec4>();
			}

				auto spriteRendererComponent = entity["SpriteRenderer"];
				if (spriteRendererComponent)
				{
					auto& src = deserializedEntity.AddComponent<SpriteRenderer>();
					if (spriteRendererComponent["Enabled"])
						src.Enabled = spriteRendererComponent["Enabled"].as<bool>();
					src._Color = spriteRendererComponent["Color"].as<glm::vec4>();
				}

				auto rigidbody2DComponent = entity["Rigidbody2D"];
				if (rigidbody2DComponent)
				{
					auto& rb2d = deserializedEntity.AddComponent<Rigidbody2D>();
					if (rigidbody2DComponent["Enabled"])
						rb2d.Enabled = rigidbody2DComponent["Enabled"].as<bool>();
					rb2d.Type = RigidBody2DBodyTypeFromString(rigidbody2DComponent["BodyType"].as<std::string>());
					rb2d.FixedRotation = rigidbody2DComponent["FixedRotation"].as<bool>();
				}

				auto boxCollider2DComponent = entity["BoxCollider2D"];
				if (boxCollider2DComponent)
				{
					auto& bc2d = deserializedEntity.AddComponent<BoxCollider2D>();
					if (boxCollider2DComponent["Enabled"])
						bc2d.Enabled = boxCollider2DComponent["Enabled"].as<bool>();
					bc2d.Offset = boxCollider2DComponent["Offset"].as<glm::vec2>();
					bc2d.Size = boxCollider2DComponent["Size"].as<glm::vec2>();
					bc2d.Density = boxCollider2DComponent["Density"].as<float>();
					bc2d.Friction = boxCollider2DComponent["Friction"].as<float>();
					bc2d.Restitution = boxCollider2DComponent["Restitution"].as<float>();
					bc2d.RestitutionThreshold = boxCollider2DComponent["RestitutionThreshold"].as<float>();
				}

			}
		}

		return true;
	}

	bool SceneSerializer::DeserializeRuntime(const std::string& filepath)
	{
		// Not implemented
		TC_Core_Assert(false);
		return false;
	}

}
