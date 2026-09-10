#include "tcpch.h"
#include "ProjectManager.h"
#include "TomCat/Utils/FileSystemUtils.h"
#include "TomCat/Utils/PathUtils.h"
#include "TomCat/Utils/PlatformUtils.h"

#include <filesystem>
#include <fstream>
#include <algorithm>
#include <unordered_set>
#include <string_view>
#include <array>
#include <cstdint>
#include <iterator>
#include <yaml-cpp/yaml.h>

namespace TomCat {

	namespace {
		constexpr int s_HubSettingsSchemaVersion = 1;

		std::string EscapeJsonString(std::string_view value)
		{
			static constexpr char hex[] = "0123456789ABCDEF";
			std::string escaped;
			escaped.reserve(value.size() + 2);
			escaped.push_back('"');
			for (const unsigned char character : value)
			{
				switch (character)
				{
					case '"': escaped += "\\\""; break;
					case '\\': escaped += "\\\\"; break;
					case '\b': escaped += "\\b"; break;
					case '\f': escaped += "\\f"; break;
					case '\n': escaped += "\\n"; break;
					case '\r': escaped += "\\r"; break;
					case '\t': escaped += "\\t"; break;
					default:
						if (character < 0x20)
						{
							escaped += "\\u00";
							escaped.push_back(hex[(character >> 4) & 0x0F]);
							escaped.push_back(hex[character & 0x0F]);
						}
						else
						{
							escaped.push_back(static_cast<char>(character));
						}
						break;
				}
			}
			escaped.push_back('"');
			return escaped;
		}

		enum class JsonValueType
		{
			Null,
			Boolean,
			Number,
			String,
			Array,
			Object
		};

		struct JsonValue
		{
			JsonValueType Type = JsonValueType::Null;
			std::string Text;
			std::vector<JsonValue> Array;
			std::unordered_map<std::string, JsonValue> Object;

			const JsonValue* Find(std::string_view key) const
			{
				const auto found = Object.find(std::string(key));
				return found == Object.end() ? nullptr : &found->second;
			}
		};

		class JsonParser
		{
		public:
			explicit JsonParser(std::string_view input)
				: m_Input(input)
			{
			}

			JsonValue Parse()
			{
				SkipWhitespace();
				JsonValue value = ParseValue(0);
				SkipWhitespace();
				if (m_Position != m_Input.size())
					Fail("unexpected trailing data");
				return value;
			}

		private:
			[[noreturn]] void Fail(const char* message) const
			{
				throw std::runtime_error(std::string(message) + " at byte " +
					std::to_string(m_Position));
			}

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

			void ConsumeLiteral(std::string_view literal)
			{
				if (m_Input.substr(m_Position, literal.size()) != literal)
					Fail("invalid literal");
				m_Position += literal.size();
			}

			JsonValue ParseValue(size_t depth)
			{
				if (depth > 64)
					Fail("JSON nesting is too deep");
				if (m_Position >= m_Input.size())
					Fail("expected a JSON value");

				switch (m_Input[m_Position])
				{
					case 'n':
						ConsumeLiteral("null");
						return {};
					case 't':
						ConsumeLiteral("true");
						return { JsonValueType::Boolean, "true" };
					case 'f':
						ConsumeLiteral("false");
						return { JsonValueType::Boolean, "false" };
					case '"':
						return { JsonValueType::String, ParseString() };
					case '[':
						return ParseArray(depth);
					case '{':
						return ParseObject(depth);
					default:
						if (m_Input[m_Position] == '-' ||
							(m_Input[m_Position] >= '0' && m_Input[m_Position] <= '9'))
						{
							return { JsonValueType::Number, ParseNumber() };
						}
						Fail("invalid JSON value");
				}
			}

			JsonValue ParseArray(size_t depth)
			{
				JsonValue value;
				value.Type = JsonValueType::Array;
				++m_Position;
				SkipWhitespace();
				if (Consume(']'))
					return value;

				while (true)
				{
					SkipWhitespace();
					value.Array.emplace_back(ParseValue(depth + 1));
					SkipWhitespace();
					if (Consume(']'))
						return value;
					if (!Consume(','))
						Fail("expected ',' or ']' in array");
				}
			}

			JsonValue ParseObject(size_t depth)
			{
				JsonValue value;
				value.Type = JsonValueType::Object;
				++m_Position;
				SkipWhitespace();
				if (Consume('}'))
					return value;

				while (true)
				{
					SkipWhitespace();
					if (m_Position >= m_Input.size() || m_Input[m_Position] != '"')
						Fail("expected a string key in object");
					std::string key = ParseString();
					SkipWhitespace();
					if (!Consume(':'))
						Fail("expected ':' after object key");
					SkipWhitespace();
					JsonValue member = ParseValue(depth + 1);
					if (!value.Object.emplace(std::move(key), std::move(member)).second)
						Fail("duplicate object key");
					SkipWhitespace();
					if (Consume('}'))
						return value;
					if (!Consume(','))
						Fail("expected ',' or '}' in object");
				}
			}

			static int HexDigit(char character)
			{
				if (character >= '0' && character <= '9')
					return character - '0';
				if (character >= 'a' && character <= 'f')
					return character - 'a' + 10;
				if (character >= 'A' && character <= 'F')
					return character - 'A' + 10;
				return -1;
			}

			uint32_t ParseHexCodeUnit()
			{
				if (m_Position + 4 > m_Input.size())
					Fail("incomplete Unicode escape");
				uint32_t value = 0;
				for (int index = 0; index < 4; ++index)
				{
					const int digit = HexDigit(m_Input[m_Position++]);
					if (digit < 0)
						Fail("invalid Unicode escape");
					value = (value << 4) | static_cast<uint32_t>(digit);
				}
				return value;
			}

