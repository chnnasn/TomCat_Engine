#include "PlayerBuilder.h"

#include <TomCat/Asset/AssetManager.h>
#include <TomCat/Core/Log.h>
#include <TomCat/Project/Project.h>
#include <TomCat/Runtime/RuntimeCompatibility.h>
#include <TomCat/Utils/PathUtils.h>

#include <algorithm>
#include <array>
#include <cctype>
#include <fstream>
#include <iomanip>
#include <initializer_list>
#include <limits>
#include <set>
#include <sstream>
#include <stdexcept>
#include <string_view>
#include <system_error>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include <yaml-cpp/yaml.h>

#ifdef TC_PLATFORM_WINDOWS
	#include <Windows.h>
	#include <bcrypt.h>
	#pragma comment(lib, "bcrypt.lib")
#endif

namespace TomCat {
	namespace {

		struct TemplateFile
		{
			std::filesystem::path RelativePath;
			std::string SHA256;
		};

		struct TemplateManifest
		{
			std::vector<TemplateFile> Files;
		};

		std::string LowerASCII(std::string value)
		{
			std::transform(value.begin(), value.end(), value.begin(),
				[](unsigned char character)
				{
					return static_cast<char>(std::tolower(character));
				});
			return value;
		}

		std::string NormalizedKey(const std::filesystem::path& path)
		{
			std::string value = LowerASCII(PathToUTF8(path.lexically_normal()));
			std::replace(value.begin(), value.end(), '\\', '/');
			return value;
		}

		bool IsSafeRelativePath(const std::filesystem::path& path)
		{
			if (path.empty() || path.is_absolute() || path.has_root_name() ||
				path.has_root_directory() || path == ".")
				return false;
			for (const auto& component : path.lexically_normal())
			{
				if (component == ".." || component == ".")
					return false;
			}
			return true;
		}

		bool IsReparsePoint(const std::filesystem::path& path)
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

		bool IsLowerHex256(const std::string& value)
		{
			if (value.size() != 64)
				return false;
			return std::all_of(value.begin(), value.end(), [](char character)
				{
					return (character >= '0' && character <= '9') ||
						(character >= 'a' && character <= 'f');
				});
		}

		bool IsSafeBuildID(std::string_view value)
		{
			if (value.empty() || value.size() > 128 || value == "." || value == "..")
				return false;
			return std::all_of(value.begin(), value.end(), [](unsigned char character)
			{
				return std::isalnum(character) || character == '-' ||
					character == '_' || character == '.';
			});
		}

		bool IsDotNet10Version(std::string_view value)
		{
			if (!value.starts_with("10."))
				return false;
			uint32_t componentCount = 0;
			bool hasDigit = false;
			for (const char character : value)
			{
				if (character >= '0' && character <= '9')
				{
					hasDigit = true;
					continue;
				}
				if (character != '.' || !hasDigit)
					return false;
				++componentCount;
				hasDigit = false;
			}
			return hasDigit && componentCount == 2;
		}

		std::vector<std::string> PathComponents(
			const std::filesystem::path& relative)
		{
			std::vector<std::string> components;
			for (const auto& component : relative.lexically_normal())
				components.emplace_back(LowerASCII(PathToUTF8(component)));
			return components;
		}

