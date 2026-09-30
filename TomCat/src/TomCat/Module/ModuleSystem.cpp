#include "tcpch.h"
#include "TomCat/Module/ModuleSystem.h"

#include "TomCat/Asset/AssetManager.h"
#include "TomCat/Core/Version.h"
#include "TomCat/Module/ModuleSdk.h"
#include "TomCat/Scene/ComponentRegistry.h"
#include "TomCat/Utils/FileSystemUtils.h"
#include "TomCat/Utils/PathUtils.h"

#include <algorithm>
#include <yaml-cpp/yaml.h>

#ifdef TC_PLATFORM_WINDOWS
	#include <Windows.h>
#endif

namespace TomCat {
	namespace {

		constexpr std::string_view ManifestName = "module.tomcat";

		bool IsSafeModuleName(std::string_view name)
		{
			if (name.empty() || name.size() > 64)
				return false;
			for (const char character : name)
			{
				const bool allowed = (character >= 'a' && character <= 'z')
					|| (character >= 'A' && character <= 'Z')
					|| (character >= '0' && character <= '9')
					|| character == '_' || character == '-' || character == '.';
				if (!allowed)
					return false;
			}
			return name.front() != '.' && name.back() != '.';
		}

		bool IsSafeRelativeLibraryPath(const std::filesystem::path& path)
		{
			if (path.empty() || path.is_absolute() || path.has_root_name()
				|| path.has_root_directory())
				return false;
			const std::filesystem::path normalized = path.lexically_normal();
			if (normalized.empty() || normalized == ".")
				return false;
			for (const auto& part : normalized)
			{
				if (part == ".." || part == "." || part.empty())
					return false;
			}
			return true;
		}

		void ModuleLogInfo(const char* message)
		{
			if (message)
				TC_Core_Info("[module] {0}", message);
		}

		void ModuleLogError(const char* message)
		{
			if (message)
				TC_Core_Error("[module] {0}", message);
		}

		int32_t ModuleRegisterComponent(
			TomCat::ComponentDescriptor descriptor)
		{
			std::string error;
			if (!ComponentRegistry::Get().Register(std::move(descriptor), error))
			{
				TC_Core_Error("[module] component registration rejected: {0}",
					error);
				return TomCatModule::ModuleStatusRejected;
			}
			return TomCatModule::ModuleStatusSuccess;
		}

		int32_t ModuleRegisterImporter(TomCat::AssetType type,
			std::shared_ptr<const TomCat::IAssetImporter> importer)
		{
			if (!importer || type == TomCat::AssetType::None)
				return TomCatModule::ModuleStatusInvalidArgument;
			if (!AssetManager::Get().GetDatabase().GetImporters().Register(
				std::move(importer), true))
			{
				TC_Core_Error("[module] importer registration rejected for type {0}",
					static_cast<int>(type));
				return TomCatModule::ModuleStatusRejected;
			}
			ModuleSystem::Get().RecordModuleImporter(type);
			return TomCatModule::ModuleStatusSuccess;
		}

		int32_t ModuleRegisterEditorCommand(const char* label,
			void (TC_MODULE_CALL* callback)())
		{
			if (!label || !*label || !callback)
				return TomCatModule::ModuleStatusInvalidArgument;
			ModuleSystem::Get().PublishEditorCommand(label, callback);
			return TomCatModule::ModuleStatusSuccess;
		}

		TomCatModule::ModuleHostApiV1 MakeHostApi()
		{
			TomCatModule::ModuleHostApiV1 host;
			host.RegisterComponent = &ModuleRegisterComponent;
			host.RegisterImporter = &ModuleRegisterImporter;
			host.RegisterEditorCommand = &ModuleRegisterEditorCommand;
			host.LogInfo = &ModuleLogInfo;
			host.LogError = &ModuleLogError;
			return host;
		}

		using TomCatModuleMainFn = uint32_t (TC_MODULE_CALL*)(
			const TomCatModule::ModuleHostApiV1*,
			const TomCatModule::ModuleContextV1*);

		uint64_t DeriveProviderID(std::string_view name)
		{
			uint64_t hash = 0xcbf29ce484222325ULL;
			for (const char character : name)
			{
				hash ^= static_cast<uint64_t>(static_cast<uint8_t>(character));
				hash *= 0x100000001b3ULL;
			}
			return hash == 0 ? 1 : hash;
		}

	}

