#pragma once

#include "TomCat/Asset/Asset.h"
#include "TomCat/Asset/ImporterRegistry.h"

#include <filesystem>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

namespace TomCat {

	struct ModuleManifest
	{
		std::string Name;
		std::string DisplayName;
		std::string Version;
		std::filesystem::path Library;
		std::string EngineBuildID;
		bool Enabled = true;

		bool operator==(const ModuleManifest&) const = default;
	};

	struct ModuleEditorCommand
	{
		std::string Label;
		std::string ModuleName;
		void (*Callback)() = nullptr;
	};

	struct LoadedModuleInfo
	{
		std::string Name;
		std::string DisplayName;
		std::string Version;
		std::filesystem::path Path;
	};

	// Discovers, validates and loads native modules below
	// <projectDirectory>/Modules. Modules are an editor/tooling extension
	// surface: cooked Player packages do not carry module libraries, and scenes
	// authored with module components round-trip through the existing opaque
	// missing-component path when a module is not loaded.
	class ModuleSystem
	{
	public:
		static ModuleSystem& Get();

		ModuleSystem(const ModuleSystem&) = delete;
		ModuleSystem& operator=(const ModuleSystem&) = delete;

		// Reads and validates Modules/<name>/module.tomcat without touching the
		// filesystem beyond reading the manifest.
		[[nodiscard]] static bool ParseManifest(
			const std::filesystem::path& manifestPath, ModuleManifest& manifest,
			std::string& error);

		// Every valid manifest below <projectDirectory>/Modules, sorted by name.
		// Disabled modules are included with Enabled=false.
		[[nodiscard]] static bool DiscoverModules(
			const std::filesystem::path& projectDirectory,
			std::vector<std::pair<std::filesystem::path, ModuleManifest>>& modules,
			std::string& error);

		// Loads every enabled module of the project. A module that fails to
		// load is reported and skipped; already-loaded modules stay loaded.
		[[nodiscard]] bool LoadProjectModules(
			const std::filesystem::path& projectDirectory, std::string& error);

		// Unloads every module in reverse load order. Registered components are
		// removed through ComponentRegistry::UnregisterProvider; live entities
		// must have been serialized or torn down by the caller first.
		[[nodiscard]] bool UnloadAllModules(std::string& error);

		[[nodiscard]] std::vector<LoadedModuleInfo> GetLoadedModules() const;
		[[nodiscard]] std::vector<ModuleEditorCommand> GetEditorCommands() const;
		[[nodiscard]] bool HasModule(const std::string& name) const;

		// Called by the host API while TomCatModuleMain runs on the current
		// module's behalf; attributes the command to that module.
		void PublishEditorCommand(const std::string& label,
			void (*callback)());
		// Attributes an importer registration to the module being loaded so
		// unload can remove it before the library is released.
		void RecordModuleImporter(AssetType type);

	private:
		ModuleSystem() = default;

		struct LoadedModule
		{
			ModuleManifest Manifest;
			std::filesystem::path Path;
			void* LibraryHandle = nullptr;
			uint64_t ProviderId = 0;
			std::vector<AssetType> RegisteredImporters;
		};

		[[nodiscard]] bool LoadModule(const std::filesystem::path& libraryPath,
			const ModuleManifest& manifest,
			const std::filesystem::path& projectDirectory, std::string& error);
		[[nodiscard]] bool UnloadModule(LoadedModule& loaded, std::string& error);

		std::vector<LoadedModule> m_Loaded;
		std::vector<ModuleEditorCommand> m_Commands;
		LoadedModule* m_PendingModule = nullptr;
		mutable std::mutex m_Mutex;
	};

}
