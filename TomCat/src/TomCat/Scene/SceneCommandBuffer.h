#pragma once

#include "TomCat/Core/UUID.h"

#include <optional>
#include <string>
#include <unordered_set>
#include <variant>
#include <vector>

namespace TomCat {

	class Scene;

	// A frame-safe structural mutation queue. Flush first replays the complete
	// batch against a Scene copy; invalid references, hierarchy cycles or transform
	// failures therefore leave the live Scene untouched.
	class SceneCommandBuffer final
	{
	public:
		explicit SceneCommandBuffer(Scene& scene);

		// Returns a stable reserved identity immediately. The Entity becomes live
		// only after a successful Flush.
		UUID CreateEntity(std::string name = {}, std::optional<UUID> parent = {});
		// Accepts an identity reserved by another safe-point queue (for example the
		// C# ScriptEngine bridge). It never replaces an existing Scene entity.
		bool CreateEntityWithReservedId(UUID entity, std::string name = {},
			std::optional<UUID> parent = {});
		bool DestroyEntity(UUID entity);
		bool ReparentEntity(UUID child, std::optional<UUID> parent);

		bool Flush(std::string& error);
		void Clear();
		size_t Size() const { return m_Commands.size(); }
		bool Empty() const { return m_Commands.empty(); }

	private:
		struct CreateCommand
		{
			UUID EntityId = UUID(0);
			std::string Name;
			std::optional<UUID> Parent;
		};
		struct DestroyCommand { UUID EntityId = UUID(0); };
		struct ReparentCommand
		{
			UUID Child = UUID(0);
			std::optional<UUID> Parent;
		};
		using Command = std::variant<CreateCommand, DestroyCommand, ReparentCommand>;

		bool Apply(Scene& scene, bool publishRuntimeCreates,
			std::string& error) const;

		Scene* m_Scene = nullptr;
		std::vector<Command> m_Commands;
		std::unordered_set<UUID> m_ReservedEntities;
	};

}
