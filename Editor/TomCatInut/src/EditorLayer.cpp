#include "EditorLayer.h"
#include <imgui/imgui.h>

#include <algorithm>
#include <charconv>
#include <cctype>
#include <cmath>
#include <fstream>
#include <iomanip>
#include <iterator>
#include <limits>
#include <sstream>
#include <string_view>
#include <system_error>
#include <vector>

#ifdef TC_PLATFORM_WINDOWS
	#include <Windows.h>
#endif

#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>
#include "TomCat/Scene/SceneSerializer.h"
#include "TomCat/Asset/AssetManager.h"
#include "TomCat/Utils/FileSystemUtils.h"
#include "TomCat/Utils/PlatformUtils.h"
#include "TomCat/Utils/PathUtils.h"
#include "TomCat/Project/ProjectManager.h"
#include "TomCat/Scripting/ManagedRuntimeFactory.h"
#include "TomCat/Scripting/ScriptDiagnosticSink.h"
#include "TomCat/Scripting/ScriptEngine.h"

#include "ImGuizmo.h"


namespace TomCat {

	extern const std::filesystem::path g_AssetPath;

	namespace {
		constexpr float kSceneToolbarPadding = 5.0f;
		constexpr float kSceneToolbarHandleWidth = 24.0f;
		constexpr float kSceneToolbarItemGap = 4.0f;
		constexpr float kSceneToolbarDockGap = 4.0f;
		constexpr float kSceneModeButtonWidth = 62.0f;
		constexpr float kSceneTransformButtonWidth = 34.0f;
		constexpr float kSceneModeToolbarWidth = kSceneToolbarPadding * 2.0f +
			kSceneToolbarHandleWidth + kSceneToolbarItemGap +
			kSceneModeButtonWidth * 2.0f + kSceneToolbarItemGap;
		constexpr float kSceneTransformToolbarWidth = kSceneToolbarPadding * 2.0f +
			kSceneToolbarHandleWidth + kSceneToolbarItemGap +
			kSceneTransformButtonWidth * 4.0f + kSceneToolbarItemGap * 3.0f;

		ImTextureID ToImGuiTextureID(const Ref<Texture2D>& texture)
		{
			return texture
				? reinterpret_cast<ImTextureID>(static_cast<uintptr_t>(texture->GetRendererID()))
				: nullptr;
		}

		void DrawEditorIcon(ImDrawList* drawList, const Ref<EditorIconSet>& icons,
			EditorIcon icon, const ImVec2& minimum, const ImVec2& maximum,
			ImU32 tint = IM_COL32_WHITE)
		{
			if (!drawList || !icons)
				return;
			const Ref<Texture2D>& texture = icons->Get(icon);
			if (!texture)
				return;
			drawList->AddImage(ToImGuiTextureID(texture), minimum, maximum,
				ImVec2(0.0f, 1.0f), ImVec2(1.0f, 0.0f), tint);
		}

		uint32_t ToFramebufferExtent(float value)
		{
			if (!std::isfinite(value) || value <= 0.0f)
				return 0;
			return static_cast<uint32_t>(std::round(std::clamp(value, 1.0f,
				static_cast<float>(Framebuffer::MaxFramebufferSize))));
		}

		std::filesystem::path AbsoluteLexicalPath(const std::filesystem::path& path)
		{
			if (path.empty())
				return {};
			std::error_code error;
			const std::filesystem::path absolute = std::filesystem::absolute(path, error);
			return (error ? path : absolute).lexically_normal();
		}

		bool TryGetRelativeWithin(const std::filesystem::path& root,
			const std::filesystem::path& candidate, std::filesystem::path& relative)
		{
			const std::filesystem::path normalizedRoot = AbsoluteLexicalPath(root);
			const std::filesystem::path normalizedCandidate = AbsoluteLexicalPath(candidate);
			if (normalizedRoot.empty() || normalizedCandidate.empty())
				return false;
			relative = normalizedCandidate.lexically_relative(normalizedRoot);
			if (relative.empty() || relative.is_absolute())
				return false;
			for (const auto& part : relative)
			{
				if (part == "..")
					return false;
			}
			if (relative == ".")
				relative.clear();
			return true;
		}

		std::string TrimASCIIWhitespace(std::string value)
		{
			auto isWhitespace = [](unsigned char character) { return std::isspace(character) != 0; };
			value.erase(value.begin(), std::find_if(value.begin(), value.end(),
				[&](unsigned char character) { return !isWhitespace(character); }));
			value.erase(std::find_if(value.rbegin(), value.rend(),
				[&](unsigned char character) { return !isWhitespace(character); }).base(), value.end());
			return value;
		}

		bool IsImGuiManagedIniSection(const std::string& header)
		{
			return header.rfind("[Window][", 0) == 0 ||
				header.rfind("[Table][", 0) == 0 ||
				header.rfind("[Docking][", 0) == 0;
		}

		std::string::size_type FindIniSectionHeader(const std::string& ini,
			std::string_view sectionName)
		{
			std::string::size_type position = 0;
			while ((position = ini.find(sectionName, position)) != std::string::npos)
			{
				const bool lineStart = position == 0 || ini[position - 1] == '\n';
				const size_t end = position + sectionName.size();
				const bool lineEnd = end == ini.size() || ini[end] == '\n' || ini[end] == '\r';
				if (lineStart && lineEnd)
					return position;
				position = end;
			}
			return std::string::npos;
		}

		std::filesystem::path GetDefaultEditorLayoutPath()
		{
			std::error_code error;
			const std::filesystem::path currentDirectory = std::filesystem::current_path(error);
			return error ? std::filesystem::path("imgui.ini") : currentDirectory / "imgui.ini";
		}

		std::filesystem::path GetEditorLayoutPath(const Ref<Project>& project)
		{
			if (project)
			{
				if (project->GetProjectPath().empty())
				{
					TC_Core_Error("Cannot resolve an Editor layout for a project with an empty path");
					return {};
				}
				return project->GetProjectPath().parent_path() / "UserSettings" / "imgui.ini";
			}

			const std::optional<std::filesystem::path> settingsRoot = GetTomCatSettingsRoot();
			return settingsRoot ? *settingsRoot / "editor-layout.ini" : std::filesystem::path{};
		}

		bool EnsureSettingsDirectory(const std::filesystem::path& settingsPath)
		{
			if (settingsPath.empty())
				return false;
			const std::filesystem::path directory = settingsPath.parent_path();
			if (directory.empty())
				return true;

			std::error_code error;
			std::filesystem::create_directories(directory, error);
			if (!error)
				return true;

			TC_Core_Error("Failed to create editor settings directory '{0}': {1}",
				PathToUTF8(directory), error.message());
			return false;
		}

		bool LoadImGuiSettings(const std::filesystem::path& path)
		{
			std::ifstream input(path, std::ios::binary);
			if (!input)
				return false;
			std::ostringstream contents;
			contents << input.rdbuf();
			if (input.bad())
				return false;
			const std::string settings = contents.str();
			std::istringstream lines(settings);
			std::string line;
			bool hasManagedSection = false;
			while (std::getline(lines, line))
			{
				if (!line.empty() && line.back() == '\r')
					line.pop_back();
				if (IsImGuiManagedIniSection(line))
				{
					hasManagedSection = true;
					break;
				}
			}
			// A user file may initially contain only custom toolbar/browser sections.
			// Loading that through ImGui would clear the default Docking settings.
			if (!hasManagedSection)
				return false;
			ImGui::LoadIniSettingsFromMemory(settings.data(), settings.size());
			return true;
		}

		bool ReadCustomIniSections(const std::filesystem::path& path,
			std::vector<std::string>& sections)
		{
			sections.clear();
			std::error_code existsError;
			const bool exists = std::filesystem::exists(path, existsError);
			if (existsError)
			{
				TC_Core_Error("Could not inspect ImGui settings '{0}': {1}",
					PathToUTF8(path), existsError.message());
				return false;
			}

			std::ifstream input(path, std::ios::binary);
			if (exists && !input)
			{
				TC_Core_Error("Could not read ImGui settings '{0}'", PathToUTF8(path));
				return false;
			}
			if (!input)
				return true;
			std::ostringstream contents;
			contents << input.rdbuf();
			if (input.bad())
			{
				TC_Core_Error("Failed while reading ImGui settings '{0}'", PathToUTF8(path));
				return false;
			}
			const std::string ini = contents.str();

			std::string::size_type sectionStart = 0;
			while (sectionStart < ini.size())
			{
				if (ini[sectionStart] != '[' || (sectionStart > 0 && ini[sectionStart - 1] != '\n'))
				{
					sectionStart = ini.find("\n[", sectionStart);
					if (sectionStart == std::string::npos)
						break;
					++sectionStart;
				}
				const std::string::size_type headerEnd = ini.find('\n', sectionStart);
				const std::string header = ini.substr(sectionStart,
					headerEnd == std::string::npos ? std::string::npos : headerEnd - sectionStart);
				const std::string::size_type nextMarker = headerEnd == std::string::npos
					? std::string::npos : ini.find("\n[", headerEnd);
				const std::string::size_type sectionEnd = nextMarker == std::string::npos
					? ini.size() : nextMarker + 1;
				if (!IsImGuiManagedIniSection(header))
					sections.push_back(ini.substr(sectionStart, sectionEnd - sectionStart));
				if (nextMarker == std::string::npos)
					break;
				sectionStart = nextMarker + 1;
			}
			return true;
		}

		bool SaveImGuiSettingsPreservingCustomSections(const std::filesystem::path& path)
		{
			if (!EnsureSettingsDirectory(path))
				return false;

			std::vector<std::string> customSections;
			if (!ReadCustomIniSections(path, customSections))
				return false;
			size_t size = 0;
			const char* settings = ImGui::SaveIniSettingsToMemory(&size);
			if (!settings)
				return false;

			std::string ini(settings, size);
			for (const std::string& section : customSections)
			{
				if (!ini.empty() && ini.back() != '\n')
					ini.push_back('\n');
				ini += section;
			}
			std::string writeError;
			if (FileSystem::WriteFileAtomically(path, ini, writeError))
				return true;
			TC_Core_Error("Failed to save ImGui settings '{0}': {1}", PathToUTF8(path), writeError);
			return false;
		}

		std::string LowerASCII(std::string value)
		{
			std::transform(value.begin(), value.end(), value.begin(),
				[](unsigned char character)
				{
					return static_cast<char>(std::tolower(character));
				});
			return value;
		}

		std::string SanitizePlayerDirectoryName(std::string value)
		{
			for (char& character : value)
			{
				const unsigned char byte = static_cast<unsigned char>(character);
				if (byte < 0x20 || character == '<' || character == '>' ||
					character == ':' || character == '"' || character == '/' ||
					character == '\\' || character == '|' || character == '?' ||
					character == '*')
					character = '_';
			}
			while (!value.empty() && (value.front() == ' ' || value.front() == '.'))
				value.erase(value.begin());
			while (!value.empty() && (value.back() == ' ' || value.back() == '.'))
				value.pop_back();
			if (value.empty())
				value = "TomCatGame";

			std::string stem = value.substr(0, value.find('.'));
			stem = LowerASCII(std::move(stem));
			bool reserved = stem == "con" || stem == "prn" || stem == "aux" ||
				stem == "nul";
			if (!reserved && stem.size() == 4 &&
				(stem.rfind("com", 0) == 0 || stem.rfind("lpt", 0) == 0) &&
				stem[3] >= '1' && stem[3] <= '9')
				reserved = true;
			if (reserved)
				value.insert(value.begin(), '_');
			return value;
		}

		bool IsFilesystemReparsePoint(const std::filesystem::path& path)
		{
#ifdef TC_PLATFORM_WINDOWS
			const DWORD attributes = GetFileAttributesW(path.c_str());
			return attributes != INVALID_FILE_ATTRIBUTES &&
				(attributes & FILE_ATTRIBUTE_REPARSE_POINT) != 0;
#else
			std::error_code error;
			return std::filesystem::is_symlink(
				std::filesystem::symlink_status(path, error));
#endif
		}

		bool ReadBinaryFileForBuild(const std::filesystem::path& path,
			std::vector<uint8_t>& bytes, std::string& errorMessage)
		{
			bytes.clear();
			std::error_code error;
			const uintmax_t fileSize = std::filesystem::file_size(path, error);
			if (error || fileSize > static_cast<uintmax_t>((std::numeric_limits<size_t>::max)()))
			{
				errorMessage = "Could not inspect '" + PathToUTF8(path) + "': " +
					(error ? error.message() : "file is too large");
				return false;
			}
			std::ifstream input(path, std::ios::binary);
			if (!input)
			{
				errorMessage = "Could not open '" + PathToUTF8(path) + "'.";
				return false;
			}
			bytes.resize(static_cast<size_t>(fileSize));
			if (!bytes.empty())
				input.read(reinterpret_cast<char*>(bytes.data()),
					static_cast<std::streamsize>(bytes.size()));
			if (!input || input.peek() != std::char_traits<char>::eof())
			{
				errorMessage = "Could not read '" + PathToUTF8(path) + "' completely.";
				bytes.clear();
				return false;
			}
			return true;
		}

		bool ReadTextFileForBuild(const std::filesystem::path& path,
			std::string& contents, std::string& errorMessage)
		{
			std::ifstream input(path, std::ios::binary);
			if (!input)
			{
				errorMessage = "Could not open '" + PathToUTF8(path) + "'.";
				return false;
			}
			std::ostringstream output;
			output << input.rdbuf();
			if (input.bad())
			{
				errorMessage = "Could not read '" + PathToUTF8(path) + "'.";
				return false;
			}
			contents = output.str();
			return true;
		}

		bool CopyPlayerFile(const std::filesystem::path& source,
			const std::filesystem::path& destination, std::string& errorMessage)
		{
			std::error_code error;
			const std::filesystem::file_status status =
				std::filesystem::symlink_status(source, error);
			if (error || !std::filesystem::is_regular_file(status) ||
				std::filesystem::is_symlink(status) || IsFilesystemReparsePoint(source))
			{
				errorMessage = "Required release file is missing or unsafe: '" +
					PathToUTF8(source) + "'.";
				return false;
			}
			std::filesystem::create_directories(destination.parent_path(), error);
			if (error)
			{
				errorMessage = "Could not create '" +
					PathToUTF8(destination.parent_path()) + "': " + error.message();
				return false;
			}
			std::filesystem::copy_file(source, destination,
				std::filesystem::copy_options::overwrite_existing, error);
			if (error)
			{
				errorMessage = "Could not copy '" + PathToUTF8(source) + "' to '" +
					PathToUTF8(destination) + "': " + error.message();
				return false;
			}
			return true;
		}

		bool CopyPlayerDirectory(const std::filesystem::path& source,
			const std::filesystem::path& destination, std::string& errorMessage)
		{
			std::error_code error;
			const std::filesystem::file_status rootStatus =
				std::filesystem::symlink_status(source, error);
			if (error || !std::filesystem::is_directory(rootStatus) ||
				std::filesystem::is_symlink(rootStatus) || IsFilesystemReparsePoint(source))
			{
				errorMessage = "Required release directory is missing or unsafe: '" +
					PathToUTF8(source) + "'.";
				return false;
			}
			std::filesystem::create_directories(destination, error);
			if (error)
			{
				errorMessage = "Could not create '" + PathToUTF8(destination) +
					"': " + error.message();
				return false;
			}

			std::filesystem::recursive_directory_iterator iterator(source,
				std::filesystem::directory_options::none, error), end;
			for (; !error && iterator != end; iterator.increment(error))
			{
				const std::filesystem::file_status status = iterator->symlink_status(error);
				if (error)
					break;
				if (std::filesystem::is_symlink(status) ||
					IsFilesystemReparsePoint(iterator->path()))
				{
					errorMessage = "Release inputs may not contain symbolic links: '" +
						PathToUTF8(iterator->path()) + "'.";
					return false;
				}
				const std::filesystem::path relative =
					iterator->path().lexically_relative(source);
				if (relative.empty() || relative.is_absolute())
				{
					errorMessage = "Could not derive a safe release-relative path for '" +
						PathToUTF8(iterator->path()) + "'.";
					return false;
				}
				const std::filesystem::path target = destination / relative;
				if (std::filesystem::is_directory(status))
					std::filesystem::create_directories(target, error);
				else if (std::filesystem::is_regular_file(status))
				{
					if (!CopyPlayerFile(iterator->path(), target, errorMessage))
						return false;
				}
				else
				{
					errorMessage = "Release inputs contain an unsupported filesystem entry: '" +
						PathToUTF8(iterator->path()) + "'.";
					return false;
				}
			}
			if (error)
			{
				errorMessage = "Could not enumerate '" + PathToUTF8(source) +
					"': " + error.message();
				return false;
			}
			return true;
		}

		bool ParseNumericVersion(std::string_view value,
			std::vector<uint32_t>& components)
		{
			components.clear();
			size_t cursor = 0;
			while (cursor < value.size())
			{
				const size_t separator = value.find('.', cursor);
				const size_t end = separator == std::string_view::npos
					? value.size() : separator;
				if (end == cursor)
					return false;
				uint32_t component = 0;
				const char* begin = value.data() + cursor;
				const char* finish = value.data() + end;
				const auto parsed = std::from_chars(begin, finish, component);
				if (parsed.ec != std::errc{} || parsed.ptr != finish)
					return false;
				components.push_back(component);
				if (separator == std::string_view::npos)
					return true;
				cursor = separator + 1;
			}
			return !components.empty();
		}

		bool NumericVersionLess(std::vector<uint32_t> left,
			std::vector<uint32_t> right)
		{
			const size_t count = std::max(left.size(), right.size());
			left.resize(count);
			right.resize(count);
			return std::lexicographical_compare(left.begin(), left.end(),
				right.begin(), right.end());
		}

		bool ReadFrameworkVersion(const std::filesystem::path& runtimeConfig,
			std::vector<uint32_t>& requestedVersion, std::string& errorMessage)
		{
			std::string json;
			if (!ReadTextFileForBuild(runtimeConfig, json, errorMessage))
				return false;
			size_t cursor = json.find("\"framework\"");
			if (cursor != std::string::npos)
				cursor = json.find("\"version\"", cursor);
			if (cursor == std::string::npos)
			{
				errorMessage = "TomCat.ScriptHost.runtimeconfig.json has no framework version.";
				return false;
			}
			cursor = json.find(':', cursor);
			cursor = cursor == std::string::npos ? cursor : json.find('"', cursor + 1);
			const size_t end = cursor == std::string::npos
				? cursor : json.find('"', cursor + 1);
			if (cursor == std::string::npos || end == std::string::npos ||
				!ParseNumericVersion(std::string_view(json).substr(cursor + 1,
					end - cursor - 1), requestedVersion) || requestedVersion.size() < 2)
			{
				errorMessage = "TomCat.ScriptHost.runtimeconfig.json has an invalid framework version.";
				return false;
			}
			return true;
		}

		bool FindHighestDotNetVersionDirectory(const std::filesystem::path& root,
			const std::vector<uint32_t>* requestedFramework,
			std::filesystem::path& selected, std::string& errorMessage)
		{
			selected.clear();
			std::vector<uint32_t> selectedVersion;
			std::error_code error;
			std::filesystem::directory_iterator iterator(root,
				std::filesystem::directory_options::none, error), end;
			for (; !error && iterator != end; iterator.increment(error))
			{
				if (!iterator->is_directory(error) || error)
					continue;
				std::vector<uint32_t> version;
				if (!ParseNumericVersion(PathToUTF8(iterator->path().filename()), version))
					continue;
				if (requestedFramework)
				{
					if (version.size() < 2 || (*requestedFramework).size() < 2 ||
						version[0] != (*requestedFramework)[0] ||
						version[1] != (*requestedFramework)[1] ||
						NumericVersionLess(version, *requestedFramework))
						continue;
				}
				if (selected.empty() || NumericVersionLess(selectedVersion, version))
				{
					selected = iterator->path();
					selectedVersion = std::move(version);
				}
			}
			if (error)
			{
				errorMessage = "Could not enumerate .NET versions in '" +
					PathToUTF8(root) + "': " + error.message();
				return false;
			}
			return !selected.empty();
		}

#ifdef TC_PLATFORM_WINDOWS
		std::filesystem::path ReadWindowsEnvironmentPath(const wchar_t* name)
		{
			const DWORD required = GetEnvironmentVariableW(name, nullptr, 0);
			if (required == 0)
				return {};
			std::vector<wchar_t> value(static_cast<size_t>(required));
			const DWORD written = GetEnvironmentVariableW(name, value.data(), required);
			if (written == 0 || written >= required)
				return {};
			return std::filesystem::path(std::wstring(value.data(), written));
		}
#endif

		bool ResolvePrivateDotNetPayload(const std::filesystem::path& runtimeConfig,
			std::filesystem::path& dotnetRoot, std::filesystem::path& hostFxrDirectory,
			std::filesystem::path& runtimeDirectory, std::string& errorMessage)
		{
#ifdef TC_PLATFORM_WINDOWS
			std::vector<uint32_t> requestedFramework;
			if (!ReadFrameworkVersion(runtimeConfig, requestedFramework, errorMessage))
				return false;

			std::vector<std::filesystem::path> candidates;
			const std::filesystem::path configured =
				ReadWindowsEnvironmentPath(L"DOTNET_ROOT");
			if (!configured.empty())
				candidates.push_back(configured);
			const std::filesystem::path programFiles =
				ReadWindowsEnvironmentPath(L"ProgramFiles");
			if (!programFiles.empty())
				candidates.push_back(programFiles / L"dotnet");

			for (const std::filesystem::path& candidate : candidates)
			{
				std::error_code error;
				const std::filesystem::path absolute =
					std::filesystem::absolute(candidate, error).lexically_normal();
				if (error || !std::filesystem::is_directory(
					absolute / "host" / "fxr", error) || error)
					continue;

				std::string selectionError;
				std::filesystem::path selectedHostFxr;
				std::filesystem::path selectedRuntime;
				if (!FindHighestDotNetVersionDirectory(absolute / "host" / "fxr",
					nullptr, selectedHostFxr, selectionError) ||
					!FindHighestDotNetVersionDirectory(absolute / "shared" /
						"Microsoft.NETCore.App", &requestedFramework,
						selectedRuntime, selectionError))
					continue;
				if (!std::filesystem::is_regular_file(
					selectedHostFxr / "hostfxr.dll", error) || error ||
					!std::filesystem::is_regular_file(
						selectedRuntime / "hostpolicy.dll", error) || error ||
					!std::filesystem::is_regular_file(
						selectedRuntime / "coreclr.dll", error) || error)
					continue;

				dotnetRoot = absolute;
				hostFxrDirectory = selectedHostFxr;
				runtimeDirectory = selectedRuntime;
				return true;
			}
			errorMessage = "Could not resolve the .NET " +
				std::to_string(requestedFramework[0]) + "." +
				std::to_string(requestedFramework[1]) +
				" host and runtime required by TomCat.ScriptHost.";
			return false;
#else
			(void)runtimeConfig;
			(void)dotnetRoot;
			(void)hostFxrDirectory;
			(void)runtimeDirectory;
			errorMessage = "TomCat Player export is currently implemented for Windows only.";
			return false;
#endif
		}

		bool GetRunningExecutablePath(std::filesystem::path& executable,
			std::string& errorMessage)
		{
#ifdef TC_PLATFORM_WINDOWS
			std::vector<wchar_t> buffer(MAX_PATH);
			for (;;)
			{
				const DWORD length = GetModuleFileNameW(nullptr, buffer.data(),
					static_cast<DWORD>(buffer.size()));
				if (length == 0)
				{
					errorMessage = "Could not resolve the Editor executable (Win32 error " +
						std::to_string(GetLastError()) + ").";
					return false;
				}
				if (length < buffer.size() - 1)
				{
					executable = std::filesystem::path(
						std::wstring(buffer.data(), length)).lexically_normal();
					return true;
				}
				if (buffer.size() >= 32768)
					break;
				buffer.resize(buffer.size() * 2);
			}
			errorMessage = "The Editor executable path exceeds the Windows path limit.";
		return false;
#else
			(void)executable;
			errorMessage = "TomCat Player export is currently implemented for Windows only.";
			return false;
#endif
		}

