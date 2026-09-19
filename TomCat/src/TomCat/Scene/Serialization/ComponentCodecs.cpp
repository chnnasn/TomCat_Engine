#include "tcpch.h"
#include "ComponentCodecs.h"

#include "TomCat/Asset/AssetManager.h"
#include "TomCat/Scene/ComponentRegistry.h"
#include "TomCat/Scene/Components.h"
#include "TomCat/Scene/Entity.h"
#include "TomCat/Scene/Scene.h"
#include "TomCat/Scene/SpriteAnimation.h"

#include <exception>

namespace TomCat {

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
			// Every engine and module authoring component is copied by its
			// descriptor. Adding a component no longer requires another edit here.
			if (!ComponentRegistry::Get().CopyRegisteredComponents(source,
				destination, error))
				return false;

			if (destination.HasComponent<SpriteRenderer>())
			{
				auto& sprite = destination.GetComponent<SpriteRenderer>();
				sprite.Sprite.reset();
				if (resolveAssets && static_cast<uint64_t>(sprite.SpriteHandle) != 0)
					sprite.Sprite = AssetManager::Get().LoadTexture(sprite.SpriteHandle);
			}
			if (destination.HasComponent<SpriteAnimator>())
				SpriteAnimatorRuntime::Reset(
					destination.GetComponent<SpriteAnimator>());
			if (destination.HasComponent<AudioSource>())
			{
				auto& audio = destination.GetComponent<AudioSource>();
				audio.RuntimeVoice = 0;
				audio.RuntimeClipHandle = AssetHandle(0);
				audio.RuntimeAutoPlayEvaluated = false;
				audio.RuntimeStreaming = false;
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
		std::string& error,
		const std::unordered_map<UUID, UUID>* attachmentMap)
	{
		error.clear();
		if (!entity)
		{
			error = "Cannot remap an invalid entity";
			return false;
		}

		if (!ComponentRegistry::Get().RemapEntityReferences(entity, entityMap,
			missingPolicy, error))
			return false;

		if (entity.HasComponent<CSharpScripts>())
		{
			for (CSharpScriptEntry& script : entity.GetComponent<CSharpScripts>().Scripts)
			{
				if (regenerateAttachmentIDs)
				{
					if (attachmentMap)
					{
						const auto found = attachmentMap->find(script.AttachmentID);
						if (found == attachmentMap->end())
						{
							error = "C# AttachmentID is missing from the instance remap";
							return false;
						}
						script.AttachmentID = found->second;
						usedAttachmentIDs.emplace(
							static_cast<uint64_t>(script.AttachmentID));
					}
					else
					{
						uint64_t attachment = 0;
						do
						{
							script.AttachmentID = UUID();
							attachment = static_cast<uint64_t>(script.AttachmentID);
						}
						while (attachment == 0
							|| !usedAttachmentIDs.emplace(attachment).second);
					}
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
			}
		}

		if (entity.HasComponent<UIButton>() && attachmentMap)
		{
			for (UIButtonOnClickListener& listener :
				entity.GetComponent<UIButton>().OnClick)
			{
				if (static_cast<uint64_t>(listener.TargetAttachmentID) == 0)
					continue;
				const auto found = attachmentMap->find(listener.TargetAttachmentID);
				if (found == attachmentMap->end())
				{
					if (missingPolicy == MissingEntityReferencePolicy::Reject)
					{
						error = "UIButton.OnClick targets a C# attachment outside the instance";
						return false;
					}
					continue;
				}
				listener.TargetAttachmentID = found->second;
			}
		}
		else if (entity.HasComponent<UIButton>())
		{
			for (const UIButtonOnClickListener& listener :
				entity.GetComponent<UIButton>().OnClick)
			{
				if (static_cast<uint64_t>(listener.TargetAttachmentID) == 0)
					continue;
				Entity target = entity.GetScene()
					? entity.GetScene()->FindEntityByUUID(listener.TargetEntity) : Entity{};
				bool matched = false;
				if (target && target.HasComponent<CSharpScripts>())
				{
					for (const CSharpScriptEntry& script :
						target.GetComponent<CSharpScripts>().Scripts)
					{
						if (script.AttachmentID == listener.TargetAttachmentID
							&& script.ScriptAsset == listener.ScriptAsset)
						{
							matched = true;
							break;
						}
					}
				}
				if (!matched && missingPolicy == MissingEntityReferencePolicy::Reject)
				{
					error = "UIButton.OnClick targets a missing C# attachment";
					return false;
				}
			}
		}
		return true;
	}

}
