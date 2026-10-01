// A minimal native TomCat module exercising the Module SDK: one registered
// component (generic property codec), one importer replacement and one editor
// command. The module links nothing from the engine; everything crosses the
// boundary through the host API table and header-inline entity access.

#include "TomCat/Module/ModuleSdk.h"

#include <atomic>
#include <string>
#include <yaml-cpp/yaml.h>

namespace {

	struct ModuleWeather
	{
		float WindSpeed = 0.0f;
		int32_t GustLevel = 0;
	};

	constexpr uint64_t WeatherTypeId = 0x7E57100000000001ULL;
	constexpr uint64_t WindSpeedPropertyId = 0x7E57100000000002ULL;
	constexpr uint64_t GustLevelPropertyId = 0x7E57100000000003ULL;

	std::atomic<uint32_t> g_PingCount{ 0 };
    bool g_Player = false;
    void (TC_MODULE_CALL* g_LogInfo)(const char*) = nullptr;

	bool HasWeather(TomCat::Entity entity)
	{
		return entity && entity.GetScene()
			&& entity.GetScene()->GetRegistry().all_of<ModuleWeather>(entity);
	}

	ModuleWeather& WeatherOf(TomCat::Entity entity)
	{
		return entity.GetScene()->GetRegistry().get<ModuleWeather>(entity);
	}

	TomCat::PropertyDescriptor FloatProperty(uint64_t id, const char* name,
		float defaultValue)
	{
		TomCat::PropertyDescriptor property;
		property.PropertyId = TomCat::UUID(id);
		property.StableName = name;
		property.DisplayName = name;
		property.Kind = TomCat::PropertyKind::Float;
		property.Get = [](TomCat::Entity entity) -> TomCat::PropertyValue
		{
			return WeatherOf(entity).WindSpeed;
		};
		property.Set = [](TomCat::Entity entity,
			const TomCat::PropertyValue& value, std::string& error)
		{
			if (!HasWeather(entity) || !std::holds_alternative<float>(value))
			{
				error = "WindSpeed requires a live weather component and a float";
				return false;
			}
			WeatherOf(entity).WindSpeed = std::get<float>(value);
            if (g_Player && g_LogInfo) { const auto message = "MODULE_TYPED WindSpeed=" + std::to_string(WeatherOf(entity).WindSpeed); g_LogInfo(message.c_str()); }
			return true;
		};
		property.DefaultValue = defaultValue;
		return property;
	}

	TomCat::PropertyDescriptor IntProperty(uint64_t id, const char* name,
		int32_t defaultValue)
	{
		TomCat::PropertyDescriptor property;
		property.PropertyId = TomCat::UUID(id);
		property.StableName = name;
		property.DisplayName = name;
		property.Kind = TomCat::PropertyKind::Int32;
		property.Get = [](TomCat::Entity entity) -> TomCat::PropertyValue
		{
			return WeatherOf(entity).GustLevel;
		};
		property.Set = [](TomCat::Entity entity,
			const TomCat::PropertyValue& value, std::string& error)
		{
			if (!HasWeather(entity) || !std::holds_alternative<int32_t>(value))
			{
				error = "GustLevel requires a live weather component and an int32";
				return false;
			}
			WeatherOf(entity).GustLevel = std::get<int32_t>(value);
            if (g_Player && g_LogInfo) { const auto message = "MODULE_TYPED GustLevel=" + std::to_string(WeatherOf(entity).GustLevel); g_LogInfo(message.c_str()); }
			return true;
		};
		property.DefaultValue = defaultValue;
		return property;
	}

	class TestWeatherImporter final : public TomCat::IAssetImporter
	{
	public:
		std::string_view GetID() const noexcept override
		{
			return "TestModule.Weather";
		}
		uint32_t GetVersion() const noexcept override { return 1; }
		TomCat::AssetType GetAssetType() const noexcept override
		{
			return TomCat::AssetType::Other;
		}
		TomCat::AssetImportResult Import(
			const TomCat::AssetImportRequest& request) const override
		{
			TomCat::AssetImportResult result;
			result.Format = "TestModule.Weather";
			result.ArtifactBytes = { request.SourceBytes.begin(),
				request.SourceBytes.end() };
			return result;
		}
	};

}

TC_MODULE_EXPORT uint32_t TC_MODULE_CALL TomCatModuleMain(
	const TomCatModule::ModuleHostApiV1* host,
	const TomCatModule::ModuleContextV1* context)
{
	if (!host || !context
		|| host->Version != TomCatModule::ModuleAbiCurrent
		|| context->ProviderId == 0 || context->Size < sizeof(TomCatModule::ModuleContextV1))
		return 1;

    g_Player = context->Host == TomCatModule::HostKind::Player;
    g_LogInfo = host->LogInfo;
	TomCat::ComponentDescriptor descriptor;
	descriptor.TypeId = TomCat::UUID(WeatherTypeId);
	descriptor.StableName = "TestModule.Weather";
	descriptor.DisplayName = "Weather (Test Module)";
	descriptor.SchemaVersion = 1;
	descriptor.ProviderId = TomCat::UUID(context->ProviderId);
	descriptor.Removable = true;
	descriptor.AddableInInspector = true;
	descriptor.DecodeIntoExisting = true;

	descriptor.Has = [](TomCat::Entity entity)
	{
		return HasWeather(entity);
	};
	descriptor.Add = [](TomCat::Entity entity, std::string& error)
	{
		if (!entity || !entity.GetScene())
		{
			error = "Weather requires a live entity";
			return false;
		}
		// Module components use the registry directly: Scene::OnComponentAdded
		// hooks exist for built-in components only.
		entity.GetScene()->GetRegistry().emplace<ModuleWeather>(entity);
		return true;
	};
	descriptor.Remove = [](TomCat::Entity entity, std::string& error)
	{
		if (!HasWeather(entity))
		{
			error = "Weather is not present on the entity";
			return false;
		}
		entity.GetScene()->GetRegistry().remove<ModuleWeather>(entity);
		return true;
	};
	descriptor.Copy = [](TomCat::Entity source, TomCat::Entity destination,
		std::string& error)
	{
		if (!HasWeather(source) || !destination || !destination.GetScene())
		{
			error = "Weather copy requires live source and destination";
			return false;
		}
		const ModuleWeather copy = WeatherOf(source);
		destination.GetScene()->GetRegistry().emplace_or_replace<ModuleWeather>(
			destination, copy);
		return true;
	};

	descriptor.Properties.push_back(FloatProperty(WindSpeedPropertyId,
		"WindSpeed", 0.0f));
	descriptor.Properties.push_back(IntProperty(GustLevelPropertyId,
		"GustLevel", 0));

	if (host->RegisterComponent(std::move(descriptor))
		!= TomCatModule::ModuleStatusSuccess)
		return 2;
	if (context->Host != TomCatModule::HostKind::Player && host->RegisterImporter(TomCat::AssetType::Other,
		std::make_shared<TestWeatherImporter>())
		!= TomCatModule::ModuleStatusSuccess)
		return 3;
	if (context->Host != TomCatModule::HostKind::Player && host->RegisterEditorCommand("TestModule: Ping", []()
		{
			++g_PingCount;
		}) != TomCatModule::ModuleStatusSuccess)
		return 4;
	host->LogInfo("TestModule loaded");
	return TomCatModule::ModuleStatusSuccess;
}

TC_MODULE_EXPORT uint32_t TC_MODULE_CALL TestModulePingCount()
{
	return g_PingCount.load(std::memory_order_acquire);
}
