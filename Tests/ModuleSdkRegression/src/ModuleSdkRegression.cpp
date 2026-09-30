#include <TomCat/Asset/AssetManager.h>
#include <TomCat/Core/Log.h>
#include <TomCat/Core/Version.h>
#include <TomCat/Module/ModuleSystem.h>
#include <TomCat/Scene/ComponentRegistry.h>
#include <TomCat/Scene/Entity.h>
#include <TomCat/Scene/Scene.h>
#include <TomCat/Scene/SceneSerializer.h>

#include <yaml-cpp/yaml.h>

#ifdef TC_PLATFORM_WINDOWS
	#include <Windows.h>
#endif

#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

	constexpr uint64_t WeatherTypeId = 0x7E57100000000001ULL;
	constexpr uint64_t WindSpeedPropertyId = 0x7E57100000000002ULL;
	constexpr uint64_t GustLevelPropertyId = 0x7E57100000000003ULL;

	void Require(bool condition, const std::string& message)
	{
		if (!condition)
			throw std::runtime_error(message);
	}

	class TemporaryDirectory
	{
	public:
		TemporaryDirectory()
		{
			const auto nonce = std::chrono::steady_clock::now().time_since_epoch()
				.count();
			Path = std::filesystem::temp_directory_path()
				/ ("TomCat-ModuleSdk-" + std::to_string(nonce));
			std::filesystem::create_directories(Path);
		}

		~TemporaryDirectory()
		{
			std::error_code error;
			std::filesystem::remove_all(Path, error);
		}

		std::filesystem::path Path;
	};

	// The regression executable and the module DLL are sibling premake
	// outputs: <workspace>/bin/<config>/<project>/<binary>.
	std::filesystem::path FindModuleLibrary()
	{
		std::error_code error;
		std::filesystem::path candidate = std::filesystem::current_path(error);
		if (error)
			candidate = std::filesystem::path(".");
		for (int depth = 0; depth < 8; ++depth)
		{
			const std::filesystem::path library = candidate
				/ "Tests/bin/Release-windows-x86_64/TestModule/TestModule.dll";
			if (std::filesystem::is_regular_file(library, error))
				return library;
			const std::filesystem::path debugLibrary = candidate
				/ "Tests/bin/Debug-windows-x86_64/TestModule/TestModule.dll";
			if (std::filesystem::is_regular_file(debugLibrary, error))
				return debugLibrary;
			if (!candidate.has_parent_path())
				break;
			candidate = candidate.parent_path();
		}
		throw std::runtime_error("the TestModule.dll fixture could not be located");
	}

	struct TestProject
	{
		TemporaryDirectory Root;
		std::filesystem::path ModuleDirectory;
		std::filesystem::path ScenePath;

		explicit TestProject(const std::filesystem::path& moduleLibrary)
		{
			ModuleDirectory = Root.Path / "Modules" / "TestWeather";
			std::filesystem::create_directories(ModuleDirectory / "lib");
			std::filesystem::copy_file(moduleLibrary,
				ModuleDirectory / "lib" / "TestModule.dll",
				std::filesystem::copy_options::overwrite_existing);
			WriteFile(ModuleDirectory / "module.tomcat",
				"ModuleVersion: 1\n"
				"Name: TestWeather\n"
				"DisplayName: Test Weather Module\n"
				"Version: 1.0.0\n"
				"EngineBuildID: " + std::string(TomCat::Version::EngineBuildID)
				+ "\nLibrary: lib/TestModule.dll\n");
			ScenePath = Root.Path / "scene.tomcat";
		}

		void WriteFile(const std::filesystem::path& path, const std::string& text)
		{
			std::ofstream output(path, std::ios::binary | std::ios::trunc);
			Require(static_cast<bool>(output), "could not write " + path.string());
			output << text;
		}
	};

	const TomCat::ComponentDescriptor& RequireWeatherDescriptor()
	{
		const TomCat::ComponentDescriptor* descriptor =
			TomCat::ComponentRegistry::Get().Find(TomCat::UUID(WeatherTypeId));
		Require(descriptor != nullptr, "the module component was not registered");
		return *descriptor;
	}

	void SetWeatherProperties(TomCat::Entity entity, float windSpeed,
		int32_t gustLevel)
	{
		const TomCat::ComponentDescriptor& descriptor = RequireWeatherDescriptor();
		std::string error;
		for (const TomCat::PropertyDescriptor& property : descriptor.Properties)
		{
			if (static_cast<uint64_t>(property.PropertyId) == WindSpeedPropertyId)
				Require(property.Set(entity, TomCat::PropertyValue(windSpeed),
					error), "WindSpeed set failed: " + error);
			if (static_cast<uint64_t>(property.PropertyId) == GustLevelPropertyId)
				Require(property.Set(entity, TomCat::PropertyValue(gustLevel),
					error), "GustLevel set failed: " + error);
		}
	}

	std::pair<float, int32_t> ReadWeatherProperties(TomCat::Entity entity)
	{
		const TomCat::ComponentDescriptor& descriptor = RequireWeatherDescriptor();
		std::pair<float, int32_t> values{ -1.0f, -1 };
		for (const TomCat::PropertyDescriptor& property : descriptor.Properties)
		{
			if (static_cast<uint64_t>(property.PropertyId) == WindSpeedPropertyId)
				values.first = std::get<float>(property.Get(entity));
			if (static_cast<uint64_t>(property.PropertyId) == GustLevelPropertyId)
				values.second = std::get<int32_t>(property.Get(entity));
		}
		return values;
	}

	TomCat::Ref<TomCat::Scene> CreateSceneWithWeather(uint64_t uuid)
	{
		auto scene = TomCat::CreateRef<TomCat::Scene>();
		TomCat::Entity entity = scene->CreateEntityWithUUID(
			TomCat::UUID(uuid), "WeatherEntity");
		Require(static_cast<bool>(entity), "entity could not be created");
		std::string error;
		Require(TomCat::ComponentRegistry::Get().Add(entity,
			TomCat::UUID(WeatherTypeId), error),
			"module component could not be added: " + error);
		SetWeatherProperties(entity, 2.5f, 3);
		return scene;
	}

	std::string LoadSceneText(const std::filesystem::path& path)
	{
		std::ifstream input(path, std::ios::binary);
		Require(static_cast<bool>(input), "could not open " + path.string());
		return { std::istreambuf_iterator<char>(input),
			std::istreambuf_iterator<char>() };
	}

	void TestModuleLoadAndRegistration(TestProject& project)
	{
		std::string error;
		const bool okLoad = TomCat::ModuleSystem::Get().LoadProjectModules(
			project.Root.Path, error);
		Require(okLoad, "module load failed: " + error);
		Require(TomCat::ModuleSystem::Get().HasModule("TestWeather"),
			"TestWeather was not reported loaded");
		RequireWeatherDescriptor();

		const auto& importer = TomCat::AssetManager::Get()
			.GetDatabase().GetImporters().Find(TomCat::AssetType::Other);
		Require(importer != nullptr, "no importer for AssetType::Other");
		Require(importer->GetID() == "TestModule.Weather",
			"the module importer was not registered");
	}

	void TestTypedComponentRoundTrip(TestProject& project)
	{
		TomCat::Ref<TomCat::Scene> scene = CreateSceneWithWeather(0xA1);
		TomCat::SceneSerializer serializer(scene);
		Require(serializer.Serialize(project.ScenePath),
			"scene serialization failed");

		auto loaded = TomCat::CreateRef<TomCat::Scene>();
		TomCat::SceneSerializer loader(loaded);
		Require(loader.Deserialize(project.ScenePath), "scene deserialization "
			"failed");
		TomCat::Entity entity = loaded->FindEntityByUUID(TomCat::UUID(0xA1));
		Require(static_cast<bool>(entity), "entity missing after load");
		Require(ReadWeatherProperties(entity) == std::make_pair(2.5f, 3),
			"module component properties did not round-trip");
	}

	void TestOpaqueRoundTripWhenModuleMissing(TestProject& project)
	{
		std::string error;
		const bool okUnload = TomCat::ModuleSystem::Get().UnloadAllModules(error);
		Require(okUnload, "module unload failed: " + error);
		Require(!TomCat::ModuleSystem::Get().HasModule("TestWeather"),
			"module still loaded after unload");
		Require(TomCat::ComponentRegistry::Get().Find(
			TomCat::UUID(WeatherTypeId)) == nullptr,
			"module component survived unload");

		// The saved scene must load losslessly with the module absent, keeping
		// the weather component as an opaque record.
		auto loaded = TomCat::CreateRef<TomCat::Scene>();
		TomCat::SceneSerializer loader(loaded);
		Require(loader.Deserialize(project.ScenePath),
			"scene without its module failed to load");
		TomCat::Entity entity = loaded->FindEntityByUUID(TomCat::UUID(0xA1));
		Require(static_cast<bool>(entity), "entity missing in opaque load");
		Require(entity.GetScene()->GetRegistry().all_of<TomCat::OpaqueComponents>(
			entity), "the module component did not become an opaque record");

		// Re-saving the opaque scene preserves the record byte-semantically.
		TomCat::SceneSerializer resaver(loaded);
		const std::filesystem::path resaved = project.Root.Path / "resaved.tomcat";
		Require(resaver.Serialize(resaved), "opaque resave failed");
		Require(LoadSceneText(resaved).find("TestModule.Weather")
			!= std::string::npos,
			"the opaque record lost the module component identity");

		// Reloading the module revives the opaque record.
		const bool okReload = TomCat::ModuleSystem::Get().LoadProjectModules(
			project.Root.Path, error);
		Require(okReload, "module reload failed: " + error);
		std::vector<TomCat::Entity> liveEntities;
		for (const TomCat::UUID uuid : loaded->GetEntityOrder())
		{
			TomCat::Entity live = loaded->FindEntityByUUID(uuid);
			if (live)
				liveEntities.push_back(live);
		}
		const auto modules = TomCat::ModuleSystem::Get().GetLoadedModules();
		Require(modules.size() == 1, "unexpected loaded module count");
		// ProviderId is derived from the module name by the host.
		uint64_t providerId = 0xcbf29ce484222325ULL;
		for (const char character : std::string("TestWeather"))
		{
			providerId ^= static_cast<uint64_t>(static_cast<uint8_t>(character));
			providerId *= 0x100000001b3ULL;
		}
		const bool okRehydrate = TomCat::ComponentRegistry::Get()
			.RehydrateOpaqueComponents(liveEntities, TomCat::UUID(providerId),
				error);
		Require(okRehydrate, "opaque rehydration failed: " + error);
		Require(ReadWeatherProperties(entity) == std::make_pair(2.5f, 3),
			"rehydrated component lost its values");
	}

	void TestEditorCommandRoundTrip(TestProject& project)
	{
		const auto commands = TomCat::ModuleSystem::Get().GetEditorCommands();
		Require(commands.size() == 1, "expected exactly one editor command");
		Require(commands[0].Label == "TestModule: Ping",
			"unexpected editor command label");
		Require(commands[0].ModuleName == "TestWeather",
			"editor command was not attributed to its module");
		commands[0].Callback();
		commands[0].Callback();

#ifdef TC_PLATFORM_WINDOWS
		const HMODULE library = ::GetModuleHandleW(L"TestModule.dll");
		Require(library != nullptr, "the module library is not loaded");
		const auto pingCount = reinterpret_cast<uint32_t(__cdecl*)()>(
			::GetProcAddress(library, "TestModulePingCount"));
		Require(pingCount != nullptr, "ping counter export is missing");
		Require(pingCount() == 2, "the editor command did not reach the module");
#endif

		std::string error;
		const bool okFinalUnload = TomCat::ModuleSystem::Get().UnloadAllModules(
			error);
		Require(okFinalUnload, "final unload failed: " + error);
	}

	void TestManifestValidation()
	{
		TemporaryDirectory directory;
		const std::filesystem::path manifest = directory.Path / "module.tomcat";
		{
			std::ofstream output(manifest, std::ios::binary | std::ios::trunc);
			output << "ModuleVersion: 1\nName: bad name!\nDisplayName: X\n"
				"Version: 1.0\nLibrary: lib/x.dll\n";
		}
		TomCat::ModuleManifest parsed;
		std::string error;
		Require(!TomCat::ModuleSystem::ParseManifest(manifest, parsed, error),
			"an invalid module name was accepted");

		std::ofstream output(manifest, std::ios::binary | std::ios::trunc);
		output << "ModuleVersion: 2\nName: ok\nDisplayName: X\nVersion: 1\n"
			"Library: lib/x.dll\n";
		Require(!TomCat::ModuleSystem::ParseManifest(manifest, parsed, error),
			"an unsupported manifest version was accepted");
	}

	void TestDisabledModuleIsSkipped(TestProject& project)
	{
		// Rewrite the manifest with Enabled: false.
		{
			std::ofstream output(project.ModuleDirectory / "module.tomcat",
				std::ios::binary | std::ios::trunc);
			output << "ModuleVersion: 1\nName: TestWeather\n"
				"DisplayName: Test Weather Module\nVersion: 1.0.0\nLibrary: "
				"lib/TestModule.dll\nEnabled: false\n";
		}
		std::string error;
		const bool okDisabled = TomCat::ModuleSystem::Get().LoadProjectModules(
			project.Root.Path, error);
		Require(okDisabled, "disabled module load failed: " + error);
		Require(!TomCat::ModuleSystem::Get().HasModule("TestWeather"),
			"a disabled module was loaded");
	}

}