			static void AppendUTF8(std::string& output, uint32_t codePoint)
			{
				if (codePoint <= 0x7F)
					output.push_back(static_cast<char>(codePoint));
				else if (codePoint <= 0x7FF)
				{
					output.push_back(static_cast<char>(0xC0 | (codePoint >> 6)));
					output.push_back(static_cast<char>(0x80 | (codePoint & 0x3F)));
				}
				else if (codePoint <= 0xFFFF)
				{
					output.push_back(static_cast<char>(0xE0 | (codePoint >> 12)));
					output.push_back(static_cast<char>(0x80 | ((codePoint >> 6) & 0x3F)));
					output.push_back(static_cast<char>(0x80 | (codePoint & 0x3F)));
				}
				else
				{
					output.push_back(static_cast<char>(0xF0 | (codePoint >> 18)));
					output.push_back(static_cast<char>(0x80 | ((codePoint >> 12) & 0x3F)));
					output.push_back(static_cast<char>(0x80 | ((codePoint >> 6) & 0x3F)));
					output.push_back(static_cast<char>(0x80 | (codePoint & 0x3F)));
				}
			}

			std::string ParseString()
			{
				if (!Consume('"'))
					Fail("expected string");
				std::string value;
				while (m_Position < m_Input.size())
				{
					const unsigned char character =
						static_cast<unsigned char>(m_Input[m_Position++]);
					if (character == '"')
						return value;
					if (character < 0x20)
						Fail("unescaped control character in string");
					if (character != '\\')
					{
						value.push_back(static_cast<char>(character));
						continue;
					}

					if (m_Position >= m_Input.size())
						Fail("incomplete string escape");
					const char escape = m_Input[m_Position++];
					switch (escape)
					{
						case '"': value.push_back('"'); break;
						case '\\': value.push_back('\\'); break;
						case '/': value.push_back('/'); break;
						case 'b': value.push_back('\b'); break;
						case 'f': value.push_back('\f'); break;
						case 'n': value.push_back('\n'); break;
						case 'r': value.push_back('\r'); break;
						case 't': value.push_back('\t'); break;
						case 'u':
						{
							uint32_t codePoint = ParseHexCodeUnit();
							if (codePoint >= 0xD800 && codePoint <= 0xDBFF)
							{
								if (m_Position + 2 > m_Input.size() ||
									m_Input[m_Position] != '\\' || m_Input[m_Position + 1] != 'u')
								{
									Fail("high surrogate is not followed by a low surrogate");
								}
								m_Position += 2;
								const uint32_t lowSurrogate = ParseHexCodeUnit();
								if (lowSurrogate < 0xDC00 || lowSurrogate > 0xDFFF)
									Fail("invalid low surrogate");
								codePoint = 0x10000 +
									((codePoint - 0xD800) << 10) + (lowSurrogate - 0xDC00);
							}
							else if (codePoint >= 0xDC00 && codePoint <= 0xDFFF)
							{
								Fail("unexpected low surrogate");
							}
							AppendUTF8(value, codePoint);
							break;
						}
						default: Fail("invalid string escape");
					}
				}
				Fail("unterminated string");
			}

			std::string ParseNumber()
			{
				const size_t start = m_Position;
				Consume('-');
				if (m_Position >= m_Input.size())
					Fail("incomplete number");
				if (m_Input[m_Position] == '0')
				{
					++m_Position;
					if (m_Position < m_Input.size() &&
						m_Input[m_Position] >= '0' && m_Input[m_Position] <= '9')
					{
						Fail("leading zero in number");
					}
				}
				else
				{
					if (m_Input[m_Position] < '1' || m_Input[m_Position] > '9')
						Fail("invalid number");
					while (m_Position < m_Input.size() &&
						m_Input[m_Position] >= '0' && m_Input[m_Position] <= '9')
					{
						++m_Position;
					}
				}

				if (m_Position < m_Input.size() && m_Input[m_Position] == '.')
				{
					++m_Position;
					const size_t fractionStart = m_Position;
					while (m_Position < m_Input.size() &&
						m_Input[m_Position] >= '0' && m_Input[m_Position] <= '9')
					{
						++m_Position;
					}
					if (fractionStart == m_Position)
						Fail("fraction requires at least one digit");
				}

				if (m_Position < m_Input.size() &&
					(m_Input[m_Position] == 'e' || m_Input[m_Position] == 'E'))
				{
					++m_Position;
					if (m_Position < m_Input.size() &&
						(m_Input[m_Position] == '+' || m_Input[m_Position] == '-'))
					{
						++m_Position;
					}
					const size_t exponentStart = m_Position;
					while (m_Position < m_Input.size() &&
						m_Input[m_Position] >= '0' && m_Input[m_Position] <= '9')
					{
						++m_Position;
					}
					if (exponentStart == m_Position)
						Fail("exponent requires at least one digit");
				}
				return std::string(m_Input.substr(start, m_Position - start));
			}

		private:
			std::string_view m_Input;
			size_t m_Position = 0;
		};

		bool PrepareManagedDirectory(const std::filesystem::path& requestedDirectory,
			std::filesystem::path& preparedDirectory, const char* description)
		{
			if (requestedDirectory.empty())
			{
				TC_Core_Error("{0} directory cannot be empty", description);
				return false;
			}

			std::error_code error;
			preparedDirectory = std::filesystem::absolute(requestedDirectory, error);
			if (error)
			{
				TC_Core_Error("Could not resolve {0} directory '{1}': {2}", description,
					PathToUTF8(requestedDirectory), error.message());
				return false;
			}
			preparedDirectory = preparedDirectory.lexically_normal();

			const bool exists = std::filesystem::exists(preparedDirectory, error);
			if (error)
			{
				TC_Core_Error("Could not inspect {0} directory '{1}': {2}", description,
					PathToUTF8(preparedDirectory), error.message());
				return false;
			}
			if (!exists)
			{
				std::filesystem::create_directories(preparedDirectory, error);
				if (error)
				{
					TC_Core_Error("Could not create {0} directory '{1}': {2}", description,
						PathToUTF8(preparedDirectory), error.message());
					return false;
				}
			}

			error.clear();
			if (!std::filesystem::is_directory(preparedDirectory, error) || error)
			{
				TC_Core_Error("{0} path is not an accessible directory: {1}{2}", description,
					PathToUTF8(preparedDirectory), error ? " (" + error.message() + ")" : std::string{});
				return false;
			}

			// Constructing an iterator is the first operation that exposes directory
			// ACL/share failures. Probe it before publishing the new setting.
			std::filesystem::directory_iterator probe(preparedDirectory, error);
			if (error)
			{
				TC_Core_Error("Could not enumerate {0} directory '{1}': {2}", description,
					PathToUTF8(preparedDirectory), error.message());
				return false;
			}
			(void)probe;
			return true;
		}

