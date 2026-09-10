#include "tcpch.h"
#include "Project.h"
#include "TomCat/Utils/FileSystemUtils.h"
#include "TomCat/Utils/PathUtils.h"

#include <algorithm>
#include <chrono>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <system_error>

#include <yaml-cpp/yaml.h>

namespace TomCat {

	namespace {

		template<typename T>
		T ReadOptional(const YAML::Node& node, const char* key, const T& fallback)
		{
			const YAML::Node value = node[key];
			return value ? value.as<T>() : fallback;
		}

		bool IsSafeRelativePath(const std::filesystem::path& path, bool allowEmpty = false)
		{
			if (path.empty())
				return allowEmpty;
			if (path.is_absolute() || path.has_root_name() || path.has_root_directory())
				return false;
			const std::filesystem::path normalized = path.lexically_normal();
			for (const auto& part : normalized)
			{
				if (part == "..")
					return false;
			}
			return normalized != ".";
		}

		std::filesystem::path AbsoluteNormalized(const std::filesystem::path& path)
		{
			std::error_code error;
			std::filesystem::path result = std::filesystem::weakly_canonical(path, error);
			if (!error)
				return result;
			error.clear();
			result = std::filesystem::absolute(path, error);
			return error ? path.lexically_normal() : result.lexically_normal();
		}

		bool IsPathWithinOrEqual(const std::filesystem::path& root, const std::filesystem::path& candidate)
		{
			std::error_code error;
			const std::filesystem::path relative = std::filesystem::relative(
				AbsoluteNormalized(candidate), AbsoluteNormalized(root), error);
			if (error || relative.is_absolute())
				return false;
			for (const auto& part : relative)
			{
				if (part == "..")
					return false;
			}
			return true;
		}

		std::filesystem::path ResolveBrowserPath(const std::string& storedPath,
			const std::filesystem::path& projectDirectory,
			const std::filesystem::path& assetRoot)
		{
			if (storedPath.empty())
				return {};

			std::filesystem::path value = UTF8ToPath(storedPath);
			if (value.is_absolute())
				return IsPathWithinOrEqual(assetRoot, value) ? AbsoluteNormalized(value) : std::filesystem::path{};

			const std::filesystem::path legacyCandidate = AbsoluteNormalized(value);
			if (IsPathWithinOrEqual(assetRoot, legacyCandidate))
				return legacyCandidate;
			const std::filesystem::path projectCandidate = AbsoluteNormalized(projectDirectory / value);
			if (IsPathWithinOrEqual(assetRoot, projectCandidate))
				return projectCandidate;
			const std::filesystem::path assetCandidate = AbsoluteNormalized(assetRoot / value);
			return IsPathWithinOrEqual(assetRoot, assetCandidate) ? assetCandidate : std::filesystem::path{};
		}

		std::string StoreAssetRelativePath(const std::filesystem::path& absolutePath,
			const std::filesystem::path& assetRoot)
		{
			if (absolutePath.empty() || !IsPathWithinOrEqual(assetRoot, absolutePath))
				return {};

			std::error_code error;
			const std::filesystem::path relative = std::filesystem::relative(
				AbsoluteNormalized(absolutePath), AbsoluteNormalized(assetRoot), error);
			if (error || relative.empty() || relative.is_absolute())
				return {};
			for (const auto& part : relative)
			{
				if (part == "..")
					return {};
			}
			return PathToUTF8(relative.lexically_normal());
		}

