#include "tcpch.h"
#include "Project.h"
#include "TomCat/Asset/ContentHash.h"
#include "TomCat/Utils/FileSystemUtils.h"
#include "TomCat/Utils/PathUtils.h"
#include "TomCat/Runtime/RuntimeCompatibility.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <cctype>
#include <chrono>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <string_view>
#include <system_error>
#include <unordered_set>

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

		bool UsesReservedProjectRoot(const std::filesystem::path& path)
		{
			const std::filesystem::path normalized = path.lexically_normal();
			if (normalized.empty())
				return false;
			std::string first = PathToUTF8(*normalized.begin());
			std::transform(first.begin(), first.end(), first.begin(),
				[](unsigned char value) { return static_cast<char>(std::tolower(value)); });
			return first == "library" || first == "cache" || first == "usersettings" ||
				first == "projectsettings" ||
				first == ".git" || first == ".svn" || first == ".hg" ||
				first == ".bzr" || first == ".jj";
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
			const std::filesystem::path& assetRoot)
		{
			if (storedPath.empty())
				return {};

			const std::filesystem::path value = UTF8ToPath(storedPath);
			if (value.lexically_normal() == ".")
				return AbsoluteNormalized(assetRoot);
			if (!IsSafeRelativePath(value))
				return {};
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

		std::string NormalizeBrowserStatePath(const std::string& storedPath,
			const std::filesystem::path& assetRoot)
		{
			std::string normalized = storedPath;
			std::replace(normalized.begin(), normalized.end(), '\\', '/');
			for (const std::string_view root : { std::string_view("@assets"), std::string_view("@packages") })
			{
				if (normalized == root)
					return std::string(root);
				const std::string prefix = std::string(root) + "/";
				if (normalized.rfind(prefix, 0) != 0)
					continue;

				const std::filesystem::path relative = UTF8ToPath(normalized.substr(prefix.size()));
				if (!IsSafeRelativePath(relative))
					return {};
				std::string relativeText = PathToUTF8(relative.lexically_normal());
				std::replace(relativeText.begin(), relativeText.end(), '\\', '/');
				return prefix + relativeText;
			}

			// Schema v1 originally stored Assets-relative paths without a root token.
			const std::filesystem::path resolved = ResolveBrowserPath(storedPath, assetRoot);
			return StoreAssetRelativePath(resolved, assetRoot);
		}

		bool BuildAssetSystemIgnoreRulesUpdate(
			const std::filesystem::path& projectDirectory, std::string& contents,
			bool& changed, std::string& errorMessage)
		{
			const std::filesystem::path ignorePath = projectDirectory / ".gitignore";
			contents.clear();
			changed = false;
			errorMessage.clear();
			std::error_code error;
			const std::filesystem::file_status status =
				std::filesystem::symlink_status(ignorePath, error);
			if (error && error != std::errc::no_such_file_or_directory)
			{
				errorMessage = "Could not inspect .gitignore: " + error.message();
				return false;
			}
			if (!error && std::filesystem::exists(status))
			{
				if (!std::filesystem::is_regular_file(status) || std::filesystem::is_symlink(status))
				{
					errorMessage = ".gitignore is not a regular, non-symlink file";
					return false;
				}
				std::ifstream input(ignorePath, std::ios::binary);
				if (!input)
				{
					errorMessage = "Could not open .gitignore";
					return false;
				}
				std::ostringstream buffer;
				buffer << input.rdbuf();
				if (input.bad())
				{
					errorMessage = "Failed while reading .gitignore";
					return false;
				}
				contents = buffer.str();
			}

			constexpr std::array<std::string_view, 5> required = {
				"/Library/", "/Cache/", "/UserSettings/",
				"/ProjectSettings/MigrationBackups/",
				"/ProjectSettings/.migration-journal.json"
			};
			std::array<bool, required.size()> present{};
			std::istringstream lines(contents);
			std::string line;
			while (std::getline(lines, line))
			{
				if (!line.empty() && line.back() == '\r')
					line.pop_back();
				for (size_t index = 0; index < required.size(); ++index)
				{
					const std::string_view rule = required[index];
					const std::string_view withoutLeadingSlash = rule.substr(1);
					if (line == rule || line == withoutLeadingSlash)
						present[index] = true;
				}
			}

			for (size_t index = 0; index < required.size(); ++index)
			{
				if (present[index])
					continue;
				if (!contents.empty() && contents.back() != '\n')
					contents.push_back('\n');
				contents.append(required[index]);
				contents.push_back('\n');
				changed = true;
			}
			return true;
		}

		bool EnsureAssetSystemIgnoreRules(const std::filesystem::path& projectDirectory)
		{
			std::string contents;
			std::string preparationError;
			bool changed = false;
			if (!BuildAssetSystemIgnoreRulesUpdate(
				projectDirectory, contents, changed, preparationError))
			{
				TC_Core_Warn("Could not prepare project ignore file '{0}': {1}",
					PathToUTF8(projectDirectory / ".gitignore"), preparationError);
				return false;
			}
			if (!changed)
				return true;

			const std::filesystem::path ignorePath = projectDirectory / ".gitignore";
			std::string writeError;
			if (!FileSystem::WriteFileAtomically(ignorePath, contents, writeError))
			{
				TC_Core_Warn("Could not update project ignore file '{0}': {1}",
					PathToUTF8(ignorePath), writeError);
				return false;
			}
			return true;
		}

		bool NormalizeAndValidateConfig(ProjectConfig& config, std::string& errorMessage)
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
			if (UsesReservedProjectRoot(config.AssetDirectory))
			{
				errorMessage = "Project.AssetDirectory cannot overlap Library, Cache, UserSettings, ProjectSettings, or version-control metadata";
				return false;
			}

			return true;
		}

		void NormalizeEditorState(EditorProjectState& state,
			const std::filesystem::path& assetRoot)
		{
			state.ContentBrowserCurrentDirectory = NormalizeBrowserStatePath(
				state.ContentBrowserCurrentDirectory, assetRoot);
			if (state.ContentBrowserCurrentDirectory.empty())
				state.ContentBrowserCurrentDirectory = ".";

			std::vector<std::string> validExpandedNodes;
			validExpandedNodes.reserve(state.ContentBrowserExpandedNodes.size());
			for (const std::string& node : state.ContentBrowserExpandedNodes)
			{
				const std::string normalized = NormalizeBrowserStatePath(node, assetRoot);
				if (!normalized.empty())
					validExpandedNodes.push_back(normalized);
			}
			std::sort(validExpandedNodes.begin(), validExpandedNodes.end());
			validExpandedNodes.erase(std::unique(validExpandedNodes.begin(), validExpandedNodes.end()),
				validExpandedNodes.end());
			state.ContentBrowserExpandedNodes = std::move(validExpandedNodes);
		}

		bool NormalizeExternalScriptEditor(std::filesystem::path& editor,
			std::string& errorMessage)
		{
			if (editor.empty())
				return true;
			if (!editor.is_absolute() || !editor.has_filename())
			{
				errorMessage = "externalScriptEditor must be an absolute executable path";
				return false;
			}

			std::string extension = PathToUTF8(editor.extension());
			std::transform(extension.begin(), extension.end(), extension.begin(),
				[](unsigned char value) { return static_cast<char>(std::tolower(value)); });
			if (extension != ".exe")
			{
				errorMessage = "externalScriptEditor must name a Windows .exe file";
				return false;
			}
			// Preserve the path representation selected by the user. Resolving an
			// existing parent with weakly_canonical can expand a Windows 8.3 path
			// (for example RUNNER~1) even when the executable itself does not exist,
			// which makes a saved setting change after a round trip.
			editor = editor.lexically_normal();
			return true;
		}

		std::string EscapeJsonString(std::string_view value)
		{
			std::ostringstream escaped;
			escaped << std::hex << std::uppercase;
			for (const unsigned char character : value)
			{
				switch (character)
				{
					case '"': escaped << "\\\""; break;
					case '\\': escaped << "\\\\"; break;
					case '\b': escaped << "\\b"; break;
					case '\f': escaped << "\\f"; break;
					case '\n': escaped << "\\n"; break;
					case '\r': escaped << "\\r"; break;
					case '\t': escaped << "\\t"; break;
					default:
						if (character < 0x20)
						{
							escaped << "\\u00" << std::setw(2) << std::setfill('0')
								<< static_cast<unsigned int>(character);
						}
						else
							escaped << static_cast<char>(character);
						break;
				}
			}
			return escaped.str();
		}

		class JsonSyntaxValidator
		{
		public:
			explicit JsonSyntaxValidator(std::string_view input)
				: m_Input(input)
			{
			}

			bool Validate()
			{
				SkipWhitespace();
				if (!ParseValue(0))
					return false;
				SkipWhitespace();
				return m_Position == m_Input.size();
			}

		private:
			void SkipWhitespace()
			{
				while (m_Position < m_Input.size())
				{
					const char character = m_Input[m_Position];
					if (character != ' ' && character != '\t' && character != '\r' && character != '\n')
						break;
					++m_Position;
				}
			}

			bool Consume(char expected)
			{
				if (m_Position >= m_Input.size() || m_Input[m_Position] != expected)
					return false;
				++m_Position;
				return true;
			}

			bool ParseValue(uint32_t depth)
			{
				if (depth > 128U || m_Position >= m_Input.size())
					return false;
				switch (m_Input[m_Position])
				{
					case '{': return ParseObject(depth + 1U);
					case '[': return ParseArray(depth + 1U);
					case '"': return ParseString();
					case 't': return ParseLiteral("true");
					case 'f': return ParseLiteral("false");
					case 'n': return ParseLiteral("null");
					default: return ParseNumber();
				}
			}

			bool ParseObject(uint32_t depth)
			{
				if (!Consume('{'))
					return false;
				SkipWhitespace();
				if (Consume('}'))
					return true;
				while (true)
				{
					if (!ParseString())
						return false;
					SkipWhitespace();
					if (!Consume(':'))
						return false;
					SkipWhitespace();
					if (!ParseValue(depth))
						return false;
					SkipWhitespace();
					if (Consume('}'))
						return true;
					if (!Consume(','))
						return false;
					SkipWhitespace();
				}
			}

			bool ParseArray(uint32_t depth)
			{
				if (!Consume('['))
					return false;
				SkipWhitespace();
				if (Consume(']'))
					return true;
				while (true)
				{
					if (!ParseValue(depth))
						return false;
					SkipWhitespace();
					if (Consume(']'))
						return true;
					if (!Consume(','))
						return false;
					SkipWhitespace();
				}
			}

			bool ParseString()
			{
				if (!Consume('"'))
					return false;
				while (m_Position < m_Input.size())
				{
					const unsigned char character = static_cast<unsigned char>(m_Input[m_Position++]);
					if (character == '"')
						return true;
					if (character < 0x20)
						return false;
					if (character != '\\')
						continue;
					if (m_Position >= m_Input.size())
						return false;
					const char escape = m_Input[m_Position++];
					if (escape == '"' || escape == '\\' || escape == '/' || escape == 'b' ||
						escape == 'f' || escape == 'n' || escape == 'r' || escape == 't')
						continue;
					if (escape != 'u' || m_Position + 4 > m_Input.size())
						return false;
					for (size_t index = 0; index < 4; ++index)
					{
						const char digit = m_Input[m_Position++];
						if (!((digit >= '0' && digit <= '9') || (digit >= 'a' && digit <= 'f') ||
							(digit >= 'A' && digit <= 'F')))
							return false;
					}
				}
				return false;
			}

			bool ParseLiteral(std::string_view literal)
			{
				if (m_Input.substr(m_Position, literal.size()) != literal)
					return false;
				m_Position += literal.size();
				return true;
			}

			bool ParseNumber()
			{
				const size_t start = m_Position;
				Consume('-');
				if (m_Position >= m_Input.size())
					return false;
				if (m_Input[m_Position] == '0')
					++m_Position;
				else
				{
					if (m_Input[m_Position] < '1' || m_Input[m_Position] > '9')
						return false;
					while (m_Position < m_Input.size() && m_Input[m_Position] >= '0' &&
						m_Input[m_Position] <= '9')
						++m_Position;
				}
				if (m_Position < m_Input.size() && m_Input[m_Position] == '.')
				{
					++m_Position;
					const size_t fractionStart = m_Position;
					while (m_Position < m_Input.size() && m_Input[m_Position] >= '0' &&
						m_Input[m_Position] <= '9')
						++m_Position;
					if (m_Position == fractionStart)
						return false;
				}
				if (m_Position < m_Input.size() &&
					(m_Input[m_Position] == 'e' || m_Input[m_Position] == 'E'))
				{
					++m_Position;
					if (m_Position < m_Input.size() &&
						(m_Input[m_Position] == '+' || m_Input[m_Position] == '-'))
						++m_Position;
					const size_t exponentStart = m_Position;
					while (m_Position < m_Input.size() && m_Input[m_Position] >= '0' &&
						m_Input[m_Position] <= '9')
						++m_Position;
					if (m_Position == exponentStart)
						return false;
				}
				return m_Position > start;
			}

			std::string_view m_Input;
			size_t m_Position = 0;
		};

		std::string ReadWholeFile(const std::filesystem::path& path)
		{
			std::ifstream input(path, std::ios::binary);
			if (!input)
				return {};
			std::ostringstream stream;
			stream << input.rdbuf();
			return input.bad() ? std::string{} : stream.str();
		}

		template<size_t FieldCount>
		void RequireExactMapFields(const YAML::Node& node, const std::string& context,
			const std::array<const char*, FieldCount>& fields)
		{
			if (!node || !node.IsMap())
				throw std::runtime_error(context + " must be a map");

			std::unordered_set<std::string> seenFields;
			for (const auto& entry : node)
			{
				if (!entry.first.IsScalar())
					throw std::runtime_error(context + " contains a non-scalar field name");
				const std::string field = entry.first.as<std::string>();
				if (!seenFields.emplace(field).second)
					throw std::runtime_error(context + " contains duplicate field '" + field + "'");

				bool known = false;
				for (const char* expected : fields)
				{
					if (field == expected)
					{
						known = true;
						break;
					}
				}
				if (!known)
					throw std::runtime_error(context + " contains unknown field '" + field + "'");
			}

			for (const char* field : fields)
			{
				if (!node[field])
					throw std::runtime_error(context + " is missing required field '" + field + "'");
			}
		}

		YAML::Node RequireSupportedProjectDocument(const YAML::Node& root,
			uint32_t& schemaVersion)
		{
			constexpr std::array<const char*, 2> rootFields = { "SchemaVersion", "Project" };
			RequireExactMapFields(root, "Project document", rootFields);

			const YAML::Node schemaNode = root["SchemaVersion"];
			schemaVersion = schemaNode.as<uint32_t>();
			if (schemaVersion != Project::OldestSupportedSchemaVersion
				&& schemaVersion != Project::CurrentSchemaVersion)
				throw std::runtime_error("Unsupported project SchemaVersion " +
					std::to_string(schemaVersion) + "; expected " +
					std::to_string(Project::OldestSupportedSchemaVersion) + " or " +
					std::to_string(Project::CurrentSchemaVersion));

			const YAML::Node projectNode = root["Project"];
			constexpr std::array<const char*, 6> currentProjectFields = {
				"Name", "Version", "Description", "EditorVersion", "Template",
				"AssetDirectory"
			};
			constexpr std::array<const char*, 8> legacyProjectFields = {
				"Name", "Version", "Description", "EditorVersion", "Template",
				"AssetDirectory", "StartScene", "StartSceneHandle"
			};
			if (schemaVersion == Project::CurrentSchemaVersion)
				RequireExactMapFields(projectNode, "Project", currentProjectFields);
			else
				RequireExactMapFields(projectNode, "Project", legacyProjectFields);

			return projectNode;
		}

		ProjectConfig ReadProjectConfig(const YAML::Node& projectNode,
			uint32_t schemaVersion)
		{
			ProjectConfig config;
			config.Name = projectNode["Name"].as<std::string>();
			config.Version = projectNode["Version"].as<std::string>();
			config.Description = projectNode["Description"].as<std::string>();
			config.EditorVersion = projectNode["EditorVersion"].as<std::string>();
			config.Template = projectNode["Template"].as<std::string>();
			config.AssetDirectory = UTF8ToPath(projectNode["AssetDirectory"].as<std::string>());
			config.StartScene.clear();
			config.StartSceneHandle = AssetHandle(0);
			if (schemaVersion == Project::OldestSupportedSchemaVersion)
			{
				config.StartScene = UTF8ToPath(projectNode["StartScene"].as<std::string>());
				if (!IsSafeRelativePath(config.StartScene))
					throw std::runtime_error(
						"Project.StartScene must be a relative path inside AssetDirectory");
				config.StartScene = config.StartScene.lexically_normal();
				config.StartSceneHandle = AssetHandle(
					projectNode["StartSceneHandle"].as<uint64_t>());
			}

			std::string validationError;
			if (!NormalizeAndValidateConfig(config, validationError))
				throw std::runtime_error(validationError);
			return config;
		}

		constexpr uint32_t kBuildSettingsSchemaVersion = 1;

		bool NormalizeAndValidateBuildSettings(BuildSettings& settings,
			std::string& errorMessage)
		{
			if (settings.Scenes.size() > RuntimeCompatibility::MaximumBuildSceneCount)
			{
				errorMessage = "BuildSettings.Scenes contains too many entries";
				return false;
			}

			std::unordered_set<AssetHandle> sceneHandles;
			bool entrySceneIsEnabled = false;
			for (std::size_t index = 0; index < settings.Scenes.size(); ++index)
			{
				BuildSceneSettings& scene = settings.Scenes[index];
				if (static_cast<uint64_t>(scene.Handle) == 0)
				{
					errorMessage = "BuildSettings.Scenes[" + std::to_string(index) +
						"].Handle cannot be 0";
					return false;
				}
				if (!sceneHandles.emplace(scene.Handle).second)
				{
					errorMessage = "BuildSettings.Scenes contains duplicate handle " +
						std::to_string(static_cast<uint64_t>(scene.Handle));
					return false;
				}
				if (!scene.PathHint.empty())
				{
					if (!IsSafeRelativePath(scene.PathHint))
					{
						errorMessage = "BuildSettings.Scenes[" + std::to_string(index) +
							"].PathHint must be a relative path inside AssetDirectory";
						return false;
					}
					scene.PathHint = scene.PathHint.lexically_normal();
				}
				if (scene.Handle == settings.EntrySceneHandle && scene.Enabled)
					entrySceneIsEnabled = true;
			}

			if (static_cast<uint64_t>(settings.EntrySceneHandle) != 0 && !entrySceneIsEnabled)
			{
				errorMessage = "BuildSettings.EntrySceneHandle must identify an enabled scene in BuildSettings.Scenes";
				return false;
			}
			return true;
		}

		enum class BuildSettingsLoadResult
		{
			Missing,
			Loaded,
			Failed
		};

		BuildSettingsLoadResult LoadBuildSettingsFile(
			const std::filesystem::path& path, BuildSettings& settings,
			std::string& errorMessage)
		{
			std::error_code filesystemError;
			const bool exists = std::filesystem::exists(path, filesystemError);
			if (filesystemError)
			{
				errorMessage = "Could not inspect build settings: " + filesystemError.message();
				return BuildSettingsLoadResult::Failed;
			}
			if (!exists)
			{
				settings = BuildSettings{};
				return BuildSettingsLoadResult::Missing;
			}
			if (!std::filesystem::is_regular_file(path, filesystemError) || filesystemError)
			{
				errorMessage = "Build settings path is not a regular file";
				return BuildSettingsLoadResult::Failed;
			}

			try
			{
				std::ifstream input(path, std::ios::binary);
				if (!input)
					throw std::runtime_error("Could not open build settings");
				std::ostringstream contents;
				contents << input.rdbuf();
				if (input.bad())
					throw std::runtime_error("Failed while reading build settings");
				const std::string document = contents.str();
				if (!JsonSyntaxValidator(document).Validate())
					throw std::runtime_error("Build settings file is not valid JSON");

				const YAML::Node root = YAML::Load(document);
				RequireExactMapFields(root, "Build settings document",
					std::array<const char*, 3>{ "schemaVersion", "entrySceneHandle", "scenes" });
				const uint32_t schemaVersion = root["schemaVersion"].as<uint32_t>();
				if (schemaVersion != kBuildSettingsSchemaVersion)
					throw std::runtime_error("Unsupported build settings schemaVersion " +
						std::to_string(schemaVersion) + "; expected " +
						std::to_string(kBuildSettingsSchemaVersion));

				const YAML::Node scenes = root["scenes"];
				if (!scenes.IsSequence())
					throw std::runtime_error("BuildSettings.Scenes must be a sequence");
				if (scenes.size() > RuntimeCompatibility::MaximumBuildSceneCount)
					throw std::runtime_error("BuildSettings.Scenes contains too many entries");

				BuildSettings loaded;
				loaded.EntrySceneHandle = AssetHandle(root["entrySceneHandle"].as<uint64_t>());
				loaded.Scenes.reserve(scenes.size());
				for (std::size_t index = 0; index < scenes.size(); ++index)
				{
					const YAML::Node scene = scenes[index];
					RequireExactMapFields(scene,
						"BuildSettings.Scenes[" + std::to_string(index) + "]",
						std::array<const char*, 3>{ "handle", "enabled", "pathHint" });
					BuildSceneSettings entry;
					entry.Handle = AssetHandle(scene["handle"].as<uint64_t>());
					entry.Enabled = scene["enabled"].as<bool>();
					entry.PathHint = UTF8ToPath(scene["pathHint"].as<std::string>());
					loaded.Scenes.push_back(std::move(entry));
				}
				if (!NormalizeAndValidateBuildSettings(loaded, errorMessage))
					return BuildSettingsLoadResult::Failed;
				settings = std::move(loaded);
				return BuildSettingsLoadResult::Loaded;
			}
			catch (const std::exception& exception)
			{
				errorMessage = exception.what();
				return BuildSettingsLoadResult::Failed;
			}
		}

		constexpr uint32_t kPlayerSettingsSchemaVersion = 1;

		enum class PlayerSettingsLoadResult
		{
			Missing,
			Loaded,
			Failed
		};

		PlayerSettingsLoadResult LoadPlayerSettingsFile(
			const std::filesystem::path& path, PlayerSettings& settings,
			std::string& errorMessage)
		{
			std::error_code filesystemError;
			const bool exists = std::filesystem::exists(path, filesystemError);
			if (filesystemError)
			{
				errorMessage = "Could not inspect Player settings: " + filesystemError.message();
				return PlayerSettingsLoadResult::Failed;
			}
			if (!exists)
				return PlayerSettingsLoadResult::Missing;
			if (!std::filesystem::is_regular_file(path, filesystemError) || filesystemError)
			{
				errorMessage = "Player settings path is not a regular file";
				return PlayerSettingsLoadResult::Failed;
			}

			try
			{
				const std::string document = ReadWholeFile(path);
				if (document.empty() || !JsonSyntaxValidator(document).Validate())
					throw std::runtime_error("Player settings file is not valid JSON");
				const YAML::Node root = YAML::Load(document);
				RequireExactMapFields(root, "Player settings document",
					std::array<const char*, 7>{ "schemaVersion", "productName",
						"companyName", "version", "icon", "display", "directories" });
				const uint32_t schemaVersion = root["schemaVersion"].as<uint32_t>();
				if (schemaVersion != kPlayerSettingsSchemaVersion)
					throw std::runtime_error("Unsupported Player settings schemaVersion "
						+ std::to_string(schemaVersion) + "; expected "
						+ std::to_string(kPlayerSettingsSchemaVersion));

				const YAML::Node display = root["display"];
				RequireExactMapFields(display, "PlayerSettings.Display",
					std::array<const char*, 5>{ "width", "height", "windowMode",
						"resizable", "vSync" });
				const YAML::Node directories = root["directories"];
				RequireExactMapFields(directories, "PlayerSettings.Directories",
					std::array<const char*, 3>{ "save", "log", "crash" });

				PlayerSettings loaded;
				loaded.ProductName = root["productName"].as<std::string>();
				loaded.CompanyName = root["companyName"].as<std::string>();
				loaded.Version = root["version"].as<std::string>();
				loaded.Icon = AssetHandle(root["icon"].as<uint64_t>());
				loaded.Width = display["width"].as<uint32_t>();
				loaded.Height = display["height"].as<uint32_t>();
				if (!PlayerWindowModeFromString(
					display["windowMode"].as<std::string>(), loaded.WindowMode))
					throw std::runtime_error("PlayerSettings.Display.WindowMode is invalid");
				loaded.Resizable = display["resizable"].as<bool>();
				loaded.VSync = display["vSync"].as<bool>();
				loaded.SaveDirectory = UTF8ToPath(directories["save"].as<std::string>());
				loaded.LogDirectory = UTF8ToPath(directories["log"].as<std::string>());
				loaded.CrashDirectory = UTF8ToPath(directories["crash"].as<std::string>());
				if (!NormalizeAndValidatePlayerSettings(loaded, errorMessage))
					return PlayerSettingsLoadResult::Failed;
				settings = std::move(loaded);
				return PlayerSettingsLoadResult::Loaded;
			}
			catch (const std::exception& exception)
			{
				errorMessage = exception.what();
				return PlayerSettingsLoadResult::Failed;
			}
		}

		constexpr uint32_t kLegacyProjectSettingsSchemaVersion = 1;
		constexpr uint32_t kProjectSettingsSchemaVersion = 2;

		bool ValidateProjectSettings(const ProjectSettings& settings,
			std::string& errorMessage)
		{
			if (settings.TagsAndLayers.Tags.empty()
				|| settings.TagsAndLayers.Tags.front() != "Untagged")
			{
				errorMessage = "TagsAndLayers.Tags must begin with the reserved 'Untagged' tag";
				return false;
			}

			std::unordered_set<std::string> tags;
			for (const std::string& tag : settings.TagsAndLayers.Tags)
			{
				if (tag.empty())
				{
					errorMessage = "TagsAndLayers.Tags cannot contain an empty tag";
					return false;
				}
				if (!tags.emplace(tag).second)
				{
					errorMessage = "TagsAndLayers.Tags contains duplicate tag '" + tag + "'";
					return false;
				}
			}

			if (settings.TagsAndLayers.LayerNames[0] != "Default")
			{
				errorMessage = "TagsAndLayers.LayerNames[0] must be the reserved 'Default' layer";
				return false;
			}
			std::unordered_set<std::string> layerNames;
			for (const std::string& layerName : settings.TagsAndLayers.LayerNames)
			{
				if (layerName.empty())
					continue;
				if (!layerNames.emplace(layerName).second)
				{
					errorMessage = "TagsAndLayers.LayerNames contains duplicate layer '" +
						layerName + "'";
					return false;
				}
			}

			for (std::size_t layerA = 0; layerA < Physics2DLayerCount; ++layerA)
			{
				for (std::size_t layerB = layerA; layerB < Physics2DLayerCount; ++layerB)
				{
					const bool aAcceptsB = (settings.Physics2D.CollisionMasks[layerA]
						& (uint16_t(1) << layerB)) != 0;
					const bool bAcceptsA = (settings.Physics2D.CollisionMasks[layerB]
						& (uint16_t(1) << layerA)) != 0;
					if (aAcceptsB != bAcceptsA)
					{
						errorMessage = "Physics2D.CollisionMasks must be symmetric";
						return false;
					}
				}
			}
			return true;
		}

		enum class ProjectSettingsLoadResult
		{
			Missing,
			Loaded,
			Failed
		};

		enum class ProjectSettingsDocumentFormat
		{
			Json,
			LegacyYaml
		};

		ProjectSettingsLoadResult LoadProjectSettingsFile(
			const std::filesystem::path& path, ProjectSettings& settings,
			std::string& errorMessage, ProjectSettingsDocumentFormat format)
		{
			std::error_code filesystemError;
			const bool exists = std::filesystem::exists(path, filesystemError);
			if (filesystemError)
			{
				errorMessage = "Could not inspect project settings: " + filesystemError.message();
				return ProjectSettingsLoadResult::Failed;
			}
			if (!exists)
			{
				settings = ProjectSettings{};
				return ProjectSettingsLoadResult::Missing;
			}
			if (!std::filesystem::is_regular_file(path, filesystemError) || filesystemError)
			{
				errorMessage = "Project settings path is not a regular file";
				return ProjectSettingsLoadResult::Failed;
			}

			try
			{
				std::ifstream input(path, std::ios::binary);
				if (!input)
					throw std::runtime_error("Could not open project settings");
				std::ostringstream contents;
				contents << input.rdbuf();
				if (input.bad())
					throw std::runtime_error("Failed while reading project settings");
				const std::string document = contents.str();
				if (format == ProjectSettingsDocumentFormat::Json
					&& !JsonSyntaxValidator(document).Validate())
				{
					throw std::runtime_error("Project settings file is not valid JSON");
				}
				const YAML::Node root = YAML::Load(document);
				const bool json = format == ProjectSettingsDocumentFormat::Json;
				const char* schemaField = json ? "schemaVersion" : "SchemaVersion";
				const char* tagsAndLayersField = json ? "tagsAndLayers" : "TagsAndLayers";
				const char* tagsField = json ? "tags" : "Tags";
				const char* layerNamesField = json ? "layerNames" : "LayerNames";
				const char* physicsField = json ? "physics2D" : "Physics2D";
				const char* masksField = json ? "collisionMasks" : "CollisionMasks";
				RequireExactMapFields(root, "Project settings document",
					std::array<const char*, 3>{ schemaField, tagsAndLayersField, physicsField });
				const uint32_t schemaVersion = root[schemaField].as<uint32_t>();
				if (schemaVersion != kLegacyProjectSettingsSchemaVersion
					&& schemaVersion != kProjectSettingsSchemaVersion)
					throw std::runtime_error("Unsupported project settings SchemaVersion " +
						std::to_string(schemaVersion) + "; expected " +
						std::to_string(kLegacyProjectSettingsSchemaVersion) + " or " +
						std::to_string(kProjectSettingsSchemaVersion));

				const YAML::Node tagsAndLayers = root[tagsAndLayersField];
				RequireExactMapFields(tagsAndLayers, "TagsAndLayers",
					std::array<const char*, 2>{ tagsField, layerNamesField });
				const YAML::Node tags = tagsAndLayers[tagsField];
				if (!tags.IsSequence())
					throw std::runtime_error("TagsAndLayers.Tags must be a sequence");
				const YAML::Node layerNames = tagsAndLayers[layerNamesField];
				if (!layerNames.IsSequence() || layerNames.size() != Physics2DLayerCount)
					throw std::runtime_error("TagsAndLayers.LayerNames must contain exactly 16 entries");

				const YAML::Node physics = root[physicsField];
				RequireExactMapFields(physics, "Physics2D",
					std::array<const char*, 1>{ masksField });
				const YAML::Node masks = physics[masksField];
				if (!masks.IsSequence() || masks.size() != Physics2DLayerCount)
					throw std::runtime_error("Physics2D.CollisionMasks must contain exactly 16 entries");

				ProjectSettings loaded;
				loaded.TagsAndLayers.Tags.clear();
				for (const YAML::Node& tag : tags)
					loaded.TagsAndLayers.Tags.push_back(tag.as<std::string>());
				for (std::size_t index = 0; index < Physics2DLayerCount; ++index)
				{
					loaded.TagsAndLayers.LayerNames[index] = layerNames[index].as<std::string>();
					loaded.Physics2D.CollisionMasks[index] = masks[index].as<uint16_t>();
				}
				if (!ValidateProjectSettings(loaded, errorMessage))
					return ProjectSettingsLoadResult::Failed;
				settings = std::move(loaded);
				return ProjectSettingsLoadResult::Loaded;
			}
			catch (const std::exception& exception)
			{
				errorMessage = exception.what();
				return ProjectSettingsLoadResult::Failed;
			}
		}

		constexpr uint32_t kProjectMigrationJournalSchemaVersion = 2;
		constexpr std::string_view kProjectMigrationJournalName =
			".migration-journal.json";
		constexpr std::string_view kProjectMigrationBackupDirectory =
			"MigrationBackups";

		struct ProjectMigrationJournalEntry
		{
			std::filesystem::path RelativeTarget;
			bool Existed = false;
			uintmax_t OriginalSize = 0;
			std::string OriginalSHA256;
			std::filesystem::path BackupFile;
		};

		struct ProjectMigrationJournal
		{
			std::string TransactionID;
			std::string State = "prepared";
			uint32_t SourceSchemaVersion = 0;
			uint32_t TargetSchemaVersion = 0;
			bool SettingsDirectoryExisted = false;
			bool BackupRootExisted = false;
			std::vector<ProjectMigrationJournalEntry> Entries;
		};

		struct ProjectMigrationDirectoryGuards
		{
			// Root remains pinned through the entire transaction. Active is
			// progressively extended through ProjectSettings, MigrationBackups,
			// and the transaction directory as those directories are created.
			FileSystem::PinnedDirectoryChain Root;
			FileSystem::PinnedDirectoryChain Active;
		};

		std::filesystem::path MigrationJournalPath(
			const std::filesystem::path& projectPath)
		{
			return projectPath.parent_path() / "ProjectSettings" /
				UTF8ToPath(std::string(kProjectMigrationJournalName));
		}

		std::filesystem::path MigrationBackupRoot(
			const std::filesystem::path& projectPath)
		{
			return projectPath.parent_path() / "ProjectSettings" /
				UTF8ToPath(std::string(kProjectMigrationBackupDirectory));
		}

		bool PinMigrationProjectDirectory(const std::filesystem::path& projectPath,
			FileSystem::PinnedDirectoryChain& guard, std::string& errorMessage)
		{
			std::filesystem::path projectDirectory = projectPath.parent_path();
			std::error_code error;
			if (projectDirectory.empty())
				projectDirectory = std::filesystem::current_path(error);
			if (error)
			{
				errorMessage = "Could not resolve the project directory: " +
					error.message();
				return false;
			}
			if (!guard.Acquire(projectDirectory, errorMessage))
			{
				errorMessage = "Could not pin project directory for migration: " +
					errorMessage;
				return false;
			}
			return true;
		}

		std::string MigrationSHA256(std::string_view bytes)
		{
			return ComputeContentSHA256(std::span<const uint8_t>(
				reinterpret_cast<const uint8_t*>(bytes.data()), bytes.size()));
		}

		bool IsMigrationSHA256(std::string_view value)
		{
			return value.size() == 64 &&
				std::all_of(value.begin(), value.end(), [](unsigned char character)
				{
					return (character >= '0' && character <= '9') ||
						(character >= 'a' && character <= 'f');
				});
		}

		bool MigrationPathComponentEqual(const std::filesystem::path& left,
			const std::filesystem::path& right)
		{
#ifdef TC_PLATFORM_WINDOWS
			std::string leftText = PathToUTF8(left);
			std::string rightText = PathToUTF8(right);
			std::transform(leftText.begin(), leftText.end(), leftText.begin(),
				[](unsigned char value) { return static_cast<char>(std::tolower(value)); });
			std::transform(rightText.begin(), rightText.end(), rightText.begin(),
				[](unsigned char value) { return static_cast<char>(std::tolower(value)); });
			return leftText == rightText;
#else
			return left == right;
#endif
		}

		bool IsMigrationPathWithin(const std::filesystem::path& root,
			const std::filesystem::path& candidate)
		{
			const std::filesystem::path normalizedRoot = root.lexically_normal();
			const std::filesystem::path normalizedCandidate =
				candidate.lexically_normal();
			auto rootPart = normalizedRoot.begin();
			const auto rootEnd = normalizedRoot.end();
			auto candidatePart = normalizedCandidate.begin();
			const auto candidateEnd = normalizedCandidate.end();
			for (; rootPart != rootEnd; ++rootPart, ++candidatePart)
			{
				if (candidatePart == candidateEnd ||
					!MigrationPathComponentEqual(*rootPart, *candidatePart))
					return false;
			}
			return true;
		}

		bool IsMissingMigrationPathError(const std::error_code& error)
		{
			return error == std::errc::no_such_file_or_directory ||
				error == std::errc::not_a_directory;
		}