		std::string ProjectPathKey(const std::filesystem::path& path)
		{
			std::error_code error;
			std::filesystem::path absolutePath = std::filesystem::absolute(path, error);
			if (error)
				absolutePath = path;
			absolutePath = absolutePath.lexically_normal();
#ifdef TC_PLATFORM_WINDOWS
			std::wstring folded = absolutePath.wstring();
			if (!folded.empty())
			{
				const int required = LCMapStringEx(LOCALE_NAME_INVARIANT, LCMAP_LOWERCASE,
					folded.data(), static_cast<int>(folded.size()), nullptr, 0, nullptr, nullptr, 0);
				if (required > 0)
				{
					std::wstring output(static_cast<size_t>(required), L'\0');
					if (LCMapStringEx(LOCALE_NAME_INVARIANT, LCMAP_LOWERCASE,
						folded.data(), static_cast<int>(folded.size()), output.data(), required,
						nullptr, nullptr, 0) > 0)
						folded = std::move(output);
				}
			}
			return PathToUTF8(std::filesystem::path(folded).lexically_normal());
#else
			return PathToUTF8(absolutePath);
#endif
		}

#ifdef _WIN32
		std::wstring ExtendedLengthPath(const std::filesystem::path& path)
		{
			std::error_code error;
			std::filesystem::path absolutePath = std::filesystem::absolute(path, error);
			std::wstring value = (error ? path : absolutePath).lexically_normal().wstring();
			if (value.rfind(L"\\\\?\\", 0) == 0)
				return value;
			if (value.rfind(L"\\\\", 0) == 0)
				return L"\\\\?\\UNC\\" + value.substr(2);
			return L"\\\\?\\" + value;
		}

		std::wstring QuoteWindowsArgument(const std::wstring& value)
		{
			std::wstring result = L"\"";
			size_t backslashes = 0;
			for (wchar_t character : value)
			{
				if (character == L'\\')
				{
					++backslashes;
					continue;
				}
				if (character == L'\"')
				{
					result.append(backslashes * 2 + 1, L'\\');
					result.push_back(character);
					backslashes = 0;
					continue;
				}
				result.append(backslashes, L'\\');
				backslashes = 0;
				result.push_back(character);
			}
			result.append(backslashes * 2, L'\\');
			result.push_back(L'\"');
			return result;
		}

#endif

	}

	ProjectManager& ProjectManager::Get()
	{
		static ProjectManager instance;
		return instance;
	}

	ProjectManager::ProjectManager()
	{
		LoadHubSettings();
	}

	bool ProjectManager::SetProjectDirectory(const std::filesystem::path& directory)
	{
		std::filesystem::path preparedDirectory;
		if (!PrepareManagedDirectory(directory, preparedDirectory, "Project"))
			return false;

		const std::filesystem::path previousDirectory = m_ProjectDirectory;
		const std::vector<Ref<Project>> previousProjects = m_Projects;
		const std::unordered_map<std::string, std::string> previousLastOpenedTimes = m_ProjectLastOpenedTimes;
		m_ProjectDirectory = std::move(preparedDirectory);
		if (!ScanProjectsInternal(false) || !SaveHubSettings())
		{
			m_ProjectDirectory = previousDirectory;
			m_Projects = previousProjects;
			m_ProjectLastOpenedTimes = previousLastOpenedTimes;
			return false;
		}
		return true;
	}

	bool ProjectManager::SetEditorDirectory(const std::filesystem::path& directory)
	{
		std::filesystem::path preparedDirectory;
		if (!PrepareManagedDirectory(directory, preparedDirectory, "Editor"))
			return false;
		const std::filesystem::path previousDirectory = m_EditorDirectory;
		m_EditorDirectory = std::move(preparedDirectory);
		if (!SaveHubSettings())
		{
			m_EditorDirectory = previousDirectory;
			return false;
		}
		return true;
	}

	std::optional<std::vector<std::string>> ProjectManager::GetEditorDirectoryFiles() const
	{
		std::vector<std::string> fileNames;
		std::error_code error;
		if (!std::filesystem::is_directory(m_EditorDirectory, error) || error)
		{
			TC_Core_Error("Editor directory is not accessible: {0}{1}", PathToUTF8(m_EditorDirectory),
				error ? " (" + error.message() + ")" : std::string{});
			return std::nullopt;
		}

		std::filesystem::directory_iterator iterator(m_EditorDirectory, error), end;
		if (error)
		{
			TC_Core_Error("Could not enumerate Editor directory '{0}': {1}",
				PathToUTF8(m_EditorDirectory), error.message());
			return std::nullopt;
		}
		for (; iterator != end; iterator.increment(error))
		{
			std::error_code entryError;
			if (iterator->is_directory(entryError) && !entryError)
				fileNames.push_back(PathToUTF8(iterator->path().filename()));
			else if (entryError)
			{
				TC_Core_Error("Could not inspect Editor directory entry '{0}': {1}",
					PathToUTF8(iterator->path()), entryError.message());
				return std::nullopt;
			}
		}
		if (error)
		{
			TC_Core_Error("Failed while enumerating Editor directory '{0}': {1}",
				PathToUTF8(m_EditorDirectory), error.message());
			return std::nullopt;
		}

		std::sort(fileNames.begin(), fileNames.end());
		return fileNames;
	}

