#include "tcpch.h"
#include "AssetRegistry.h"

#include "TomCat/Scene/SceneSerializer.h"
#include "TomCat/Utils/FileSystemUtils.h"
#include "TomCat/Utils/PathUtils.h"

#include <yaml-cpp/yaml.h>

#include <algorithm>
#include <array>
#include <cctype>
#include <fstream>
#include <limits>
#include <map>
#include <sstream>
#include <stdexcept>
#include <string_view>
#include <system_error>
#include <unordered_map>
#include <unordered_set>

namespace TomCat {

	namespace {

		constexpr uint32_t kMetadataSchemaVersion = 1;
		constexpr uint32_t kRegistryCacheSchemaVersion = 1;

		std::string LowerASCII(std::string value)
		{
			std::transform(value.begin(), value.end(), value.begin(),
				[](unsigned char character) { return static_cast<char>(std::tolower(character)); });
			return value;
		}

		bool IsVersionControlDirectoryName(const std::filesystem::path& component)
		{
			const std::string name = LowerASCII(PathToUTF8(component));
			return name == ".git" || name == ".svn" || name == ".hg" ||
				name == ".bzr" || name == ".jj";
		}

		bool ContainsVersionControlDirectory(const std::filesystem::path& path)
		{
			for (const auto& component : path.lexically_normal())
			{
				if (IsVersionControlDirectoryName(component))
					return true;
			}
			return false;
		}

		std::string PathKey(const std::filesystem::path& path)
		{
			const std::filesystem::path normalized = path.lexically_normal();
			std::string key = normalized == "." ? std::string{} : PathToUTF8(normalized);
#ifdef TC_PLATFORM_WINDOWS
			key = LowerASCII(std::move(key));
#endif
			return key;
		}

		std::filesystem::path AbsoluteLexicalPath(const std::filesystem::path& path)
		{
			if (path.empty())
				return {};
			std::error_code error;
			const std::filesystem::path absolute = std::filesystem::absolute(path, error);
			return (error ? path : absolute).lexically_normal();
		}

		bool PathComponentEqual(const std::filesystem::path& left,
			const std::filesystem::path& right)
		{
#ifdef TC_PLATFORM_WINDOWS
			return LowerASCII(PathToUTF8(left)) == LowerASCII(PathToUTF8(right));
#else
			return left == right;
#endif
		}

		bool MakeRelativeWithin(const std::filesystem::path& root,
			const std::filesystem::path& candidate, std::filesystem::path& relative)
		{
			auto rootPart = root.begin();
			auto candidatePart = candidate.begin();
			for (; rootPart != root.end(); ++rootPart, ++candidatePart)
			{
				if (candidatePart == candidate.end() || !PathComponentEqual(*rootPart, *candidatePart))
					return false;
			}

			relative.clear();
			for (; candidatePart != candidate.end(); ++candidatePart)
				relative /= *candidatePart;
			if (relative.empty())
				relative = ".";
			return true;
		}

		bool IsAtOrBelow(const std::filesystem::path& root,
			const std::filesystem::path& candidate)
		{
			const std::string rootKey = PathKey(root);
			const std::string candidateKey = PathKey(candidate);
			if (rootKey.empty())
				return true;
			return candidateKey == rootKey ||
				(candidateKey.size() > rootKey.size() &&
					candidateKey.compare(0, rootKey.size(), rootKey) == 0 &&
					candidateKey[rootKey.size()] == '/');
		}

		bool IsNoSuchFileError(const std::error_code& error)
		{
			return error == std::errc::no_such_file_or_directory ||
				error == std::errc::not_a_directory;
		}

		bool IsReparsePoint(const std::filesystem::path& path)
		{
			std::error_code error;
			const std::filesystem::file_status status = std::filesystem::symlink_status(path, error);
			if (!error && std::filesystem::is_symlink(status))
				return true;
#ifdef TC_PLATFORM_WINDOWS
			const DWORD attributes = GetFileAttributesW(path.c_str());
			return attributes != INVALID_FILE_ATTRIBUTES &&
				(attributes & FILE_ATTRIBUTE_REPARSE_POINT) != 0;
#else
			return false;
#endif
		}

		bool EntryExists(const std::filesystem::path& path)
		{
			std::error_code error;
			const std::filesystem::file_status status = std::filesystem::symlink_status(path, error);
			return !error && std::filesystem::exists(status);
		}

		bool IsMetadataTemporaryFile(const std::filesystem::path& path)
		{
			const std::string name = LowerASCII(PathToUTF8(path.filename()));
			return name.find(".tcmeta.tmp-") != std::string::npos;
		}

		std::filesystem::path SourcePathFromMetadata(const std::filesystem::path& metadataPath)
		{
			std::filesystem::path source = metadataPath;
			source.replace_extension();
			return source;
		}

		bool HasHandleSuffix(const std::string& key)
		{
			constexpr std::string_view suffix = "Handle";
			return key.size() >= suffix.size() &&
				key.compare(key.size() - suffix.size(), suffix.size(), suffix) == 0;
		}

		void CollectHandleReferences(const YAML::Node& node, const std::string& propertyPath,
			const std::unordered_set<uint64_t>& targets, AssetHandle referencingAsset,
			const std::filesystem::path& scenePath, std::vector<AssetReference>& references,
			bool& complete, uint32_t depth = 0)
		{
			if (depth > 128)
			{
				complete = false;
				return;
			}

			if (node.IsMap())
			{
				for (const auto& entry : node)
				{
					std::string key;
					try
					{
						key = entry.first.as<std::string>();
					}
					catch (const std::exception&)
					{
						complete = false;
						continue;
					}

					const YAML::Node value = entry.second;
					const std::string childPath = propertyPath + "." + key;
					if (HasHandleSuffix(key))
					{
						if (!value.IsScalar())
							complete = false;
						else
						{
							try
							{
								const uint64_t handle = value.as<uint64_t>();
								if (handle != 0 && targets.find(handle) != targets.end())
								{
									AssetReference reference;
									reference.ReferencedAsset = AssetHandle(handle);
									reference.ReferencingAsset = referencingAsset;
									reference.FilePath = scenePath;
									reference.PropertyPath = childPath;
									references.emplace_back(std::move(reference));
								}
							}
							catch (const std::exception&)
							{
								complete = false;
							}
						}
					}
					CollectHandleReferences(value, childPath, targets, referencingAsset,
						scenePath, references, complete, depth + 1);
				}
			}
			else if (node.IsSequence())
			{
				for (size_t index = 0; index < node.size(); ++index)
				{
					CollectHandleReferences(node[index], propertyPath + "[" +
						std::to_string(index) + "]", targets, referencingAsset,
						scenePath, references, complete, depth + 1);
				}
			}
		}

		bool PathsReferToSameEntry(const std::filesystem::path& left,
			const std::filesystem::path& right)
		{
			std::error_code error;
			const bool equivalent = std::filesystem::equivalent(left, right, error);
			return !error && equivalent;
		}

		bool RenameEntry(const std::filesystem::path& source,
			const std::filesystem::path& destination, std::string& errorMessage)
		{
			errorMessage.clear();
			if (source == destination)
				return true;

			const bool sameEntry = PathsReferToSameEntry(source, destination);
			if (!sameEntry)
			{
				std::error_code error;
				std::filesystem::rename(source, destination, error);
				if (!error)
					return true;
				errorMessage = error.message();
				return false;
			}

			// A same-entry rename is normally a casing-only change. Keep it one
			// filesystem operation so a crash cannot strand an untracked temporary.
#ifdef TC_PLATFORM_WINDOWS
			if (MoveFileExW(source.c_str(), destination.c_str(),
				MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH) != FALSE)
				return true;
			errorMessage = std::error_code(static_cast<int>(GetLastError()),
				std::system_category()).message();
			return false;
#else
			std::error_code error;
			std::filesystem::rename(source, destination, error);
			if (!error)
				return true;
			errorMessage = error.message();
			return false;
#endif
		}

		bool CreateDeleteStagingDirectory(const std::filesystem::path& libraryDirectory,
			std::filesystem::path& stagingDirectory, std::string& errorMessage)
		{
			stagingDirectory.clear();
			errorMessage.clear();
			if (libraryDirectory.empty() || IsReparsePoint(libraryDirectory))
			{
				errorMessage = "Library is unavailable or is a symbolic/reparse path";
				return false;
			}

			const std::filesystem::path trashRoot = libraryDirectory / "DeletedAssets";
			std::error_code error;
			std::filesystem::create_directories(trashRoot, error);
			if (error || IsReparsePoint(trashRoot))
			{
				errorMessage = error ? error.message() :
					"delete staging root is a symbolic/reparse path";
				return false;
			}

			for (uint32_t attempt = 0; attempt < 100; ++attempt)
			{
				const std::filesystem::path candidate =
					FileSystem::MakeTemporarySiblingPath(trashRoot / "delete");
				if (candidate.empty())
					break;
				error.clear();
				if (std::filesystem::create_directory(candidate, error))
				{
					stagingDirectory = candidate;
					return true;
				}
				if (error && error != std::errc::file_exists)
				{
					errorMessage = error.message();
					return false;
				}
			}

			errorMessage = "could not allocate a unique delete staging directory";
			return false;
		}

		void RemoveStagingDirectoryBestEffort(const std::filesystem::path& path)
		{
			if (path.empty())
				return;
			std::error_code ignored;
			std::filesystem::remove_all(path, ignored);
		}