		bool ComputeFileSHA256(const std::filesystem::path& path,
			std::string& digest, std::string& errorMessage)
		{
#ifdef TC_PLATFORM_WINDOWS
			BCRYPT_ALG_HANDLE algorithm = nullptr;
			BCRYPT_HASH_HANDLE hash = nullptr;
			auto closeHandles = [&]()
			{
				if (hash)
					BCryptDestroyHash(hash);
				if (algorithm)
					BCryptCloseAlgorithmProvider(algorithm, 0);
			};
			if (BCryptOpenAlgorithmProvider(&algorithm, BCRYPT_SHA256_ALGORITHM,
				nullptr, 0) < 0)
			{
				errorMessage = "Windows could not initialize SHA-256.";
				return false;
			}

			ULONG objectLength = 0;
			ULONG hashLength = 0;
			ULONG written = 0;
			if (BCryptGetProperty(algorithm, BCRYPT_OBJECT_LENGTH,
				reinterpret_cast<PUCHAR>(&objectLength), sizeof(objectLength),
				&written, 0) < 0 ||
				BCryptGetProperty(algorithm, BCRYPT_HASH_LENGTH,
					reinterpret_cast<PUCHAR>(&hashLength), sizeof(hashLength),
					&written, 0) < 0 || hashLength != 32)
			{
				closeHandles();
				errorMessage = "Windows returned an invalid SHA-256 provider.";
				return false;
			}

			std::vector<uint8_t> object(objectLength);
			std::array<uint8_t, 32> bytes{};
			if (BCryptCreateHash(algorithm, &hash, object.data(), objectLength,
				nullptr, 0, 0) < 0)
			{
				closeHandles();
				errorMessage = "Windows could not create a SHA-256 hash.";
				return false;
			}

			std::ifstream input(path, std::ios::binary);
			if (!input)
			{
				closeHandles();
				errorMessage = "Could not open template file '" + PathToUTF8(path) + "'.";
				return false;
			}
			std::array<uint8_t, 64 * 1024> buffer{};
			while (input)
			{
				input.read(reinterpret_cast<char*>(buffer.data()),
					static_cast<std::streamsize>(buffer.size()));
				const std::streamsize count = input.gcount();
				if (count > 0 && BCryptHashData(hash, buffer.data(),
					static_cast<ULONG>(count), 0) < 0)
				{
					closeHandles();
					errorMessage = "Windows could not hash template file '" +
						PathToUTF8(path) + "'.";
					return false;
				}
			}
			if (!input.eof())
			{
				closeHandles();
				errorMessage = "Could not read template file '" + PathToUTF8(path) + "'.";
				return false;
			}
			if (BCryptFinishHash(hash, bytes.data(),
				static_cast<ULONG>(bytes.size()), 0) < 0)
			{
				closeHandles();
				errorMessage = "Windows could not finish a SHA-256 hash.";
				return false;
			}
			closeHandles();

			std::ostringstream output;
			output << std::hex << std::setfill('0');
			for (uint8_t byte : bytes)
				output << std::setw(2) << static_cast<uint32_t>(byte);
			digest = output.str();
			return true;
#else
			(void)path;
			(void)digest;
			errorMessage = "Player export is supported only on Windows x64.";
			return false;
#endif
		}

		bool HasExactFields(const YAML::Node& node,
			std::initializer_list<const char*> fields)
		{
			if (!node || !node.IsMap() || node.size() != fields.size())
				return false;
			for (const char* field : fields)
			{
				if (!node[field])
					return false;
			}
			return true;
		}

		bool IsAllowedTemplatePath(const std::filesystem::path& relative)
		{
			const std::string key = NormalizedKey(relative);
			const std::string file = LowerASCII(PathToUTF8(relative.filename()));
			const std::string extension =
				LowerASCII(PathToUTF8(relative.extension()));
			if (extension == ".cs" || extension == ".csproj" ||
				extension == ".sln" || extension == ".slnx" ||
				extension == ".pdb" || extension == ".tcproj" ||
				file == "last-good.json" ||
				file == "tomcat.scriptgenerator.dll")
				return false;

			if (key == "tomcatplayer.exe")
				return true;
			const std::vector<std::string> components = PathComponents(relative);
			if (components.empty())
				return false;
			const std::string& first = components.front();
			if (first == "dotnet")
			{
				if (components.size() == 5 && components[1] == "host" &&
					components[2] == "fxr" && IsDotNet10Version(components[3]))
					return components[4] == "hostfxr.dll";
				return components.size() >= 5 && components[1] == "shared" &&
					components[2] == "microsoft.netcore.app" &&
					IsDotNet10Version(components[3]);
			}
			if (first == "managed")
			{
				return key == "managed/tomcat.managed.dll" ||
					key == "managed/tomcat.scripthost.dll" ||
					key == "managed/tomcat.scripthost.runtimeconfig.json" ||
					key == "managed/tomcat.scripthost.deps.json";
			}
			if (first == "packages")
				return key == "packages/shaders/texture.glsl" ||
					key == "packages/shaders/flatcolor.glsl";
			return key == "shaderc_shared.dll" || key == "msvcp140.dll" ||
				key == "vcruntime140.dll" || key == "vcruntime140_1.dll";
		}