		bool NormalizeAndValidateConfig(ProjectConfig& config,
			const std::filesystem::path& projectDirectory,
			std::string& errorMessage)
		{
			if (config.Name.empty())
			{
				errorMessage = "Project.Name cannot be empty";
				return false;
			}
			if (config.Template != "2D" && config.Template != "3D")
			{
				errorMessage = "Project.Template must be either '2D' or '3D'";
				return false;
			}
			if (!IsSafeRelativePath(config.AssetDirectory))
			{
				errorMessage = "Project.AssetDirectory must be a non-empty relative path inside the project";
				return false;
			}
			config.AssetDirectory = config.AssetDirectory.lexically_normal();

			if (!IsSafeRelativePath(config.StartScene))
			{
				errorMessage = "Project.StartScene must be a relative path inside AssetDirectory";
				return false;
			}
			config.StartScene = config.StartScene.lexically_normal();

			const std::filesystem::path assetRoot = AbsoluteNormalized(projectDirectory / config.AssetDirectory);
			if (!config.TwoColumnCurrentFolder.empty())
			{
				const std::filesystem::path resolved = ResolveBrowserPath(
					config.TwoColumnCurrentFolder, projectDirectory, assetRoot);
				config.TwoColumnCurrentFolder = StoreAssetRelativePath(resolved, assetRoot);
			}

			std::vector<std::string> validExpandedNodes;
			validExpandedNodes.reserve(config.ExpandedNodes.size());
			for (const std::string& node : config.ExpandedNodes)
			{
				const std::filesystem::path resolved = ResolveBrowserPath(node, projectDirectory, assetRoot);
				const std::string relative = StoreAssetRelativePath(resolved, assetRoot);
				if (!relative.empty())
					validExpandedNodes.push_back(relative);
			}
			config.ExpandedNodes = std::move(validExpandedNodes);
			return true;
		}

		std::string ReadWholeFile(const std::filesystem::path& path)
		{
			std::ifstream input(path, std::ios::binary);
			if (!input)
				return {};
			std::ostringstream stream;
			stream << input.rdbuf();
			return input.bad() ? std::string{} : stream.str();
		}

	}

	Project::Project(const std::filesystem::path& projectPath)
		: m_ProjectPath(projectPath.lexically_normal()), m_Directory(projectPath.parent_path())
	{
	}

	bool Project::SetStartScene(const std::filesystem::path& scenePath)
	{
		if (!IsSafeRelativePath(scenePath))
		{
			TC_Core_Error("Start scene must be a relative path inside the project's AssetDirectory: {0}",
				PathToUTF8(scenePath));
			return false;
		}
		m_Config.StartScene = scenePath.lexically_normal();
		return true;
	}

	void Project::UpdateLastOperationTime()
	{
		const auto now = std::chrono::system_clock::now();
		const std::time_t nowTime = std::chrono::system_clock::to_time_t(now);
		std::tm localTime{};
#ifdef _WIN32
		localtime_s(&localTime, &nowTime);
#else
		localtime_r(&nowTime, &localTime);
#endif
		std::ostringstream stream;
		stream << std::put_time(&localTime, "%Y-%m-%d %H:%M:%S");
		m_Config.LastOperationTime = stream.str();
	}

	void Project::Touch()
	{
		// Recency belongs to local Hub settings; opening a project must not rewrite
		// a shared or version-controlled Project.tcproj file.
		UpdateLastOperationTime();
	}

	std::string Project::GetLastOperationTimeAgo() const
	{
		if (m_Config.LastOperationTime.empty())
			return "Unknown";

		std::tm operationTime{};
		std::istringstream stream(m_Config.LastOperationTime);
		stream >> std::get_time(&operationTime, "%Y-%m-%d %H:%M:%S");
		if (stream.fail())
			return "Unknown";
		const double difference = std::difftime(std::time(nullptr), std::mktime(&operationTime));
		if (difference < 0)
			return "Unknown";

		constexpr double minute = 60.0;
		constexpr double hour = 3600.0;
		constexpr double day = 86400.0;
		constexpr double month = 30.0 * day;
		constexpr double year = 365.0 * day;
		if (difference < minute) return "Just now";
		if (difference < hour)
		{
			const int value = static_cast<int>(difference / minute);
			return std::to_string(value) + (value == 1 ? " minute ago" : " minutes ago");
		}
		if (difference < day)
		{
			const int value = static_cast<int>(difference / hour);
			return std::to_string(value) + (value == 1 ? " hour ago" : " hours ago");
		}
		if (difference < month)
		{
			const int value = static_cast<int>(difference / day);
			return std::to_string(value) + (value == 1 ? " day ago" : " days ago");
		}
		if (difference < year)
		{
			const int value = static_cast<int>(difference / month);
			return std::to_string(value) + (value == 1 ? " month ago" : " months ago");
		}
		const int value = static_cast<int>(difference / year);
		return std::to_string(value) + (value == 1 ? " year ago" : " years ago");
	}