		bool ComputeAssetFingerprint(const std::filesystem::path& path,
			uint64_t& size, uint64_t& hash)
		{
			size = 0;
			hash = 14695981039346656037ull;
			std::error_code error;
			const uintmax_t fileSize = std::filesystem::file_size(path, error);
			if (error || fileSize > (std::numeric_limits<uint64_t>::max)())
				return false;
			size = static_cast<uint64_t>(fileSize);

			std::ifstream input(path, std::ios::binary);
			if (!input)
				return false;
			std::array<unsigned char, 64 * 1024> buffer{};
			uint64_t remaining = size;
			while (remaining > 0)
			{
				const size_t chunk = static_cast<size_t>((std::min)(remaining,
					static_cast<uint64_t>(buffer.size())));
				if (!input.read(reinterpret_cast<char*>(buffer.data()),
					static_cast<std::streamsize>(chunk)))
					return false;
				for (size_t index = 0; index < chunk; ++index)
				{
					hash ^= buffer[index];
					hash *= 1099511628211ull;
				}
				remaining -= chunk;
			}
			return input.peek() == std::char_traits<char>::eof();
		}

		bool ReadWholeFile(const std::filesystem::path& path, std::vector<uint8_t>& bytes)
		{
			bytes.clear();
			std::ifstream input(path, std::ios::binary | std::ios::ate);
			if (!input)
				return false;
			const std::streamoff end = input.tellg();
			if (end < 0 || static_cast<uint64_t>(end) >
				static_cast<uint64_t>((std::numeric_limits<size_t>::max)()) ||
				static_cast<uint64_t>(end) > static_cast<uint64_t>(bytes.max_size()))
				return false;

			try
			{
				bytes.resize(static_cast<size_t>(end));
			}
			catch (const std::exception&)
			{
				return false;
			}

			input.seekg(0, std::ios::beg);
			constexpr size_t chunkSize = 64 * 1024;
			for (size_t copied = 0; copied < bytes.size();)
			{
				const size_t chunk = (std::min)(chunkSize, bytes.size() - copied);
				if (!input.read(reinterpret_cast<char*>(bytes.data() + copied),
					static_cast<std::streamsize>(chunk)))
				{
					bytes.clear();
					return false;
				}
				copied += chunk;
			}
			return true;
		}

	}

	bool AssetRegistry::Initialize(const std::filesystem::path& assetDirectory,
		const std::filesystem::path& libraryDirectory)
	{
		Shutdown();
		m_AssetDirectory = AbsoluteLexicalPath(assetDirectory);
		if (m_AssetDirectory.empty() || ContainsVersionControlDirectory(m_AssetDirectory) ||
			IsReparsePoint(m_AssetDirectory))
		{
			TC_Core_Error("Asset directory is empty, version-control metadata, or a symbolic/reparse path: {0}",
				PathToUTF8(assetDirectory));
			Shutdown();
			return false;
		}

		std::error_code error;
		const std::filesystem::file_status assetStatus =
			std::filesystem::symlink_status(m_AssetDirectory, error);
		if (error || !std::filesystem::is_directory(assetStatus))
		{
			TC_Core_Error("Asset directory is unavailable: {0}", PathToUTF8(m_AssetDirectory));
			Shutdown();
			return false;
		}

		m_CanonicalAssetDirectory = std::filesystem::canonical(m_AssetDirectory, error);
		if (error || m_CanonicalAssetDirectory.empty())
		{
			TC_Core_Error("Could not canonicalize asset directory '{0}': {1}",
				PathToUTF8(m_AssetDirectory), error.message());
			Shutdown();
			return false;
		}

		m_LibraryDirectory = AbsoluteLexicalPath(libraryDirectory.empty()
			? m_AssetDirectory.parent_path() / "Library" : libraryDirectory);
		std::filesystem::path libraryRelative, assetRelative;
		if (m_LibraryDirectory.empty() || ContainsVersionControlDirectory(m_LibraryDirectory) ||
			MakeRelativeWithin(m_AssetDirectory, m_LibraryDirectory, libraryRelative) ||
			MakeRelativeWithin(m_LibraryDirectory, m_AssetDirectory, assetRelative))
		{
			TC_Core_Error("Library and Assets directories must not contain one another: {0}",
				PathToUTF8(m_LibraryDirectory));
			Shutdown();
			return false;
		}

		error.clear();
		std::filesystem::create_directories(m_LibraryDirectory, error);
		if (error || IsReparsePoint(m_LibraryDirectory))
		{
			TC_Core_Error("Library directory is unavailable or is a symbolic/reparse path: {0}",
				PathToUTF8(m_LibraryDirectory));
			Shutdown();
			return false;
		}
		error.clear();
		const std::filesystem::path canonicalLibrary =
			std::filesystem::canonical(m_LibraryDirectory, error);
		if (error || MakeRelativeWithin(m_CanonicalAssetDirectory, canonicalLibrary,
			libraryRelative) || MakeRelativeWithin(canonicalLibrary,
			m_CanonicalAssetDirectory, assetRelative))
		{
			TC_Core_Error("Canonical Library and Assets directories must not overlap: {0}",
				PathToUTF8(m_LibraryDirectory));
			Shutdown();
			return false;
		}

		m_Initialized = true;
		if (!LoadCache())
			TC_Core_Warn("Ignoring invalid rebuildable asset registry cache");
		// A damaged individual sidecar is non-fatal to the rest of the project.
		(void)Refresh();
		return true;
	}

	void AssetRegistry::Shutdown()
	{
		m_Assets.clear();
		m_PathIndex.clear();
		m_AssetDirectory.clear();
		m_LibraryDirectory.clear();
		m_CanonicalAssetDirectory.clear();
		m_Initialized = false;
	}

	bool AssetRegistry::NormalizeManagedPath(const std::filesystem::path& path,
		std::filesystem::path& absolutePath, std::filesystem::path& relativePath,
		bool allowMissingLeaf, bool allowRoot) const
	{
		absolutePath.clear();
		relativePath.clear();
		if (!m_Initialized || path.empty())
			return false;
		if (!path.is_absolute() && (path.has_root_name() || path.has_root_directory()))
			return false;

		absolutePath = AbsoluteLexicalPath(path.is_absolute() ? path : m_AssetDirectory / path);
		if (!MakeRelativeWithin(m_AssetDirectory, absolutePath, relativePath))
			return false;
		if (relativePath == ".")
		{
			if (!allowRoot)
				return false;
			relativePath.clear();
			return true;
		}

		for (const auto& component : relativePath)
		{
			if (component == "..")
				return false;
		}

		std::filesystem::path inspected = m_AssetDirectory;
		bool reachedMissingComponent = false;
		for (const auto& component : relativePath)
		{
			inspected /= component;
			if (reachedMissingComponent)
				continue;

			std::error_code error;
			const std::filesystem::file_status status =
				std::filesystem::symlink_status(inspected, error);
			if (error)
			{
				if (allowMissingLeaf && IsNoSuchFileError(error))
				{
					reachedMissingComponent = true;
					continue;
				}
				return false;
			}
			if (!std::filesystem::exists(status))
			{
				if (!allowMissingLeaf)
					return false;
				reachedMissingComponent = true;
				continue;
			}
			if (std::filesystem::is_symlink(status) || IsReparsePoint(inspected))
				return false;

			error.clear();
			const std::filesystem::path canonical =
				std::filesystem::weakly_canonical(inspected, error);
			std::filesystem::path canonicalRelative;
			if (error || !MakeRelativeWithin(m_CanonicalAssetDirectory,
				canonical.lexically_normal(), canonicalRelative))
				return false;
		}

		return allowMissingLeaf || !reachedMissingComponent;
	}

	bool AssetRegistry::ValidateManagedTree(const std::filesystem::path& root) const
	{
		std::filesystem::path absoluteRoot, relativeRoot;
		if (!NormalizeManagedPath(root, absoluteRoot, relativeRoot, false, false))
			return false;

		std::error_code error;
		const std::filesystem::file_status rootStatus =
			std::filesystem::symlink_status(absoluteRoot, error);
		if (error || std::filesystem::is_symlink(rootStatus) || IsReparsePoint(absoluteRoot))
			return false;
		if (!std::filesystem::is_directory(rootStatus))
			return std::filesystem::is_regular_file(rootStatus);

		std::filesystem::recursive_directory_iterator iterator(absoluteRoot,
			std::filesystem::directory_options::none, error), end;
		if (error)
			return false;
		while (iterator != end)
		{
			const std::filesystem::path entryPath = iterator->path();
			if (IsReparsePoint(entryPath))
			{
				TC_Core_Error("Symbolic/reparse entries are not managed assets: {0}",
					PathToUTF8(entryPath));
				return false;
			}
			std::filesystem::path absoluteEntry, relativeEntry;
			if (!NormalizeManagedPath(entryPath, absoluteEntry, relativeEntry, false, false))
				return false;
			iterator.increment(error);
			if (error)
				return false;
		}
		return true;
	}