		bool LoadAndValidateManifest(const std::filesystem::path& root,
			TemplateManifest& manifest, std::string& errorMessage)
		{
			manifest.Files.clear();
			std::error_code error;
			const std::filesystem::file_status rootStatus =
				std::filesystem::symlink_status(root, error);
			if (error || !std::filesystem::is_directory(rootStatus) ||
				std::filesystem::is_symlink(rootStatus) || IsReparsePoint(root))
			{
				errorMessage = "Player template root is missing or unsafe.";
				return false;
			}
			const std::filesystem::path manifestPath = root / "template.json";
			const std::filesystem::file_status manifestStatus =
				std::filesystem::symlink_status(manifestPath, error);
			if (error || !std::filesystem::is_regular_file(manifestStatus) ||
				std::filesystem::is_symlink(manifestStatus) || IsReparsePoint(manifestPath))
			{
				errorMessage = "Player template manifest is missing or unsafe.";
				return false;
			}
			try
			{
				const YAML::Node document = YAML::LoadFile(PathToUTF8(manifestPath));
				if (!HasExactFields(document, { "SchemaVersion", "EngineBuildID",
					"TcpakVersion", "PlayerAbiVersion", "Files" }))
					throw std::runtime_error("template.json must contain only the V1 fields");
				if (document["SchemaVersion"].as<uint32_t>() !=
						RuntimeCompatibility::PlayerTemplateSchemaVersion ||
					document["EngineBuildID"].as<std::string>() !=
						RuntimeCompatibility::EngineBuildID ||
					document["TcpakVersion"].as<uint32_t>() !=
						RuntimeCompatibility::TcpakVersion ||
					document["PlayerAbiVersion"].as<uint32_t>() !=
						RuntimeCompatibility::PlayerAbiVersion)
					throw std::runtime_error("template compatibility does not match this Editor");

				const YAML::Node files = document["Files"];
				if (!files.IsSequence() || files.size() == 0)
					throw std::runtime_error("Files must be a non-empty array");
				std::unordered_set<std::string> seen;
				bool hasPlayer = false;
				bool hasManagedApi = false;
				bool hasHost = false;
				bool hasRuntimeConfig = false;
				bool hasDeps = false;
				bool hasHostFxr = false;
				bool hasCoreClr = false;
				bool hasShaderCompiler = false;
				bool hasTextureShader = false;
				bool hasFlatColorShader = false;
				std::unordered_set<std::string> hostFxrVersions;
				std::unordered_set<std::string> runtimeVersions;
				for (size_t index = 0; index < files.size(); ++index)
				{
					const YAML::Node file = files[index];
					if (!HasExactFields(file, { "Path", "SHA256" }))
						throw std::runtime_error("Files entries require only Path and SHA256");
					TemplateFile entry;
					entry.RelativePath = UTF8ToPath(file["Path"].as<std::string>())
						.lexically_normal();
					entry.SHA256 = file["SHA256"].as<std::string>();
					if (!IsSafeRelativePath(entry.RelativePath) ||
						!IsAllowedTemplatePath(entry.RelativePath) ||
						!IsLowerHex256(entry.SHA256))
						throw std::runtime_error("Files contains an unsafe path or hash");
					const std::string key = NormalizedKey(entry.RelativePath);
					const std::vector<std::string> components =
						PathComponents(entry.RelativePath);
					if (!seen.emplace(key).second)
						throw std::runtime_error("Files contains a duplicate path");
					hasPlayer |= key == "tomcatplayer.exe";
					hasManagedApi |= key == "managed/tomcat.managed.dll";
					hasHost |= key == "managed/tomcat.scripthost.dll";
					hasRuntimeConfig |=
						key == "managed/tomcat.scripthost.runtimeconfig.json";
					hasDeps |= key == "managed/tomcat.scripthost.deps.json";
					hasHostFxr |= key.size() > 12 &&
						key.rfind("dotnet/host/", 0) == 0 &&
						LowerASCII(PathToUTF8(entry.RelativePath.filename())) ==
							"hostfxr.dll";
					hasCoreClr |= key.size() > 14 &&
						key.rfind("dotnet/shared/", 0) == 0 &&
						LowerASCII(PathToUTF8(entry.RelativePath.filename())) ==
							"coreclr.dll";
					if (components.size() == 5 && components[0] == "dotnet" &&
						components[1] == "host" && components[2] == "fxr")
						hostFxrVersions.emplace(components[3]);
					if (components.size() >= 5 && components[0] == "dotnet" &&
						components[1] == "shared" &&
						components[2] == "microsoft.netcore.app")
						runtimeVersions.emplace(components[3]);
					hasShaderCompiler |= key == "shaderc_shared.dll";
					hasTextureShader |= key == "packages/shaders/texture.glsl";
					hasFlatColorShader |= key == "packages/shaders/flatcolor.glsl";
					manifest.Files.emplace_back(std::move(entry));
				}
				if (!hasPlayer || !hasManagedApi || !hasHost ||
					!hasRuntimeConfig || !hasDeps || !hasHostFxr || !hasCoreClr ||
					!hasShaderCompiler || !hasTextureShader || !hasFlatColorShader ||
					!seen.contains("msvcp140.dll") || !seen.contains("vcruntime140.dll") ||
					!seen.contains("vcruntime140_1.dll") ||
					hostFxrVersions.size() != 1 || runtimeVersions.size() != 1)
					throw std::runtime_error(
						"template is missing or mixes required Player/runtime files");
			}
			catch (const std::exception& error)
			{
				errorMessage = "Invalid Player template '" + PathToUTF8(manifestPath) +
					"': " + error.what();
				return false;
			}

			std::unordered_set<std::string> expected;
			expected.emplace("template.json");
			for (const TemplateFile& file : manifest.Files)
				expected.emplace(NormalizedKey(file.RelativePath));

			std::unordered_set<std::string> actual;
			std::filesystem::recursive_directory_iterator iterator(root,
				std::filesystem::directory_options::none, error), end;
			for (; !error && iterator != end; iterator.increment(error))
			{
				const std::filesystem::file_status status =
					iterator->symlink_status(error);
				if (error)
					break;
				if (std::filesystem::is_symlink(status) ||
					IsReparsePoint(iterator->path()))
				{
					errorMessage = "Player template contains a reparse point: '" +
						PathToUTF8(iterator->path()) + "'.";
					return false;
				}
				if (!std::filesystem::is_regular_file(status))
					continue;
				const std::filesystem::path relative =
					iterator->path().lexically_relative(root);
				if (!actual.emplace(NormalizedKey(relative)).second)
				{
					errorMessage = "Player template has a duplicate path.";
					return false;
				}
			}
			if (error || actual != expected)
			{
				errorMessage = error
					? "Could not enumerate the Player template: " + error.message()
					: "Player template files do not exactly match template.json.";
				return false;
			}

			for (const TemplateFile& file : manifest.Files)
			{
				std::string actualHash;
				if (!ComputeFileSHA256(root / file.RelativePath,
					actualHash, errorMessage))
					return false;
				if (actualHash != file.SHA256)
				{
					errorMessage = "Player template hash mismatch: '" +
						PathToUTF8(file.RelativePath) + "'.";
					return false;
				}
			}
			return true;
		}