		bool ValidateStagedPlayer(const std::filesystem::path& root,
			std::string& errorMessage)
		{
			for (const std::filesystem::path& required : {
				root / "TomCatPlayer.exe", root / "Game.tcpak",
				root / "Managed" / "TomCat.ScriptHost.dll",
				root / "Managed" / "TomCat.ScriptHost.runtimeconfig.json",
				root / "Managed" / "TomCat.Managed.dll" })
			{
				std::error_code error;
				if (!std::filesystem::is_regular_file(required, error) || error)
				{
					errorMessage = "Staged Player is missing '" +
						PathToUTF8(required.lexically_relative(root)) + "'.";
					return false;
				}
			}

			std::error_code error;
			std::filesystem::recursive_directory_iterator iterator(root,
				std::filesystem::directory_options::none, error), end;
			for (; !error && iterator != end; iterator.increment(error))
			{
				const std::filesystem::file_status status = iterator->symlink_status(error);
				if (error)
					break;
				if (std::filesystem::is_symlink(status) ||
					IsFilesystemReparsePoint(iterator->path()))
				{
					errorMessage = "Staged Player contains an unsafe symbolic link: '" +
						PathToUTF8(iterator->path().lexically_relative(root)) + "'.";
					return false;
				}
				const std::filesystem::path relative =
					iterator->path().lexically_relative(root);
				if (relative.empty() || relative.is_absolute())
				{
					errorMessage = "Staged Player contains an invalid path.";
					return false;
				}
				const size_t componentCount = static_cast<size_t>(
					std::distance(relative.begin(), relative.end()));
				const std::string first = LowerASCII(PathToUTF8(*relative.begin()));
				if (std::filesystem::is_directory(status))
				{
					if ((componentCount == 1 && first != "managed" && first != "dotnet") ||
						(componentCount > 1 && first != "dotnet"))
					{
						errorMessage = "Staged Player contains an unexpected directory: '" +
							PathToUTF8(relative) + "'.";
						return false;
					}
				}
				if (std::filesystem::is_regular_file(status))
				{
					const std::string fileName =
						LowerASCII(PathToUTF8(iterator->path().filename()));
					const std::string extension =
						LowerASCII(PathToUTF8(iterator->path().extension()));
					bool allowed = first == "dotnet";
					if (componentCount == 1)
						allowed = fileName == "tomcatplayer.exe" ||
							fileName == "game.tcpak" || extension == ".dll";
					else if (first == "managed" && componentCount == 2)
						allowed = fileName == "tomcat.scripthost.dll" ||
							fileName == "tomcat.scripthost.runtimeconfig.json" ||
							fileName == "tomcat.scripthost.deps.json" ||
							fileName == "tomcat.managed.dll";
					if (!allowed)
					{
						errorMessage = "Staged Player contains an unexpected file: '" +
							PathToUTF8(relative) + "'.";
						return false;
					}
					if (extension == ".pdb" || extension == ".cs" ||
						extension == ".csproj" ||
						extension == ".sln" || extension == ".slnx" ||
						extension == ".tcproj" || fileName == "last-good.json")
					{
						errorMessage = "Authoring input leaked into the staged Player: '" +
							PathToUTF8(relative) + "'.";
						return false;
					}
				}
			}
			if (error)
			{
				errorMessage = "Could not validate the staged Player: " + error.message();
				return false;
			}
			return true;
		}

		bool PublishStagedPlayer(const std::filesystem::path& staging,
			const std::filesystem::path& output, const std::string& buildID,
			std::string& errorMessage)
		{
			std::error_code error;
			std::filesystem::path backup = output;
			backup += ".previous-" + buildID;
			if (std::filesystem::exists(backup, error) || error)
			{
				errorMessage = "Could not reserve a rollback directory for the Player build.";
				return false;
			}

			bool movedPrevious = false;
			const std::filesystem::file_status outputStatus =
				std::filesystem::symlink_status(output, error);
			if (!error && std::filesystem::exists(outputStatus))
			{
				if (!std::filesystem::is_directory(outputStatus) ||
					std::filesystem::is_symlink(outputStatus) ||
					IsFilesystemReparsePoint(output))
				{
					errorMessage = "Player output exists but is not a safe directory: '" +
						PathToUTF8(output) + "'.";
					return false;
				}
				std::filesystem::rename(output, backup, error);
				if (error)
				{
					errorMessage = "Could not replace the previous Player build (close any "
						"running Player and try again): " + error.message();
					return false;
				}
				movedPrevious = true;
			}
			else if (error && error != std::errc::no_such_file_or_directory)
			{
				errorMessage = "Could not inspect the Player output directory: " +
					error.message();
				return false;
			}

			error.clear();
			std::filesystem::rename(staging, output, error);
			if (error)
			{
				const std::string publishError = error.message();
				if (movedPrevious)
				{
					error.clear();
					std::filesystem::rename(backup, output, error);
				}
				errorMessage = "Could not publish the staged Player: " + publishError;
				if (error)
					errorMessage += "; rollback also failed: " + error.message();
				return false;
			}

			if (movedPrevious)
			{
				error.clear();
				std::filesystem::remove_all(backup, error);
				if (error)
					TC_Core_Warn("Player build succeeded, but rollback directory '{0}' "
						"could not be removed: {1}", PathToUTF8(backup), error.message());
			}
			return true;
		}

		bool LaunchCookedPlayer(const std::filesystem::path& executable,
			const std::filesystem::path& workingDirectory, std::string& errorMessage)
		{
#ifdef TC_PLATFORM_WINDOWS
			std::wstring command = L"\"" + executable.wstring() +
				L"\" --play-cooked \"Game.tcpak\"";
			std::vector<wchar_t> mutableCommand(command.begin(), command.end());
			mutableCommand.push_back(L'\0');
			STARTUPINFOW startup{};
			startup.cb = sizeof(startup);
			PROCESS_INFORMATION process{};
			const BOOL created = CreateProcessW(executable.c_str(), mutableCommand.data(),
				nullptr, nullptr, FALSE, 0, nullptr, workingDirectory.c_str(),
				&startup, &process);
			if (!created)
			{
				errorMessage = "Could not launch TomCatPlayer.exe (Win32 error " +
					std::to_string(GetLastError()) + ").";
				return false;
			}
			CloseHandle(process.hThread);
			CloseHandle(process.hProcess);
			return true;
#else
			(void)executable;
			(void)workingDirectory;
			errorMessage = "TomCat Player launch is currently implemented for Windows only.";
			return false;
#endif
		}

	}

	EditorLayer::EditorLayer()
		: Layer("EditorLayer")
	{
		m_CurrentProject = ProjectManager::Get().GetActiveProject();
		if (m_CurrentProject)
			m_Is2DMode = m_CurrentProject->GetConfig().Template == "2D";
	}

	void EditorLayer::LoadSceneToolbarLayout()
	{
		// Restore the in-code fallback before applying the packaged baseline and
		// the selected global/project override. This prevents one layout's values
		// from leaking into another layout that has no settings file yet.
		m_GizmoModeToolbarDocked = true;
		m_GizmoTransformToolbarDocked = false;
		m_GizmoModeToolbarFirst = true;
		m_GizmoModeToolbarOffset = { 16.0f, 10.0f };
		m_GizmoToolbarOffset = { 16.0f, 48.0f };

		auto loadFrom = [&](const std::filesystem::path& iniPath) -> bool
		{
			std::ifstream fin(iniPath);
			if (!fin)
				return false;

			bool inSection = false;
			bool foundSection = false;
			bool hasModeDocked = false;
			bool hasTransformDocked = false;
			bool hasModeFirst = false;
			bool hasModeX = false, hasModeY = false;
			bool hasTransformX = false, hasTransformY = false;
			std::string line;
			while (std::getline(fin, line))
			{
				if (!line.empty() && line.back() == '\r')
					line.pop_back();
				if (line == "[SceneToolbars]")
				{
					inSection = true;
					foundSection = true;
					continue;
				}
				if (!inSection)
					continue;
				if (!line.empty() && line.front() == '[')
					break;
				const std::string::size_type equals = line.find('=');
				if (equals == std::string::npos)
					continue;
				const std::string key = line.substr(0, equals);
				const std::string value = line.substr(equals + 1);
				auto readBool = [&value](bool& destination, bool& present)
				{
					if (value == "1" || value == "true" || value == "True")
					{
						destination = true;
						present = true;
					}
					else if (value == "0" || value == "false" || value == "False")
					{
						destination = false;
						present = true;
					}
				};
				auto readFloat = [&value](float& destination, bool& present)
				{
					try
					{
						destination = std::stof(value);
						present = true;
					}
					catch (const std::exception&)
					{
						// Ignore malformed values and retain the in-code default.
					}
				};

				if (key == "ModeToolbarDocked")
					readBool(m_GizmoModeToolbarDocked, hasModeDocked);
				else if (key == "TransformToolbarDocked")
					readBool(m_GizmoTransformToolbarDocked, hasTransformDocked);
				else if (key == "ModeToolbarFirst")
					readBool(m_GizmoModeToolbarFirst, hasModeFirst);
				else if (key == "ModeToolbarOffsetX")
					readFloat(m_GizmoModeToolbarOffset.x, hasModeX);
				else if (key == "ModeToolbarOffsetY")
					readFloat(m_GizmoModeToolbarOffset.y, hasModeY);
				else if (key == "TransformToolbarOffsetX")
					readFloat(m_GizmoToolbarOffset.x, hasTransformX);
				else if (key == "TransformToolbarOffsetY")
					readFloat(m_GizmoToolbarOffset.y, hasTransformY);
			}
			return foundSection;
		};

		// Custom sections are not consumed by ImGui itself. Apply the packaged
		// defaults first, then let the selected global/project section override them.
		loadFrom(GetDefaultEditorLayoutPath());
		loadFrom(GetEditorLayoutPath(m_CurrentProject));
	}

	void EditorLayer::SaveSceneToolbarLayout()
	{
		const std::filesystem::path iniPath = GetEditorLayoutPath(m_CurrentProject);
		if (!EnsureSettingsDirectory(iniPath))
			return;

		std::string ini;
		{
			std::error_code existsError;
			const bool exists = std::filesystem::exists(iniPath, existsError);
			if (existsError)
			{
				TC_Core_Error("Could not inspect Scene toolbar settings '{0}': {1}",
					PathToUTF8(iniPath), existsError.message());
				return;
			}

			std::ifstream fin(iniPath, std::ios::binary);
			if (exists && !fin)
			{
				TC_Core_Error("Could not read Scene toolbar settings '{0}'", PathToUTF8(iniPath));
				return;
			}
			if (fin)
			{
				std::stringstream contents;
				contents << fin.rdbuf();
				if (fin.bad())
				{
					TC_Core_Error("Failed while reading Scene toolbar settings '{0}'", PathToUTF8(iniPath));
					return;
				}
				ini = contents.str();
			}
		}

		std::ostringstream section;
		section << "\n[SceneToolbars]\n"
			<< "ModeToolbarDocked=" << (m_GizmoModeToolbarDocked ? 1 : 0) << "\n"
			<< "TransformToolbarDocked=" << (m_GizmoTransformToolbarDocked ? 1 : 0) << "\n"
			<< "ModeToolbarFirst=" << (m_GizmoModeToolbarFirst ? 1 : 0) << "\n"
			<< std::fixed << std::setprecision(3)
			<< "ModeToolbarOffsetX=" << m_GizmoModeToolbarOffset.x << "\n"
			<< "ModeToolbarOffsetY=" << m_GizmoModeToolbarOffset.y << "\n"
			<< "TransformToolbarOffsetX=" << m_GizmoToolbarOffset.x << "\n"
			<< "TransformToolbarOffsetY=" << m_GizmoToolbarOffset.y << "\n";

		const std::string sectionName = "[SceneToolbars]";
		const std::string::size_type sectionPos = FindIniSectionHeader(ini, sectionName);
		if (sectionPos != std::string::npos)
		{
			const std::string::size_type nextSection = ini.find("\n[", sectionPos + sectionName.size());
			ini.erase(sectionPos, nextSection == std::string::npos ? std::string::npos : nextSection - sectionPos);
		}
		if (!ini.empty() && ini.back() != '\n')
			ini.push_back('\n');
		ini += section.str();

		std::string writeError;
		if (!FileSystem::WriteFileAtomically(iniPath, ini, writeError))
			TC_Core_Error("Failed to save Scene toolbar layout '{0}': {1}", PathToUTF8(iniPath), writeError);
	}

	void EditorLayer::OnAttach()
	{
		TC_PROFILE_FUNCTION();

		m_EditorIcons = CreateRef<EditorIconSet>();
		if (!m_EditorIcons->Load())
			TC_Core_Warn("One or more editor icons could not be loaded");
		m_SceneHierarchyPanel.SetIcons(m_EditorIcons);
		m_SceneHierarchyPanel.SetProject(m_CurrentProject);
		m_SceneHierarchyPanel.SetScriptMetadataProvider([this](AssetHandle handle)
		{
			return m_ScriptMetadata.Find(handle);
		});
		m_ContentBrowserPanel.SetIcons(m_EditorIcons);
		m_ContentBrowserPanel.SetActiveScenePath(m_EditorScenePath);
		Scripting::SetScriptDiagnosticSink([this](const Scripting::ScriptDiagnostic& diagnostic)
		{
			ConsoleMessage message;
			message.Source = "C# Runtime";
			message.File = diagnostic.File;
			message.Line = diagnostic.Line;
			message.Column = diagnostic.Column;
			switch (diagnostic.Severity)
			{
				case Scripting::ScriptDiagnosticSeverity::Info:
					message.Severity = ConsoleMessageSeverity::Info;
					break;
				case Scripting::ScriptDiagnosticSeverity::Warning:
					message.Severity = ConsoleMessageSeverity::Warning;
					break;
				case Scripting::ScriptDiagnosticSeverity::Error:
					message.Severity = ConsoleMessageSeverity::Error;
					break;
			}
			const size_t stackStart = diagnostic.Message.find('\n');
			if (stackStart == std::string::npos)
				message.Text = diagnostic.Message;
			else
			{
				message.Text = diagnostic.Message.substr(0, stackStart);
				if (!message.Text.empty() && message.Text.back() == '\r')
					message.Text.pop_back();
				message.StackTrace = diagnostic.Message.substr(stackStart + 1);
			}
			m_ConsolePanel.Push(std::move(message));
		});

		m_ScriptCompiler.SetDiagnosticCallback([this](const ScriptCompilerDiagnostic& diagnostic)
		{
			ConsoleMessage message;
			message.Source = "C# Compiler";
			message.Code = diagnostic.Code;
			message.Text = diagnostic.Message;
			message.File = diagnostic.File;
			message.Line = diagnostic.Line;
			message.Column = diagnostic.Column;
			switch (diagnostic.Level)
			{
				case ScriptCompilerDiagnostic::Severity::Info:
					message.Severity = ConsoleMessageSeverity::Info;
					break;
				case ScriptCompilerDiagnostic::Severity::Warning:
					message.Severity = ConsoleMessageSeverity::Warning;
					break;
				case ScriptCompilerDiagnostic::Severity::Error:
					message.Severity = ConsoleMessageSeverity::Error;
					break;
			}
			m_ConsolePanel.Push(std::move(message));
		});
		if (m_CurrentProject && m_ScriptCompiler.Configure(m_CurrentProject))
		{
			ResetScriptCompileTracking();
			if (m_ScriptCompiler.IsCurrentSourceBuilt())
				PrepareManagedRuntime();
		}

		FramebufferSpecification fbSpec;
		fbSpec.Attachments = { FramebufferTextureFormat::RGBA8, FramebufferTextureFormat::RED_INTEGER, FramebufferTextureFormat::Depth };
		fbSpec.Width = 1280;
		fbSpec.Height = 720;
		m_Framebuffer = Framebuffer::Create(fbSpec);
		m_GameFramebuffer = Framebuffer::Create(fbSpec);

		// The packaged root file is a read-only baseline. The selected writable
		// layout is global in no-project mode and project-local otherwise.
		LoadImGuiSettings(GetDefaultEditorLayoutPath());
		LoadImGuiSettings(GetEditorLayoutPath(m_CurrentProject));

		LoadSceneToolbarLayout();

		m_EditorCamera = EditorCamera(30.0f, 1.778f, 0.1f, 1000.0f);
		m_EditorCamera.Set2DMode(m_Is2DMode);

		m_SceneHierarchyPanel.SetSceneLoadCallback([this](AssetHandle handle) {
			const std::filesystem::path path = AssetManager::Get().ResolvePath(handle);
			if (!path.empty())
				OpenScene(path);
		});
		m_ContentBrowserPanel.SetSceneOpenCallback([this](AssetHandle handle) {
			const std::filesystem::path path = AssetManager::Get().ResolvePath(handle);
			if (!path.empty())
				OpenScene(path);
		});
		m_ContentBrowserPanel.SetAssetRenamedCallback([this](const std::filesystem::path& oldPath,
			const std::filesystem::path& newPath) {
			const std::filesystem::path previousEditorScenePath = m_EditorScenePath;
			const std::filesystem::path previousStartScene = m_CurrentProject
				? m_CurrentProject->GetConfig().StartScene : std::filesystem::path{};
			std::filesystem::path relative;
			if (!m_EditorScenePath.empty() && TryGetRelativeWithin(oldPath, m_EditorScenePath, relative))
				m_EditorScenePath = newPath / relative;

			// StartScene is an authoring locator; StartSceneHandle remains the
			// authoritative identity across this move.
			if (m_CurrentProject)
			{
				const std::filesystem::path oldStartScene = m_CurrentProject->GetAssetPath() /
					m_CurrentProject->GetConfig().StartScene;
				if (TryGetRelativeWithin(oldPath, oldStartScene, relative))
				{
					const std::filesystem::path movedStartScene = newPath / relative;
					std::filesystem::path assetRelative;
					const bool updated = TryGetRelativeWithin(m_CurrentProject->GetAssetPath(),
						movedStartScene, assetRelative) &&
						m_CurrentProject->SetStartScene(assetRelative) && m_CurrentProject->Save();
					if (!updated)
					{
						m_EditorScenePath = previousEditorScenePath;
						m_ContentBrowserPanel.SetActiveScenePath(m_EditorScenePath);
						(void)m_CurrentProject->SetStartScene(previousStartScene);
						TC_Core_Error("The start scene move was rejected because Project.tcproj could not be updated");
						return false;
					}
				}
			}
			m_ContentBrowserPanel.SetActiveScenePath(m_EditorScenePath);
			return true;
		});
		m_ContentBrowserPanel.SetAssetDeletedCallback([this](const std::filesystem::path& deletedPath) {
			std::filesystem::path relative;
			if (!m_EditorScenePath.empty() && TryGetRelativeWithin(deletedPath, m_EditorScenePath, relative))
			{
				m_EditorScenePath.clear();
				m_ContentBrowserPanel.SetActiveScenePath({});
				m_SceneDirty = true;
			}
			if (m_CurrentProject)
			{
				const std::filesystem::path startScene = m_CurrentProject->GetAssetPath() /
					m_CurrentProject->GetConfig().StartScene;
				if (TryGetRelativeWithin(deletedPath, startScene, relative))
					TC_Warn("The configured start scene was deleted and is now a missing asset: {0}",
						PathToUTF8(startScene));
			}
			// Sprite handles deliberately survive deletion. AssetManager resolves
			// them to the shared missing-resource texture until the asset is restored.
		});

		m_SceneHierarchyPanel.SetSpriteCreateCallback([this](AssetHandle handle) {
			if (!m_ActiveScene)
				return;
			const AssetMetadata* metadata = AssetManager::Get().GetRegistry().GetMetadata(handle);
			if (!metadata || metadata->Type != AssetType::Texture2D)
				return;
			const std::filesystem::path path = AssetManager::Get().ResolvePath(handle);
			std::string fileName = PathToUTF8(path.stem());
			auto Square = m_ActiveScene->CreateEntity(fileName);
			auto& SpriteR = Square.AddComponent<SpriteRenderer>(glm::vec4{ 1.0f, 1.0f, 1.0f, 1.0f });
			SpriteR.SpriteHandle = handle;
			SpriteR.Sprite = AssetManager::Get().LoadTexture(handle);
			if (m_SceneState == SceneState::Edit)
				m_SceneDirty = true;
		});
		m_SceneHierarchyPanel.SetSceneModifiedCallback([this]() {
			if (m_SceneState == SceneState::Edit)
				m_SceneDirty = true;
		});
		AssetManager::Get().SetLiveReferenceProvider([this](AssetHandle handle) {
			std::vector<AssetReference> references;
			if (m_EditorScene)
				references = m_EditorScene->FindAssetReferences(handle);
			AssetHandle sceneHandle(0);
			if (!m_EditorScenePath.empty())
			{
				if (const AssetMetadata* metadata =
					AssetManager::Get().GetRegistry().GetMetadata(m_EditorScenePath))
					sceneHandle = metadata->Handle;
			}
			for (AssetReference& reference : references)
			{
				reference.ReferencingAsset = sceneHandle;
				reference.FilePath = m_EditorScenePath.empty()
					? std::filesystem::path("<Unsaved Scene>") : m_EditorScenePath;
			}
			return references;
		});

		if (m_CurrentProject)
			OpenProjectStartScene();
		else
		{
			NewScene();
			m_SceneDirty = false;
		}
	}

	void EditorLayer::OnDetach()
	{
		TC_PROFILE_FUNCTION();
		if (IsSceneRunning())
			OnSceneStop();

		if (m_CurrentProject)
			m_ContentBrowserPanel.Serialize();

		// ImGui-managed state and custom panel sections share either the global
		// no-project layout or the active project's UserSettings/imgui.ini.
		SaveImGuiSettingsPreservingCustomSections(GetEditorLayoutPath(m_CurrentProject));
		m_ContentBrowserPanel.SaveLayoutSetting();
		SaveSceneToolbarLayout();
		m_SceneHierarchyPanel.SetScriptMetadataProvider({});
		m_ScriptMetadata.Clear();
		Scripting::SetScriptDiagnosticSink({});
		Scripting::ScriptEngine::Get().SetRuntime({});
		m_ScriptCompiler.Reset();
		AssetManager::Get().SetLiveReferenceProvider({});
		AssetManager::Get().Shutdown();
	}

	void EditorLayer::ResetScriptCompileTracking()
	{
		m_ScriptSourcePollCountdown = 0.0f;
		m_ScriptCompileDebounceRemaining = 0.65f;
		m_ObservedScriptSourceHash = m_ScriptCompiler.GetCurrentSourceHash();
		m_PlayScriptDirtyNoticeShown = false;
	}

	void EditorLayer::UpdateScriptCompilation(Timestep ts)
	{
		ScriptBuildResult completed;
		if (m_ScriptCompiler.PollCompile(completed))
		{
			m_ObservedScriptSourceHash = m_ScriptCompiler.GetCurrentSourceHash();
			m_ScriptCompileDebounceRemaining = 0.65f;
			if (completed.Succeeded && m_SceneState == SceneState::Edit)
				PrepareManagedRuntime();
		}
		if (!m_CurrentProject)
			return;

		const float deltaSeconds = std::clamp(ts.GetSeconds(), 0.0f, 0.25f);
		m_ScriptSourcePollCountdown -= deltaSeconds;
		if (!m_ScriptCompiler.IsCompileInProgress() &&
			m_ScriptSourcePollCountdown <= 0.0f)
		{
			m_ScriptSourcePollCountdown = 0.35f;
			if (m_ScriptCompiler.RefreshSourceState())
			{
				const std::string& sourceHash =
					m_ScriptCompiler.GetCurrentSourceHash();
				if (sourceHash != m_ObservedScriptSourceHash)
				{
					m_ObservedScriptSourceHash = sourceHash;
					m_ScriptCompileDebounceRemaining = 0.65f;
				}
				if (IsSceneRunning() &&
					!m_ScriptCompiler.IsCurrentSourceBuilt() &&
					!m_PlayScriptDirtyNoticeShown)
				{
					m_PlayScriptDirtyNoticeShown = true;
					m_ShowConsolePanel = true;
					m_ConsolePanel.Push(ConsoleMessageSeverity::Warning,
						"C# source changes were detected. Stop Play to apply them; "
						"the running scene will keep its current assembly.",
						"Editor");
				}
			}
		}

		if (m_SceneState == SceneState::Edit &&
			!m_ScriptCompiler.IsCompileInProgress() &&
			m_ScriptCompiler.GetState() == ScriptBuildState::Dirty)
		{
			m_ScriptCompileDebounceRemaining -= deltaSeconds;
			if (m_ScriptCompileDebounceRemaining <= 0.0f)
			{
				m_ScriptCompileDebounceRemaining = 0.65f;
				m_ScriptCompiler.StartCompile();
			}
		}
	}

