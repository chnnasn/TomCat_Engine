#include "tcpch.h"
#include "EditorRuntimeBundle.h"

#include "ApplicationPaths.h"
#include "Version.h"
#include "TomCat/Asset/ContentHash.h"
#include "TomCat/Utils/FileSystemUtils.h"
#include "TomCat/Utils/PathUtils.h"

#include <yaml-cpp/yaml.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <charconv>
#include <chrono>
#include <cctype>
#include <fstream>
#include <iterator>
#include <limits>
#include <map>
#include <mutex>
#include <span>
#include <sstream>
#include <stdexcept>
#include <system_error>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#ifdef TC_PLATFORM_WINDOWS
	#include <Windows.h>
	#include <wincrypt.h>
#else
	#include <sys/stat.h>
#endif

namespace TomCat {
	namespace {

		constexpr uint32_t RuntimeManifestSchema = 1;
		constexpr uint64_t MaximumManifestBytes = 4ULL * 1024ULL * 1024ULL;
		constexpr uint64_t MaximumCompletionMarkerBytes = 4ULL * 1024ULL * 1024ULL;
		constexpr uint64_t MaximumRuntimeFileBytes = 2ULL * 1024ULL * 1024ULL * 1024ULL;
		constexpr uint64_t MaximumRuntimeBytes = 4ULL * 1024ULL * 1024ULL * 1024ULL;
		constexpr std::string_view RuntimeManifestName = "runtime-manifest.json";
		constexpr std::string_view RuntimeCompletionHeader =
			"TomCatEditorRuntimeCompletionV1";
		constexpr std::string_view RuntimeCompletionSuffix = ".complete-v1";
		constexpr std::string_view RuntimeStagingSuffix = ".staging";
		constexpr std::string_view RuntimeCorruptSuffix = ".corrupt-replaced";
		std::atomic<uint64_t> s_RuntimePathCounter{ 0 };

		struct RuntimeManifestFile
		{
			std::filesystem::path RelativePath;
			std::string Key;
			uint64_t Size = 0;
			std::string SHA256;
		};

		struct RuntimeManifest
		{
			std::string EngineBuildID;
			std::string Document;
			std::string SHA256;
			std::vector<RuntimeManifestFile> Files;
		};

		struct RuntimeFileStamp
		{
			uint64_t Size = 0;
			uint64_t Volume = 0;
			uint64_t FileID = 0;
			uint64_t CreationTime = 0;
			uint64_t LastWriteTime = 0;
			uint64_t ChangeTime = 0;
			uint64_t Attributes = 0;

			bool operator==(const RuntimeFileStamp&) const = default;
		};

		struct RuntimeDirectoryIdentity
		{
			uint64_t Volume = 0;
			uint64_t FileID = 0;

			bool operator==(const RuntimeDirectoryIdentity&) const = default;
		};

		using RuntimeFileStamps = std::map<std::string, RuntimeFileStamp>;

		bool IsLowerSHA256(std::string_view value)
		{
			return value.size() == 64 && std::all_of(value.begin(), value.end(),
				[](unsigned char character)
				{
					return (character >= '0' && character <= '9') ||
						(character >= 'a' && character <= 'f');
				});
		}

		bool IsSafeWindowsSegment(std::string_view value)
		{
			if (value.empty() || value == "." || value == ".." ||
				value.back() == ' ' || value.back() == '.')
				return false;
			for (unsigned char character : value)
			{
				if (character < 0x20 || character == 0x7f || character == '<' ||
					character == '>' || character == ':' || character == '"' ||
					character == '/' || character == '\\' || character == '|' ||
					character == '?' || character == '*')
					return false;
			}

			std::string base(value.substr(0, value.find('.')));
			std::transform(base.begin(), base.end(), base.begin(),
				[](unsigned char character)
				{
					return static_cast<char>(std::toupper(character));
				});
			if (base == "CON" || base == "PRN" || base == "AUX" || base == "NUL")
				return false;
			return !(base.size() == 4 && (base.rfind("COM", 0) == 0 ||
				base.rfind("LPT", 0) == 0) && base[3] >= '1' && base[3] <= '9');
		}

		bool IsSafeRelativeRuntimePath(const std::filesystem::path& relative)
		{
			if (relative.empty() || relative.is_absolute() || relative.has_root_name() ||
				relative.has_root_directory() || relative.filename().empty() ||
				relative.lexically_normal() != relative)
				return false;
			for (const auto& component : relative)
			{
				if (!IsSafeWindowsSegment(PathToUTF8(component)))
					return false;
			}
			return true;
		}

		std::string NormalizedRuntimeKey(const std::filesystem::path& path)
		{
			std::string key = PathToUTF8(path.lexically_normal());
			std::replace(key.begin(), key.end(), '\\', '/');
			std::transform(key.begin(), key.end(), key.begin(),
				[](unsigned char character)
				{
					return static_cast<char>(std::tolower(character));
				});
			return key;
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
				std::filesystem::symlink_status(path, error)) && !error;
#endif
		}

		std::filesystem::path RuntimeSiblingWithSuffix(
			const std::filesystem::path& root, std::string_view suffix)
		{
			std::filesystem::path sibling = root;
			sibling += UTF8ToPath(suffix);
			return sibling;
		}

#ifdef TC_PLATFORM_WINDOWS
		std::wstring ExtendedLengthRuntimePath(const std::filesystem::path& path)
		{
			std::error_code error;
			const std::filesystem::path absolute =
				std::filesystem::absolute(path, error);
			std::wstring value = (error ? path : absolute).lexically_normal().wstring();
			if (value.rfind(L"\\\\?\\", 0) == 0)
				return value;
			if (value.rfind(L"\\\\", 0) == 0)
				return L"\\\\?\\UNC\\" + value.substr(2);
			return L"\\\\?\\" + value;
		}

		std::string RuntimeWindowsError(DWORD error)
		{
			return std::error_code(static_cast<int>(error),
				std::system_category()).message();
		}

		class RuntimeFileHandle
		{
		public:
			explicit RuntimeFileHandle(HANDLE handle = INVALID_HANDLE_VALUE)
				: m_Handle(handle) {}
			~RuntimeFileHandle()
			{
				if (m_Handle != INVALID_HANDLE_VALUE)
					CloseHandle(m_Handle);
			}

			RuntimeFileHandle(const RuntimeFileHandle&) = delete;
			RuntimeFileHandle& operator=(const RuntimeFileHandle&) = delete;

			HANDLE Get() const { return m_Handle; }
			bool Valid() const { return m_Handle != INVALID_HANDLE_VALUE; }
			bool Close(DWORD& error)
			{
				if (!Valid())
				{
					error = ERROR_SUCCESS;
					return true;
				}
				const HANDLE handle = m_Handle;
				m_Handle = INVALID_HANDLE_VALUE;
				if (CloseHandle(handle) != FALSE)
				{
					error = ERROR_SUCCESS;
					return true;
				}
				error = GetLastError();
				return false;
			}