	Ref<Project> Project::CreateNew(const std::filesystem::path& projectPath, const ProjectConfig& config)
	{
		if (projectPath.empty() || projectPath.extension() != ".tcproj")
		{
			TC_Core_Error("A project path must point to a .tcproj file: {0}", PathToUTF8(projectPath));
			return nullptr;
		}

		std::error_code error;
		if (std::filesystem::exists(projectPath, error) || error)
		{
			TC_Core_Error("Refusing to overwrite existing project file: {0}", PathToUTF8(projectPath));
			return nullptr;
		}

		std::filesystem::path projectDirectory = projectPath.parent_path();
		if (projectDirectory.empty())
		{
			projectDirectory = std::filesystem::current_path(error);
			if (error)
			{
				TC_Core_Error("Could not resolve the directory for new project '{0}'", PathToUTF8(projectPath));
				return nullptr;
			}
		}

		error.clear();
		const bool projectDirectoryExisted = std::filesystem::exists(projectDirectory, error);
		if (error)
		{
			TC_Core_Error("Could not inspect new project directory '{0}': {1}", PathToUTF8(projectDirectory), error.message());
			return nullptr;
		}
		if (projectDirectoryExisted)
		{
			if (!std::filesystem::is_directory(projectDirectory, error) || error)
			{
				TC_Core_Error("New project path is not a directory: {0}", PathToUTF8(projectDirectory));
				return nullptr;
			}

			std::filesystem::directory_iterator first(projectDirectory, error);
			if (error)
			{
				TC_Core_Error("Could not inspect new project directory '{0}': {1}", PathToUTF8(projectDirectory), error.message());
				return nullptr;
			}
			if (first != std::filesystem::directory_iterator{})
			{
				TC_Core_Error("Refusing to create a project in non-empty directory '{0}'", PathToUTF8(projectDirectory));
				return nullptr;
			}
		}

		std::filesystem::path normalizedProjectPath = projectPath;
		if (projectPath.parent_path().empty())
			normalizedProjectPath = projectDirectory / projectPath.filename();
		auto project = CreateRef<Project>(normalizedProjectPath);
		project->m_Config = config;
		std::string validationError;
		if (!NormalizeAndValidateConfig(project->m_Config, project->m_Directory, validationError))
		{
			TC_Core_Error("Cannot create project '{0}': {1}", PathToUTF8(projectPath), validationError);
			return nullptr;
		}

		const std::filesystem::path assetPath = project->GetAssetPath();
		std::vector<std::filesystem::path> missingDirectories;
		for (std::filesystem::path current = assetPath; !current.empty(); current = current.parent_path())
		{
			error.clear();
			const bool exists = std::filesystem::exists(current, error);
			if (error)
			{
				TC_Core_Error("Could not inspect directory '{0}' while creating project: {1}",
					PathToUTF8(current), error.message());
				return nullptr;
			}
			if (exists)
				break;
			missingDirectories.push_back(current);
			if (current == current.root_path() || current.parent_path() == current)
				break;
		}
		std::reverse(missingDirectories.begin(), missingDirectories.end());

		std::vector<std::filesystem::path> createdDirectories;
		auto rollbackCreatedDirectories = [&createdDirectories]()
		{
			for (auto iterator = createdDirectories.rbegin(); iterator != createdDirectories.rend(); ++iterator)
			{
				std::error_code cleanupError;
				// remove() only removes an empty directory. A concurrent writer's files
				// therefore prevent rollback instead of being deleted recursively.
				std::filesystem::remove(*iterator, cleanupError);
			}
		};

		for (const std::filesystem::path& directory : missingDirectories)
		{
			error.clear();
			const bool created = std::filesystem::create_directory(directory, error);
			if (error)
			{
				rollbackCreatedDirectories();
				TC_Core_Error("Could not create project directory '{0}': {1}",
					PathToUTF8(directory), error.message());
				return nullptr;
			}
			if (created)
				createdDirectories.push_back(directory);
			else
			{
				error.clear();
				if (!std::filesystem::is_directory(directory, error) || error)
				{
					rollbackCreatedDirectories();
					TC_Core_Error("Project directory became unavailable while it was being created: {0}",
						PathToUTF8(directory));
					return nullptr;
				}
			}
		}
		if (!project->Save())
		{
			rollbackCreatedDirectories();
			TC_Core_Error("Could not save new project '{0}'", PathToUTF8(normalizedProjectPath));
			return nullptr;
		}

		project->UpdateLastOperationTime();
		return project;
	}