	void EditorLayer::OnUpdate(Timestep ts)
	{
		TC_PROFILE_FUNCTION();
		Scripting::ScriptEngine::Get().CaptureInputState();
		UpdateScriptCompilation(ts);

		// Resize Scene Framebuffer
		const uint32_t sceneWidth = ToFramebufferExtent(m_ViewportSize.x);
		const uint32_t sceneHeight = ToFramebufferExtent(m_ViewportSize.y);
		if (FramebufferSpecification spec = m_Framebuffer->GetSpecification();
			sceneWidth > 0 && sceneHeight > 0 &&
			(spec.Width != sceneWidth || spec.Height != sceneHeight))
		{
			if (m_Framebuffer->Resize(sceneWidth, sceneHeight))
				m_EditorCamera.SetViewportSize(static_cast<float>(sceneWidth), static_cast<float>(sceneHeight));
		}

		// Resize Game Framebuffer
		const uint32_t gameWidth = ToFramebufferExtent(m_GameViewportSize.x);
		const uint32_t gameHeight = ToFramebufferExtent(m_GameViewportSize.y);
		if (FramebufferSpecification gameSpec = m_GameFramebuffer->GetSpecification();
			gameWidth > 0 && gameHeight > 0 &&
			(gameSpec.Width != gameWidth || gameSpec.Height != gameHeight))
		{
			if (m_GameFramebuffer->Resize(gameWidth, gameHeight))
				m_ActiveScene->OnViewportResize(gameWidth, gameHeight);
		}

		// Render Scene View (Editor Camera).  Unity's Scene canvas is one step
		// lighter than the surrounding #383838 panels (#474747); the grid and
		// selection overlays then provide the additional contrast seen in the
		// reference.
		Renderer2D::ResetStats();
		m_Framebuffer->Bind();
		RenderCommand::SetClearColor({ 71.0f / 255.0f, 71.0f / 255.0f, 71.0f / 255.0f, 1 });
		RenderCommand::Clear();
		m_Framebuffer->ClearAttachment(1, -1);

		// Update
		m_EditorCamera.OnUpdate(ts, m_ViewportCameraDragOwned);

		// Scene窗口始终使用EditorCamera渲染
		m_ActiveScene->OnUpdateEditor(ts, m_EditorCamera);

		// Mouse picking for Scene viewport
		auto [mx, my] = ImGui::GetMousePos();
		mx -= m_ViewportBounds[0].x;
		my -= m_ViewportBounds[0].y;
		glm::vec2 viewportSize = m_ViewportBounds[1] - m_ViewportBounds[0];
		my = viewportSize.y - my;
		int mouseX = (int)mx;
		int mouseY = (int)my;

		if (mouseX >= 0 && mouseY >= 0 && mouseX < (int)viewportSize.x && mouseY < (int)viewportSize.y)
		{
			int pixelData = m_Framebuffer->ReadPixel(1, mouseX, mouseY);
			m_HoveredEntity = pixelData == -1 ? Entity() : Entity((entt::entity)pixelData, m_ActiveScene.get());
		}
		else
		{
			m_HoveredEntity = {};
		}

		// Collider overlays are submitted only after entity picking has sampled the
		// ID attachment. Renderer2D utility primitives intentionally use entity ID
		// -1, so drawing them any earlier would punch holes in sprite picking.
		RenderSceneColliderOverlays();

		m_Framebuffer->Unbind();

		// Render Game View (Runtime Camera) - Always render runtime camera
		m_GameFramebuffer->Bind();

		RenderCommand::SetClearColor({ 0.1f, 0.1f, 0.1f, 1 });
		RenderCommand::Clear();
		m_GameFramebuffer->ClearAttachment(1, -1);

		// Game窗口使用Runtime渲染，背景色由摄像机的BackgroundColor设置
		if (m_SceneState == SceneState::Play)
		{
			m_ActiveScene->OnUpdateRuntime(ts);
		}
		else if (m_SceneState == SceneState::Pause && m_StepRequested)
		{
			m_ActiveScene->OnRuntimeStep();
			m_StepRequested = false;
		}
		else
			m_ActiveScene->OnRenderRuntime();
		m_GameFramebuffer->Unbind();
	}

	void EditorLayer::UpdateWindowTitle()
	{
		const std::string projectName = m_CurrentProject && !m_CurrentProject->GetName().empty()
			? m_CurrentProject->GetName() : "TomCat Editor";
		std::string sceneName = "Untitled";
		if (!m_EditorScenePath.empty())
			sceneName = PathToUTF8(m_EditorScenePath.stem());
		else if (m_EditorScene)
		{
			sceneName = m_EditorScene->GetSceneName();
		}
		if (sceneName.empty())
			sceneName = "Untitled";

		const char* rendererName = Renderer::GetAPI() == RendererAPI::API::OpenGL
			? "OpenGL" : "No Renderer";
		std::string title = projectName + " - " + sceneName;
		if (m_SceneDirty)
			title += '*';
		title += " - Windows, Mac, Linux - TomCat Editor";
		if (m_CurrentProject && !m_CurrentProject->GetEditorVersion().empty())
		{
			title += ' ';
			title += m_CurrentProject->GetEditorVersion();
		}
		title += " <";
		title += rendererName;
		title += '>';

		if (title == m_LastWindowTitle)
			return;
		Application::Get().GetWindow().SetTitle(title);
		m_LastWindowTitle = std::move(title);
	}

	void EditorLayer::FocusEditorPanel(const char* panelName, bool& panelVisible)
	{
		panelVisible = true;
		m_PendingPanelFocus = panelName ? panelName : "";
		const std::string_view name = panelName ? panelName : "";
		if (name == "Game") m_EditorPanelCycleIndex = 1;
		else if (name == "Hierarchy") m_EditorPanelCycleIndex = 2;
		else if (name == "Inspector") m_EditorPanelCycleIndex = 3;
		else if (name == "Project") m_EditorPanelCycleIndex = 4;
		else if (name == "Scene") m_EditorPanelCycleIndex = 5;
		else if (name == "Console") m_EditorPanelCycleIndex = 6;
	}

	void EditorLayer::CycleEditorPanel(int direction)
	{
		constexpr int panelCount = 7;
		if (direction == 0)
			return;

		auto isPanelOpen = [this](int index)
		{
			switch (index)
			{
				case 0: return m_ShowBuildSettingsPanel;
				case 1: return m_ShowGamePanel;
				case 2: return m_ShowHierarchyPanel;
				case 3: return m_ShowInspectorPanel;
				case 4: return m_ShowProjectPanel;
				case 5: return m_ShowScenePanel;
				case 6: return m_ShowConsolePanel;
				default: return false;
			}
		};

		for (int step = 1; step <= panelCount; ++step)
		{
			const int delta = direction > 0 ? step : -step;
			const int candidate = (m_EditorPanelCycleIndex + delta +
				panelCount * 2) % panelCount;
			if (!isPanelOpen(candidate))
				continue;

			m_EditorPanelCycleIndex = candidate;
			switch (candidate)
			{
				case 0: m_FocusBuildSettingsPanel = true; break;
				case 1: FocusEditorPanel("Game", m_ShowGamePanel); break;
				case 2: FocusEditorPanel("Hierarchy", m_ShowHierarchyPanel); break;
				case 3: FocusEditorPanel("Inspector", m_ShowInspectorPanel); break;
				case 4: FocusEditorPanel("Project", m_ShowProjectPanel); break;
				case 5: FocusEditorPanel("Scene", m_ShowScenePanel); break;
				case 6: FocusEditorPanel("Console", m_ShowConsolePanel); break;
			}
			return;
		}
	}

	void EditorLayer::UI_MainMenuBar()
	{
		const ImVec4 menuText(0.055f, 0.065f, 0.080f, 1.0f);
		const ImVec4 menuTextDisabled(0.48f, 0.50f, 0.54f, 1.0f);
		const ImVec4 menuSurface(0.985f, 0.988f, 0.992f, 1.0f);
		const ImVec4 menuSelected(0.925f, 0.935f, 0.948f, 1.0f);
		const ImVec4 menuHover(0.855f, 0.918f, 0.980f, 1.0f);
		const ImVec4 menuActive(0.785f, 0.875f, 0.965f, 1.0f);
		const ImVec4 menuLine(0.78f, 0.80f, 0.83f, 1.0f);
		ImGui::PushStyleColor(ImGuiCol_Text, menuText);
		ImGui::PushStyleColor(ImGuiCol_TextDisabled, menuTextDisabled);
		ImGui::PushStyleColor(ImGuiCol_PopupBg, menuSurface);
		ImGui::PushStyleColor(ImGuiCol_Header, menuSelected);
		ImGui::PushStyleColor(ImGuiCol_HeaderHovered, menuHover);
		ImGui::PushStyleColor(ImGuiCol_HeaderActive, menuActive);
		ImGui::PushStyleColor(ImGuiCol_Separator, menuLine);
		ImGui::PushStyleColor(ImGuiCol_Border, menuLine);
		ImGui::PushStyleColor(ImGuiCol_CheckMark, menuText);
		ImGui::PushStyleVar(ImGuiStyleVar_PopupRounding, 1.0f);
		ImGui::PushStyleVar(ImGuiStyleVar_PopupBorderSize, 1.0f);

		if (ImGui::BeginMenuBar())
		{
			if (ImGui::BeginMenu("File"))
			{
				if (ImGui::MenuItem("Open Project..."))
					OpenProject();
				if (ImGui::MenuItem("Save Project"))
					SaveProject();
				if (ImGui::MenuItem("Build Settings..."))
				{
					m_ShowBuildSettingsPanel = true;
					m_FocusBuildSettingsPanel = true;
					m_EditorPanelCycleIndex = 0;
				}
				ImGui::Separator();
				if (ImGui::MenuItem("New Scene", "Ctrl+N"))
					NewScene();
				if (ImGui::MenuItem("Open Scene...", "Ctrl+O"))
					OpenScene();
				if (ImGui::MenuItem("Save Scene", "Ctrl+S"))
					SaveScene();
				if (ImGui::MenuItem("Save Scene As...", "Ctrl+Shift+S"))
					SaveSceneAs();
				ImGui::Separator();
				if (ImGui::MenuItem("Exit"))
					RequestExit();
				ImGui::EndMenu();
			}

			if (ImGui::BeginMenu("Edit"))
			{
				ImGui::MenuItem("Undo", "Ctrl+Z", false, false);
				ImGui::MenuItem("Redo", "Ctrl+Y", false, false);
				ImGui::Separator();
				if (ImGui::MenuItem("Project Settings..."))
					OpenProjectSettingsPanel();
				ImGui::EndMenu();
			}

			if (ImGui::BeginMenu("Assets", m_CurrentProject != nullptr))
			{
				m_ContentBrowserPanel.DrawAssetsMenu();
				ImGui::Separator();
				if (ImGui::MenuItem("Compile C# Scripts", nullptr, false,
					!IsSceneRunning() && !m_ScriptCompiler.IsCompileInProgress()))
				{
					m_ShowConsolePanel = true;
					m_PendingPanelFocus = "Console";
					m_ScriptCompileDebounceRemaining = 0.65f;
					m_ScriptCompiler.StartCompile(true);
				}
				ImGui::EndMenu();
			}

			if (ImGui::BeginMenu("GameObject", m_ActiveScene != nullptr))
			{
				if (m_SceneHierarchyPanel.DrawGameObjectMenu())
					FocusEditorPanel("Hierarchy", m_ShowHierarchyPanel);
				ImGui::EndMenu();
			}

			if (ImGui::BeginMenu("Component"))
			{
				ImGui::MenuItem("Add Component...", nullptr, false, false);
				ImGui::TextDisabled("Use Add Component in the Inspector.");
				ImGui::EndMenu();
			}

			auto drawUnavailableMenu = [](const char* label, const char* message)
			{
				if (!ImGui::BeginMenu(label))
					return;
				ImGui::MenuItem(message, nullptr, false, false);
				ImGui::EndMenu();
			};
			drawUnavailableMenu("Services", "No services configured");
			drawUnavailableMenu("Jobs", "No jobs available");
			drawUnavailableMenu("Tools", "No additional tools installed");

			if (ImGui::BeginMenu("Window"))
			{
				if (ImGui::BeginMenu("Panels"))
				{
					const bool hasFloatingPanel = m_ShowBuildSettingsPanel || m_ShowProjectSettingsPanel ||
						(m_ShowScenePanel && !m_ScenePanelDocked) ||
						(m_ShowGamePanel && !m_GamePanelDocked) ||
						(m_ShowHierarchyPanel && !m_SceneHierarchyPanel.IsHierarchyDocked()) ||
						(m_ShowInspectorPanel && !m_SceneHierarchyPanel.IsInspectorDocked()) ||
						(m_ShowProjectPanel && !m_ContentBrowserPanel.IsDocked()) ||
						(m_ShowConsolePanel && !m_ConsolePanel.IsDocked());
					if (ImGui::MenuItem("Close all floating panels...", nullptr, false,
						hasFloatingPanel))
					{
						m_ShowBuildSettingsPanel = false;
						m_ShowProjectSettingsPanel = false;
						if (!m_ScenePanelDocked) m_ShowScenePanel = false;
						if (!m_GamePanelDocked) m_ShowGamePanel = false;
						if (!m_SceneHierarchyPanel.IsHierarchyDocked()) m_ShowHierarchyPanel = false;
						if (!m_SceneHierarchyPanel.IsInspectorDocked()) m_ShowInspectorPanel = false;
						if (!m_ContentBrowserPanel.IsDocked()) m_ShowProjectPanel = false;
						if (!m_ConsolePanel.IsDocked()) m_ShowConsolePanel = false;
					}
					ImGui::Separator();
					ImGui::MenuItem("1 Animator", nullptr, false, false);
					if (ImGui::MenuItem("2 Build Settings"))
					{
						m_ShowBuildSettingsPanel = true;
						m_FocusBuildSettingsPanel = true;
						m_EditorPanelCycleIndex = 0;
					}
					if (ImGui::MenuItem("3 Console"))
						FocusEditorPanel("Console", m_ShowConsolePanel);
					if (ImGui::MenuItem("4 Game"))
						FocusEditorPanel("Game", m_ShowGamePanel);
					if (ImGui::MenuItem("5 Hierarchy"))
						FocusEditorPanel("Hierarchy", m_ShowHierarchyPanel);
					if (ImGui::MenuItem("6 Inspector"))
						FocusEditorPanel("Inspector", m_ShowInspectorPanel);
					if (ImGui::MenuItem("7 Project"))
						FocusEditorPanel("Project", m_ShowProjectPanel);
					if (ImGui::MenuItem("8 Scene"))
						FocusEditorPanel("Scene", m_ShowScenePanel);
					ImGui::EndMenu();
				}
				ImGui::Separator();
				if (ImGui::MenuItem("Next Window", "Ctrl+Tab"))
					CycleEditorPanel(1);
				if (ImGui::MenuItem("Previous Window", "Ctrl+Shift+Tab"))
					CycleEditorPanel(-1);
				ImGui::Separator();
				if (ImGui::BeginMenu("Layouts"))
				{
					ImGui::MenuItem("Current Layout", nullptr, true, false);
					ImGui::EndMenu();
				}
				ImGui::Separator();
				ImGui::MenuItem("Version Control", nullptr, false, false);
				ImGui::BeginMenu("Search", false);
				ImGui::Separator();
				ImGui::MenuItem("Asset Store", nullptr, false, false);
				ImGui::MenuItem("Package Manager", nullptr, false, false);
				ImGui::EndMenu();
			}

			if (ImGui::BeginMenu("Help"))
			{
				ImGui::MenuItem("TomCat Editor", nullptr, false, false);
				ImGui::EndMenu();
			}

			ImGui::EndMenuBar();
		}

		ImGui::PopStyleVar(2);
		ImGui::PopStyleColor(9);
	}

	void EditorLayer::OnImGuiRender()
	{
		TC_PROFILE_FUNCTION();
		UpdateWindowTitle();

		static bool dockspaceOpen = true;
		static bool opt_fullscreen_persistant = true;
		bool opt_fullscreen = opt_fullscreen_persistant;
		static ImGuiDockNodeFlags dockspace_flags = ImGuiDockNodeFlags_None;

		ImGuiWindowFlags window_flags = ImGuiWindowFlags_MenuBar | ImGuiWindowFlags_NoDocking;
		if (opt_fullscreen)
		{
			ImGuiViewport* viewport = ImGui::GetMainViewport();
			ImGui::SetNextWindowPos(viewport->Pos);
			ImGui::SetNextWindowSize(viewport->Size);
			ImGui::SetNextWindowViewport(viewport->ID);
			ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
			ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
			window_flags |= ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove;
			window_flags |= ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoNavFocus;
		}

		if (dockspace_flags & ImGuiDockNodeFlags_PassthruCentralNode)
			window_flags |= ImGuiWindowFlags_NoBackground;

		ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
		ImGui::PushStyleColor(ImGuiCol_MenuBarBg, ImVec4(0.965f, 0.972f, 0.980f, 1.0f));
		ImGui::Begin("DockSpace Demo", &dockspaceOpen, window_flags);
		ImGui::PopStyleColor();
		ImGui::PopStyleVar();

		if (opt_fullscreen)
			ImGui::PopStyleVar(2);

		UI_MainMenuBar();

		float toolbarHeight = 48.0f;
		// Unity keeps the global playbar one step darker than docked panels.
		ImGui::PushStyleColor(ImGuiCol_ChildBg, ImGui::GetStyle().Colors[ImGuiCol_TitleBg]);
		ImGui::BeginChild("ToolbarRegion", ImVec2(0, toolbarHeight), false,
			ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
		UI_Toolbar();
		ImGui::EndChild();
		ImGui::PopStyleColor();

		ImGui::Separator();

		ImGui::BeginChild("DockSpaceRegion", ImVec2(0, 0), false,
			ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);

		ImGuiIO& io = ImGui::GetIO();
		ImGuiStyle& style = ImGui::GetStyle();
		float minWinSizeX = style.WindowMinSize.x;
		style.WindowMinSize.x = 370.0f;

		if (io.ConfigFlags & ImGuiConfigFlags_DockingEnable)
		{
			ImGuiID dockspace_id = ImGui::GetID("MyDockSpace");
			ImGui::DockSpace(dockspace_id, ImVec2(0.0f, 0.0f), dockspace_flags);
		}

		style.WindowMinSize.x = minWinSizeX;

		ImGui::EndChild(); 

		m_SceneHierarchyPanel.SetColliderEditingAllowed(m_SceneState == SceneState::Edit);
		m_SceneHierarchyPanel.OnImGuiRender(&m_ShowHierarchyPanel, &m_ShowInspectorPanel);
		if (m_SceneHierarchyPanel.IsHierarchyFocused())
			m_EditorPanelCycleIndex = 2;
		else if (m_SceneHierarchyPanel.IsInspectorFocused())
			m_EditorPanelCycleIndex = 3;
		m_ContentBrowserPanel.OnImGuiRender(&m_ShowProjectPanel);
		if (m_ContentBrowserPanel.IsFocused())
			m_EditorPanelCycleIndex = 4;
		m_ConsolePanel.OnImGuiRender(&m_ShowConsolePanel);
		if (m_ConsolePanel.IsFocused())
			m_EditorPanelCycleIndex = 6;

		if (m_ShowScenePanel)
		{
		ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2{ 0, 0 });

		// Use the same native menu-bar slot as Hierarchy.  ImGui's dock tab and
		// menu-bar layout then share one geometry source, eliminating the hand-
		// positioned gap that appeared with the custom Scene strip.
		const char* sceneTitle = m_SceneDirty ? "Scene *###Scene" : "Scene###Scene";
		const bool sceneVisible = ImGui::Begin(sceneTitle, &m_ShowScenePanel, ImGuiWindowFlags_MenuBar);
		m_ScenePanelDocked = ImGui::IsWindowDocked();
		if (!sceneVisible)
		{
			m_ViewportFocused = false;
			m_HoveredEntity = {};
		}

		auto viewportMinRegion = ImGui::GetWindowContentRegionMin();
		auto viewportMaxRegion = ImGui::GetWindowContentRegionMax();
		auto viewportOffset = ImGui::GetWindowPos();
		m_ViewportBounds[0] = { viewportMinRegion.x + viewportOffset.x, viewportMinRegion.y + viewportOffset.y };
		m_ViewportBounds[1] = { viewportMaxRegion.x + viewportOffset.x, viewportMaxRegion.y + viewportOffset.y };
		// The content origin is the bottom edge of the native MenuBar.  Derive the
		// dock row from that edge so the custom Scene toolbar occupies the actual
		// menu-bar slot (and remains vertically centered at every font/DPI scale).
		m_GizmoModeDockHeight = ImGui::GetFrameHeight();
		m_GizmoModeDockY = m_ViewportBounds[0].y - m_GizmoModeDockHeight;

		m_ViewportFocused = ImGui::IsWindowFocused();
		if (m_ViewportFocused)
			m_EditorPanelCycleIndex = 5;

		ImVec2 viewportPanelSize = ImGui::GetContentRegionAvail();
		m_ViewportSize = { viewportPanelSize.x, viewportPanelSize.y };

		uint64_t sceneTextureID = m_Framebuffer->GetColorAttachmentRendererID();
		ImGui::Image(reinterpret_cast<void*>(sceneTextureID), ImVec2{ m_ViewportSize.x, m_ViewportSize.y },
			ImVec2{ 0, 1 }, ImVec2{ 1, 0 });
		m_ViewportCanvasHovered = sceneVisible && ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenBlockedByActiveItem);
		Application::Get().GetImGuiLayer()->BlockEvents(!m_ViewportCanvasHovered && !m_ViewportCameraDragOwned);

		// The framebuffer image must remain the current ImGui item while registering
		// its drop target. Toolbar items submitted later must never steal the target.
		if (ImGui::BeginDragDropTarget())
		{
			const ImGuiDragDropFlags flags = ImGuiDragDropFlags_AcceptNoDrawDefaultRect;
			if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload(AssetDragDropPayloadID, flags))
			{
				if (payload->DataSize == sizeof(uint64_t) && m_ActiveScene)
				{
					const AssetHandle handle(*static_cast<const uint64_t*>(payload->Data));
					const AssetMetadata* metadata = AssetManager::Get().GetRegistry().GetMetadata(handle);
					if (metadata && metadata->Type == AssetType::Texture2D)
					{
						Entity sprite = m_ActiveScene->CreateEntity(PathToUTF8(metadata->FilePath.stem()));
						auto& renderer = sprite.AddComponent<SpriteRenderer>(glm::vec4{ 1.0f });
						renderer.SpriteHandle = handle;
						renderer.Sprite = AssetManager::Get().LoadTexture(handle);
						if (m_SceneState == SceneState::Edit)
							m_SceneDirty = true;
					}
				}
			}
			ImGui::EndDragDropTarget();
		}

		// Draw the mode bar in the foreground layer for both docked and floating
		// states.  The docked position is still computed from the Scene row, while
		// the foreground draw list keeps it above that row during drag operations.
		// Foreground primitives are global.  Do not submit ordinary overlays while
		// the Scene tab is hidden; an active drag is the one exception below so the
		// toolbar remains visible while crossing the tab bar.
		// Keep rendering an active drag even when docking temporarily marks the
		// Scene tab hidden (for example while the cursor crosses the Scene/Game
		// tab bar).  Otherwise the drag state has no frame in which to paint and
		// the anchor toolbar appears to disappear.
		if (sceneVisible || m_GizmoModeToolbarDragging || m_GizmoTransformToolbarDragging)
		{
			// BeginMenuBar switches ImGui to the same full-width clip/layout region
			// used by Hierarchy.  Submit the custom controls after the framebuffer
			// image so their foreground draw order remains above the Scene content.
			if (sceneVisible && ImGui::BeginMenuBar())
			{
				// MenuBarBg is intentionally the darker foundation.  Scene's toolbar
				// row is an explicit lighter overlay, painted before the controls so
				// an empty dock still shows the complete top-bar surface.
				ImGui::GetWindowDrawList()->AddRectFilled(
					ImVec2(m_ViewportBounds[0].x, m_GizmoModeDockY),
					ImVec2(m_ViewportBounds[1].x, m_GizmoModeDockY + m_GizmoModeDockHeight),
					ImGui::GetColorU32(ImGuiCol_Tab));
				UI_SceneGizmoModeToolbarOverlay();
				UI_SceneGizmoToolbar();
				UI_SceneToolbarDockPreview();
				UI_SceneColliderVisibilityToggle();
				ImGui::EndMenuBar();
			}
			else
			{
				// Keep the overlay alive if the native menu-bar slot is temporarily
				// unavailable (for example while the tab is being hidden during a drag).
				UI_SceneGizmoModeToolbarOverlay();
				UI_SceneGizmoToolbar();
				UI_SceneToolbarDockPreview();
			}
		}

		// Gizmos
		Entity selectedEntity = m_SceneHierarchyPanel.GetSelectedEntity();

		if (selectedEntity && m_GizmoType != -1 && !m_SceneHierarchyPanel.IsEditingCollider())
		{
			ImGuizmo::AllowAxisFlip(false);
			ImGuizmo::SetOrthographic(false);
			ImGuizmo::SetDrawlist();

			ImGuizmo::SetRect(m_ViewportBounds[0].x, m_ViewportBounds[0].y,
				m_ViewportBounds[1].x - m_ViewportBounds[0].x,
				m_ViewportBounds[1].y - m_ViewportBounds[0].y);

			const glm::mat4& cameraProjection = m_EditorCamera.GetProjection();
			glm::mat4 cameraView = m_EditorCamera.GetViewMatrix();

			if (selectedEntity.HasComponent<Transform>())
			{
				auto& tc = selectedEntity.GetComponent<Transform>();
				glm::mat4 transform = tc.GetTransform();

				bool snap = Input::IsKeyPressed(Key::LeftControl);
				float snapValue = 0.5f;
				if (m_GizmoType == ImGuizmo::OPERATION::ROTATE)
					snapValue = 45.0f;

				float snapValues[3] = { snapValue, snapValue, snapValue };

				ImGuizmo::Manipulate(glm::value_ptr(cameraView), glm::value_ptr(cameraProjection),
					(ImGuizmo::OPERATION)m_GizmoType,
					m_GizmoSpaceMode == GizmoSpaceMode::Local ? ImGuizmo::LOCAL : ImGuizmo::WORLD,
					glm::value_ptr(transform),
					nullptr, snap ? snapValues : nullptr);

				if (ImGuizmo::IsUsing())
				{
					if (m_ActiveScene->SetWorldTransform(selectedEntity, transform)
						&& m_SceneState == SceneState::Edit)
						m_SceneDirty = true;
				}
			}
		}