	bool AssetRegistry::ReadMetadata(const std::filesystem::path& metadataPath,
		AssetMetadata& metadata, MetadataTransaction* transaction) const
	{
		try
		{
			std::filesystem::path absoluteMetadata, relativeMetadata;
			if (!NormalizeManagedPath(metadataPath, absoluteMetadata, relativeMetadata,
				false, false) || !IsMetaFile(absoluteMetadata))
				throw std::runtime_error("metadata path is outside Assets");
			if (IsReparsePoint(metadataPath))
				throw std::runtime_error("symbolic/reparse metadata files are forbidden");
			std::ifstream input(metadataPath, std::ios::binary);
			if (!input)
				throw std::runtime_error("could not open the file");
			const YAML::Node root = YAML::Load(input);
			if (input.bad() || !root.IsMap())
				throw std::runtime_error("document must be a map");
			if (!root["SchemaVersion"] ||
				root["SchemaVersion"].as<uint32_t>() != kMetadataSchemaVersion)
				throw std::runtime_error("unsupported or missing SchemaVersion");

			const YAML::Node asset = root["Asset"];
			if (!asset || !asset.IsMap() || !asset["Handle"] || !asset["Type"])
				throw std::runtime_error("Asset.Handle and Asset.Type are required");
			const uint64_t handle = asset["Handle"].as<uint64_t>();
			if (handle == 0)
				throw std::runtime_error("Handle 0 is reserved");
			const AssetType type = AssetTypeFromString(asset["Type"].as<std::string>());
			if (type == AssetType::None)
				throw std::runtime_error("unknown or invalid asset type");

			AssetImportSettings settings;
			const YAML::Node importSettings = asset["ImportSettings"];
			if (!importSettings || !importSettings.IsMap())
				throw std::runtime_error("Asset.ImportSettings is required and must be a map");
			for (const auto& entry : importSettings)
			{
				if (!entry.first.IsScalar() || !entry.second.IsScalar())
					throw std::runtime_error("import setting keys and values must be scalar strings");
				settings.emplace(entry.first.as<std::string>(), entry.second.as<std::string>());
			}

			MetadataTransaction parsedTransaction;
			const YAML::Node transactionNode = root["Transaction"];
			if (transactionNode)
			{
				if (!transactionNode.IsMap() || !transactionNode["Operation"] ||
					!transactionNode["Source"] || !transactionNode["OriginalSize"] ||
					!transactionNode["OriginalHash"])
					throw std::runtime_error("Transaction operation, source, and fingerprint are required");
				const std::string operation = transactionNode["Operation"].as<std::string>();
				parsedTransaction.Source = UTF8ToPath(
					transactionNode["Source"].as<std::string>()).lexically_normal();
				parsedTransaction.OriginalSize = transactionNode["OriginalSize"].as<uint64_t>();
				parsedTransaction.OriginalHash = transactionNode["OriginalHash"].as<uint64_t>();
				parsedTransaction.HasFingerprint = true;
				if (operation == "Move")
				{
					if (!transactionNode["Destination"])
						throw std::runtime_error("Move transaction requires Destination");
					parsedTransaction.Type = MetadataTransactionType::Move;
					parsedTransaction.Destination = UTF8ToPath(
						transactionNode["Destination"].as<std::string>()).lexically_normal();
				}
				else if (operation == "Delete")
				{
					if (!transactionNode["Staging"])
						throw std::runtime_error("Delete transaction requires Staging");
					parsedTransaction.Type = MetadataTransactionType::Delete;
					parsedTransaction.Staging = UTF8ToPath(
						transactionNode["Staging"].as<std::string>()).lexically_normal();
				}
				else
					throw std::runtime_error("unknown metadata transaction operation");
			}

			metadata = AssetMetadata{};
			metadata.Handle = AssetHandle(handle);
			metadata.Type = type;
			metadata.ImportSettings = std::move(settings);
			if (transaction)
				*transaction = std::move(parsedTransaction);
			return true;
		}
		catch (const std::exception& exception)
		{
			TC_Core_Error("Damaged asset metadata '{0}' was not overwritten: {1}",
				PathToUTF8(metadataPath), exception.what());
			return false;
		}
	}

	bool AssetRegistry::WriteMetadata(const std::filesystem::path& metadataPath,
		const AssetMetadata& metadata, bool replaceExisting,
		const MetadataTransaction* transaction) const
	{
		if (static_cast<uint64_t>(metadata.Handle) == 0 || metadata.Type == AssetType::None)
			return false;
		if (transaction && transaction->Type != MetadataTransactionType::None &&
			!transaction->HasFingerprint)
			return false;
		std::filesystem::path absoluteMetadata, relativeMetadata;
		if (!NormalizeManagedPath(metadataPath, absoluteMetadata, relativeMetadata,
			true, false) || !IsMetaFile(absoluteMetadata))
		{
			TC_Core_Error("Refusing to write metadata outside Assets: {0}",
				PathToUTF8(metadataPath));
			return false;
		}
		if (EntryExists(metadataPath) && IsReparsePoint(metadataPath))
		{
			TC_Core_Error("Refusing to replace symbolic/reparse metadata file: {0}",
				PathToUTF8(metadataPath));
			return false;
		}

		YAML::Emitter output;
		output << YAML::BeginMap;
		output << YAML::Key << "SchemaVersion" << YAML::Value << kMetadataSchemaVersion;
		output << YAML::Key << "Asset" << YAML::Value << YAML::BeginMap;
		output << YAML::Key << "Handle" << YAML::Value <<
			static_cast<uint64_t>(metadata.Handle);
		output << YAML::Key << "Type" << YAML::Value << AssetTypeToString(metadata.Type);
		output << YAML::Key << "ImportSettings" << YAML::Value << YAML::BeginMap;
		for (const auto& [key, value] : metadata.ImportSettings)
			output << YAML::Key << key << YAML::Value << value;
		output << YAML::EndMap;
		output << YAML::EndMap;
		if (transaction && transaction->Type != MetadataTransactionType::None)
		{
			output << YAML::Key << "Transaction" << YAML::Value << YAML::BeginMap;
			output << YAML::Key << "Operation" << YAML::Value <<
				(transaction->Type == MetadataTransactionType::Move ? "Move" : "Delete");
			output << YAML::Key << "Source" << YAML::Value << PathToUTF8(transaction->Source);
			output << YAML::Key << "OriginalSize" << YAML::Value << transaction->OriginalSize;
			output << YAML::Key << "OriginalHash" << YAML::Value << transaction->OriginalHash;
			if (transaction->Type == MetadataTransactionType::Move)
				output << YAML::Key << "Destination" << YAML::Value <<
					PathToUTF8(transaction->Destination);
			else
				output << YAML::Key << "Staging" << YAML::Value <<
					PathToUTF8(transaction->Staging);
			output << YAML::EndMap;
		}
		output << YAML::EndMap;
		if (!output.good())
		{
			TC_Core_Error("Could not serialize asset metadata '{0}': {1}",
				PathToUTF8(metadataPath), output.GetLastError());
			return false;
		}

		std::string writeError;
		if (replaceExisting)
		{
			if (!FileSystem::WriteFileAtomically(metadataPath, output.c_str(), writeError))
			{
				TC_Core_Error("Could not atomically write asset metadata '{0}': {1}",
					PathToUTF8(metadataPath), writeError);
				return false;
			}
			return true;
		}

		// Orphan migration and first import must never replace an authoritative
		// sidecar that appeared after the registry scan. Publish a fully written
		// sibling through an atomic create-only hard link.
		const std::filesystem::path temporary =
			FileSystem::MakeTemporarySiblingPath(metadataPath);
		if (temporary.empty() ||
			!FileSystem::WriteFileAtomically(temporary, output.c_str(), writeError))
		{
			TC_Core_Error("Could not stage new asset metadata '{0}': {1}",
				PathToUTF8(metadataPath), writeError);
			return false;
		}

		std::error_code publishError;
#ifdef TC_PLATFORM_WINDOWS
		if (MoveFileExW(temporary.c_str(), metadataPath.c_str(),
			MOVEFILE_WRITE_THROUGH) == FALSE)
			publishError = std::error_code(static_cast<int>(GetLastError()),
				std::system_category());
#else
		std::filesystem::create_hard_link(temporary, metadataPath, publishError);
#endif
		if (publishError)
		{
			std::error_code cleanupError;
			std::filesystem::remove(temporary, cleanupError);
			TC_Core_Error("Refusing to replace existing or unavailable asset metadata '{0}': {1}",
				PathToUTF8(metadataPath), publishError.message());
			return false;
		}

#ifndef TC_PLATFORM_WINDOWS
		std::error_code cleanupError;
		std::filesystem::remove(temporary, cleanupError);
		if (cleanupError)
			TC_Core_Warn("New metadata was committed, but its staging link remains: {0}",
				PathToUTF8(temporary));
#endif
		return true;
	}