		bool CopyFile(const std::filesystem::path& source,
			const std::filesystem::path& destination, std::string& errorMessage)
		{
			std::error_code error;
			std::filesystem::create_directories(destination.parent_path(), error);
			if (!error)
				std::filesystem::copy_file(source, destination,
					std::filesystem::copy_options::none, error);
			if (!error)
				return true;
			errorMessage = "Could not copy '" + PathToUTF8(source) + "' to '" +
				PathToUTF8(destination) + "': " + error.message();
			return false;
		}

		std::string SanitizeDirectoryName(std::string value)
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
			const std::string stem =
				LowerASCII(value.substr(0, value.find('.')));
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

		bool Publish(const std::filesystem::path& staging,
			const std::filesystem::path& output, const std::string& buildID,
			std::string& errorMessage)
		{
			std::filesystem::path backup = output;
			backup += ".previous-" + buildID;
			std::error_code error;
			if (std::filesystem::exists(backup, error) || error)
			{
				errorMessage = "Could not reserve the Player rollback directory.";
				return false;
			}

			bool movedPrevious = false;
			const std::filesystem::file_status outputStatus =
				std::filesystem::symlink_status(output, error);
			if (!error && std::filesystem::exists(outputStatus))
			{
				if (!std::filesystem::is_directory(outputStatus) ||
					std::filesystem::is_symlink(outputStatus) ||
					IsReparsePoint(output))
				{
					errorMessage = "Existing Player output is not a safe directory.";
					return false;
				}
				std::filesystem::rename(output, backup, error);
				if (error)
				{
					errorMessage = "Could not replace the previous Player build: " +
						error.message();
					return false;
				}
				movedPrevious = true;
			}
			else if (error && error != std::errc::no_such_file_or_directory)
			{
				errorMessage = "Could not inspect the Player output: " + error.message();
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
				errorMessage = "Could not publish the Player: " + publishError;
				if (error)
					errorMessage += "; rollback also failed: " + error.message();
				return false;
			}
			if (movedPrevious)
			{
				error.clear();
				std::filesystem::remove_all(backup, error);
				if (error)
					TC_Core_Warn("Player rollback directory could not be removed: {0}",
						error.message());
			}
			return true;
		}

	}