	bool ProjectManager::ScanProjects()
	{
		return ScanProjectsInternal(true);
	}

	bool ProjectManager::ScanProjectsInternal(bool persistMigratedState)
	{
		std::unordered_map<std::string, std::string> stagedLastOpenedTimes = m_ProjectLastOpenedTimes;
		std::vector<Ref<Project>> scannedProjects;
		std::unordered_set<std::string> seen;

		auto addProjectFile = [&](const std::filesystem::path& file) -> bool
		{
			std::error_code error;
			const bool regularFile = std::filesystem::is_regular_file(file, error);
			if (error)
			{
				if (error == std::errc::no_such_file_or_directory)
					return true;
				TC_Core_Error("Could not inspect project file '{0}': {1}",
					PathToUTF8(file), error.message());
				return false;
			}
			if (!regularFile)
				return true;

			const std::string key = ProjectPathKey(file);
			if (m_IgnoredProjectPaths.find(key) != m_IgnoredProjectPaths.end()
				|| seen.find(key) != seen.end())
				return true;

			Ref<Project> project;
			try
			{
				project = Project::Load(file);
			}
			catch (const std::exception& exception)
			{
				TC_Core_Error("Could not load project '{0}': {1}", PathToUTF8(file), exception.what());
				return true;
			}
			if (project)
			{
				ApplyStoredLastOpenedTime(project, stagedLastOpenedTimes);
				seen.insert(key);
				scannedProjects.push_back(project);
			}
			return true;
		};

		// 1. Scan the default project directory (one level deep). Build a new
		// result off to the side so a transient I/O error cannot wipe the Hub list.
		if (!m_ProjectDirectory.empty())
		{
			std::error_code error;
			const bool directoryExists = std::filesystem::is_directory(m_ProjectDirectory, error);
			if (error)
			{
				TC_Core_Error("Could not inspect Project directory '{0}': {1}",
					PathToUTF8(m_ProjectDirectory), error.message());
				return false;
			}
			if (directoryExists)
			{
				std::filesystem::directory_iterator projects(m_ProjectDirectory, error), end;
				if (error)
				{
					TC_Core_Error("Could not enumerate Project directory '{0}': {1}",
						PathToUTF8(m_ProjectDirectory), error.message());
					return false;
				}
				for (; projects != end; projects.increment(error))
				{
					std::error_code entryError;
					if (!projects->is_directory(entryError))
					{
						if (entryError)
						{
							TC_Core_Error("Could not inspect Project directory entry '{0}': {1}",
								PathToUTF8(projects->path()), entryError.message());
							return false;
						}
						continue;
					}

					std::filesystem::directory_iterator files(projects->path(), entryError), fileEnd;
					if (entryError)
					{
						TC_Core_Error("Could not enumerate project folder '{0}': {1}",
							PathToUTF8(projects->path()), entryError.message());
						return false;
					}
					for (; files != fileEnd; files.increment(entryError))
					{
						if (IsProjectFile(files->path()))
						{
							if (!addProjectFile(files->path()))
								return false;
							break;
						}
					}
					if (entryError)
					{
						TC_Core_Error("Failed while enumerating project folder '{0}': {1}",
							PathToUTF8(projects->path()), entryError.message());
						return false;
					}
				}
				if (error)
				{
					TC_Core_Error("Failed while enumerating Project directory '{0}': {1}",
						PathToUTF8(m_ProjectDirectory), error.message());
					return false;
				}
			}
		}

		// 2. Add projects serialized locally (added from any other path).
		for (const auto& path : m_KnownProjectPaths)
		{
			if (!addProjectFile(path))
				return false;
		}

		std::sort(scannedProjects.begin(), scannedProjects.end(),
			[](const Ref<Project>& a, const Ref<Project>& b) {
				return a->GetLastOperationTime() > b->GetLastOperationTime();
			});
		const bool migratedRecency = stagedLastOpenedTimes.size() != m_ProjectLastOpenedTimes.size();
		if (migratedRecency)
		{
			const std::unordered_map<std::string, std::string> previousLastOpenedTimes = m_ProjectLastOpenedTimes;
			m_ProjectLastOpenedTimes = std::move(stagedLastOpenedTimes);
			if (persistMigratedState && !SaveHubSettings())
			{
				m_ProjectLastOpenedTimes = previousLastOpenedTimes;
				return false;
			}
		}
		m_Projects = std::move(scannedProjects);
		return true;
	}

	Ref<Project> ProjectManager::CreateProject(const std::filesystem::path& projectPath, const ProjectConfig& config)
	{
		auto project = Project::CreateNew(projectPath, config);
		if (project)
		{
			const std::vector<std::filesystem::path> previousKnownProjectPaths = m_KnownProjectPaths;
			const std::unordered_set<std::string> previousIgnoredProjectPaths = m_IgnoredProjectPaths;
			m_IgnoredProjectPaths.erase(ProjectPathKey(project->GetProjectPath()));

			// Remember the project even when it lives outside any mount, so it
			// stays in the list after a restart.
			const std::string projectKey = ProjectPathKey(project->GetProjectPath());
			bool found = false;
			for (const auto& known : m_KnownProjectPaths)
			{
				if (ProjectPathKey(known) == projectKey)
				{
					found = true;
					break;
				}
			}
			if (!found)
				m_KnownProjectPaths.push_back(projectPath);

			if (!RecordProjectOpened(project))
			{
				m_KnownProjectPaths = previousKnownProjectPaths;
				m_IgnoredProjectPaths = previousIgnoredProjectPaths;
				TC_Core_Error("Project '{0}' was created on disk, but could not be added to the Hub",
					PathToUTF8(project->GetProjectPath()));
				return nullptr;
			}

			m_Projects.push_back(project);
		}
		return project;
	}