int main()
{
	TomCat::Log::Init();
	try
	{
		std::string cleanupError;
		const bool okCleanup = TomCat::ModuleSystem::Get().UnloadAllModules(
			cleanupError);
		Require(okCleanup, "startup module cleanup failed: " + cleanupError);

		TestManifestValidation();

		const std::filesystem::path moduleLibrary = FindModuleLibrary();
		TestProject project(moduleLibrary);
		std::cout << "ENTER TestModuleLoadAndRegistration" << std::endl;
		TestModuleLoadAndRegistration(project);
		std::cout << "EXIT TestModuleLoadAndRegistration" << std::endl;
		std::cout << "ENTER TestTypedComponentRoundTrip" << std::endl;
		TestTypedComponentRoundTrip(project);
		std::cout << "EXIT TestTypedComponentRoundTrip" << std::endl;
		std::cout << "ENTER TestOpaqueRoundTripWhenModuleMissing" << std::endl;
		TestOpaqueRoundTripWhenModuleMissing(project);
		std::cout << "EXIT TestOpaqueRoundTripWhenModuleMissing" << std::endl;
		std::cout << "ENTER TestEditorCommandRoundTrip" << std::endl;
		TestEditorCommandRoundTrip(project);
		std::cout << "EXIT TestEditorCommandRoundTrip" << std::endl;
		std::cout << "ENTER TestDisabledModuleIsSkipped" << std::endl;
		TestDisabledModuleIsSkipped(project);
		std::cout << "EXIT TestDisabledModuleIsSkipped" << std::endl;

		std::cout << "PASS module SDK: manifest validation and discovery, "
			"versioned host handshake, component/importer/editor-command "
			"registration, typed scene round-trip, opaque round-trip while the "
			"module is missing, rehydration on reload and disabled-module "
			"handling\n";
		TomCat::Log::Shutdown();
		return 0;
	}
	catch (const std::exception& exception)
	{
		std::cerr << "FAIL module SDK regression: " << exception.what() << '\n';
		TomCat::Log::Shutdown();
		return 1;
	}
}
