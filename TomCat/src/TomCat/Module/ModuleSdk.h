#pragma once

// TomCat Module SDK — the stable, versioned entry surface for native engine
// modules. A module is a shared library plus a module.tomcat manifest that
// registers components, asset importers and editor commands through the host
// API table handed to its TomCatModuleMain export.
//
// Compatibility contract (v1):
//   * Modules are built against the same engine headers, the same MSVC
//     toolchain and the same entt/yaml-cpp revisions as the host binary. The
//     host verifies a nonempty manifest EngineBuildID before loading the
//     library. An omitted/empty ID is unpinned and is the module author's
//     responsibility. The module validates the host's ModuleAbiVersion in
//     TomCatModuleMain before registering capabilities.
//   * The host table below is append-only: new capabilities are added as new
//     trailing fields and a new capability version. Never reorder or remove
//     existing fields.
//   * Everything the host hands out is owned by the host. Modules must not
//     free host strings or keep host pointers beyond their callbacks.
//   * All registration happens on the main thread inside TomCatModuleMain.

#include "TomCat/Asset/Importer.h"
#include "TomCat/Scene/ComponentRegistry.h"
#include "TomCat/Scene/Entity.h"

#include <cstdint>
#include <functional>
#include <memory>

#if defined(_WIN32)
	#define TC_MODULE_EXPORT extern "C" __declspec(dllexport)
	#define TC_MODULE_CALL __cdecl
#else
	#define TC_MODULE_EXPORT extern "C" __attribute__((visibility("default")))
	#define TC_MODULE_CALL
#endif

namespace TomCatModule {

	inline constexpr uint32_t ModuleAbiCurrent = 1;
	enum class HostKind : uint32_t { Editor = 0, Tool = 1, Player = 2 };

	// Sent to TomCatModuleMain once per load, before any registration call.
	struct ModuleContextV1
	{
		uint32_t Version = ModuleAbiCurrent;
		uint32_t Size = sizeof(ModuleContextV1);
		const char* EngineBuildID = nullptr;
		const char* ProjectDirectory = nullptr;
		const char* ModuleDirectory = nullptr;
		// Derived from the manifest Name; stamp every ComponentDescriptor the
		// module registers with this ProviderId so the engine can remove the
		// module's components exactly on unload.
		uint64_t ProviderId = 0;
		HostKind Host = HostKind::Editor;
	};

	// The stable host surface. Every call returns a TomCatScriptStatus-style
	// code: 0 = success, negative = failure (see the status constants below).
	struct ModuleHostApiV1
	{
		uint32_t Version = ModuleAbiCurrent;
		uint32_t Size = sizeof(ModuleHostApiV1);

		// Registers a complete ComponentDescriptor. The descriptor must be
		// unique by TypeId and StableName; ProviderId must be the module's
		// nonzero provider identity. The engine takes ownership.
		int32_t (TC_MODULE_CALL* RegisterComponent)(
			TomCat::ComponentDescriptor descriptor) = nullptr;

		// Registers (or replaces) the importer responsible for one asset type.
		int32_t (TC_MODULE_CALL* RegisterImporter)(
			TomCat::AssetType type,
			std::shared_ptr<const TomCat::IAssetImporter> importer) = nullptr;

		// Registers an editor menu command. Commands are inert in the Player
		// and in headless tools. The label pointer is copied; the callback must
		// remain valid until the module unloads.
		int32_t (TC_MODULE_CALL* RegisterEditorCommand)(const char* label,
			void (TC_MODULE_CALL* callback)()) = nullptr;

		void (TC_MODULE_CALL* LogInfo)(const char* message) = nullptr;
		void (TC_MODULE_CALL* LogError)(const char* message) = nullptr;
	};

	inline constexpr int32_t ModuleStatusSuccess = 0;
	inline constexpr int32_t ModuleStatusInvalidArgument = -1;
	inline constexpr int32_t ModuleStatusRejected = -2; // duplicate/invalid registration
	inline constexpr int32_t ModuleStatusUnavailable = -3; // editor-only surface in Player

}

// The one export every module provides. Return 0 after a successful load;
// nonzero aborts the load and the engine unloads the library. Aborting after
// a partial registration is safe: the engine rolls the module back.
TC_MODULE_EXPORT uint32_t TC_MODULE_CALL TomCatModuleMain(
	const TomCatModule::ModuleHostApiV1* host,
	const TomCatModule::ModuleContextV1* context);