	Ref<Project> ProjectManager::LoadProject(const std::filesystem::path& projectPath)
	{
		auto project = Project::Load(projectPath);
		if (project)
		{
			const std::unordered_set<std::string> previousIgnoredProjectPaths = m_IgnoredProjectPaths;
			m_IgnoredProjectPaths.erase(ProjectPathKey(project->GetProjectPath()));
			if (!RecordProjectOpened(project))
			{
				m_IgnoredProjectPaths = previousIgnoredProjectPaths;
				TC_Core_Warn("Project loaded, but its Hub recency metadata could not be saved");
			}
			m_ActiveProject = project;
		}
		return project;
	}

	Ref<Project> ProjectManager::AddProject(const std::filesystem::path& projectPath)
	{
		auto project = Project::Load(projectPath);
		if (project)
		{
			const std::vector<std::filesystem::path> previousKnownProjectPaths = m_KnownProjectPaths;
			const std::unordered_set<std::string> previousIgnoredProjectPaths = m_IgnoredProjectPaths;
			const std::unordered_map<std::string, std::string> previousLastOpenedTimes = m_ProjectLastOpenedTimes;
			const std::vector<Ref<Project>> previousProjects = m_Projects;
			const bool removedFromIgnored = m_IgnoredProjectPaths.erase(
				ProjectPathKey(project->GetProjectPath())) != 0;
			const std::string projectKey = ProjectPathKey(project->GetProjectPath());
			bool found = false;
			for (const auto& known : m_KnownProjectPaths)
			{
				if (ProjectPathKey(known) == projectKey)
				{
					found = true;
					break;
				}
			}
			if (!found)
				m_KnownProjectPaths.push_back(projectPath);

			if (!ScanProjectsInternal(false))
			{
				m_KnownProjectPaths = previousKnownProjectPaths;
				m_IgnoredProjectPaths = previousIgnoredProjectPaths;
				m_ProjectLastOpenedTimes = previousLastOpenedTimes;
				m_Projects = previousProjects;
				return nullptr;
			}

			const bool migratedRecency = m_ProjectLastOpenedTimes.size() != previousLastOpenedTimes.size();
			if ((!found || removedFromIgnored || migratedRecency) && !SaveHubSettings())
			{
				m_KnownProjectPaths = previousKnownProjectPaths;
				m_IgnoredProjectPaths = previousIgnoredProjectPaths;
				m_ProjectLastOpenedTimes = previousLastOpenedTimes;
				m_Projects = previousProjects;
				return nullptr;
			}
			ApplyStoredLastOpenedTime(project, m_ProjectLastOpenedTimes);
		}
		return project;
	}

	bool ProjectManager::RemoveProject(const std::filesystem::path& projectPath)
	{
		const std::string projectKey = ProjectPathKey(projectPath);
		auto it = std::find_if(m_Projects.begin(), m_Projects.end(),
			[&projectKey](const Ref<Project>& p) {
				return ProjectPathKey(p->GetProjectPath()) == projectKey;
			});

		if (it != m_Projects.end())
		{
			const std::vector<Ref<Project>> previousProjects = m_Projects;
			const Ref<Project> previousActiveProject = m_ActiveProject;
			const std::vector<std::filesystem::path> previousKnownProjectPaths = m_KnownProjectPaths;
			const std::unordered_map<std::string, std::string> previousLastOpenedTimes = m_ProjectLastOpenedTimes;
			const std::unordered_set<std::string> previousIgnoredProjectPaths = m_IgnoredProjectPaths;

			if (m_ActiveProject && ProjectPathKey(m_ActiveProject->GetProjectPath()) == projectKey)
				m_ActiveProject = nullptr;

			m_Projects.erase(it);

			// Also forget the project in the persisted known list.
			m_KnownProjectPaths.erase(
				std::remove_if(m_KnownProjectPaths.begin(), m_KnownProjectPaths.end(),
					[&projectKey](const std::filesystem::path& known)
					{
						return ProjectPathKey(known) == projectKey;
					}),
				m_KnownProjectPaths.end());
			m_ProjectLastOpenedTimes.erase(projectKey);
			m_IgnoredProjectPaths.insert(projectKey);
			if (!SaveHubSettings())
			{
				m_Projects = previousProjects;
				m_ActiveProject = previousActiveProject;
				m_KnownProjectPaths = previousKnownProjectPaths;
				m_ProjectLastOpenedTimes = previousLastOpenedTimes;
				m_IgnoredProjectPaths = previousIgnoredProjectPaths;
				return false;
			}
			return true;
		}
		return false;
	}

	void ProjectManager::ApplyStoredLastOpenedTime(const Ref<Project>& project,
		std::unordered_map<std::string, std::string>& lastOpenedTimes) const
	{
		if (!project)
			return;

		const std::string key = ProjectPathKey(project->GetProjectPath());
		auto stored = lastOpenedTimes.find(key);
		if (stored != lastOpenedTimes.end())
		{
			project->m_Config.LastOperationTime = stored->second;
		}
		else if (!project->m_Config.LastOperationTime.empty())
		{
			// One-way migration from the schema-v1 project field. It is removed the
			// next time the project is explicitly saved.
			lastOpenedTimes.emplace(key, project->m_Config.LastOperationTime);
		}
	}

	bool ProjectManager::RecordProjectOpened(const Ref<Project>& project)
	{
		if (!project)
			return false;

		const std::string key = ProjectPathKey(project->GetProjectPath());
		const std::string previousProjectTimestamp = project->m_Config.LastOperationTime;
		const auto previousStoredTimestamp = m_ProjectLastOpenedTimes.find(key);
		const bool hadStoredTimestamp = previousStoredTimestamp != m_ProjectLastOpenedTimes.end();
		const std::string storedTimestamp = hadStoredTimestamp ? previousStoredTimestamp->second : std::string{};
		project->Touch();
		m_ProjectLastOpenedTimes[key] = project->GetLastOperationTime();
		if (SaveHubSettings())
			return true;

		project->m_Config.LastOperationTime = previousProjectTimestamp;
		if (hadStoredTimestamp)
			m_ProjectLastOpenedTimes[key] = storedTimestamp;
		else
			m_ProjectLastOpenedTimes.erase(key);
		return false;
	}