	ModuleSystem& ModuleSystem::Get()
	{
		static ModuleSystem instance;
		return instance;
	}

	bool ModuleSystem::ParseManifest(const std::filesystem::path& manifestPath,
		ModuleManifest& manifest, std::string& error)
	{
		manifest = {};
		try
		{
			std::ifstream input(manifestPath, std::ios::binary);
			if (!input)
			{
				error = "could not open " + PathToUTF8(manifestPath);
				return false;
			}
			YAML::Node document = YAML::Load(input);
			if (!document.IsMap())
			{
				error = "module manifest is not a mapping: "
					+ PathToUTF8(manifestPath);
				return false;
			}
			static const char* RequiredFields[] = { "ModuleVersion", "Name",
				"DisplayName", "Version", "Library" };
			static const char* OptionalFields[] = { "EngineBuildID", "Enabled" };
			for (const char* field : RequiredFields)
			{
				if (!document[field])
				{
					error = std::string("module manifest is missing '") + field
						+ "': " + PathToUTF8(manifestPath);
					return false;
				}
			}
			for (auto entry = document.begin(); entry != document.end(); ++entry)
			{
				const std::string key = entry->first.as<std::string>();
				const bool known = std::any_of(std::begin(RequiredFields),
					std::end(RequiredFields), [&](const char* field)
					{
						return key == field;
					})
					|| std::any_of(std::begin(OptionalFields),
						std::end(OptionalFields), [&](const char* field)
						{
							return key == field;
						});
				if (!known)
				{
					error = "module manifest has an unknown field '" + key
						+ "': " + PathToUTF8(manifestPath);
					return false;
				}
			}
			if (document["ModuleVersion"].as<uint32_t>() != 1)
			{
				error = "module manifest ModuleVersion must be 1: "
					+ PathToUTF8(manifestPath);
				return false;
			}
			manifest.Name = document["Name"].as<std::string>();
			manifest.DisplayName = document["DisplayName"].as<std::string>();
			manifest.Version = document["Version"].as<std::string>();
			manifest.Library = UTF8ToPath(
				document["Library"].as<std::string>());
			if (document["EngineBuildID"])
				manifest.EngineBuildID =
					document["EngineBuildID"].as<std::string>();
			if (document["Enabled"])
				manifest.Enabled = document["Enabled"].as<bool>();
		}
		catch (const YAML::Exception& exception)
		{
			error = std::string("module manifest is not valid YAML: ")
				+ exception.what() + " (" + PathToUTF8(manifestPath) + ")";
			return false;
		}
		catch (const std::exception& exception)
		{
			error = exception.what();
			return false;
		}

		if (!IsSafeModuleName(manifest.Name)
			|| manifest.DisplayName.empty()
			|| manifest.DisplayName.size() > 128
			|| manifest.Version.empty()
			|| !IsSafeRelativeLibraryPath(manifest.Library))
		{
			error = "module manifest has an invalid identity or library path: "
				+ PathToUTF8(manifestPath);
			return false;
		}
		return true;
	}

	bool ModuleSystem::DiscoverModules(const std::filesystem::path& projectDirectory,
		std::vector<std::pair<std::filesystem::path, ModuleManifest>>& modules,
		std::string& error)
	{
		modules.clear();
		const std::filesystem::path modulesRoot = projectDirectory / "Modules";
		std::error_code code;
		if (!std::filesystem::is_directory(modulesRoot, code))
			return true;

		std::vector<std::filesystem::path> manifests;
		std::filesystem::directory_iterator iterator(modulesRoot, code);
		const std::filesystem::directory_iterator iteratorEnd;
		for (; !code && iterator != iteratorEnd; iterator.increment(code))
		{
			const std::filesystem::directory_entry& entry = *iterator;
			if (!entry.is_directory(code) || code)
				continue;
			const std::filesystem::path manifest = entry.path()
				/ std::string(ManifestName);
			if (std::filesystem::is_regular_file(manifest, code) && !code)
				manifests.push_back(manifest);
		}
		if (code)
		{
			error = "could not enumerate the project Modules directory: "
				+ code.message();
			return false;
		}

		std::sort(manifests.begin(), manifests.end());
		for (const std::filesystem::path& manifestPath : manifests)
		{
			ModuleManifest manifest;
			if (!ParseManifest(manifestPath, manifest, error))
				return false;
			modules.emplace_back(manifestPath, std::move(manifest));
		}
		return true;
	}