	bool AssetRegistry::RecoverInterruptedTransaction(
		const std::filesystem::path& metadataPath, const AssetMetadata& metadata,
		const MetadataTransaction& transaction) const
	{
		if (transaction.Type == MetadataTransactionType::None ||
			static_cast<uint64_t>(metadata.Handle) == 0 || !transaction.HasFingerprint)
			return false;

		std::filesystem::path absoluteMetadata, relativeMetadata;
		std::filesystem::path absoluteSource, relativeSource;
		if (!NormalizeManagedPath(metadataPath, absoluteMetadata, relativeMetadata,
			false, false) ||
			!NormalizeManagedPath(transaction.Source, absoluteSource, relativeSource,
			true, false) || IsMetaFile(absoluteSource))
			return false;

		const std::filesystem::path sourceMetadata = GetMetadataPath(absoluteSource);
		const bool metadataAtSource =
			PathKey(absoluteMetadata) == PathKey(sourceMetadata);
		if (transaction.Type == MetadataTransactionType::Delete)
		{
			if (!metadataAtSource)
				return false;

			if (transaction.Staging.empty() || transaction.Staging.is_absolute() ||
				transaction.Staging.has_root_name() || transaction.Staging.has_root_directory())
				return false;
			const std::filesystem::path absoluteStaging = AbsoluteLexicalPath(
				m_LibraryDirectory / transaction.Staging);
			std::filesystem::path relativeStaging;
			if (!MakeRelativeWithin(m_LibraryDirectory, absoluteStaging, relativeStaging))
				return false;
			for (const auto& component : relativeStaging)
			{
				if (component == "..")
					return false;
			}
			const std::filesystem::path stagedAsset = absoluteStaging / "asset";
			std::error_code error;
			const std::filesystem::file_status sourceStatus =
				std::filesystem::symlink_status(absoluteSource, error);
			const bool sourceExists = !error && std::filesystem::exists(sourceStatus);
			if (error && !IsNoSuchFileError(error))
				return false;
			if (sourceExists && (!std::filesystem::is_regular_file(sourceStatus) ||
				IsReparsePoint(absoluteSource)))
				return false;

			error.clear();
			const std::filesystem::file_status stagedStatus =
				std::filesystem::symlink_status(stagedAsset, error);
			const bool stagedExists = !error && std::filesystem::exists(stagedStatus);
			if (error && !IsNoSuchFileError(error))
				return false;
			if (stagedExists && (!std::filesystem::is_regular_file(stagedStatus) ||
				IsReparsePoint(absoluteStaging) || IsReparsePoint(stagedAsset)))
				return false;

			bool sourceMatches = false;
			if (sourceExists)
			{
				uint64_t size = 0;
				uint64_t hash = 0;
				if (!ComputeAssetFingerprint(absoluteSource, size, hash))
					return false;
				sourceMatches = size == transaction.OriginalSize &&
					hash == transaction.OriginalHash;
			}

			if (!stagedExists && sourceMatches)
			{
				// The original data never left Assets: abort the interrupted delete.
				return WriteMetadata(sourceMetadata, metadata, true, nullptr);
			}

			// A staged original proves deletion crossed its commit point. If a
			// different file has appeared at Source, leave it untouched and remove
			// only the old UUID sidecar so it receives a new identity on rescan.
			error.clear();
			return !IsReparsePoint(sourceMetadata) &&
				std::filesystem::remove(sourceMetadata, error) && !error;
		}

		std::filesystem::path absoluteDestination, relativeDestination;
		if (!NormalizeManagedPath(transaction.Destination, absoluteDestination,
			relativeDestination, true, false) || IsMetaFile(absoluteDestination))
			return false;
		const std::filesystem::path destinationMetadata =
			GetMetadataPath(absoluteDestination);
		const bool metadataAtDestination =
			PathKey(absoluteMetadata) == PathKey(destinationMetadata);
		if (!metadataAtSource && !metadataAtDestination)
			return false;

		auto inspectAsset = [](const std::filesystem::path& path, bool& exists)
		{
			exists = false;
			std::error_code error;
			const std::filesystem::file_status status =
				std::filesystem::symlink_status(path, error);
			if (error)
				return IsNoSuchFileError(error);
			if (!std::filesystem::exists(status))
				return true;
			if (!std::filesystem::is_regular_file(status) || IsReparsePoint(path))
				return false;
			exists = true;
			return true;
		};

		bool sourceExists = false;
		bool destinationExists = false;
		if (!inspectAsset(absoluteSource, sourceExists) ||
			!inspectAsset(absoluteDestination, destinationExists))
			return false;
		const bool sameAsset = sourceExists && destinationExists &&
			PathsReferToSameEntry(absoluteSource, absoluteDestination);
		auto matchesOriginal = [&transaction](const std::filesystem::path& path, bool exists,
			bool& matches)
		{
			matches = false;
			if (!exists)
				return true;
			uint64_t size = 0;
			uint64_t hash = 0;
			if (!ComputeAssetFingerprint(path, size, hash))
				return false;
			matches = size == transaction.OriginalSize && hash == transaction.OriginalHash;
			return true;
		};
		bool sourceMatches = false;
		bool destinationMatches = false;
		if (!matchesOriginal(absoluteSource, sourceExists, sourceMatches) ||
			!matchesOriginal(absoluteDestination, destinationExists, destinationMatches))
			return false;

		if (sameAsset && sourceMatches && destinationMatches)
		{
			std::string renameError;
			if (!RenameEntry(absoluteSource, absoluteDestination, renameError))
				return false;
			sourceMatches = false;
			destinationMatches = true;
		}
		if (sourceMatches == destinationMatches)
		{
			// Neither endpoint existing is not evidence that the move committed. Keep the
			// journal quarantined instead of guessing an owner for the old UUID.
			TC_Core_Error("Cannot identify the original asset while recovering move '{0}' -> '{1}'",
				PathToUTF8(relativeSource), PathToUTF8(relativeDestination));
			return false;
		}

		// Only the sidecar is reconciled; replacement files at the other endpoint
		// remain untouched and will receive their own UUID during the rescan.
		const bool commitMove = destinationMatches;
		const std::filesystem::path targetMetadata = commitMove
			? destinationMetadata : sourceMetadata;
		if (PathKey(absoluteMetadata) != PathKey(targetMetadata))
		{
			if (EntryExists(targetMetadata))
				return false;
			std::string renameError;
			if (!RenameEntry(absoluteMetadata, targetMetadata, renameError))
			{
				TC_Core_Error("Could not recover interrupted asset move metadata: {0}", renameError);
				return false;
			}
		}
		return WriteMetadata(targetMetadata, metadata, true, nullptr);
	}

	AssetHandle AssetRegistry::GenerateUniqueHandle(
		const std::unordered_map<AssetHandle, std::filesystem::path>& claimed) const
	{
		AssetHandle handle;
		while (static_cast<uint64_t>(handle) == 0 ||
			m_Assets.find(handle) != m_Assets.end() || claimed.find(handle) != claimed.end())
			handle = AssetHandle();
		return handle;
	}