#ifdef TC_PLATFORM_WINDOWS
		std::wstring ExtendedMigrationPath(const std::filesystem::path& path)
		{
			std::wstring value = path.wstring();
			if (value.rfind(L"\\\\?\\", 0) == 0)
				return value;
			if (value.rfind(L"\\\\", 0) == 0)
				return L"\\\\?\\UNC\\" + value.substr(2);
			return L"\\\\?\\" + value;
		}
#endif

		bool ValidateNoReparsePathChain(const std::filesystem::path& path,
			bool allowMissingSuffix, std::string& errorMessage)
		{
			std::error_code error;
			const std::filesystem::path absolute =
				std::filesystem::absolute(path, error).lexically_normal();
			if (error || absolute.empty())
			{
				errorMessage = "Could not make migration path absolute: " +
					PathToUTF8(path);
				return false;
			}

			auto inspect = [&](const std::filesystem::path& current,
				bool isLeaf) -> std::optional<bool>
			{
				error.clear();
				const std::filesystem::file_status status =
					std::filesystem::symlink_status(current, error);
				if (IsMissingMigrationPathError(error) ||
					(!error && !std::filesystem::exists(status)))
				{
					if (!allowMissingSuffix)
					{
						errorMessage = "Migration path does not exist: " +
							PathToUTF8(current);
						return false;
					}
					return std::nullopt;
				}
				if (error)
				{
					errorMessage = "Could not inspect migration path ancestor '" +
						PathToUTF8(current) + "': " + error.message();
					return false;
				}
				if (std::filesystem::is_symlink(status))
				{
					errorMessage = "Migration path contains a symlink or reparse point: " +
						PathToUTF8(current);
					return false;
				}
#ifdef TC_PLATFORM_WINDOWS
				const DWORD attributes =
					GetFileAttributesW(ExtendedMigrationPath(current).c_str());
				if (attributes == INVALID_FILE_ATTRIBUTES)
				{
					errorMessage = "Could not inspect migration path attributes: " +
						PathToUTF8(current);
					return false;
				}
				if ((attributes & FILE_ATTRIBUTE_REPARSE_POINT) != 0)
				{
					errorMessage = "Migration path contains a symlink or reparse point: " +
						PathToUTF8(current);
					return false;
				}
#endif
				if (!isLeaf && !std::filesystem::is_directory(status))
				{
					errorMessage = "Migration path ancestor is not a directory: " +
						PathToUTF8(current);
					return false;
				}
				return true;
			};

			std::filesystem::path current = absolute.root_path();
			if (!current.empty())
			{
				const std::optional<bool> inspected = inspect(
					current, current == absolute);
				if (!inspected.has_value())
					return allowMissingSuffix;
				if (!*inspected)
					return false;
			}
			for (const auto& component : absolute.relative_path())
			{
				current /= component;
				const std::optional<bool> inspected = inspect(
					current, current == absolute);
				if (!inspected.has_value())
					return allowMissingSuffix;
				if (!*inspected)
					return false;
			}
			return true;
		}

		bool ValidateMigrationPath(const std::filesystem::path& projectPath,
			const std::filesystem::path& candidate, bool allowMissingSuffix,
			std::string& errorMessage)
		{
			errorMessage.clear();
			std::error_code error;
			std::filesystem::path projectDirectory = projectPath.parent_path();
			if (projectDirectory.empty())
				projectDirectory = std::filesystem::current_path(error);
			if (error)
			{
				errorMessage = "Could not resolve the project directory";
				return false;
			}
			const std::filesystem::path absoluteRoot =
				std::filesystem::absolute(projectDirectory, error).lexically_normal();
			if (error)
			{
				errorMessage = "Could not make the project directory absolute";
				return false;
			}
			const std::filesystem::path absoluteCandidate =
				std::filesystem::absolute(candidate, error).lexically_normal();
			if (error || !IsMigrationPathWithin(absoluteRoot, absoluteCandidate))
			{
				errorMessage = "Migration path escaped the project directory: " +
					PathToUTF8(candidate);
				return false;
			}
			if (!ValidateNoReparsePathChain(
				absoluteRoot, false, errorMessage) ||
				!ValidateNoReparsePathChain(
					absoluteCandidate, allowMissingSuffix, errorMessage))
				return false;

			const std::filesystem::path canonicalRoot =
				std::filesystem::canonical(absoluteRoot, error);
			if (error)
			{
				errorMessage = "Could not canonicalize the project directory: " +
					error.message();
				return false;
			}
			const std::filesystem::path canonicalCandidate =
				std::filesystem::weakly_canonical(absoluteCandidate, error);
			if (error || !IsMigrationPathWithin(canonicalRoot, canonicalCandidate))
			{
				errorMessage = "Canonical migration path escaped the project directory: " +
					PathToUTF8(candidate);
				return false;
			}
			return true;
		}

		bool RequireNoActiveMigrationJournal(
			const std::filesystem::path& projectPath, std::string& errorMessage)
		{
			const std::filesystem::path journalPath = MigrationJournalPath(projectPath);
			if (!ValidateMigrationPath(
				projectPath, projectPath, false, errorMessage) ||
				!ValidateMigrationPath(
					projectPath, journalPath, true, errorMessage))
				return false;

			std::error_code filesystemError;
			const std::filesystem::file_status journalStatus =
				std::filesystem::symlink_status(journalPath, filesystemError);
			if (filesystemError == std::errc::no_such_file_or_directory ||
				(!filesystemError && !std::filesystem::exists(journalStatus)))
				return true;
			if (filesystemError)
			{
				errorMessage = "Could not inspect the migration journal: " +
					filesystemError.message();
				return false;
			}
			errorMessage =
				"An interrupted migration must be recovered explicitly before loading";
			return false;
		}

		bool RemoveMigrationPathSafely(const std::filesystem::path& projectPath,
			const std::filesystem::path& path, bool requireExisting,
			std::string& errorMessage)
		{
			if (!ValidateMigrationPath(projectPath, path, true, errorMessage))
				return false;
			bool removed = false;
			if (!FileSystem::RemovePathSafely(path, removed, errorMessage))
				return false;
			if (requireExisting && !removed)
			{
				errorMessage = "Migration path disappeared before safe deletion: " +
					PathToUTF8(path);
				return false;
			}
			return true;
		}

		bool ReadFileBytes(const std::filesystem::path& path, std::string& contents,
			std::string& errorMessage)
		{
			contents.clear();
			std::error_code error;
			const std::filesystem::file_status status =
				std::filesystem::symlink_status(path, error);
			if (error || !std::filesystem::is_regular_file(status) ||
				std::filesystem::is_symlink(status))
			{
				errorMessage = "Path is not a regular, non-symlink file: " +
					PathToUTF8(path);
				return false;
			}
			std::ifstream input(path, std::ios::binary);
			if (!input)
			{
				errorMessage = "Could not open file: " + PathToUTF8(path);
				return false;
			}
			std::ostringstream buffer;
			buffer << input.rdbuf();
			if (input.bad())
			{
				errorMessage = "Failed while reading file: " + PathToUTF8(path);
				return false;
			}
			contents = buffer.str();
			return true;
		}

		bool ReadMigrationFileBytes(const std::filesystem::path& projectPath,
			const std::filesystem::path& path, std::string& contents,
			std::string& errorMessage)
		{
			if (!ValidateMigrationPath(projectPath, path, false, errorMessage) ||
				!ReadFileBytes(path, contents, errorMessage))
				return false;
			return ValidateMigrationPath(projectPath, path, false, errorMessage);
		}

		bool InspectMigrationTarget(const std::filesystem::path& projectPath,
			const std::filesystem::path& path, bool& existed, uintmax_t& size,
			std::string& sha256, std::string& errorMessage)
		{
			existed = false;
			size = 0;
			sha256.clear();
			if (!ValidateMigrationPath(projectPath, path, true, errorMessage))
				return false;
			std::error_code error;
			const std::filesystem::file_status status =
				std::filesystem::symlink_status(path, error);
			if (error == std::errc::no_such_file_or_directory)
				return true;
			if (error)
			{
				errorMessage = "Could not inspect migration target '" +
					PathToUTF8(path) + "': " + error.message();
				return false;
			}
			if (!std::filesystem::exists(status))
				return true;
			if (!std::filesystem::is_regular_file(status) ||
				std::filesystem::is_symlink(status))
			{
				errorMessage = "Migration target is not a regular, non-symlink file: " +
					PathToUTF8(path);
				return false;
			}
			std::string contents;
			if (!ReadMigrationFileBytes(
				projectPath, path, contents, errorMessage))
				return false;
			size = contents.size();
			sha256 = MigrationSHA256(contents);
			existed = true;
			return true;
		}

		bool AddMigrationChange(ProjectMigrationPreview& preview,
			const std::filesystem::path& projectPath,
			const std::filesystem::path& targetPath, std::string reason,
			std::string& errorMessage)
		{
			const std::filesystem::path projectDirectory =
				projectPath.parent_path().lexically_normal();
			const std::filesystem::path relative = projectDirectory.empty() ?
				targetPath.lexically_normal() :
				targetPath.lexically_normal().lexically_relative(projectDirectory);
			if (!IsSafeRelativePath(relative))
			{
				errorMessage = "Migration target escaped the project directory";
				return false;
			}
			for (const ProjectMigrationChange& existing : preview.Changes)
			{
				if (existing.RelativePath == relative)
					return true;
			}

			bool existed = false;
			uintmax_t size = 0;
			std::string sha256;
			if (!InspectMigrationTarget(
				projectPath, targetPath, existed, size, sha256, errorMessage))
				return false;
			ProjectMigrationChange change;
			change.RelativePath = relative;
			change.Kind = existed ? ProjectMigrationChangeKind::Replace :
				ProjectMigrationChangeKind::Create;
			change.OriginalSize = size;
			change.OriginalSHA256 = std::move(sha256);
			change.Reason = std::move(reason);
			preview.Changes.push_back(std::move(change));
			return true;
		}

		bool BuildProjectMigrationPreview(
			const std::filesystem::path& projectPath, uint32_t schemaVersion,
			bool buildSettingsMissing, bool playerSettingsMissing,
			ProjectMigrationPreview& preview, std::string& errorMessage)
		{
			preview = {};
			preview.ProjectPath = AbsoluteNormalized(projectPath);
			preview.SourceSchemaVersion = schemaVersion;
			preview.TargetSchemaVersion = Project::CurrentSchemaVersion;
			preview.BackupRoot = std::filesystem::path("ProjectSettings") /
				UTF8ToPath(std::string(kProjectMigrationBackupDirectory));

			const std::filesystem::path projectDirectory = projectPath.parent_path();
			if (schemaVersion != Project::CurrentSchemaVersion)
			{
				if (!AddMigrationChange(preview, projectPath, projectPath,
					"upgrade Project.tcproj schema", errorMessage) ||
					!AddMigrationChange(preview, projectPath,
						projectDirectory / "ProjectSettings" / "BuildSettings.json",
						buildSettingsMissing ? "create BuildSettings from the legacy start scene" :
						"validate and rewrite BuildSettings with the project schema",
						errorMessage) ||
					!AddMigrationChange(preview, projectPath,
						projectDirectory / "ProjectSettings" / "PlayerSettings.json",
						playerSettingsMissing ? "create default PlayerSettings" :
						"validate and rewrite PlayerSettings with the project schema",
						errorMessage))
					return false;
			}
			else if (playerSettingsMissing)
			{
				if (!AddMigrationChange(preview, projectPath,
					projectDirectory / "ProjectSettings" / "PlayerSettings.json",
					"create required default PlayerSettings", errorMessage))
					return false;
			}

			if (!preview.RequiresMigration())
				return true;

			std::string ignoreContents;
			bool ignoreChanged = false;
			if (!BuildAssetSystemIgnoreRulesUpdate(projectDirectory, ignoreContents,
				ignoreChanged, errorMessage))
				return false;
			if (ignoreChanged && !AddMigrationChange(preview, projectPath,
				projectDirectory / ".gitignore",
				"add derived-data and user-state ignore rules", errorMessage))
				return false;
			return true;
		}

		bool MigrationPreviewsMatch(const ProjectMigrationPreview& approved,
			const ProjectMigrationPreview& current)
		{
			if (approved.ProjectPath.lexically_normal() !=
					current.ProjectPath.lexically_normal() ||
				approved.SourceSchemaVersion != current.SourceSchemaVersion ||
				approved.TargetSchemaVersion != current.TargetSchemaVersion ||
				approved.BackupRoot.lexically_normal() !=
					current.BackupRoot.lexically_normal() ||
				approved.Changes.size() != current.Changes.size())
				return false;
			for (size_t index = 0; index < approved.Changes.size(); ++index)
			{
				const ProjectMigrationChange& expected = approved.Changes[index];
				const ProjectMigrationChange& actual = current.Changes[index];
				if (expected.RelativePath.lexically_normal() !=
						actual.RelativePath.lexically_normal() ||
					expected.Kind != actual.Kind ||
					expected.OriginalSize != actual.OriginalSize ||
					expected.OriginalSHA256 != actual.OriginalSHA256 ||
					expected.Reason != actual.Reason)
					return false;
			}
			return true;
		}

		bool IsSafeTransactionID(std::string_view value)
		{
			if (value.empty() || value.size() > 96)
				return false;
			return std::all_of(value.begin(), value.end(), [](unsigned char character)
			{
				return std::isalnum(character) || character == '-' || character == '_';
			});
		}

		bool IsAllowedMigrationTarget(const std::filesystem::path& projectPath,
			const std::filesystem::path& relative)
		{
			const std::filesystem::path normalized = relative.lexically_normal();
			return normalized == projectPath.filename() ||
				normalized == std::filesystem::path("ProjectSettings") / "BuildSettings.json" ||
				normalized == std::filesystem::path("ProjectSettings") / "PlayerSettings.json" ||
				normalized == ".gitignore";
		}

		std::string SerializeMigrationJournal(
			const ProjectMigrationJournal& journal)
		{
			std::ostringstream output;
			output << "{\n"
				<< "  \"schemaVersion\": " << kProjectMigrationJournalSchemaVersion << ",\n"
				<< "  \"transactionId\": \"" << EscapeJsonString(journal.TransactionID) << "\",\n"
				<< "  \"state\": \"" << EscapeJsonString(journal.State) << "\",\n"
				<< "  \"sourceSchemaVersion\": " << journal.SourceSchemaVersion << ",\n"
				<< "  \"targetSchemaVersion\": " << journal.TargetSchemaVersion << ",\n"
				<< "  \"settingsDirectoryExisted\": "
				<< (journal.SettingsDirectoryExisted ? "true" : "false") << ",\n"
				<< "  \"backupRootExisted\": "
				<< (journal.BackupRootExisted ? "true" : "false") << ",\n"
				<< "  \"entries\": [";
			for (size_t index = 0; index < journal.Entries.size(); ++index)
			{
				const ProjectMigrationJournalEntry& entry = journal.Entries[index];
				if (index != 0)
					output << ',';
				output << "\n    {\n"
					<< "      \"target\": \""
					<< EscapeJsonString(entry.RelativeTarget.generic_string())
					<< "\",\n"
					<< "      \"existed\": " << (entry.Existed ? "true" : "false") << ",\n"
					<< "      \"originalSize\": " << entry.OriginalSize << ",\n"
					<< "      \"originalSha256\": \""
					<< EscapeJsonString(entry.OriginalSHA256) << "\",\n"
					<< "      \"backup\": \""
					<< EscapeJsonString(entry.BackupFile.generic_string())
					<< "\"\n"
					<< "    }";
			}
			if (!journal.Entries.empty())
				output << '\n';
			output << "  ]\n}\n";
			return output.str();
		}

		bool ParseMigrationJournal(const std::filesystem::path& projectPath,
			std::string_view document, ProjectMigrationJournal& journal,
			std::string& errorMessage)
		{
			try
			{
				if (!JsonSyntaxValidator(document).Validate())
					throw std::runtime_error("migration journal is not valid JSON");
				const YAML::Node root = YAML::Load(std::string(document));
				RequireExactMapFields(root, "Migration journal",
					std::array<const char*, 8>{ "schemaVersion", "transactionId",
						"state", "sourceSchemaVersion", "targetSchemaVersion",
						"settingsDirectoryExisted", "backupRootExisted", "entries" });
				if (root["schemaVersion"].as<uint32_t>() !=
					kProjectMigrationJournalSchemaVersion)
					throw std::runtime_error("unsupported migration journal schema");

				ProjectMigrationJournal parsed;
				parsed.TransactionID = root["transactionId"].as<std::string>();
				parsed.State = root["state"].as<std::string>();
				parsed.SourceSchemaVersion = root["sourceSchemaVersion"].as<uint32_t>();
				parsed.TargetSchemaVersion = root["targetSchemaVersion"].as<uint32_t>();
				parsed.SettingsDirectoryExisted =
					root["settingsDirectoryExisted"].as<bool>();
				parsed.BackupRootExisted = root["backupRootExisted"].as<bool>();
				if (!IsSafeTransactionID(parsed.TransactionID))
					throw std::runtime_error("invalid migration transaction id");
				if (parsed.State != "prepared" && parsed.State != "committed")
					throw std::runtime_error("invalid migration journal state");
				if (parsed.TargetSchemaVersion != Project::CurrentSchemaVersion)
					throw std::runtime_error("migration journal targets another project schema");

				const YAML::Node entries = root["entries"];
				if (!entries.IsSequence() || entries.size() == 0 || entries.size() > 4)
					throw std::runtime_error("migration journal entries are invalid");
				std::unordered_set<std::string> targets;
				for (size_t index = 0; index < entries.size(); ++index)
				{
					const YAML::Node node = entries[index];
					RequireExactMapFields(node, "Migration journal entry",
						std::array<const char*, 5>{ "target", "existed",
							"originalSize", "originalSha256", "backup" });
					ProjectMigrationJournalEntry entry;
					entry.RelativeTarget = UTF8ToPath(node["target"].as<std::string>()).
						lexically_normal();
					entry.Existed = node["existed"].as<bool>();
					entry.OriginalSize = node["originalSize"].as<uintmax_t>();
					entry.OriginalSHA256 =
						node["originalSha256"].as<std::string>();
					entry.BackupFile = UTF8ToPath(node["backup"].as<std::string>()).
						lexically_normal();
					if (!IsSafeRelativePath(entry.RelativeTarget) ||
						!IsAllowedMigrationTarget(projectPath, entry.RelativeTarget))
						throw std::runtime_error("migration journal target is outside the allowed file set");
					if (!targets.emplace(entry.RelativeTarget.generic_string()).second)
						throw std::runtime_error("migration journal contains duplicate targets");
					if (!entry.BackupFile.has_filename() ||
						entry.BackupFile.parent_path() != std::filesystem::path{} ||
						entry.BackupFile.extension() != ".bin")
						throw std::runtime_error("migration journal backup name is invalid");
					if (entry.Existed && entry.BackupFile.empty())
						throw std::runtime_error("migration journal is missing an original backup");
					if (entry.Existed && !IsMigrationSHA256(entry.OriginalSHA256))
						throw std::runtime_error("migration journal original SHA-256 is invalid");
					if (!entry.Existed &&
						(entry.OriginalSize != 0 || !entry.OriginalSHA256.empty()))
						throw std::runtime_error(
							"migration journal created-file integrity fields are invalid");
					parsed.Entries.push_back(std::move(entry));
				}
				journal = std::move(parsed);
				return true;
			}
			catch (const std::exception& exception)
			{
				errorMessage = exception.what();
				return false;
			}
		}

		bool WriteMigrationJournalFile(const std::filesystem::path& projectPath,
			const std::filesystem::path& path,
			const ProjectMigrationJournal& journal, std::string& errorMessage)
		{
			if (!ValidateMigrationPath(projectPath, path, true, errorMessage))
				return false;
			const std::string serialized = SerializeMigrationJournal(journal);
			if (!FileSystem::WriteFileAtomically(path, serialized, errorMessage) ||
				!ValidateMigrationPath(projectPath, path, false, errorMessage))
				return false;
			std::string persisted;
			if (!ReadMigrationFileBytes(
				projectPath, path, persisted, errorMessage))
				return false;
			if (persisted != serialized)
			{
				errorMessage = "Migration journal changed while it was persisted: " +
					PathToUTF8(path);
				return false;
			}
			return true;
		}

		void RemoveEmptyMigrationDirectories(const std::filesystem::path& projectPath,
			const ProjectMigrationJournal& journal)
		{
			const std::filesystem::path backupRoot = MigrationBackupRoot(projectPath);
			std::string validationError;
			if (!journal.BackupRootExisted)
				RemoveMigrationPathSafely(
					projectPath, backupRoot, false, validationError);
			const std::filesystem::path settingsDirectory =
				projectPath.parent_path() / "ProjectSettings";
			if (!journal.SettingsDirectoryExisted)
				RemoveMigrationPathSafely(
					projectPath, settingsDirectory, false, validationError);
		}

		void CleanupPreparedMigrationArtifacts(
			const std::filesystem::path& projectPath,
			const ProjectMigrationJournal& journal,
			const std::filesystem::path& backupDirectory,
			bool removeActiveJournal)
		{
			std::string validationError;
			if (removeActiveJournal)
			{
				const std::filesystem::path activeJournal =
					MigrationJournalPath(projectPath);
				RemoveMigrationPathSafely(
					projectPath, activeJournal, false, validationError);
			}
			for (const ProjectMigrationJournalEntry& entry : journal.Entries)
			{
				if (!entry.Existed)
					continue;
				const std::filesystem::path backup =
					backupDirectory / entry.BackupFile;
				RemoveMigrationPathSafely(
					projectPath, backup, false, validationError);
			}
			const std::filesystem::path archivedJournal =
				backupDirectory / "journal.json";
			RemoveMigrationPathSafely(
				projectPath, archivedJournal, false, validationError);
			RemoveMigrationPathSafely(
				projectPath, backupDirectory, false, validationError);
			RemoveEmptyMigrationDirectories(projectPath, journal);
		}

		bool RestoreMigrationJournal(const std::filesystem::path& projectPath,
			const ProjectMigrationJournal& journal, std::string& errorMessage,
			FileSystem::PinnedDirectoryChain* activeDirectoryGuard = nullptr,
			const ProjectMigrationRecoveryPreview* approvedState = nullptr,
			std::string_view approvedJournal = {})
		{
			const std::filesystem::path journalPath = MigrationJournalPath(projectPath);
			const std::filesystem::path backupDirectory =
				MigrationBackupRoot(projectPath) / UTF8ToPath(journal.TransactionID);
			auto requireApprovedJournal = [&]()
			{
				if (approvedJournal.empty())
					return true;
				std::string currentJournal;
				if (!ReadMigrationFileBytes(
					projectPath, journalPath, currentJournal, errorMessage))
					return false;
				if (currentJournal != approvedJournal)
				{
					errorMessage =
						"Migration journal changed after recovery approval";
					return false;
				}
				return true;
			};
			if (!requireApprovedJournal())
				return false;

			if (journal.State == "committed")
			{
				if (!ValidateMigrationPath(
					projectPath, journalPath, false, errorMessage))
					return false;
				if (!RemoveMigrationPathSafely(
					projectPath, journalPath, true, errorMessage))
				{
					errorMessage = "Could not clear committed migration journal: " +
						errorMessage;
					return false;
				}
				return true;
			}

			if (!ValidateMigrationPath(
				projectPath, journalPath, false, errorMessage) ||
				!ValidateMigrationPath(
					projectPath, backupDirectory, false, errorMessage))
				return false;

			// Verify every original backup before touching any target. Keeping the
			// validated bytes in memory also prevents a later backup-file change
			// from influencing the restore.
			std::vector<std::string> originals(journal.Entries.size());
			for (size_t index = 0; index < journal.Entries.size(); ++index)
			{
				const ProjectMigrationJournalEntry& entry = journal.Entries[index];
				const std::filesystem::path target =
					projectPath.parent_path() / entry.RelativeTarget;
				if (!ValidateMigrationPath(
					projectPath, target, true, errorMessage))
					return false;
				const std::filesystem::path backup =
					backupDirectory / entry.BackupFile;
				if (!entry.Existed)
				{
					if (!ValidateMigrationPath(
						projectPath, backup, true, errorMessage))
						return false;
					std::error_code backupError;
					const std::filesystem::file_status backupStatus =
						std::filesystem::symlink_status(backup, backupError);
					if ((!backupError && std::filesystem::exists(backupStatus)) ||
						(backupError && !IsMissingMigrationPathError(backupError)))
					{
						errorMessage =
							"Migration journal has an unexpected backup for a created file";
						return false;
					}
					continue;
				}
				if (!ReadMigrationFileBytes(
					projectPath, backup, originals[index], errorMessage))
					return false;
				if (originals[index].size() != entry.OriginalSize ||
					MigrationSHA256(originals[index]) != entry.OriginalSHA256)
				{
					errorMessage = "Migration backup SHA-256 or size does not match "
						"its journal: " + PathToUTF8(backup);
					return false;
				}
			}

			for (size_t reverseIndex = journal.Entries.size();
				reverseIndex > 0; --reverseIndex)
			{
				const size_t index = reverseIndex - 1;
				const ProjectMigrationJournalEntry& entry = journal.Entries[index];
				const std::filesystem::path target =
					projectPath.parent_path() / entry.RelativeTarget;
				bool currentExists = false;
				uintmax_t currentSize = 0;
				std::string currentSHA256;
				if (!InspectMigrationTarget(projectPath, target, currentExists,
					currentSize, currentSHA256, errorMessage))
					return false;
				if (approvedState)
				{
					if (index >= approvedState->Changes.size())
					{
						errorMessage =
							"Approved recovery target set is incomplete";
						return false;
					}
					const ProjectMigrationRecoveryChange& approved =
						approvedState->Changes[index];
					if (approved.RelativePath.lexically_normal() !=
							entry.RelativeTarget.lexically_normal() ||
						approved.CurrentExists != currentExists ||
						approved.CurrentSize != currentSize ||
						approved.CurrentSHA256 != currentSHA256)
					{
						errorMessage =
							"Migration target changed after recovery approval: " +
							PathToUTF8(target);
						return false;
					}
				}
				if (entry.Existed)
				{
					if (!currentExists || currentSize != entry.OriginalSize ||
						currentSHA256 != entry.OriginalSHA256)
					{
						std::string writeError;
						if (!ValidateMigrationPath(
							projectPath, target, true, writeError) ||
							!FileSystem::WriteFileAtomically(
								target, originals[index], writeError))
						{
							errorMessage = "Could not restore '" + PathToUTF8(target) +
								"': " + writeError;
							return false;
						}
					}
				}
				else
				{
					if (currentExists)
					{
						if (!RemoveMigrationPathSafely(
							projectPath, target, true, errorMessage))
						{
							errorMessage = "Could not remove created migration target: " +
								errorMessage;
							return false;
						}
					}
				}
			}

			// The journal is the recovery authority. Remove it only after every
			// target has been restored and verified.
			for (const ProjectMigrationJournalEntry& entry : journal.Entries)
			{
				const std::filesystem::path target =
					projectPath.parent_path() / entry.RelativeTarget;
				if (entry.Existed)
				{
					std::string restored;
					if (!ReadMigrationFileBytes(
						projectPath, target, restored, errorMessage) ||
						restored.size() != entry.OriginalSize ||
						MigrationSHA256(restored) != entry.OriginalSHA256)
					{
						errorMessage = "Migration rollback verification failed for '" +
							PathToUTF8(target) + "'";
						return false;
					}
				}
				else
				{
					if (!ValidateMigrationPath(
						projectPath, target, true, errorMessage))
						return false;
					std::error_code existsError;
					const std::filesystem::file_status status =
						std::filesystem::symlink_status(target, existsError);
					if ((!existsError && std::filesystem::exists(status)) ||
						(existsError && !IsMissingMigrationPathError(existsError)))
					{
						errorMessage = "Migration rollback left a newly-created file";
						return false;
					}
				}
			}

			if (!requireApprovedJournal())
				return false;
			if (!RemoveMigrationPathSafely(
				projectPath, journalPath, true, errorMessage))
			{
				errorMessage = "Could not remove completed rollback journal: " +
					errorMessage;
				return false;
			}

			// Cleanup is deliberately file-by-file and empty-directory-only. A
			// concurrent writer can therefore prevent cleanup without losing data.
			for (const ProjectMigrationJournalEntry& entry : journal.Entries)
			{
				if (!entry.Existed)
					continue;
				std::string cleanupError;
				RemoveMigrationPathSafely(projectPath,
					backupDirectory / entry.BackupFile, false, cleanupError);
			}
			std::string cleanupError;
			RemoveMigrationPathSafely(projectPath,
				backupDirectory / "journal.json", false, cleanupError);
			// Deleting the transaction directory itself requires releasing its
			// no-FILE_SHARE_DELETE handle. Root remains pinned by the caller, and
			// each cleanup mutation pins its own complete parent chain.
			if (activeDirectoryGuard)
				activeDirectoryGuard->Reset();
			RemoveMigrationPathSafely(
				projectPath, backupDirectory, false, cleanupError);
			RemoveEmptyMigrationDirectories(projectPath, journal);
			return true;
		}

		bool PrepareMigrationJournal(const std::filesystem::path& projectPath,
			const ProjectMigrationPreview& preview, ProjectMigrationJournal& journal,
			ProjectMigrationDirectoryGuards& guards, std::string& errorMessage)
		{
			static std::atomic<uint64_t> transactionCounter{ 0 };
			journal = {};
			journal.SourceSchemaVersion = preview.SourceSchemaVersion;
			journal.TargetSchemaVersion = preview.TargetSchemaVersion;
			journal.TransactionID = std::to_string(
				std::chrono::system_clock::now().time_since_epoch().count()) + "-" +
				std::to_string(transactionCounter.fetch_add(1,
					std::memory_order_relaxed));

			const std::filesystem::path settingsDirectory =
				projectPath.parent_path() / "ProjectSettings";
			const std::filesystem::path backupRoot = MigrationBackupRoot(projectPath);
			const std::filesystem::path backupDirectory =
				backupRoot / UTF8ToPath(journal.TransactionID);
			if (!ValidateMigrationPath(projectPath, projectPath, false, errorMessage) ||
				!ValidateMigrationPath(
					projectPath, settingsDirectory, true, errorMessage) ||
				!ValidateMigrationPath(
					projectPath, backupRoot, true, errorMessage) ||
				!ValidateMigrationPath(
					projectPath, backupDirectory, true, errorMessage))
				return false;

			FileSystem::PinnedDirectoryChain nextGuard;
			bool settingsCreated = false;
			if (!FileSystem::CreateDirectoryAndPin(settingsDirectory, nextGuard,
				settingsCreated, errorMessage))
			{
				errorMessage = "Could not create or pin ProjectSettings for migration: " +
					errorMessage;
				return false;
			}
			journal.SettingsDirectoryExisted = !settingsCreated;
			guards.Active = std::move(nextGuard);
			if (!ValidateMigrationPath(
				projectPath, settingsDirectory, false, errorMessage))
			{
				guards.Active.Reset();
				RemoveEmptyMigrationDirectories(projectPath, journal);
				return false;
			}

			bool backupRootCreated = false;
			if (!FileSystem::CreateDirectoryAndPin(backupRoot, nextGuard,
				backupRootCreated, errorMessage))
			{
				errorMessage = "Could not create or pin migration backup root: " +
					errorMessage;
				guards.Active.Reset();
				RemoveEmptyMigrationDirectories(projectPath, journal);
				return false;
			}
			journal.BackupRootExisted = !backupRootCreated;
			guards.Active = std::move(nextGuard);
			if (!ValidateMigrationPath(
				projectPath, backupRoot, false, errorMessage))
			{
				guards.Active.Reset();
				RemoveEmptyMigrationDirectories(projectPath, journal);
				return false;
			}

			bool backupDirectoryCreated = false;
			if (!FileSystem::CreateDirectoryAndPin(backupDirectory, nextGuard,
				backupDirectoryCreated, errorMessage) || !backupDirectoryCreated)
			{
				if (errorMessage.empty())
					errorMessage = "Migration transaction directory already exists";
				else
					errorMessage = "Could not create or pin migration backup: " +
						errorMessage;
				guards.Active.Reset();
				RemoveEmptyMigrationDirectories(projectPath, journal);
				return false;
			}
			guards.Active = std::move(nextGuard);
			if (!ValidateMigrationPath(
				projectPath, backupDirectory, false, errorMessage))
			{
				guards.Active.Reset();
				std::string cleanupError;
				RemoveMigrationPathSafely(
					projectPath, backupDirectory, false, cleanupError);
				RemoveEmptyMigrationDirectories(projectPath, journal);
				return false;
			}

			for (size_t index = 0; index < preview.Changes.size(); ++index)
			{
				const ProjectMigrationChange& change = preview.Changes[index];
				ProjectMigrationJournalEntry entry;
				entry.RelativeTarget = change.RelativePath;
				entry.Existed = change.Kind == ProjectMigrationChangeKind::Replace;
				entry.OriginalSize = change.OriginalSize;
				entry.OriginalSHA256 = change.OriginalSHA256;
				entry.BackupFile = "original-" + std::to_string(index) + ".bin";
				const std::filesystem::path target =
					projectPath.parent_path() / entry.RelativeTarget;
				if (!IsSafeRelativePath(entry.RelativeTarget) ||
					!IsAllowedMigrationTarget(projectPath, entry.RelativeTarget) ||
					!ValidateMigrationPath(
						projectPath, target, true, errorMessage))
					break;
				if (entry.Existed)
				{
					if (!IsMigrationSHA256(entry.OriginalSHA256))
					{
						errorMessage = "Migration preview has an invalid original SHA-256";
						break;
					}
					std::string original;
					if (!ReadMigrationFileBytes(
						projectPath, target, original, errorMessage) ||
						original.size() != entry.OriginalSize ||
						MigrationSHA256(original) != entry.OriginalSHA256)
					{
						errorMessage = "Migration target changed while its backup was prepared";
						break;
					}
					const std::filesystem::path backup =
						backupDirectory / entry.BackupFile;
					std::string writeError;
					if (!ValidateMigrationPath(
						projectPath, backup, true, writeError) ||
						!FileSystem::WriteFileAtomically(
							backup, original, writeError))
					{
						errorMessage = "Could not write migration backup: " + writeError;
						break;
					}
					std::string backedUp;
					std::string sourceAfterBackup;
					if (!ReadMigrationFileBytes(
						projectPath, backup, backedUp, errorMessage) ||
						backedUp.size() != entry.OriginalSize ||
						MigrationSHA256(backedUp) != entry.OriginalSHA256 ||
						!ReadMigrationFileBytes(
							projectPath, target, sourceAfterBackup, errorMessage) ||
						sourceAfterBackup.size() != entry.OriginalSize ||
						MigrationSHA256(sourceAfterBackup) != entry.OriginalSHA256)
					{
						errorMessage =
							"Migration backup or source failed post-backup SHA-256 verification";
						break;
					}
				}
				else
				{
					if (entry.OriginalSize != 0 || !entry.OriginalSHA256.empty())
					{
						errorMessage =
							"Migration preview has integrity data for a created file";
						break;
					}
					bool targetExists = false;
					uintmax_t targetSize = 0;
					std::string targetSHA256;
					if (!InspectMigrationTarget(projectPath, target, targetExists,
						targetSize, targetSHA256, errorMessage) || targetExists)
					{
						if (errorMessage.empty())
							errorMessage =
								"Migration create target appeared after the preview";
						break;
					}
				}
				journal.Entries.push_back(std::move(entry));
			}

			if (journal.Entries.size() != preview.Changes.size())
			{
				guards.Active.Reset();
				CleanupPreparedMigrationArtifacts(
					projectPath, journal, backupDirectory, false);
				return false;
			}

			std::string writeError;
			if (!WriteMigrationJournalFile(
				projectPath, backupDirectory / "journal.json", journal, writeError) ||
				!WriteMigrationJournalFile(
					projectPath, MigrationJournalPath(projectPath), journal, writeError))
			{
				errorMessage = "Could not persist migration journal: " + writeError;
				guards.Active.Reset();
				CleanupPreparedMigrationArtifacts(
					projectPath, journal, backupDirectory, true);
				return false;
			}
			return true;
		}

		bool ValidatePreparedMigrationSources(
			const std::filesystem::path& projectPath,
			const ProjectMigrationJournal& journal, std::string& errorMessage)
		{
			for (const ProjectMigrationJournalEntry& entry : journal.Entries)
			{
				const std::filesystem::path target =
					projectPath.parent_path() / entry.RelativeTarget;
				bool existed = false;
				uintmax_t size = 0;
				std::string sha256;
				if (!InspectMigrationTarget(
					projectPath, target, existed, size, sha256, errorMessage))
					return false;
				if (existed != entry.Existed ||
					(entry.Existed && (size != entry.OriginalSize ||
						sha256 != entry.OriginalSHA256)))
				{
					errorMessage =
						"Migration target changed after its backup was verified: " +
						PathToUTF8(target);
					return false;
				}
			}
			return true;
		}

		bool ExecuteProjectMigration(Project& project,
			const ProjectMigrationPreview& preview, std::string& errorMessage)
		{
			ProjectMigrationDirectoryGuards guards;
			if (!PinMigrationProjectDirectory(
				project.GetProjectPath(), guards.Root, errorMessage))
				return false;
			ProjectMigrationJournal journal;
			if (!PrepareMigrationJournal(
				project.GetProjectPath(), preview, journal, guards, errorMessage))
				return false;

			bool writesSucceeded = ValidatePreparedMigrationSources(
				project.GetProjectPath(), journal, errorMessage);
			if (preview.SourceSchemaVersion != preview.TargetSchemaVersion)
			{
				if (writesSucceeded)
					writesSucceeded = project.Save();
			}
			else if (writesSucceeded)
				writesSucceeded = project.SavePlayerSettings();

			const bool updatesIgnoreFile = std::any_of(
				preview.Changes.begin(), preview.Changes.end(),
				[](const ProjectMigrationChange& change)
				{
					return change.RelativePath == ".gitignore";
				});
			if (writesSucceeded && updatesIgnoreFile)
				writesSucceeded = EnsureAssetSystemIgnoreRules(project.GetProjectDirectory());

			if (writesSucceeded && !Project::Inspect(project.GetProjectPath()))
			{
				errorMessage = "Migrated project failed read-back validation";
				writesSucceeded = false;
			}

			if (!writesSucceeded)
			{
				if (errorMessage.empty())
					errorMessage = "One or more migration writes failed";
				std::string rollbackError;
				if (!RestoreMigrationJournal(
					project.GetProjectPath(), journal, rollbackError,
					&guards.Active))
					errorMessage += "; rollback remains journaled: " + rollbackError;
				return false;
			}

			journal.State = "committed";
			const std::filesystem::path backupDirectory =
				MigrationBackupRoot(project.GetProjectPath()) /
				UTF8ToPath(journal.TransactionID);
			std::string writeError;
			if (!WriteMigrationJournalFile(
				project.GetProjectPath(), backupDirectory / "journal.json",
				journal, writeError) ||
				!WriteMigrationJournalFile(
					project.GetProjectPath(),
					MigrationJournalPath(project.GetProjectPath()), journal, writeError))
			{
				errorMessage = "Could not commit migration journal: " + writeError;
				std::string rollbackError;
				journal.State = "prepared";
				if (!RestoreMigrationJournal(
					project.GetProjectPath(), journal, rollbackError,
					&guards.Active))
					errorMessage += "; rollback remains journaled: " + rollbackError;
				return false;
			}

			if (!ValidateMigrationPath(project.GetProjectPath(),
				MigrationJournalPath(project.GetProjectPath()), false, writeError))
			{
				errorMessage = "Could not validate committed migration journal: " +
					writeError;
				return false;
			}
			bool journalRemoved = false;
			if (!FileSystem::RemovePathSafely(
				MigrationJournalPath(project.GetProjectPath()), journalRemoved,
				writeError))
			{
				TC_Core_Warn("Committed project migration left a cleanup journal: {0}",
					writeError);
			}
			return true;
		}

		bool MigrationRecoveryPreviewsMatch(
			const ProjectMigrationRecoveryPreview& approved,
			const ProjectMigrationRecoveryPreview& current)
		{
			if (approved.ProjectPath.lexically_normal() !=
					current.ProjectPath.lexically_normal() ||
				approved.TransactionID != current.TransactionID ||
				approved.JournalState != current.JournalState ||
				approved.JournalSize != current.JournalSize ||
				approved.JournalSHA256 != current.JournalSHA256 ||
				approved.BackupDirectory.lexically_normal() !=
					current.BackupDirectory.lexically_normal() ||
				approved.Changes.size() != current.Changes.size())
				return false;
			for (size_t index = 0; index < approved.Changes.size(); ++index)
			{
				const ProjectMigrationRecoveryChange& expected =
					approved.Changes[index];
				const ProjectMigrationRecoveryChange& actual =
					current.Changes[index];
				if (expected.RelativePath.lexically_normal() !=
						actual.RelativePath.lexically_normal() ||
					expected.OriginalExisted != actual.OriginalExisted ||
					expected.OriginalSize != actual.OriginalSize ||
					expected.OriginalSHA256 != actual.OriginalSHA256 ||
					expected.CurrentExists != actual.CurrentExists ||
					expected.CurrentSize != actual.CurrentSize ||
					expected.CurrentSHA256 != actual.CurrentSHA256 ||
					expected.Action != actual.Action)
					return false;
			}
			return true;
		}

		bool InspectInterruptedMigrationJournal(
			const std::filesystem::path& projectPath,
			ProjectMigrationRecoveryPreview& preview,
			ProjectMigrationJournal& journal, std::string& journalDocument,
			ProjectMigrationDirectoryGuards& guards, std::string& errorMessage)
		{
			preview = {};
			preview.ProjectPath = AbsoluteNormalized(projectPath);
			journal = {};
			journalDocument.clear();
			errorMessage.clear();
			if (!PinMigrationProjectDirectory(projectPath, guards.Root, errorMessage))
				return false;

			const std::filesystem::path journalPath =
				MigrationJournalPath(projectPath);
			if (!ValidateMigrationPath(
				projectPath, projectPath, true, errorMessage) ||
				!ValidateMigrationPath(
					projectPath, journalPath, true, errorMessage))
				return false;

			std::error_code filesystemError;
			const std::filesystem::file_status status =
				std::filesystem::symlink_status(journalPath, filesystemError);
			if (IsMissingMigrationPathError(filesystemError) ||
				(!filesystemError && !std::filesystem::exists(status)))
				return true;
			if (filesystemError)
			{
				errorMessage = "Could not inspect migration journal: " +
					filesystemError.message();
				return false;
			}
			if (!std::filesystem::is_regular_file(status) ||
				std::filesystem::is_symlink(status))
			{
				errorMessage =
					"Migration journal is not a regular, non-symlink file";
				return false;
			}
			if (!guards.Active.Acquire(journalPath.parent_path(), errorMessage))
			{
				errorMessage = "Could not pin migration journal directory: " +
					errorMessage;
				return false;
			}
			if (!ReadMigrationFileBytes(
				projectPath, journalPath, journalDocument, errorMessage) ||
				!ParseMigrationJournal(
					projectPath, journalDocument, journal, errorMessage))
				return false;

			preview.TransactionID = journal.TransactionID;
			preview.JournalState = journal.State;
			preview.JournalSize = journalDocument.size();
			preview.JournalSHA256 = MigrationSHA256(journalDocument);
			preview.BackupDirectory =
				std::filesystem::path("ProjectSettings") /
				UTF8ToPath(std::string(kProjectMigrationBackupDirectory)) /
				UTF8ToPath(journal.TransactionID);

			const std::filesystem::path backupDirectory =
				MigrationBackupRoot(projectPath) /
				UTF8ToPath(journal.TransactionID);
			if (journal.State == "prepared")
			{
				if (!ValidateMigrationPath(
					projectPath, backupDirectory, false, errorMessage))
					return false;
				FileSystem::PinnedDirectoryChain transactionGuard;
				if (!transactionGuard.Acquire(backupDirectory, errorMessage))
				{
					errorMessage = "Could not pin migration backup directory: " +
						errorMessage;
					return false;
				}
				guards.Active = std::move(transactionGuard);
			}

			preview.Changes.reserve(journal.Entries.size());
			for (const ProjectMigrationJournalEntry& entry : journal.Entries)
			{
				if (journal.State == "prepared")
				{
					const std::filesystem::path backup =
						backupDirectory / entry.BackupFile;
					if (entry.Existed)
					{
						std::string original;
						if (!ReadMigrationFileBytes(
							projectPath, backup, original, errorMessage))
							return false;
						if (original.size() != entry.OriginalSize ||
							MigrationSHA256(original) != entry.OriginalSHA256)
						{
							errorMessage =
								"Migration backup SHA-256 or size does not match "
								"its journal: " + PathToUTF8(backup);
							return false;
						}
					}
					else
					{
						if (!ValidateMigrationPath(
							projectPath, backup, true, errorMessage))
							return false;
						std::error_code backupError;
						const std::filesystem::file_status backupStatus =
							std::filesystem::symlink_status(backup, backupError);
						if ((!backupError && std::filesystem::exists(backupStatus)) ||
							(backupError &&
								!IsMissingMigrationPathError(backupError)))
						{
							errorMessage =
								"Migration journal has an unexpected backup for "
								"a created file";
							return false;
						}
					}
				}

				ProjectMigrationRecoveryChange change;
				change.RelativePath = entry.RelativeTarget;
				change.OriginalExisted = entry.Existed;
				change.OriginalSize = entry.OriginalSize;
				change.OriginalSHA256 = entry.OriginalSHA256;
				const std::filesystem::path target =
					projectPath.parent_path() / entry.RelativeTarget;
				if (!InspectMigrationTarget(projectPath, target,
					change.CurrentExists, change.CurrentSize,
					change.CurrentSHA256, errorMessage))
					return false;
				if (journal.State == "committed")
					change.Action = ProjectMigrationRecoveryAction::KeepCurrent;
				else if (entry.Existed)
					change.Action = change.CurrentExists &&
						change.CurrentSize == entry.OriginalSize &&
						change.CurrentSHA256 == entry.OriginalSHA256
						? ProjectMigrationRecoveryAction::AlreadyRestored
						: ProjectMigrationRecoveryAction::RestoreOriginal;
				else
					change.Action = change.CurrentExists
						? ProjectMigrationRecoveryAction::RemoveCreatedFile
						: ProjectMigrationRecoveryAction::AlreadyAbsent;
				preview.Changes.push_back(std::move(change));
			}
			return true;
		}

	}

	Project::Project(const std::filesystem::path& projectPath)
		: m_ProjectPath(projectPath.lexically_normal()), m_Directory(projectPath.parent_path())
	{
	}

	bool Project::SaveSettings() const
	{
		try
		{
			if (m_ProjectPath.empty())
				throw std::runtime_error("Project path is empty");
			std::string validationError;
			if (!ValidateProjectSettings(m_Settings, validationError))
				throw std::runtime_error(validationError);

			const std::filesystem::path settingsPath = GetSettingsPath();
			std::error_code directoryError;
			std::filesystem::create_directories(settingsPath.parent_path(), directoryError);
			if (directoryError)
				throw std::runtime_error("Could not create ProjectSettings directory: " +
					directoryError.message());

			std::ostringstream output;
			output << "{\n"
				<< "  \"schemaVersion\": " << kProjectSettingsSchemaVersion << ",\n"
				<< "  \"tagsAndLayers\": {\n"
				<< "    \"tags\": [";
			for (std::size_t index = 0; index < m_Settings.TagsAndLayers.Tags.size(); ++index)
			{
				if (index != 0)
					output << ", ";
				output << '"' << EscapeJsonString(m_Settings.TagsAndLayers.Tags[index]) << '"';
			}
			output << "],\n"
				<< "    \"layerNames\": [";
			for (std::size_t index = 0; index < m_Settings.TagsAndLayers.LayerNames.size(); ++index)
			{
				if (index != 0)
					output << ", ";
				output << '"' << EscapeJsonString(m_Settings.TagsAndLayers.LayerNames[index]) << '"';
			}
			output << "]\n"
				<< "  },\n"
				<< "  \"physics2D\": {\n"
				<< "    \"collisionMasks\": [";
			for (std::size_t index = 0; index < m_Settings.Physics2D.CollisionMasks.size(); ++index)
			{
				if (index != 0)
					output << ", ";
				output << m_Settings.Physics2D.CollisionMasks[index];
			}
			output << "]\n"
				<< "  }\n"
				<< "}\n";
			if (!output.good())
				throw std::runtime_error("Could not serialize project settings JSON");

			std::string writeError;
			if (!FileSystem::WriteFileAtomically(settingsPath, output.str(), writeError))
				throw std::runtime_error("Could not atomically replace project settings: " + writeError);
			return true;
		}
		catch (const std::exception& exception)
		{
			TC_Core_Error("Failed to save project settings '{0}': {1}",
				PathToUTF8(GetSettingsPath()), exception.what());
			return false;
		}
	}

	bool Project::SavePlayerSettings() const
	{
		try
		{
			if (m_ProjectPath.empty())
				throw std::runtime_error("Project path is empty");
			PlayerSettings normalized = m_PlayerSettings;
			std::string validationError;
			if (!NormalizeAndValidatePlayerSettings(normalized, validationError))
				throw std::runtime_error(validationError);

			const std::filesystem::path settingsPath = GetPlayerSettingsPath();
			std::error_code directoryError;
			std::filesystem::create_directories(settingsPath.parent_path(), directoryError);
			if (directoryError)
				throw std::runtime_error("Could not create ProjectSettings directory: "
					+ directoryError.message());

			std::ostringstream output;
			output << "{\n"
				<< "  \"schemaVersion\": " << kPlayerSettingsSchemaVersion << ",\n"
				<< "  \"productName\": \"" << EscapeJsonString(normalized.ProductName) << "\",\n"
				<< "  \"companyName\": \"" << EscapeJsonString(normalized.CompanyName) << "\",\n"
				<< "  \"version\": \"" << EscapeJsonString(normalized.Version) << "\",\n"
				<< "  \"icon\": " << static_cast<uint64_t>(normalized.Icon) << ",\n"
				<< "  \"display\": {\n"
				<< "    \"width\": " << normalized.Width << ",\n"
				<< "    \"height\": " << normalized.Height << ",\n"
				<< "    \"windowMode\": \""
				<< PlayerWindowModeToString(normalized.WindowMode) << "\",\n"
				<< "    \"resizable\": " << (normalized.Resizable ? "true" : "false") << ",\n"
				<< "    \"vSync\": " << (normalized.VSync ? "true" : "false") << "\n"
				<< "  },\n"
				<< "  \"directories\": {\n"
				<< "    \"save\": \"" << EscapeJsonString(PathToUTF8(normalized.SaveDirectory)) << "\",\n"
				<< "    \"log\": \"" << EscapeJsonString(PathToUTF8(normalized.LogDirectory)) << "\",\n"
				<< "    \"crash\": \"" << EscapeJsonString(PathToUTF8(normalized.CrashDirectory)) << "\"\n"
				<< "  }\n"
				<< "}\n";
			if (!output.good())
				throw std::runtime_error("Could not serialize Player settings JSON");
			std::string writeError;
			if (!FileSystem::WriteFileAtomically(settingsPath, output.str(), writeError))
				throw std::runtime_error("Could not atomically replace Player settings: " + writeError);
			return true;
		}
		catch (const std::exception& exception)
		{
			TC_Core_Error("Failed to save Player settings '{0}': {1}",
				PathToUTF8(GetPlayerSettingsPath()), exception.what());
			return false;
		}
	}

	bool Project::SaveBuildSettings() const
	{
		try
		{
			if (m_ProjectPath.empty())
				throw std::runtime_error("Project path is empty");
			BuildSettings normalized = m_BuildSettings;
			std::string validationError;
			if (!NormalizeAndValidateBuildSettings(normalized, validationError))
				throw std::runtime_error(validationError);

			const std::filesystem::path settingsPath = GetBuildSettingsPath();
			std::error_code directoryError;
			std::filesystem::create_directories(settingsPath.parent_path(), directoryError);
			if (directoryError)
				throw std::runtime_error("Could not create ProjectSettings directory: " +
					directoryError.message());

			std::ostringstream output;
			output << "{\n"
				<< "  \"schemaVersion\": " << kBuildSettingsSchemaVersion << ",\n"
				<< "  \"entrySceneHandle\": "
				<< static_cast<uint64_t>(normalized.EntrySceneHandle) << ",\n"
				<< "  \"scenes\": [";
			for (std::size_t index = 0; index < normalized.Scenes.size(); ++index)
			{
				const BuildSceneSettings& scene = normalized.Scenes[index];
				if (index != 0)
					output << ',';
				output << "\n    {\n"
					<< "      \"handle\": " << static_cast<uint64_t>(scene.Handle) << ",\n"
					<< "      \"enabled\": " << (scene.Enabled ? "true" : "false") << ",\n"
					<< "      \"pathHint\": \""
					<< EscapeJsonString(PathToUTF8(scene.PathHint)) << "\"\n"
					<< "    }";
			}
			if (!normalized.Scenes.empty())
				output << '\n';
			output << "  ]\n}\n";
			if (!output.good())
				throw std::runtime_error("Could not serialize build settings JSON");

			std::string writeError;
			if (!FileSystem::WriteFileAtomically(settingsPath, output.str(), writeError))
				throw std::runtime_error("Could not atomically replace build settings: " + writeError);
			return true;
		}
		catch (const std::exception& exception)
		{
			TC_Core_Error("Failed to save build settings '{0}': {1}",
				PathToUTF8(GetBuildSettingsPath()), exception.what());
			return false;
		}
	}

	bool Project::SetSettings(const ProjectSettings& settings)
	{
		std::string validationError;
		if (!ValidateProjectSettings(settings, validationError))
		{
			TC_Core_Error("Cannot save project settings: {0}", validationError);
			return false;
		}
		const ProjectSettings previous = m_Settings;
		m_Settings = settings;
		if (SaveSettings())
			return true;
		m_Settings = previous;
		return false;
	}

	bool Project::SetPlayerSettings(const PlayerSettings& settings)
	{
		PlayerSettings normalized = settings;
		std::string validationError;
		if (!NormalizeAndValidatePlayerSettings(normalized, validationError))
		{
			TC_Core_Error("Cannot save Player settings: {0}", validationError);
			return false;
		}
		const PlayerSettings previous = m_PlayerSettings;
		m_PlayerSettings = std::move(normalized);
		if (SavePlayerSettings())
			return true;
		m_PlayerSettings = previous;
		return false;
	}

	bool Project::SetBuildSettings(const BuildSettings& settings)
	{
		BuildSettings normalized = settings;
		std::string validationError;
		if (!NormalizeAndValidateBuildSettings(normalized, validationError))
		{
			TC_Core_Error("Cannot save build settings: {0}", validationError);
			return false;
		}

		const BuildSettings previous = m_BuildSettings;
		const std::filesystem::path previousStartScene = m_Config.StartScene;
		const AssetHandle previousStartSceneHandle = m_Config.StartSceneHandle;
		m_BuildSettings = std::move(normalized);
		SynchronizeLegacyStartSceneMirror();
		if (SaveBuildSettings())
			return true;
		m_BuildSettings = previous;
		m_Config.StartScene = previousStartScene;
		m_Config.StartSceneHandle = previousStartSceneHandle;
		return false;
	}

	void Project::SynchronizeLegacyStartSceneMirror()
	{
		m_Config.StartSceneHandle = m_BuildSettings.EntrySceneHandle;
		m_Config.StartScene.clear();
		if (static_cast<uint64_t>(m_BuildSettings.EntrySceneHandle) == 0)
			return;
		const auto entry = std::find_if(m_BuildSettings.Scenes.begin(),
			m_BuildSettings.Scenes.end(), [this](const BuildSceneSettings& scene)
			{
				return scene.Handle == m_BuildSettings.EntrySceneHandle;
			});
		if (entry != m_BuildSettings.Scenes.end())
			m_Config.StartScene = entry->PathHint;
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
		for (BuildSceneSettings& scene : m_BuildSettings.Scenes)
		{
			if (scene.Handle == m_BuildSettings.EntrySceneHandle)
			{
				scene.PathHint = m_Config.StartScene;
				break;
			}
		}
		return true;
	}

	bool Project::SetStartSceneHandle(AssetHandle handle)
	{
		BuildSettings candidate = m_BuildSettings;
		candidate.EntrySceneHandle = handle;
		if (static_cast<uint64_t>(handle) != 0)
		{
			auto scene = std::find_if(candidate.Scenes.begin(), candidate.Scenes.end(),
				[handle](const BuildSceneSettings& value) { return value.Handle == handle; });
			if (scene == candidate.Scenes.end())
			{
				BuildSceneSettings entry;
				entry.Handle = handle;
				entry.Enabled = true;
				entry.PathHint = m_Config.StartScene;
				candidate.Scenes.push_back(std::move(entry));
			}
			else
			{
				scene->Enabled = true;
				if (!m_Config.StartScene.empty())
					scene->PathHint = m_Config.StartScene;
			}
		}

		std::string validationError;
		if (!NormalizeAndValidateBuildSettings(candidate, validationError))
		{
			TC_Core_Error("Cannot set entry scene: {0}", validationError);
			return false;
		}
		m_BuildSettings = std::move(candidate);
		SynchronizeLegacyStartSceneMirror();
		return true;
	}

	EditorProjectStateLoadResult Project::LoadEditorState(EditorProjectState& state) const
	{
		if (m_ProjectPath.empty())
			return EditorProjectStateLoadResult::Missing;

		const std::filesystem::path settingsPath = GetUserSettingsPath() / "editor.json";
		std::error_code error;
		const bool exists = std::filesystem::exists(settingsPath, error);
		if (error)
		{
			TC_Core_Warn("Could not inspect Editor settings '{0}': {1}",
				PathToUTF8(settingsPath), error.message());
			return EditorProjectStateLoadResult::Failed;
		}
		if (!exists)
			return EditorProjectStateLoadResult::Missing;

		try
		{
			std::ifstream input(settingsPath, std::ios::binary);
			if (!input)
				throw std::runtime_error("Could not open the settings file");
			std::ostringstream contents;
			contents << input.rdbuf();
			if (input.bad())
				throw std::runtime_error("Failed while reading the settings file");
			const std::string json = contents.str();
			if (!JsonSyntaxValidator(json).Validate())
				throw std::runtime_error("File is not valid JSON");
			YAML::Node root = YAML::Load(json);
			if (!root.IsMap())
				throw std::runtime_error("Root must be a JSON object");

			const YAML::Node schemaNode = root["schemaVersion"];
			if (!schemaNode)
				throw std::runtime_error("schemaVersion is required");
			const uint32_t schemaVersion = schemaNode.as<uint32_t>();
			if (schemaVersion != 1U)
				throw std::runtime_error("Unsupported schemaVersion " + std::to_string(schemaVersion));

			const YAML::Node browserNode = root["contentBrowser"];
			if (!browserNode || !browserNode.IsMap())
				throw std::runtime_error("contentBrowser must be a JSON object");

			EditorProjectState loaded;
			loaded.ContentBrowserCurrentDirectory = ReadOptional<std::string>(
				browserNode, "currentDirectory", ".");
			loaded.ExternalScriptEditor = UTF8ToPath(ReadOptional<std::string>(
				root, "externalScriptEditor", ""));
			const YAML::Node expandedNodes = browserNode["expandedNodes"];
			if (expandedNodes)
			{
				if (!expandedNodes.IsSequence())
					throw std::runtime_error("contentBrowser.expandedNodes must be an array");
				for (const YAML::Node& node : expandedNodes)
					loaded.ContentBrowserExpandedNodes.push_back(node.as<std::string>());
			}

			std::string editorError;
			if (!NormalizeExternalScriptEditor(loaded.ExternalScriptEditor, editorError))
				throw std::runtime_error(editorError);
			NormalizeEditorState(loaded, GetAssetPath());
			state = std::move(loaded);
			return EditorProjectStateLoadResult::Loaded;
		}
		catch (const std::exception& exception)
		{
			TC_Core_Warn("Failed to load Editor settings '{0}': {1}",
				PathToUTF8(settingsPath), exception.what());
			return EditorProjectStateLoadResult::Failed;
		}
	}

	bool Project::SaveEditorState(const EditorProjectState& state) const
	{
		if (m_ProjectPath.empty())
			return false;

		const std::filesystem::path settingsPath = GetUserSettingsPath() / "editor.json";
		std::error_code error;
		std::filesystem::create_directories(settingsPath.parent_path(), error);
		if (error)
		{
			TC_Core_Error("Could not create Editor settings directory '{0}': {1}",
				PathToUTF8(settingsPath.parent_path()), error.message());
			return false;
		}

		EditorProjectState normalized = state;
		std::string editorError;
		if (!NormalizeExternalScriptEditor(normalized.ExternalScriptEditor, editorError))
		{
			TC_Core_Error("Cannot save Editor settings '{0}': {1}",
				PathToUTF8(settingsPath), editorError);
			return false;
		}
		NormalizeEditorState(normalized, GetAssetPath());

		std::ostringstream json;
		json << "{\n"
			<< "  \"schemaVersion\": 1,\n"
			<< "  \"contentBrowser\": {\n"
			<< "    \"currentDirectory\": \""
			<< EscapeJsonString(normalized.ContentBrowserCurrentDirectory) << "\",\n"
			<< "    \"expandedNodes\": [";
		for (size_t index = 0; index < normalized.ContentBrowserExpandedNodes.size(); ++index)
		{
			if (index == 0)
				json << '\n';
			else
				json << ",\n";
			json << "      \"" << EscapeJsonString(normalized.ContentBrowserExpandedNodes[index]) << '"';
		}
		if (!normalized.ContentBrowserExpandedNodes.empty())
			json << '\n' << "    ";
		json << "]\n"
			<< "  },\n"
			<< "  \"externalScriptEditor\": \""
			<< EscapeJsonString(PathToUTF8(normalized.ExternalScriptEditor)) << "\"\n"
			<< "}\n";

		std::string writeError;
		if (!FileSystem::WriteFileAtomically(settingsPath, json.str(), writeError))
		{
			TC_Core_Error("Failed to save Editor settings '{0}': {1}",
				PathToUTF8(settingsPath), writeError);
			return false;
		}
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
		if (!NormalizeAndValidateConfig(project->m_Config, validationError))
		{
			TC_Core_Error("Cannot create project '{0}': {1}", PathToUTF8(projectPath), validationError);
			return nullptr;
		}
		project->m_BuildSettings = BuildSettings{};
		project->m_PlayerSettings = MakeDefaultPlayerSettings(
			project->m_Config.Name, project->m_Config.Version);
		if (static_cast<uint64_t>(project->m_Config.StartSceneHandle) != 0)
		{
			BuildSceneSettings entry;
			entry.Handle = project->m_Config.StartSceneHandle;
			entry.Enabled = true;
			entry.PathHint = project->m_Config.StartScene;
			project->m_BuildSettings.EntrySceneHandle = entry.Handle;
			project->m_BuildSettings.Scenes.push_back(std::move(entry));
		}
		project->SynchronizeLegacyStartSceneMirror();

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
		if (!project->Save() || !project->SaveSettings())
		{
			std::error_code cleanupError;
			std::filesystem::remove(project->GetSettingsPath(), cleanupError);
			cleanupError.clear();
			std::filesystem::remove(project->GetBuildSettingsPath(), cleanupError);
			cleanupError.clear();
			std::filesystem::remove(project->GetPlayerSettingsPath(), cleanupError);
			cleanupError.clear();
			std::filesystem::remove(project->GetSettingsPath().parent_path(), cleanupError);
			cleanupError.clear();
			std::filesystem::remove(project->GetProjectPath(), cleanupError);
			rollbackCreatedDirectories();
			TC_Core_Error("Could not save new project and its settings '{0}'",
				PathToUTF8(normalizedProjectPath));
			return nullptr;
		}

		// Only Assets and their .tcmeta sidecars are project source content. Local
		// Editor state and derived/imported data must never be committed.
		if (!EnsureAssetSystemIgnoreRules(projectDirectory))
			TC_Core_Warn("Could not ensure asset-system ignore rules for '{0}'",
				PathToUTF8(projectDirectory));

		project->UpdateLastOperationTime();
		return project;
	}

	Ref<Project> Project::Inspect(const std::filesystem::path& projectPath)
	{
		return LoadInternal(projectPath, true, nullptr);
	}

	bool Project::PreviewMigration(const std::filesystem::path& projectPath,
		ProjectMigrationPreview& preview, std::string& errorMessage)
	{
		preview = {};
		errorMessage.clear();
		try
		{
			if (!RequireNoActiveMigrationJournal(projectPath, errorMessage))
				return false;

			Ref<Project> inspected = Inspect(projectPath);
			if (!inspected)
				throw std::runtime_error("Project validation failed");

			std::string projectDocument;
			if (!ReadMigrationFileBytes(
				projectPath, projectPath, projectDocument, errorMessage))
				return false;
			const YAML::Node root = YAML::Load(projectDocument);
			uint32_t schemaVersion = 0;
			(void)RequireSupportedProjectDocument(root, schemaVersion);

			bool buildSettingsExist = false;
			uintmax_t ignoredSize = 0;
			std::string ignoredSHA256;
			if (!InspectMigrationTarget(projectPath,
				inspected->GetBuildSettingsPath(), buildSettingsExist,
				ignoredSize, ignoredSHA256, errorMessage))
				return false;
			bool playerSettingsExist = false;
			if (!InspectMigrationTarget(projectPath,
				inspected->GetPlayerSettingsPath(), playerSettingsExist,
				ignoredSize, ignoredSHA256, errorMessage))
				return false;

			return BuildProjectMigrationPreview(projectPath, schemaVersion,
				!buildSettingsExist, !playerSettingsExist, preview, errorMessage);
		}
		catch (const std::exception& exception)
		{
			errorMessage = exception.what();
			return false;
		}
	}

	bool Project::PreviewInterruptedMigration(
		const std::filesystem::path& projectPath,
		ProjectMigrationRecoveryPreview& preview, std::string& errorMessage)
	{
		ProjectMigrationJournal journal;
		ProjectMigrationDirectoryGuards guards;
		std::string journalDocument;
		return InspectInterruptedMigrationJournal(projectPath, preview,
			journal, journalDocument, guards, errorMessage);
	}

	bool Project::RecoverInterruptedMigration(
		const std::filesystem::path& projectPath,
		const ProjectMigrationRecoveryPreview& approvedRecovery,
		std::string& errorMessage)
	{
		ProjectMigrationRecoveryPreview current;
		ProjectMigrationJournal journal;
		ProjectMigrationDirectoryGuards guards;
		std::string journalDocument;
		if (!InspectInterruptedMigrationJournal(projectPath, current,
			journal, journalDocument, guards, errorMessage))
			return false;
		if (!MigrationRecoveryPreviewsMatch(approvedRecovery, current))
		{
			errorMessage =
				"Interrupted migration recovery changed after approval; review it again";
			return false;
		}
		if (!current.HasPendingRecovery())
			return true;
		if (!RestoreMigrationJournal(
			projectPath, journal, errorMessage, &guards.Active,
			&current, journalDocument))
			return false;
		TC_Core_Warn("Recovered interrupted project migration '{0}'",
			journal.TransactionID);
		return true;
	}

	bool Project::AbandonInterruptedMigrationRecovery(
		const std::filesystem::path& projectPath,
		const ProjectMigrationRecoveryPreview& approvedRecovery,
		std::string& errorMessage)
	{
		ProjectMigrationRecoveryPreview current;
		ProjectMigrationJournal journal;
		ProjectMigrationDirectoryGuards guards;
		std::string journalDocument;
		if (!InspectInterruptedMigrationJournal(projectPath, current,
			journal, journalDocument, guards, errorMessage))
			return false;
		if (!current.HasPendingRecovery())
		{
			errorMessage = "There is no interrupted migration to abandon";
			return false;
		}
		if (!MigrationRecoveryPreviewsMatch(approvedRecovery, current))
		{
			errorMessage =
				"Interrupted migration recovery changed after approval; review it again";
			return false;
		}

		const std::filesystem::path journalPath =
			MigrationJournalPath(projectPath);
		for (const ProjectMigrationRecoveryChange& change : current.Changes)
		{
			bool targetExists = false;
			uintmax_t targetSize = 0;
			std::string targetSHA256;
			const std::filesystem::path target =
				projectPath.parent_path() / change.RelativePath;
			if (!InspectMigrationTarget(projectPath, target, targetExists,
				targetSize, targetSHA256, errorMessage))
				return false;
			if (targetExists != change.CurrentExists ||
				targetSize != change.CurrentSize ||
				targetSHA256 != change.CurrentSHA256)
			{
				errorMessage =
					"Migration target changed after recovery approval: " +
					PathToUTF8(target);
				return false;
			}
		}
		std::string currentJournal;
		if (!ReadMigrationFileBytes(
			projectPath, journalPath, currentJournal, errorMessage) ||
			currentJournal != journalDocument)
		{
			if (errorMessage.empty())
				errorMessage =
					"Migration journal changed after recovery approval";
			return false;
		}
		if (guards.Active.IsAcquired() &&
			!guards.Active.Verify(errorMessage))
			return false;
		if (journal.State == "prepared")
		{
			const std::filesystem::path archivedJournal =
				MigrationBackupRoot(projectPath) /
				UTF8ToPath(journal.TransactionID) / "journal.json";
			std::string archivedDocument;
			if (!ReadMigrationFileBytes(projectPath, archivedJournal,
				archivedDocument, errorMessage) ||
				archivedDocument != journalDocument)
			{
				if (errorMessage.empty())
					errorMessage =
						"Migration backup archive journal does not match the active journal";
				return false;
			}
		}
		if (!RemoveMigrationPathSafely(
			projectPath, journalPath, true, errorMessage))
		{
			errorMessage =
				"Could not abandon the interrupted migration recovery: " +
				errorMessage;
			return false;
		}
		TC_Core_Warn(
			"Abandoned interrupted project migration recovery '{0}'; "
			"current files and backup archive were retained",
			journal.TransactionID);
		errorMessage.clear();
		return true;
	}

	bool Project::ExportInterruptedMigrationBackup(
		const std::filesystem::path& projectPath,
		const ProjectMigrationRecoveryPreview& approvedRecovery,
		const std::filesystem::path& destinationDirectory,
		std::string& errorMessage)
	{
		ProjectMigrationRecoveryPreview current;
		ProjectMigrationJournal journal;
		ProjectMigrationDirectoryGuards guards;
		std::string journalDocument;
		if (!InspectInterruptedMigrationJournal(projectPath, current,
			journal, journalDocument, guards, errorMessage))
			return false;
		if (!current.HasPendingRecovery())
		{
			errorMessage = "There is no interrupted migration to export";
			return false;
		}
		if (!MigrationRecoveryPreviewsMatch(approvedRecovery, current))
		{
			errorMessage =
				"Interrupted migration recovery changed after approval; review it again";
			return false;
		}
		if (destinationDirectory.empty())
		{
			errorMessage = "Migration recovery export destination is empty";
			return false;
		}

		const std::filesystem::path destination =
			AbsoluteNormalized(destinationDirectory);
		if (IsPathWithinOrEqual(projectPath.parent_path(), destination))
		{
			errorMessage =
				"Migration recovery backup must be exported outside the project";
			return false;
		}
		std::error_code filesystemError;
		const std::filesystem::file_status destinationStatus =
			std::filesystem::symlink_status(destination, filesystemError);
		if ((!filesystemError && std::filesystem::exists(destinationStatus)) ||
			(filesystemError && !IsMissingMigrationPathError(filesystemError)))
		{
			errorMessage =
				"Migration recovery export destination already exists or cannot be inspected";
			return false;
		}
		const std::filesystem::path destinationParent =
			destination.parent_path();
		if (!ValidateNoReparsePathChain(
			destinationParent, false, errorMessage))
		{
			errorMessage = "Migration recovery export parent is unsafe: " +
				errorMessage;
			return false;
		}
		FileSystem::PinnedDirectoryChain exportParentGuard;
		if (!exportParentGuard.Acquire(destinationParent, errorMessage))
		{
			errorMessage = "Could not pin migration recovery export parent: " +
				errorMessage;
			return false;
		}

		struct ExportFile
		{
			std::filesystem::path RelativePath;
			std::string Contents;
		};
		std::vector<ExportFile> files;
		files.push_back({ "migration-journal.json", journalDocument });
		files.push_back({ "README.txt",
			"TomCat interrupted project migration recovery export\n\n"
			"Original/ contains the validated pre-migration files.\n"
			"Current/ contains the affected files as they existed when this "
			"export was approved.\n"
			"The project and its active recovery journal were not changed.\n" });

		const std::filesystem::path backupDirectory =
			MigrationBackupRoot(projectPath) /
				UTF8ToPath(journal.TransactionID);
		for (size_t index = 0; index < journal.Entries.size(); ++index)
		{
			const ProjectMigrationJournalEntry& entry = journal.Entries[index];
			const ProjectMigrationRecoveryChange& change =
				current.Changes[index];
			if (entry.Existed)
			{
				std::string original;
				const std::filesystem::path backup =
					backupDirectory / entry.BackupFile;
				if (!ReadMigrationFileBytes(
					projectPath, backup, original, errorMessage))
					return false;
				if (original.size() != entry.OriginalSize ||
					MigrationSHA256(original) != entry.OriginalSHA256)
				{
					errorMessage =
						"Migration backup changed during export: " +
						PathToUTF8(backup);
					return false;
				}
				files.push_back({
					std::filesystem::path("Original") / entry.RelativeTarget,
					std::move(original) });
			}

			const std::filesystem::path target =
				projectPath.parent_path() / entry.RelativeTarget;
			bool targetExists = false;
			uintmax_t targetSize = 0;
			std::string targetSHA256;
			if (!InspectMigrationTarget(projectPath, target, targetExists,
				targetSize, targetSHA256, errorMessage))
				return false;
			if (targetExists != change.CurrentExists ||
				targetSize != change.CurrentSize ||
				targetSHA256 != change.CurrentSHA256)
			{
				errorMessage =
					"Migration target changed during recovery export: " +
					PathToUTF8(target);
				return false;
			}
			if (targetExists)
			{
				std::string targetContents;
				if (!ReadMigrationFileBytes(
					projectPath, target, targetContents, errorMessage) ||
					targetContents.size() != targetSize ||
					MigrationSHA256(targetContents) != targetSHA256)
				{
					errorMessage =
						"Migration target changed while it was exported: " +
						PathToUTF8(target);
					return false;
				}
				files.push_back({
					std::filesystem::path("Current") / entry.RelativeTarget,
					std::move(targetContents) });
			}
		}

		const std::filesystem::path staging =
			FileSystem::MakeTemporarySiblingPath(destination);
		if (staging.empty())
		{
			errorMessage =
				"Could not allocate a migration recovery export staging path";
			return false;
		}
		filesystemError.clear();
		if (!std::filesystem::create_directory(staging, filesystemError) ||
			filesystemError)
		{
			errorMessage = "Could not create migration recovery export: " +
				filesystemError.message();
			return false;
		}
		auto cleanupStaging = [&]()
		{
			std::error_code cleanupError;
			std::filesystem::remove_all(staging, cleanupError);
		};
		for (const ExportFile& file : files)
		{
			const std::filesystem::path output = staging / file.RelativePath;
			filesystemError.clear();
			std::filesystem::create_directories(
				output.parent_path(), filesystemError);
			if (filesystemError)
			{
				errorMessage =
					"Could not create migration recovery export directory: " +
					filesystemError.message();
				cleanupStaging();
				return false;
			}
			std::string writeError;
			if (!FileSystem::WriteFileAtomically(
				output, file.Contents, writeError))
			{
				errorMessage =
					"Could not write migration recovery export: " + writeError;
				cleanupStaging();
				return false;
			}
		}
		filesystemError.clear();
		if (!exportParentGuard.Verify(errorMessage))
		{
			cleanupStaging();
			return false;
		}
		filesystemError.clear();
		const std::filesystem::file_status publishStatus =
			std::filesystem::symlink_status(destination, filesystemError);
		if ((!filesystemError && std::filesystem::exists(publishStatus)) ||
			(filesystemError && !IsMissingMigrationPathError(filesystemError)))
		{
			errorMessage =
				"Migration recovery export destination appeared before publish";
			cleanupStaging();
			return false;
		}
		filesystemError.clear();
		std::filesystem::rename(staging, destination, filesystemError);
		if (filesystemError)
		{
			errorMessage = "Could not publish migration recovery export: " +
				filesystemError.message();
			cleanupStaging();
			return false;
		}
		errorMessage.clear();
		return true;
	}

	Ref<Project> Project::Load(const std::filesystem::path& projectPath)
	{
		return LoadInternal(projectPath, false, nullptr);
	}

	Ref<Project> Project::LoadWithMigration(
		const std::filesystem::path& projectPath,
		const ProjectMigrationPreview& approvedMigration)
	{
		return LoadInternal(projectPath, false, &approvedMigration);
	}

	Ref<Project> Project::LoadInternal(const std::filesystem::path& projectPath,
		bool inspectOnly, const ProjectMigrationPreview* approvedMigration)
	{
		if (!inspectOnly)
		{
			std::string journalError;
			if (!RequireNoActiveMigrationJournal(projectPath, journalError))
			{
				TC_Core_Error("Could not load project '{0}': {1}",
					PathToUTF8(projectPath), journalError);
				return nullptr;
			}
		}

		std::error_code error;
		if (!std::filesystem::is_regular_file(projectPath, error) || error)
			return nullptr;

		try
		{
			YAML::Node data;
			{
				std::ifstream input(projectPath, std::ios::binary);
				if (!input)
					throw std::runtime_error("Could not open the project file");
				data = YAML::Load(input);
				if (input.bad())
					throw std::runtime_error("Failed while reading the project file");
			}
			uint32_t schemaVersion = 0;
			const YAML::Node projectNode = RequireSupportedProjectDocument(data, schemaVersion);

			auto project = CreateRef<Project>(projectPath);
			project->m_Config = ReadProjectConfig(projectNode, schemaVersion);
			std::string buildSettingsError;
			const BuildSettingsLoadResult buildSettingsResult = LoadBuildSettingsFile(
				project->GetBuildSettingsPath(), project->m_BuildSettings, buildSettingsError);
			if (buildSettingsResult == BuildSettingsLoadResult::Failed)
				throw std::runtime_error("Invalid build settings: " + buildSettingsError);
			if (buildSettingsResult == BuildSettingsLoadResult::Missing)
			{
				if (schemaVersion == CurrentSchemaVersion)
					throw std::runtime_error("ProjectSettings/BuildSettings.json is required by project schema " +
						std::to_string(CurrentSchemaVersion));
				if (static_cast<uint64_t>(project->m_Config.StartSceneHandle) != 0)
				{
					BuildSceneSettings entry;
					entry.Handle = project->m_Config.StartSceneHandle;
					entry.Enabled = true;
					entry.PathHint = project->m_Config.StartScene;
					project->m_BuildSettings.EntrySceneHandle = entry.Handle;
					project->m_BuildSettings.Scenes.push_back(std::move(entry));
				}
			}
			project->SynchronizeLegacyStartSceneMirror();
			std::string settingsError;
			ProjectSettingsLoadResult settingsResult = LoadProjectSettingsFile(
				project->GetSettingsPath(), project->m_Settings, settingsError,
				ProjectSettingsDocumentFormat::Json);
			if (settingsResult == ProjectSettingsLoadResult::Missing)
			{
				const std::filesystem::path legacySettingsPath = project->m_Directory
					/ "ProjectSettings" / "ProjectSettings.tcsettings";
				settingsResult = LoadProjectSettingsFile(legacySettingsPath,
					project->m_Settings, settingsError,
					ProjectSettingsDocumentFormat::LegacyYaml);
			}
			if (settingsResult == ProjectSettingsLoadResult::Failed)
				throw std::runtime_error("Invalid project settings: " + settingsError);

			project->m_PlayerSettings = MakeDefaultPlayerSettings(
				project->m_Config.Name, project->m_Config.Version);
			std::string playerSettingsError;
			const PlayerSettingsLoadResult playerSettingsResult = LoadPlayerSettingsFile(
				project->GetPlayerSettingsPath(), project->m_PlayerSettings,
				playerSettingsError);
			if (playerSettingsResult == PlayerSettingsLoadResult::Failed)
				throw std::runtime_error("Invalid Player settings: " + playerSettingsError);

			project->m_PreservedDocument = ReadWholeFile(projectPath);
			if (!inspectOnly)
			{
				ProjectMigrationPreview migration;
				std::string migrationError;
				if (!BuildProjectMigrationPreview(projectPath, schemaVersion,
					buildSettingsResult == BuildSettingsLoadResult::Missing,
					playerSettingsResult == PlayerSettingsLoadResult::Missing,
					migration, migrationError))
					throw std::runtime_error("Could not prepare project migration: " +
						migrationError);
				if (approvedMigration &&
					!MigrationPreviewsMatch(*approvedMigration, migration))
					throw std::runtime_error(
						"Approved project migration plan is stale or incomplete");
				if (migration.RequiresMigration())
				{
					if (!approvedMigration)
						throw std::runtime_error(
							"Project migration is required; preview and explicitly approve it before loading");
					if (!ExecuteProjectMigration(*project, migration, migrationError))
						throw std::runtime_error("Project migration failed: " +
							migrationError);
				}
			}
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
			if (!NormalizeAndValidateConfig(m_Config, validationError))
				throw std::runtime_error(validationError);
			if (!NormalizeAndValidateBuildSettings(m_BuildSettings, validationError))
				throw std::runtime_error(validationError);
			PlayerSettings normalizedPlayerSettings = m_PlayerSettings;
			if (!NormalizeAndValidatePlayerSettings(normalizedPlayerSettings, validationError))
				throw std::runtime_error(validationError);
			SynchronizeLegacyStartSceneMirror();

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
				uint32_t schemaVersion = 0;
				const YAML::Node projectNode = RequireSupportedProjectDocument(root, schemaVersion);
				(void)ReadProjectConfig(projectNode, schemaVersion);
			}
			else if (!m_PreservedDocument.empty())
			{
				root = YAML::Load(m_PreservedDocument);
				uint32_t schemaVersion = 0;
				const YAML::Node projectNode = RequireSupportedProjectDocument(root, schemaVersion);
				(void)ReadProjectConfig(projectNode, schemaVersion);
			}
			else
			{
				root = YAML::Node(YAML::NodeType::Map);
				root["Project"] = YAML::Node(YAML::NodeType::Map);
			}

			if (!SaveBuildSettings())
				throw std::runtime_error("Could not save ProjectSettings/BuildSettings.json");
			if (!SavePlayerSettings())
				throw std::runtime_error("Could not save ProjectSettings/PlayerSettings.json");

			root["SchemaVersion"] = CurrentSchemaVersion;
			YAML::Node projectNode(YAML::NodeType::Map);
			projectNode["Name"] = m_Config.Name;
			projectNode["Version"] = m_Config.Version;
			projectNode["Description"] = m_Config.Description;
			projectNode["EditorVersion"] = m_Config.EditorVersion;
			projectNode["Template"] = m_Config.Template;
			projectNode["AssetDirectory"] = PathToUTF8(m_Config.AssetDirectory);
			root["Project"] = projectNode;

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
		m_Settings = std::move(reloaded->m_Settings);
		m_PlayerSettings = std::move(reloaded->m_PlayerSettings);
		m_BuildSettings = std::move(reloaded->m_BuildSettings);
		if (!localLastOperationTime.empty())
			m_Config.LastOperationTime = localLastOperationTime;
		m_PreservedDocument = std::move(reloaded->m_PreservedDocument);
		return true;
	}

}