	bool ModuleSystem::LoadProjectModules(
		const std::filesystem::path& projectDirectory, std::string& error)
	{
		std::vector<std::pair<std::filesystem::path, ModuleManifest>> discovered;
		if (!DiscoverModules(projectDirectory, discovered, error))
			return false;

		// TomCatModuleMain reenters the host API (command registration), so
		// the module mutex must not be held across module entry points.
		for (const auto& [manifestPath, manifest] : discovered)
		{
			if (!manifest.Enabled)
				continue;
			const bool alreadyLoaded = [&manifest]()
			{
				std::lock_guard lock(ModuleSystem::Get().m_Mutex);
				return std::any_of(ModuleSystem::Get().m_Loaded.begin(),
					ModuleSystem::Get().m_Loaded.end(),
					[&manifest](const LoadedModule& loaded)
					{
						return loaded.Manifest.Name == manifest.Name;
					});
			}();
			if (alreadyLoaded)
				continue;
			const std::filesystem::path libraryPath = manifestPath.parent_path()
				/ manifest.Library;
			std::string moduleError;
			if (!LoadModule(libraryPath, manifest, projectDirectory, moduleError))
			{
				TC_Core_Error("Module '{0}' failed to load: {1}", manifest.Name,
					moduleError);
			}
		}
		return true;
	}

	bool ModuleSystem::LoadModule(const std::filesystem::path& libraryPath,
		const ModuleManifest& manifest,
		const std::filesystem::path& projectDirectory, std::string& error)
	{
		std::error_code fileCode;
		if (!std::filesystem::is_regular_file(libraryPath, fileCode))
		{
			error = "module library is missing: " + PathToUTF8(libraryPath);
			return false;
		}

#ifdef TC_PLATFORM_WINDOWS
		HMODULE library = ::LoadLibraryW(libraryPath.c_str());
		if (!library)
		{
			error = "LoadLibrary failed with error "
				+ std::to_string(::GetLastError());
			return false;
		}
		const auto mainFn = reinterpret_cast<TomCatModuleMainFn>(
			::GetProcAddress(library, "TomCatModuleMain"));
#else
		void* library = dlopen(PathToUTF8(libraryPath).c_str(), RTLD_NOW | RTLD_LOCAL);
		if (!library)
		{
			error = "dlopen failed";
			return false;
		}
		const auto mainFn = reinterpret_cast<TomCatModuleMainFn>(
			dlsym(library, "TomCatModuleMain"));
#endif
		if (!mainFn)
		{
			error = "the library does not export TomCatModuleMain";
#ifdef TC_PLATFORM_WINDOWS
			::FreeLibrary(library);
#else
			dlclose(library);
#endif
			return false;
		}

		LoadedModule loaded;
		loaded.Manifest = manifest;
		loaded.Path = libraryPath;
		loaded.LibraryHandle = library;

		// Registration context: modules stamp descriptors with their own
		// provider identity derived from the module name so unload can remove
		// exactly what the module added.
		const std::string moduleDirectory =
			PathToUTF8(libraryPath.parent_path());
		TomCatModule::ModuleContextV1 context;
		context.EngineBuildID = TomCat::Version::EngineBuildID.data();
		context.ProjectDirectory = PathToUTF8(projectDirectory).c_str();
		context.ModuleDirectory = moduleDirectory.c_str();
		context.ProviderId = loaded.ProviderId = DeriveProviderID(
			manifest.Name);

		const TomCatModule::ModuleHostApiV1 host = MakeHostApi();
		// Publish the pending module so RegisterEditorCommand can attribute
		// commands during TomCatModuleMain.
		m_PendingModule = &loaded;
		const uint32_t status = mainFn(&host, &context);
		m_PendingModule = nullptr;
		if (status != TomCatModule::ModuleStatusSuccess)
		{
			error = "TomCatModuleMain returned status "
				+ std::to_string(status);
			// Roll back anything the module registered before failing.
			std::string rollbackError;
			(void)UnloadModule(loaded, rollbackError);
			return false;
		}
		{
			std::lock_guard lock(m_Mutex);
			m_Loaded.push_back(std::move(loaded));
		}
		TC_Core_Info("Module '{0}' ({1}) loaded from {2}", manifest.Name,
			manifest.Version, PathToUTF8(libraryPath));
		return true;
	}