		if (sceneVisible)
			UI_ColliderEditHandles();
		else
			ResetColliderEditState();

		ImGui::End();
		ImGui::PopStyleVar();
		}
		else
		{
			m_ViewportFocused = false;
			m_ViewportCanvasHovered = false;
			m_ViewportCameraDragOwned = false;
			m_HoveredEntity = {};
		}

		if (m_ShowGamePanel)
		{
		ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2{ 0, 0 });

		ImGui::Begin("Game", &m_ShowGamePanel, ImGuiWindowFlags_MenuBar);
		m_GamePanelDocked = ImGui::IsWindowDocked();
		if (ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows))
			m_EditorPanelCycleIndex = 1;

		if (ImGui::BeginMenuBar())
		{
			// Stats is a toolbar control in the Game view, not a text-only menu
			// entry.  Give it the same full frame height as the neighboring Unity
			// controls while keeping enough width for the current font/DPI scale.
			const ImGuiStyle& gameStyle = ImGui::GetStyle();
			const ImVec2 statsLabelSize = ImGui::CalcTextSize("Stats");
			const float statsButtonHeight = ImGui::GetFrameHeight();
			const float statsButtonWidth = std::max(69.0f,
				statsLabelSize.x + gameStyle.FramePadding.x * 2.0f);
			// MenuBarBg is the dark foundation now; toolbar controls keep the
			// lighter neutral surface used by the existing editor chrome.
			const ImVec4 statsSurface = gameStyle.Colors[ImGuiCol_Tab];
			const ImVec4 statsHovered = gameStyle.Colors[ImGuiCol_ButtonHovered];
			const ImVec4 statsActive = gameStyle.Colors[ImGuiCol_ButtonActive];
			ImGui::PushStyleColor(ImGuiCol_Button, statsSurface);
			ImGui::PushStyleColor(ImGuiCol_ButtonHovered, statsHovered);
			ImGui::PushStyleColor(ImGuiCol_ButtonActive, statsActive);
			const bool statsPressed = ImGui::Button("Stats##GameStatsButton",
				ImVec2(statsButtonWidth, statsButtonHeight));
			ImGui::PopStyleColor(3);
			const ImVec2 statsPopupPos(ImGui::GetItemRectMin().x,
				ImGui::GetItemRectMax().y);

			if (statsPressed)
				ImGui::OpenPopup("##GameStatsPopup");

			ImGui::SetNextWindowPos(statsPopupPos, ImGuiCond_Appearing);
			if (ImGui::BeginPopup("##GameStatsPopup"))
			{
				std::string name = "None";
				if (m_HoveredEntity)
					name = m_HoveredEntity.GetComponent<Tag>()._Tag;
				ImGui::Text("Hovered Entity : %s", name.c_str());

				auto stats = Renderer2D::GetStats();
				ImGui::Text("Stats:");
				ImGui::Text("Draw Calls: %d", stats.DrawCalls);
				ImGui::Text("Quads: %d", stats.QuadCount);
				ImGui::Text("Circles: %d", stats.CircleCount);
				ImGui::Text("Lines: %d", stats.LineCount);
				ImGui::Text("Vertices: %d", stats.GetTotalVertexCount());
				ImGui::Text("Indices: %d", stats.GetTotalIndexCount());

				ImGui::EndPopup();
			}
			ImGui::EndMenuBar();
		}

		ImVec2 gameViewportPanelSize = ImGui::GetContentRegionAvail();
		m_GameViewportSize = { gameViewportPanelSize.x, gameViewportPanelSize.y };

		// 始终显示GameFramebuffer（Runtime摄像机渲染内容）
		uint64_t gameTextureID = m_GameFramebuffer->GetColorAttachmentRendererID();
		ImGui::Image(reinterpret_cast<void*>(gameTextureID), ImVec2{ m_GameViewportSize.x, m_GameViewportSize.y },
			ImVec2{ 0, 1 }, ImVec2{ 1, 0 });
		UI_GameNoCameraOverlay();

		ImGui::End();
		ImGui::PopStyleVar();
		}

		UI_BuildSettings();
		UI_ProjectSettings();
		UI_UnsavedChangesModal();
		if (!m_PendingPanelFocus.empty())
		{
			ImGui::SetWindowFocus(m_PendingPanelFocus.c_str());
			m_PendingPanelFocus.clear();
		}
		ImGui::End();
	}

	void EditorLayer::UI_GameNoCameraOverlay()
	{
		if (!m_ActiveScene || m_ActiveScene->GetPrimaryCameraEntity())
			return;

		ImVec2 imageMin = ImGui::GetItemRectMin();
		ImVec2 imageMax = ImGui::GetItemRectMax();
		ImVec2 imageCenter((imageMin.x + imageMax.x) * 0.5f, (imageMin.y + imageMax.y) * 0.5f);
		const float panelWidth = std::min(520.0f, std::max(300.0f, imageMax.x - imageMin.x - 40.0f));
		const float panelHeight = 112.0f;
		ImVec2 panelMin(imageCenter.x - panelWidth * 0.5f, imageCenter.y - panelHeight * 0.5f);
		ImVec2 panelMax(imageCenter.x + panelWidth * 0.5f, imageCenter.y + panelHeight * 0.5f);

		ImDrawList* draw = ImGui::GetWindowDrawList();
		// Unity's empty Game view uses a soft rounded notification card; keep its
		// larger radius while moving the fill into the reference gray ramp.
		draw->AddRectFilled(panelMin, panelMax, IM_COL32(98, 98, 98, 235), 18.0f);
		draw->AddRect(panelMin, panelMax, IM_COL32(140, 140, 140, 255), 18.0f, 0, 1.0f);

		const char* messageText = "No cameras rendering";
		ImVec2 messageSize = ImGui::CalcTextSize(messageText);
		ImVec2 messagePos(imageCenter.x - messageSize.x * 0.5f, imageCenter.y - messageSize.y * 0.5f);
		draw->AddText(messagePos, IM_COL32(243, 243, 243, 255), messageText);
	}

	void EditorLayer::UI_BuildSettings()
	{
		if (!m_ShowBuildSettingsPanel)
			return;

		if (m_FocusBuildSettingsPanel)
		{
			ImGui::SetNextWindowFocus();
			m_FocusBuildSettingsPanel = false;
		}
		ImGui::SetNextWindowSize(ImVec2(900.0f, 720.0f), ImGuiCond_FirstUseEver);
		ImGui::SetNextWindowSizeConstraints(ImVec2(720.0f, 560.0f), ImVec2(1600.0f, 1200.0f));
		const bool buildSettingsVisible = ImGui::Begin("Build Settings",
			&m_ShowBuildSettingsPanel, ImGuiWindowFlags_NoDocking);
		if (ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows))
			m_EditorPanelCycleIndex = 0;
		if (!buildSettingsVisible)
		{
			ImGui::End();
			return;
		}

		const ProjectConfig* config = m_CurrentProject ? &m_CurrentProject->GetConfig() : nullptr;
		const bool hasConfiguredStartScene = config
			&& static_cast<uint64_t>(config->StartSceneHandle) != 0;
		const AssetMetadata* startSceneMetadata = hasConfiguredStartScene
			? AssetManager::Get().GetRegistry().GetMetadata(config->StartSceneHandle) : nullptr;
		const bool startSceneReady = startSceneMetadata && !startSceneMetadata->IsMissing
			&& startSceneMetadata->Type == AssetType::Scene;

		if (!m_CurrentProject)
			ImGui::TextColored(ImVec4(0.95f, 0.72f, 0.25f, 1.0f),
				"Open a project to inspect its scenes and Player settings.");

		ImGui::TextUnformatted("Scenes In Build");
		ImGui::BeginChild("##ScenesInBuild", ImVec2(0.0f, 145.0f), true,
			ImGuiWindowFlags_HorizontalScrollbar);
		if (hasConfiguredStartScene)
		{
			const std::string scenePath = PathToUTF8(config->StartScene);
			std::string sceneName = PathToUTF8(config->StartScene.stem());
			if (sceneName.empty())
				sceneName = scenePath.empty() ? "<Unresolved start scene>" : scenePath;

			const ImGuiTableFlags sceneFlags = ImGuiTableFlags_RowBg |
				ImGuiTableFlags_BordersInnerH | ImGuiTableFlags_SizingStretchProp;
			if (ImGui::BeginTable("##SceneBuildRows", 3, sceneFlags))
			{
				ImGui::TableSetupColumn("Enabled", ImGuiTableColumnFlags_WidthFixed, 30.0f);
				ImGui::TableSetupColumn("Scene", ImGuiTableColumnFlags_WidthStretch);
				ImGui::TableSetupColumn("Index", ImGuiTableColumnFlags_WidthFixed, 32.0f);
				ImGui::TableNextRow(0, ImGui::GetFrameHeightWithSpacing());
				ImGui::TableSetColumnIndex(0);
				bool included = startSceneReady;
				ImGui::BeginDisabled();
				ImGui::Checkbox("##StartSceneIncluded", &included);
				ImGui::EndDisabled();
				ImGui::TableSetColumnIndex(1);
				ImGui::AlignTextToFramePadding();
				if (startSceneReady)
					ImGui::TextUnformatted(sceneName.c_str());
				else
					ImGui::TextColored(ImVec4(0.95f, 0.35f, 0.35f, 1.0f), "%s (Missing)",
						sceneName.c_str());
				if (!scenePath.empty() && ImGui::IsItemHovered(ImGuiHoveredFlags_DelayNormal))
					ImGui::SetTooltip("%s", scenePath.c_str());
				ImGui::TableSetColumnIndex(2);
				ImGui::AlignTextToFramePadding();
				const char* indexText = "0";
				ImGui::SetCursorPosX(ImGui::GetCursorPosX() + std::max(0.0f,
					ImGui::GetContentRegionAvail().x - ImGui::CalcTextSize(indexText).x));
				ImGui::TextUnformatted(indexText);
				ImGui::EndTable();
			}
		}
		else
			ImGui::TextDisabled(m_CurrentProject
				? "No start scene is configured." : "No project is open.");
		ImGui::EndChild();

		const float addOpenScenesWidth = ImGui::CalcTextSize("Add Open Scenes").x
			+ ImGui::GetStyle().FramePadding.x * 2.0f;
		ImGui::SetCursorPosX(std::max(ImGui::GetCursorPosX(),
			ImGui::GetWindowContentRegionMax().x - addOpenScenesWidth));
		ImGui::BeginDisabled();
		ImGui::Button("Add Open Scenes");
		ImGui::EndDisabled();
		if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
			ImGui::SetTooltip("The build-scene list will be editable when Player export is implemented.");

		ImGui::TextUnformatted("Platform");
		const float platformHeight = std::clamp(ImGui::GetContentRegionAvail().y - 150.0f,
			245.0f, 355.0f);
		const ImGuiTableFlags platformFlags = ImGuiTableFlags_Resizable |
			ImGuiTableFlags_BordersInnerV | ImGuiTableFlags_SizingStretchProp;
		if (ImGui::BeginTable("##BuildPlatformColumns", 2, platformFlags,
			ImVec2(0.0f, platformHeight)))
		{
			ImGui::TableSetupColumn("Targets", ImGuiTableColumnFlags_WidthStretch, 0.42f);
			ImGui::TableSetupColumn("Options", ImGuiTableColumnFlags_WidthStretch, 0.58f);
			ImGui::TableNextRow();
			ImGui::TableSetColumnIndex(0);
			ImGui::BeginChild("##PlatformList", ImVec2(0.0f, platformHeight), true);

			auto drawPlatformGlyph = [](int kind, const ImVec2& rowMinimum,
				float rowHeight, ImU32 color)
			{
				ImDrawList* draw = ImGui::GetWindowDrawList();
				const float left = rowMinimum.x + 10.0f;
				const float centerY = rowMinimum.y + rowHeight * 0.5f;
				if (kind == 0)
				{
					draw->AddRect(ImVec2(left, centerY - 8.0f), ImVec2(left + 24.0f, centerY + 6.0f),
						color, 2.0f, 0, 1.5f);
					draw->AddLine(ImVec2(left + 12.0f, centerY + 6.0f),
						ImVec2(left + 12.0f, centerY + 10.0f), color, 1.5f);
					draw->AddLine(ImVec2(left + 7.0f, centerY + 10.0f),
						ImVec2(left + 17.0f, centerY + 10.0f), color, 1.5f);
				}
				else if (kind == 1)
				{
					for (int row = -1; row <= 1; ++row)
					{
						const float top = centerY + static_cast<float>(row) * 7.0f - 2.5f;
						draw->AddRect(ImVec2(left, top), ImVec2(left + 24.0f, top + 5.0f),
							color, 1.0f, 0, 1.2f);
						draw->AddCircleFilled(ImVec2(left + 4.0f, top + 2.5f), 1.0f, color);
					}
				}
				else if (kind == 2)
				{
					draw->AddRect(ImVec2(left + 5.0f, centerY - 11.0f),
						ImVec2(left + 19.0f, centerY + 11.0f), color, 2.0f, 0, 1.5f);
					draw->AddCircleFilled(ImVec2(left + 12.0f, centerY + 8.0f), 1.0f, color);
				}
				else if (kind == 3)
				{
					draw->AddCircle(ImVec2(left + 12.0f, centerY), 10.0f, color, 16, 1.3f);
					draw->AddLine(ImVec2(left + 2.0f, centerY),
						ImVec2(left + 22.0f, centerY), color, 1.0f);
					draw->AddLine(ImVec2(left + 12.0f, centerY - 10.0f),
						ImVec2(left + 12.0f, centerY + 10.0f), color, 1.0f);
				}
				else
				{
					draw->AddRect(ImVec2(left + 1.0f, centerY - 7.0f),
						ImVec2(left + 23.0f, centerY + 7.0f), color, 2.0f, 0, 1.3f);
				}
			};

			auto drawPlatformRow = [&](const char* label, bool selected, bool disabled, int glyph)
			{
				ImGui::PushID(label);
				if (disabled)
					ImGui::BeginDisabled();
				ImGui::Selectable("##PlatformTarget", selected, ImGuiSelectableFlags_None,
					ImVec2(0.0f, 39.0f));
				const ImVec2 rowMinimum = ImGui::GetItemRectMin();
				const float rowHeight = ImGui::GetItemRectSize().y;
				const ImVec4 textColor = ImGui::GetStyleColorVec4(disabled
					? ImGuiCol_TextDisabled : ImGuiCol_Text);
				const ImU32 packedColor = ImGui::ColorConvertFloat4ToU32(textColor);
				drawPlatformGlyph(glyph, rowMinimum, rowHeight, packedColor);
				const ImVec2 textSize = ImGui::CalcTextSize(label);
				ImGui::GetWindowDrawList()->AddText(
					ImVec2(rowMinimum.x + 43.0f,
						rowMinimum.y + (rowHeight - textSize.y) * 0.5f), packedColor, label);
				if (disabled)
					ImGui::EndDisabled();
				ImGui::PopID();
			};

			drawPlatformRow("Windows, Mac, Linux", true, false, 0);
			drawPlatformRow("Dedicated Server", false, true, 1);
			drawPlatformRow("Android", false, true, 2);
			drawPlatformRow("iOS", false, true, 2);
			drawPlatformRow("PS4", false, true, 4);
			drawPlatformRow("PS5", false, true, 4);
			drawPlatformRow("WebGL", false, true, 3);
			drawPlatformRow("Universal Windows Platform", false, true, 0);
			ImGui::EndChild();

			ImGui::TableSetColumnIndex(1);
			ImGui::BeginChild("##PlatformOptions", ImVec2(0.0f, platformHeight), false);
			const ImVec2 headerStart = ImGui::GetCursorScreenPos();
			ImGui::Dummy(ImVec2(34.0f, 28.0f));
			drawPlatformGlyph(0, headerStart, 28.0f,
				ImGui::GetColorU32(ImGuiCol_Text));
			ImGui::SameLine();
			ImGui::AlignTextToFramePadding();
			ImGui::TextUnformatted("Windows, Mac, Linux");
			ImGui::Separator();

			const ImGuiTableFlags optionFlags = ImGuiTableFlags_SizingStretchProp;
			if (ImGui::BeginTable("##BuildTargetOptions", 2, optionFlags))
			{
				ImGui::TableSetupColumn("Label", ImGuiTableColumnFlags_WidthStretch, 0.55f);
				ImGui::TableSetupColumn("Value", ImGuiTableColumnFlags_WidthStretch, 0.45f);
				auto drawDisabledCombo = [](const char* label, const char* id, const char* preview)
				{
					ImGui::TableNextRow();
					ImGui::TableSetColumnIndex(0);
					ImGui::AlignTextToFramePadding();
					ImGui::TextUnformatted(label);
					ImGui::TableSetColumnIndex(1);
					ImGui::SetNextItemWidth(-1.0f);
					ImGui::BeginDisabled();
					if (ImGui::BeginCombo(id, preview))
						ImGui::EndCombo();
					ImGui::EndDisabled();
				};
				auto drawDisabledCheckbox = [](const char* label, const char* id, bool muted)
				{
					ImGui::TableNextRow();
					ImGui::TableSetColumnIndex(0);
					if (muted)
						ImGui::TextDisabled("%s", label);
					else
						ImGui::TextUnformatted(label);
					ImGui::TableSetColumnIndex(1);
					bool value = false;
					ImGui::BeginDisabled();
					ImGui::Checkbox(id, &value);
					ImGui::EndDisabled();
				};

				drawDisabledCombo("Target Platform", "##TargetPlatform", "Windows");
				drawDisabledCombo("Architecture", "##Architecture", "Intel 64-bit");
				drawDisabledCheckbox("Copy PDB files", "##CopyPDB", false);
				drawDisabledCheckbox("Create Visual Studio Solution", "##CreateSolution", false);
				drawDisabledCheckbox("Development Build", "##DevelopmentBuild", false);
				drawDisabledCheckbox("Autoconnect Profiler", "##AutoconnectProfiler", true);
				drawDisabledCheckbox("Deep Profiling Support", "##DeepProfiling", true);
				drawDisabledCheckbox("Script Debugging", "##ScriptDebugging", true);
				drawDisabledCombo("Compression Method", "##CompressionMethod", "Default");
				ImGui::EndTable();
			}
			ImGui::EndChild();
			ImGui::EndTable();
		}

		if (ImGui::CollapsingHeader("Asset Import Overrides",
			ImGuiTreeNodeFlags_DefaultOpen))
		{
			if (ImGui::BeginTable("##AssetImportOverrides", 2,
				ImGuiTableFlags_SizingFixedFit))
			{
				ImGui::TableSetupColumn("Label", ImGuiTableColumnFlags_WidthFixed, 185.0f);
				ImGui::TableSetupColumn("Value", ImGuiTableColumnFlags_WidthFixed, 210.0f);
				const std::array<std::pair<const char*, const char*>, 2> overrides = {
					std::pair{ "Max Texture Size", "##MaxTextureSize" },
					std::pair{ "Texture Compression", "##TextureCompression" }
				};
				for (const auto& [label, id] : overrides)
				{
					ImGui::TableNextRow();
					ImGui::TableSetColumnIndex(0);
					ImGui::AlignTextToFramePadding();
					ImGui::TextUnformatted(label);
					ImGui::TableSetColumnIndex(1);
					ImGui::SetNextItemWidth(-1.0f);
					ImGui::BeginDisabled();
					if (ImGui::BeginCombo(id, "No Override"))
						ImGui::EndCombo();
					ImGui::EndDisabled();
				}
				ImGui::EndTable();
			}
		}

		ImGui::Separator();
		if (ImGui::Button("Player Settings..."))
			OpenProjectSettingsPanel();

		const float buildWidth = ImGui::CalcTextSize("Build").x
			+ ImGui::GetStyle().FramePadding.x * 2.0f + 22.0f;
		const float buildAndRunWidth = ImGui::CalcTextSize("Build And Run").x
			+ ImGui::GetStyle().FramePadding.x * 2.0f;
		const float buildButtonsWidth = buildWidth + buildAndRunWidth
			+ ImGui::GetStyle().ItemSpacing.x;
		ImGui::SameLine(std::max(ImGui::GetCursorPosX() + 20.0f,
			ImGui::GetWindowContentRegionMax().x - buildButtonsWidth));
		const bool canBuild = m_CurrentProject && startSceneReady &&
			!IsSceneRunning() && !m_ScriptCompiler.IsCompileInProgress();
		ImGui::BeginDisabled(!canBuild);
		const bool buildPressed = ImGui::Button("Build", ImVec2(buildWidth, 0.0f));
		const bool buildHovered = ImGui::IsItemHovered(
			ImGuiHoveredFlags_AllowWhenDisabled);
		ImGui::SameLine();
		const bool buildAndRunPressed = ImGui::Button("Build And Run",
			ImVec2(buildAndRunWidth, 0.0f));
		const bool buildAndRunHovered = ImGui::IsItemHovered(
			ImGuiHoveredFlags_AllowWhenDisabled);
		ImGui::EndDisabled();
		if (!canBuild && (buildHovered || buildAndRunHovered))
		{
			if (!m_CurrentProject)
				ImGui::SetTooltip("Open a project before building a Player.");
			else if (!startSceneReady)
				ImGui::SetTooltip("Configure a live Scene asset as the project's Start Scene.");
			else if (IsSceneRunning())
				ImGui::SetTooltip("Stop Play mode before building a Player.");
			else
				ImGui::SetTooltip("Wait for the background C# compilation to finish.");
		}
		if (buildPressed)
			BuildPlayer(false);
		else if (buildAndRunPressed)
			BuildPlayer(true);

		if (!m_PlayerBuildStatus.empty())
		{
			ImGui::Spacing();
			const ImVec4 color = m_PlayerBuildSucceeded
				? ImVec4(0.45f, 0.86f, 0.48f, 1.0f)
				: ImVec4(0.95f, 0.42f, 0.38f, 1.0f);
			ImGui::PushStyleColor(ImGuiCol_Text, color);
			ImGui::TextWrapped("%s", m_PlayerBuildStatus.c_str());
			ImGui::PopStyleColor();
		}
		ImGui::End();
	}

	bool EditorLayer::BuildPlayer(bool runAfterBuild)
	{
		m_PlayerBuildSucceeded = false;
		m_PlayerBuildStatus = "Building Player...";
		auto fail = [this](std::string message)
		{
			m_PlayerBuildSucceeded = false;
			m_PlayerBuildStatus = std::move(message);
			m_ShowConsolePanel = true;
			m_PendingPanelFocus = "Console";
			m_ConsolePanel.Push(ConsoleMessageSeverity::Error,
				m_PlayerBuildStatus, "Player Build");
			TC_Core_Error("{0}", m_PlayerBuildStatus);
			return false;
		};

		if (!m_CurrentProject)
			return fail("Player build failed: no project is open.");
		if (IsSceneRunning())
			return fail("Player build failed: stop Play mode before building.");
		if (m_ScriptCompiler.IsCompileInProgress())
			return fail("Player build failed: the background C# compilation is still running.");

		const AssetHandle startScene = m_CurrentProject->GetConfig().StartSceneHandle;
		if (static_cast<uint64_t>(startScene) == 0)
			return fail("Player build failed: no Start Scene is configured.");

		// A Player build is always based on a newly compiled and validated Release
		// candidate. Never fall back to the previous last-good artifact here.
		ScriptBuildResult scriptBuild = m_ScriptCompiler.CompileNow();
		if (!scriptBuild.Succeeded || scriptBuild.SourceChangedDuringBuild ||
			scriptBuild.BuildID.empty() || scriptBuild.AssemblyPath.empty())
		{
			return fail(scriptBuild.SourceChangedDuringBuild
				? "Player build failed: C# sources changed during the Release compile. Build again."
				: "Player build failed: the fresh Release C# candidate did not validate. See Console diagnostics.");
		}
		if (!m_ScriptCompiler.RefreshSourceState() ||
			!m_ScriptCompiler.IsCurrentSourceBuilt() ||
			m_ScriptCompiler.GetCurrentSourceHash() != scriptBuild.SourceHash ||
			m_ScriptCompiler.GetLastGoodBuildID() != scriptBuild.BuildID ||
			AbsoluteLexicalPath(m_ScriptCompiler.GetLastGoodAssemblyPath()) !=
				AbsoluteLexicalPath(scriptBuild.AssemblyPath))
		{
			return fail("Player build failed: the fresh C# candidate no longer matches the current sources.");
		}

		const std::filesystem::path managedDirectory =
			m_ScriptCompiler.GetManagedRuntimeDirectory();
		if (managedDirectory.empty())
			return fail("Player build failed: TomCat.ScriptHost Release outputs are unavailable.");
		std::string runtimeError;
		auto runtime = Scripting::CreateManagedScriptRuntime(managedDirectory,
			scriptBuild.AssemblyPath, scriptBuild.PdbPath, {}, &runtimeError);
		if (!runtime || !runtime->IsReady())
		{
			if (runtimeError.empty())
				runtimeError = "managed host initialization failed";
			return fail("Player build failed: the fresh C# candidate could not be hosted: " +
				runtimeError);
		}
		std::string scriptManifestJson;
		if (!runtime->ReadProjectMetadata(scriptManifestJson))
			return fail("Player build failed: the fresh C# manifest could not be read.");
		std::string metadataError;
		if (!m_ScriptMetadata.ParseAndReplace(scriptManifestJson, metadataError))
			return fail("Player build failed: the fresh C# manifest is invalid: " + metadataError);

		Scripting::ScriptEngine::Get().SetRuntime(std::move(runtime));
		if (!m_EditorScene)
			return fail("Player build failed: there is no current scene to reconcile and save.");
		if (ReconcileManagedScriptFields(m_EditorScene))
			m_SceneDirty = true;
		SaveScene();
		if (m_EditorScenePath.empty() || m_SceneDirty)
			return fail("Player build failed: save the current scene before building.");

		AssetManager& assetManager = AssetManager::Get();
		if (!assetManager.Refresh())
			return fail("Player build failed: the Asset Registry could not be refreshed.");
		const AssetMetadata* startSceneMetadata =
			assetManager.GetRegistry().GetMetadata(startScene);
		if (!startSceneMetadata || startSceneMetadata->IsMissing ||
			startSceneMetadata->Type != AssetType::Scene)
			return fail("Player build failed: the configured Start Scene is missing or is not a Scene asset.");

		std::vector<uint8_t> assemblyBytes;
		std::string fileError;
		if (!ReadBinaryFileForBuild(scriptBuild.AssemblyPath, assemblyBytes, fileError))
			return fail("Player build failed: " + fileError);

		std::error_code pathError;
		const std::filesystem::path projectDirectory =
			std::filesystem::weakly_canonical(m_CurrentProject->GetProjectDirectory(), pathError);
		if (pathError || projectDirectory.empty())
			return fail("Player build failed: the project directory could not be resolved.");
		const std::filesystem::path buildRoot = projectDirectory / "Build";
		std::filesystem::create_directories(buildRoot, pathError);
		if (pathError)
			return fail("Player build failed: the Build directory could not be created: " +
				pathError.message());
		const std::filesystem::path canonicalBuildRoot =
			std::filesystem::weakly_canonical(buildRoot, pathError);
		const std::filesystem::path buildRelative =
			canonicalBuildRoot.lexically_relative(projectDirectory);
		if (pathError || buildRelative.empty() || buildRelative.is_absolute() ||
			std::distance(buildRelative.begin(), buildRelative.end()) != 1 ||
			LowerASCII(PathToUTF8(*buildRelative.begin())) != "build")
			return fail("Player build failed: the Build directory resolves outside the project.");

		const std::string directoryName =
			SanitizePlayerDirectoryName(m_CurrentProject->GetName());
		const std::filesystem::path directoryComponent = UTF8ToPath(directoryName);
		if (directoryComponent.empty() || directoryComponent != directoryComponent.filename() ||
			directoryComponent == "." || directoryComponent == "..")
			return fail("Player build failed: the project name cannot form a safe output directory.");
		const std::filesystem::path outputDirectory = canonicalBuildRoot / directoryComponent;
		const std::filesystem::path stagingDirectory = canonicalBuildRoot /
			UTF8ToPath(directoryName + ".staging-" + scriptBuild.BuildID);

		const std::filesystem::file_status stagingStatus =
			std::filesystem::symlink_status(stagingDirectory, pathError);
		if (!pathError && std::filesystem::exists(stagingStatus))
			return fail("Player build failed: an isolated staging directory already exists.");
		if (pathError && pathError != std::errc::no_such_file_or_directory)
			return fail("Player build failed: the staging directory could not be inspected: " +
				pathError.message());
		pathError.clear();
		std::filesystem::create_directory(stagingDirectory, pathError);
		if (pathError)
			return fail("Player build failed: the staging directory could not be created: " +
				pathError.message());

		auto failStaged = [&](std::string message)
		{
			std::error_code cleanupError;
			if (IsFilesystemReparsePoint(stagingDirectory))
				std::filesystem::remove(stagingDirectory, cleanupError);
			else
				std::filesystem::remove_all(stagingDirectory, cleanupError);
			if (cleanupError)
				message += " Staging cleanup also failed: " + cleanupError.message();
			return fail(std::move(message));
		};

		if (!assetManager.SetManagedCookPayload(std::move(assemblyBytes),
			scriptManifestJson, scriptBuild.BuildID, {}))
			return failStaged("Player build failed: the fresh managed payload was rejected.");
		const bool cooked = assetManager.CookToPackage(
			stagingDirectory / "Game.tcpak", startScene);
		assetManager.ClearManagedCookPayload();
		if (!cooked)
			return failStaged("Player build failed while cooking Game.tcpak. See preceding diagnostics.");

		std::filesystem::path editorExecutable;
		if (!GetRunningExecutablePath(editorExecutable, fileError) ||
			!CopyPlayerFile(editorExecutable,
				stagingDirectory / "TomCatPlayer.exe", fileError))
			return failStaged("Player build failed: " + fileError);

		std::filesystem::directory_iterator nativeIterator(
			editorExecutable.parent_path(), std::filesystem::directory_options::none,
			pathError), nativeEnd;
		for (; !pathError && nativeIterator != nativeEnd;
			nativeIterator.increment(pathError))
		{
			std::error_code statusError;
			const std::filesystem::file_status status =
				nativeIterator->symlink_status(statusError);
			if (statusError)
			{
				pathError = statusError;
				break;
			}
			if (!std::filesystem::is_regular_file(status) ||
				LowerASCII(PathToUTF8(nativeIterator->path().extension())) != ".dll")
				continue;
			if (!CopyPlayerFile(nativeIterator->path(),
				stagingDirectory / nativeIterator->path().filename(), fileError))
				return failStaged("Player build failed: " + fileError);
		}
		if (pathError)
			return failStaged("Player build failed while enumerating native dependencies: " +
				pathError.message());

		for (const char* fileName : { "TomCat.ScriptHost.dll",
			"TomCat.ScriptHost.runtimeconfig.json", "TomCat.Managed.dll" })
		{
			if (!CopyPlayerFile(managedDirectory / fileName,
				stagingDirectory / "Managed" / fileName, fileError))
				return failStaged("Player build failed: " + fileError);
		}
		const std::filesystem::path depsSource =
			managedDirectory / "TomCat.ScriptHost.deps.json";
		pathError.clear();
		if (std::filesystem::is_regular_file(depsSource, pathError) && !pathError &&
			!CopyPlayerFile(depsSource, stagingDirectory / "Managed" /
				"TomCat.ScriptHost.deps.json", fileError))
			return failStaged("Player build failed: " + fileError);

		std::filesystem::path dotnetRoot;
		std::filesystem::path hostFxrDirectory;
		std::filesystem::path runtimeDirectory;
		if (!ResolvePrivateDotNetPayload(managedDirectory /
			"TomCat.ScriptHost.runtimeconfig.json", dotnetRoot,
			hostFxrDirectory, runtimeDirectory, fileError))
			return failStaged("Player build failed: " + fileError);
		if (!CopyPlayerDirectory(hostFxrDirectory, stagingDirectory / "dotnet" /
			"host" / "fxr" / hostFxrDirectory.filename(), fileError) ||
			!CopyPlayerDirectory(runtimeDirectory, stagingDirectory / "dotnet" /
			"shared" / "Microsoft.NETCore.App" / runtimeDirectory.filename(),
				fileError))
			return failStaged("Player build failed: " + fileError);

		if (!ValidateStagedPlayer(stagingDirectory, fileError))
			return failStaged("Player build failed: " + fileError);
		// Staging a private runtime can take long enough for an external editor to
		// save another C# revision. Recheck at the publication boundary so a package
		// is never presented as current after its source snapshot became stale.
		if (!m_ScriptCompiler.RefreshSourceState() ||
			!m_ScriptCompiler.IsCurrentSourceBuilt() ||
			m_ScriptCompiler.GetCurrentSourceHash() != scriptBuild.SourceHash ||
			m_ScriptCompiler.GetLastGoodBuildID() != scriptBuild.BuildID ||
			AbsoluteLexicalPath(m_ScriptCompiler.GetLastGoodAssemblyPath()) !=
				AbsoluteLexicalPath(scriptBuild.AssemblyPath))
		{
			return failStaged("Player build failed: C# sources changed while the Player was being staged. Build again.");
		}
		if (!PublishStagedPlayer(stagingDirectory, outputDirectory,
			scriptBuild.BuildID, fileError))
			return failStaged("Player build failed: " + fileError);

		m_PlayerBuildSucceeded = true;
		m_PlayerBuildStatus = "Player build succeeded: " + PathToUTF8(outputDirectory) +
			" (C# build " + scriptBuild.BuildID + ", .NET host " +
			PathToUTF8(hostFxrDirectory.filename()) + ", runtime " +
			PathToUTF8(runtimeDirectory.filename()) + ").";
		m_ConsolePanel.Push(ConsoleMessageSeverity::Info,
			m_PlayerBuildStatus, "Player Build");
		TC_Core_Info("{0}", m_PlayerBuildStatus);

		if (runAfterBuild && !LaunchCookedPlayer(outputDirectory /
			"TomCatPlayer.exe", outputDirectory, fileError))
		{
			return fail("Player build succeeded, but Build And Run failed: " + fileError);
		}
		return true;
	}

	void EditorLayer::OpenProjectSettingsPanel()
	{
		if (!m_ShowProjectSettingsPanel || m_ProjectSettingsDraftProject != m_CurrentProject)
			LoadProjectSettingsDraft();
		m_ShowProjectSettingsPanel = true;
		m_FocusProjectSettingsPanel = true;
	}

	void EditorLayer::ClearProjectSettingsFeedback()
	{
		m_ProjectSettingsError.clear();
		m_ProjectSettingsStatus.clear();
	}

	void EditorLayer::LoadProjectSettingsDraft()
	{
		m_ProjectSettingsDraftProject = m_CurrentProject;
		m_ProjectSettingsDraft = m_CurrentProject
			? m_CurrentProject->GetSettings() : ProjectSettings{};
		SyncProjectSettingsLayerBuffers();
		m_NewProjectTagBuffer.fill('\0');
		ClearProjectSettingsFeedback();
	}

	void EditorLayer::SyncProjectSettingsLayerBuffers()
	{
		for (std::size_t layer = 0; layer < Physics2DLayerCount; ++layer)
		{
			auto& buffer = m_ProjectLayerNameBuffers[layer];
			buffer.fill('\0');
			const std::string& name = m_ProjectSettingsDraft.TagsAndLayers.LayerNames[layer];
			const std::size_t count = std::min(name.size(), buffer.size() - 1);
			std::copy_n(name.data(), count, buffer.data());
		}
	}

	bool EditorLayer::PersistProjectSettingsDraft()
	{
		ClearProjectSettingsFeedback();
		auto reportValidationError = [this](std::string message)
		{
			// Keep an invalid intermediate edit in memory so users can type through
			// a temporarily duplicate name (for example Player -> PlayerOnly). The
			// settings file remains on the last valid value until this draft validates.
			m_ProjectSettingsError = std::move(message);
			return false;
		};
		auto rejectSaveAndRestore = [this](std::string message)
		{
			if (m_CurrentProject)
				m_ProjectSettingsDraft = m_CurrentProject->GetSettings();
			else
				m_ProjectSettingsDraft = ProjectSettings{};
			SyncProjectSettingsLayerBuffers();
			m_ProjectSettingsError = std::move(message);
			return false;
		};

		if (!m_CurrentProject)
			return rejectSaveAndRestore("No project is open.");
		if (IsSceneRunning())
			return rejectSaveAndRestore("Stop Play Mode before changing project settings.");
		if (m_ProjectSettingsDraft == m_CurrentProject->GetSettings())
			return true;

		const auto& tags = m_ProjectSettingsDraft.TagsAndLayers.Tags;
		if (tags.empty() || tags.front() != "Untagged")
			return reportValidationError("The reserved Untagged tag must remain first.");
		for (std::size_t index = 0; index < tags.size(); ++index)
		{
			if (tags[index].empty())
				return reportValidationError("Tag names cannot be empty.");
			if (std::find(tags.begin(), tags.begin() + index, tags[index]) != tags.begin() + index)
				return reportValidationError("Duplicate tag: " + tags[index]);
		}
		const auto& layerNames = m_ProjectSettingsDraft.TagsAndLayers.LayerNames;
		if (layerNames[0] != "Default")
			return reportValidationError("Layer 0 must remain Default.");
		for (std::size_t index = 0; index < layerNames.size(); ++index)
		{
			if (layerNames[index].empty())
				continue;
			if (std::find(layerNames.begin(), layerNames.begin() + index,
				layerNames[index]) != layerNames.begin() + index)
				return reportValidationError("Duplicate layer name: " + layerNames[index]);
		}

		if (!m_CurrentProject->SetSettings(m_ProjectSettingsDraft))
			return rejectSaveAndRestore(
				"Project settings could not be saved. Verify that ProjectSettings.json is writable.");

		m_SceneHierarchyPanel.SetProject(m_CurrentProject);
		m_ProjectSettingsDraft = m_CurrentProject->GetSettings();
		SyncProjectSettingsLayerBuffers();
		m_ProjectSettingsStatus = "Saved automatically to ProjectSettings/ProjectSettings.json.";
		return true;
	}

	void EditorLayer::UI_ProjectSettings()
	{
		if (!m_ShowProjectSettingsPanel)
			return;
		if (m_ProjectSettingsDraftProject != m_CurrentProject)
			LoadProjectSettingsDraft();

		if (m_FocusProjectSettingsPanel)
		{
			ImGui::SetNextWindowFocus();
			m_FocusProjectSettingsPanel = false;
		}
		ImGui::SetNextWindowSize(ImVec2(820.0f, 580.0f), ImGuiCond_FirstUseEver);
		if (!ImGui::Begin("Project Settings", &m_ShowProjectSettingsPanel,
			ImGuiWindowFlags_NoDocking))
		{
			ImGui::End();
			return;
		}

		const bool hasProject = m_CurrentProject != nullptr;
		const bool editable = hasProject && !IsSceneRunning();
		if (!hasProject)
			ImGui::TextColored(ImVec4(0.95f, 0.72f, 0.25f, 1.0f),
				"Open a project to edit Tags, Layers, and Physics 2D settings.");
		else if (IsSceneRunning())
			ImGui::TextColored(ImVec4(0.95f, 0.72f, 0.25f, 1.0f),
				"Project settings are read-only while the scene is running. Stop Play Mode to edit them.");

		const float navigationWidth = 170.0f;
		ImGui::BeginChild("##ProjectSettingsNavigation", ImVec2(navigationWidth, 0.0f), true);
		if (ImGui::Selectable("Tags and Layers", m_ProjectSettingsPage == 0))
			m_ProjectSettingsPage = 0;
		if (ImGui::Selectable("Physics 2D", m_ProjectSettingsPage == 1))
			m_ProjectSettingsPage = 1;
		ImGui::EndChild();
		ImGui::SameLine();

		ImGui::BeginChild("##ProjectSettingsPage", ImVec2(0.0f, 0.0f), true);
		ImGui::BeginDisabled(!editable);
		if (m_ProjectSettingsPage == 0)
		{
			ImGui::TextUnformatted("Tags");
			ImGui::Separator();
			ImGui::TextDisabled("Untagged is reserved and cannot be removed.");
			std::size_t tagToRemove = static_cast<std::size_t>(-1);
			const auto& tags = m_ProjectSettingsDraft.TagsAndLayers.Tags;
			for (std::size_t index = 0; index < tags.size(); ++index)
			{
				ImGui::PushID(static_cast<int>(index));
				ImGui::TextUnformatted(tags[index].empty() ? "<Empty>" : tags[index].c_str());
				if (index == 0)
				{
					ImGui::SameLine();
					ImGui::TextDisabled("(Reserved)");
				}
				else
				{
					const float removeWidth = ImGui::CalcTextSize("Remove").x +
						ImGui::GetStyle().FramePadding.x * 2.0f;
					ImGui::SameLine(std::max(ImGui::GetCursorPosX(),
						ImGui::GetWindowContentRegionMax().x - removeWidth));
					if (ImGui::SmallButton("Remove"))
						tagToRemove = index;
				}
				ImGui::PopID();
			}
			if (tagToRemove != static_cast<std::size_t>(-1))
			{
				m_ProjectSettingsDraft.TagsAndLayers.Tags.erase(
					m_ProjectSettingsDraft.TagsAndLayers.Tags.begin() + tagToRemove);
				PersistProjectSettingsDraft();
			}

			ImGui::Spacing();
			const float addButtonWidth = ImGui::CalcTextSize("Add Tag").x +
				ImGui::GetStyle().FramePadding.x * 2.0f;
			ImGui::SetNextItemWidth(std::max(80.0f,
				ImGui::GetContentRegionAvail().x - addButtonWidth - ImGui::GetStyle().ItemSpacing.x));
			bool addTag = ImGui::InputTextWithHint("##NewProjectTag", "New tag",
				m_NewProjectTagBuffer.data(), m_NewProjectTagBuffer.size(),
				ImGuiInputTextFlags_EnterReturnsTrue);
			ImGui::SameLine();
			addTag |= ImGui::Button("Add Tag");
			if (addTag)
			{
				const std::string newTag = TrimASCIIWhitespace(m_NewProjectTagBuffer.data());
				if (newTag.empty())
				{
					ClearProjectSettingsFeedback();
					m_ProjectSettingsError = "Tag names cannot be empty.";
				}
				else if (std::find(m_ProjectSettingsDraft.TagsAndLayers.Tags.begin(),
					m_ProjectSettingsDraft.TagsAndLayers.Tags.end(), newTag)
					!= m_ProjectSettingsDraft.TagsAndLayers.Tags.end())
				{
					ClearProjectSettingsFeedback();
					m_ProjectSettingsError = "A tag with that name already exists.";
				}
				else
				{
					m_ProjectSettingsDraft.TagsAndLayers.Tags.push_back(newTag);
					if (PersistProjectSettingsDraft())
						m_NewProjectTagBuffer.fill('\0');
				}
			}

			ImGui::Spacing();
			ImGui::TextUnformatted("Layers");
			ImGui::Separator();
			ImGui::TextDisabled("Layer slots are stable. Clear a name to hide that layer from entity menus.");
			for (std::size_t layer = 0; layer < Physics2DLayerCount; ++layer)
			{
				ImGui::PushID(static_cast<int>(layer));
				ImGui::AlignTextToFramePadding();
				ImGui::Text("%02u", static_cast<unsigned int>(layer));
				ImGui::SameLine();
				ImGui::SetNextItemWidth(-1.0f);
				ImGui::BeginDisabled(layer == 0);
				if (ImGui::InputText("##LayerName", m_ProjectLayerNameBuffers[layer].data(),
					m_ProjectLayerNameBuffers[layer].size()))
				{
					m_ProjectSettingsDraft.TagsAndLayers.LayerNames[layer] =
						m_ProjectLayerNameBuffers[layer].data();
					PersistProjectSettingsDraft();
				}
				ImGui::EndDisabled();
				if (layer == 0 && ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
					ImGui::SetTooltip("Layer 0 is reserved as Default.");
				ImGui::PopID();
			}
		}
		else
		{
			ImGui::TextUnformatted("Physics 2D Layer Collision Matrix");
			ImGui::Separator();
			ImGui::TextWrapped("A checked cell allows the two named entity layers to collide. The matrix is symmetric.");

			std::vector<uint8_t> namedLayers;
			for (uint8_t layer = 0; layer < Physics2DLayerCount; ++layer)
			{
				if (!m_ProjectSettingsDraft.TagsAndLayers.LayerNames[layer].empty())
					namedLayers.push_back(layer);
			}
			if (ImGui::Button("Enable All"))
			{
				for (std::size_t row = 0; row < namedLayers.size(); ++row)
					for (std::size_t column = 0; column <= row; ++column)
						m_ProjectSettingsDraft.Physics2D.SetLayersCollide(
							namedLayers[row], namedLayers[column], true);
				PersistProjectSettingsDraft();
			}
			ImGui::SameLine();
			if (ImGui::Button("Disable All"))
			{
				for (std::size_t row = 0; row < namedLayers.size(); ++row)
					for (std::size_t column = 0; column <= row; ++column)
						m_ProjectSettingsDraft.Physics2D.SetLayersCollide(
							namedLayers[row], namedLayers[column], false);
				PersistProjectSettingsDraft();
			}

			if (namedLayers.empty())
				ImGui::TextDisabled("Define at least one named layer on the Tags and Layers page.");
			else
			{
				const ImGuiTableFlags flags = ImGuiTableFlags_Borders |
					ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingFixedFit |
					ImGuiTableFlags_ScrollX;
				const float matrixHeight = std::min(430.0f,
					(static_cast<float>(namedLayers.size()) + 2.0f) * ImGui::GetFrameHeightWithSpacing());
				if (ImGui::BeginTable("##Physics2DLayerMatrix",
					static_cast<int>(namedLayers.size()) + 1, flags, ImVec2(0.0f, matrixHeight)))
				{
					ImGui::TableSetupScrollFreeze(1, 1);
					ImGui::TableSetupColumn("Layer", ImGuiTableColumnFlags_WidthFixed, 170.0f);
					std::array<std::string, Physics2DLayerCount> columnLabels;
					for (std::size_t column = 0; column < namedLayers.size(); ++column)
					{
						columnLabels[column] = std::to_string(
							static_cast<unsigned int>(namedLayers[column]));
						ImGui::TableSetupColumn(columnLabels[column].c_str(),
							ImGuiTableColumnFlags_WidthFixed, 38.0f);
					}
					ImGui::TableHeadersRow();

					for (std::size_t row = 0; row < namedLayers.size(); ++row)
					{
						const uint8_t layerA = namedLayers[row];
						ImGui::TableNextRow();
						ImGui::TableSetColumnIndex(0);
						const std::string rowLabel = std::to_string(
							static_cast<unsigned int>(layerA)) + "  " +
							m_ProjectSettingsDraft.TagsAndLayers.LayerNames[layerA];
						ImGui::TextUnformatted(rowLabel.c_str());
						for (std::size_t column = 0; column < namedLayers.size(); ++column)
						{
							ImGui::TableSetColumnIndex(static_cast<int>(column) + 1);
							if (column > row)
							{
								ImGui::TextDisabled("-");
								continue;
							}
							const uint8_t layerB = namedLayers[column];
							bool collide = m_ProjectSettingsDraft.Physics2D.CanLayersCollide(layerA, layerB);
							ImGui::PushID(static_cast<int>(layerA) * 32 + layerB);
							if (ImGui::Checkbox("##Collide", &collide))
							{
								m_ProjectSettingsDraft.Physics2D.SetLayersCollide(layerA, layerB, collide);
								PersistProjectSettingsDraft();
							}
							if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayNormal))
								ImGui::SetTooltip("%s / %s",
									m_ProjectSettingsDraft.TagsAndLayers.LayerNames[layerA].c_str(),
									m_ProjectSettingsDraft.TagsAndLayers.LayerNames[layerB].c_str());
							ImGui::PopID();
						}
					}
					ImGui::EndTable();
				}
			}
		}
		ImGui::EndDisabled();

		ImGui::Separator();
		ImGui::TextDisabled("Valid changes are saved automatically to ProjectSettings/ProjectSettings.json.");
		if (!m_ProjectSettingsError.empty())
		{
			ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.95f, 0.35f, 0.35f, 1.0f));
			ImGui::TextWrapped("Error: %s", m_ProjectSettingsError.c_str());
			ImGui::PopStyleColor();
		}
		else if (!m_ProjectSettingsStatus.empty())
			ImGui::TextColored(ImVec4(0.45f, 0.85f, 0.45f, 1.0f), "%s",
				m_ProjectSettingsStatus.c_str());
		ImGui::EndChild();
		ImGui::End();
	}

	void EditorLayer::RenderSceneColliderOverlays()
	{
		if (!m_ActiveScene)
			return;

		const SceneHierarchyPanel::ColliderEditMode editMode =
			m_SceneState == SceneState::Edit
			? m_SceneHierarchyPanel.GetColliderEditMode()
			: SceneHierarchyPanel::ColliderEditMode::None;
		if (!m_ShowColliders && editMode == SceneHierarchyPanel::ColliderEditMode::None)
			return;

		UUID selectedUUID(0);
		const Entity selectedEntity = m_SceneHierarchyPanel.GetSelectedEntity();
		if (selectedEntity && selectedEntity.HasComponent<ID>())
			selectedUUID = selectedEntity.GetUUID();

		const std::vector<ColliderDebugShape> shapes =
			m_ActiveScene->GetColliderDebugShapes(m_SceneState != SceneState::Edit);
		if (shapes.empty())
			return;

		const float previousLineWidth = Renderer2D::GetLineWidth();
		Renderer2D::SetLineWidth(2.0f);
		RenderCommand::SetDepthTest(false);
		Renderer2D::BeginScene(m_EditorCamera);

		for (const ColliderDebugShape& shape : shapes)
		{
			const bool selected = selectedUUID != UUID(0) && shape.EntityID == selectedUUID;
			const bool edited = selected &&
				((editMode == SceneHierarchyPanel::ColliderEditMode::Box &&
					shape.Type == ColliderDebugShapeType::Box) ||
				 (editMode == SceneHierarchyPanel::ColliderEditMode::Circle &&
					shape.Type == ColliderDebugShapeType::Circle));
			if (!m_ShowColliders && !edited)
				continue;

			const glm::vec4 color = !shape.Enabled
				? glm::vec4(0.55f, 0.58f, 0.55f, selected ? 0.9f : 0.62f)
				: edited
				? glm::vec4(0.45f, 1.0f, 0.35f, 1.0f)
				: selected
					? glm::vec4(0.32f, 0.95f, 0.48f, 1.0f)
					: glm::vec4(0.25f, 0.82f, 0.42f, 0.82f);

			if (shape.Type == ColliderDebugShapeType::Box)
				Renderer2D::DrawRect(shape.Transform, color, -1);
			else if (shape.Type == ColliderDebugShapeType::Circle)
				Renderer2D::DrawCircle(shape.Transform, color, 0.045f, 0.005f, -1);
		}

		Renderer2D::EndScene();
		RenderCommand::SetDepthTest(true);
		Renderer2D::SetLineWidth(previousLineWidth);
	}

	bool EditorLayer::WorldToScreen(const glm::vec3& worldPosition, glm::vec2& screenPosition) const
	{
		const glm::vec2 viewportSize = m_ViewportBounds[1] - m_ViewportBounds[0];
		if (viewportSize.x <= 0.0f || viewportSize.y <= 0.0f)
			return false;

		const glm::vec4 clip = m_EditorCamera.GetViewProjection() * glm::vec4(worldPosition, 1.0f);
		if (!std::isfinite(clip.w) || clip.w <= 0.000001f)
			return false;
		const glm::vec3 ndc = glm::vec3(clip) / clip.w;
		if (!std::isfinite(ndc.x) || !std::isfinite(ndc.y))
			return false;

		screenPosition.x = m_ViewportBounds[0].x + (ndc.x * 0.5f + 0.5f) * viewportSize.x;
		screenPosition.y = m_ViewportBounds[0].y + (0.5f - ndc.y * 0.5f) * viewportSize.y;
		return std::isfinite(screenPosition.x) && std::isfinite(screenPosition.y);
	}

	bool EditorLayer::ScreenToWorldOnPlane(const glm::vec2& screenPosition, float worldZ,
		glm::vec2& worldPosition) const
	{
		const glm::vec2 viewportSize = m_ViewportBounds[1] - m_ViewportBounds[0];
		if (viewportSize.x <= 0.0f || viewportSize.y <= 0.0f)
			return false;

		const float ndcX = ((screenPosition.x - m_ViewportBounds[0].x) / viewportSize.x) * 2.0f - 1.0f;
		const float ndcY = 1.0f - ((screenPosition.y - m_ViewportBounds[0].y) / viewportSize.y) * 2.0f;
		const glm::mat4 inverseViewProjection = glm::inverse(m_EditorCamera.GetViewProjection());
		glm::vec4 nearPoint = inverseViewProjection * glm::vec4(ndcX, ndcY, -1.0f, 1.0f);
		glm::vec4 farPoint = inverseViewProjection * glm::vec4(ndcX, ndcY, 1.0f, 1.0f);
		if (std::abs(nearPoint.w) <= 0.000001f || std::abs(farPoint.w) <= 0.000001f)
			return false;
		nearPoint /= nearPoint.w;
		farPoint /= farPoint.w;

		const glm::vec3 ray = glm::vec3(farPoint - nearPoint);
		if (!std::isfinite(ray.z) || std::abs(ray.z) <= 0.000001f)
			return false;
		const float distance = (worldZ - nearPoint.z) / ray.z;
		const glm::vec3 intersection = glm::vec3(nearPoint) + ray * distance;
		if (!std::isfinite(intersection.x) || !std::isfinite(intersection.y))
			return false;

		worldPosition = { intersection.x, intersection.y };
		return true;
	}

	void EditorLayer::UI_SceneColliderVisibilityToggle()
	{
		constexpr float buttonHeight = 24.0f;
		const ImVec2 labelSize = ImGui::CalcTextSize("Colliders");
		const float buttonWidth = labelSize.x + 30.0f;
		const float availableWidth = m_ViewportBounds[1].x - m_ViewportBounds[0].x;
		if (availableWidth < buttonWidth + 16.0f)
			return;

		const ImVec2 savedCursor = ImGui::GetCursorScreenPos();
		const ImVec2 minimum(m_ViewportBounds[1].x - buttonWidth - 8.0f,
			m_GizmoModeDockY + (m_GizmoModeDockHeight - buttonHeight) * 0.5f);
		const ImVec2 maximum(minimum.x + buttonWidth, minimum.y + buttonHeight);
		ImGui::SetCursorScreenPos(minimum);
		ImGui::InvisibleButton("##scene_show_colliders", ImVec2(buttonWidth, buttonHeight));
		const bool hovered = ImGui::IsItemHovered();
		if (ImGui::IsItemClicked())
			m_ShowColliders = !m_ShowColliders;
		if (hovered)
			ImGui::SetTooltip("Show collider outlines in the Scene view");

		ImDrawList* draw = ImGui::GetWindowDrawList();
		const ImU32 fill = m_ShowColliders
			? IM_COL32(44, 93, 135, 255)
			: hovered ? IM_COL32(98, 98, 98, 245) : IM_COL32(71, 71, 71, 245);
		draw->AddRectFilled(minimum, maximum, fill, 2.0f);
		draw->AddRect(minimum, maximum, IM_COL32(25, 25, 25, 255), 2.0f, 0, 1.0f);
		const ImVec2 indicator(minimum.x + 11.0f, (minimum.y + maximum.y) * 0.5f);
		draw->AddCircle(indicator, 5.0f,
			m_ShowColliders ? IM_COL32(120, 238, 116, 255) : IM_COL32(145, 145, 145, 255),
			20, 1.7f);
		draw->AddText(ImVec2(minimum.x + 21.0f,
			minimum.y + (buttonHeight - labelSize.y) * 0.5f), IM_COL32(235, 235, 235, 255), "Colliders");
		ImGui::SetCursorScreenPos(savedCursor);
	}

	void EditorLayer::ResetColliderEditState()
	{
		m_ActiveColliderHandle = ColliderEditHandle::None;
		m_ColliderEditEntity = UUID(0);
		m_ColliderDragStartMouseWorld = { 0.0f, 0.0f };
		m_ColliderDragStartCenter = { 0.0f, 0.0f };
		m_ColliderDragStartHalfSize = { 0.0f, 0.0f };
		m_ColliderDragStartRadius = 0.0f;
		m_ColliderDragPlaneZ = 0.0f;
		m_ColliderHandleHovered = false;
	}

	void EditorLayer::UI_ColliderEditHandles()
	{
		m_ColliderHandleHovered = false;
		const SceneHierarchyPanel::ColliderEditMode editMode = m_SceneHierarchyPanel.GetColliderEditMode();
		Entity selectedEntity = m_SceneHierarchyPanel.GetSelectedEntity();
		if (m_SceneState != SceneState::Edit ||
			editMode == SceneHierarchyPanel::ColliderEditMode::None ||
			!m_ActiveScene ||
			!selectedEntity || !selectedEntity.HasComponent<Transform>() ||
			!selectedEntity.HasComponent<ID>())
		{
			ResetColliderEditState();
			return;
		}

		const UUID selectedUUID = selectedEntity.GetUUID();
		if (m_ActiveColliderHandle != ColliderEditHandle::None &&
			m_ColliderEditEntity != selectedUUID)
			ResetColliderEditState();

		const ColliderDebugShapeType expectedType =
			editMode == SceneHierarchyPanel::ColliderEditMode::Box
			? ColliderDebugShapeType::Box : ColliderDebugShapeType::Circle;
		const std::vector<ColliderDebugShape> shapes = m_ActiveScene->GetColliderDebugShapes(false);
		const auto shapeIt = std::find_if(shapes.begin(), shapes.end(),
			[&](const ColliderDebugShape& shape)
			{
				return shape.EntityID == selectedUUID && shape.Type == expectedType;
			});
		if (shapeIt == shapes.end())
		{
			ResetColliderEditState();
			return;
		}

		const ColliderDebugShape& shape = *shapeIt;
		const Transform& transform = selectedEntity.GetComponent<Transform>();
		const float scaleX = std::abs(transform._Scale.x);
		const float scaleY = std::abs(transform._Scale.y);
		const float maximumScale = std::max(scaleX, scaleY);
		if (scaleX <= 0.000001f || scaleY <= 0.000001f ||
			(editMode == SceneHierarchyPanel::ColliderEditMode::Circle && maximumScale <= 0.000001f))
		{
			ResetColliderEditState();
			return;
		}

		const float cosine = std::cos(shape.Rotation);
		const float sine = std::sin(shape.Rotation);
		const glm::vec2 right(cosine, sine);
		const glm::vec2 up(-sine, cosine);
		const glm::vec2 center = shape.Center;
		const float handleRadius = 6.0f;
		const ImVec2 savedCursor = ImGui::GetCursorScreenPos();
		ImDrawList* draw = ImGui::GetWindowDrawList();
		ImGui::PushClipRect(ImVec2(m_ViewportBounds[0].x, m_ViewportBounds[0].y),
			ImVec2(m_ViewportBounds[1].x, m_ViewportBounds[1].y), true);
		ImGui::PushID("ColliderEditHandles");
		ImGui::PushID(static_cast<int>(selectedEntity));

		auto submitHandle = [&](ColliderEditHandle handle, const char* id,
			const glm::vec2& worldPosition, ImGuiMouseCursor cursor, bool offsetHandle)
		{
			glm::vec2 screenPosition;
			if (!WorldToScreen(glm::vec3(worldPosition, transform._Translation.z), screenPosition))
				return;
			if (screenPosition.x < m_ViewportBounds[0].x - handleRadius ||
				screenPosition.x > m_ViewportBounds[1].x + handleRadius ||
				screenPosition.y < m_ViewportBounds[0].y - handleRadius ||
				screenPosition.y > m_ViewportBounds[1].y + handleRadius)
				return;

			const ImVec2 minimum(screenPosition.x - handleRadius, screenPosition.y - handleRadius);
			const ImVec2 maximum(screenPosition.x + handleRadius, screenPosition.y + handleRadius);
			ImGui::SetCursorScreenPos(minimum);
			ImGui::PushID(id);
			ImGui::InvisibleButton("##handle", ImVec2(handleRadius * 2.0f, handleRadius * 2.0f));
			const bool hovered = ImGui::IsItemHovered();
			const bool active = m_ActiveColliderHandle == handle && ImGui::IsItemActive();
			m_ColliderHandleHovered = m_ColliderHandleHovered || hovered || active;
			if (hovered || active)
				ImGui::SetMouseCursor(cursor);

			if (ImGui::IsItemActivated())
			{
				glm::vec2 mouseWorld;
				const ImVec2 mouse = ImGui::GetMousePos();
				if (ScreenToWorldOnPlane({ mouse.x, mouse.y }, transform._Translation.z, mouseWorld))
				{
					m_ActiveColliderHandle = handle;
					m_ColliderEditEntity = selectedUUID;
					m_ColliderDragStartMouseWorld = mouseWorld;
					m_ColliderDragStartCenter = shape.Center;
					m_ColliderDragStartHalfSize = shape.HalfSize;
					m_ColliderDragStartRadius = shape.Radius;
					m_ColliderDragPlaneZ = transform._Translation.z;
				}
			}

			const ImU32 handleColor = active
				? IM_COL32(255, 176, 65, 255)
				: hovered ? IM_COL32(220, 255, 185, 255) : IM_COL32(115, 235, 110, 255);
			if (offsetHandle)
			{
				const ImVec2 top(screenPosition.x, screenPosition.y - handleRadius);
				const ImVec2 rightPoint(screenPosition.x + handleRadius, screenPosition.y);
				const ImVec2 bottom(screenPosition.x, screenPosition.y + handleRadius);
				const ImVec2 leftPoint(screenPosition.x - handleRadius, screenPosition.y);
				draw->AddQuadFilled(top, rightPoint, bottom, leftPoint, IM_COL32(25, 25, 25, 255));
				draw->AddQuadFilled(ImVec2(top.x, top.y + 1.5f),
					ImVec2(rightPoint.x - 1.5f, rightPoint.y),
					ImVec2(bottom.x, bottom.y - 1.5f),
					ImVec2(leftPoint.x + 1.5f, leftPoint.y), handleColor);
			}
			else
			{
				draw->AddRectFilled(minimum, maximum, IM_COL32(25, 25, 25, 255), 1.0f);
				draw->AddRectFilled(ImVec2(minimum.x + 1.5f, minimum.y + 1.5f),
					ImVec2(maximum.x - 1.5f, maximum.y - 1.5f), handleColor, 1.0f);
			}
			ImGui::PopID();
		};

		if (editMode == SceneHierarchyPanel::ColliderEditMode::Box)
		{
			const glm::vec2 halfSize = shape.HalfSize;
			submitHandle(ColliderEditHandle::BoxLeft, "Left", center - right * halfSize.x,
				ImGuiMouseCursor_ResizeEW, false);
			submitHandle(ColliderEditHandle::BoxRight, "Right", center + right * halfSize.x,
				ImGuiMouseCursor_ResizeEW, false);
			submitHandle(ColliderEditHandle::BoxBottom, "Bottom", center - up * halfSize.y,
				ImGuiMouseCursor_ResizeNS, false);
			submitHandle(ColliderEditHandle::BoxTop, "Top", center + up * halfSize.y,
				ImGuiMouseCursor_ResizeNS, false);
			submitHandle(ColliderEditHandle::BoxBottomLeft, "BottomLeft",
				center - right * halfSize.x - up * halfSize.y, ImGuiMouseCursor_ResizeNESW, false);
			submitHandle(ColliderEditHandle::BoxBottomRight, "BottomRight",
				center + right * halfSize.x - up * halfSize.y, ImGuiMouseCursor_ResizeNWSE, false);
			submitHandle(ColliderEditHandle::BoxTopLeft, "TopLeft",
				center - right * halfSize.x + up * halfSize.y, ImGuiMouseCursor_ResizeNWSE, false);
			submitHandle(ColliderEditHandle::BoxTopRight, "TopRight",
				center + right * halfSize.x + up * halfSize.y, ImGuiMouseCursor_ResizeNESW, false);
		}
		else
		{
			submitHandle(ColliderEditHandle::CircleLeft, "CircleLeft", center - right * shape.Radius,
				ImGuiMouseCursor_ResizeEW, false);
			submitHandle(ColliderEditHandle::CircleRight, "CircleRight", center + right * shape.Radius,
				ImGuiMouseCursor_ResizeEW, false);
			submitHandle(ColliderEditHandle::CircleBottom, "CircleBottom", center - up * shape.Radius,
				ImGuiMouseCursor_ResizeNS, false);
			submitHandle(ColliderEditHandle::CircleTop, "CircleTop", center + up * shape.Radius,
				ImGuiMouseCursor_ResizeNS, false);
		}

		// Submit the offset handle last so it remains reachable for very small
		// colliders whose resize handles overlap the center.
		submitHandle(ColliderEditHandle::Offset, "Offset", center, ImGuiMouseCursor_ResizeAll, true);

		ImGui::PopID();
		ImGui::PopID();
		ImGui::PopClipRect();
		ImGui::SetCursorScreenPos(savedCursor);

		if (m_ActiveColliderHandle == ColliderEditHandle::None)
			return;
		if (!ImGui::IsMouseDown(ImGuiMouseButton_Left))
		{
			m_ActiveColliderHandle = ColliderEditHandle::None;
			return;
		}

		const ImVec2 mouse = ImGui::GetMousePos();
		glm::vec2 mouseWorld;
		if (!ScreenToWorldOnPlane({ mouse.x, mouse.y }, m_ColliderDragPlaneZ, mouseWorld))
			return;
		const glm::vec2 mouseDelta = mouseWorld - m_ColliderDragStartMouseWorld;

		auto centerToOffset = [&](const glm::vec2& worldCenter, glm::vec2& offset)
		{
			const glm::vec2 relative = worldCenter - glm::vec2(transform._Translation);
			const float transformCosine = std::cos(transform._Rotation.z);
			const float transformSine = std::sin(transform._Rotation.z);
			const glm::vec2 scaledLocal(
				transformCosine * relative.x + transformSine * relative.y,
				-transformSine * relative.x + transformCosine * relative.y);
			if (std::abs(transform._Scale.x) <= 0.000001f ||
				std::abs(transform._Scale.y) <= 0.000001f)
				return false;
			offset = { scaledLocal.x / transform._Scale.x, scaledLocal.y / transform._Scale.y };
			return std::isfinite(offset.x) && std::isfinite(offset.y);
		};

		constexpr float minimumComponentExtent = 0.001f;
		if (editMode == SceneHierarchyPanel::ColliderEditMode::Box &&
			selectedEntity.HasComponent<BoxCollider2D>())
		{
			glm::vec2 newCenter = m_ColliderDragStartCenter;
			glm::vec2 newHalfSize = m_ColliderDragStartHalfSize;
			bool resizeX = false;
			bool resizeY = false;
			float signX = 0.0f;
			float signY = 0.0f;

			switch (m_ActiveColliderHandle)
			{
			case ColliderEditHandle::BoxLeft: resizeX = true; signX = -1.0f; break;
			case ColliderEditHandle::BoxRight: resizeX = true; signX = 1.0f; break;
			case ColliderEditHandle::BoxBottom: resizeY = true; signY = -1.0f; break;
			case ColliderEditHandle::BoxTop: resizeY = true; signY = 1.0f; break;
			case ColliderEditHandle::BoxBottomLeft:
				resizeX = resizeY = true; signX = signY = -1.0f; break;
			case ColliderEditHandle::BoxBottomRight:
				resizeX = resizeY = true; signX = 1.0f; signY = -1.0f; break;
			case ColliderEditHandle::BoxTopLeft:
				resizeX = resizeY = true; signX = -1.0f; signY = 1.0f; break;
			case ColliderEditHandle::BoxTopRight:
				resizeX = resizeY = true; signX = signY = 1.0f; break;
			default: break;
			}

			if (m_ActiveColliderHandle == ColliderEditHandle::Offset)
				newCenter += mouseDelta;
			if (resizeX)
			{
				const glm::vec2 outward = right * signX;
				const float requestedHalfSize = m_ColliderDragStartHalfSize.x +
					glm::dot(mouseDelta, outward) * 0.5f;
				newHalfSize.x = std::max(scaleX * minimumComponentExtent, requestedHalfSize);
				newCenter += outward * (newHalfSize.x - m_ColliderDragStartHalfSize.x);
			}
			if (resizeY)
			{
				const glm::vec2 outward = up * signY;
				const float requestedHalfSize = m_ColliderDragStartHalfSize.y +
					glm::dot(mouseDelta, outward) * 0.5f;
				newHalfSize.y = std::max(scaleY * minimumComponentExtent, requestedHalfSize);
				newCenter += outward * (newHalfSize.y - m_ColliderDragStartHalfSize.y);
			}

			auto& collider = selectedEntity.GetComponent<BoxCollider2D>();
			glm::vec2 newOffset;
			if (centerToOffset(newCenter, newOffset))
			{
				const glm::vec2 newSize(newHalfSize.x / scaleX, newHalfSize.y / scaleY);
				if (glm::length(collider.Offset - newOffset) > 0.000001f ||
					glm::length(collider.Size - newSize) > 0.000001f)
				{
					collider.Offset = newOffset;
					collider.Size = newSize;
					m_SceneDirty = true;
				}
			}
		}
		else if (editMode == SceneHierarchyPanel::ColliderEditMode::Circle &&
			selectedEntity.HasComponent<CircleCollider2D>())
		{
			auto& collider = selectedEntity.GetComponent<CircleCollider2D>();
			if (m_ActiveColliderHandle == ColliderEditHandle::Offset)
			{
				glm::vec2 newOffset;
				if (centerToOffset(m_ColliderDragStartCenter + mouseDelta, newOffset) &&
					glm::length(collider.Offset - newOffset) > 0.000001f)
				{
					collider.Offset = newOffset;
					m_SceneDirty = true;
				}
			}
			else
			{
				glm::vec2 outward(0.0f);
				switch (m_ActiveColliderHandle)
				{
				case ColliderEditHandle::CircleLeft: outward = -right; break;
				case ColliderEditHandle::CircleRight: outward = right; break;
				case ColliderEditHandle::CircleBottom: outward = -up; break;
				case ColliderEditHandle::CircleTop: outward = up; break;
				default: break;
				}
				if (glm::dot(outward, outward) > 0.0f)
				{
					const float worldRadius = std::max(maximumScale * minimumComponentExtent,
						m_ColliderDragStartRadius + glm::dot(mouseDelta, outward));
					const float newRadius = worldRadius / maximumScale;
					if (std::abs(collider.Radius - newRadius) > 0.000001f)
					{
						collider.Radius = newRadius;
						m_SceneDirty = true;
					}
				}
			}
		}
	}

	void EditorLayer::UI_SceneToolbarDragHandle(const char* id, glm::vec2& offset, bool& docked, bool& dragging,
		const ImVec2& handleMin, const ImVec2& handleMax, float tearX, bool canDock)
	{
		ImGui::PushID(id);
		ImGui::SetCursorScreenPos(handleMin);
		ImGui::InvisibleButton("##drag", ImVec2(handleMax.x - handleMin.x, handleMax.y - handleMin.y));

		const bool pressing = ImGui::IsMouseDown(ImGuiMouseButton_Left);
		if (!dragging && ImGui::IsItemActive() && ImGui::IsMouseDragging(ImGuiMouseButton_Left))
		{
			dragging = true;
			if (docked)
			{
				docked = false;
				const ImVec2 mouse = ImGui::GetMousePos();
				offset = { mouse.x - m_ViewportBounds[0].x - tearX,
					m_GizmoModeDockY - m_ViewportBounds[0].y };
			}
		}

		if (dragging && pressing)
		{
			const ImVec2 delta = ImGui::GetIO().MouseDelta;
			offset.x += delta.x;
			offset.y += delta.y;
		}
		else if (dragging)
		{
			const ImVec2 mouse = ImGui::GetMousePos();
			if (canDock &&
				mouse.y >= m_GizmoModeDockY - 6.0f &&
				mouse.y <= m_GizmoModeDockY + m_GizmoModeDockHeight + 6.0f)
			{
				docked = true;
				offset = { 16.0f, 10.0f };
			}
			dragging = false;
		}

		ImGui::PopID();
	}

	void EditorLayer::UI_SceneGizmoToolbar()
	{
		// Draw directly over the Scene image. This keeps the palette clipped and
		// owned by the Scene view instead of creating another dockable ImGui window.
		// The native Scene menu bar clips to its own row while it is active; widen
		// this toolbar pass to the Scene bounds so a floating palette below the row
		// remains visible and interactive.
		const float toolbarClipTop = m_GizmoTransformToolbarDragging
			? ImGui::GetWindowPos().y : m_GizmoModeDockY;
		ImGui::PushClipRect(ImVec2(ImGui::GetWindowPos().x, toolbarClipTop),
			ImVec2(m_ViewportBounds[1].x, m_ViewportBounds[1].y), false);
		const float dockPadding = kSceneToolbarPadding;
		const float dockHandleWidth = kSceneToolbarHandleWidth;
		const float dockButtonWidth = kSceneTransformButtonWidth;
		const float dockButtonHeight = 28.0f;
		const float dockGap = kSceneToolbarItemGap;
		const float dockWidth = kSceneTransformToolbarWidth;
		const float modeWidth = kSceneModeToolbarWidth;

		if (m_GizmoTransformToolbarDocked)
		{
			const float dockStartX = m_ViewportBounds[0].x + 8.0f;
			const float transformDockX = (m_GizmoModeToolbarDocked && m_GizmoModeToolbarFirst)
				? dockStartX + modeWidth + dockGap : dockStartX;
			const float transformHeight = dockButtonHeight + dockPadding * 2.0f;
			const float transformDockOffsetY = (m_GizmoModeDockHeight - transformHeight) * 0.5f;
			ImVec2 topLeft(transformDockX, m_GizmoModeDockY + transformDockOffsetY);
			ImVec2 bottomRight(topLeft.x + dockWidth, topLeft.y + dockButtonHeight + dockPadding * 2.0f);
			// A docked toolbar may be torn off while the cursor leaves the Scene
			// window.  Use the viewport foreground list during the drag so the
			// active bar is not clipped away by the Scene window bounds.
			ImDrawList* draw = m_GizmoTransformToolbarDragging
				? ImGui::GetForegroundDrawList() : ImGui::GetWindowDrawList();
			const ImU32 outer = IM_COL32(40, 40, 40, 245);
			const ImU32 normal = IM_COL32(71, 71, 71, 245);
			const ImU32 active = IM_COL32(44, 93, 135, 255);
			const ImU32 handleLine = IM_COL32(137, 137, 137, 255);
			draw->AddRectFilled(topLeft, bottomRight, outer, 2.0f);

			ImVec2 handleMin(topLeft.x + dockPadding, topLeft.y + dockPadding);
			ImVec2 handleMax(handleMin.x + dockHandleWidth, topLeft.y + dockButtonHeight + dockPadding);
			ImVec2 handleCenter((handleMin.x + handleMax.x) * 0.5f, (handleMin.y + handleMax.y) * 0.5f);
			for (int i = -1; i <= 1; ++i)
				draw->AddLine(ImVec2(handleCenter.x - 7.0f, handleCenter.y + i * 4.0f),
					ImVec2(handleCenter.x + 7.0f, handleCenter.y + i * 4.0f), handleLine, 2.0f);

			const bool transformWasDragging = m_GizmoTransformToolbarDragging;
			UI_SceneToolbarDragHandle("##scene_transform_toolbar_docked", m_GizmoToolbarOffset,
				m_GizmoTransformToolbarDocked, m_GizmoTransformToolbarDragging,
				handleMin, handleMax, 0.0f, true);
			if (transformWasDragging && !m_GizmoTransformToolbarDragging && m_GizmoTransformToolbarDocked)
			{
				const float x = ImGui::GetMousePos().x;
				m_GizmoModeToolbarFirst = !m_GizmoModeToolbarDocked ||
					x >= dockStartX + modeWidth * 0.5f;
				SaveSceneToolbarLayout();
			}

			const int tools[] = { -1, ImGuizmo::OPERATION::TRANSLATE,
				ImGuizmo::OPERATION::ROTATE, ImGuizmo::OPERATION::SCALE };
			const EditorIcon toolIcons[] = { EditorIcon::Hand, EditorIcon::Move,
				EditorIcon::Rotate, EditorIcon::Scale };
			for (int i = 0; i < 4; ++i)
			{
				ImVec2 min(topLeft.x + dockPadding + dockHandleWidth + dockGap + i * (dockButtonWidth + dockGap),
					topLeft.y + dockPadding);
				ImVec2 max(min.x + dockButtonWidth, min.y + dockButtonHeight);
				const bool selected = m_GizmoType == tools[i];
				draw->AddRectFilled(min, max, selected ? active : normal, 2.0f);
				draw->AddRect(min, max, selected ? active : IM_COL32(25, 25, 25, 255), 2.0f, 0, 1.0f);
				const ImVec2 center((min.x + max.x) * 0.5f, (min.y + max.y) * 0.5f);
				constexpr float dockIconRadius = 11.0f;
				DrawEditorIcon(draw, m_EditorIcons, toolIcons[i],
					ImVec2(center.x - dockIconRadius, center.y - dockIconRadius),
					ImVec2(center.x + dockIconRadius, center.y + dockIconRadius));
				ImGui::SetCursorScreenPos(min);
				ImGui::InvisibleButton((std::string("##scene_docked_tool_") + std::to_string(i)).c_str(),
					ImVec2(max.x - min.x, max.y - min.y));
				if (ImGui::IsItemClicked())
					m_GizmoType = tools[i];
			}
			ImGui::PopClipRect();
			return;
		}

		const float width = 52.0f;
		const float handleHeight = 28.0f;
		const float buttonHeight = 50.0f;
		const float gap = 2.0f;
		const float height = handleHeight + gap + buttonHeight * 4.0f + gap * 3.0f + 5.0f;

		// Never allow the palette to become stranded outside the Scene view.
		// While dragging, leave the offset free so the bar follows the mouse.
		if (!m_GizmoTransformToolbarDragging)
		{
			const float maxOffsetX = m_ViewportSize.x > width + 8.0f ? m_ViewportSize.x - width - 4.0f : 4.0f;
			const float maxOffsetY = m_ViewportSize.y > height + 8.0f ? m_ViewportSize.y - height - 4.0f : 4.0f;
			if (m_GizmoToolbarOffset.x < 4.0f) m_GizmoToolbarOffset.x = 4.0f;
			if (m_GizmoToolbarOffset.y < 4.0f) m_GizmoToolbarOffset.y = 4.0f;
			if (m_GizmoToolbarOffset.x > maxOffsetX) m_GizmoToolbarOffset.x = maxOffsetX;
			if (m_GizmoToolbarOffset.y > maxOffsetY) m_GizmoToolbarOffset.y = maxOffsetY;
		}

		ImVec2 topLeft(m_ViewportBounds[0].x + m_GizmoToolbarOffset.x,
			m_ViewportBounds[0].y + m_GizmoToolbarOffset.y);
		ImVec2 bottomRight(topLeft.x + width, topLeft.y + height);
		ImDrawList* draw = m_GizmoTransformToolbarDragging
			? ImGui::GetForegroundDrawList() : ImGui::GetWindowDrawList();
		const ImU32 outer = IM_COL32(40, 40, 40, 245);
		const ImU32 normal = IM_COL32(71, 71, 71, 245);
		const ImU32 active = IM_COL32(44, 93, 135, 255);

		draw->AddRectFilled(topLeft, bottomRight, outer, 2.0f);

		// The top handle is the only draggable area, matching the reference UI.
		ImVec2 handleMin(topLeft.x + 5.0f, topLeft.y + 4.0f);
		ImVec2 handleMax(topLeft.x + width - 5.0f, topLeft.y + handleHeight);
		ImVec2 handleCenter((handleMin.x + handleMax.x) * 0.5f, (handleMin.y + handleMax.y) * 0.5f);
		const ImU32 handleLine = IM_COL32(137, 137, 137, 255);
		for (int i = -1; i <= 1; ++i)
			draw->AddLine(ImVec2(handleCenter.x - 12.0f, handleCenter.y + i * 5.0f),
				ImVec2(handleCenter.x + 12.0f, handleCenter.y + i * 5.0f), handleLine, 2.0f);
		const bool transformWasDragging = m_GizmoTransformToolbarDragging;
		UI_SceneToolbarDragHandle("##scene_transform_toolbar", m_GizmoToolbarOffset,
			m_GizmoTransformToolbarDocked, m_GizmoTransformToolbarDragging,
			handleMin, handleMax, 0.0f, true);
		if (transformWasDragging && !m_GizmoTransformToolbarDragging && m_GizmoTransformToolbarDocked)
		{
			const float modeWidth = kSceneModeToolbarWidth;
			const float dockStartX = m_ViewportBounds[0].x + 8.0f;
			m_GizmoModeToolbarFirst = !m_GizmoModeToolbarDocked ||
				ImGui::GetMousePos().x >= dockStartX + modeWidth * 0.5f;
			SaveSceneToolbarLayout();
		}

		const int tools[] = { -1, ImGuizmo::OPERATION::TRANSLATE,
			ImGuizmo::OPERATION::ROTATE, ImGuizmo::OPERATION::SCALE };
		const EditorIcon toolIcons[] = { EditorIcon::Hand, EditorIcon::Move,
			EditorIcon::Rotate, EditorIcon::Scale };
		for (int i = 0; i < 4; ++i)
		{
			ImVec2 min(topLeft.x + 5.0f, topLeft.y + handleHeight + gap + i * (buttonHeight + gap));
			ImVec2 max(min.x + width - 10.0f, min.y + buttonHeight);
			bool selected = m_GizmoType == tools[i];
			draw->AddRectFilled(min, max, selected ? active : normal, 2.0f);

			const ImVec2 center((min.x + max.x) * 0.5f, (min.y + max.y) * 0.5f);
			constexpr float floatingIconRadius = 17.0f;
			DrawEditorIcon(draw, m_EditorIcons, toolIcons[i],
				ImVec2(center.x - floatingIconRadius, center.y - floatingIconRadius),
				ImVec2(center.x + floatingIconRadius, center.y + floatingIconRadius));

			ImGui::SetCursorScreenPos(min);
			ImGui::InvisibleButton((std::string("##scene_tool_") + std::to_string(i)).c_str(),
				ImVec2(max.x - min.x, max.y - min.y));
			if (ImGui::IsItemClicked())
				m_GizmoType = tools[i];
		}
		ImGui::PopClipRect();
	}

	void EditorLayer::UI_SceneGizmoModeToolbarOverlay()
	{
		const float padding = kSceneToolbarPadding;
		const float handleWidth = kSceneToolbarHandleWidth;
		const float buttonWidth = kSceneModeButtonWidth;
		const float buttonHeight = 28.0f;
		const float gap = kSceneToolbarItemGap;
		const float height = buttonHeight + padding * 2.0f;
		const float width = kSceneModeToolbarWidth;
		// The Q/W/E/R toolbar uses the same strip when docked.  Keep these
		// dimensions here (and in its renderer below) so insertion previews and
		// the two bars always agree about their occupied widths.
		const float transformWidth = kSceneTransformToolbarWidth;
		const float dockGap = kSceneToolbarDockGap;

		// While dragging, let the bar follow the mouse freely so it can cover the
		// Scene top strip.  When idle, keep it inside the viewport while still
		// allowing it to sit over that strip if the user parked it there.
		if (!m_GizmoModeToolbarDragging)
		{
			const float maxOffsetX = m_ViewportSize.x > width + 8.0f ? m_ViewportSize.x - width - 4.0f : 4.0f;
			const float minOffsetY = m_GizmoModeDockY - m_ViewportBounds[0].y + 1.0f;
			const float maxOffsetY = m_ViewportSize.y > height + 8.0f ? m_ViewportSize.y - height - 4.0f : 4.0f;
			if (m_GizmoModeToolbarOffset.x < 4.0f) m_GizmoModeToolbarOffset.x = 4.0f;
			if (m_GizmoModeToolbarOffset.y < minOffsetY) m_GizmoModeToolbarOffset.y = minOffsetY;
			if (m_GizmoModeToolbarOffset.x > maxOffsetX) m_GizmoModeToolbarOffset.x = maxOffsetX;
			if (m_GizmoModeToolbarOffset.y > maxOffsetY) m_GizmoModeToolbarOffset.y = maxOffsetY;
		}

		// Dock to the same strip used by the drop preview. Extend the ImGui item
		// clip rectangle as well as the drawing clip so the handle stays interactive.
		const float dockStartX = m_ViewportBounds[0].x + 8.0f;
		const float modeDockX = (m_GizmoModeToolbarDocked && m_GizmoTransformToolbarDocked && !m_GizmoModeToolbarFirst)
			? dockStartX + transformWidth + dockGap : dockStartX;
		const float modeDockOffsetY = (m_GizmoModeDockHeight - height) * 0.5f;
		ImVec2 topLeft = m_GizmoModeToolbarDocked
			? ImVec2(modeDockX, m_GizmoModeDockY + modeDockOffsetY)
			: ImVec2(m_ViewportBounds[0].x + m_GizmoModeToolbarOffset.x,
				m_ViewportBounds[0].y + m_GizmoModeToolbarOffset.y);
		ImVec2 bottomRight(topLeft.x + width, topLeft.y + height);

		const ImVec2 savedCursor = ImGui::GetCursorScreenPos();
		const float toolbarClipTop = (m_GizmoModeToolbarDragging || m_GizmoTransformToolbarDragging)
			? ImGui::GetWindowPos().y : m_GizmoModeDockY;
		ImGui::PushClipRect(ImVec2(ImGui::GetWindowPos().x, toolbarClipTop),
			ImVec2(m_ViewportBounds[1].x, m_ViewportBounds[1].y), false);
		// Keep the idle toolbar body in the Scene window layer. Only the active
		// toolbar is promoted to the viewport foreground while it is dragged.
		ImDrawList* dockDraw = ImGui::GetWindowDrawList();
		ImDrawList* draw = m_GizmoModeToolbarDragging
			? ImGui::GetForegroundDrawList() : dockDraw;
		const ImU32 outer = IM_COL32(40, 40, 40, 245);
		const ImU32 normal = IM_COL32(71, 71, 71, 245);
		const ImU32 hover = IM_COL32(98, 98, 98, 245);
		const ImU32 line = IM_COL32(196, 196, 196, 255);
		const ImU32 arrow = IM_COL32(137, 137, 137, 255);
		const ImU32 accent = IM_COL32(212, 127, 42, 255);
		// The caller paints the Scene menu-bar overlay before entering this helper.
		// Keep this pass focused on the toolbar body; the insertion preview is a
		// final shared pass so neither toolbar can cover it based on call order.

		draw->AddRectFilled(topLeft, bottomRight, outer, 2.0f);

		ImVec2 handleMin(topLeft.x + padding, topLeft.y + padding);
		ImVec2 handleMax(handleMin.x + handleWidth, topLeft.y + height - padding);
		ImVec2 handleCenter((handleMin.x + handleMax.x) * 0.5f, (handleMin.y + handleMax.y) * 0.5f);
		const ImU32 handleLine = IM_COL32(137, 137, 137, 255);
		for (int i = -1; i <= 1; ++i)
			draw->AddLine(ImVec2(handleCenter.x - 7.0f, handleCenter.y + i * 4.0f),
				ImVec2(handleCenter.x + 7.0f, handleCenter.y + i * 4.0f), handleLine, 2.0f);

		const bool modeWasDragging = m_GizmoModeToolbarDragging;
		UI_SceneToolbarDragHandle("##scene_gizmo_mode", m_GizmoModeToolbarOffset,
			m_GizmoModeToolbarDocked, m_GizmoModeToolbarDragging,
			handleMin, handleMax, 17.0f, true);
		if (modeWasDragging && !m_GizmoModeToolbarDragging && m_GizmoModeToolbarDocked)
		{
			m_GizmoModeToolbarFirst = !m_GizmoTransformToolbarDocked ||
				ImGui::GetMousePos().x < dockStartX + transformWidth * 0.5f;
			SaveSceneToolbarLayout();
		}

		auto DrawFrame = [&](const ImVec2& min, const ImVec2& max, bool hovered)
		{
			// Pivot and space choices are represented by their icons. Keep each button
			// surface neutral after a click; only pointer hover may tint it.
			draw->AddRectFilled(min, max, hovered ? hover : normal, 2.0f);
			draw->AddRect(min, max, IM_COL32(25, 25, 25, 255), 2.0f, 0, 1.0f);
		};

		auto DrawDropArrow = [&](const ImVec2& min, const ImVec2& max)
		{
			const ImVec2 c(max.x - 8.0f, (min.y + max.y) * 0.5f + 1.0f);
			draw->AddTriangleFilled(ImVec2(c.x - 3.0f, c.y - 2.0f), ImVec2(c.x + 3.0f, c.y - 2.0f), ImVec2(c.x, c.y + 2.5f), arrow);
		};

		auto DrawPivotIcon = [&](const ImVec2& min, const ImVec2& max)
		{
			const ImVec2 c((min.x + max.x) * 0.5f - 3.5f, (min.y + max.y) * 0.5f);
			draw->AddRect(ImVec2(c.x - 9.0f, c.y - 9.0f), ImVec2(c.x + 9.0f, c.y + 9.0f), line, 0.0f, 0, 1.8f);
			draw->AddCircleFilled(ImVec2(c.x - 5.0f, c.y + 5.0f), 3.0f, accent);
		};

		auto DrawCenterIcon = [&](const ImVec2& min, const ImVec2& max)
		{
			const ImVec2 c((min.x + max.x) * 0.5f - 3.5f, (min.y + max.y) * 0.5f);
			draw->AddRect(ImVec2(c.x - 9.0f, c.y - 9.0f), ImVec2(c.x + 9.0f, c.y + 9.0f), line, 0.0f, 0, 1.8f);
			draw->AddCircleFilled(c, 3.0f, accent);
		};

		auto DrawLocalIcon = [&](const ImVec2& min, const ImVec2& max)
		{
			const ImVec2 c((min.x + max.x) * 0.5f - 3.5f, (min.y + max.y) * 0.5f);
			draw->AddRect(ImVec2(c.x - 9.0f, c.y - 8.0f), ImVec2(c.x + 9.0f, c.y + 9.0f), line, 0.0f, 0, 1.8f);
			draw->AddLine(ImVec2(c.x - 8.0f, c.y - 3.0f), ImVec2(c.x + 1.5f, c.y - 9.0f), line, 2.0f);
			draw->AddLine(ImVec2(c.x + 1.5f, c.y - 9.0f), ImVec2(c.x + 8.0f, c.y - 1.5f), line, 2.0f);
			draw->AddLine(ImVec2(c.x + 8.0f, c.y - 1.5f), ImVec2(c.x + 8.0f, c.y + 8.0f), line, 2.0f);
			draw->AddCircleFilled(ImVec2(c.x - 3.5f, c.y + 4.0f), 2.8f, accent);
		};

		auto DrawWorldIcon = [&](const ImVec2& min, const ImVec2& max)
		{
			const ImVec2 c((min.x + max.x) * 0.5f - 3.5f, (min.y + max.y) * 0.5f);
			draw->AddCircle(c, 9.0f, line, 24, 1.8f);
			draw->AddLine(ImVec2(c.x - 9.0f, c.y), ImVec2(c.x + 9.0f, c.y), line, 1.8f);
			draw->AddLine(ImVec2(c.x, c.y - 9.0f), ImVec2(c.x, c.y + 9.0f), line, 1.8f);
			draw->AddCircleFilled(ImVec2(c.x + 3.0f, c.y - 3.0f), 2.8f, accent);
		};

		const float buttonMinY = topLeft.y + padding;
		const ImVec2 pivotMin(topLeft.x + padding + handleWidth + gap, buttonMinY);
		const ImVec2 pivotMax(pivotMin.x + buttonWidth, pivotMin.y + buttonHeight);
		const ImVec2 spaceMin(pivotMax.x + gap, buttonMinY);
		const ImVec2 spaceMax(spaceMin.x + buttonWidth, spaceMin.y + buttonHeight);

		ImGui::SetCursorScreenPos(pivotMin);
		ImGui::InvisibleButton("##scene_gizmo_pivot_mode", ImVec2(pivotMax.x - pivotMin.x, pivotMax.y - pivotMin.y));
		const bool pivotHovered = ImGui::IsItemHovered();
		if (ImGui::IsItemClicked())
			ImGui::OpenPopup("##scene_gizmo_pivot_popup");
		DrawFrame(pivotMin, pivotMax, pivotHovered);
		if (m_GizmoPivotMode == GizmoPivotMode::Pivot)
			DrawPivotIcon(pivotMin, pivotMax);
		else
			DrawCenterIcon(pivotMin, pivotMax);
		DrawDropArrow(pivotMin, pivotMax);
		if (ImGui::BeginPopup("##scene_gizmo_pivot_popup"))
		{
			if (ImGui::MenuItem("Pivot", nullptr, m_GizmoPivotMode == GizmoPivotMode::Pivot))
				m_GizmoPivotMode = GizmoPivotMode::Pivot;
			if (ImGui::MenuItem("Center", nullptr, m_GizmoPivotMode == GizmoPivotMode::Center))
				m_GizmoPivotMode = GizmoPivotMode::Center;
			ImGui::EndPopup();
		}

		ImGui::SetCursorScreenPos(spaceMin);
		ImGui::InvisibleButton("##scene_gizmo_space_mode", ImVec2(spaceMax.x - spaceMin.x, spaceMax.y - spaceMin.y));
		const bool spaceHovered = ImGui::IsItemHovered();
		if (ImGui::IsItemClicked())
			ImGui::OpenPopup("##scene_gizmo_space_popup");
		DrawFrame(spaceMin, spaceMax, spaceHovered);
		if (m_GizmoSpaceMode == GizmoSpaceMode::Local)
			DrawLocalIcon(spaceMin, spaceMax);
		else
			DrawWorldIcon(spaceMin, spaceMax);
		DrawDropArrow(spaceMin, spaceMax);
		if (ImGui::BeginPopup("##scene_gizmo_space_popup"))
		{
			if (ImGui::MenuItem("Local", nullptr, m_GizmoSpaceMode == GizmoSpaceMode::Local))
				m_GizmoSpaceMode = GizmoSpaceMode::Local;
			if (ImGui::MenuItem("World", nullptr, m_GizmoSpaceMode == GizmoSpaceMode::World))
				m_GizmoSpaceMode = GizmoSpaceMode::World;
			ImGui::EndPopup();
		}
		ImGui::PopClipRect();
		ImGui::SetCursorScreenPos(savedCursor);
	}

	void EditorLayer::UI_SceneToolbarDockPreview()
	{
		const bool draggingMode = m_GizmoModeToolbarDragging;
		const bool draggingTransform = m_GizmoTransformToolbarDragging;
		if (!draggingMode && !draggingTransform)
			return;

		const ImVec2 mouse = ImGui::GetMousePos();
		if (mouse.y < m_GizmoModeDockY - 5.0f ||
			mouse.y > m_GizmoModeDockY + m_GizmoModeDockHeight + 5.0f)
			return;

		const float dockStartX = m_ViewportBounds[0].x + 8.0f;
		const float previewWidth = draggingMode
			? kSceneModeToolbarWidth : kSceneTransformToolbarWidth;
		const bool otherDocked = draggingMode
			? m_GizmoTransformToolbarDocked : m_GizmoModeToolbarDocked;
		const float otherWidth = draggingMode
			? kSceneTransformToolbarWidth : kSceneModeToolbarWidth;
		float previewX = dockStartX;
		if (otherDocked)
		{
			const bool insertBefore = mouse.x < dockStartX + otherWidth * 0.5f;
			previewX = insertBefore
				? dockStartX : dockStartX + otherWidth + kSceneToolbarDockGap;
		}
		previewX = std::max(dockStartX, std::min(previewX,
			m_ViewportBounds[1].x - previewWidth - 4.0f));

		// Submit this after both toolbar bodies and on the foreground layer. This
		// makes the insertion target equally visible in either drag direction.
		ImDrawList* previewDraw = ImGui::GetForegroundDrawList();
		previewDraw->PushClipRect(
			ImVec2(m_ViewportBounds[0].x, m_GizmoModeDockY),
			ImVec2(m_ViewportBounds[1].x, m_GizmoModeDockY + m_GizmoModeDockHeight), false);
		previewDraw->AddRectFilled(ImVec2(previewX, m_GizmoModeDockY),
			ImVec2(previewX + previewWidth, m_GizmoModeDockY + m_GizmoModeDockHeight),
			IM_COL32(44, 93, 135, 85), 1.0f);
		previewDraw->AddRect(ImVec2(previewX + 1.0f, m_GizmoModeDockY + 1.0f),
			ImVec2(previewX + previewWidth - 1.0f, m_GizmoModeDockY + m_GizmoModeDockHeight - 1.0f),
			IM_COL32(80, 165, 235, 230), 1.0f, 0, 1.0f);
		previewDraw->PopClipRect();
	}

	void EditorLayer::UI_Toolbar()
	{
		constexpr float iconSize = 24.0f;
		constexpr int framePadding = 4;
		const float buttonSize = iconSize + framePadding * 2.0f;
		const float spacing = ImGui::GetStyle().ItemSpacing.x;
		const float groupWidth = buttonSize * 4.0f + spacing * 3.0f;
		ImGui::SetCursorPosX(std::max(0.0f, (ImGui::GetWindowWidth() - groupWidth) * 0.5f));
		ImGui::SetCursorPosY(std::max(ImGui::GetCursorPosY(),
			(ImGui::GetWindowHeight() - buttonSize) * 0.5f));

		auto drawButton = [&](const char* id, EditorIcon icon, bool enabled,
			bool selected, const char* tooltip)
		{
			ImGui::PushID(id);
			const ImVec4 buttonColor = selected
				? ImGui::GetStyleColorVec4(ImGuiCol_HeaderActive)
				: ImGui::GetStyleColorVec4(ImGuiCol_Button);
			ImGui::PushStyleColor(ImGuiCol_Button, buttonColor);
			if (!enabled)
				ImGui::BeginDisabled();

			bool pressed = false;
			const Ref<Texture2D>& texture = m_EditorIcons->Get(icon);
			if (texture)
			{
				pressed = ImGui::ImageButton(ToImGuiTextureID(texture), ImVec2(iconSize, iconSize),
					ImVec2(0.0f, 1.0f), ImVec2(1.0f, 0.0f), framePadding,
					ImVec4(0.0f, 0.0f, 0.0f, 0.0f));
			}
			else
				pressed = ImGui::Button("?", ImVec2(buttonSize, buttonSize));

			if (ImGui::IsItemHovered())
				ImGui::SetTooltip("%s", tooltip);
			if (!enabled)
				ImGui::EndDisabled();
			ImGui::PopStyleColor();
			ImGui::PopID();
			return pressed && enabled;
		};

		const bool running = IsSceneRunning();
		if (drawButton("Play", EditorIcon::Play, m_EditorScene != nullptr,
			running, running ? "Stop" : "Play"))
		{
			if (running)
				OnSceneStop();
			else
				OnScenePlay();
		}
		ImGui::SameLine();
		if (drawButton("Pause", EditorIcon::Pause, running,
			m_SceneState == SceneState::Pause, m_SceneState == SceneState::Pause ? "Resume" : "Pause"))
			OnScenePause();
		ImGui::SameLine();
		if (drawButton("Step", EditorIcon::Step, m_SceneState == SceneState::Pause,
			false, "Step one fixed physics frame (1/60 s)"))
			OnSceneStep();
		ImGui::SameLine();
		if (drawButton("Stop", EditorIcon::Stop, running, false, "Stop"))
			OnSceneStop();
	}

	bool EditorLayer::IsSceneRunning() const
	{
		return m_SceneState != SceneState::Edit;
	}

	bool EditorLayer::PrepareManagedRuntime()
	{
		if (IsSceneRunning())
			return false;
		m_ScriptMetadata.Clear();
		Scripting::ScriptEngine::Get().SetRuntime({});
		auto fail = [this](std::string message)
		{
			m_ShowConsolePanel = true;
			m_PendingPanelFocus = "Console";
			m_ConsolePanel.Push(ConsoleMessageSeverity::Error, std::move(message),
				"Script Runtime");
			return false;
		};

		const std::filesystem::path managedDirectory =
			m_ScriptCompiler.GetManagedRuntimeDirectory();
		if (managedDirectory.empty())
			return fail("TomCat.ScriptHost outputs could not be located.");
		const std::filesystem::path assembly =
			m_ScriptCompiler.GetLastGoodAssemblyPath();
		std::filesystem::path pdb = assembly;
		pdb.replace_extension(".pdb");
		std::error_code pdbError;
		if (!std::filesystem::is_regular_file(pdb, pdbError) || pdbError)
			pdb.clear();

		std::string runtimeError;
		auto runtime = Scripting::CreateManagedScriptRuntime(managedDirectory,
			assembly, pdb, {}, &runtimeError);
		if (!runtime)
		{
			if (runtimeError.empty())
				runtimeError = "managed host initialization failed";
			return fail("The managed runtime could not be initialized: " + runtimeError);
		}

		std::string manifestJson;
		if (!runtime->ReadProjectMetadata(manifestJson))
			return fail("The generated C# script manifest could not be read.");
		std::string metadataError;
		if (!m_ScriptMetadata.ParseAndReplace(manifestJson, metadataError))
			return fail("The generated C# script manifest is invalid: " + metadataError);
		if (ReconcileManagedScriptFields(m_EditorScene))
			m_SceneDirty = true;

		Scripting::ScriptEngine::Get().SetRuntime(std::move(runtime));
		return true;
	}

	bool EditorLayer::ReconcileManagedScriptFields(const Ref<Scene>& scene)
	{
		if (!scene)
			return false;
		bool changed = false;
		for (UUID entityID : scene->m_EntityOrder)
		{
			Entity entity = scene->FindEntityByUUID(entityID);
			if (!entity || !entity.HasComponent<CSharpScripts>())
				continue;
			for (CSharpScriptEntry& entry :
				entity.GetComponent<CSharpScripts>().Scripts)
			{
				const std::optional<EditorScriptMetadata> metadata =
					m_ScriptMetadata.Find(entry.ScriptAsset);
				if (!metadata)
					continue;
				if (!metadata->TypeName.empty()
					&& entry.LastKnownClassName != metadata->TypeName)
				{
					entry.LastKnownClassName = metadata->TypeName;
					changed = true;
				}
				changed = ReconcileScriptEntryFields(entry, *metadata) || changed;
			}
		}
		return changed;
	}

	void EditorLayer::OnScenePlay()
	{
		if (m_SceneState != SceneState::Edit || !m_EditorScene)
			return;
		if (m_CurrentProject)
		{
			ScriptBuildResult completed;
			(void)m_ScriptCompiler.PollCompile(completed);
			if (m_ScriptCompiler.IsCompileInProgress())
			{
				m_ShowConsolePanel = true;
				m_PendingPanelFocus = "Console";
				m_ConsolePanel.Push(ConsoleMessageSeverity::Warning,
					"Play is waiting for the background C# compilation to finish.",
					"Editor");
				return;
			}
			if (!m_ScriptCompiler.RefreshSourceState() ||
				!m_ScriptCompiler.IsCurrentSourceBuilt())
			{
				m_ShowConsolePanel = true;
				m_PendingPanelFocus = "Console";
				m_ScriptCompiler.StartCompile(true);
				m_ConsolePanel.Push(ConsoleMessageSeverity::Error,
					"Play was blocked because the current C# sources have no validated "
					"build. Compilation is running in the background.",
					"Editor");
				return;
			}
		}
		if (m_CurrentProject && !PrepareManagedRuntime())
			return;
		m_ActiveScene = Scene::Copy(m_EditorScene);
		if (!m_ActiveScene)
			return;
		ResizeSceneForGameView(m_ActiveScene);
		if (!m_ActiveScene->OnRuntimeStart())
		{
			m_ActiveScene = m_EditorScene;
			m_ShowConsolePanel = true;
			m_PendingPanelFocus = "Console";
			m_ConsolePanel.Push(ConsoleMessageSeverity::Error,
				"Play could not start because runtime initialization failed. See the preceding diagnostics.",
				"Editor");
			return;
		}
		m_SceneState = SceneState::Play;
		m_PlayScriptDirtyNoticeShown = false;
		m_StepRequested = false;
		m_SceneHierarchyPanel.SetColliderEditingAllowed(false);
		m_SceneHierarchyPanel.SetContext(m_ActiveScene, false, true);
		ResetSceneInteractionState();

		// 切换到Game窗口焦点
		ImGui::SetWindowFocus("Game");

	}

	void EditorLayer::OnScenePause()
	{
		if (m_SceneState == SceneState::Play)
			m_SceneState = SceneState::Pause;
		else if (m_SceneState == SceneState::Pause)
			m_SceneState = SceneState::Play;
		m_StepRequested = false;
	}

	void EditorLayer::OnSceneStep()
	{
		if (m_SceneState == SceneState::Pause)
			m_StepRequested = true;
	}

	void EditorLayer::OnSceneStop()
	{
		if (!IsSceneRunning())
			return;

		Ref<Scene> runtimeScene = m_ActiveScene;
		if (runtimeScene)
			runtimeScene->OnRuntimeStop();
		m_ActiveScene = m_EditorScene;
		m_SceneState = SceneState::Edit;
		m_PlayScriptDirtyNoticeShown = false;
		m_ScriptSourcePollCountdown = 0.0f;
		m_ScriptCompileDebounceRemaining = 0.35f;
		m_StepRequested = false;
		ResizeSceneForGameView(m_ActiveScene);
		m_SceneHierarchyPanel.SetContext(m_ActiveScene, false, true);
		m_SceneHierarchyPanel.SetColliderEditingAllowed(true);
		ResetSceneInteractionState();

		// 切换到Scene窗口焦点
		ImGui::SetWindowFocus("Scene");
	}

	void EditorLayer::OnEvent(Event& e)
	{
		if (m_ViewportCanvasHovered)
			m_EditorCamera.OnEvent(e);

		EventDispatcher dispatcher(e);
		dispatcher.Dispatch<WindowCloseEvent>(TC_Bind_Event_Fn(EditorLayer::OnWindowClose));
		dispatcher.Dispatch<KeyPressedEvent>(TC_Bind_Event_Fn(EditorLayer::OnKeyPressed));
		dispatcher.Dispatch<MouseButtonPressedEvent>(TC_Bind_Event_Fn(EditorLayer::OnMouseButtonPressed));
		dispatcher.Dispatch<MouseButtonReleasedEvent>(TC_Bind_Event_Fn(EditorLayer::OnMouseButtonReleased));
	}

	bool EditorLayer::OnKeyPressed(KeyPressedEvent& e)
	{
		// Shortcuts
		if (e.GetRepeatCount() > 0)
			return false;

		const bool control = e.IsControlDown();
		const bool shift = e.IsShiftDown();
		const bool alt = e.IsAltDown();
		const bool super = e.IsSuperDown();
		bool handled = false;
		switch (e.GetKeyCode())
		{
		case Key::N:
		{
			if (control && !shift && !alt && !super)
			{
				NewScene();
				handled = true;
			}

			break;
		}
		case Key::O:
		{
			if (control && !shift && !alt && !super)
			{
				OpenScene();
				handled = true;
			}

			break;
		}
		case Key::S:
		{
			if (control && !alt && !super)
			{
				if (shift)
					SaveSceneAs();
				else
					SaveScene();
				handled = true;
			}

			break;
		}
		case Key::Escape:
		{
			if (!ImGui::GetIO().WantTextInput && !control && !shift && !alt && !super &&
				m_SceneHierarchyPanel.IsEditingCollider())
			{
				m_SceneHierarchyPanel.ClearColliderEditMode();
				ResetColliderEditState();
				handled = true;
			}
			break;
		}

		// Scene commands only belong to the Scene canvas or Hierarchy. This keeps
		// Delete/Cut/Copy/Paste from leaking out of text fields and Project assets.
		case Key::D:
		{
			if (!ImGui::GetIO().WantTextInput &&
				(m_ViewportFocused || m_SceneHierarchyPanel.IsHierarchyFocused()) &&
				control && !shift && !alt && !super)
			{
				handled = m_SceneHierarchyPanel.HandleShortcut(e.GetKeyCode(), control);
				if (handled && m_SceneHierarchyPanel.HasPendingRenameFocus())
					FocusEditorPanel("Hierarchy", m_ShowHierarchyPanel);
			}

			break;
		}
		case Key::X:
		case Key::C:
		case Key::V:
		{
			if (!ImGui::GetIO().WantTextInput &&
				(m_ViewportFocused || m_SceneHierarchyPanel.IsHierarchyFocused()) &&
				control && !shift && !alt && !super)
			{
				handled = m_SceneHierarchyPanel.HandleShortcut(e.GetKeyCode(), control);
				if (handled && m_SceneHierarchyPanel.HasPendingRenameFocus())
					FocusEditorPanel("Hierarchy", m_ShowHierarchyPanel);
			}
			break;
		}
		case Key::F2:
		case Key::Delete:
		{
			if (!ImGui::GetIO().WantTextInput &&
				(m_ViewportFocused || m_SceneHierarchyPanel.IsHierarchyFocused()) &&
				!control && !shift && !alt && !super)
			{
				handled = m_SceneHierarchyPanel.HandleShortcut(e.GetKeyCode(), control);
				if (handled && m_SceneHierarchyPanel.HasPendingRenameFocus())
					FocusEditorPanel("Hierarchy", m_ShowHierarchyPanel);
			}
			break;
		}

		// Gizmos
		case Key::Q:
		{
			if (m_ViewportFocused && !control && !shift && !alt && !super && !ImGuizmo::IsUsing())
			{
				m_GizmoType = -1;
				handled = true;
			}
			break;
		}
		case Key::W:
		{
			if (m_ViewportFocused && !control && !shift && !alt && !super && !ImGuizmo::IsUsing())
			{
				m_GizmoType = ImGuizmo::OPERATION::TRANSLATE;
				handled = true;
			}
			break;
		}
		case Key::E:
		{
			if (m_ViewportFocused && !control && !shift && !alt && !super && !ImGuizmo::IsUsing())
			{
				m_GizmoType = ImGuizmo::OPERATION::ROTATE;
				handled = true;
			}
			break;
		}
		case Key::R:
		{
			if (m_ViewportFocused && !control && !shift && !alt && !super && !ImGuizmo::IsUsing())
			{
				m_GizmoType = ImGuizmo::OPERATION::SCALE;
				handled = true;
			}
			break;
		}
		}

		return handled;
	}

	bool EditorLayer::OnMouseButtonPressed(MouseButtonPressedEvent& e)
	{
		const int button = e.GetMouseButton();
		const bool altDown = e.IsAltDown();
		const bool cameraButton = button == Mouse::ButtonMiddle || button == Mouse::ButtonRight ||
			(button == Mouse::ButtonLeft && altDown);
		if (cameraButton && m_ViewportFocused && m_ViewportCanvasHovered)
		{
			m_ViewportCameraDragOwned = true;
			return true;
		}

		if (e.GetMouseButton() == Mouse::ButtonLeft)
		{
			if (m_ColliderHandleHovered || m_ActiveColliderHandle != ColliderEditHandle::None)
				return true;
			if (m_ViewportCanvasHovered && !ImGuizmo::IsOver() && !altDown)
			{
				m_SceneHierarchyPanel.SetSelectedEntity(m_HoveredEntity);
				return true;
			}
		}
		return false;
	}

	bool EditorLayer::OnMouseButtonReleased(MouseButtonReleasedEvent& e)
	{
		const int button = e.GetMouseButton();
		if (button == Mouse::ButtonLeft && m_ActiveColliderHandle != ColliderEditHandle::None)
			return true;
		if (m_ViewportCameraDragOwned &&
			(button == Mouse::ButtonLeft || button == Mouse::ButtonMiddle || button == Mouse::ButtonRight))
		{
			m_ViewportCameraDragOwned = false;
			return true;
		}
		return false;
	}

	bool EditorLayer::OnWindowClose(WindowCloseEvent&)
	{
		RequestExit();
		return true;
	}


	void EditorLayer::NewScene()
	{
		if (m_SceneDirty)
		{
			RequestDestructiveAction([this]() {
				NewScene();
				return true;
			});
			return;
		}
		if (IsSceneRunning())
			OnSceneStop();
		m_EditorScene = CreateRef<Scene>();
		m_EditorScene->SetSceneName("Untitled");
		m_ActiveScene = m_EditorScene;
		AddDefaultMainCamera();
		ResizeSceneForGameView(m_ActiveScene);
		m_SceneHierarchyPanel.SetContext(m_ActiveScene);
		m_SceneHierarchyPanel.SetSelectedEntity({});
		m_SceneDirty = true;

		m_EditorScenePath = std::filesystem::path();
		m_ContentBrowserPanel.SetActiveScenePath({});
		ResetSceneInteractionState();
	}

	void EditorLayer::AddDefaultMainCamera()
	{
		if (!m_ActiveScene)
			return;

		Entity mainCamera = m_ActiveScene->CreateEntity("MainCamera");
		auto& camera = mainCamera.AddComponent<C_Camera>();
		if (m_Is2DMode)
			camera._Camera.SetOrthographic(10.0f, -1.0f, 1.0f);
		else
			camera._Camera.SetPerspective(glm::radians(45.0f), 0.01f, 1000.0f);
	}

	bool EditorLayer::OpenProjectStartScene()
	{
		if (!m_CurrentProject)
			return false;
		const AssetHandle startSceneHandle = m_CurrentProject->GetConfig().StartSceneHandle;
		if (static_cast<uint64_t>(startSceneHandle) == 0)
		{
			TC_Core_Error("Project has no start scene: StartSceneHandle is 0");
			NewScene();
			return false;
		}

		std::filesystem::path startScene;
		const AssetMetadata* metadata = AssetManager::Get().GetRegistry().GetMetadata(startSceneHandle);
		if (metadata && metadata->Type == AssetType::Scene && !metadata->IsMissing)
			startScene = AssetManager::Get().ResolvePath(startSceneHandle);
		else
			TC_Core_Error("Project start scene handle is missing or is not a Scene: {0}",
				static_cast<uint64_t>(startSceneHandle));
		std::error_code error;
		if (!startScene.empty() && std::filesystem::is_regular_file(startScene, error))
		{
			if (OpenScene(startScene))
				return true;
			TC_Core_Error("Failed to load project start scene: {0}", PathToUTF8(startScene));
		}
		else
		{
			TC_Warn("Project start scene is missing: {0}", startScene.empty()
				? std::to_string(static_cast<uint64_t>(startSceneHandle))
				: PathToUTF8(startScene));
		}

		// A project switch must never leave the previous project's scene or path
		// active when the new start scene is missing or malformed.
		NewScene();
		return false;
	}

	bool EditorLayer::OpenScene()
	{
		const std::filesystem::path filepath = FileDialogs::OpenFile("TomCat Scene (*.tomcat)\0*.tomcat\0");
		return !filepath.empty() && OpenScene(filepath);
	}

	bool EditorLayer::OpenScene(const std::filesystem::path& path)
	{
		std::string extension = PathToUTF8(path.extension());
		std::transform(extension.begin(), extension.end(), extension.begin(),
			[](unsigned char c) { return static_cast<char>(std::tolower(c)); });
		if (extension != ".tomcat")
		{
			TC_Warn("Could not load {0} - not a scene file", PathToUTF8(path.filename()));
			return false;
		}
		if (m_SceneDirty)
		{
			RequestDestructiveAction([this, path]() { return OpenScene(path); });
			return false;
		}
		if (m_CurrentProject)
		{
			const AssetHandle handle = AssetManager::Get().ImportAsset(path);
			const AssetMetadata* metadata = AssetManager::Get().GetRegistry().GetMetadata(handle);
			if (static_cast<uint64_t>(handle) == 0 || !metadata || metadata->IsMissing ||
				metadata->Type != AssetType::Scene)
			{
				TC_Warn("Project scenes must be registered .tomcat assets inside Assets: {0}",
					PathToUTF8(path));
				return false;
			}
		}

		Ref<Scene> newScene = CreateRef<Scene>();
		SceneSerializer serializer(newScene);
		if (!serializer.Deserialize(path))
			return false;

		if (IsSceneRunning())
			OnSceneStop();
		newScene->SetSceneName(PathToUTF8(path.stem()));
		m_EditorScene = newScene;
		ResizeSceneForGameView(m_EditorScene);
		m_SceneHierarchyPanel.SetContext(m_EditorScene);

		m_ActiveScene = m_EditorScene;
		m_EditorScenePath = AbsoluteLexicalPath(path);
		m_ContentBrowserPanel.SetActiveScenePath(m_EditorScenePath);
		m_SceneDirty = false;
		ResetSceneInteractionState();
		return true;
	}

	void EditorLayer::SaveScene()
	{
		if (!m_EditorScene)
			return;
		if (!m_EditorScenePath.empty())
		{
			if (SerializeScene(m_EditorScene, m_EditorScenePath))
				m_SceneDirty = false;
		}
		else
			SaveSceneAs();
	}	

	void EditorLayer::SaveSceneAs()
	{
		if (!m_EditorScene)
			return;
		std::filesystem::path filepath = FileDialogs::SaveFile("TomCat Scene (*.tomcat)\0*.tomcat\0");
		if (!filepath.empty())
		{
			std::filesystem::path path = std::move(filepath);
			std::string extension = PathToUTF8(path.extension());
			std::transform(extension.begin(), extension.end(), extension.begin(),
				[](unsigned char c) { return static_cast<char>(std::tolower(c)); });
			if (extension != ".tomcat")
				path.replace_extension(".tomcat");
			if (SerializeScene(m_EditorScene, path))
			{
				m_EditorScenePath = AbsoluteLexicalPath(path);
				m_ContentBrowserPanel.SetActiveScenePath(m_EditorScenePath);
				m_SceneDirty = false;
			}
		}
	}

	bool EditorLayer::SerializeScene(const Ref<Scene>& scene, const std::filesystem::path& path)
	{
		if (!scene || path.empty())
			return false;
		const std::string previousName = scene->GetSceneName();
		scene->SetSceneName(PathToUTF8(path.stem()));
		SceneSerializer serializer(scene);
		if (!serializer.Serialize(path))
		{
			scene->SetSceneName(previousName);
			return false;
		}
		return true;
	}

	void EditorLayer::ResizeSceneForGameView(const Ref<Scene>& scene)
	{
		const uint32_t width = ToFramebufferExtent(m_GameViewportSize.x);
		const uint32_t height = ToFramebufferExtent(m_GameViewportSize.y);
		if (scene && width > 0 && height > 0)
			scene->OnViewportResize(width, height);
	}

	void EditorLayer::ResetSceneInteractionState()
	{
		m_HoveredEntity = {};
		m_ViewportCameraDragOwned = false;
		ResetColliderEditState();
	}

	void EditorLayer::RequestDestructiveAction(std::function<bool()> action)
	{
		if (!m_SceneDirty)
		{
			action();
			return;
		}
		m_PendingUnsavedAction = std::move(action);
		m_OpenUnsavedChangesModal = true;
	}

	void EditorLayer::UI_UnsavedChangesModal()
	{
		if (m_OpenUnsavedChangesModal)
		{
			ImGui::OpenPopup("Unsaved Scene Changes");
			m_OpenUnsavedChangesModal = false;
		}

		std::function<bool()> actionToRun;
		bool restoreDirtyOnFailure = false;
		if (ImGui::BeginPopupModal("Unsaved Scene Changes", nullptr, ImGuiWindowFlags_AlwaysAutoResize))
		{
			ImGui::TextUnformatted("The current scene has unsaved changes.");
			ImGui::TextUnformatted("Save before continuing?");
			ImGui::Separator();
			if (ImGui::Button("Save"))
			{
				SaveScene();
				if (!m_SceneDirty)
				{
					actionToRun = std::move(m_PendingUnsavedAction);
					ImGui::CloseCurrentPopup();
				}
			}
			ImGui::SameLine();
			if (ImGui::Button("Discard"))
			{
				m_SceneDirty = false;
				actionToRun = std::move(m_PendingUnsavedAction);
				restoreDirtyOnFailure = true;
				ImGui::CloseCurrentPopup();
			}
			ImGui::SameLine();
			if (ImGui::Button("Cancel"))
			{
				m_PendingUnsavedAction = {};
				ImGui::CloseCurrentPopup();
			}
			ImGui::EndPopup();
		}
		if (actionToRun && !actionToRun() && restoreDirtyOnFailure)
			m_SceneDirty = true;
	}

	void EditorLayer::RequestExit()
	{
		RequestDestructiveAction([]() {
			Application::Get().Close();
			return true;
		});
	}

	bool EditorLayer::OpenProject()
	{
		const std::filesystem::path filepath = FileDialogs::OpenFile("TomCat Project (*.tcproj)\0*.tcproj\0");
		if (filepath.empty())
			return false;

		const std::filesystem::path projectPath = filepath;
		if (m_SceneDirty)
		{
			RequestDestructiveAction([this, projectPath]() { return OpenProject(projectPath); });
			return false;
		}
		return OpenProject(projectPath);
	}

	bool EditorLayer::OpenProject(const std::filesystem::path& path)
	{
		if (m_SceneDirty)
		{
			RequestDestructiveAction([this, path]() { return OpenProject(path); });
			return false;
		}

		// Persist the current layout before ProjectManager changes the active
		// project. No-project mode writes to the global LocalAppData layout.
		SaveImGuiSettingsPreservingCustomSections(GetEditorLayoutPath(m_CurrentProject));
		m_ContentBrowserPanel.SaveLayoutSetting();
		SaveSceneToolbarLayout();
		if (m_CurrentProject)
			m_ContentBrowserPanel.Serialize();
		auto project = ProjectManager::Get().LoadProject(path);
		if (!project)
			return false;

		if (IsSceneRunning())
			OnSceneStop();
		m_CurrentProject = project;
		m_SceneHierarchyPanel.SetProject(m_CurrentProject);
		if (m_ShowProjectSettingsPanel)
			LoadProjectSettingsDraft();
		m_Is2DMode = project->GetConfig().Template == "2D";
		m_EditorCamera.Set2DMode(m_Is2DMode);

		// Reapply the packaged baseline before the new project's override so UI
		// state never carries over from the project that was just closed.
		LoadImGuiSettings(GetDefaultEditorLayoutPath());
		LoadImGuiSettings(GetEditorLayoutPath(m_CurrentProject));
		LoadSceneToolbarLayout();

		m_ContentBrowserPanel.SetProject(m_CurrentProject);
		m_ScriptMetadata.Clear();
		Scripting::ScriptEngine::Get().SetRuntime({});
		if (m_ScriptCompiler.Configure(m_CurrentProject))
		{
			ResetScriptCompileTracking();
			if (m_ScriptCompiler.IsCurrentSourceBuilt())
				PrepareManagedRuntime();
		}
		OpenProjectStartScene();
		return true;
	}

	void EditorLayer::SaveProject()
	{
		if (m_CurrentProject)
			m_ContentBrowserPanel.Serialize();
		SaveImGuiSettingsPreservingCustomSections(GetEditorLayoutPath(m_CurrentProject));
		m_ContentBrowserPanel.SaveLayoutSetting();
		SaveSceneToolbarLayout();
	}

}