		private:
			HANDLE m_Handle = INVALID_HANDLE_VALUE;
		};

		bool ComputeRuntimeFileSHA256(const std::filesystem::path& path,
			std::string& digest, std::string& errorMessage)
		{
			digest.clear();
			RuntimeFileHandle input(CreateFileW(ExtendedLengthRuntimePath(path).c_str(),
				GENERIC_READ, FILE_SHARE_READ, nullptr,
				OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL | FILE_FLAG_SEQUENTIAL_SCAN |
				FILE_FLAG_OPEN_REPARSE_POINT, nullptr));
			if (!input.Valid())
			{
				const DWORD openError = GetLastError();
				errorMessage = "could not open runtime file for hashing '" +
					PathToUTF8(path) + "': " + RuntimeWindowsError(openError);
				return false;
			}

			HCRYPTPROV provider = 0;
			if (CryptAcquireContextW(&provider, nullptr, nullptr, PROV_RSA_AES,
				CRYPT_VERIFYCONTEXT) == FALSE)
			{
				const DWORD providerError = GetLastError();
				errorMessage = "could not initialize runtime SHA-256: " +
					RuntimeWindowsError(providerError);
				return false;
			}
			HCRYPTHASH hash = 0;
			if (CryptCreateHash(provider, CALG_SHA_256, 0, 0, &hash) == FALSE)
			{
				const DWORD hashError = GetLastError();
				CryptReleaseContext(provider, 0);
				errorMessage = "could not create runtime SHA-256 state: " +
					RuntimeWindowsError(hashError);
				return false;
			}
			auto releaseHash = [&]()
			{
				CryptDestroyHash(hash);
				CryptReleaseContext(provider, 0);
			};

			std::array<uint8_t, 64 * 1024> buffer{};
			for (;;)
			{
				DWORD byteCount = 0;
				if (ReadFile(input.Get(), buffer.data(),
					static_cast<DWORD>(buffer.size()), &byteCount, nullptr) == FALSE)
				{
					const DWORD readError = GetLastError();
					releaseHash();
					errorMessage = "could not read runtime file for hashing '" +
						PathToUTF8(path) + "': " + RuntimeWindowsError(readError);
					return false;
				}
				if (byteCount == 0)
					break;
				if (CryptHashData(hash, buffer.data(), byteCount, 0) == FALSE)
				{
					const DWORD hashError = GetLastError();
					releaseHash();
					errorMessage = "could not update runtime SHA-256 for '" +
						PathToUTF8(path) + "': " + RuntimeWindowsError(hashError);
					return false;
				}
			}

			std::array<uint8_t, 32> bytes{};
			DWORD byteCount = static_cast<DWORD>(bytes.size());
			const BOOL finishSucceeded = CryptGetHashParam(
				hash, HP_HASHVAL, bytes.data(), &byteCount, 0);
			if (finishSucceeded == FALSE || byteCount != bytes.size())
			{
				const DWORD hashError = finishSucceeded == FALSE
					? GetLastError() : ERROR_INVALID_DATA;
				releaseHash();
				errorMessage = "could not finish runtime SHA-256 for '" +
					PathToUTF8(path) + "': " + RuntimeWindowsError(hashError);
				return false;
			}
			releaseHash();
			static constexpr char hex[] = "0123456789abcdef";
			digest.reserve(bytes.size() * 2);
			for (const uint8_t byte : bytes)
			{
				digest.push_back(hex[byte >> 4]);
				digest.push_back(hex[byte & 0x0f]);
			}
			return true;
		}
#else
		bool ComputeRuntimeFileSHA256(const std::filesystem::path& path,
			std::string& digest, std::string& errorMessage)
		{
			return ComputeFileContentSHA256(path, digest, errorMessage);
		}
#endif

		bool ReadRuntimeFileStamp(const std::filesystem::path& path,
			RuntimeFileStamp& stamp, std::string& errorMessage)
		{
			stamp = {};
#ifdef TC_PLATFORM_WINDOWS
			const HANDLE handle = CreateFileW(ExtendedLengthRuntimePath(path).c_str(),
				FILE_READ_ATTRIBUTES,
				FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr,
				OPEN_EXISTING, FILE_FLAG_OPEN_REPARSE_POINT, nullptr);
			if (handle == INVALID_HANDLE_VALUE)
			{
				const DWORD openError = GetLastError();
				errorMessage = "could not inspect runtime file '" + PathToUTF8(path) +
					"': " + RuntimeWindowsError(openError);
				return false;
			}
			FILE_ATTRIBUTE_TAG_INFO attributes{};
			BY_HANDLE_FILE_INFORMATION identity{};
			FILE_BASIC_INFO times{};
			const bool inspected = GetFileInformationByHandleEx(handle,
				FileAttributeTagInfo, &attributes, sizeof(attributes)) != FALSE &&
				GetFileInformationByHandle(handle, &identity) != FALSE &&
				GetFileInformationByHandleEx(handle, FileBasicInfo,
					&times, sizeof(times)) != FALSE;
			const DWORD inspectError = inspected ? ERROR_SUCCESS : GetLastError();
			if (!inspected)
			{
				CloseHandle(handle);
				errorMessage = "could not read runtime file metadata '" +
					PathToUTF8(path) + "': " + RuntimeWindowsError(inspectError);
				return false;
			}
			if ((attributes.FileAttributes &
				(FILE_ATTRIBUTE_DIRECTORY | FILE_ATTRIBUTE_REPARSE_POINT)) != 0)
			{
				CloseHandle(handle);
				errorMessage = "runtime file is not a regular, non-reparse file: " +
					PathToUTF8(path);
				return false;
			}
			stamp.Size = (static_cast<uint64_t>(identity.nFileSizeHigh) << 32) |
				identity.nFileSizeLow;
			stamp.Volume = identity.dwVolumeSerialNumber;
			stamp.FileID = (static_cast<uint64_t>(identity.nFileIndexHigh) << 32) |
				identity.nFileIndexLow;
			stamp.CreationTime = static_cast<uint64_t>(times.CreationTime.QuadPart);
			stamp.LastWriteTime = static_cast<uint64_t>(times.LastWriteTime.QuadPart);
			stamp.ChangeTime = static_cast<uint64_t>(times.ChangeTime.QuadPart);
			stamp.Attributes = attributes.FileAttributes;
			CloseHandle(handle);
#else
			struct stat information{};
			if (lstat(path.c_str(), &information) != 0 ||
				!S_ISREG(information.st_mode))
			{
				errorMessage = "runtime file is missing or unsafe: " + PathToUTF8(path);
				return false;
			}
			stamp.Size = static_cast<uint64_t>(information.st_size);
			stamp.Volume = static_cast<uint64_t>(information.st_dev);
			stamp.FileID = static_cast<uint64_t>(information.st_ino);
			stamp.CreationTime = 0;
			stamp.LastWriteTime = static_cast<uint64_t>(information.st_mtime);
			stamp.ChangeTime = static_cast<uint64_t>(information.st_ctime);
			stamp.Attributes = static_cast<uint64_t>(information.st_mode);
#endif
			return true;
		}

