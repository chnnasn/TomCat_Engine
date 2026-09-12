#pragma once

#include "ScriptTypes.h"

#include <span>
#include <string_view>

namespace TomCat::Scripting {

	// Runtime-facing contract used by Scene. Tests can install a fake backend to
	// verify ordering without starting CoreCLR; ManagedScriptRuntime implements the
	// same batched calls through ManagedApiV1.
	class IScriptRuntime
	{
	public:
		virtual ~IScriptRuntime() = default;

		virtual bool IsReady() const = 0;
		virtual ScriptStatus CreateSceneRuntime(uint64_t sceneSessionId,
			uint64_t runtimeGeneration) = 0;
		virtual ScriptStatus InstantiateAll(
			std::span<const NativeScriptAttachmentV1> attachments) = 0;
		virtual ScriptStatus ApplySerializedFields(std::string_view fieldsJson) = 0;
		virtual ScriptStatus InvokeCreateAll() = 0;
		virtual ScriptStatus SetEnabled(uint64_t attachmentId, bool enabled) = 0;
		virtual ScriptStatus UpdateAll(float deltaTime) = 0;
		virtual ScriptStatus FixedUpdateAll(float fixedDeltaTime) = 0;
		virtual ScriptStatus DispatchPhysicsEvents(
			std::span<const NativePhysicsEventV1> events) = 0;
		virtual ScriptStatus DestroyAll() = 0;
		virtual ScriptStatus DestroyAttachments(std::span<const uint64_t>)
		{
			return ScriptStatus::Success;
		}
		virtual bool ReadProjectMetadata(std::string&) { return false; }
		// Collectible managed runtimes use this to cooperatively verify that their
		// project domain has unloaded. Native/fake runtimes have nothing to poll.
		virtual bool PollUnload() { return true; }
		// Managed runtimes use this terminal notification to prevent another
		// project generation from loading after a collectible domain leaks.
		virtual void OnUnloadFailed(std::string_view) {}
	};

}
