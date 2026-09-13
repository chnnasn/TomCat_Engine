#pragma once

#include <TomCat/Asset/Asset.h>
#include <TomCat/Core/Base.h>

#include <filesystem>
#include <functional>
#include <string>
#include <vector>

namespace TomCat {

	class Project;

	struct PlayerBuildRequest
	{
		Ref<Project> ProjectInstance;
		AssetHandle EntryScene = AssetHandle(0);
		std::filesystem::path TemplateDirectory;
		std::vector<uint8_t> ManagedAssembly;
		std::string ScriptManifestJson;
		std::string ScriptBuildID;
		std::function<bool()> ValidateBeforePublish;
	};

	struct PlayerBuildResult
	{
		bool Succeeded = false;
		std::filesystem::path OutputDirectory;
		std::filesystem::path PlayerExecutable;
		std::string Message;
	};

	class PlayerBuilder final
	{
	public:
		static std::filesystem::path FindDefaultTemplateDirectory();
		static PlayerBuildResult Build(PlayerBuildRequest request);
		static bool Launch(const std::filesystem::path& executable,
			const std::filesystem::path& workingDirectory, std::string& errorMessage);
	};

}