		bool ReadRuntimeDirectoryIdentity(const std::filesystem::path& path,
			RuntimeDirectoryIdentity& identity, std::string& errorMessage)
		{
			identity = {};
#ifdef TC_PLATFORM_WINDOWS
			const HANDLE handle = CreateFileW(ExtendedLengthRuntimePath(path).c_str(),
				FILE_READ_ATTRIBUTES,
				FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr,
				OPEN_EXISTING,
				FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OPEN_REPARSE_POINT, nullptr);
			if (handle == INVALID_HANDLE_VALUE)
			{
				const DWORD openError = GetLastError();
				errorMessage = "could not inspect runtime directory '" +
					PathToUTF8(path) + "': " + RuntimeWindowsError(openError);
				return false;
			}
			FILE_ATTRIBUTE_TAG_INFO attributes{};
			BY_HANDLE_FILE_INFORMATION information{};
			const bool inspected = GetFileInformationByHandleEx(handle,
				FileAttributeTagInfo, &attributes, sizeof(attributes)) != FALSE &&
				GetFileInformationByHandle(handle, &information) != FALSE;
			const DWORD inspectError = inspected ? ERROR_SUCCESS : GetLastError();
			if (!inspected ||
				(attributes.FileAttributes & FILE_ATTRIBUTE_DIRECTORY) == 0 ||
				(attributes.FileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) != 0)
			{
				CloseHandle(handle);
				errorMessage = inspected
					? "runtime directory is missing or unsafe: " + PathToUTF8(path)
					: "could not read runtime directory identity '" +
						PathToUTF8(path) + "': " + RuntimeWindowsError(inspectError);
				return false;
			}
			identity.Volume = information.dwVolumeSerialNumber;
			identity.FileID =
				(static_cast<uint64_t>(information.nFileIndexHigh) << 32) |
				information.nFileIndexLow;
			CloseHandle(handle);
#else
			struct stat information{};
			if (lstat(path.c_str(), &information) != 0 ||
				!S_ISDIR(information.st_mode))
			{
				errorMessage = "runtime directory is missing or unsafe: " +
					PathToUTF8(path);
				return false;
			}
			identity.Volume = static_cast<uint64_t>(information.st_dev);
			identity.FileID = static_cast<uint64_t>(information.st_ino);
#endif
			return true;
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

		bool ParseUnsigned(const YAML::Node& node, uint64_t& value)
		{
			if (!node || !node.IsScalar())
				return false;
			const std::string text = node.Scalar();
			if (text.empty() || !std::all_of(text.begin(), text.end(),
				[](unsigned char character) { return std::isdigit(character) != 0; }))
				return false;
			const auto parsed = std::from_chars(
				text.data(), text.data() + text.size(), value);
			return parsed.ec == std::errc{} && parsed.ptr == text.data() + text.size();
		}

		bool ReadBoundedRegularFile(const std::filesystem::path& path,
			uint64_t maximumBytes, std::string_view description,
			std::string& contents, std::string& errorMessage)
		{
			contents.clear();
			std::error_code error;
			const std::filesystem::file_status status =
				std::filesystem::symlink_status(path, error);
			if (error || !std::filesystem::is_regular_file(status) ||
				std::filesystem::is_symlink(status) || IsReparsePoint(path))
			{
				errorMessage = std::string(description) + " is missing or unsafe: " +
					PathToUTF8(path);
				return false;
			}
			const uintmax_t size = std::filesystem::file_size(path, error);
			if (error || size > maximumBytes)
			{
				errorMessage = std::string(description) + " exceeds its size limit";
				return false;
			}
			std::ifstream input(path, std::ios::binary);
			if (!input)
			{
				errorMessage = "could not open " + std::string(description) + ": " +
					PathToUTF8(path);
				return false;
			}
			contents.assign(std::istreambuf_iterator<char>(input),
				std::istreambuf_iterator<char>());
			if (input.bad() || contents.size() != size)
			{
				errorMessage = "could not read the complete " +
					std::string(description);
				return false;
			}
			return true;
		}

		bool IsAllowedRuntimePath(const std::string& key)
		{
			static const std::unordered_set<std::string> rootFiles = {
				"tomcatcli.exe", "shaderc_shared.dll", "msvcp140.dll",
				"vcruntime140.dll", "vcruntime140_1.dll"
			};
			static const std::unordered_set<std::string> managedFiles = {
				"managed/tomcat.managed.dll",
				"managed/tomcat.scripthost.dll",
				"managed/tomcat.scripthost.runtimeconfig.json",
				"managed/tomcat.scripthost.deps.json",
				"managed/tomcat.scriptgenerator.dll"
			};
			return rootFiles.contains(key) || managedFiles.contains(key) ||
				key.starts_with("packages/playertemplates/win-x64/");
		}

		bool HasRequiredRuntimeFiles(const RuntimeManifest& manifest,
			std::string& errorMessage)
		{
			std::unordered_set<std::string> keys;
			for (const RuntimeManifestFile& file : manifest.Files)
				keys.emplace(file.Key);
			static const std::array required = {
				"tomcatcli.exe", "shaderc_shared.dll", "msvcp140.dll",
				"vcruntime140.dll", "vcruntime140_1.dll",
				"managed/tomcat.managed.dll",
				"managed/tomcat.scripthost.dll",
				"managed/tomcat.scripthost.runtimeconfig.json",
				"managed/tomcat.scripthost.deps.json",
				"managed/tomcat.scriptgenerator.dll",
				"packages/playertemplates/win-x64/template.json"
			};
			for (std::string_view requiredPath : required)
			{
				if (!keys.contains(std::string(requiredPath)))
				{
					errorMessage = "runtime manifest is missing required file '" +
						std::string(requiredPath) + "'";
					return false;
				}
			}
			return true;
		}

		bool LoadRuntimeManifest(const std::filesystem::path& payloadRoot,
			RuntimeManifest& manifest, std::string& errorMessage)
		{
			manifest = {};
			std::error_code filesystemError;
			const std::filesystem::file_status rootStatus =
				std::filesystem::symlink_status(payloadRoot, filesystemError);
			if (filesystemError || !std::filesystem::is_directory(rootStatus) ||
				std::filesystem::is_symlink(rootStatus) || IsReparsePoint(payloadRoot))
			{
				errorMessage = "runtime payload root is missing or unsafe: " +
					PathToUTF8(payloadRoot);
				return false;
			}

			if (!ReadBoundedRegularFile(payloadRoot / RuntimeManifestName,
				MaximumManifestBytes, "runtime manifest", manifest.Document,
				errorMessage))
				return false;
			manifest.SHA256 = ComputeContentSHA256(std::span<const uint8_t>(
				reinterpret_cast<const uint8_t*>(manifest.Document.data()),
				manifest.Document.size()));

			try
			{
				const YAML::Node document = YAML::Load(manifest.Document);
				if (!HasExactFields(document,
					{ "schemaVersion", "engineBuildId", "files" }))
					throw std::runtime_error(
						"runtime manifest must contain only the V1 fields");
				uint64_t schemaVersion = 0;
				if (!ParseUnsigned(document["schemaVersion"], schemaVersion) ||
					schemaVersion != RuntimeManifestSchema)
					throw std::runtime_error("unsupported runtime manifest schema");
				if (!document["engineBuildId"].IsScalar())
					throw std::runtime_error("engineBuildId must be a string");
				manifest.EngineBuildID = document["engineBuildId"].as<std::string>();
				if (manifest.EngineBuildID.size() > 128 ||
					!IsSafeWindowsSegment(manifest.EngineBuildID))
					throw std::runtime_error("engineBuildId is not a safe path segment");

				const YAML::Node files = document["files"];
				if (!files.IsSequence() || files.size() == 0 || files.size() > 10000)
					throw std::runtime_error("files must be a bounded non-empty array");
				std::unordered_set<std::string> seen;
				uint64_t totalSize = 0;
				manifest.Files.reserve(files.size());
				for (size_t index = 0; index < files.size(); ++index)
				{
					const YAML::Node node = files[index];
					if (!HasExactFields(node, { "path", "size", "sha256" }) ||
						!node["path"].IsScalar() || !node["sha256"].IsScalar())
						throw std::runtime_error(
							"each files entry requires only path, size, and sha256");
					RuntimeManifestFile file;
					const std::string path = node["path"].as<std::string>();
					if (path.size() > 4096)
						throw std::runtime_error("runtime file path exceeds its limit");
					file.RelativePath = UTF8ToPath(path);
					if (!IsSafeRelativeRuntimePath(file.RelativePath) ||
						NormalizedRuntimeKey(file.RelativePath) == RuntimeManifestName)
						throw std::runtime_error("runtime manifest contains an unsafe path");
					file.Key = NormalizedRuntimeKey(file.RelativePath);
					if (!IsAllowedRuntimePath(file.Key))
						throw std::runtime_error("runtime manifest contains an unexpected path");
					if (!seen.emplace(file.Key).second)
						throw std::runtime_error("runtime manifest contains a duplicate path");
					if (!ParseUnsigned(node["size"], file.Size) ||
						file.Size > MaximumRuntimeFileBytes ||
						totalSize > MaximumRuntimeBytes - file.Size)
						throw std::runtime_error("runtime file size exceeds its limit");
					totalSize += file.Size;
					file.SHA256 = node["sha256"].as<std::string>();
					if (!IsLowerSHA256(file.SHA256))
						throw std::runtime_error("runtime file has an invalid SHA-256");
					manifest.Files.push_back(std::move(file));
				}
			}
			catch (const std::exception& exception)
			{
				errorMessage = "invalid runtime manifest: " +
					std::string(exception.what());
				return false;
			}
			return HasRequiredRuntimeFiles(manifest, errorMessage);
		}

		std::map<std::string, std::filesystem::path> RuntimeDirectories(
			const RuntimeManifest& manifest)
		{
			std::map<std::string, std::filesystem::path> directories;
			for (const RuntimeManifestFile& file : manifest.Files)
			{
				std::filesystem::path current;
				for (const auto& component : file.RelativePath.parent_path())
				{
					current /= component;
					directories.emplace(NormalizedRuntimeKey(current), current);
				}
			}
			return directories;
		}

		bool PinRuntimeDirectoryTree(const std::filesystem::path& root,
			const RuntimeManifest& manifest, bool create,
			std::vector<FileSystem::PinnedDirectoryChain>& guards,
			bool* rootCreated, std::string& errorMessage)
		{
			guards.clear();
			if (rootCreated)
				*rootCreated = false;
			FileSystem::PinnedDirectoryChain rootGuard;
			if (create)
			{
				bool created = false;
				if (!FileSystem::CreateDirectoryAndPin(
					root, rootGuard, created, errorMessage))
					return false;
				if (rootCreated)
					*rootCreated = created;
			}
			else if (!rootGuard.Acquire(root, errorMessage))
				return false;
			guards.push_back(std::move(rootGuard));

			for (const auto& [key, relative] : RuntimeDirectories(manifest))
			{
				(void)key;
				FileSystem::PinnedDirectoryChain guard;
				if (create)
				{
					bool created = false;
					if (!FileSystem::CreateDirectoryAndPin(
						root / relative, guard, created, errorMessage))
						return false;
				}
				else if (!guard.Acquire(root / relative, errorMessage))
					return false;
				guards.push_back(std::move(guard));
			}
			return true;
		}

		bool VerifyRuntimeDirectoryGuards(
			const std::vector<FileSystem::PinnedDirectoryChain>& guards,
			std::string& errorMessage)
		{
			for (const auto& guard : guards)
			{
				if (!guard.Verify(errorMessage))
					return false;
			}
			return true;
		}

		bool ComputeCheckedFileSHA256(const std::filesystem::path& path,
			uint64_t expectedSize, std::string_view expectedSHA256,
			RuntimeFileStamp* stableStamp, std::string& errorMessage)
		{
			RuntimeFileStamp before;
			if (!ReadRuntimeFileStamp(path, before, errorMessage))
				return false;
			if (before.Size != expectedSize)
			{
				errorMessage = "runtime file size mismatch: " + PathToUTF8(path);
				return false;
			}
			std::string digest;
			if (!ComputeRuntimeFileSHA256(path, digest, errorMessage))
				return false;
			RuntimeFileStamp after;
			if (!ReadRuntimeFileStamp(path, after, errorMessage))
				return false;
			if (before != after)
			{
				errorMessage = "runtime file changed while it was being verified: " +
					PathToUTF8(path);
				return false;
			}
			if (digest != expectedSHA256)
			{
				errorMessage = "runtime file SHA-256 mismatch: " + PathToUTF8(path);
				return false;
			}
			if (stableStamp)
				*stableStamp = after;
			return true;
		}

		std::string SerializeRuntimeCompletion(const RuntimeManifest& manifest,
			const RuntimeFileStamps& stamps)
		{
			std::ostringstream body;
			body << RuntimeCompletionHeader << '\n'
				<< "manifest\t" << manifest.SHA256 << '\n'
				<< "files\t" << stamps.size() << '\n';
			for (const auto& [key, stamp] : stamps)
			{
				body << key << '\t' << stamp.Size << '\t' << stamp.Volume << '\t'
					<< stamp.FileID << '\t' << stamp.CreationTime << '\t'
					<< stamp.LastWriteTime << '\t' << stamp.ChangeTime << '\t'
					<< stamp.Attributes << '\n';
			}
			const std::string document = body.str();
			return document + "digest\t" + ComputeContentSHA256(
				std::span<const uint8_t>(
					reinterpret_cast<const uint8_t*>(document.data()), document.size())) +
				"\n";
		}

		bool InspectRuntimeTreeMetadata(const std::filesystem::path& root,
			const RuntimeManifest& manifest, RuntimeFileStamps& stamps,
			std::string& errorMessage)
		{
			stamps.clear();
			std::vector<FileSystem::PinnedDirectoryChain> guards;
			if (!PinRuntimeDirectoryTree(root, manifest, false, guards,
				nullptr, errorMessage))
				return false;

			std::unordered_map<std::string, uint64_t> expectedFiles;
			for (const RuntimeManifestFile& file : manifest.Files)
				expectedFiles.emplace(file.Key, file.Size);
			expectedFiles.emplace(std::string(RuntimeManifestName),
				manifest.Document.size());
			std::unordered_set<std::string> expectedDirectories;
			for (const auto& [key, relative] : RuntimeDirectories(manifest))
			{
				(void)relative;
				expectedDirectories.emplace(key);
			}
			std::unordered_set<std::string> actualDirectories;

			std::error_code error;
			std::filesystem::recursive_directory_iterator iterator(root,
				std::filesystem::directory_options::none, error), end;
			for (; !error && iterator != end; iterator.increment(error))
			{
				const std::filesystem::file_status status = iterator->symlink_status(error);
				if (error)
					break;
				if (std::filesystem::is_symlink(status) || IsReparsePoint(iterator->path()))
				{
					errorMessage = "runtime tree contains a reparse point: " +
						PathToUTF8(iterator->path());
					return false;
				}
				const std::filesystem::path relative =
					iterator->path().lexically_relative(root);
				if (!IsSafeRelativeRuntimePath(relative))
				{
					errorMessage = "runtime tree contains an unsafe relative path";
					return false;
				}
				const std::string key = NormalizedRuntimeKey(relative);
				if (std::filesystem::is_directory(status))
				{
					if (!expectedDirectories.contains(key) ||
						!actualDirectories.emplace(key).second)
					{
						errorMessage = "runtime tree contains an unexpected directory: " +
							PathToUTF8(relative);
						return false;
					}
					continue;
				}
				if (!std::filesystem::is_regular_file(status))
				{
					errorMessage = "runtime tree contains a non-regular entry";
					return false;
				}
				const auto expected = expectedFiles.find(key);
				if (expected == expectedFiles.end() || stamps.contains(key))
				{
					errorMessage = "runtime tree contains an unexpected file: " +
						PathToUTF8(relative);
					return false;
				}
				RuntimeFileStamp stamp;
				if (!ReadRuntimeFileStamp(iterator->path(), stamp, errorMessage))
					return false;
				if (stamp.Size != expected->second)
				{
					errorMessage = "runtime file size mismatch: " +
						PathToUTF8(iterator->path());
					return false;
				}
				stamps.emplace(key, stamp);
			}
			if (error)
			{
				errorMessage = "could not enumerate runtime tree: " + error.message();
				return false;
			}
			if (stamps.size() != expectedFiles.size() ||
				actualDirectories.size() != expectedDirectories.size())
			{
				errorMessage = "runtime tree does not match the manifest file set";
				return false;
			}
			return VerifyRuntimeDirectoryGuards(guards, errorMessage);
		}

		bool BuildRuntimeCompletionDocument(const std::filesystem::path& root,
			const RuntimeManifest& manifest, std::string& document,
			RuntimeFileStamps* observedStamps, std::string& errorMessage)
		{
			RuntimeFileStamps stamps;
			if (!InspectRuntimeTreeMetadata(root, manifest, stamps, errorMessage))
				return false;
			document = SerializeRuntimeCompletion(manifest, stamps);
			if (observedStamps)
				*observedStamps = std::move(stamps);
			return true;
		}

		bool ValidateRuntimeTreeFully(const std::filesystem::path& root,
			const RuntimeManifest& manifest, std::string& completionDocument,
			std::string& errorMessage)
		{
			std::vector<FileSystem::PinnedDirectoryChain> guards;
			if (!PinRuntimeDirectoryTree(root, manifest, false, guards,
				nullptr, errorMessage))
				return false;
			RuntimeFileStamps verified;
			for (const RuntimeManifestFile& file : manifest.Files)
			{
				RuntimeFileStamp stamp;
				if (!ComputeCheckedFileSHA256(root / file.RelativePath, file.Size,
					file.SHA256, &stamp, errorMessage))
					return false;
				verified.emplace(file.Key, stamp);
			}
			RuntimeFileStamp manifestStamp;
			if (!ComputeCheckedFileSHA256(root / RuntimeManifestName,
				manifest.Document.size(), manifest.SHA256, &manifestStamp,
				errorMessage))
				return false;
			verified.emplace(std::string(RuntimeManifestName), manifestStamp);
			if (!VerifyRuntimeDirectoryGuards(guards, errorMessage))
				return false;

			RuntimeFileStamps observed;
			if (!BuildRuntimeCompletionDocument(root, manifest,
				completionDocument, &observed, errorMessage))
				return false;
			if (verified != observed)
			{
				errorMessage = "runtime tree changed after full verification";
				return false;
			}
			return true;
		}

		bool ValidateRuntimeTreeQuickly(const std::filesystem::path& root,
			const std::filesystem::path& completionPath,
			const RuntimeManifest& manifest, std::string& errorMessage)
		{
			std::string stored;
			if (!ReadBoundedRegularFile(completionPath,
				MaximumCompletionMarkerBytes, "runtime completion marker", stored,
				errorMessage))
				return false;
			std::string observed;
			RuntimeFileStamps stamps;
			if (!BuildRuntimeCompletionDocument(root, manifest, observed,
				&stamps, errorMessage))
				return false;
			if (stored != observed)
			{
				errorMessage = "runtime completion metadata changed";
				return false;
			}
			RuntimeFileStamp manifestStamp;
			if (!ComputeCheckedFileSHA256(root / RuntimeManifestName,
				manifest.Document.size(), manifest.SHA256, &manifestStamp,
				errorMessage))
				return false;
			const auto expected = stamps.find(std::string(RuntimeManifestName));
			if (expected == stamps.end() || expected->second != manifestStamp)
			{
				errorMessage = "runtime manifest changed during quick validation";
				return false;
			}
			return true;
		}

		bool CopyRuntimeFile(const std::filesystem::path& source,
			const std::filesystem::path& destination,
			const RuntimeManifestFile& manifestFile, RuntimeFileStamp& stamp,
			std::string& errorMessage)
		{
			RuntimeFileStamp sourceStamp;
			if (!ReadRuntimeFileStamp(source, sourceStamp, errorMessage) ||
				sourceStamp.Size != manifestFile.Size)
			{
				if (errorMessage.empty())
					errorMessage = "runtime source file size mismatch: " +
						PathToUTF8(source);
				return false;
			}
			// EVB virtual files are reliable through ordinary read handles, while
			// CopyFileW (used by filesystem::copy_file on Windows) can return altered
			// bytes for some compressed virtual JSON files. Stream the bytes through
			// the process and validate the physical destination against the manifest.
			// Keep the extraction buffer off the Windows thread stack. Release
			// executables commonly use a 1 MiB stack reserve.
			std::vector<char> buffer(1024 * 1024);
			uint64_t copied = 0;
#ifdef TC_PLATFORM_WINDOWS
			RuntimeFileHandle input(CreateFileW(ExtendedLengthRuntimePath(source).c_str(),
				GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING,
				FILE_ATTRIBUTE_NORMAL | FILE_FLAG_SEQUENTIAL_SCAN |
				FILE_FLAG_OPEN_REPARSE_POINT, nullptr));
			if (!input.Valid())
			{
				const DWORD openError = GetLastError();
				errorMessage = "could not open virtual runtime file '" +
					PathToUTF8(source) + "': " + RuntimeWindowsError(openError);
				return false;
			}
			RuntimeFileHandle output(CreateFileW(
				ExtendedLengthRuntimePath(destination).c_str(), GENERIC_WRITE, 0,
				nullptr, CREATE_NEW,
				FILE_ATTRIBUTE_NORMAL | FILE_FLAG_SEQUENTIAL_SCAN |
				FILE_FLAG_OPEN_REPARSE_POINT, nullptr));
			if (!output.Valid())
			{
				const DWORD openError = GetLastError();
				errorMessage = "could not create extracted runtime file '" +
					PathToUTF8(destination) + "': " + RuntimeWindowsError(openError);
				return false;
			}
			for (;;)
			{
				DWORD byteCount = 0;
				if (ReadFile(input.Get(), buffer.data(),
					static_cast<DWORD>(buffer.size()), &byteCount, nullptr) == FALSE)
				{
					const DWORD readError = GetLastError();
					errorMessage = "could not read the complete virtual runtime file '" +
						PathToUTF8(source) + "': " + RuntimeWindowsError(readError);
					return false;
				}
				if (byteCount == 0)
					break;
				if (copied > manifestFile.Size ||
					static_cast<uint64_t>(byteCount) > manifestFile.Size - copied)
				{
					errorMessage = "virtual runtime file exceeded its declared size: " +
						PathToUTF8(source);
					return false;
				}
				DWORD offset = 0;
				while (offset < byteCount)
				{
					DWORD written = 0;
					const BOOL writeSucceeded = WriteFile(output.Get(),
						buffer.data() + offset, byteCount - offset, &written, nullptr);
					if (writeSucceeded == FALSE || written == 0)
					{
						const DWORD writeError = writeSucceeded == FALSE
							? GetLastError() : ERROR_WRITE_FAULT;
						errorMessage = "could not write the complete runtime file '" +
							PathToUTF8(destination) + "': " +
							RuntimeWindowsError(writeError);
						return false;
					}
					offset += written;
				}
				copied += byteCount;
			}
			if (copied != manifestFile.Size)
			{
				errorMessage = "runtime source file size changed while extracting: " +
					PathToUTF8(source);
				return false;
			}
			if (FlushFileBuffers(output.Get()) == FALSE)
			{
				const DWORD flushError = GetLastError();
				errorMessage = "could not flush the extracted runtime file '" +
					PathToUTF8(destination) + "': " + RuntimeWindowsError(flushError);
				return false;
			}
			DWORD closeError = ERROR_SUCCESS;
			if (!input.Close(closeError))
			{
				errorMessage = "could not close virtual runtime file '" +
					PathToUTF8(source) + "': " + RuntimeWindowsError(closeError);
				return false;
			}
			if (!output.Close(closeError))
			{
				errorMessage = "could not close the extracted runtime file '" +
					PathToUTF8(destination) + "': " + RuntimeWindowsError(closeError);
				return false;
			}
#else
			std::ifstream input(source, std::ios::binary);
			std::ofstream output(destination,
				std::ios::binary | std::ios::out | std::ios::trunc);
			if (!input || !output)
			{
				errorMessage = "could not open runtime extraction streams for '" +
					PathToUTF8(source) + "'";
				return false;
			}
			for (;;)
			{
				input.read(buffer.data(), static_cast<std::streamsize>(buffer.size()));
				const std::streamsize count = input.gcount();
				if (count > 0)
				{
					const uint64_t byteCount = static_cast<uint64_t>(count);
					if (copied > manifestFile.Size ||
						byteCount > manifestFile.Size - copied)
					{
						errorMessage = "virtual runtime file exceeded its declared size: " +
							PathToUTF8(source);
						return false;
					}
					output.write(buffer.data(), count);
					if (!output)
					{
						errorMessage = "could not write the complete runtime file '" +
							PathToUTF8(destination) + "'";
						return false;
					}
					copied += byteCount;
				}
				if (input.eof())
					break;
				if (!input)
				{
					errorMessage = "could not read the complete virtual runtime file '" +
						PathToUTF8(source) + "'";
					return false;
				}
			}
			output.flush();
			if (!output || copied != manifestFile.Size)
			{
				errorMessage = "runtime source file size changed while extracting: " +
					PathToUTF8(source);
				return false;
			}
			output.close();
			if (!output)
			{
				errorMessage = "could not close the extracted runtime file '" +
					PathToUTF8(destination) + "'";
				return false;
			}
#endif
			return ComputeCheckedFileSHA256(destination, manifestFile.Size,
				manifestFile.SHA256, &stamp, errorMessage);
		}

		std::filesystem::path AbsoluteLexical(const std::filesystem::path& path,
			std::string& errorMessage)
		{
			std::error_code error;
			const std::filesystem::path result = std::filesystem::absolute(path, error);
			if (error)
			{
				errorMessage = "could not make runtime path absolute: " + error.message();
				return {};
			}
			return result.lexically_normal();
		}

		bool CreateDirectoryChainAndPin(const std::filesystem::path& directory,
			FileSystem::PinnedDirectoryChain& guard, std::string& errorMessage)
		{
			guard.Reset();
			const std::filesystem::path absolute = directory.lexically_normal();
			std::filesystem::path current = absolute.root_path();
			if (current.empty() || !guard.Acquire(current, errorMessage))
				return false;
			for (const auto& component : absolute.relative_path())
			{
				current /= component;
				FileSystem::PinnedDirectoryChain next;
				bool created = false;
				if (!FileSystem::CreateDirectoryAndPin(
					current, next, created, errorMessage))
					return false;
				guard = std::move(next);
			}
			return true;
		}

		bool PathExistsWithoutFollowing(const std::filesystem::path& path,
			bool& exists, std::string& errorMessage)
		{
			std::error_code error;
			const auto status = std::filesystem::symlink_status(path, error);
			if (error == std::errc::no_such_file_or_directory)
			{
				exists = false;
				return true;
			}
			if (error)
			{
				errorMessage = "could not inspect runtime cache path '" +
					PathToUTF8(path) + "': " + error.message();
				return false;
			}
			exists = std::filesystem::exists(status);
			return true;
		}

		bool RemoveRuntimeTreeSafely(const std::filesystem::path& root,
			std::string& errorMessage)
		{
			bool exists = false;
			if (!PathExistsWithoutFollowing(root, exists, errorMessage))
				return false;
			if (!exists)
				return true;
			std::error_code statusError;
			const auto rootStatus = std::filesystem::symlink_status(root, statusError);
			if (statusError)
			{
				errorMessage = "could not inspect stale runtime tree: " +
					statusError.message();
				return false;
			}
			if (std::filesystem::is_symlink(rootStatus) || IsReparsePoint(root))
			{
				errorMessage = "refusing to clean a runtime reparse point: " +
					PathToUTF8(root);
				return false;
			}
			if (!std::filesystem::is_directory(rootStatus))
			{
				bool removed = false;
				return FileSystem::RemovePathSafely(root, removed, errorMessage) && removed;
			}

			FileSystem::PinnedDirectoryChain rootGuard;
			if (!rootGuard.Acquire(root, errorMessage))
				return false;
			std::vector<std::filesystem::path> entries;
			std::error_code error;
			std::filesystem::recursive_directory_iterator iterator(root,
				std::filesystem::directory_options::none, error), end;
			for (; !error && iterator != end; iterator.increment(error))
			{
				const auto status = iterator->symlink_status(error);
				if (error)
					break;
				if (std::filesystem::is_symlink(status) || IsReparsePoint(iterator->path()))
				{
					iterator.disable_recursion_pending();
					errorMessage = "refusing to clean a runtime tree containing a reparse point: " +
						PathToUTF8(iterator->path());
					return false;
				}
				if (!std::filesystem::is_directory(status) &&
					!std::filesystem::is_regular_file(status))
				{
					errorMessage = "refusing to clean a runtime tree with a special entry";
					return false;
				}
				entries.push_back(iterator->path());
			}
			if (error || !rootGuard.Verify(errorMessage))
			{
				if (error)
					errorMessage = "could not enumerate stale runtime tree: " + error.message();
				return false;
			}
			rootGuard.Reset();
			std::sort(entries.begin(), entries.end(),
				[](const auto& left, const auto& right)
				{
					return std::distance(left.begin(), left.end()) >
						std::distance(right.begin(), right.end());
				});
			for (const auto& entry : entries)
			{
				bool removed = false;
				if (!FileSystem::RemovePathSafely(entry, removed, errorMessage) || !removed)
					return false;
			}
			bool removed = false;
			return FileSystem::RemovePathSafely(root, removed, errorMessage) && removed;
		}

		class RuntimeBundleMutex
		{
		public:
			RuntimeBundleMutex() = default;
			~RuntimeBundleMutex()
			{
#ifdef TC_PLATFORM_WINDOWS
				if (m_Acquired)
					ReleaseMutex(m_Handle);
				if (m_Handle)
					CloseHandle(m_Handle);
#else
				if (m_Lock.owns_lock())
					m_Lock.unlock();
#endif
			}

			RuntimeBundleMutex(const RuntimeBundleMutex&) = delete;
			RuntimeBundleMutex& operator=(const RuntimeBundleMutex&) = delete;

			bool Acquire(const std::filesystem::path& cachePath,
				std::string& errorMessage)
			{
				std::string key = NormalizedRuntimeKey(cachePath);
				const std::string digest = ComputeContentSHA256(
					std::span<const uint8_t>(
						reinterpret_cast<const uint8_t*>(key.data()), key.size()));
#ifdef TC_PLATFORM_WINDOWS
				const std::wstring name = L"Local\\TomCatEditorRuntimeBundle-" +
					UTF8ToPath(digest).wstring();
				m_Handle = CreateMutexW(nullptr, FALSE, name.c_str());
				if (!m_Handle)
				{
					const DWORD mutexError = GetLastError();
					errorMessage = "could not create the runtime extraction mutex: " +
						std::error_code(static_cast<int>(mutexError),
							std::system_category()).message();
					return false;
				}
				const DWORD wait = WaitForSingleObject(m_Handle, 120000);
				if (wait != WAIT_OBJECT_0 && wait != WAIT_ABANDONED)
				{
					errorMessage = wait == WAIT_TIMEOUT
						? "timed out waiting for another Editor to extract the runtime"
						: "could not wait for the runtime extraction mutex";
					return false;
				}
				m_Acquired = true;
#else
				(void)digest;
				m_Lock = std::unique_lock<std::mutex>(s_FallbackMutex);
#endif
				return true;
			}

		private:
#ifdef TC_PLATFORM_WINDOWS
			HANDLE m_Handle = nullptr;
			bool m_Acquired = false;
#else
			inline static std::mutex s_FallbackMutex;
			std::unique_lock<std::mutex> m_Lock;
#endif
		};

		void PopulateResult(const std::filesystem::path& root,
			const RuntimeManifest& manifest, bool reused,
			EditorRuntimeBundleResult& result)
		{
			result.Root = root;
			result.ManagedDirectory = root / "Managed";
			result.PlayerTemplateDirectory =
				root / "Packages" / "PlayerTemplates" / "win-x64";
			result.CliExecutable = root / "TomCatCLI.exe";
			result.EngineBuildID = manifest.EngineBuildID;
			result.ManifestSHA256 = manifest.SHA256;
			result.ReusedExisting = reused;
		}

	}

