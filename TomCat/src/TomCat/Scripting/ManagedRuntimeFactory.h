#pragma once

#include "IScriptRuntime.h"

#include <filesystem>
#include <memory>
#include <span>
#include <string>
#include <string_view>

namespace TomCat::Scripting {

	// Kept in its own translation unit so native-only tools and regression tests
	// that use ScriptEngine do not pull hostfxr/CoreCLR bootstrap code into their
	// executables.
	std::shared_ptr<IScriptRuntime> CreateManagedScriptRuntime(
		const std::filesystem::path& managedDirectory,
		const std::filesystem::path& projectAssembly,
		const std::filesystem::path& projectPdb = {},
		const std::filesystem::path& dotnetRoot = {},
		std::string* error = nullptr);

	// Browser builds link the .NET runtime and native engine into one WASM
	// module. Managed startup registers its UnmanagedCallersOnly API entry, then
	// cooked/editor assemblies are loaded directly from verified bytes.
	bool InstallWebManagedApi(GetManagedApiFn getManagedApi,
		std::string* error = nullptr);
	std::shared_ptr<IScriptRuntime> CreateWebManagedScriptRuntime(
		std::span<const uint8_t> assembly, std::span<const uint8_t> pdb = {},
		std::string* error = nullptr);

	bool IsManagedScriptReloadBlocked();
	std::string GetManagedScriptReloadBlockReason();
	void BlockManagedScriptReload(std::string_view reason);

}