	bool AssetRegistry::Refresh()
	{
		if (!m_Initialized)
			return false;

		for (auto& [handle, metadata] : m_Assets)
			metadata.IsMissing = true;

		struct ScanRecord
		{
			std::filesystem::path SourcePath;
			std::filesystem::path MetadataPath;
			std::filesystem::path RelativePath;
			AssetMetadata ParsedMetadata;
			MetadataTransaction Transaction;
			bool HasSource = false;
			bool HasMetadata = false;
			bool AmbiguousMetadata = false;
			bool MetadataParsed = false;
			bool MetadataDamaged = false;
			bool TransactionFailed = false;
		};

		std::map<std::string, ScanRecord> records;
		bool complete = true;
		std::error_code error;
		std::filesystem::recursive_directory_iterator iterator(m_AssetDirectory,
			std::filesystem::directory_options::none, error), end;
		if (error)
		{
			TC_Core_Error("Could not scan asset directory '{0}': {1}",
				PathToUTF8(m_AssetDirectory), error.message());
			return false;
		}

		while (iterator != end)
		{
			const std::filesystem::path entryPath = iterator->path();
			if (IsReparsePoint(entryPath))
			{
				TC_Core_Error("Skipping symbolic/reparse entry in Assets: {0}", PathToUTF8(entryPath));
				complete = false;
				std::error_code directoryError;
				if (iterator->is_directory(directoryError) && iterator.recursion_pending())
					iterator.disable_recursion_pending();
				iterator.increment(error);
				if (error)
					break;
				continue;
			}

			std::error_code statusError;
			const std::filesystem::file_status status =
				std::filesystem::symlink_status(entryPath, statusError);
			if (statusError)
			{
				TC_Core_Error("Could not inspect asset entry '{0}': {1}",
					PathToUTF8(entryPath), statusError.message());
				complete = false;
			}
			else if (std::filesystem::is_regular_file(status) &&
				!IsMetadataTemporaryFile(entryPath))
			{
				std::filesystem::path absoluteEntry, relativeEntry;
				if (!NormalizeManagedPath(entryPath, absoluteEntry, relativeEntry, false, false))
				{
					TC_Core_Error("Asset entry escapes the managed root: {0}", PathToUTF8(entryPath));
					complete = false;
				}
				else if (IsMetaFile(entryPath))
				{
					const std::filesystem::path sourcePath = SourcePathFromMetadata(entryPath);
					if (!sourcePath.filename().empty() && !IsMetaFile(sourcePath))
					{
						std::filesystem::path absoluteSource, relativeSource;
						if (NormalizeManagedPath(sourcePath, absoluteSource, relativeSource, true, false))
						{
							ScanRecord& record = records[PathKey(relativeSource)];
							if (record.HasMetadata && record.MetadataPath != absoluteEntry)
								record.AmbiguousMetadata = true;
							else
							{
								record.MetadataPath = absoluteEntry;
								record.HasMetadata = true;
								record.RelativePath = relativeSource;
								if (!record.HasSource)
									record.SourcePath = absoluteSource;
							}
						}
					}
				}
				else
				{
					ScanRecord& record = records[PathKey(relativeEntry)];
					record.SourcePath = absoluteEntry;
					record.RelativePath = relativeEntry;
					record.HasSource = true;
				}
			}

			iterator.increment(error);
			if (error)
				break;
		}
		if (error)
		{
			TC_Core_Error("Asset directory scan stopped early: {0}", error.message());
			complete = false;
		}

		std::unordered_map<AssetHandle, std::filesystem::path> declaredClaims;
		std::unordered_set<AssetHandle> conflictingHandles;
		for (auto& [key, record] : records)
		{
			if (!record.HasMetadata || record.AmbiguousMetadata)
				continue;
			record.MetadataParsed = ReadMetadata(record.MetadataPath,
				record.ParsedMetadata, &record.Transaction);
			record.MetadataDamaged = !record.MetadataParsed;
			if (!record.MetadataParsed)
			{
				complete = false;
				continue;
			}
			const auto [owner, inserted] = declaredClaims.emplace(
				record.ParsedMetadata.Handle, record.RelativePath);
			if (!inserted)
			{
				conflictingHandles.emplace(record.ParsedMetadata.Handle);
				TC_Core_Error("Duplicate asset handle {0} in '{1}' and '{2}'; resolve the .tcmeta conflict explicitly",
					static_cast<uint64_t>(record.ParsedMetadata.Handle), PathToUTF8(owner->second),
					PathToUTF8(record.RelativePath));
			}
		}

		bool recoveredTransaction = false;
		for (auto& [key, record] : records)
		{
			if (!record.MetadataParsed ||
				record.Transaction.Type == MetadataTransactionType::None)
				continue;
			if (RecoverInterruptedTransaction(record.MetadataPath,
				record.ParsedMetadata, record.Transaction))
				recoveredTransaction = true;
			else
			{
				record.TransactionFailed = true;
				complete = false;
				TC_Core_Error("Could not recover interrupted asset transaction in '{0}'",
					PathToUTF8(record.MetadataPath));
			}
		}
		if (recoveredTransaction)
			return Refresh() && complete;

		for (AssetHandle handle : conflictingHandles)
		{
			m_Assets.erase(handle);
			complete = false;
		}

		std::unordered_map<AssetHandle, std::filesystem::path> claimed;
		for (auto& [key, record] : records)
		{
			if (record.AmbiguousMetadata || record.TransactionFailed)
			{
				if (record.TransactionFailed)
					TC_Core_Error("Asset transaction is quarantined at '{0}'",
						PathToUTF8(record.RelativePath));
				else
					TC_Core_Error("Multiple .tcmeta files map to asset path '{0}'",
						PathToUTF8(record.RelativePath));
				complete = false;
				continue;
			}
			if (!record.HasSource && !record.HasMetadata)
				continue;

			AssetMetadata metadata;
			bool metadataNeedsWrite = false;
			bool metadataMustBeNew = false;
			if (record.HasMetadata)
			{
				if (record.MetadataDamaged || !record.MetadataParsed)
				{
					complete = false;
					continue;
				}
				metadata = record.ParsedMetadata;
				if (conflictingHandles.find(metadata.Handle) != conflictingHandles.end())
					continue;
			}
			else
			{
				metadata.Handle = GenerateUniqueHandle(claimed);
				metadata.Type = AssetTypeFromPath(record.SourcePath);
				record.MetadataPath = GetMetadataPath(record.SourcePath);
				metadataNeedsWrite = true;
				metadataMustBeNew = true;
			}

			metadata.FilePath = record.RelativePath.lexically_normal();
			metadata.IsMissing = !record.HasSource;
			const AssetType detectedType = AssetTypeFromPath(record.SourcePath);
			if (metadata.Type != detectedType)
			{
				metadata.Type = detectedType;
				metadataNeedsWrite = true;
			}

			const auto duplicate = claimed.find(metadata.Handle);
			if (duplicate != claimed.end())
			{
				// A merge conflict cannot be repaired by guessing which file owns old
				// references. Quarantine every claimant so the handle resolves missing,
				// never silently to the wrong resource; leave both sidecars untouched.
				TC_Core_Error("Duplicate asset handle {0} in '{1}' and '{2}'; resolve the .tcmeta conflict explicitly",
					static_cast<uint64_t>(metadata.Handle), PathToUTF8(duplicate->second),
					PathToUTF8(metadata.FilePath));
				m_Assets.erase(metadata.Handle);
				complete = false;
				continue;
			}

			if (metadataNeedsWrite &&
				!WriteMetadata(record.MetadataPath, metadata, !metadataMustBeNew))
			{
				complete = false;
				continue;
			}
			claimed.emplace(metadata.Handle, metadata.FilePath);
			m_Assets.insert_or_assign(metadata.Handle, std::move(metadata));
		}

		// Sidecars, not Library, are authoritative. Drop every cache-only record
		// that was not claimed by a valid sidecar in this scan; otherwise an
		// interrupted delete could later recreate an old UUID at the same path.
		for (auto iterator = m_Assets.begin(); iterator != m_Assets.end();)
		{
			if (claimed.find(iterator->first) == claimed.end())
				iterator = m_Assets.erase(iterator);
			else
				++iterator;
		}

		RebuildPathIndex();
		if (!SaveCache())
			TC_Core_Warn("Asset registry is usable, but its rebuildable Library cache was not saved");
		return complete;
	}

	void AssetRegistry::RebuildPathIndex()
	{
		m_PathIndex.clear();
		std::vector<const AssetMetadata*> ordered;
		ordered.reserve(m_Assets.size());
		for (const auto& [handle, metadata] : m_Assets)
			ordered.push_back(&metadata);
		std::sort(ordered.begin(), ordered.end(), [](const AssetMetadata* left,
			const AssetMetadata* right)
		{
			if (left->IsMissing != right->IsMissing)
				return left->IsMissing && !right->IsMissing;
			const std::string leftKey = PathKey(left->FilePath);
			const std::string rightKey = PathKey(right->FilePath);
			if (leftKey != rightKey)
				return leftKey < rightKey;
			return static_cast<uint64_t>(left->Handle) < static_cast<uint64_t>(right->Handle);
		});
		for (const AssetMetadata* metadata : ordered)
			m_PathIndex.insert_or_assign(PathKey(metadata->FilePath), metadata->Handle);
	}

	AssetHandle AssetRegistry::ImportAsset(const std::filesystem::path& path)
	{
		std::filesystem::path absolutePath, relativePath;
		if (!NormalizeManagedPath(path, absolutePath, relativePath, true, false) ||
			IsMetaFile(absolutePath) || IsMetadataTemporaryFile(absolutePath))
		{
			TC_Core_Error("Cannot import path outside Assets, a symbolic link, or metadata: {0}",
				PathToUTF8(path));
			return AssetHandle(0);
		}

		const AssetMetadata* existing = GetMetadata(relativePath);
		std::error_code error;
		const std::filesystem::file_status status =
			std::filesystem::symlink_status(absolutePath, error);
		const bool hasSource = !error && std::filesystem::is_regular_file(status);
		const std::filesystem::path metadataPath = GetMetadataPath(absolutePath);

		if (hasSource)
		{
			if (existing && !existing->IsMissing && EntryExists(metadataPath) &&
				!IsReparsePoint(metadataPath))
			{
				error.clear();
				const std::filesystem::file_status metadataStatus =
					std::filesystem::symlink_status(metadataPath, error);
				AssetMetadata diskMetadata;
				if (!error && std::filesystem::is_regular_file(metadataStatus) &&
					ReadMetadata(metadataPath, diskMetadata) &&
					diskMetadata.Handle == existing->Handle &&
					diskMetadata.Type == existing->Type &&
					diskMetadata.Type == AssetTypeFromPath(absolutePath) &&
					diskMetadata.ImportSettings == existing->ImportSettings)
					return existing->Handle;
			}

			// A live file without an authoritative sidecar is a new identity. Refresh
			// creates/loads that sidecar instead of reviving a stale Library tombstone.
			(void)Refresh();
			existing = GetMetadata(relativePath);
			return existing && !existing->IsMissing ? existing->Handle : AssetHandle(0);
		}

		if ((error && !IsNoSuchFileError(error)) ||
			(!error && std::filesystem::exists(status)))
			return AssetHandle(0);

		// A missing source has an identity only when a valid committed sidecar
		// already owns it. Library cache entries are rebuildable and never create
		// orphan metadata for a path that has no source.
		if (!EntryExists(metadataPath) || IsReparsePoint(metadataPath))
			return AssetHandle(0);
		(void)Refresh();
		existing = GetMetadata(relativePath);
		if (!existing || !existing->IsMissing)
			return AssetHandle(0);
		AssetMetadata diskMetadata;
		MetadataTransaction transaction;
		if (!ReadMetadata(metadataPath, diskMetadata, &transaction) ||
			transaction.Type != MetadataTransactionType::None ||
			diskMetadata.Handle != existing->Handle ||
			diskMetadata.Type != existing->Type ||
			diskMetadata.ImportSettings != existing->ImportSettings)
			return AssetHandle(0);
		return existing->Handle;
	}

	bool AssetRegistry::IsManagedPath(const std::filesystem::path& path,
		bool allowMissingLeaf) const
	{
		std::filesystem::path absolutePath, relativePath;
		return NormalizeManagedPath(path, absolutePath, relativePath, allowMissingLeaf, false) &&
			!IsMetaFile(absolutePath) && !IsMetadataTemporaryFile(absolutePath);
	}

	const AssetMetadata* AssetRegistry::GetMetadata(AssetHandle handle) const
	{
		if (static_cast<uint64_t>(handle) == 0)
			return nullptr;
		const auto iterator = m_Assets.find(handle);
		return iterator == m_Assets.end() ? nullptr : &iterator->second;
	}

	const AssetMetadata* AssetRegistry::GetMetadata(const std::filesystem::path& path) const
	{
		std::filesystem::path absolutePath, relativePath;
		if (!NormalizeManagedPath(path, absolutePath, relativePath, true, true))
			return nullptr;
		const auto iterator = m_PathIndex.find(PathKey(relativePath));
		return iterator == m_PathIndex.end() ? nullptr : GetMetadata(iterator->second);
	}