	void ProjectManager::OpenProjectInEditor(Ref<Project> project)
	{
		if (!project)
			return;

		std::filesystem::path editorPath = ProjectManager::Get().GetEditorDirectory() / project->GetEditorVersion() / "TomCat.exe";

		TC_Core_Info("Looking for editor at: {0}", PathToUTF8(editorPath));

		std::error_code pathError;
		if (!std::filesystem::is_regular_file(editorPath, pathError) || pathError)
		{
			std::array<wchar_t, 32768> modulePath{};
			const DWORD length = GetModuleFileNameW(nullptr, modulePath.data(), static_cast<DWORD>(modulePath.size()));
			if (length == 0 || length >= modulePath.size())
			{
				TC_Core_Error("Could not resolve the Hub executable directory. Error code: {0}", GetLastError());
				return;
			}
			editorPath = std::filesystem::path(std::wstring(modulePath.data(), length)).parent_path() / "TomCat.exe";
			TC_Core_Info("Fallback to: {0}", PathToUTF8(editorPath));
		}

		pathError.clear();
		if (!std::filesystem::is_regular_file(editorPath, pathError) || pathError)
		{
			TC_Core_Error("Editor executable was not found: {0}", PathToUTF8(editorPath));
			return;
		}

		const std::wstring applicationPath = ExtendedLengthPath(editorPath);
		const std::wstring projectPath = ExtendedLengthPath(project->GetProjectPath());
		std::wstring command = QuoteWindowsArgument(applicationPath) + L" " + QuoteWindowsArgument(projectPath);
		STARTUPINFOW si{};
		si.cb = sizeof(si);
		PROCESS_INFORMATION pi = {};
		const std::wstring workingDirectory = ExtendedLengthPath(editorPath.parent_path());

		if (CreateProcessW(applicationPath.c_str(), command.data(), nullptr, nullptr,
			FALSE, 0, nullptr, workingDirectory.c_str(), &si, &pi))
		{
			TC_Core_Info("Editor launched successfully");
			CloseHandle(pi.hProcess);
			CloseHandle(pi.hThread);
			if (!RecordProjectOpened(project))
				TC_Core_Warn("Editor launched, but the project's Hub recency metadata could not be saved");
		}
		else
		{
			TC_Core_Error("Failed to open editor. Error code: {0}", GetLastError());
		}
	}

	bool ProjectManager::IsProjectFile(const std::filesystem::path& path) const
	{
		return path.extension() == ".tcproj" && path.filename() == "Project.tcproj";
	}


	std::optional<std::filesystem::path> ProjectManager::GetHubSettingsPath() const
	{
		const std::optional<std::filesystem::path> settingsRoot = GetTomCatSettingsRoot();
		if (!settingsRoot)
			return std::nullopt;
		return *settingsRoot / "hub.json";
	}

