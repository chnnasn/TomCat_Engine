#include "tcpch.h"
#include "AssetManager.h"

#include "TomCat/Project/Project.h"
#include "TomCat/Scene/SceneSerializer.h"
#include "TomCat/Utils/FileSystemUtils.h"
#include "TomCat/Utils/PathUtils.h"

#include <array>
#include <fstream>
#include <limits>
#include <new>
#include <sstream>
#include <type_traits>

#include <yaml-cpp/yaml.h>

namespace TomCat {

	namespace {

		constexpr std::array<char, 8> kPackageMagic = { 'T', 'C', 'P', 'A', 'C', 'K', '0', '1' };
		constexpr uint32_t kPackageVersion = 3;
		// Base manifest (32 bytes) followed by sixteen uint16 collision-mask rows.
		constexpr uint32_t kPackageHeaderSize = 64;
		constexpr uint64_t kPackageEntrySize = 32;
		constexpr size_t kCopyBufferSize = 64 * 1024;

		template<typename UInt>
		bool WriteLittleEndian(std::ostream& output, UInt value)
		{
			static_assert(std::is_unsigned_v<UInt>);
			std::array<unsigned char, sizeof(UInt)> bytes{};
			for (size_t index = 0; index < bytes.size(); ++index)
				bytes[index] = static_cast<unsigned char>((value >> (index * 8)) & static_cast<UInt>(0xff));
			output.write(reinterpret_cast<const char*>(bytes.data()),
				static_cast<std::streamsize>(bytes.size()));
			return output.good();
		}

		bool IsSymmetricCollisionMatrix(const Physics2DSettings& settings)
		{
			for (std::size_t layerA = 0; layerA < Physics2DLayerCount; ++layerA)
			{
				for (std::size_t layerB = layerA; layerB < Physics2DLayerCount; ++layerB)
				{
					if (settings.CanLayersCollide(static_cast<uint8_t>(layerA),
						static_cast<uint8_t>(layerB))
						!= settings.CanLayersCollide(static_cast<uint8_t>(layerB),
							static_cast<uint8_t>(layerA)))
						return false;
				}
			}
			return true;
		}

		template<typename UInt>
		bool ReadLittleEndian(std::istream& input, UInt& value)
		{
			static_assert(std::is_unsigned_v<UInt>);
			std::array<unsigned char, sizeof(UInt)> bytes{};
			if (!input.read(reinterpret_cast<char*>(bytes.data()),
				static_cast<std::streamsize>(bytes.size())))
				return false;
			value = 0;
			for (size_t index = 0; index < bytes.size(); ++index)
				value |= static_cast<UInt>(bytes[index]) << (index * 8);
			return true;
		}

		bool CheckedAdd(uint64_t left, uint64_t right, uint64_t& result)
		{
			if (right > (std::numeric_limits<uint64_t>::max)() - left)
				return false;
			result = left + right;
			return true;
		}

		bool CheckedMultiply(uint64_t left, uint64_t right, uint64_t& result)
		{
			if (left != 0 && right > (std::numeric_limits<uint64_t>::max)() / left)
				return false;
			result = left * right;
			return true;
		}

		std::filesystem::path AbsoluteLexical(const std::filesystem::path& path)
		{
			if (path.empty())
				return {};
			std::error_code error;
			const std::filesystem::path absolute = std::filesystem::absolute(path, error);
			return (error ? path : absolute).lexically_normal();
		}

		std::filesystem::path CanonicalForContainment(const std::filesystem::path& path)
		{
			const std::filesystem::path absolute = AbsoluteLexical(path);
			if (absolute.empty())
				return {};
			std::error_code error;
			const std::filesystem::path canonical = std::filesystem::weakly_canonical(absolute, error);
			return (error ? absolute : canonical).lexically_normal();
		}