	std::filesystem::path AssetRegistry::GetFileSystemPath(AssetHandle handle) const
	{
		const AssetMetadata* metadata = GetMetadata(handle);
		return metadata ? (m_AssetDirectory / metadata->FilePath).lexically_normal()
			: std::filesystem::path{};
	}

	std::vector<AssetHandle> AssetRegistry::GetHandlesUnderPath(
		const std::filesystem::path& path) const
	{
		std::filesystem::path absolutePath, relativePath;
		if (!NormalizeManagedPath(path, absolutePath, relativePath, true, true))
			return {};

		std::vector<const AssetMetadata*> matches;
		for (const auto& [handle, metadata] : m_Assets)
		{
			if (IsAtOrBelow(relativePath, metadata.FilePath))
				matches.push_back(&metadata);
		}
		std::sort(matches.begin(), matches.end(), [](const AssetMetadata* left,
			const AssetMetadata* right)
		{
			const std::string leftKey = PathKey(left->FilePath);
			const std::string rightKey = PathKey(right->FilePath);
			if (leftKey != rightKey)
				return leftKey < rightKey;
			return static_cast<uint64_t>(left->Handle) < static_cast<uint64_t>(right->Handle);
		});

		std::vector<AssetHandle> handles;
		handles.reserve(matches.size());
		for (const AssetMetadata* metadata : matches)
			handles.push_back(metadata->Handle);
		return handles;
	}

	bool AssetRegistry::MoveAsset(const std::filesystem::path& source,
		const std::filesystem::path& destination, AssetHandle expectedHandle)
	{
		std::filesystem::path absoluteSource, relativeSource;
		std::filesystem::path absoluteDestination, relativeDestination;
		if (!NormalizeManagedPath(source, absoluteSource, relativeSource, false, false) ||
			!NormalizeManagedPath(destination, absoluteDestination, relativeDestination, true, false) ||
			IsMetaFile(absoluteSource) || IsMetaFile(absoluteDestination))
		{
			TC_Core_Error("Invalid asset move from '{0}' to '{1}'",
				PathToUTF8(source), PathToUTF8(destination));
			return false;
		}

		std::filesystem::path parentAbsolute, parentRelative;
		if (!NormalizeManagedPath(absoluteDestination.parent_path(), parentAbsolute,
			parentRelative, false, true))
			return false;
		std::error_code error;
		if (!std::filesystem::is_directory(parentAbsolute, error) || error)
			return false;

		const bool sameEntry = PathsReferToSameEntry(absoluteSource, absoluteDestination);
		if (EntryExists(absoluteDestination) && !sameEntry)
		{
			TC_Core_Error("Asset move destination already exists: {0}",
				PathToUTF8(absoluteDestination));
			return false;
		}
		if (absoluteSource == absoluteDestination)
			return true;

		const std::filesystem::file_status sourceStatus =
			std::filesystem::symlink_status(absoluteSource, error);
		if (error || (!std::filesystem::is_regular_file(sourceStatus) &&
			!std::filesystem::is_directory(sourceStatus)) || !ValidateManagedTree(absoluteSource))
			return false;
		const bool isDirectory = std::filesystem::is_directory(sourceStatus);
		if (isDirectory && !sameEntry && IsAtOrBelow(relativeSource, relativeDestination))
		{
			TC_Core_Error("A directory cannot be moved into itself: {0}", PathToUTF8(source));
			return false;
		}

		if (!Refresh())
		{
			TC_Core_Error("Asset move refused because the registry refresh was incomplete");
			return false;
		}
		if (static_cast<uint64_t>(expectedHandle) != 0)
		{
			if (isDirectory)
				return false;
			const AssetMetadata* expected = GetMetadata(relativeSource);
			AssetMetadata diskMetadata;
			if (!expected || expected->IsMissing || expected->Handle != expectedHandle ||
				!ReadMetadata(GetMetadataPath(absoluteSource), diskMetadata) ||
				diskMetadata.Handle != expectedHandle)
			{
				TC_Core_Error("Asset move identity changed before the operation: expected {0} at '{1}'",
					static_cast<uint64_t>(expectedHandle), PathToUTF8(relativeSource));
				return false;
			}
		}
		AssetMetadata registeredFileMetadata;
		if (!isDirectory)
		{
			const AssetMetadata* metadata = GetMetadata(relativeSource);
			if (!metadata || metadata->IsMissing)
			{
				TC_Core_Error("Asset is not registered and cannot be moved safely: {0}",
					PathToUTF8(relativeSource));
				return false;
			}
			registeredFileMetadata = *metadata;
		}
		else
		{
			std::filesystem::recursive_directory_iterator check(absoluteSource,
				std::filesystem::directory_options::none, error), checkEnd;
			while (!error && check != checkEnd)
			{
				const std::filesystem::path entryPath = check->path();
				const std::filesystem::file_status entryStatus =
					std::filesystem::symlink_status(entryPath, error);
				if (error)
					break;
				if (std::filesystem::is_regular_file(entryStatus) && !IsMetaFile(entryPath) &&
					!IsMetadataTemporaryFile(entryPath))
				{
					const AssetMetadata* metadata = GetMetadata(entryPath);
					if (!metadata || metadata->IsMissing)
					{
						TC_Core_Error("Directory contains an unregistered asset and cannot be moved: {0}",
							PathToUTF8(entryPath));
						return false;
					}
				}
				check.increment(error);
			}
			if (error)
				return false;
		}

		std::string renameError;
		if (isDirectory)
		{
			if (!RenameEntry(absoluteSource, absoluteDestination, renameError))
			{
				TC_Core_Error("Could not move asset directory '{0}': {1}",
					PathToUTF8(absoluteSource), renameError);
				return false;
			}
		}
		else
		{
			const std::filesystem::path sourceMetadata = GetMetadataPath(absoluteSource);
			const std::filesystem::path destinationMetadata = GetMetadataPath(absoluteDestination);
			if (!EntryExists(sourceMetadata) || IsReparsePoint(sourceMetadata) ||
				(EntryExists(destinationMetadata) &&
					!PathsReferToSameEntry(sourceMetadata, destinationMetadata)))
			{
				TC_Core_Error("Asset metadata cannot be moved safely from '{0}' to '{1}'",
					PathToUTF8(sourceMetadata), PathToUTF8(destinationMetadata));
				return false;
			}
			MetadataTransaction transaction;
			transaction.Type = MetadataTransactionType::Move;
			transaction.Source = relativeSource;
			transaction.Destination = relativeDestination;
			if (!ComputeAssetFingerprint(absoluteSource, transaction.OriginalSize,
				transaction.OriginalHash))
			{
				TC_Core_Error("Could not fingerprint asset before move: {0}",
					PathToUTF8(relativeSource));
				return false;
			}
			transaction.HasFingerprint = true;

			// Fingerprinting can take seconds for a large asset. Re-read the sidecar
			// afterwards so an external replacement during that work is never
			// overwritten with the registry's stale identity/settings.
			AssetMetadata diskMetadata;
			MetadataTransaction existingTransaction;
			if (!ReadMetadata(sourceMetadata, diskMetadata, &existingTransaction) ||
				diskMetadata.Handle != registeredFileMetadata.Handle ||
				(static_cast<uint64_t>(expectedHandle) != 0 &&
					diskMetadata.Handle != expectedHandle) ||
				existingTransaction.Type != MetadataTransactionType::None)
			{
				TC_Core_Error("Asset move identity changed before metadata was moved: expected {0}",
					static_cast<uint64_t>(registeredFileMetadata.Handle));
				return false;
			}
			if (!WriteMetadata(sourceMetadata, diskMetadata, true, &transaction))
			{
				TC_Core_Error("Could not journal asset move for '{0}'",
					PathToUTF8(relativeSource));
				return false;
			}

			if (!RenameEntry(absoluteSource, absoluteDestination, renameError))
			{
				if (!WriteMetadata(sourceMetadata, diskMetadata, true, nullptr))
					TC_Core_Error("Could not clear the failed asset-move journal: {0}",
						PathToUTF8(sourceMetadata));
				TC_Core_Error("Could not move asset '{0}': {1}",
					PathToUTF8(absoluteSource), renameError);
				return false;
			}
			if (!RenameEntry(sourceMetadata, destinationMetadata, renameError))
			{
				std::string rollbackError;
				const bool rolledBack = RenameEntry(absoluteDestination,
					absoluteSource, rollbackError);
				if (rolledBack)
					(void)WriteMetadata(sourceMetadata, diskMetadata, true, nullptr);
				TC_Core_Error("Could not move asset metadata '{0}': {1}{2}",
					PathToUTF8(sourceMetadata), renameError,
					rolledBack ? "" : " (asset rollback failed; journal retained for recovery)");
				return false;
			}
			if (!WriteMetadata(destinationMetadata, diskMetadata, true, nullptr))
				TC_Core_Warn("Asset move completed with a recovery journal still present: {0}",
					PathToUTF8(destinationMetadata));
		}

		if (!Refresh())
			TC_Core_Warn("Asset move completed, but the registry refresh was incomplete");
		return true;
	}

