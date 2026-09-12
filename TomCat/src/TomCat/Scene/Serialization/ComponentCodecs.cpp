#include "tcpch.h"
#include "ComponentCodecs.h"

#include "TomCat/Asset/AssetManager.h"
#include "TomCat/Scene/Components.h"
#include "TomCat/Scene/Entity.h"

#include <exception>

namespace TomCat {

	namespace {

		template<typename Component>
		void CopyIfPresent(Entity source, Entity destination)
		{
			if (source.HasComponent<Component>())
				destination.AddOrReplaceComponent<Component>(source.GetComponent<Component>());
		}

		bool RemapEntityValue(uint64_t& value,
			const std::unordered_map<UUID, UUID>& entityMap,
			ComponentCodecs::MissingEntityReferencePolicy missingPolicy,
			const std::string& context, std::string& error)
		{
			if (value == 0)
				return true;
			const auto mapped = entityMap.find(UUID(value));
			if (mapped != entityMap.end())
			{
				value = static_cast<uint64_t>(mapped->second);
				return true;
			}
			if (missingPolicy == ComponentCodecs::MissingEntityReferencePolicy::Preserve)
				return true;
			if (missingPolicy == ComponentCodecs::MissingEntityReferencePolicy::Clear)
			{
				value = 0;
				return true;
			}
			error = context + " references an entity outside the instantiated archive";
			return false;
		}

	}

	bool ComponentCodecs::CopyAuthoringComponents(Entity source, Entity destination,
		bool resolveAssets, std::string& error)
	{
		error.clear();
		if (!source || !destination || !source.HasComponent<Tag>()
			|| !source.HasComponent<EntityMetadata>() || !source.HasComponent<Transform>()
			|| !destination.HasComponent<Tag>() || !destination.HasComponent<EntityMetadata>()
			|| !destination.HasComponent<Transform>())
		{
			error = "Source and destination must be valid entities with required components";
			return false;
		}

		try
		{
			// CreateEntity has already chosen the destination name.
			destination.GetComponent<Tag>().Visible = source.GetComponent<Tag>().Visible;
			destination.AddOrReplaceComponent<EntityMetadata>(
				source.GetComponent<EntityMetadata>());
			destination.AddOrReplaceComponent<Transform>(source.GetComponent<Transform>());
			CopyIfPresent<SpriteRenderer>(source, destination);
			CopyIfPresent<LineRenderer>(source, destination);
			CopyIfPresent<C_Camera>(source, destination);
			CopyIfPresent<CSharpScripts>(source, destination);
			CopyIfPresent<Rigidbody2D>(source, destination);
			CopyIfPresent<BoxCollider2D>(source, destination);
			CopyIfPresent<CircleCollider2D>(source, destination);
			CopyIfPresent<DistanceJoint2D>(source, destination);

			if (destination.HasComponent<SpriteRenderer>())
			{
				auto& sprite = destination.GetComponent<SpriteRenderer>();
				sprite.Sprite.reset();
				if (resolveAssets && static_cast<uint64_t>(sprite.SpriteHandle) != 0)
					sprite.Sprite = AssetManager::Get().LoadTexture(sprite.SpriteHandle);
			}
			if (destination.HasComponent<Rigidbody2D>())
				destination.GetComponent<Rigidbody2D>().RuntimeBody = nullptr;
			if (destination.HasComponent<BoxCollider2D>())
				destination.GetComponent<BoxCollider2D>().RuntimeFixture = nullptr;
			if (destination.HasComponent<CircleCollider2D>())
				destination.GetComponent<CircleCollider2D>().RuntimeFixture = nullptr;
			if (destination.HasComponent<DistanceJoint2D>())
				destination.GetComponent<DistanceJoint2D>().RuntimeJoint = nullptr;
			return true;
		}
		catch (const std::exception& exception)
		{
			error = exception.what();
			return false;
		}
	}

	bool ComponentCodecs::RemapInstanceReferences(Entity entity,
		const std::unordered_map<UUID, UUID>& entityMap,
		MissingEntityReferencePolicy missingPolicy,
		std::unordered_set<uint64_t>& usedAttachmentIDs,
		bool regenerateAttachmentIDs,
		std::string& error)
	{
		error.clear();
		if (!entity)
		{
			error = "Cannot remap an invalid entity";
			return false;
		}

		if (entity.HasComponent<DistanceJoint2D>())
		{
			auto& joint = entity.GetComponent<DistanceJoint2D>();
			uint64_t connected = static_cast<uint64_t>(joint.ConnectedEntity);
			if (!RemapEntityValue(connected, entityMap, missingPolicy,
				"DistanceJoint2D.ConnectedEntity", error))
				return false;
			joint.ConnectedEntity = UUID(connected);
		}

		if (!entity.HasComponent<CSharpScripts>())
			return true;
		for (CSharpScriptEntry& script : entity.GetComponent<CSharpScripts>().Scripts)
		{
			if (regenerateAttachmentIDs)
			{
				uint64_t attachment = 0;
				do
				{
					script.AttachmentID = UUID();
					attachment = static_cast<uint64_t>(script.AttachmentID);
				}
				while (attachment == 0 || !usedAttachmentIDs.emplace(attachment).second);
			}
			else
			{
				const uint64_t attachment = static_cast<uint64_t>(script.AttachmentID);
				if (attachment == 0 || !usedAttachmentIDs.emplace(attachment).second)
				{
					error = "C# AttachmentID must be nonzero and unique";
					return false;
				}
			}

			for (ScriptField& field : script.Fields)
			{
				if (field.Type != ScriptFieldType::Entity)
					continue;
				if (!std::holds_alternative<uint64_t>(field.Value))
				{
					error = "C# Entity field '" + field.Name + "' has an incompatible value";
					return false;
				}
				uint64_t value = std::get<uint64_t>(field.Value);
				if (!RemapEntityValue(value, entityMap, missingPolicy,
					"C# Entity field '" + field.Name + "'", error))
					return false;
				field.Value = value;
			}
		}
		return true;
	}

}