	Ref<Project> Project::Load(const std::filesystem::path& projectPath)
	{
		std::error_code error;
		if (!std::filesystem::is_regular_file(projectPath, error) || error)
			return nullptr;

		try
		{
			std::ifstream input(projectPath, std::ios::binary);
			if (!input)
				throw std::runtime_error("Could not open the project file");
			YAML::Node data = YAML::Load(input);
			if (input.bad())
				throw std::runtime_error("Failed while reading the project file");
			if (!data.IsMap())
				throw std::runtime_error("Project document must be a map");
			const uint32_t schemaVersion = data["SchemaVersion"] ? data["SchemaVersion"].as<uint32_t>() : 1U;
			if (schemaVersion == 0 || schemaVersion > CurrentSchemaVersion)
				throw std::runtime_error("Unsupported project SchemaVersion " + std::to_string(schemaVersion));

			YAML::Node projectNode = data["Project"];
			if (!projectNode || !projectNode.IsMap())
				throw std::runtime_error("Project document is missing the 'Project' map");
			if (!projectNode["Name"])
				throw std::runtime_error("Project.Name is required");

			auto project = CreateRef<Project>(projectPath);
			ProjectConfig config;
			config.Name = projectNode["Name"].as<std::string>();
			config.Version = ReadOptional<std::string>(projectNode, "Version", "1.0.0");
			config.Description = ReadOptional<std::string>(projectNode, "Description", "");
			config.EditorVersion = ReadOptional<std::string>(projectNode, "EditorVersion", "");
			config.Template = ReadOptional<std::string>(projectNode, "Template", "3D");
			config.AssetDirectory = UTF8ToPath(ReadOptional<std::string>(projectNode, "AssetDirectory", "Assets"));
			config.StartScene = UTF8ToPath(ReadOptional<std::string>(projectNode, "StartScene", "sample.tomcat"));
			config.TwoColumnCurrentFolder = ReadOptional<std::string>(projectNode, "TwoColumnCurrentFolder", "");
			config.LastOperationTime = ReadOptional<std::string>(projectNode, "LastOperationTime", "");
			if (projectNode["ExpandedNodes"])
			{
				if (!projectNode["ExpandedNodes"].IsSequence())
					throw std::runtime_error("Project.ExpandedNodes must be a sequence");
				for (const YAML::Node& node : projectNode["ExpandedNodes"])
					config.ExpandedNodes.push_back(node.as<std::string>());
			}

			std::string validationError;
			if (!NormalizeAndValidateConfig(config, project->m_Directory, validationError))
				throw std::runtime_error(validationError);
			project->m_Config = std::move(config);
			project->m_PreservedDocument = ReadWholeFile(projectPath);
			return project;
		}
		catch (const std::exception& exception)
		{
			TC_Core_Error("Failed to load project '{0}': {1}", PathToUTF8(projectPath), exception.what());
			return nullptr;
		}
	}