	std::vector<AssetReference> AssetRegistry::FindReferencesInternal(
		const std::vector<AssetHandle>& handles,
		const std::vector<std::filesystem::path>& ignoredSceneRoots,
		bool& complete) const
	{
		complete = true;
		std::unordered_set<uint64_t> targets;
		for (AssetHandle handle : handles)
		{
			if (static_cast<uint64_t>(handle) != 0)
				targets.insert(static_cast<uint64_t>(handle));
		}
		if (targets.empty() || !m_Initialized)
			return {};

		std::vector<AssetReference> references;
		std::error_code error;
		std::filesystem::recursive_directory_iterator iterator(m_AssetDirectory,
			std::filesystem::directory_options::none, error), end;
		if (error)
		{
			complete = false;
			return references;
		}
		while (iterator != end)
		{
			const std::filesystem::path scenePath = iterator->path();
			if (IsReparsePoint(scenePath))
			{
				complete = false;
				std::error_code directoryError;
				if (iterator->is_directory(directoryError) && iterator.recursion_pending())
					iterator.disable_recursion_pending();
				iterator.increment(error);
				if (error)
					break;
				continue;
			}

			std::error_code statusError;
			const std::filesystem::file_status status =
				std::filesystem::symlink_status(scenePath, statusError);
			if (statusError)
				complete = false;
			else if (std::filesystem::is_regular_file(status) &&
				AssetTypeFromPath(scenePath) == AssetType::Scene)
			{
				std::filesystem::path absoluteScene, relativeScene;
				if (!NormalizeManagedPath(scenePath, absoluteScene, relativeScene, false, false))
					complete = false;
				else
				{
					bool ignored = false;
					for (const auto& ignoredRoot : ignoredSceneRoots)
					{
						if (IsAtOrBelow(ignoredRoot, relativeScene))
						{
							ignored = true;
							break;
						}
					}
					if (!ignored)
					{
						try
						{
							std::vector<uint8_t> sceneBytes;
							if (!ReadWholeFile(absoluteScene, sceneBytes))
								throw std::runtime_error("could not read the scene");
							if (!SceneSerializer::ValidateCurrentFormat(sceneBytes, absoluteScene))
								throw std::runtime_error(
									"scene does not conform to the complete current scene schema");
							std::string serialized(sceneBytes.begin(), sceneBytes.end());
							std::istringstream input(std::move(serialized));
							const YAML::Node root = YAML::Load(input);
							const AssetMetadata* sceneMetadata = GetMetadata(relativeScene);
							const AssetHandle referencing = sceneMetadata && !sceneMetadata->IsMissing
								? sceneMetadata->Handle : AssetHandle(0);
							CollectHandleReferences(root, "$", targets, referencing, relativeScene,
								references, complete);
						}
						catch (const std::exception& exception)
						{
							TC_Core_Error("Could not inspect asset references in scene '{0}': {1}",
								PathToUTF8(relativeScene), exception.what());
							complete = false;
						}
					}
				}
			}

			iterator.increment(error);
			if (error)
				break;
		}
		if (error)
			complete = false;

		std::sort(references.begin(), references.end(), [](const AssetReference& left,
			const AssetReference& right)
		{
			const std::string leftPath = PathKey(left.FilePath);
			const std::string rightPath = PathKey(right.FilePath);
			if (leftPath != rightPath)
				return leftPath < rightPath;
			if (left.PropertyPath != right.PropertyPath)
				return left.PropertyPath < right.PropertyPath;
			return static_cast<uint64_t>(left.ReferencedAsset) <
				static_cast<uint64_t>(right.ReferencedAsset);
		});
		return references;
	}

	std::vector<AssetReference> AssetRegistry::FindReferences(AssetHandle handle) const
	{
		bool complete = true;
		std::vector<AssetReference> references = FindReferencesInternal({ handle }, {}, complete);
		if (!complete)
			TC_Core_Warn("Asset reference scan was incomplete");
		return references;
	}

	bool AssetRegistry::DeleteAsset(const std::filesystem::path& path, bool force,
		std::vector<AssetReference>& references, AssetHandle expectedHandle)
	{
		references.clear();
		std::filesystem::path absolutePath, relativePath;
		if (!NormalizeManagedPath(path, absolutePath, relativePath, false, false) ||
			IsMetaFile(absolutePath) || !ValidateManagedTree(absolutePath))
		{
			TC_Core_Error("Invalid or unsafe asset delete path: {0}", PathToUTF8(path));
			return false;
		}

		const bool refreshComplete = Refresh();
		if (!refreshComplete && (!force || static_cast<uint64_t>(expectedHandle) != 0))
		{
			TC_Core_Error("Asset delete refused because the registry refresh was incomplete");
			return false;
		}
		std::error_code error;
		const std::filesystem::file_status status =
			std::filesystem::symlink_status(absolutePath, error);
		if (error || (!std::filesystem::is_regular_file(status) &&
			!std::filesystem::is_directory(status)))
			return false;
		const bool isDirectory = std::filesystem::is_directory(status);
		AssetMetadata registeredFileMetadata;
		bool hasRegisteredFileMetadata = false;
		if (!isDirectory)
		{
			const AssetMetadata* current = GetMetadata(relativePath);
			if (current && !current->IsMissing)
			{
				registeredFileMetadata = *current;
				hasRegisteredFileMetadata = true;
			}
		}
		if (static_cast<uint64_t>(expectedHandle) != 0)
		{
			AssetMetadata diskMetadata;
			if (isDirectory || !hasRegisteredFileMetadata ||
				registeredFileMetadata.Handle != expectedHandle ||
				!ReadMetadata(GetMetadataPath(absolutePath), diskMetadata) ||
				diskMetadata.Handle != expectedHandle)
			{
				TC_Core_Error("Asset delete identity changed before the operation: expected {0} at '{1}'",
					static_cast<uint64_t>(expectedHandle), PathToUTF8(relativePath));
				return false;
			}
		}

		if (!force)
		{
			if (!isDirectory)
			{
				if (!hasRegisteredFileMetadata)
				{
					TC_Core_Error("Unregistered asset requires a forced delete: {0}",
						PathToUTF8(relativePath));
					return false;
				}
			}
			else
			{
				std::filesystem::recursive_directory_iterator check(absolutePath,
					std::filesystem::directory_options::none, error), checkEnd;
				while (!error && check != checkEnd)
				{
					const std::filesystem::path entryPath = check->path();
					const std::filesystem::file_status entryStatus =
						std::filesystem::symlink_status(entryPath, error);
					if (error)
						break;
					if (std::filesystem::is_regular_file(entryStatus) && !IsMetaFile(entryPath) &&
						!IsMetadataTemporaryFile(entryPath))
					{
						const AssetMetadata* metadata = GetMetadata(entryPath);
						if (!metadata || metadata->IsMissing)
						{
							TC_Core_Error("Directory contains an unregistered asset; force is required: {0}",
								PathToUTF8(entryPath));
							return false;
						}
					}
					check.increment(error);
				}
				if (error)
					return false;
			}
		}

		std::vector<AssetHandle> affectedHandles;
		for (AssetHandle handle : GetHandlesUnderPath(relativePath))
		{
			const AssetMetadata* metadata = GetMetadata(handle);
			if (!metadata)
				continue;
			const std::filesystem::path sourcePath = GetFileSystemPath(handle);
			if (!metadata->IsMissing || EntryExists(GetMetadataPath(sourcePath)))
				affectedHandles.push_back(handle);
		}

		bool referenceScanComplete = true;
		references = FindReferencesInternal(affectedHandles, { relativePath }, referenceScanComplete);
		if (!force && (!referenceScanComplete || !references.empty()))
		{
			if (!referenceScanComplete)
				TC_Core_Error("Delete refused because the asset reference scan was incomplete");
			return false;
		}

		std::filesystem::path stagingDirectory;
		std::string stagingError;
		if (!CreateDeleteStagingDirectory(m_LibraryDirectory, stagingDirectory, stagingError))
		{
			TC_Core_Error("Could not stage asset deletion safely: {0}", stagingError);
			return false;
		}

		const std::filesystem::path stagedAsset = stagingDirectory / "asset";
		const std::filesystem::path metadataPath = isDirectory
			? std::filesystem::path{} : GetMetadataPath(absolutePath);
		const bool hasMetadataFile = !isDirectory && EntryExists(metadataPath);
		AssetMetadata diskMetadata;
		bool journaledDelete = false;
		if (!isDirectory)
		{
			if (hasMetadataFile && IsReparsePoint(metadataPath))
			{
				RemoveStagingDirectoryBestEffort(stagingDirectory);
				return false;
			}
			if (hasRegisteredFileMetadata)
			{
				MetadataTransaction transaction;
				transaction.Type = MetadataTransactionType::Delete;
				transaction.Source = relativePath;
				transaction.Staging = stagingDirectory.lexically_relative(
					m_LibraryDirectory).lexically_normal();
				if (!ComputeAssetFingerprint(absolutePath, transaction.OriginalSize,
					transaction.OriginalHash))
				{
					RemoveStagingDirectoryBestEffort(stagingDirectory);
					return false;
				}
				transaction.HasFingerprint = true;

				// Do the final sidecar identity check after the potentially expensive
				// fingerprint, immediately before installing the recovery journal.
				MetadataTransaction existingTransaction;
				if (!hasMetadataFile || !ReadMetadata(metadataPath, diskMetadata,
					&existingTransaction) ||
					diskMetadata.Handle != registeredFileMetadata.Handle ||
					existingTransaction.Type != MetadataTransactionType::None)
				{
					TC_Core_Error("Asset delete identity changed before staging: expected {0}",
						static_cast<uint64_t>(registeredFileMetadata.Handle));
					RemoveStagingDirectoryBestEffort(stagingDirectory);
					return false;
				}
				if (!WriteMetadata(metadataPath, diskMetadata, true, &transaction))
				{
					RemoveStagingDirectoryBestEffort(stagingDirectory);
					return false;
				}
				journaledDelete = true;
			}
		}

		std::string renameError;
		if (!RenameEntry(absolutePath, stagedAsset, renameError))
		{
			if (journaledDelete)
				(void)WriteMetadata(metadataPath, diskMetadata, true, nullptr);
			RemoveStagingDirectoryBestEffort(stagingDirectory);
			TC_Core_Error("Could not stage asset deletion for '{0}': {1}",
				PathToUTF8(absolutePath), renameError);
			return false;
		}

		if (hasMetadataFile)
		{
			const std::filesystem::path stagedMetadata = stagingDirectory / "asset.tcmeta";
			if (!RenameEntry(metadataPath, stagedMetadata, renameError))
			{
				std::string rollbackError;
				const bool rolledBack = RenameEntry(stagedAsset, absolutePath, rollbackError);
				if (rolledBack && journaledDelete)
					(void)WriteMetadata(metadataPath, diskMetadata, true, nullptr);
				if (rolledBack)
				{
					RemoveStagingDirectoryBestEffort(stagingDirectory);
					TC_Core_Error("Could not stage asset metadata deletion '{0}': {1}",
						PathToUTF8(metadataPath), renameError);
					return false;
				}
				// The requested delete has taken effect and the source remains recoverable
				// in Library. Keep the journal so the next Refresh can discard the orphan.
				TC_Core_Error("Asset deletion completed, but rollback and metadata staging failed; recovery data is at '{0}'",
					PathToUTF8(stagingDirectory));
			}
		}

		// An explicit asset-system deletion removes the registry identity as well.
		// Refresh intentionally preserves tombstones for files that disappear behind
		// our back, but retaining one here could let an old handle alias a future file
		// created at the same path.
		for (AssetHandle handle : affectedHandles)
			m_Assets.erase(handle);
		if (!Refresh())
			TC_Core_Warn("Asset deletion completed, but the registry refresh was incomplete");

		error.clear();
		std::filesystem::remove_all(stagingDirectory, error);
		if (error)
			TC_Core_Warn("Deleted asset recovery data remains in Library: {0}",
				PathToUTF8(stagingDirectory));
		return true;
	}

