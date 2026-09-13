#include "tcpch.h"
#include "SceneCommandBuffer.h"

#include "TomCat/Scene/Entity.h"
#include "TomCat/Scene/Scene.h"

#include <algorithm>
#include <type_traits>

namespace TomCat {

	SceneCommandBuffer::SceneCommandBuffer(Scene& scene)
		: m_Scene(&scene)
	{
	}

	UUID SceneCommandBuffer::CreateEntity(std::string name,
		std::optional<UUID> parent)
	{
		UUID id;
		while (static_cast<uint64_t>(id) == 0 || m_Scene->FindEntityByUUID(id)
			|| m_ReservedEntities.contains(id))
			id = UUID();
		if (parent && static_cast<uint64_t>(*parent) == 0)
			parent.reset();
		m_ReservedEntities.emplace(id);
		m_Commands.emplace_back(CreateCommand{ id, std::move(name), parent });
		return id;
	}

	bool SceneCommandBuffer::CreateEntityWithReservedId(UUID entity,
		std::string name, std::optional<UUID> parent)
	{
		if (!m_Scene || static_cast<uint64_t>(entity) == 0
			|| m_Scene->FindEntityByUUID(entity) || m_ReservedEntities.contains(entity))
			return false;
		if (parent && static_cast<uint64_t>(*parent) == 0)
			parent.reset();
		m_ReservedEntities.emplace(entity);
		m_Commands.emplace_back(CreateCommand{ entity, std::move(name), parent });
		return true;
	}

	bool SceneCommandBuffer::DestroyEntity(UUID entity)
	{
		if (static_cast<uint64_t>(entity) == 0)
			return false;
		m_Commands.emplace_back(DestroyCommand{ entity });
		return true;
	}

	bool SceneCommandBuffer::ReparentEntity(UUID child,
		std::optional<UUID> parent)
	{
		if (static_cast<uint64_t>(child) == 0)
			return false;
		if (parent && static_cast<uint64_t>(*parent) == 0)
			parent.reset();
		m_Commands.emplace_back(ReparentCommand{ child, parent });
		return true;
	}

	bool SceneCommandBuffer::Apply(Scene& scene, bool publishRuntimeCreates,
		std::string& error) const
	{
		std::vector<UUID> created;
		for (size_t index = 0; index < m_Commands.size(); ++index)
		{
			bool success = std::visit([&](const auto& command)
			{
				using T = std::decay_t<decltype(command)>;
				if constexpr (std::is_same_v<T, CreateCommand>)
				{
					if (scene.FindEntityByUUID(command.EntityId))
						return false;
					Entity entity = scene.CreateEntityWithUUID(command.EntityId,
						command.Name);
					if (!entity)
						return false;
					if (command.Parent)
					{
						Entity parent = scene.FindEntityByUUID(*command.Parent);
						if (!parent || !scene.SetParent(entity, parent))
							return false;
					}
					created.push_back(command.EntityId);
					return true;
				}
				else if constexpr (std::is_same_v<T, DestroyCommand>)
				{
					Entity entity = scene.FindEntityByUUID(command.EntityId);
					if (!entity)
						return false;
					scene.DestroyEntity(entity);
					return !scene.FindEntityByUUID(command.EntityId);
				}
				else
				{
					Entity child = scene.FindEntityByUUID(command.Child);
					if (!child)
						return false;
					Entity parent;
					if (command.Parent)
					{
						parent = scene.FindEntityByUUID(*command.Parent);
						if (!parent)
							return false;
					}
					return scene.SetParent(child, parent);
				}
			}, m_Commands[index]);
			if (!success)
			{
				error = "Scene command " + std::to_string(index)
					+ " failed validation or application";
				return false;
			}
		}

		if (publishRuntimeCreates && scene.IsRuntimeRunning())
		{
			created.erase(std::remove_if(created.begin(), created.end(),
				[&scene](UUID id) { return !scene.FindEntityByUUID(id); }), created.end());
			if (!created.empty())
				scene.QueueRuntimeEntityBatchCreated(std::move(created));
		}
		return true;
	}

	bool SceneCommandBuffer::Flush(std::string& error)
	{
		error.clear();
		if (!m_Scene)
		{
			error = "SceneCommandBuffer has no Scene";
			return false;
		}
		if (m_Commands.empty())
			return true;

		// Scene::Copy is the validation transaction. The live Scene cannot observe
		// any command until the whole batch succeeds on the staged copy.
		Ref<Scene> nonOwning(m_Scene, [](Scene*) {});
		Ref<Scene> staged = Scene::Copy(nonOwning);
		if (!staged)
		{
			error = "Could not create SceneCommandBuffer validation snapshot";
			return false;
		}
		if (!Apply(*staged, false, error))
			return false;
		if (!Apply(*m_Scene, true, error))
		{
			error = "Validated SceneCommandBuffer unexpectedly failed on the live Scene: "
				+ error;
			return false;
		}
		Clear();
		return true;
	}

	void SceneCommandBuffer::Clear()
	{
		m_Commands.clear();
		m_ReservedEntities.clear();
	}

}