		bool IsWithinOrEqual(const std::filesystem::path& root,
			const std::filesystem::path& candidate)
		{
			const std::filesystem::path normalizedRoot = CanonicalForContainment(root);
			const std::filesystem::path normalizedCandidate = CanonicalForContainment(candidate);
			if (normalizedRoot.empty() || normalizedCandidate.empty())
				return false;
			const std::filesystem::path relative = normalizedCandidate.lexically_relative(normalizedRoot);
			if (relative.empty() || relative.is_absolute())
				return false;
			for (const auto& part : relative)
			{
				if (part == "..")
					return false;
			}
			return true;
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
			size_t copied = 0;
			while (copied < bytes.size())
			{
				const size_t chunk = (std::min)(kCopyBufferSize, bytes.size() - copied);
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

		bool CopyFileBytes(const std::filesystem::path& source, uint64_t expectedSize,
			std::ostream& output)
		{
			std::ifstream input(source, std::ios::binary);
			if (!input)
				return false;

			std::array<char, kCopyBufferSize> buffer{};
			uint64_t remaining = expectedSize;
			while (remaining > 0)
			{
				const size_t chunk = static_cast<size_t>((std::min)(remaining,
					static_cast<uint64_t>(buffer.size())));
				if (!input.read(buffer.data(), static_cast<std::streamsize>(chunk)))
					return false;
				output.write(buffer.data(), static_cast<std::streamsize>(chunk));
				if (!output.good())
					return false;
				remaining -= chunk;
			}

			// Detect a source that grew after the index was built. A source that
			// shrank is caught by the short read above.
			return input.peek() == std::char_traits<char>::eof();
		}

		bool IsCurrentAsset(const AssetRegistry& registry, const AssetMetadata& metadata,
			AssetType expectedType)
		{
			if (metadata.IsMissing || metadata.Type != expectedType ||
				static_cast<uint64_t>(metadata.Handle) == 0)
				return false;
			const AssetMetadata* current = registry.GetMetadata(metadata.FilePath);
			return current && current->Handle == metadata.Handle && !current->IsMissing &&
				current->Type == expectedType;
		}

		bool ValidateCookedSpriteHandle(const AssetRegistry& registry, uint64_t rawHandle,
			const std::filesystem::path& scenePath, const std::string& propertyPath,
			std::string& errorMessage)
		{
			if (rawHandle == 0)
				return true;
			const AssetMetadata* metadata = registry.GetMetadata(AssetHandle(rawHandle));
			if (!metadata || !IsCurrentAsset(registry, *metadata, AssetType::Texture2D))
			{
				errorMessage = "Scene '" + PathToUTF8(scenePath) + "' property " + propertyPath +
					" references missing or non-texture asset " + std::to_string(rawHandle);
				return false;
			}
			return true;
		}

		bool PrepareSceneBytesForCook(const AssetRegistry& registry,
			const std::filesystem::path& scenePath, std::vector<uint8_t>& bytes,
			std::string& errorMessage)
		{
			bytes.clear();
			std::vector<uint8_t> sourceBytes;
			if (!ReadWholeFile(scenePath, sourceBytes))
			{
				errorMessage = "Could not read scene '" + PathToUTF8(scenePath) + "'";
				return false;
			}
			if (!SceneSerializer::ValidateCurrentFormat(sourceBytes, scenePath))
			{
				errorMessage = "Scene '" + PathToUTF8(scenePath) +
					"' does not conform to the complete current scene schema";
				return false;
			}
			try
			{
				std::string serialized(sourceBytes.begin(), sourceBytes.end());
				std::istringstream input(std::move(serialized));
				const YAML::Node root = YAML::Load(input);
				const YAML::Node entities = root["Entities"];
				for (size_t index = 0; index < entities.size(); ++index)
				{
					const YAML::Node entity = entities[index];
					const YAML::Node sprite = entity["SpriteRenderer"];
					if (!sprite)
						continue;

					const std::string propertyPath = "Entities[" + std::to_string(index) +
						"].SpriteRenderer.SpriteHandle";
					const YAML::Node handleNode = sprite["SpriteHandle"];
					const uint64_t rawHandle = handleNode.as<uint64_t>();
					if (!ValidateCookedSpriteHandle(registry, rawHandle, scenePath,
						propertyPath, errorMessage))
						return false;
				}

				YAML::Emitter output;
				output << root;
				if (!output.good())
				{
					errorMessage = "Could not emit cooked scene '" + PathToUTF8(scenePath) +
						"': " + output.GetLastError();
					return false;
				}
				const std::string cooked = output.c_str();
				bytes.assign(cooked.begin(), cooked.end());
				return true;
			}
			catch (const std::exception& error)
			{
				errorMessage = "Could not prepare scene '" + PathToUTF8(scenePath) +
					"' for cooking: " + error.what();
				return false;
			}
		}

		void RemoveTemporaryFile(const std::filesystem::path& path)
		{
			if (path.empty())
				return;
			std::error_code ignored;
			std::filesystem::remove(path, ignored);
		}

	}

	AssetManager& AssetManager::Get()
	{
		static AssetManager manager;
		return manager;
	}

	bool AssetManager::Initialize(const std::filesystem::path& assetRoot,
		const std::filesystem::path& libraryRoot)
	{
		Shutdown();
		if (assetRoot.empty() || libraryRoot.empty())
		{
			TC_Core_Error("AssetManager requires non-empty Assets and Library roots");
			return false;
		}

		m_RegistryInitialized = m_Registry.Initialize(assetRoot, libraryRoot);
		if (!m_RegistryInitialized)
		{
			m_Registry.Shutdown();
			TC_Core_Error("Failed to initialize the asset registry for '{0}'", PathToUTF8(assetRoot));
		}
		return m_RegistryInitialized;
	}

	bool AssetManager::SetProject(const Ref<Project>& project)
	{
		if (!project || project->GetProjectPath().empty())
		{
			Shutdown();
			return project == nullptr;
		}
		if (!Initialize(project->GetAssetPath(), project->GetLibraryPath()))
			return false;
		m_AuthoringProject = project;
		m_UsesProjectConfiguration = true;

		const AssetHandle configuredHandle = project->GetConfig().StartSceneHandle;
		if (static_cast<uint64_t>(configuredHandle) != 0)
		{
			const AssetMetadata* metadata = m_Registry.GetMetadata(configuredHandle);
			if (metadata && IsCurrentAsset(m_Registry, *metadata, AssetType::Scene) &&
				metadata->FilePath != project->GetConfig().StartScene)
			{
				// The UUID is authoritative. Repair the authoring path after an
				// external move or a crash between an asset move and project save.
				if (project->SetStartScene(metadata->FilePath) && !project->Save())
					TC_Core_Warn("StartScene was resolved by Handle, but Project.tcproj could not store its repaired path");
			}
			return true;
		}
		return true;
	}

	Physics2DSettings AssetManager::GetPhysics2DSettings() const
	{
		if (IsCookedPackageMounted())
			return m_CookedPhysics2DSettings;
		if (const Ref<Project> project = m_AuthoringProject.lock())
			return project->GetSettings().Physics2D;
		return Physics2DSettings{};
	}

	void AssetManager::Shutdown()
	{
		ReleaseAll();
		{
			std::lock_guard<std::mutex> lock(m_CookedPackageMutex);
			m_CookedPackageStream.close();
			m_CookedPackageStream.clear();
		}
		m_CookedEntries.clear();
		m_CookedPackagePath.clear();
		m_CookedPackageSize = 0;
		m_CookedStartSceneHandle = AssetHandle(0);
		m_CookedPhysics2DSettings = Physics2DSettings{};
		m_AuthoringProject.reset();
		m_UsesProjectConfiguration = false;
		m_Registry.Shutdown();
		m_RegistryInitialized = false;
	}

	bool AssetManager::Refresh()
	{
		if (!m_RegistryInitialized || IsCookedPackageMounted())
			return false;
		const bool complete = m_Registry.Refresh();
		// Refresh may apply a safe partial scan while reporting damaged sidecars.
		// Any such metadata change must invalidate both successful and missing loads.
		ReleaseAll();
		return complete;
	}

	AssetHandle AssetManager::ImportAsset(const std::filesystem::path& path)
	{
		if (!m_RegistryInitialized || IsCookedPackageMounted())
			return AssetHandle(0);
		const AssetHandle handle = m_Registry.ImportAsset(path);
		// ImportAsset may perform a full registry refresh (for example when a
		// duplicate UUID appears or is resolved). A handle can therefore acquire a
		// different path even when the requested path has no before/after record.
		// Invalidate all typed instances so cached bytes can never cross identities.
		ReleaseAll();
		return handle;
	}

	bool AssetManager::SetImportSettings(AssetHandle handle,
		const AssetImportSettings& settings)
	{
		if (!m_RegistryInitialized || IsCookedPackageMounted() ||
			static_cast<uint64_t>(handle) == 0)
			return false;
		if (!m_Registry.SetImportSettings(handle, settings))
			return false;
		Release(handle);
		return true;
	}

	Ref<Texture2D> AssetManager::GetMissingTexture()
	{
		if (m_MissingTexture && m_MissingTexture->IsLoaded())
			return m_MissingTexture;

		m_MissingTexture = Texture2D::Create(2, 2);
		if (!m_MissingTexture || !m_MissingTexture->IsLoaded())
		{
			TC_Core_Error("Failed to create the missing-asset texture");
			m_MissingTexture.reset();
			return nullptr;
		}

		constexpr std::array<uint8_t, 16> pixels = {
			255, 0, 255, 255,   0, 0, 0, 255,
			0, 0, 0, 255,       255, 0, 255, 255
		};
		m_MissingTexture->SetData(pixels.data(), static_cast<uint32_t>(pixels.size()));
		return m_MissingTexture;
	}

	Ref<Texture2D> AssetManager::CacheMissingTexture(AssetHandle handle, const char* reason)
	{
		if (static_cast<uint64_t>(handle) != 0)
			TC_Core_Warn("Using the missing texture for asset {0}: {1}",
				static_cast<uint64_t>(handle), reason ? reason : "asset is unavailable");
		Ref<Texture2D> missing = GetMissingTexture();
		if (static_cast<uint64_t>(handle) != 0 && missing)
			m_TextureCache[handle] = missing;
		return missing;
	}

	Ref<Texture2D> AssetManager::LoadTexture(AssetHandle handle)
	{
		if (static_cast<uint64_t>(handle) == 0)
			return GetMissingTexture();

		AssetType registeredType = AssetType::None;
		if (IsCookedPackageMounted())
		{
			const auto cooked = m_CookedEntries.find(handle);
			if (cooked == m_CookedEntries.end())
				return CacheMissingTexture(handle, "handle is not present in the cooked package");
			registeredType = cooked->second.Type;
		}
		else
		{
			if (!m_RegistryInitialized)
				return CacheMissingTexture(handle, "asset registry is not initialized");
			const AssetMetadata* metadata = m_Registry.GetMetadata(handle);
			if (!metadata || metadata->IsMissing)
				return CacheMissingTexture(handle, "handle is not present in the asset registry");
			const AssetMetadata* current = m_Registry.GetMetadata(metadata->FilePath);
			if (!current || current->Handle != handle || current->IsMissing)
				return CacheMissingTexture(handle, "handle no longer owns its registered path");
			registeredType = metadata->Type;
		}
		if (registeredType != AssetType::Texture2D)
			return CacheMissingTexture(handle, "asset type is not Texture2D");

		const auto cached = m_TextureCache.find(handle);
		if (cached != m_TextureCache.end())
		{
			// A formerly missing handle may become valid after a conflict is fixed or
			// the source is restored. Never let the shared placeholder pin that stale
			// state once the current registry/package identity is valid again.
			if (cached->second && cached->second != m_MissingTexture)
				return cached->second;
			m_TextureCache.erase(cached);
		}
		if (IsCookedPackageMounted())
		{
			const auto cooked = m_CookedEntries.find(handle);
			if (cooked == m_CookedEntries.end() || cooked->second.Size >
				static_cast<uint64_t>((std::numeric_limits<int>::max)()))
				return CacheMissingTexture(handle, "encoded texture exceeds the decoder size limit");
		}
		else
		{
			std::error_code error;
			const std::filesystem::path source = m_Registry.GetFileSystemPath(handle);
			const uintmax_t size = source.empty() ? 0 : std::filesystem::file_size(source, error);
			if (error || source.empty() || size >
				static_cast<uintmax_t>((std::numeric_limits<int>::max)()))
				return CacheMissingTexture(handle, "encoded texture exceeds the decoder size limit");
		}

		AssetType type = AssetType::None;
		std::vector<uint8_t> bytes;
		if (!ReadAssetBytes(handle, bytes, &type))
			return CacheMissingTexture(handle, "asset bytes could not be read");
		if (type != registeredType)
			return CacheMissingTexture(handle, "asset type changed while it was being loaded");

		const std::filesystem::path sourcePath = ResolvePath(handle);
		Ref<Texture2D> texture = Texture2D::Create(bytes.data(), bytes.size(), sourcePath);
		if (!texture || !texture->IsLoaded())
			return CacheMissingTexture(handle, "encoded image could not be decoded");

		m_TextureCache.emplace(handle, texture);
		return texture;
	}

	void AssetManager::Release(AssetHandle handle)
	{
		m_TextureCache.erase(handle);
	}

	void AssetManager::ReleaseAll()
	{
		m_TextureCache.clear();
		m_MissingTexture.reset();
	}

	void AssetManager::ReleaseHandles(const std::vector<AssetHandle>& handles)
	{
		for (const AssetHandle handle : handles)
			m_TextureCache.erase(handle);
	}

	bool AssetManager::MoveAsset(const std::filesystem::path& source,
		const std::filesystem::path& destination)
	{
		if (!m_RegistryInitialized || IsCookedPackageMounted())
			return false;
		if (!Refresh())
		{
			TC_Core_Error("Asset move refused because the registry refresh was incomplete");
			return false;
		}

		std::vector<AssetHandle> handles = m_Registry.GetHandlesUnderPath(source);
		if (handles.empty())
		{
			if (const AssetMetadata* metadata = m_Registry.GetMetadata(source))
				handles.push_back(metadata->Handle);
		}
		if (!m_Registry.MoveAsset(source, destination))
			return false;
		ReleaseHandles(handles);
		return true;
	}

	bool AssetManager::MoveAsset(AssetHandle handle, const std::filesystem::path& destination)
	{
		if (static_cast<uint64_t>(handle) == 0 || !m_RegistryInitialized ||
			IsCookedPackageMounted())
			return false;
		if (!Refresh())
			return false;
		const AssetMetadata* metadata = m_Registry.GetMetadata(handle);
		if (!metadata || metadata->IsMissing)
			return false;
		const AssetMetadata* current = m_Registry.GetMetadata(metadata->FilePath);
		if (!current || current->Handle != handle)
			return false;
		const std::filesystem::path source = m_Registry.GetFileSystemPath(handle);
		if (source.empty() || !m_Registry.MoveAsset(source, destination, handle))
			return false;
		Release(handle);
		return true;
	}

	bool AssetManager::DeleteAsset(const std::filesystem::path& path, bool force,
		std::vector<AssetReference>* references)
	{
		return DeleteAssetInternal(path, force, references, AssetHandle(0));
	}

	bool AssetManager::DeleteAssetInternal(const std::filesystem::path& path, bool force,
		std::vector<AssetReference>* references, AssetHandle expectedHandle)
	{
		if (!m_RegistryInitialized || IsCookedPackageMounted())
			return false;
		const bool refreshComplete = Refresh();
		if (!refreshComplete && (!force || static_cast<uint64_t>(expectedHandle) != 0))
		{
			TC_Core_Error("Asset delete refused because the registry refresh was incomplete");
			return false;
		}

		std::vector<AssetHandle> handles;
		if (static_cast<uint64_t>(expectedHandle) != 0)
		{
			const AssetMetadata* expected = m_Registry.GetMetadata(path);
			if (!expected || expected->IsMissing || expected->Handle != expectedHandle)
				return false;
			handles.push_back(expectedHandle);
		}
		else
		{
			handles = m_Registry.GetHandlesUnderPath(path);
			if (handles.empty())
			{
				if (const AssetMetadata* metadata = m_Registry.GetMetadata(path))
					handles.push_back(metadata->Handle);
			}
		}
		std::vector<AssetReference> liveReferences;
		for (AssetHandle handle : handles)
		{
			if (const Ref<Project> project = m_AuthoringProject.lock(); project &&
				project->GetConfig().StartSceneHandle == handle)
			{
				AssetReference reference;
				reference.ReferencedAsset = handle;
				reference.FilePath = project->GetProjectPath();
				reference.PropertyPath = "Project.StartSceneHandle";
				liveReferences.emplace_back(std::move(reference));
			}
			if (!m_LiveReferenceProvider)
				continue;
			std::vector<AssetReference> current = m_LiveReferenceProvider(handle);
			liveReferences.insert(liveReferences.end(), current.begin(), current.end());
		}
		if (!force && !liveReferences.empty())
		{
			if (references)
				*references = std::move(liveReferences);
			return false;
		}

		std::vector<AssetReference> foundReferences;
		const bool deleted = m_Registry.DeleteAsset(path, force, foundReferences,
			expectedHandle);
		foundReferences.insert(foundReferences.end(), liveReferences.begin(), liveReferences.end());
		if (references)
			*references = foundReferences;
		if (!deleted)
			return false;

		ReleaseHandles(handles);
		return true;
	}

	bool AssetManager::DeleteAsset(AssetHandle handle, bool force,
		std::vector<AssetReference>* references)
	{
		if (static_cast<uint64_t>(handle) == 0 || !m_RegistryInitialized ||
			IsCookedPackageMounted())
			return false;
		if (!Refresh())
			return false;
		const AssetMetadata* metadata = m_Registry.GetMetadata(handle);
		if (!metadata || metadata->IsMissing)
			return false;
		const AssetMetadata* current = m_Registry.GetMetadata(metadata->FilePath);
		if (!current || current->Handle != handle)
			return false;
		const std::filesystem::path path = m_Registry.GetFileSystemPath(handle);
		return !path.empty() && DeleteAssetInternal(path, force, references, handle);
	}

	std::vector<AssetReference> AssetManager::FindReferences(AssetHandle handle) const
	{
		if (!m_RegistryInitialized || IsCookedPackageMounted() ||
			static_cast<uint64_t>(handle) == 0)
			return {};
		std::vector<AssetReference> references = m_Registry.FindReferences(handle);
		if (const Ref<Project> project = m_AuthoringProject.lock(); project &&
			project->GetConfig().StartSceneHandle == handle)
		{
			AssetReference reference;
			reference.ReferencedAsset = handle;
			reference.FilePath = project->GetProjectPath();
			reference.PropertyPath = "Project.StartSceneHandle";
			references.emplace_back(std::move(reference));
		}
		if (m_LiveReferenceProvider)
		{
			std::vector<AssetReference> live = m_LiveReferenceProvider(handle);
			references.insert(references.end(), live.begin(), live.end());
		}
		return references;
	}

	std::filesystem::path AssetManager::ResolvePath(AssetHandle handle) const
	{
		// A mounted package intentionally has no source-path fallback.
		if (!m_RegistryInitialized || IsCookedPackageMounted() ||
			static_cast<uint64_t>(handle) == 0)
			return {};
		const AssetMetadata* metadata = m_Registry.GetMetadata(handle);
		if (!metadata || metadata->IsMissing)
			return {};
		const AssetMetadata* current = m_Registry.GetMetadata(metadata->FilePath);
		return current && current->Handle == handle
			? m_Registry.GetFileSystemPath(handle) : std::filesystem::path{};
	}

	bool AssetManager::ReadAssetBytes(AssetHandle handle, std::vector<uint8_t>& bytes,
		AssetType* type) const
	{
		bytes.clear();
		if (type)
			*type = AssetType::None;
		if (static_cast<uint64_t>(handle) == 0)
			return false;

		if (!IsCookedPackageMounted())
		{
			if (!m_RegistryInitialized)
				return false;
			const AssetMetadata* metadata = m_Registry.GetMetadata(handle);
			if (!metadata || metadata->IsMissing)
				return false;
			const AssetMetadata* current = m_Registry.GetMetadata(metadata->FilePath);
			if (!current || current->Handle != handle)
				return false;
			const std::filesystem::path path = m_Registry.GetFileSystemPath(handle);
			if (path.empty() || !ReadWholeFile(path, bytes))
				return false;
			if (type)
				*type = metadata->Type;
			return true;
		}

		const auto iterator = m_CookedEntries.find(handle);
		if (iterator == m_CookedEntries.end())
			return false;
		const CookedEntry& entry = iterator->second;
		if (entry.Offset > m_CookedPackageSize || entry.Size > m_CookedPackageSize - entry.Offset ||
			entry.Size > static_cast<uint64_t>((std::numeric_limits<size_t>::max)()) ||
			entry.Size > static_cast<uint64_t>(bytes.max_size()) ||
			entry.Offset > static_cast<uint64_t>((std::numeric_limits<std::streamoff>::max)()))
			return false;

		try
		{
			bytes.resize(static_cast<size_t>(entry.Size));
		}
		catch (const std::exception&)
		{
			return false;
		}

		{
			std::lock_guard<std::mutex> lock(m_CookedPackageMutex);
			m_CookedPackageStream.clear();
			m_CookedPackageStream.seekg(static_cast<std::streamoff>(entry.Offset), std::ios::beg);
			if (!m_CookedPackageStream)
			{
				bytes.clear();
				return false;
			}

			size_t copied = 0;
			while (copied < bytes.size())
			{
				const size_t chunk = (std::min)(kCopyBufferSize, bytes.size() - copied);
				if (!m_CookedPackageStream.read(
					reinterpret_cast<char*>(bytes.data() + copied),
					static_cast<std::streamsize>(chunk)))
				{
					bytes.clear();
					return false;
				}
				copied += chunk;
			}
		}
		if (type)
			*type = entry.Type;
		return true;
	}

	std::vector<uint8_t> AssetManager::ReadAssetBytes(AssetHandle handle) const
	{
		std::vector<uint8_t> bytes;
		ReadAssetBytes(handle, bytes, nullptr);
		return bytes;
	}

	bool AssetManager::CookToPackage(const std::filesystem::path& packagePath)
	{
		if (m_UsesProjectConfiguration)
		{
			const Ref<Project> project = m_AuthoringProject.lock();
			if (!project)
			{
				TC_Core_Error("Cannot cook because the active Project is no longer available");
				return false;
			}
			const AssetHandle startScene = project->GetConfig().StartSceneHandle;
			const AssetMetadata* metadata = m_Registry.GetMetadata(startScene);
			if (static_cast<uint64_t>(startScene) == 0 || !metadata ||
				!IsCurrentAsset(m_Registry, *metadata, AssetType::Scene))
			{
				TC_Core_Error("Project StartSceneHandle must identify a live Scene asset: {0}",
					static_cast<uint64_t>(startScene));
				return false;
			}
			return CookToPackage(packagePath, startScene);
		}
		return CookToPackage(packagePath, AssetHandle(0));
	}

	bool AssetManager::CookToPackage(const std::filesystem::path& packagePath,
		AssetHandle startSceneHandle)
	{
		if (!m_RegistryInitialized || IsCookedPackageMounted() || packagePath.empty())
			return false;
		if (m_UsesProjectConfiguration && static_cast<uint64_t>(startSceneHandle) == 0)
		{
			TC_Core_Error("A project cook requires a nonzero StartSceneHandle");
			return false;
		}
		if (IsWithinOrEqual(m_Registry.GetAssetDirectory(), packagePath))
		{
			TC_Core_Error("Cooked package must be written outside Assets: {0}", PathToUTF8(packagePath));
			return false;
		}
		if (!Refresh())
			return false;
		Physics2DSettings packagePhysicsSettings;
		if (m_UsesProjectConfiguration)
		{
			const Ref<Project> project = m_AuthoringProject.lock();
			if (!project)
			{
				TC_Core_Error("Cannot cook because the active Project is no longer available");
				return false;
			}
			packagePhysicsSettings = project->GetSettings().Physics2D;
		}
		if (!IsSymmetricCollisionMatrix(packagePhysicsSettings))
		{
			TC_Core_Error("Cannot cook an asymmetric Physics2D collision matrix");
			return false;
		}
		if (static_cast<uint64_t>(startSceneHandle) != 0)
		{
			const AssetMetadata* startScene = m_Registry.GetMetadata(startSceneHandle);
			if (!startScene || !IsCurrentAsset(m_Registry, *startScene, AssetType::Scene))
			{
				TC_Core_Error("Cook start scene {0} is missing or is not a Scene asset",
					static_cast<uint64_t>(startSceneHandle));
				return false;
			}
		}

		struct SourceEntry
		{
			AssetHandle Handle = AssetHandle(0);
			AssetType Type = AssetType::None;
			std::filesystem::path Path;
			std::vector<uint8_t> CookedBytes;
			bool HasCookedBytes = false;
			uint64_t Offset = 0;
			uint64_t Size = 0;
		};

		std::vector<SourceEntry> entries;
		entries.reserve(m_Registry.GetAssets().size());
		for (const auto& [handle, metadata] : m_Registry.GetAssets())
		{
			if (static_cast<uint64_t>(handle) == 0 || metadata.Type == AssetType::None)
				continue;
			const AssetMetadata* current = m_Registry.GetMetadata(metadata.FilePath);
			// Missing cache tombstones and shadowed historical records are not source
			// assets. References to them are rejected while preparing scenes below.
			if (metadata.IsMissing || !current || current->Handle != handle || current->IsMissing)
				continue;
			const std::filesystem::path source = m_Registry.GetFileSystemPath(handle);
			std::error_code error;
			if (source.empty() || !std::filesystem::is_regular_file(source, error) || error)
			{
				TC_Core_Error("Cannot cook missing asset {0} ('{1}')",
					static_cast<uint64_t>(handle), PathToUTF8(metadata.FilePath));
				return false;
			}
			SourceEntry entry;
			entry.Handle = handle;
			entry.Type = metadata.Type;
			entry.Path = source;
			if (metadata.Type == AssetType::Scene)
			{
				std::string sceneError;
				if (!PrepareSceneBytesForCook(m_Registry, source, entry.CookedBytes, sceneError))
				{
					TC_Core_Error("Cannot cook scene: {0}", sceneError);
					return false;
				}
				entry.HasCookedBytes = true;
				entry.Size = static_cast<uint64_t>(entry.CookedBytes.size());
			}
			else
			{
				const uintmax_t fileSize = std::filesystem::file_size(source, error);
				if (error || fileSize > (std::numeric_limits<uint64_t>::max)())
				{
					TC_Core_Error("Cannot inspect asset while cooking: {0}", PathToUTF8(source));
					return false;
				}
				entry.Size = static_cast<uint64_t>(fileSize);
			}
			entries.push_back(std::move(entry));
		}

		std::sort(entries.begin(), entries.end(), [](const SourceEntry& left, const SourceEntry& right) {
			return static_cast<uint64_t>(left.Handle) < static_cast<uint64_t>(right.Handle);
		});

		uint64_t indexSize = 0;
		uint64_t dataOffset = 0;
		if (!CheckedMultiply(static_cast<uint64_t>(entries.size()), kPackageEntrySize, indexSize) ||
			!CheckedAdd(kPackageHeaderSize, indexSize, dataOffset))
		{
			TC_Core_Error("Cooked package index is too large");
			return false;
		}
		for (SourceEntry& entry : entries)
		{
			entry.Offset = dataOffset;
			if (!CheckedAdd(dataOffset, entry.Size, dataOffset))
			{
				TC_Core_Error("Cooked package exceeds the supported 64-bit size");
				return false;
			}
		}

		std::error_code directoryError;
		if (!packagePath.parent_path().empty())
			std::filesystem::create_directories(packagePath.parent_path(), directoryError);
		if (directoryError)
		{
			TC_Core_Error("Cannot create cooked package directory '{0}': {1}",
				PathToUTF8(packagePath.parent_path()), directoryError.message());
			return false;
		}

		const std::filesystem::path temporary = FileSystem::MakeTemporarySiblingPath(packagePath);
		if (temporary.empty())
		{
			TC_Core_Error("Could not allocate a temporary cooked package path");
			return false;
		}

		std::ofstream output(temporary, std::ios::binary | std::ios::trunc);
		if (!output)
		{
			TC_Core_Error("Could not open temporary cooked package '{0}'", PathToUTF8(temporary));
			return false;
		}

		output.write(kPackageMagic.data(), static_cast<std::streamsize>(kPackageMagic.size()));
		bool succeeded = output.good() &&
			WriteLittleEndian<uint32_t>(output, kPackageVersion) &&
			WriteLittleEndian<uint32_t>(output, kPackageHeaderSize) &&
			WriteLittleEndian<uint64_t>(output, static_cast<uint64_t>(entries.size())) &&
			WriteLittleEndian<uint64_t>(output, static_cast<uint64_t>(startSceneHandle));
		for (uint16_t mask : packagePhysicsSettings.CollisionMasks)
			succeeded = succeeded && WriteLittleEndian<uint16_t>(output, mask);
		for (const SourceEntry& entry : entries)
		{
			if (!succeeded)
				break;
			succeeded = WriteLittleEndian<uint64_t>(output, static_cast<uint64_t>(entry.Handle)) &&
				WriteLittleEndian<uint16_t>(output, static_cast<uint16_t>(entry.Type)) &&
				WriteLittleEndian<uint16_t>(output, 0) &&
				WriteLittleEndian<uint32_t>(output, 0) &&
				WriteLittleEndian<uint64_t>(output, entry.Offset) &&
				WriteLittleEndian<uint64_t>(output, entry.Size);
		}
		for (const SourceEntry& entry : entries)
		{
			if (!succeeded)
				break;
			if (entry.HasCookedBytes)
			{
				if (!entry.CookedBytes.empty())
					output.write(reinterpret_cast<const char*>(entry.CookedBytes.data()),
						static_cast<std::streamsize>(entry.CookedBytes.size()));
				succeeded = output.good();
			}
			else
				succeeded = CopyFileBytes(entry.Path, entry.Size, output);
		}

		output.flush();
		succeeded = succeeded && output.good();
		output.close();
		succeeded = succeeded && !output.fail();
		if (!succeeded)
		{
			RemoveTemporaryFile(temporary);
			TC_Core_Error("Failed while writing cooked package '{0}'", PathToUTF8(packagePath));
			return false;
		}

		std::string installError;
		if (!FileSystem::InstallTemporaryFileAtomically(temporary, packagePath, installError))
		{
			TC_Core_Error("Could not install cooked package '{0}': {1}",
				PathToUTF8(packagePath), installError);
			return false;
		}
		return true;
	}

	bool AssetManager::MountCookedPackage(const std::filesystem::path& packagePath)
	{
		if (packagePath.empty())
			return false;
		std::ifstream input(packagePath, std::ios::binary | std::ios::ate);
		if (!input)
			return false;
		const std::streamoff packageEnd = input.tellg();
		if (packageEnd < static_cast<std::streamoff>(kPackageHeaderSize))
			return false;
		const uint64_t packageSize = static_cast<uint64_t>(packageEnd);
		input.seekg(0, std::ios::beg);

		std::array<char, kPackageMagic.size()> magic{};
		uint32_t version = 0;
		uint32_t headerSize = 0;
		uint64_t entryCount = 0;
		uint64_t rawStartSceneHandle = 0;
		if (!input.read(magic.data(), static_cast<std::streamsize>(magic.size())) ||
			magic != kPackageMagic ||
			!ReadLittleEndian<uint32_t>(input, version) ||
			version != kPackageVersion ||
			!ReadLittleEndian<uint32_t>(input, headerSize) ||
			!ReadLittleEndian<uint64_t>(input, entryCount) ||
			!ReadLittleEndian<uint64_t>(input, rawStartSceneHandle))
			return false;
		Physics2DSettings mountedPhysicsSettings;
		for (uint16_t& mask : mountedPhysicsSettings.CollisionMasks)
		{
			if (!ReadLittleEndian<uint16_t>(input, mask))
				return false;
		}
		if (headerSize != kPackageHeaderSize || headerSize > packageSize)
			return false;
		if (!IsSymmetricCollisionMatrix(mountedPhysicsSettings))
			return false;

		uint64_t indexSize = 0;
		uint64_t dataStart = 0;
		if (!CheckedMultiply(entryCount, kPackageEntrySize, indexSize) ||
			!CheckedAdd(headerSize, indexSize, dataStart) || dataStart > packageSize ||
			entryCount > static_cast<uint64_t>((std::numeric_limits<size_t>::max)()))
			return false;

		input.seekg(static_cast<std::streamoff>(headerSize), std::ios::beg);
		if (!input)
			return false;

		std::unordered_map<AssetHandle, CookedEntry> entries;
		std::vector<std::pair<uint64_t, uint64_t>> occupiedRanges;
		try
		{
			entries.reserve(static_cast<size_t>(entryCount));
			occupiedRanges.reserve(static_cast<size_t>(entryCount));
		}
		catch (const std::exception&)
		{
			return false;
		}

		for (uint64_t index = 0; index < entryCount; ++index)
		{
			uint64_t rawHandle = 0;
			uint16_t rawType = 0;
			uint16_t flags = 0;
			uint32_t reserved = 0;
			uint64_t offset = 0;
			uint64_t size = 0;
			if (!ReadLittleEndian<uint64_t>(input, rawHandle) ||
				!ReadLittleEndian<uint16_t>(input, rawType) ||
				!ReadLittleEndian<uint16_t>(input, flags) ||
				!ReadLittleEndian<uint32_t>(input, reserved) ||
				!ReadLittleEndian<uint64_t>(input, offset) ||
				!ReadLittleEndian<uint64_t>(input, size))
				return false;

			if (rawHandle == 0 || rawType == static_cast<uint16_t>(AssetType::None) ||
				rawType > static_cast<uint16_t>(AssetType::Other) || flags != 0 || reserved != 0 ||
				offset < dataStart || offset > packageSize || size > packageSize - offset)
				return false;

			const AssetHandle handle(rawHandle);
			if (!entries.emplace(handle,
				CookedEntry{ static_cast<AssetType>(rawType), offset, size }).second)
				return false;
			if (size != 0)
				occupiedRanges.emplace_back(offset, offset + size);
		}

		std::sort(occupiedRanges.begin(), occupiedRanges.end());
		for (size_t index = 1; index < occupiedRanges.size(); ++index)
		{
			if (occupiedRanges[index].first < occupiedRanges[index - 1].second)
				return false;
		}
		if (rawStartSceneHandle != 0)
		{
			const auto startScene = entries.find(AssetHandle(rawStartSceneHandle));
			if (startScene == entries.end() || startScene->second.Type != AssetType::Scene)
				return false;
		}

		ReleaseAll();
		{
			std::lock_guard<std::mutex> lock(m_CookedPackageMutex);
			m_CookedPackageStream = std::move(input);
			m_CookedPackageStream.clear();
		}
		m_CookedEntries = std::move(entries);
		m_CookedPackagePath = AbsoluteLexical(packagePath);
		m_CookedPackageSize = packageSize;
		m_CookedStartSceneHandle = AssetHandle(rawStartSceneHandle);
		m_CookedPhysics2DSettings = mountedPhysicsSettings;
		return true;
	}

	void AssetManager::UnmountCookedPackage()
	{
		if (!IsCookedPackageMounted())
			return;
		ReleaseAll();
		{
			std::lock_guard<std::mutex> lock(m_CookedPackageMutex);
			m_CookedPackageStream.close();
			m_CookedPackageStream.clear();
		}
		m_CookedEntries.clear();
		m_CookedPackagePath.clear();
		m_CookedPackageSize = 0;
		m_CookedStartSceneHandle = AssetHandle(0);
		m_CookedPhysics2DSettings = Physics2DSettings{};
	}

}