	std::filesystem::path PlayerBuilder::FindDefaultTemplateDirectory()
	{
#ifdef TC_PLATFORM_WINDOWS
		std::vector<wchar_t> buffer(MAX_PATH);
		for (;;)
		{
			const DWORD length = GetModuleFileNameW(nullptr, buffer.data(),
				static_cast<DWORD>(buffer.size()));
			if (length == 0)
				break;
			if (length < buffer.size() - 1)
			{
				const std::filesystem::path executable(
					std::wstring(buffer.data(), length));
				return executable.parent_path() / "Packages" /
					"PlayerTemplates" / "win-x64";
			}
			if (buffer.size() >= 32768)
				break;
			buffer.resize(buffer.size() * 2);
		}
#endif
		return {};
	}

	PlayerBuildResult PlayerBuilder::Build(PlayerBuildRequest request)
	{
		PlayerBuildResult result;
		auto fail = [&](std::string message)
		{
			result.Message = std::move(message);
			return result;
		};
		if (!request.ProjectInstance ||
			static_cast<uint64_t>(request.EntryScene) == 0 ||
			request.TemplateDirectory.empty() ||
			request.ManagedAssembly.empty() ||
			request.ScriptManifestJson.empty() ||
			!IsSafeBuildID(request.ScriptBuildID))
			return fail("PlayerBuilder received an incomplete build request.");

		TemplateManifest manifest;
		std::string errorMessage;
		if (!LoadAndValidateManifest(request.TemplateDirectory,
			manifest, errorMessage))
			return fail(std::move(errorMessage));

		std::error_code error;
		const std::filesystem::path projectDirectory =
			std::filesystem::weakly_canonical(
				request.ProjectInstance->GetProjectDirectory(), error);
		if (error || projectDirectory.empty())
			return fail("The project directory could not be resolved.");
		const std::filesystem::path buildRoot = projectDirectory / "Build";
		std::filesystem::create_directories(buildRoot, error);
		if (error)
			return fail("The Build directory could not be created: " + error.message());

		const std::string directoryName =
			SanitizeDirectoryName(request.ProjectInstance->GetName());
		const std::filesystem::path output = buildRoot / UTF8ToPath(directoryName);
		const std::filesystem::path staging = buildRoot /
			UTF8ToPath(directoryName + ".staging-" + request.ScriptBuildID);
		const std::filesystem::file_status stagingStatus =
			std::filesystem::symlink_status(staging, error);
		if (!error && std::filesystem::exists(stagingStatus))
			return fail("An isolated Player staging directory already exists.");
		if (error && error != std::errc::no_such_file_or_directory)
			return fail("The Player staging directory could not be inspected.");
		error.clear();
		std::filesystem::create_directory(staging, error);
		if (error)
			return fail("The Player staging directory could not be created: " +
				error.message());

		auto failStaged = [&](std::string message)
		{
			std::error_code cleanupError;
			if (IsReparsePoint(staging))
				std::filesystem::remove(staging, cleanupError);
			else
				std::filesystem::remove_all(staging, cleanupError);
			if (cleanupError)
				message += " Staging cleanup also failed: " + cleanupError.message();
			return fail(std::move(message));
		};

		for (const TemplateFile& file : manifest.Files)
		{
			if (!CopyFile(request.TemplateDirectory / file.RelativePath,
				staging / file.RelativePath, errorMessage))
				return failStaged(std::move(errorMessage));
			std::string stagedHash;
			if (!ComputeFileSHA256(staging / file.RelativePath, stagedHash,
				errorMessage))
				return failStaged(std::move(errorMessage));
			if (stagedHash != file.SHA256)
				return failStaged("A Player template file changed while it was being staged: '" +
					PathToUTF8(file.RelativePath) + "'.");
		}

		AssetManager& assets = AssetManager::Get();
		if (!assets.SetManagedCookPayload(std::move(request.ManagedAssembly),
			request.ScriptManifestJson, request.ScriptBuildID, {}))
			return failStaged("The fresh managed payload was rejected.");
		const bool cooked = assets.CookToPackage(
			staging / "Game.tcpak", request.EntryScene);
		assets.ClearManagedCookPayload();
		if (!cooked)
			return failStaged("Cooking Game.tcpak failed. See preceding diagnostics.");

		std::unordered_set<std::string> expected{ "game.tcpak" };
		for (const TemplateFile& file : manifest.Files)
			expected.emplace(NormalizedKey(file.RelativePath));
		std::unordered_set<std::string> actual;
		std::filesystem::recursive_directory_iterator iterator(staging,
			std::filesystem::directory_options::none, error), end;
		for (; !error && iterator != end; iterator.increment(error))
		{
			const std::filesystem::file_status status =
				iterator->symlink_status(error);
			if (error)
				break;
			if (std::filesystem::is_symlink(status) ||
				IsReparsePoint(iterator->path()))
				return failStaged("The staged Player contains a reparse point.");
			if (std::filesystem::is_regular_file(status))
				actual.emplace(NormalizedKey(
					iterator->path().lexically_relative(staging)));
		}
		if (error || actual != expected)
			return failStaged(error
				? "Could not enumerate the staged Player: " + error.message()
				: "The staged Player does not match the strict template whitelist.");
		if (request.ValidateBeforePublish && !request.ValidateBeforePublish())
			return failStaged(
				"C# sources changed while the Player was being staged. Build again.");

		if (!Publish(staging, output, request.ScriptBuildID, errorMessage))
			return failStaged(std::move(errorMessage));

		result.Succeeded = true;
		result.OutputDirectory = output;
		result.PlayerExecutable = output / "TomCatPlayer.exe";
		result.Message = "Player build succeeded: " + PathToUTF8(output) +
			" (C# build " + request.ScriptBuildID + ").";
		return result;
	}

	bool PlayerBuilder::Launch(const std::filesystem::path& executable,
		const std::filesystem::path& workingDirectory, std::string& errorMessage)
	{
#ifdef TC_PLATFORM_WINDOWS
		std::wstring command = L"\"" + executable.wstring() + L"\"";
		std::vector<wchar_t> mutableCommand(command.begin(), command.end());
		mutableCommand.push_back(L'\0');
		STARTUPINFOW startup{};
		startup.cb = sizeof(startup);
		PROCESS_INFORMATION process{};
		const BOOL created = CreateProcessW(executable.c_str(),
			mutableCommand.data(), nullptr, nullptr, FALSE, 0, nullptr,
			workingDirectory.c_str(), &startup, &process);
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
		errorMessage = "Player launch is supported only on Windows x64.";
		return false;
#endif
	}

}