	bool EnsureEditorRuntimeBundle(const std::filesystem::path& payloadRoot,
		const std::filesystem::path& cacheBaseRoot,
		EditorRuntimeBundleResult& result, std::string& errorMessage)
	{
		result = {};
		errorMessage.clear();
		if (payloadRoot.empty() || cacheBaseRoot.empty())
		{
			errorMessage = "runtime payload and cache roots are required";
			return false;
		}

		std::string pathError;
		const std::filesystem::path absolutePayload =
			AbsoluteLexical(payloadRoot, pathError);
		if (absolutePayload.empty())
		{
			errorMessage = std::move(pathError);
			return false;
		}
		const std::filesystem::path absoluteCache =
			AbsoluteLexical(cacheBaseRoot, pathError);
		if (absoluteCache.empty())
		{
			errorMessage = std::move(pathError);
			return false;
		}

		RuntimeManifest manifest;
		if (!LoadRuntimeManifest(absolutePayload, manifest, errorMessage))
			return false;
		if (manifest.EngineBuildID != Version::EngineBuildID)
		{
			errorMessage = "runtime manifest engineBuildId does not match this Editor";
			return false;
		}
		const std::filesystem::path engineRoot =
			absoluteCache / UTF8ToPath(manifest.EngineBuildID);
		const std::filesystem::path finalRoot = engineRoot / manifest.SHA256;
		const std::filesystem::path completionPath = RuntimeSiblingWithSuffix(
			finalRoot, RuntimeCompletionSuffix);
		const std::filesystem::path staging = RuntimeSiblingWithSuffix(
			finalRoot, RuntimeStagingSuffix);
		const std::filesystem::path corrupt = RuntimeSiblingWithSuffix(
			finalRoot, RuntimeCorruptSuffix);
		RuntimeBundleMutex extractionMutex;
		if (!extractionMutex.Acquire(finalRoot, errorMessage))
			return false;

		FileSystem::PinnedDirectoryChain engineGuard;
		if (!CreateDirectoryChainAndPin(engineRoot, engineGuard, errorMessage))
			return false;

		bool finalExists = false;
		if (!PathExistsWithoutFollowing(finalRoot, finalExists, errorMessage))
			return false;
		if (finalExists)
		{
			std::string validationError;
			if (ValidateRuntimeTreeQuickly(
				finalRoot, completionPath, manifest, validationError))
			{
				PopulateResult(finalRoot, manifest, true, result);
				return true;
			}

			std::string completionDocument;
			if (ValidateRuntimeTreeFully(
				finalRoot, manifest, completionDocument, validationError))
			{
				std::string writeError;
				if (!FileSystem::WriteFileAtomically(
					completionPath, completionDocument, writeError))
				{
					errorMessage = "could not refresh the runtime completion marker: " +
						writeError;
					return false;
				}
				PopulateResult(finalRoot, manifest, true, result);
				return true;
			}
		}

		auto removeCompletionMarker = [&]()
		{
			bool markerExists = false;
			if (!PathExistsWithoutFollowing(
				completionPath, markerExists, errorMessage))
				return false;
			if (!markerExists)
				return true;
			bool removed = false;
			if (!FileSystem::RemovePathSafely(
				completionPath, removed, errorMessage) || !removed)
			{
				if (errorMessage.empty())
					errorMessage = "could not remove stale runtime completion marker";
				return false;
			}
			return true;
		};

		if (!removeCompletionMarker())
			return false;
		if (!RemoveRuntimeTreeSafely(staging, errorMessage) ||
			!RemoveRuntimeTreeSafely(corrupt, errorMessage))
			return false;

		std::error_code filesystemError;
		if (finalExists)
		{
			if (IsReparsePoint(finalRoot))
			{
				errorMessage = "refusing to quarantine a runtime cache reparse point";
				return false;
			}
			if (!engineGuard.Verify(errorMessage))
				return false;
			std::filesystem::rename(finalRoot, corrupt, filesystemError);
			if (filesystemError)
			{
				errorMessage = "could not quarantine the damaged Editor runtime: " +
					filesystemError.message();
				return false;
			}
		}

		std::vector<FileSystem::PinnedDirectoryChain> payloadGuards;
		if (!PinRuntimeDirectoryTree(absolutePayload, manifest, false,
			payloadGuards, nullptr, errorMessage))
		{
			errorMessage = "runtime payload contains an unsafe directory: " + errorMessage;
			return false;
		}

		std::vector<FileSystem::PinnedDirectoryChain> stagingGuards;
		bool stagingCreated = false;
		if (!PinRuntimeDirectoryTree(staging, manifest, true,
			stagingGuards, &stagingCreated, errorMessage) || !stagingCreated)
		{
			if (errorMessage.empty())
				errorMessage = "Editor runtime staging already existed after cleanup";
			return false;
		}
		auto discardStaging = [&]()
		{
			stagingGuards.clear();
			std::string ignored;
			RemoveRuntimeTreeSafely(staging, ignored);
		};

		RuntimeFileStamps copiedStamps;
		for (const RuntimeManifestFile& file : manifest.Files)
		{
			RuntimeFileStamp stamp;
			if (!CopyRuntimeFile(absolutePayload / file.RelativePath,
				staging / file.RelativePath, file, stamp, errorMessage))
			{
				discardStaging();
				return false;
			}
			copiedStamps.emplace(file.Key, stamp);
		}
		std::string writeError;
		if (!FileSystem::WriteFileAtomically(staging / RuntimeManifestName,
			manifest.Document, writeError))
		{
			errorMessage = "could not publish the runtime completion manifest: " +
				writeError;
			discardStaging();
			return false;
		}
		RuntimeFileStamp manifestStamp;
		if (!ComputeCheckedFileSHA256(staging / RuntimeManifestName,
			manifest.Document.size(), manifest.SHA256, &manifestStamp, errorMessage))
		{
			discardStaging();
			return false;
		}
		copiedStamps.emplace(std::string(RuntimeManifestName), manifestStamp);
		if (!VerifyRuntimeDirectoryGuards(stagingGuards, errorMessage) ||
			!VerifyRuntimeDirectoryGuards(payloadGuards, errorMessage))
		{
			discardStaging();
			return false;
		}
		std::string completionDocument;
		RuntimeFileStamps stagedStamps;
		if (!BuildRuntimeCompletionDocument(staging, manifest,
			completionDocument, &stagedStamps, errorMessage) ||
			copiedStamps != stagedStamps)
		{
			if (errorMessage.empty())
				errorMessage = "staged Editor runtime changed after file verification";
			discardStaging();
			return false;
		}
		RuntimeDirectoryIdentity stagingIdentity;
		if (!ReadRuntimeDirectoryIdentity(staging, stagingIdentity, errorMessage))
		{
			discardStaging();
			return false;
		}
		stagingGuards.clear();
		payloadGuards.clear();
		if (!engineGuard.Verify(errorMessage))
		{
			discardStaging();
			return false;
		}
		filesystemError.clear();
		std::filesystem::rename(staging, finalRoot, filesystemError);
		if (filesystemError)
		{
			errorMessage = "could not publish the Editor runtime atomically: " +
				filesystemError.message();
			discardStaging();
			return false;
		}
		RuntimeDirectoryIdentity publishedIdentity;
		std::string publishedDocument;
		if (!ReadRuntimeDirectoryIdentity(
			finalRoot, publishedIdentity, errorMessage) ||
			publishedIdentity != stagingIdentity ||
			!BuildRuntimeCompletionDocument(finalRoot, manifest,
				publishedDocument, nullptr, errorMessage) ||
			publishedDocument != completionDocument)
		{
			if (errorMessage.empty())
				errorMessage = "published Editor runtime identity or metadata changed";
			return false;
		}
		if (!FileSystem::WriteFileAtomically(
			completionPath, completionDocument, writeError))
		{
			errorMessage = "could not publish the runtime completion marker: " +
				writeError;
			return false;
		}

		PopulateResult(finalRoot, manifest, false, result);
		return true;
	}

	bool ConfigurePackagedEditorRuntime(
		const std::filesystem::path& payloadRoot,
		EditorRuntimeBundleResult& result, std::string& errorMessage)
	{
		const auto cacheRoot = ApplicationPaths::GetEditorRuntimeCacheRoot();
		if (!cacheRoot)
		{
			result = {};
			errorMessage = "the LocalAppData Editor runtime cache is unavailable";
			return false;
		}
		if (!EnsureEditorRuntimeBundle(payloadRoot, *cacheRoot, result, errorMessage))
			return false;
		ApplicationPaths::SetRuntimeEditorRoot(result.Root);
		return true;
	}

}
