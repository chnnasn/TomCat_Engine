#pragma once

#include "TomCat/Scene/Scene.h"
#include "TomCat/Scene/SceneCommandBuffer.h"

#include <stdexcept>
#include <string>
#include <vector>

namespace SceneCommandBufferRegression {

	inline void Check(bool condition, const std::string& message)
	{
		if (!condition)
			throw std::runtime_error(message);
	}

	inline void Run()
	{
		TomCat::Scene scene;
		TomCat::Entity oldParent = scene.CreateEntity("Old parent");
		TomCat::Entity survivor = scene.CreateEntity("Survivor");
		const TomCat::UUID oldParentId = oldParent.GetUUID();
		Check(scene.SetParent(survivor, oldParent), "could not create source hierarchy");

		TomCat::SceneCommandBuffer commands(scene);
		const TomCat::UUID newParentId = commands.CreateEntity("New parent");
		const TomCat::UUID newChildId = commands.CreateEntity("New child", newParentId);
		Check(!scene.FindEntityByUUID(newParentId)
			&& !scene.FindEntityByUUID(newChildId),
			"reserved command-buffer identities became live before Flush");
		Check(commands.ReparentEntity(survivor.GetUUID(), newParentId)
			&& commands.DestroyEntity(oldParentId),
			"could not queue structural commands");
		std::string error;
		Check(commands.Flush(error), error);
		TomCat::Entity newParent = scene.FindEntityByUUID(newParentId);
		TomCat::Entity newChild = scene.FindEntityByUUID(newChildId);
		Check(newParent && newChild && scene.GetParent(newChild) == newParent
			&& scene.GetParent(survivor) == newParent
			&& !scene.FindEntityByUUID(oldParentId) && commands.Empty(),
			"valid create/reparent/destroy batch did not commit in order");

		TomCat::SceneCommandBuffer invalid(scene);
		const TomCat::UUID cycleA = invalid.CreateEntity("Cycle A");
		const TomCat::UUID cycleB = invalid.CreateEntity("Cycle B", cycleA);
		Check(invalid.ReparentEntity(cycleA, cycleB), "could not queue cycle fixture");
		Check(!invalid.Flush(error) && !scene.FindEntityByUUID(cycleA)
			&& !scene.FindEntityByUUID(cycleB) && invalid.Size() == 3,
			"invalid hierarchy batch partially changed the live Scene");
		invalid.Clear();

		TomCat::Scene runtime;
		Check(runtime.OnRuntimeStart(), "could not start command-buffer runtime fixture");
		std::vector<TomCat::UUID> delivered;
		runtime.SetRuntimeEntityBatchCreatedCallback(
			[&delivered](std::span<const TomCat::UUID> ids)
			{
				delivered.assign(ids.begin(), ids.end());
			});
		TomCat::SceneCommandBuffer runtimeCommands(runtime);
		const TomCat::UUID runtimeId = runtimeCommands.CreateEntity("Runtime queued");
		Check(runtimeCommands.Flush(error) && runtime.FindEntityByUUID(runtimeId)
			&& delivered.empty() && runtime.GetPendingRuntimeEntityCreateCount() == 1,
			"runtime command buffer bypassed the Scene safe-point batch");
		runtime.OnRuntimeStep();
		Check(delivered == std::vector<TomCat::UUID>{ runtimeId }
			&& runtime.GetPendingRuntimeEntityCreateCount() == 0,
			"runtime command-buffer entity was not published at the safe point");
		runtime.OnRuntimeStop();
	}

}