	bool ModuleSystem::UnloadModule(LoadedModule& loaded, std::string& error)
	{
		if (loaded.ProviderId != 0)
		{
			// No live entities here: module unloads happen between editor scene
			// teardown and project switch, or at exit.
			static const std::vector<Entity> noEntities;
			if (!ComponentRegistry::Get().UnregisterProvider(
				UUID(loaded.ProviderId), noEntities, error))
				return false;
		}
		// Module-registered importers own code inside the library; they must be
		// released before FreeLibrary or their destructors run into unmapped
		// memory at shutdown.
		ImporterRegistry& importers = AssetManager::Get().GetDatabase()
			.GetImporters();
		for (const AssetType type : loaded.RegisteredImporters)
			(void)importers.Unregister(type);
		loaded.RegisteredImporters.clear();
		m_Commands.erase(std::remove_if(m_Commands.begin(), m_Commands.end(),
			[&loaded](const ModuleEditorCommand& command)
			{
				return command.ModuleName == loaded.Manifest.Name;
			}), m_Commands.end());
#ifdef TC_PLATFORM_WINDOWS
		if (loaded.LibraryHandle && !::FreeLibrary(
			static_cast<HMODULE>(loaded.LibraryHandle)))
		{
			error = "FreeLibrary failed with error "
				+ std::to_string(::GetLastError());
			return false;
		}
#else
		if (loaded.LibraryHandle)
			dlclose(loaded.LibraryHandle);
#endif
		loaded.LibraryHandle = nullptr;
		return true;
	}

	bool ModuleSystem::UnloadAllModules(std::string& error)
	{
		std::lock_guard lock(m_Mutex);
		bool allUnloaded = true;
		for (auto iterator = m_Loaded.rbegin(); iterator != m_Loaded.rend();
			++iterator)
		{
			std::string moduleError;
			if (!UnloadModule(*iterator, moduleError))
			{
				TC_Core_Error("Module '{0}' failed to unload: {1}",
					iterator->Manifest.Name, moduleError);
				allUnloaded = false;
			}
		}
		m_Loaded.clear();
		m_Commands.clear();
		return allUnloaded;
	}

	void ModuleSystem::RecordModuleImporter(AssetType type)
	{
		std::lock_guard lock(m_Mutex);
		if (m_PendingModule)
			m_PendingModule->RegisteredImporters.push_back(type);
	}

	void ModuleSystem::PublishEditorCommand(const std::string& label,
		void (*callback)())
	{
		std::lock_guard lock(m_Mutex);
		ModuleEditorCommand command;
		command.Label = label;
		command.Callback = callback;
		command.ModuleName = m_PendingModule ? m_PendingModule->Manifest.Name
			: std::string();
		m_Commands.push_back(std::move(command));
	}

	std::vector<LoadedModuleInfo> ModuleSystem::GetLoadedModules() const
	{
		std::lock_guard lock(m_Mutex);
		std::vector<LoadedModuleInfo> info;
		for (const LoadedModule& loaded : m_Loaded)
		{
			info.push_back(LoadedModuleInfo{ loaded.Manifest.Name,
				loaded.Manifest.DisplayName, loaded.Manifest.Version,
				loaded.Path });
		}
		return info;
	}

	std::vector<ModuleEditorCommand> ModuleSystem::GetEditorCommands() const
	{
		std::lock_guard lock(m_Mutex);
		return m_Commands;
	}

	bool ModuleSystem::HasModule(const std::string& name) const
	{
		std::lock_guard lock(m_Mutex);
		return std::any_of(m_Loaded.begin(), m_Loaded.end(),
			[&name](const LoadedModule& loaded)
			{
				return loaded.Manifest.Name == name;
			});
	}

}