	bool Project::Save()
	{
		try
		{
			if (m_ProjectPath.empty())
				throw std::runtime_error("Project path is empty");
			std::string validationError;
			if (!NormalizeAndValidateConfig(m_Config, m_Directory, validationError))
				throw std::runtime_error(validationError);

			std::error_code error;
			const bool destinationExists = std::filesystem::exists(m_ProjectPath, error);
			if (error)
				throw std::runtime_error("Could not inspect the destination project file");

			YAML::Node root;
			if (destinationExists)
			{
				std::ifstream input(m_ProjectPath, std::ios::binary);
				if (!input)
					throw std::runtime_error("Could not open the existing project document");
				root = YAML::Load(input);
				if (input.bad())
					throw std::runtime_error("Failed while reading the existing project document");
				if (!root.IsMap() || !root["Project"] || !root["Project"].IsMap())
					throw std::runtime_error("Refusing to overwrite an invalid existing project document");
				const uint32_t schemaVersion = root["SchemaVersion"]
					? root["SchemaVersion"].as<uint32_t>() : 1U;
				if (schemaVersion == 0 || schemaVersion > CurrentSchemaVersion)
					throw std::runtime_error("Refusing to overwrite unsupported project SchemaVersion "
						+ std::to_string(schemaVersion));
			}
			else if (!m_PreservedDocument.empty())
			{
				root = YAML::Load(m_PreservedDocument);
				if (!root.IsMap())
					throw std::runtime_error("Preserved project document is invalid");
			}
			else
			{
				root = YAML::Node(YAML::NodeType::Map);
				root["Project"] = YAML::Node(YAML::NodeType::Map);
			}

			root["SchemaVersion"] = CurrentSchemaVersion;
			YAML::Node projectNode = root["Project"];
			projectNode["Name"] = m_Config.Name;
			projectNode["Version"] = m_Config.Version;
			projectNode["Description"] = m_Config.Description;
			projectNode["EditorVersion"] = m_Config.EditorVersion;
			projectNode["Template"] = m_Config.Template;
			projectNode["AssetDirectory"] = PathToUTF8(m_Config.AssetDirectory);
			projectNode["StartScene"] = PathToUTF8(m_Config.StartScene);
			projectNode["TwoColumnCurrentFolder"] = m_Config.TwoColumnCurrentFolder;
			YAML::Node expandedNodes(YAML::NodeType::Sequence);
			for (const std::string& node : m_Config.ExpandedNodes)
				expandedNodes.push_back(node);
			projectNode["ExpandedNodes"] = expandedNodes;
			projectNode.remove("LastOperationTime");

			YAML::Emitter out;
			out << root;
			if (!out.good())
				throw std::runtime_error(out.GetLastError());
			std::string writeError;
			if (!FileSystem::WriteFileAtomically(m_ProjectPath, out.c_str(), writeError))
				throw std::runtime_error("Could not atomically replace the project file: " + writeError);

			m_PreservedDocument = out.c_str();
			return true;
		}
		catch (const std::exception& exception)
		{
			TC_Core_Error("Failed to save project '{0}': {1}", PathToUTF8(m_ProjectPath), exception.what());
			return false;
		}
	}

	bool Project::Reload()
	{
		const std::string localLastOperationTime = m_Config.LastOperationTime;
		Ref<Project> reloaded = Load(m_ProjectPath);
		if (!reloaded)
			return false;
		m_Directory = std::move(reloaded->m_Directory);
		m_Config = std::move(reloaded->m_Config);
		if (!localLastOperationTime.empty())
			m_Config.LastOperationTime = localLastOperationTime;
		m_PreservedDocument = std::move(reloaded->m_PreservedDocument);
		return true;
	}

}
