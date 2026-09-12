#pragma once

#include "IScriptRuntime.h"

#include <filesystem>
#include <memory>
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

	bool IsManagedScriptReloadBlocked();
	std::string GetManagedScriptReloadBlockReason();
	void BlockManagedScriptReload(std::string_view reason);

}