	void ProjectManager::LoadHubSettings()
	{
		m_ProjectDirectory.clear();
		m_EditorDirectory.clear();
		m_KnownProjectPaths.clear();
		m_ProjectLastOpenedTimes.clear();
		m_IgnoredProjectPaths.clear();
		m_HubSettingsWriteBlocked = false;

		const std::optional<std::filesystem::path> settingsPathResult = GetHubSettingsPath();
		if (!settingsPathResult)
			return;
		const std::filesystem::path& settingsPath = *settingsPathResult;

		std::error_code settingsError;
		const bool settingsExist = std::filesystem::exists(settingsPath, settingsError);
		if (settingsError)
		{
			m_HubSettingsWriteBlocked = true;
			TC_Core_Error("Could not inspect Hub settings '{0}': {1}",
				PathToUTF8(settingsPath), settingsError.message());
			return;
		}

		if (settingsExist)
		{
			try
			{
				std::error_code sizeError;
				const std::uintmax_t settingsSize = std::filesystem::file_size(settingsPath, sizeError);
				if (sizeError)
					throw std::runtime_error("could not inspect the file size: " + sizeError.message());
				if (settingsSize > 16 * 1024 * 1024)
					throw std::runtime_error("the file exceeds the 16 MiB safety limit");

				std::ifstream input(settingsPath, std::ios::binary);
				if (!input)
					throw std::runtime_error("could not open the file");

				const std::string contents{
					std::istreambuf_iterator<char>{ input }, std::istreambuf_iterator<char>{} };
				if (input.bad())
					throw std::runtime_error("failed while reading the file");
				const JsonValue root = JsonParser{ contents }.Parse();
				if (root.Type != JsonValueType::Object)
					throw std::runtime_error("the JSON root must be an object");

				const JsonValue* schemaVersion = root.Find("schemaVersion");
				if (!schemaVersion || schemaVersion->Type != JsonValueType::Number)
					throw std::runtime_error("schemaVersion is missing or invalid");
				if (schemaVersion->Text != std::to_string(s_HubSettingsSchemaVersion))
					throw std::runtime_error("unsupported schemaVersion " + schemaVersion->Text);

				std::filesystem::path projectDirectory;
				std::filesystem::path editorDirectory;
				std::vector<std::filesystem::path> knownProjectPaths;
				std::unordered_set<std::string> ignoredProjectPaths;
				std::unordered_map<std::string, std::string> projectLastOpenedTimes;

				auto readOptionalPath = [&root](const char* name) -> std::filesystem::path
				{
					const JsonValue* value = root.Find(name);
					if (!value)
						return {};
					if (value->Type != JsonValueType::String)
						throw std::runtime_error(std::string(name) + " must be a string");
					return UTF8ToPath(value->Text);
				};
				projectDirectory = readOptionalPath("projectDirectory");
				editorDirectory = readOptionalPath("editorDirectory");

				const JsonValue* knownProjects = root.Find("knownProjects");
				if (knownProjects)
				{
					if (knownProjects->Type != JsonValueType::Array)
						throw std::runtime_error("knownProjects must be an array");
					for (const JsonValue& project : knownProjects->Array)
					{
						if (project.Type != JsonValueType::String)
							throw std::runtime_error("knownProjects entries must be strings");
						knownProjectPaths.emplace_back(UTF8ToPath(project.Text));
					}
				}

				const JsonValue* ignoredProjects = root.Find("ignoredProjects");
				if (ignoredProjects)
				{
					if (ignoredProjects->Type != JsonValueType::Array)
						throw std::runtime_error("ignoredProjects must be an array");
					for (const JsonValue& project : ignoredProjects->Array)
					{
						if (project.Type != JsonValueType::String)
							throw std::runtime_error("ignoredProjects entries must be strings");
						const std::string& path = project.Text;
						if (!path.empty())
							ignoredProjectPaths.insert(ProjectPathKey(UTF8ToPath(path)));
					}
				}

				const JsonValue* lastOpened = root.Find("projectLastOpened");
				if (lastOpened)
				{
					if (lastOpened->Type != JsonValueType::Array)
						throw std::runtime_error("projectLastOpened must be an array");
					for (const JsonValue& entry : lastOpened->Array)
					{
						if (entry.Type != JsonValueType::Object)
							throw std::runtime_error("projectLastOpened entries must be objects");
						const JsonValue* projectPath = entry.Find("projectPath");
						const JsonValue* timestamp = entry.Find("lastOpened");
						if (!projectPath || projectPath->Type != JsonValueType::String ||
							!timestamp || timestamp->Type != JsonValueType::String)
						{
							throw std::runtime_error(
								"projectLastOpened entries require string projectPath and lastOpened fields");
						}
						const std::string& encodedPath = projectPath->Text;
						const std::string& encodedTimestamp = timestamp->Text;
						if (!encodedPath.empty() && !encodedTimestamp.empty())
						{
							projectLastOpenedTimes[ProjectPathKey(UTF8ToPath(encodedPath))] =
								encodedTimestamp;
						}
					}
				}

				m_ProjectDirectory = std::move(projectDirectory);
				m_EditorDirectory = std::move(editorDirectory);
				m_KnownProjectPaths = std::move(knownProjectPaths);
				m_IgnoredProjectPaths = std::move(ignoredProjectPaths);
				m_ProjectLastOpenedTimes = std::move(projectLastOpenedTimes);
			}
			catch (const std::exception& error)
			{
				// Never silently replace a malformed or unreadable settings file. The
				// user can repair/delete it, while this process continues with defaults.
				m_HubSettingsWriteBlocked = true;
				TC_Core_Error("Failed to load Hub settings '{0}': {1}. Writes are disabled to preserve the file",
					PathToUTF8(settingsPath), error.what());
			}
			return;
		}

		try
		{
			auto getProgramRoot = []()
			{
				std::array<wchar_t, 32768> modulePath{};
				const DWORD length = GetModuleFileNameW(nullptr, modulePath.data(),
					static_cast<DWORD>(modulePath.size()));
				if (length == 0 || length >= modulePath.size())
				{
					throw std::runtime_error("Could not resolve the program directory. Windows error "
						+ std::to_string(GetLastError()));
				}
				return std::filesystem::path(std::wstring(modulePath.data(), length)).parent_path();
			};

			auto loadIniSection = [this](const std::filesystem::path& path) -> bool
			{
				std::error_code pathError;
				const bool exists = std::filesystem::exists(path, pathError);
				if (pathError)
					throw std::runtime_error("Could not inspect legacy Hub settings '" + PathToUTF8(path)
						+ "': " + pathError.message());
				if (!exists)
					return false;

				std::ifstream input(path, std::ios::binary);
				if (!input)
					throw std::runtime_error("Could not open legacy Hub settings '" + PathToUTF8(path) + "'");

				bool sawSection = false;
				bool inSection = false;
				std::string line;
				while (std::getline(input, line))
				{
					if (!line.empty() && line.back() == '\r')
						line.pop_back();
					if (line == "[HubConfig]")
					{
						inSection = true;
						sawSection = true;
						continue;
					}
					if (!inSection)
						continue;
					if (line.empty() || line[0] == '[')
						break;

					if (line.rfind("ProjectDirectory=", 0) == 0)
						m_ProjectDirectory = UTF8ToPath(line.substr(17));
					else if (line.rfind("EditorDirectory=", 0) == 0)
						m_EditorDirectory = UTF8ToPath(line.substr(16));
					else if (line.rfind("KnownProjects=", 0) == 0)
						m_KnownProjectPaths.emplace_back(UTF8ToPath(line.substr(14)));
					else if (line.rfind("IgnoredProjects=", 0) == 0)
					{
						const std::string encodedPath = line.substr(16);
						if (!encodedPath.empty())
							m_IgnoredProjectPaths.insert(ProjectPathKey(UTF8ToPath(encodedPath)));
					}
					else if (line.rfind("ProjectLastOpened=", 0) == 0)
					{
						const std::string value = line.substr(18);
						const size_t separator = value.find('|');
						if (separator != std::string::npos && separator > 0 && separator + 1 < value.size())
						{
							const std::string timestamp = value.substr(0, separator);
							const std::filesystem::path projectPath = UTF8ToPath(value.substr(separator + 1));
							m_ProjectLastOpenedTimes[ProjectPathKey(projectPath)] = timestamp;
						}
					}
				}
				if (input.bad())
					throw std::runtime_error("Failed while reading legacy Hub settings '" + PathToUTF8(path) + "'");
				return sawSection;
			};

			const std::filesystem::path legacyLocalRoot =
				settingsPath.parent_path().parent_path() / "UserSettings";
			bool migrated = loadIniSection(legacyLocalRoot / "Hub" / "imgui.ini");
			if (!migrated)
				migrated = loadIniSection(legacyLocalRoot / "Manager" / "imgui.ini");
			if (!migrated)
				migrated = loadIniSection(
					getProgramRoot() / "UserSettings" / "Manager" / "imgui.ini");
			if (!migrated)
				migrated = loadIniSection(getProgramRoot() / "imgui.ini");

			// HubConfig.tomcat predates the INI settings and is the final read-only
			// migration source.
			if (!migrated)
			{
				const std::filesystem::path legacyPath = getProgramRoot() / "HubConfig.tomcat";
				std::error_code legacyError;
				const bool legacyExists = std::filesystem::exists(legacyPath, legacyError);
				if (legacyError)
					throw std::runtime_error("Could not inspect legacy Hub settings: " + legacyError.message());
				if (legacyExists)
				{
					std::ifstream input(legacyPath, std::ios::binary);
					if (!input)
						throw std::runtime_error("Could not open legacy Hub settings");
					const YAML::Node data = YAML::Load(input);
					if (input.bad())
						throw std::runtime_error("Failed while reading legacy Hub settings");
					const YAML::Node config = data["HubConfig"];
					if (config)
					{
						m_ProjectDirectory = config["ProjectDirectory"]
							? UTF8ToPath(config["ProjectDirectory"].as<std::string>()) : std::filesystem::path{};
						m_EditorDirectory = config["EditorDirectory"]
							? UTF8ToPath(config["EditorDirectory"].as<std::string>()) : std::filesystem::path{};
						if (config["KnownProjects"])
						{
							for (const auto& node : config["KnownProjects"])
								m_KnownProjectPaths.emplace_back(UTF8ToPath(node.as<std::string>()));
						}
						migrated = true;
					}
				}
			}

			if (migrated && !SaveHubSettings())
				TC_Core_Warn("Legacy Hub settings were loaded but could not be migrated to hub.json");
		}
		catch (const std::exception& error)
		{
			m_ProjectDirectory.clear();
			m_EditorDirectory.clear();
			m_KnownProjectPaths.clear();
			m_ProjectLastOpenedTimes.clear();
			m_IgnoredProjectPaths.clear();
			TC_Core_Error("Failed to migrate legacy Hub settings: {0}", error.what());
		}
	}