	bool AssetRegistry::SetImportSettings(AssetHandle handle,
		const AssetImportSettings& settings)
	{
		const AssetMetadata* current = GetMetadata(handle);
		if (!current)
			return false;
		const std::filesystem::path sourcePath = GetFileSystemPath(handle);
		const std::filesystem::path metadataPath = GetMetadataPath(sourcePath);
		if (!EntryExists(metadataPath) || IsReparsePoint(metadataPath))
			return false;

		AssetMetadata diskMetadata;
		if (!ReadMetadata(metadataPath, diskMetadata) || diskMetadata.Handle != handle)
		{
			TC_Core_Error("Refusing to overwrite changed metadata for asset {0}",
				static_cast<uint64_t>(handle));
			return false;
		}
		diskMetadata.FilePath = current->FilePath;
		diskMetadata.IsMissing = current->IsMissing;
		diskMetadata.ImportSettings = settings;
		if (!WriteMetadata(metadataPath, diskMetadata))
			return false;
		m_Assets.insert_or_assign(handle, std::move(diskMetadata));
		RebuildPathIndex();
		if (!SaveCache())
			TC_Core_Warn("Import settings were saved, but the rebuildable registry cache was not updated");
		return true;
	}

	bool AssetRegistry::IsMetaFile(const std::filesystem::path& path)
	{
		const std::string name = LowerASCII(PathToUTF8(path.filename()));
		constexpr std::string_view suffix = ".tcmeta";
		return name.size() >= suffix.size() &&
			name.compare(name.size() - suffix.size(), suffix.size(), suffix) == 0;
	}

	std::filesystem::path AssetRegistry::GetMetadataPath(
		const std::filesystem::path& sourcePath)
	{
		if (sourcePath.empty())
			return {};
		std::filesystem::path metadataPath = sourcePath;
		metadataPath += ".tcmeta";
		return metadataPath;
	}

	bool AssetRegistry::LoadCache()
	{
		m_Assets.clear();
		m_PathIndex.clear();
		const std::filesystem::path cachePath = m_LibraryDirectory / "AssetRegistry.yaml";
		if (!EntryExists(cachePath))
			return true;

		try
		{
			if (IsReparsePoint(cachePath))
				throw std::runtime_error("cache is a symbolic/reparse file");
			std::ifstream input(cachePath, std::ios::binary);
			if (!input)
				throw std::runtime_error("could not open cache");
			const YAML::Node root = YAML::Load(input);
			if (input.bad() || !root.IsMap() || !root["SchemaVersion"] ||
				root["SchemaVersion"].as<uint32_t>() != kRegistryCacheSchemaVersion)
				throw std::runtime_error("unsupported cache schema");
			const YAML::Node assets = root["Assets"];
			if (!assets || !assets.IsSequence())
				throw std::runtime_error("Assets must be a sequence");

			for (const YAML::Node& entry : assets)
			{
				if (!entry.IsMap() || !entry["Handle"] || !entry["Type"] ||
					!entry["FilePath"] || !entry["ImportSettings"])
					throw std::runtime_error("cache entry is incomplete");
				const uint64_t rawHandle = entry["Handle"].as<uint64_t>();
				const AssetType type = AssetTypeFromString(entry["Type"].as<std::string>());
				if (rawHandle == 0 || type == AssetType::None)
					throw std::runtime_error("cache entry has an invalid handle or type");

				const std::filesystem::path cachedPath =
					UTF8ToPath(entry["FilePath"].as<std::string>());
				if (cachedPath.is_absolute() || cachedPath.has_root_name() ||
					cachedPath.has_root_directory())
					throw std::runtime_error("cache entry path must be project-relative");
				std::filesystem::path absolutePath, relativePath;
				if (!NormalizeManagedPath(cachedPath,
					absolutePath, relativePath, true, false))
					throw std::runtime_error("cache entry path escapes Assets");

				AssetMetadata metadata;
				metadata.Handle = AssetHandle(rawHandle);
				metadata.Type = type;
				metadata.FilePath = relativePath;
				metadata.IsMissing = true;
				const YAML::Node settings = entry["ImportSettings"];
				if (!settings.IsMap())
					throw std::runtime_error("cache import settings must be a map");
				for (const auto& setting : settings)
				{
					if (!setting.first.IsScalar() || !setting.second.IsScalar())
						throw std::runtime_error("cache import settings must be scalar strings");
					metadata.ImportSettings.emplace(setting.first.as<std::string>(),
						setting.second.as<std::string>());
				}
				if (m_Assets.find(metadata.Handle) != m_Assets.end())
					throw std::runtime_error("cache contains a duplicate handle");
				m_Assets.emplace(metadata.Handle, std::move(metadata));
			}
			RebuildPathIndex();
			return true;
		}
		catch (const std::exception& exception)
		{
			TC_Core_Warn("Could not load rebuildable asset registry cache '{0}': {1}",
				PathToUTF8(cachePath), exception.what());
			m_Assets.clear();
			m_PathIndex.clear();
			return false;
		}
	}

	bool AssetRegistry::SaveCache() const
	{
		if (!m_Initialized)
			return false;

		std::vector<const AssetMetadata*> ordered;
		ordered.reserve(m_Assets.size());
		for (const auto& [handle, metadata] : m_Assets)
			ordered.push_back(&metadata);
		std::sort(ordered.begin(), ordered.end(), [](const AssetMetadata* left,
			const AssetMetadata* right)
		{
			const std::string leftKey = PathKey(left->FilePath);
			const std::string rightKey = PathKey(right->FilePath);
			if (leftKey != rightKey)
				return leftKey < rightKey;
			return static_cast<uint64_t>(left->Handle) < static_cast<uint64_t>(right->Handle);
		});

		YAML::Emitter output;
		output << YAML::BeginMap;
		output << YAML::Key << "SchemaVersion" << YAML::Value << kRegistryCacheSchemaVersion;
		output << YAML::Key << "Assets" << YAML::Value << YAML::BeginSeq;
		for (const AssetMetadata* metadata : ordered)
		{
			output << YAML::BeginMap;
			output << YAML::Key << "Handle" << YAML::Value <<
				static_cast<uint64_t>(metadata->Handle);
			output << YAML::Key << "Type" << YAML::Value << AssetTypeToString(metadata->Type);
			output << YAML::Key << "FilePath" << YAML::Value << PathToUTF8(metadata->FilePath);
			output << YAML::Key << "ImportSettings" << YAML::Value << YAML::BeginMap;
			for (const auto& [key, value] : metadata->ImportSettings)
				output << YAML::Key << key << YAML::Value << value;
			output << YAML::EndMap;
			output << YAML::EndMap;
		}
		output << YAML::EndSeq;
		output << YAML::EndMap;
		if (!output.good())
			return false;

		std::string writeError;
		const std::filesystem::path cachePath = m_LibraryDirectory / "AssetRegistry.yaml";
		if (!FileSystem::WriteFileAtomically(cachePath, output.c_str(), writeError))
		{
			TC_Core_Warn("Could not atomically save asset registry cache '{0}': {1}",
				PathToUTF8(cachePath), writeError);
			return false;
		}
		return true;
	}

}
