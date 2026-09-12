#include "tcpch.h"
#include "ManagedRuntimeFactory.h"

#include "ManagedScriptRuntime.h"
#include "ScriptGlue.h"

#include <atomic>
#include <mutex>

namespace TomCat::Scripting {
	namespace {
		std::atomic_bool s_ReloadBlocked{ false };
		std::mutex s_ReloadBlockMutex;
		std::string s_ReloadBlockReason;
	}

	bool IsManagedScriptReloadBlocked()
	{
		return s_ReloadBlocked.load(std::memory_order_acquire);
	}

	std::string GetManagedScriptReloadBlockReason()
	{
		std::lock_guard<std::mutex> lock(s_ReloadBlockMutex);
		return s_ReloadBlockReason;
	}

	void BlockManagedScriptReload(std::string_view reason)
	{
		{
			std::lock_guard<std::mutex> lock(s_ReloadBlockMutex);
			if (s_ReloadBlockReason.empty())
				s_ReloadBlockReason = reason.empty()
					? "A collectible AssemblyLoadContext did not unload"
					: std::string(reason);
		}
		s_ReloadBlocked.store(true, std::memory_order_release);
		TC_Core_Error("C# script reloads are blocked for this process: {0}. Restart the Editor.",
			GetManagedScriptReloadBlockReason());
	}

	std::shared_ptr<IScriptRuntime> CreateManagedScriptRuntime(
		const std::filesystem::path& managedDirectory,
		const std::filesystem::path& projectAssembly,
		const std::filesystem::path& projectPdb,
		const std::filesystem::path& dotnetRoot,
		std::string* error)
	{
		if (IsManagedScriptReloadBlocked())
		{
			if (error)
				*error = "Managed script reload is blocked until restart: "
					+ GetManagedScriptReloadBlockReason();
			return {};
		}
		auto runtime = std::make_shared<ManagedScriptRuntime>();
		DotNetHost::Configuration configuration;
		configuration.RuntimeConfigPath = managedDirectory
			/ "TomCat.ScriptHost.runtimeconfig.json";
		configuration.ScriptHostAssemblyPath = managedDirectory
			/ "TomCat.ScriptHost.dll";
		configuration.DotNetRoot = dotnetRoot;
		if (!runtime->Initialize(configuration, BuildNativeApiV1())
			|| !runtime->SetProjectAssembly(projectAssembly, projectPdb))
		{
			if (error)
				*error = runtime->GetLastError();
			return {};
		}
		if (error)
			error->clear();
		return runtime;
	}

}