	bool ProjectManager::SaveHubSettings()
	{
		try
		{
			if (m_HubSettingsWriteBlocked)
				throw std::runtime_error(
					"writes are disabled because the existing hub.json could not be loaded");

			const std::optional<std::filesystem::path> settingsPathResult = GetHubSettingsPath();
			if (!settingsPathResult)
				return false;
			const std::filesystem::path& settingsPath = *settingsPathResult;

			std::error_code directoryError;
			std::filesystem::create_directories(settingsPath.parent_path(), directoryError);
			if (directoryError)
				throw std::runtime_error("Could not create Hub settings directory '"
					+ PathToUTF8(settingsPath.parent_path()) + "': " + directoryError.message());
			directoryError.clear();
			if (!std::filesystem::is_directory(settingsPath.parent_path(), directoryError) || directoryError)
				throw std::runtime_error("Hub settings parent path is not an accessible directory: "
					+ PathToUTF8(settingsPath.parent_path()));

			std::vector<std::string> ignoredProjects(
				m_IgnoredProjectPaths.begin(), m_IgnoredProjectPaths.end());
			std::sort(ignoredProjects.begin(), ignoredProjects.end());
			std::vector<std::pair<std::string, std::string>> lastOpened(
				m_ProjectLastOpenedTimes.begin(), m_ProjectLastOpenedTimes.end());
			std::sort(lastOpened.begin(), lastOpened.end(),
				[](const auto& left, const auto& right) { return left.first < right.first; });

			std::string json;
			json += "{\n";
			json += "  \"schemaVersion\": " + std::to_string(s_HubSettingsSchemaVersion) + ",\n";
			json += "  \"projectDirectory\": " + EscapeJsonString(PathToUTF8(m_ProjectDirectory)) + ",\n";
			json += "  \"editorDirectory\": " + EscapeJsonString(PathToUTF8(m_EditorDirectory)) + ",\n";
			json += "  \"knownProjects\": [\n";
			for (size_t index = 0; index < m_KnownProjectPaths.size(); ++index)
			{
				json += "    " + EscapeJsonString(PathToUTF8(m_KnownProjectPaths[index]));
				json += index + 1 < m_KnownProjectPaths.size() ? ",\n" : "\n";
			}
			json += "  ],\n";
			json += "  \"ignoredProjects\": [\n";
			for (size_t index = 0; index < ignoredProjects.size(); ++index)
			{
				json += "    " + EscapeJsonString(ignoredProjects[index]);
				json += index + 1 < ignoredProjects.size() ? ",\n" : "\n";
			}
			json += "  ],\n";
			json += "  \"projectLastOpened\": [\n";
			for (size_t index = 0; index < lastOpened.size(); ++index)
			{
				json += "    { \"projectPath\": " + EscapeJsonString(lastOpened[index].first)
					+ ", \"lastOpened\": " + EscapeJsonString(lastOpened[index].second) + " }";
				json += index + 1 < lastOpened.size() ? ",\n" : "\n";
			}
			json += "  ]\n";
			json += "}\n";

			std::string writeError;
			if (!FileSystem::WriteFileAtomically(settingsPath, json, writeError))
				throw std::runtime_error("Could not atomically replace Hub settings: " + writeError);
			return true;
		}
		catch (const std::exception& e)
		{
			TC_Core_Error("Failed to save Hub settings: {0}", e.what());
			return false;
		}
	}

}
