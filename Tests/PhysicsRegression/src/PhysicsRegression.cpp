#include "TomCat/Core/Log.h"
#include "TomCat/Core/Input.h"
#include "TomCat/Asset/AssetManager.h"
#include "TomCat/Asset/ContentHash.h"
#include "TomCat/Project/Project.h"
#include "TomCat/Runtime/RuntimeCompatibility.h"
#include "TomCat/Scene/Components.h"
#include "TomCat/Scene/Entity.h"
#include "TomCat/Scene/Scene.h"
#include "TomCat/Scene/SceneSerializer.h"
#include "TomCat/Scripting/DotNetHost.h"
#include "TomCat/Scripting/IScriptRuntime.h"
#include "TomCat/Scripting/ScriptEngine.h"
#include "TomCat/Scripting/ScriptGlue.h"

#include "SceneManagerRegression.h"
#include "PrefabRegression.h"
#include "ComponentRegistryRegression.h"
#include "SceneCommandBufferRegression.h"
#include "RuntimeUIRegression.h"
#include "WindowMetricsRegression.h"

#include "box2d/b2_body.h"
#include "box2d/b2_fixture.h"

#include <algorithm>
#include <atomic>
#include <array>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iostream>
#include <iterator>
#include <limits>
#include <memory>
#include <sstream>
#include <span>
#include <stdexcept>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

namespace {
	class ManagedRuntimeProbe final : public TomCat::Scripting::IScriptRuntime
	{
	public:
		enum class PhysicsMutation
		{
			None,
			RemoveRigidbody2D,
			DestroyEntity
		};

		bool IsReady() const override { return Ready; }
		TomCat::Scripting::ScriptStatus CreateSceneRuntime(uint64_t sceneSessionId,
			uint64_t runtimeGeneration) override
		{
			Calls.emplace_back("CreateSceneRuntime");
			LastSceneSession = sceneSessionId;
			LastRuntimeGeneration = runtimeGeneration;
			Active = true;
			return TomCat::Scripting::ScriptStatus::Success;
		}
		TomCat::Scripting::ScriptStatus InstantiateAll(
			std::span<const TomCat::Scripting::NativeScriptAttachmentV1> attachments) override
		{
			Calls.emplace_back("InstantiateAll");
			Attachments.assign(attachments.begin(), attachments.end());
			return TomCat::Scripting::ScriptStatus::Success;
		}
		TomCat::Scripting::ScriptStatus ApplySerializedFields(
			std::string_view fieldsJson) override
		{
			Calls.emplace_back("ApplySerializedFields");
			LastFields.assign(fieldsJson);
			return TomCat::Scripting::ScriptStatus::Success;
		}
		TomCat::Scripting::ScriptStatus InvokeCreateAll() override
		{
			Calls.emplace_back("InvokeCreateAll");
			if (InvokeCreateStatus == TomCat::Scripting::ScriptStatus::Success
				&& InvokeCreateAction)
				InvokeCreateAction();
			return InvokeCreateStatus;
		}
		TomCat::Scripting::ScriptStatus SetEnabled(
			uint64_t attachmentId, bool enabled) override
		{
			Calls.emplace_back("SetEnabled");
			return SetEnabledAction
				? SetEnabledAction(attachmentId, enabled)
				: SetEnabledStatus;
		}
		TomCat::Scripting::ScriptStatus UpdateAll(float deltaTime) override
		{
			++UpdateCount;
			LastUpdateDelta = deltaTime;
			Calls.emplace_back("UpdateAll");
			return UpdateAllStatus;
		}
		TomCat::Scripting::ScriptStatus FixedUpdateAll(float fixedDeltaTime) override
		{
			++FixedUpdateCount;
			LastFixedDelta = fixedDeltaTime;
			Calls.emplace_back("FixedUpdateAll");
			if (FixedUpdateAction)
				FixedUpdateAction();
			return FixedUpdateAllStatus;
		}
		TomCat::Scripting::ScriptStatus DispatchPhysicsEvents(
			std::span<const TomCat::Scripting::NativePhysicsEventV1> events) override
		{
			PhysicsEventCount += static_cast<uint32_t>(events.size());
			PhysicsEvents.insert(PhysicsEvents.end(), events.begin(), events.end());
			Calls.emplace_back("DispatchPhysicsEvents");
			if (PhysicsEventAction)
				PhysicsEventAction();
			if (!MutationAttempted && Mutation != PhysicsMutation::None
				&& !events.empty() && !Attachments.empty())
			{
				MutationAttempted = true;
				if (Mutation == PhysicsMutation::RemoveRigidbody2D)
					MutationQueued = TomCat::Scripting::ScriptEngine::Get().QueueRemoveComponent(
						Attachments.front().Entity,
						TomCat::Scripting::NativeComponentType::Rigidbody2D);
				else
					MutationQueued = TomCat::Scripting::ScriptEngine::Get().QueueDestroyEntity(
						Attachments.front().Entity);
			}
			return DispatchPhysicsEventsStatus;
		}
		TomCat::Scripting::ScriptStatus DestroyAll() override
		{
			Calls.emplace_back("DestroyAll");
			Active = false;
			return TomCat::Scripting::ScriptStatus::Success;
		}
		TomCat::Scripting::ScriptStatus DestroyAttachments(
			std::span<const uint64_t> attachmentIds) override
		{
			++DestroyAttachmentsCallCount;
			DestroyedAttachmentCount += static_cast<uint32_t>(attachmentIds.size());
			Calls.emplace_back("DestroyAttachments");
			if (DestroyAttachmentsAction)
				DestroyAttachmentsAction(attachmentIds);
			return DestroyAttachmentsStatus;
		}
		TomCat::Scripting::ScriptStatus ResolveDeferredCommandBatch(
			bool committed) override
		{
			if (committed)
				++DeferredBatchCommitCount;
			else
				++DeferredBatchAbortCount;
			Calls.emplace_back(committed ? "CommitDeferredBatch"
				: "AbortDeferredBatch");
			if (ResolveDeferredBatchAction)
				ResolveDeferredBatchAction(committed);
			return ResolveDeferredBatchStatus;
		}
		TomCat::Scripting::ScriptStatus InstantiateAttachments(
			std::span<const TomCat::Scripting::NativeScriptAttachmentV1> attachments,
			std::string_view fieldsJson) override
		{
			++DynamicInstantiateCount;
			DynamicAttachments.assign(attachments.begin(), attachments.end());
			DynamicFields.assign(fieldsJson);
			if (InspectDynamicPhysics)
			{
				for (const auto& attachment : attachments)
				{
					TomCat::Entity entity = TomCat::Scripting::ScriptEngine::Get()
						.ResolveEntity(attachment.Entity);
					DynamicPhysicsReady = DynamicPhysicsReady && entity
						&& entity.HasComponent<TomCat::Rigidbody2D>()
						&& entity.HasComponent<TomCat::BoxCollider2D>()
						&& entity.GetComponent<TomCat::Rigidbody2D>().RuntimeBody != nullptr
						&& entity.GetComponent<TomCat::BoxCollider2D>().RuntimeFixture != nullptr;
				}
			}
			Calls.emplace_back("InstantiateAttachments");
			if (DynamicInstantiateStatus == TomCat::Scripting::ScriptStatus::Success
				&& DynamicInstantiateAction)
				DynamicInstantiateAction();
			return DynamicInstantiateStatus;
		}
		bool PollUnload() override { return UnloadSucceeds; }
		void OnUnloadFailed(std::string_view reason) override
		{
			++UnloadFailureCount;
			LastUnloadFailure.assign(reason);
		}

		bool Ready = true;
		bool Active = false;
		bool UnloadSucceeds = true;
		TomCat::Scripting::ScriptStatus InvokeCreateStatus =
			TomCat::Scripting::ScriptStatus::Success;
		TomCat::Scripting::ScriptStatus UpdateAllStatus =
			TomCat::Scripting::ScriptStatus::Success;
		TomCat::Scripting::ScriptStatus FixedUpdateAllStatus =
			TomCat::Scripting::ScriptStatus::Success;
		TomCat::Scripting::ScriptStatus DispatchPhysicsEventsStatus =
			TomCat::Scripting::ScriptStatus::Success;
		TomCat::Scripting::ScriptStatus DynamicInstantiateStatus =
			TomCat::Scripting::ScriptStatus::Success;
		TomCat::Scripting::ScriptStatus SetEnabledStatus =
			TomCat::Scripting::ScriptStatus::Success;
		TomCat::Scripting::ScriptStatus DestroyAttachmentsStatus =
			TomCat::Scripting::ScriptStatus::Success;
		TomCat::Scripting::ScriptStatus ResolveDeferredBatchStatus =
			TomCat::Scripting::ScriptStatus::Success;
		uint64_t LastSceneSession = 0;
		uint64_t LastRuntimeGeneration = 0;
		uint32_t UpdateCount = 0;
		uint32_t FixedUpdateCount = 0;
		uint32_t PhysicsEventCount = 0;
		uint32_t DestroyAttachmentsCallCount = 0;
		uint32_t DestroyedAttachmentCount = 0;
		uint32_t UnloadFailureCount = 0;
		uint32_t DynamicInstantiateCount = 0;
		uint32_t DeferredBatchCommitCount = 0;
		uint32_t DeferredBatchAbortCount = 0;
		float LastUpdateDelta = 0.0f;
		float LastFixedDelta = 0.0f;
		PhysicsMutation Mutation = PhysicsMutation::None;
		bool MutationAttempted = false;
		bool MutationQueued = false;
		bool InspectDynamicPhysics = false;
		bool DynamicPhysicsReady = true;
		std::function<void()> InvokeCreateAction;
		std::function<void()> DynamicInstantiateAction;
		std::function<void()> FixedUpdateAction;
		std::function<void()> PhysicsEventAction;
		std::function<TomCat::Scripting::ScriptStatus(uint64_t, bool)>
			SetEnabledAction;
		std::function<void(std::span<const uint64_t>)> DestroyAttachmentsAction;
		std::function<void(bool)> ResolveDeferredBatchAction;
		std::string LastFields;
		std::string LastUnloadFailure;
		std::string DynamicFields;
		std::vector<TomCat::Scripting::NativeScriptAttachmentV1> Attachments;
		std::vector<TomCat::Scripting::NativePhysicsEventV1> PhysicsEvents;
		std::vector<TomCat::Scripting::NativeScriptAttachmentV1> DynamicAttachments;
		std::vector<std::string> Calls;
	};

	class ScriptRuntimeOverride final
	{
	public:
		explicit ScriptRuntimeOverride(std::shared_ptr<TomCat::Scripting::IScriptRuntime> runtime)
		{
			TomCat::Scripting::ScriptEngine::Get().SetRuntime(std::move(runtime));
		}

		~ScriptRuntimeOverride()
		{
			TomCat::Scripting::ScriptEngine::Get().SetRuntime({});
		}

		ScriptRuntimeOverride(const ScriptRuntimeOverride&) = delete;
		ScriptRuntimeOverride& operator=(const ScriptRuntimeOverride&) = delete;
	};

	void Require(bool condition, const char* message)
	{
		if (!condition)
			throw std::runtime_error(message);
	}

	std::string ReadTextFile(const std::filesystem::path& path)
	{
		std::ifstream input(path, std::ios::binary);
		Require(static_cast<bool>(input), "could not open text fixture");
		return std::string(std::istreambuf_iterator<char>(input),
			std::istreambuf_iterator<char>());
	}

	void WriteTextFile(const std::filesystem::path& path, const std::string& contents)
	{
		std::ofstream output(path, std::ios::binary | std::ios::trunc);
		Require(static_cast<bool>(output), "could not create text fixture");
		output.write(contents.data(), static_cast<std::streamsize>(contents.size()));
		Require(output.good(), "could not write text fixture");
	}

	std::vector<uint8_t> ReadBinaryFile(const std::filesystem::path& path)
	{
		std::ifstream input(path, std::ios::binary);
		Require(static_cast<bool>(input), "could not open binary fixture");
		return std::vector<uint8_t>(std::istreambuf_iterator<char>(input),
			std::istreambuf_iterator<char>());
	}

	void WriteBinaryFile(const std::filesystem::path& path,
		const std::vector<uint8_t>& contents)
	{
		std::ofstream output(path, std::ios::binary | std::ios::trunc);
		Require(static_cast<bool>(output), "could not create binary fixture");
		if (!contents.empty())
			output.write(reinterpret_cast<const char*>(contents.data()),
				static_cast<std::streamsize>(contents.size()));
		Require(output.good(), "could not write binary fixture");
	}

	std::vector<uint8_t> MakeTestTGA()
	{
		// Two opaque BGRA pixels. Positive Cook tests must use an actually
		// decodable source now that TCPAK v6 packages importer artifacts rather
		// than blindly copying authoring bytes.
		std::vector<uint8_t> bytes(18 + 8, 0);
		bytes[2] = 2;
		bytes[12] = 2;
		bytes[14] = 1;
		bytes[16] = 32;
		bytes[17] = 0x28;
		bytes[18] = 0; bytes[19] = 0; bytes[20] = 255; bytes[21] = 255;
		bytes[22] = 0; bytes[23] = 255; bytes[24] = 0; bytes[25] = 255;
		return bytes;
	}

	uint16_t ReadLittleEndian16(const std::vector<uint8_t>& bytes, std::size_t offset)
	{
		Require(offset + 2 <= bytes.size(), "binary fixture has no uint16 at requested offset");
		return static_cast<uint16_t>(bytes[offset])
			| static_cast<uint16_t>(static_cast<uint16_t>(bytes[offset + 1]) << 8);
	}

	uint32_t ReadLittleEndian32(const std::vector<uint8_t>& bytes, std::size_t offset)
	{
		Require(offset + 4 <= bytes.size(), "binary fixture has no uint32 at requested offset");
		uint32_t value = 0;
		for (std::size_t index = 0; index < 4; ++index)
			value |= static_cast<uint32_t>(bytes[offset + index]) << (index * 8);
		return value;
	}

	uint64_t ReadLittleEndian64(const std::vector<uint8_t>& bytes, std::size_t offset)
	{
		Require(offset + 8 <= bytes.size(), "binary fixture has no uint64 at requested offset");
		uint64_t value = 0;
		for (std::size_t index = 0; index < 8; ++index)
			value |= static_cast<uint64_t>(bytes[offset + index]) << (index * 8);
		return value;
	}

	void WriteLittleEndian16(std::vector<uint8_t>& bytes, std::size_t offset, uint16_t value)
	{
		Require(offset + 2 <= bytes.size(), "binary fixture has no uint16 at requested offset");
		bytes[offset] = static_cast<uint8_t>(value & 0xff);
		bytes[offset + 1] = static_cast<uint8_t>((value >> 8) & 0xff);
	}

	void WriteLittleEndian32(std::vector<uint8_t>& bytes, std::size_t offset, uint32_t value)
	{
		Require(offset + 4 <= bytes.size(), "binary fixture has no uint32 at requested offset");
		for (std::size_t index = 0; index < 4; ++index)
			bytes[offset + index] = static_cast<uint8_t>((value >> (index * 8)) & 0xff);
	}

	void WriteLittleEndian64(std::vector<uint8_t>& bytes, std::size_t offset, uint64_t value)
	{
		Require(offset + 8 <= bytes.size(), "binary fixture has no uint64 at requested offset");
		for (std::size_t index = 0; index < 8; ++index)
			bytes[offset + index] = static_cast<uint8_t>((value >> (index * 8)) & 0xff);
	}

	std::vector<uint8_t> MakeManagedAssemblyFixture(const std::string& manifest)
	{
		std::vector<uint8_t> assembly = { 'M', 'Z' };
		assembly.reserve(assembly.size() + manifest.size() * 2);
		for (const unsigned char character : manifest)
		{
			assembly.push_back(character);
			assembly.push_back(0);
		}
		return assembly;
	}

	std::size_t FindManagedPackageIndexEntry(const std::vector<uint8_t>& package)
	{
		Require(package.size() >= TomCat::RuntimeCompatibility::TcpakV5BaseHeaderSize,
			"package fixture has no supported tcpak base header");
		const uint32_t version = ReadLittleEndian32(package, 8);
		Require(TomCat::RuntimeCompatibility::IsSupportedTcpakVersion(version),
			"package fixture has an unsupported tcpak version");
		const uint32_t baseHeaderSize =
			TomCat::RuntimeCompatibility::TcpakBaseHeaderSizeForVersion(version);
		const uint64_t entrySize =
			TomCat::RuntimeCompatibility::TcpakEntrySizeForVersion(version);
		const uint32_t headerSize = ReadLittleEndian32(package, 12);
		Require(headerSize >= baseHeaderSize
			&& headerSize <= package.size(),
			"package fixture has an invalid variable header");
		const uint64_t entryCount = ReadLittleEndian64(package, 16);
		Require(entryCount <= (package.size() - headerSize)
			/ entrySize,
			"package fixture has an out-of-bounds index");
		for (uint64_t index = 0; index < entryCount; ++index)
		{
			const std::size_t offset = headerSize + static_cast<std::size_t>(index
				* entrySize);
			if (ReadLittleEndian64(package, offset)
				== (std::numeric_limits<uint64_t>::max)())
				return offset;
		}
		throw std::runtime_error("package fixture has no managed payload index entry");
	}

	void RefreshTcpakEntryDigest(std::vector<uint8_t>& package,
		std::size_t entryOffset)
	{
		const uint32_t version = ReadLittleEndian32(package, 8);
		const uint64_t entrySize =
			TomCat::RuntimeCompatibility::TcpakEntrySizeForVersion(version);
		Require(version == TomCat::RuntimeCompatibility::TcpakVersion
			&& TomCat::RuntimeCompatibility::TcpakHasEntryDigests(version)
			&& entryOffset <= package.size()
			&& entrySize <= package.size() - entryOffset,
			"tcpak digest refresh requires a complete current index entry");
		const uint64_t payloadOffset = ReadLittleEndian64(package,
			entryOffset + 16);
		const uint64_t payloadSize = ReadLittleEndian64(package,
			entryOffset + 24);
		Require(payloadOffset <= package.size()
			&& payloadSize <= package.size() - payloadOffset,
			"tcpak digest refresh payload is out of range");
		const TomCat::ContentSHA256Digest digest =
			TomCat::ComputeContentSHA256Digest(std::span<const uint8_t>(
				package.data() + static_cast<size_t>(payloadOffset),
				static_cast<size_t>(payloadSize)));
		const size_t digestOffset = entryOffset
			+ static_cast<size_t>(
				TomCat::RuntimeCompatibility::TcpakLegacyEntrySize);
		std::copy(digest.begin(), digest.end(),
			package.begin() + digestOffset);
	}

	std::vector<uint8_t> MakeLegacyTcpakCompatibilityFixture(
		const std::vector<uint8_t>& current, uint32_t targetVersion)
	{
		Require((targetVersion == TomCat::RuntimeCompatibility::OldestSupportedTcpakVersion
				|| targetVersion == TomCat::RuntimeCompatibility::TcpakBootManifestVersion)
			&& current.size() >= TomCat::RuntimeCompatibility::TcpakBaseHeaderSize
			&& ReadLittleEndian32(current, 8)
				== TomCat::RuntimeCompatibility::TcpakVersion,
			"legacy conversion requires a current tcpak source and v5/v6 target");
		const uint32_t currentHeaderSize = ReadLittleEndian32(current, 12);
		const uint64_t entryCount = ReadLittleEndian64(current, 16);
		const uint64_t buildSceneCount = ReadLittleEndian64(current, 32);
		Require(buildSceneCount <= TomCat::RuntimeCompatibility::MaximumBuildSceneCount,
			"current tcpak source has too many build scenes");
		const uint64_t sceneBytes64 = buildSceneCount * sizeof(uint64_t);
		Require(sceneBytes64 <= currentHeaderSize
			&& currentHeaderSize >= TomCat::RuntimeCompatibility::TcpakBaseHeaderSize
				+ sceneBytes64,
			"current tcpak source has an invalid scene table");
		const std::size_t sceneBytes = static_cast<std::size_t>(sceneBytes64);
		const std::size_t sceneOffset = currentHeaderSize - sceneBytes;
		const uint32_t targetHeaderSize =
			targetVersion == TomCat::RuntimeCompatibility::OldestSupportedTcpakVersion
			? TomCat::RuntimeCompatibility::TcpakV5BaseHeaderSize
				+ static_cast<uint32_t>(sceneBytes)
			: currentHeaderSize;
		const uint64_t currentEntrySize =
			TomCat::RuntimeCompatibility::TcpakEntrySize;
		const uint64_t targetEntrySize =
			TomCat::RuntimeCompatibility::TcpakLegacyEntrySize;
		Require(currentHeaderSize <= current.size()
			&& entryCount <= (current.size() - currentHeaderSize)
				/ TomCat::RuntimeCompatibility::TcpakEntrySize,
			"current tcpak source has an invalid index");
		const uint64_t currentDataStart = currentHeaderSize
			+ entryCount * currentEntrySize;
		const uint64_t targetDataStart = targetHeaderSize
			+ entryCount * targetEntrySize;
		Require(targetDataStart <= currentDataStart
			&& currentDataStart <= current.size(),
			"current tcpak source has invalid data offsets");
		const uint64_t removedBytes = currentDataStart - targetDataStart;

		std::vector<uint8_t> legacy;
		legacy.reserve(current.size() - static_cast<std::size_t>(removedBytes));
		if (targetVersion == TomCat::RuntimeCompatibility::OldestSupportedTcpakVersion)
		{
			legacy.insert(legacy.end(), current.begin(),
				current.begin() + TomCat::RuntimeCompatibility::TcpakV5BaseHeaderSize);
			legacy.insert(legacy.end(), current.begin() + sceneOffset,
				current.begin() + currentHeaderSize);
		}
		else
		{
			legacy.insert(legacy.end(), current.begin(),
				current.begin() + currentHeaderSize);
		}
		for (uint64_t index = 0; index < entryCount; ++index)
		{
			const std::size_t sourceEntryOffset = currentHeaderSize
				+ static_cast<std::size_t>(index * currentEntrySize);
			const std::size_t targetEntryOffset = legacy.size();
			legacy.insert(legacy.end(), current.begin() + sourceEntryOffset,
				current.begin() + sourceEntryOffset + targetEntrySize);
			const uint64_t dataOffset =
				ReadLittleEndian64(legacy, targetEntryOffset + 16);
			Require(dataOffset >= removedBytes,
				"current tcpak index cannot be rebased to its legacy layout");
			WriteLittleEndian64(legacy, targetEntryOffset + 16,
				dataOffset - removedBytes);
		}
		legacy.insert(legacy.end(), current.begin()
			+ static_cast<std::size_t>(currentDataStart), current.end());
		WriteLittleEndian32(legacy, 8, targetVersion);
		WriteLittleEndian32(legacy, 12, targetHeaderSize);
		return legacy;
	}

	std::vector<uint8_t> MakeTcpakV5CompatibilityFixture(
		const std::vector<uint8_t>& current)
	{
		return MakeLegacyTcpakCompatibilityFixture(current,
			TomCat::RuntimeCompatibility::OldestSupportedTcpakVersion);
	}

	std::vector<uint8_t> MakeTcpakV6CompatibilityFixture(
		const std::vector<uint8_t>& current)
	{
		return MakeLegacyTcpakCompatibilityFixture(current,
			TomCat::RuntimeCompatibility::TcpakBootManifestVersion);
	}

	bool Near(float actual, float expected, float tolerance = 1.0e-4f)
	{
		return std::abs(actual - expected) <= tolerance;
	}

	bool Near(const glm::vec2& actual, const glm::vec2& expected,
		float tolerance = 1.0e-4f)
	{
		return Near(actual.x, expected.x, tolerance)
			&& Near(actual.y, expected.y, tolerance);
	}

	bool Near(const glm::vec3& actual, const glm::vec3& expected,
		float tolerance = 1.0e-4f)
	{
		return Near(actual.x, expected.x, tolerance)
			&& Near(actual.y, expected.y, tolerance)
			&& Near(actual.z, expected.z, tolerance);
	}

	bool Near(const glm::mat4& actual, const glm::mat4& expected,
		float tolerance = 1.0e-4f)
	{
		for (glm::length_t column = 0; column < 4; ++column)
			for (glm::length_t row = 0; row < 4; ++row)
				if (!Near(actual[column][row], expected[column][row], tolerance))
					return false;
		return true;
	}

	bool Contains(const TomCat::CollisionEnter2D& event, TomCat::UUID entityID)
	{
		return event.EntityA == entityID || event.EntityB == entityID;
	}

	bool Contains(const TomCat::CollisionExit2D& event, TomCat::UUID entityID)
	{
		return event.EntityA == entityID || event.EntityB == entityID;
	}

	bool Contains(const TomCat::TriggerEnter2D& event, TomCat::UUID entityID)
	{
		return event.EntityA == entityID || event.EntityB == entityID;
	}

	bool Contains(const TomCat::TriggerExit2D& event, TomCat::UUID entityID)
	{
		return event.EntityA == entityID || event.EntityB == entityID;
	}

	class TemporaryCookedProject final
	{
	public:
		TemporaryCookedProject()
		{
			TomCat::AssetManager::Get().Shutdown();
			Root = std::filesystem::temp_directory_path()
				/ ("tomcat_physics_cooked_"
					+ std::to_string(static_cast<uint64_t>(TomCat::UUID())));
		}

		~TemporaryCookedProject()
		{
			// A mounted package owns an open file handle on Windows, so always close
			// the process-global asset manager before removing this unique test tree.
			TomCat::AssetManager::Get().Shutdown();
			std::error_code error;
			std::filesystem::remove_all(Root, error);
		}

		std::filesystem::path Root;
	};

	void TestExplicitDotNetRootIsExclusive()
	{
		TemporaryCookedProject environment;
		std::filesystem::create_directories(environment.Root / "Managed");
		const std::filesystem::path runtimeConfig =
			environment.Root / "Managed" / "TomCat.ScriptHost.runtimeconfig.json";
		const std::filesystem::path scriptHost =
			environment.Root / "Managed" / "TomCat.ScriptHost.dll";
		WriteTextFile(runtimeConfig, "{}");
		WriteTextFile(scriptHost, "not loaded because the private runtime is missing");

		TomCat::DotNetHost host;
		TomCat::DotNetHost::Configuration configuration;
		configuration.RuntimeConfigPath = runtimeConfig;
		configuration.ScriptHostAssemblyPath = scriptHost;
		configuration.DotNetRoot = environment.Root / "MissingPrivateDotNet";
		Require(!host.Initialize(configuration)
			&& host.GetLastError().find("explicitly selected private dotnet root")
				!= std::string::npos,
			"an explicit Player dotnet root fell back to a global .NET installation");
	}

	void TestProjectSettingsPersistenceAndValidation()
	{
		TemporaryCookedProject environment;
		TomCat::ProjectConfig config;
		config.Name = "Project Settings Regression";
		config.Template = "2D";
		config.AssetDirectory = "Assets";
		config.StartScene = "Main.tomcat";
		const std::filesystem::path projectPath = environment.Root / "Project.tcproj";
		auto project = TomCat::Project::CreateNew(projectPath, config);
		Require(project != nullptr, "CreateNew rejected a valid project settings fixture");

		const TomCat::ProjectSettings defaults;
		Require(project->GetSettings() == defaults,
			"CreateNew did not initialize default project settings");
		Require(project->GetSettingsPath()
			== environment.Root / "ProjectSettings" / "ProjectSettings.json",
			"project settings path is not the project-root ProjectSettings document");
		Require(std::filesystem::is_regular_file(project->GetSettingsPath()),
			"CreateNew did not persist default project settings");
		for (std::size_t layer = 0; layer < TomCat::Physics2DLayerCount; ++layer)
			Require(defaults.Physics2D.CollisionMasks[layer] == 0xffff,
				"default collision matrix is not all-on");

		const std::string projectDocument = ReadTextFile(projectPath);
		Require(projectDocument.find("SchemaVersion: 4") != std::string::npos,
			"Project.tcproj did not use schema v4");
		Require(projectDocument.find("TagsAndLayers") == std::string::npos
			&& projectDocument.find("Physics2D") == std::string::npos
			&& projectDocument.find("StartScene") == std::string::npos,
			"shared settings or legacy StartScene fields leaked into Project.tcproj");
		Require(project->GetBuildSettingsPath()
			== environment.Root / "ProjectSettings" / "BuildSettings.json"
			&& std::filesystem::is_regular_file(project->GetBuildSettingsPath())
			&& project->GetBuildSettings().Scenes.empty()
			&& static_cast<uint64_t>(project->GetBuildSettings().EntrySceneHandle) == 0,
			"CreateNew did not persist default shared BuildSettings");

		TomCat::BuildSettings buildSettings;
		buildSettings.EntrySceneHandle = TomCat::AssetHandle(101);
		buildSettings.Scenes = {
			{ TomCat::AssetHandle(101), true, "Main.tomcat" },
			{ TomCat::AssetHandle(202), false, "Bonus.tomcat" }
		};
		Require(project->SetBuildSettings(buildSettings),
			"SetBuildSettings rejected a valid ordered scene list");
		const std::string validBuildSettingsDocument =
			ReadTextFile(project->GetBuildSettingsPath());
		Require(validBuildSettingsDocument.find("\"schemaVersion\": 1") != std::string::npos
			&& validBuildSettingsDocument.find("\"entrySceneHandle\": 101") != std::string::npos
			&& validBuildSettingsDocument.find("\"enabled\": false") != std::string::npos
			&& validBuildSettingsDocument.find("\"pathHint\": \"Bonus.tomcat\"") != std::string::npos,
			"BuildSettings writer did not emit the strict shared JSON schema");
		auto buildRoundtrip = TomCat::Project::Load(projectPath);
		Require(buildRoundtrip != nullptr
			&& buildRoundtrip->GetBuildSettings() == buildSettings
			&& buildRoundtrip->GetConfig().StartSceneHandle == buildSettings.EntrySceneHandle
			&& buildRoundtrip->GetConfig().StartScene == "Main.tomcat",
			"BuildSettings order, flags, PathHints, or compatibility mirror did not roundtrip");

		auto requireBuildRejected = [&](TomCat::BuildSettings invalid, const char* message)
		{
			const TomCat::BuildSettings before = project->GetBuildSettings();
			const std::string fileBefore = ReadTextFile(project->GetBuildSettingsPath());
			Require(!project->SetBuildSettings(invalid), message);
			Require(project->GetBuildSettings() == before
				&& ReadTextFile(project->GetBuildSettingsPath()) == fileBefore,
				"rejected BuildSettings changed in-memory or persisted state");
		};
		TomCat::BuildSettings invalidBuild = buildSettings;
		invalidBuild.Scenes.push_back(invalidBuild.Scenes.front());
		requireBuildRejected(invalidBuild, "SetBuildSettings accepted a duplicate scene handle");
		invalidBuild = buildSettings;
		invalidBuild.EntrySceneHandle = TomCat::AssetHandle(303);
		requireBuildRejected(invalidBuild, "SetBuildSettings accepted an entry absent from scenes");
		invalidBuild = buildSettings;
		invalidBuild.Scenes.front().Enabled = false;
		requireBuildRejected(invalidBuild, "SetBuildSettings accepted a disabled entry scene");
		invalidBuild = buildSettings;
		invalidBuild.Scenes.front().PathHint = "../Outside.tomcat";
		requireBuildRejected(invalidBuild, "SetBuildSettings accepted an escaping PathHint");

		std::string unknownBuildField = validBuildSettingsDocument;
		const std::size_t buildRootClose = unknownBuildField.rfind("\n}");
		Require(buildRootClose != std::string::npos,
			"could not locate BuildSettings JSON root terminator");
		unknownBuildField.replace(buildRootClose, 2,
			",\n  \"unexpected\": true\n}");
		WriteTextFile(project->GetBuildSettingsPath(), unknownBuildField);
		Require(TomCat::Project::Load(projectPath) == nullptr,
			"strict BuildSettings loader accepted an unknown top-level field");
		WriteTextFile(project->GetBuildSettingsPath(), validBuildSettingsDocument + "trailing-data");
		Require(TomCat::Project::Load(projectPath) == nullptr,
			"BuildSettings loader accepted invalid JSON syntax");
		WriteTextFile(project->GetBuildSettingsPath(), validBuildSettingsDocument);
		std::error_code buildRemoveError;
		Require(std::filesystem::remove(project->GetBuildSettingsPath(), buildRemoveError)
			&& !buildRemoveError && TomCat::Project::Load(projectPath) == nullptr,
			"schema-v4 project loaded without authoritative BuildSettings.json");
		WriteTextFile(project->GetBuildSettingsPath(), validBuildSettingsDocument);

		TomCat::EditorProjectState editorState;
		editorState.ContentBrowserCurrentDirectory = "@assets/Scripts";
		editorState.ContentBrowserExpandedNodes = { "@assets", "@assets/Scripts" };
		editorState.ExternalScriptEditor = std::filesystem::absolute(
			environment.Root / "Tools" / "Code Editor.EXE").lexically_normal();
		Require(project->SaveEditorState(editorState),
			"SaveEditorState rejected a valid absolute external C# editor path");
		TomCat::EditorProjectState loadedEditorState;
		Require(project->LoadEditorState(loadedEditorState)
			== TomCat::EditorProjectStateLoadResult::Loaded,
			"LoadEditorState could not reload project-local editor settings");
		Require(loadedEditorState.ExternalScriptEditor == editorState.ExternalScriptEditor
			&& loadedEditorState.ContentBrowserCurrentDirectory == "@assets/Scripts"
			&& loadedEditorState.ContentBrowserExpandedNodes == editorState.ContentBrowserExpandedNodes,
			"project-local editor settings did not preserve the configured C# editor and browser state");
		const std::filesystem::path editorStatePath = project->GetUserSettingsPath()
			/ "editor.json";
		const std::string validEditorStateDocument = ReadTextFile(editorStatePath);
		Require(validEditorStateDocument.find("\"externalScriptEditor\":")
			!= std::string::npos
			&& ReadTextFile(projectPath) == projectDocument,
			"external C# editor persistence was not isolated to UserSettings/editor.json");
		TomCat::EditorProjectState invalidEditorState = editorState;
		invalidEditorState.ExternalScriptEditor = "relative-editor.exe";
		Require(!project->SaveEditorState(invalidEditorState)
			&& ReadTextFile(editorStatePath) == validEditorStateDocument,
			"SaveEditorState accepted a relative C# editor path or modified the last valid state");

		TomCat::ProjectSettings customized;
		customized.TagsAndLayers.Tags = { "Untagged", "Player", "Enemy" };
		customized.TagsAndLayers.LayerNames[1] = "Player";
		customized.TagsAndLayers.LayerNames[2] = "Enemy";
		customized.Physics2D.SetLayersCollide(1, 2, false);
		Require(project->SetSettings(customized),
			"SetSettings rejected valid tags, layers, and symmetric matrix data");
		Require(project->GetSettings() == customized,
			"SetSettings did not update the in-memory project settings");
		Require(project->SaveSettings(), "SaveSettings rejected valid settings");

		auto loaded = TomCat::Project::Load(projectPath);
		Require(loaded != nullptr && loaded->GetSettings() == customized,
			"Project::Load did not roundtrip separately persisted settings");
		const std::string validSettingsDocument = ReadTextFile(project->GetSettingsPath());
		Require(validSettingsDocument.find("\"schemaVersion\": 2") != std::string::npos
			&& validSettingsDocument.find("\"tagsAndLayers\": {") != std::string::npos
			&& validSettingsDocument.find("\"physics2D\": {") != std::string::npos
			&& validSettingsDocument.find("SchemaVersion:") == std::string::npos,
			"settings writer did not emit its strict lowerCamel JSON schema");

		auto requireRejected = [&](const TomCat::ProjectSettings& invalid, const char* message)
		{
			const TomCat::ProjectSettings before = project->GetSettings();
			const std::string fileBefore = ReadTextFile(project->GetSettingsPath());
			Require(!project->SetSettings(invalid), message);
			Require(project->GetSettings() == before,
				"rejected settings changed the in-memory project state");
			Require(ReadTextFile(project->GetSettingsPath()) == fileBefore,
				"rejected settings changed the persisted project state");
		};

		TomCat::ProjectSettings invalid = customized;
		invalid.TagsAndLayers.Tags[0] = "NotUntagged";
		requireRejected(invalid, "SetSettings accepted a missing reserved Untagged tag");
		invalid = customized;
		invalid.TagsAndLayers.Tags.push_back("");
		requireRejected(invalid, "SetSettings accepted an empty tag");
		invalid = customized;
		invalid.TagsAndLayers.Tags.push_back("Player");
		requireRejected(invalid, "SetSettings accepted a duplicate tag");
		invalid = customized;
		invalid.TagsAndLayers.LayerNames[0] = "NotDefault";
		requireRejected(invalid, "SetSettings accepted a renamed reserved Default layer");
		invalid = customized;
		invalid.TagsAndLayers.LayerNames[3] = "Player";
		requireRejected(invalid, "SetSettings accepted a duplicate nonempty layer name");
		invalid = customized;
		invalid.Physics2D.CollisionMasks[1] |= uint16_t(1) << 2;
		requireRejected(invalid, "SetSettings accepted an asymmetric collision matrix");

		std::string unknownField = validSettingsDocument;
		const std::size_t rootClose = unknownField.rfind("\n}");
		Require(rootClose != std::string::npos,
			"could not locate the project settings JSON root terminator");
		unknownField.replace(rootClose, 2, ",\n  \"unexpected\": true\n}");
		WriteTextFile(project->GetSettingsPath(), unknownField);
		Require(TomCat::Project::Load(projectPath) == nullptr,
			"strict settings loader accepted an unknown top-level field");
		WriteTextFile(project->GetSettingsPath(), validSettingsDocument + "trailing-data");
		Require(TomCat::Project::Load(projectPath) == nullptr,
			"settings loader accepted invalid JSON syntax");
		WriteTextFile(project->GetSettingsPath(), validSettingsDocument);
		std::string wrongSchema = validSettingsDocument;
		const std::size_t schemaPosition = wrongSchema.find("\"schemaVersion\": 2");
		Require(schemaPosition != std::string::npos,
			"could not locate project settings schema version");
		wrongSchema.replace(schemaPosition, std::string("\"schemaVersion\": 2").size(),
			"\"schemaVersion\": 3");
		WriteTextFile(project->GetSettingsPath(), wrongSchema);
		Require(TomCat::Project::Load(projectPath) == nullptr,
			"settings loader accepted an unsupported schema version");
		WriteTextFile(project->GetSettingsPath(), validSettingsDocument);

		const std::filesystem::path legacySettingsPath = environment.Root
			/ "ProjectSettings" / "ProjectSettings.tcsettings";
		std::ostringstream legacySettings;
		legacySettings << "SchemaVersion: 1\n"
			<< "TagsAndLayers:\n"
			<< "  Tags: [Untagged, Player, Enemy]\n"
			<< "  LayerNames: [";
		for (std::size_t index = 0; index < customized.TagsAndLayers.LayerNames.size(); ++index)
		{
			if (index != 0)
				legacySettings << ", ";
			legacySettings << '"' << customized.TagsAndLayers.LayerNames[index] << '"';
		}
		legacySettings << "]\n"
			<< "Physics2D:\n"
			<< "  CollisionMasks: [";
		for (std::size_t index = 0; index < customized.Physics2D.CollisionMasks.size(); ++index)
		{
			if (index != 0)
				legacySettings << ", ";
			legacySettings << customized.Physics2D.CollisionMasks[index];
		}
		legacySettings << "]\n";
		WriteTextFile(legacySettingsPath, legacySettings.str());

		WriteTextFile(project->GetSettingsPath(), "{ invalid JSON");
		Require(TomCat::Project::Load(projectPath) == nullptr,
			"a valid legacy settings file hid a damaged authoritative JSON file");

		std::error_code removeError;
		Require(std::filesystem::remove(project->GetSettingsPath(), removeError) && !removeError,
			"could not remove JSON settings fixture for legacy compatibility test");
		auto legacySettingsProject = TomCat::Project::Load(projectPath);
		Require(legacySettingsProject != nullptr
			&& legacySettingsProject->GetSettings() == customized,
			"missing JSON settings did not fall back to valid legacy settings");
		Require(!std::filesystem::exists(project->GetSettingsPath()),
			"loading legacy settings unexpectedly rewrote the project");
		Require(legacySettingsProject->SaveSettings(),
			"SaveSettings could not migrate loaded legacy data to JSON");
		Require(std::filesystem::is_regular_file(project->GetSettingsPath()),
			"SaveSettings did not create the authoritative JSON settings file");
		auto migratedSettingsProject = TomCat::Project::Load(projectPath);
		Require(migratedSettingsProject != nullptr
			&& migratedSettingsProject->GetSettings() == customized,
			"project settings changed while legacy data was saved as JSON");

		removeError.clear();
		Require(std::filesystem::remove(project->GetSettingsPath(), removeError) && !removeError,
			"could not remove JSON settings fixture for missing-file test");
		removeError.clear();
		Require(std::filesystem::remove(legacySettingsPath, removeError) && !removeError,
			"could not remove legacy settings fixture for missing-file test");
		auto missingSettings = TomCat::Project::Load(projectPath);
		Require(missingSettings != nullptr && missingSettings->GetSettings() == defaults,
			"missing project settings did not load backward-compatible defaults");
		Require(project->SaveSettings(), "could not restore settings after missing-file test");
	}

	void TestPlayerSettingsPersistenceAndValidation()
	{
		TemporaryCookedProject environment;
		TomCat::ProjectConfig config;
		config.Name = "Player Settings Migration";
		config.Version = "2.4.6";
		config.Template = "2D";
		config.AssetDirectory = "Assets";
		const std::filesystem::path projectPath = environment.Root / "Project.tcproj";
		auto project = TomCat::Project::CreateNew(projectPath, config);
		Require(project != nullptr
			&& project->GetPlayerSettingsPath() == environment.Root
				/ "ProjectSettings" / "PlayerSettings.json"
			&& std::filesystem::is_regular_file(project->GetPlayerSettingsPath())
			&& project->GetPlayerSettings().ProductName == config.Name
			&& project->GetPlayerSettings().Version == config.Version,
			"CreateNew did not persist PlayerSettings defaults from the project identity");

		TomCat::PlayerSettings customized = project->GetPlayerSettings();
		customized.ProductName = "Moon Rabbit";
		customized.CompanyName = "Example Studio";
		customized.Version = "3.1.4";
		customized.Icon = TomCat::AssetHandle(987654321);
		customized.Width = 1600;
		customized.Height = 900;
		customized.WindowMode = TomCat::PlayerWindowMode::Borderless;
		customized.Resizable = false;
		customized.VSync = false;
		customized.SaveDirectory = "Data/Saves";
		customized.LogDirectory = "Diagnostics/Logs";
		customized.CrashDirectory = "Diagnostics/Crashes";
		Require(project->SetPlayerSettings(customized),
			"SetPlayerSettings rejected a valid complete settings document");
		const std::string validDocument = ReadTextFile(project->GetPlayerSettingsPath());
		Require(validDocument.find("\"schemaVersion\": 1") != std::string::npos
			&& validDocument.find("\"productName\": \"Moon Rabbit\"") != std::string::npos
			&& validDocument.find("\"windowMode\": \"Borderless\"") != std::string::npos
			&& validDocument.find("\"save\": \"Data/Saves\"") != std::string::npos,
			"PlayerSettings writer did not emit its strict versioned JSON schema");
		auto roundtrip = TomCat::Project::Load(projectPath);
		Require(roundtrip != nullptr && roundtrip->GetPlayerSettings() == customized,
			"PlayerSettings fields did not roundtrip through Project::Load");

		auto requireRejected = [&](TomCat::PlayerSettings invalid, const char* message)
		{
			const TomCat::PlayerSettings before = project->GetPlayerSettings();
			const std::string fileBefore = ReadTextFile(project->GetPlayerSettingsPath());
			Require(!project->SetPlayerSettings(invalid), message);
			Require(project->GetPlayerSettings() == before
				&& ReadTextFile(project->GetPlayerSettingsPath()) == fileBefore,
				"rejected PlayerSettings changed in-memory or persisted state");
		};
		TomCat::PlayerSettings invalid = customized;
		invalid.ProductName.clear();
		requireRejected(invalid, "PlayerSettings accepted an empty ProductName");
		invalid = customized;
		invalid.Width = 0;
		requireRejected(invalid, "PlayerSettings accepted an invalid window width");
		invalid = customized;
		invalid.SaveDirectory = "../Outside";
		requireRejected(invalid, "PlayerSettings accepted an escaping save directory");

		std::string unknownField = validDocument;
		const std::size_t rootClose = unknownField.rfind("\n}");
		Require(rootClose != std::string::npos,
			"could not locate PlayerSettings JSON root terminator");
		unknownField.replace(rootClose, 2, ",\n  \"unexpected\": true\n}");
		WriteTextFile(project->GetPlayerSettingsPath(), unknownField);
		Require(TomCat::Project::Load(projectPath) == nullptr,
			"strict PlayerSettings loader accepted an unknown top-level field");
		std::string wrongMode = validDocument;
		const std::string modeToken = "\"windowMode\": \"Borderless\"";
		const std::size_t modePosition = wrongMode.find(modeToken);
		Require(modePosition != std::string::npos,
			"could not locate PlayerSettings window mode token");
		wrongMode.replace(modePosition, modeToken.size(),
			"\"windowMode\": \"Maximized\"");
		WriteTextFile(project->GetPlayerSettingsPath(), wrongMode);
		Require(TomCat::Project::Load(projectPath) == nullptr,
			"strict PlayerSettings loader accepted an unknown WindowMode");
		WriteTextFile(project->GetPlayerSettingsPath(), validDocument + "trailing-data");
		Require(TomCat::Project::Load(projectPath) == nullptr,
			"PlayerSettings loader accepted trailing non-JSON data");
		WriteTextFile(project->GetPlayerSettingsPath(), validDocument);

		std::error_code removeError;
		Require(std::filesystem::remove(project->GetPlayerSettingsPath(), removeError)
			&& !removeError, "could not remove PlayerSettings migration fixture");
		auto inspected = TomCat::Project::Inspect(projectPath);
		Require(inspected != nullptr
			&& inspected->GetPlayerSettings().ProductName == config.Name
			&& inspected->GetPlayerSettings().Version == config.Version
			&& !std::filesystem::exists(project->GetPlayerSettingsPath()),
			"read-only project inspection wrote PlayerSettings migration output");
		TomCat::ProjectMigrationPreview migrationPreview;
		std::string migrationPreviewError;
		Require(TomCat::Project::PreviewMigration(
			projectPath, migrationPreview, migrationPreviewError),
			"missing PlayerSettings migration preview failed");
		Require(TomCat::Project::Load(projectPath) == nullptr
			&& !std::filesystem::exists(project->GetPlayerSettingsPath()),
			"default Project::Load implicitly created missing PlayerSettings");
		auto migrated = TomCat::Project::LoadWithMigration(
			projectPath, migrationPreview);
		Require(migrated != nullptr
			&& migrated->GetPlayerSettings().ProductName == config.Name
			&& migrated->GetPlayerSettings().Version == config.Version
			&& std::filesystem::is_regular_file(project->GetPlayerSettingsPath()),
			"approved migration did not create missing PlayerSettings defaults");
	}

	void TestLegacyProjectBuildSettingsMigration()
	{
		TemporaryCookedProject environment;
		std::filesystem::create_directories(environment.Root / "Assets");
		const std::filesystem::path projectPath = environment.Root / "Legacy.tcproj";
		constexpr uint64_t legacyHandle = 0xf123456789abcdefULL;
		std::ostringstream legacyProject;
		legacyProject
			<< "SchemaVersion: 3\n"
			<< "Project:\n"
			<< "  Name: Legacy Build Settings\n"
			<< "  Version: 1.0.0\n"
			<< "  Description: ''\n"
			<< "  EditorVersion: ''\n"
			<< "  Template: 2D\n"
			<< "  AssetDirectory: Assets\n"
			<< "  StartScene: Scenes/Legacy.tomcat\n"
			<< "  StartSceneHandle: " << legacyHandle << "\n";
		WriteTextFile(projectPath, legacyProject.str());

		TomCat::ProjectMigrationPreview migrationPreview;
		std::string migrationPreviewError;
		Require(TomCat::Project::PreviewMigration(
			projectPath, migrationPreview, migrationPreviewError),
			"legacy project migration preview failed");
		Require(TomCat::Project::Load(projectPath) == nullptr
			&& ReadTextFile(projectPath) == legacyProject.str()
			&& !std::filesystem::exists(
				environment.Root / "ProjectSettings" / "BuildSettings.json"),
			"default Project::Load implicitly migrated a schema-v3 project");
		auto migrated = TomCat::Project::LoadWithMigration(
			projectPath, migrationPreview);
		Require(migrated != nullptr,
			"approved schema-v3 project migration was rejected");
		const TomCat::BuildSettings& build = migrated->GetBuildSettings();
		Require(build.EntrySceneHandle == TomCat::AssetHandle(legacyHandle)
			&& build.Scenes.size() == 1
			&& build.Scenes[0].Handle == TomCat::AssetHandle(legacyHandle)
			&& build.Scenes[0].Enabled
			&& build.Scenes[0].PathHint == "Scenes/Legacy.tomcat",
			"legacy StartSceneHandle did not migrate into authoritative BuildSettings");
		const std::string migratedProject = ReadTextFile(projectPath);
		Require(migratedProject.find("SchemaVersion: 4") != std::string::npos
			&& migratedProject.find("StartScene") == std::string::npos,
			"legacy project was not rewritten without duplicate StartScene truth");
		const std::string migratedBuild = ReadTextFile(migrated->GetBuildSettingsPath());
		Require(migratedBuild.find(std::to_string(legacyHandle)) != std::string::npos,
			"BuildSettings migration lost an unsigned 64-bit scene handle");
		auto reloaded = TomCat::Project::Load(projectPath);
		Require(reloaded != nullptr && reloaded->GetBuildSettings() == build,
			"migrated schema-v4 project did not reload its BuildSettings unchanged");
	}

	TomCat::Entity AddCircleBody(TomCat::Scene& scene, const char* name,
		TomCat::Rigidbody2D::BodyType bodyType, const glm::vec2& position,
		float radius = 0.5f)
	{
		TomCat::Entity entity = scene.CreateEntity(name);
		auto& transform = entity.GetComponent<TomCat::Transform>();
		transform._Translation = { position.x, position.y, 0.0f };
		transform._LocalTranslation = transform._Translation;
		auto& body = entity.AddComponent<TomCat::Rigidbody2D>();
		body.Type = bodyType;
		entity.AddComponent<TomCat::CircleCollider2D>().Radius = radius;
		return entity;
	}

	void AttachManagedProbe(TomCat::Entity entity, uint64_t scriptAsset)
	{
		TomCat::CSharpScriptEntry script;
		script.AttachmentID = TomCat::UUID();
		script.ScriptAsset = TomCat::AssetHandle(scriptAsset);
		script.LastKnownClassName = "Game.PhysicsRegressionProbe";
		entity.AddComponent<TomCat::CSharpScripts>().Scripts.push_back(std::move(script));
	}

	void TestManagedTransformSettersRespectHierarchy()
	{
		auto runtime = std::make_shared<ManagedRuntimeProbe>();
		ScriptRuntimeOverride runtimeOverride(runtime);
		TomCat::Scene scene;
		TomCat::Entity parent = scene.CreateEntity("Managed transform parent");
		TomCat::Entity child = scene.CreateEntity("Managed transform child");

		const glm::mat4 parentWorld = TomCat::Math::ComposeTransform(
			{ 10.0f, -4.0f, 2.0f }, { 0.0f, 0.0f, 0.35f },
			{ 2.0f, 2.0f, 2.0f });
		const glm::mat4 childWorld = TomCat::Math::ComposeTransform(
			{ 3.0f, 5.0f, -1.0f }, { 0.0f, 0.0f, -0.2f },
			{ 0.75f, 0.75f, 0.75f });
		Require(scene.SetWorldTransform(parent, parentWorld)
			&& scene.SetWorldTransform(child, childWorld)
			&& scene.SetParent(child, parent),
			"could not create the managed Transform hierarchy fixture");
		AttachManagedProbe(child, 8110);
		Require(scene.OnRuntimeStart(),
			"managed Transform hierarchy fixture did not start");

		const TomCat::Scripting::NativeApiV1 api =
			TomCat::Scripting::BuildNativeApiV1();
		const TomCat::Scripting::EntityHandleV1 handle{
			runtime->LastSceneSession,
			static_cast<uint64_t>(child.GetUUID()),
			runtime->LastRuntimeGeneration
		};
		auto requireSynchronized = [&]()
		{
			const auto& parentTransform = parent.GetComponent<TomCat::Transform>();
			const auto& childTransform = child.GetComponent<TomCat::Transform>();
			Require(Near(parentTransform.GetTransform() * childTransform.GetLocalTransform(),
				childTransform.GetTransform(), 3.0e-4f),
				"managed world Transform setter left child local/world values inconsistent");
		};
		requireSynchronized();

		const glm::vec3 requestedPosition{ 20.0f, 6.0f, -2.5f };
		Require(api.TransformSetPosition(handle,
			{ requestedPosition.x, requestedPosition.y, requestedPosition.z }) == 0,
			"managed Transform.position setter rejected a live child Entity");
		TomCat::Scripting::ScriptEngine::Get().FlushDeferredCommands(
			runtime->LastSceneSession);
		Require(Near(child.GetComponent<TomCat::Transform>()._Translation,
			requestedPosition),
			"managed Transform.position setter did not update world position");
		requireSynchronized();

		const glm::vec3 requestedRotation{ 0.0f, 0.0f, -0.65f };
		Require(api.TransformSetRotationEuler(handle,
			{ requestedRotation.x, requestedRotation.y, requestedRotation.z }) == 0,
			"managed Transform.rotation setter rejected a live child Entity");
		TomCat::Scripting::ScriptEngine::Get().FlushDeferredCommands(
			runtime->LastSceneSession);
		Require(Near(child.GetComponent<TomCat::Transform>()._Rotation,
			requestedRotation, 3.0e-4f),
			"managed Transform.rotation setter did not update world rotation");
		requireSynchronized();

		const glm::vec3 requestedScale{ 1.25f, 1.25f, 1.25f };
		Require(api.TransformSetScale(handle,
			{ requestedScale.x, requestedScale.y, requestedScale.z }) == 0,
			"managed Transform.scale setter rejected a live child Entity");
		TomCat::Scripting::ScriptEngine::Get().FlushDeferredCommands(
			runtime->LastSceneSession);
		Require(Near(child.GetComponent<TomCat::Transform>()._Scale,
			requestedScale, 3.0e-4f),
			"managed Transform.scale setter did not update world scale");
		requireSynchronized();

		TomCat::Scripting::NativeVector3 readback{};
		Require(api.TransformGetPosition(handle, &readback) == 0
			&& Near(glm::vec3(readback.X, readback.Y, readback.Z), requestedPosition),
			"managed Transform getter did not return the synchronized world position");
		scene.OnRuntimeStop();
	}

	void MoveRuntimeBody(TomCat::Entity entity, const glm::vec2& position)
	{
		auto* body = static_cast<b2Body*>(entity.GetComponent<TomCat::Rigidbody2D>().RuntimeBody);
		Require(body != nullptr, "entity has no runtime body");
		body->SetTransform({ position.x, position.y }, body->GetAngle());
	}

	void TestFixedAccumulatorAndStep()
	{
		auto runtime = std::make_shared<ManagedRuntimeProbe>();
		ScriptRuntimeOverride runtimeOverride(runtime);
		TomCat::Scene scene;
		TomCat::Entity entity = scene.CreateEntity("Fixed step probe");
		AttachManagedProbe(entity, 8101);
		Require(scene.OnRuntimeStart(), "managed fixed-step probe did not start");

		scene.OnUpdateRuntime(TomCat::Timestep(1.0f / 120.0f));
		Require(runtime->FixedUpdateCount == 0, "half step advanced simulation early");
		scene.OnUpdateRuntime(TomCat::Timestep(1.0f / 120.0f));
		Require(runtime->FixedUpdateCount == 1,
			"two half steps did not produce one managed fixed update");
		Require(Near(runtime->LastFixedDelta, TomCat::Scene::FixedRuntimeTimestep),
			"managed script did not receive the fixed timestep");

		scene.OnUpdateRuntime(TomCat::Timestep(1.0f));
		Require(runtime->FixedUpdateCount == 1 + TomCat::Scene::MaximumRuntimeSubsteps,
			"hitch was not capped at the maximum substep count");
		scene.OnUpdateRuntime(TomCat::Timestep(0.0f));
		Require(runtime->FixedUpdateCount == 1 + TomCat::Scene::MaximumRuntimeSubsteps,
			"overdue whole steps were not discarded after substep cap");

		scene.OnRuntimeStep();
		Require(runtime->FixedUpdateCount == 2 + TomCat::Scene::MaximumRuntimeSubsteps,
			"single-step did not advance exactly one managed fixed update");
		scene.OnRuntimeStop();
		Require(!runtime->Calls.empty() && runtime->Calls.back() == "DestroyAll",
			"managed fixed-step runtime was not destroyed on stop");
	}

	struct FallingBodyResult
	{
		float PositionY = 0.0f;
		float VelocityY = 0.0f;
	};

	FallingBodyResult SimulateFallingBody(int displayHz, int seconds)
	{
		TomCat::Scene scene;
		TomCat::Entity entity = AddCircleBody(scene, "Falling body",
			TomCat::Rigidbody2D::BodyType::Dynamic, { 0.0f, 0.0f });
		scene.OnRuntimeStart();
		for (int frame = 0; frame < displayHz * seconds; ++frame)
			scene.OnUpdateRuntime(TomCat::Timestep(1.0f / static_cast<float>(displayHz)));

		auto* body = static_cast<b2Body*>(entity.GetComponent<TomCat::Rigidbody2D>().RuntimeBody);
		Require(body != nullptr, "falling body did not receive a runtime body");
		const FallingBodyResult result{ body->GetPosition().y, body->GetLinearVelocity().y };
		scene.OnRuntimeStop();
		return result;
	}

	void CompareFrameRatePhysics(int simulationSeconds)
	{
		const FallingBodyResult at30Hz = SimulateFallingBody(30, simulationSeconds);
		const FallingBodyResult at60Hz = SimulateFallingBody(60, simulationSeconds);
		const FallingBodyResult at144Hz = SimulateFallingBody(144, simulationSeconds);
		Require(Near(at30Hz.PositionY, at60Hz.PositionY) && Near(at30Hz.VelocityY, at60Hz.VelocityY),
			"30Hz and 60Hz produced different physics results");
		Require(Near(at30Hz.PositionY, at144Hz.PositionY) && Near(at30Hz.VelocityY, at144Hz.VelocityY),
			"30Hz and 144Hz produced different physics results");
	}

	void TestFrameRateIndependentPhysics()
	{
		CompareFrameRatePhysics(1);
		CompareFrameRatePhysics(10);
	}

	void TestIncrementalPhysicsSynchronizationPreservesObjectIdentity()
	{
		TomCat::Scene scene;
		TomCat::Entity changed = AddCircleBody(scene, "Incremental changed",
			TomCat::Rigidbody2D::BodyType::Dynamic, { -5.0f, 0.0f });
		TomCat::Entity untouched = AddCircleBody(scene, "Incremental untouched",
			TomCat::Rigidbody2D::BodyType::Dynamic, { 5.0f, 0.0f });
		Require(scene.OnRuntimeStart(), "incremental physics scene did not start");

		void* changedBody = changed.GetComponent<TomCat::Rigidbody2D>().RuntimeBody;
		void* untouchedBody = untouched.GetComponent<TomCat::Rigidbody2D>().RuntimeBody;
		void* untouchedFixture =
			untouched.GetComponent<TomCat::CircleCollider2D>().RuntimeFixture;
		Require(changedBody && untouchedBody && untouchedFixture,
			"incremental identity fixtures were not materialized");

		changed.GetComponent<TomCat::CircleCollider2D>().Radius = 0.75f;
		scene.OnRuntimeStep();
		Require(changed.GetComponent<TomCat::Rigidbody2D>().RuntimeBody == changedBody,
			"collider edit replaced its owning Box2D body");
		Require(untouched.GetComponent<TomCat::Rigidbody2D>().RuntimeBody == untouchedBody
			&& untouched.GetComponent<TomCat::CircleCollider2D>().RuntimeFixture
				== untouchedFixture,
			"collider edit replaced an unrelated body or fixture");

		auto& box = changed.AddComponent<TomCat::BoxCollider2D>();
		box.Size = { 0.25f, 0.5f };
		scene.OnRuntimeStep();
		Require(changed.GetComponent<TomCat::Rigidbody2D>().RuntimeBody == changedBody
			&& untouched.GetComponent<TomCat::Rigidbody2D>().RuntimeBody == untouchedBody
			&& untouched.GetComponent<TomCat::CircleCollider2D>().RuntimeFixture
				== untouchedFixture,
			"targeted fixture addition rebuilt existing Box2D objects");

		auto& changedRigidbody = changed.GetComponent<TomCat::Rigidbody2D>();
		changedRigidbody.Type = TomCat::Rigidbody2D::BodyType::Kinematic;
		changedRigidbody.FixedRotation = true;
		scene.OnRuntimeStep();
		Require(changedRigidbody.RuntimeBody == changedBody
			&& static_cast<b2Body*>(changedBody)->GetType() == b2_kinematicBody
			&& static_cast<b2Body*>(changedBody)->IsFixedRotation(),
			"Rigidbody2D definition edit did not mutate the existing body in place");
		Require(untouched.GetComponent<TomCat::Rigidbody2D>().RuntimeBody == untouchedBody
			&& untouched.GetComponent<TomCat::CircleCollider2D>().RuntimeFixture
				== untouchedFixture,
			"body definition edit rebuilt unrelated Box2D objects");

		auto& joint = changed.AddComponent<TomCat::DistanceJoint2D>();
		joint.ConnectedEntity = untouched.GetUUID();
		joint.Distance = 10.0f;
		scene.OnRuntimeStep();
		Require(joint.RuntimeJoint != nullptr
			&& changedRigidbody.RuntimeBody == changedBody
			&& untouched.GetComponent<TomCat::Rigidbody2D>().RuntimeBody == untouchedBody,
			"joint addition rebuilt one of its endpoint bodies");
		joint.Distance = 9.0f;
		scene.OnRuntimeStep();
		Require(joint.RuntimeJoint != nullptr
			&& changedRigidbody.RuntimeBody == changedBody
			&& untouched.GetComponent<TomCat::Rigidbody2D>().RuntimeBody == untouchedBody
			&& untouched.GetComponent<TomCat::CircleCollider2D>().RuntimeFixture
				== untouchedFixture,
			"joint edit rebuilt endpoint bodies or an unrelated fixture");

		scene.DestroyEntity(changed);
		scene.OnRuntimeStep();
		Require(untouched.GetComponent<TomCat::Rigidbody2D>().RuntimeBody == untouchedBody
			&& untouched.GetComponent<TomCat::CircleCollider2D>().RuntimeFixture
				== untouchedFixture,
			"entity removal rebuilt an unrelated Box2D body or fixture");
		scene.OnRuntimeStop();
	}

	void TestPhysicsSynchronizationStageCoalescing()
	{
		TomCat::Scene scene;
		TomCat::Entity changed = AddCircleBody(scene, "Sync statistics changed",
			TomCat::Rigidbody2D::BodyType::Dynamic, { -20.0f, 10.0f });
		TomCat::Entity untouched = AddCircleBody(scene, "Sync statistics untouched",
			TomCat::Rigidbody2D::BodyType::Dynamic, { 20.0f, 10.0f });
		Require(scene.OnRuntimeStart(), "sync statistics scene did not start");

		void* changedBody = changed.GetComponent<TomCat::Rigidbody2D>().RuntimeBody;
		void* untouchedBody = untouched.GetComponent<TomCat::Rigidbody2D>().RuntimeBody;
		void* untouchedFixture =
			untouched.GetComponent<TomCat::CircleCollider2D>().RuntimeFixture;
		Require(changedBody && untouchedBody && untouchedFixture,
			"sync statistics fixtures were not materialized");

		scene.ResetRuntimePhysicsSyncStatistics();
		scene.OnUpdateRuntime(TomCat::Timestep(
			TomCat::Scene::FixedRuntimeTimestep * 4.0f));
		const TomCat::RuntimePhysicsSyncStatistics stable =
			scene.GetRuntimePhysicsSyncStatistics();
		Require(stable.DefinitionScans == 1,
			"one unchanged display frame repeated the full physics definition scan");
		Require(stable.CoalescedRequests > 1,
			"same-stage physics synchronization requests were not coalesced");
		Require(stable.WorldRebuilds == 0 && stable.BodiesCreated == 0
			&& stable.BodiesDestroyed == 0 && stable.BodiesUpdatedInPlace == 0
			&& stable.BoxFixturesCreated == 0
			&& stable.BoxFixturesDestroyed == 0
			&& stable.CircleFixturesCreated == 0
			&& stable.CircleFixturesDestroyed == 0
			&& stable.DistanceJointsCreated == 0
			&& stable.DistanceJointsDestroyed == 0,
			"an unchanged display frame mutated runtime physics definitions");

		scene.ResetRuntimePhysicsSyncStatistics();
		changed.GetComponent<TomCat::CircleCollider2D>().Radius = 0.875f;
		scene.OnUpdateRuntime(TomCat::Timestep(0.0f));
		const TomCat::RuntimePhysicsSyncStatistics colliderEdit =
			scene.GetRuntimePhysicsSyncStatistics();
		Require(colliderEdit.DefinitionScans == 1,
			"a direct public collider-field edit triggered repeated definition scans");
		Require(colliderEdit.WorldRebuilds == 0
			&& colliderEdit.BodiesCreated == 0
			&& colliderEdit.BodiesDestroyed == 0
			&& colliderEdit.BodiesUpdatedInPlace == 0
			&& colliderEdit.BoxFixturesCreated == 0
			&& colliderEdit.BoxFixturesDestroyed == 0
			&& colliderEdit.CircleFixturesCreated == 1
			&& colliderEdit.CircleFixturesDestroyed == 1
			&& colliderEdit.DistanceJointsCreated == 0
			&& colliderEdit.DistanceJointsDestroyed == 0,
			"one collider edit changed Box2D objects outside its target fixture");
		Require(changed.GetComponent<TomCat::Rigidbody2D>().RuntimeBody == changedBody
			&& untouched.GetComponent<TomCat::Rigidbody2D>().RuntimeBody == untouchedBody
			&& untouched.GetComponent<TomCat::CircleCollider2D>().RuntimeFixture
				== untouchedFixture,
			"one collider edit replaced an owning or unrelated Box2D object");

		scene.ResetRuntimePhysicsSyncStatistics();
		changed.GetComponent<TomCat::Rigidbody2D>().Type =
			TomCat::Rigidbody2D::BodyType::Kinematic;
		scene.OnUpdateRuntime(TomCat::Timestep(0.0f));
		const TomCat::RuntimePhysicsSyncStatistics bodyEdit =
			scene.GetRuntimePhysicsSyncStatistics();
		Require(bodyEdit.DefinitionScans == 1 && bodyEdit.WorldRebuilds == 0
			&& bodyEdit.BodiesCreated == 0 && bodyEdit.BodiesDestroyed == 0
			&& bodyEdit.BodiesUpdatedInPlace == 1
			&& bodyEdit.BoxFixturesCreated == 0
			&& bodyEdit.BoxFixturesDestroyed == 0
			&& bodyEdit.CircleFixturesCreated == 0
			&& bodyEdit.CircleFixturesDestroyed == 0
			&& bodyEdit.DistanceJointsCreated == 0
			&& bodyEdit.DistanceJointsDestroyed == 0,
			"one Rigidbody2D edit did not remain a single in-place body update");
		Require(changed.GetComponent<TomCat::Rigidbody2D>().RuntimeBody == changedBody
			&& untouched.GetComponent<TomCat::Rigidbody2D>().RuntimeBody == untouchedBody
			&& untouched.GetComponent<TomCat::CircleCollider2D>().RuntimeFixture
				== untouchedFixture,
			"one body edit replaced the target or an unrelated Box2D object");
		scene.OnRuntimeStop();
	}

	void TestPhysicsRenderInterpolation()
	{
		TomCat::Scene scene;
		TomCat::Entity bodyEntity = AddCircleBody(scene, "Interpolated parent",
			TomCat::Rigidbody2D::BodyType::Kinematic, { 0.0f, 0.0f });
		TomCat::Entity child = scene.CreateEntity("Interpolated child");
		auto& childTransform = child.GetComponent<TomCat::Transform>();
		childTransform._Translation.x = 2.0f;
		childTransform._LocalTranslation = childTransform._Translation;
		Require(scene.SetParent(child, bodyEntity),
			"could not create interpolation hierarchy fixture");
		Require(scene.OnRuntimeStart(), "interpolation scene did not start");

		auto* body = static_cast<b2Body*>(
			bodyEntity.GetComponent<TomCat::Rigidbody2D>().RuntimeBody);
		Require(body != nullptr, "interpolation body was not materialized");
		body->SetLinearVelocity({ 6.0f, 0.0f });
		body->SetAngularVelocity(6.0f);

		scene.OnUpdateRuntime(TomCat::Timestep(
			TomCat::Scene::FixedRuntimeTimestep * 1.5f));
		Require(Near(scene.GetRuntimeInterpolationAlpha(), 0.5f, 2.0e-4f),
			"runtime interpolation alpha did not preserve the fixed-step remainder");
		const auto& current = bodyEntity.GetComponent<TomCat::Transform>();
		Require(Near(current._Translation.x, 0.1f, 2.0e-4f)
			&& Near(current._Rotation.z, 0.1f, 2.0e-4f),
			"ECS did not retain the authoritative current physics pose");

		const glm::mat4 parentRender =
			scene.GetRuntimeRenderTransform(bodyEntity.GetUUID());
		Require(Near(parentRender[3].x, 0.05f, 2.0e-4f)
			&& Near(parentRender[0][0], std::cos(0.05f), 2.0e-4f),
			"render transform was not halfway between previous and current poses");
		const glm::mat4 childRender =
			scene.GetRuntimeRenderTransform(child.GetUUID());
		Require(Near(childRender[3].x, 0.05f + 2.0f * std::cos(0.05f), 3.0e-4f)
			&& Near(childRender[3].y, 2.0f * std::sin(0.05f), 3.0e-4f),
			"non-physics child did not inherit its parent's interpolated pose");
		const glm::mat4 childCameraRender =
			scene.GetRuntimeCameraTransform(child.GetUUID());
		Require(Near(childCameraRender[3].x, childRender[3].x, 3.0e-4f)
			&& Near(childCameraRender[3].y, childRender[3].y, 3.0e-4f)
			&& Near(childCameraRender[0].x, std::cos(0.05f), 3.0e-4f)
			&& Near(childCameraRender[0].y, std::sin(0.05f), 3.0e-4f)
			&& Near(childCameraRender[2].z, 1.0f, 3.0e-4f),
			"child camera position and +Z-forward orientation used different interpolation poses");
		Require(Near(bodyEntity.GetComponent<TomCat::Transform>()._Translation.x,
			0.1f, 2.0e-4f),
			"reading an interpolated render transform mutated ECS authoring state");

		scene.OnRuntimeStep();
		Require(Near(scene.GetRuntimeInterpolationAlpha(), 1.0f)
			&& Near(scene.GetRuntimeRenderTransform(bodyEntity.GetUUID())[3].x,
				0.2f, 3.0e-4f),
			"manual single-step did not present the newly completed physics pose");
		scene.OnRuntimeStop();
	}

	void TestIncrementalFixtureContactContinuity()
	{
		TomCat::Scene scene;
		TomCat::Entity trigger = scene.CreateEntity("Incremental contact trigger");
		auto& triggerCollider = trigger.AddComponent<TomCat::CircleCollider2D>();
		triggerCollider.Radius = 2.0f;
		triggerCollider.IsTrigger = true;
		TomCat::Entity visitor = AddCircleBody(scene, "Incremental contact visitor",
			TomCat::Rigidbody2D::BodyType::Dynamic, { 0.0f, 0.0f }, 0.25f);

		int enters = 0;
		int exits = 0;
		scene.AddTriggerEnter2DListener(
			[&](const TomCat::TriggerEnter2D&) { ++enters; });
		scene.AddTriggerExit2DListener(
			[&](const TomCat::TriggerExit2D&) { ++exits; });
		Require(scene.OnRuntimeStart(), "incremental contact scene did not start");
		scene.OnRuntimeStep();
		Require(enters == 1 && exits == 0,
			"initial persistent trigger pair was not reported once");

		void* visitorBody = visitor.GetComponent<TomCat::Rigidbody2D>().RuntimeBody;
		triggerCollider.Friction = 0.75f;
		scene.OnRuntimeStep();
		Require(enters == 1 && exits == 0
			&& visitor.GetComponent<TomCat::Rigidbody2D>().RuntimeBody == visitorBody,
			"fixture replacement emitted a false contact transition or rebuilt its peer");

		triggerCollider.Offset.x = 10.0f;
		scene.OnRuntimeStep();
		Require(enters == 1 && exits == 1,
			"moving a recreated fixture away did not emit one final Exit");
		scene.OnRuntimeStep();
		Require(enters == 1 && exits == 1,
			"separated incremental fixtures emitted duplicate transitions");
		scene.OnRuntimeStop();
	}

	void TestPauseRenderPathAndSingleStep()
	{
		auto runtime = std::make_shared<ManagedRuntimeProbe>();
		ScriptRuntimeOverride runtimeOverride(runtime);
		TomCat::Scene scene;
		TomCat::Entity entity = AddCircleBody(scene, "Paused body",
			TomCat::Rigidbody2D::BodyType::Dynamic, { 0.0f, 0.0f });
		AttachManagedProbe(entity, 8102);
		Require(scene.OnRuntimeStart(), "managed pause probe did not start");
		scene.OnRuntimeStep();
		auto* body = static_cast<b2Body*>(entity.GetComponent<TomCat::Rigidbody2D>().RuntimeBody);
		Require(body != nullptr, "paused test body did not receive a runtime body");
		const float pausedPosition = body->GetPosition().y;
		const uint32_t pausedUpdates = runtime->FixedUpdateCount;

		for (int frame = 0; frame < 20; ++frame)
			scene.OnRenderRuntime();
		Require(runtime->FixedUpdateCount == pausedUpdates && runtime->UpdateCount == 0,
			"render-only pause path advanced managed scripts");
		Require(Near(body->GetPosition().y, pausedPosition),
			"render-only pause path advanced physics");

		scene.OnRuntimeStep();
		Require(runtime->FixedUpdateCount == pausedUpdates + 1,
			"paused single-step did not advance managed scripts exactly once");
		Require(body->GetPosition().y < pausedPosition,
			"paused single-step did not advance physics exactly one step");
		scene.OnRuntimeStop();
	}

	void TestColliderOnlyStaticBody()
	{
		TomCat::Scene scene;
		TomCat::Entity ground = scene.CreateEntity("Collider-only ground");
		auto& groundTransform = ground.GetComponent<TomCat::Transform>();
		groundTransform._Translation.y = -1.0f;
		groundTransform._LocalTranslation = groundTransform._Translation;
		auto& box = ground.AddComponent<TomCat::BoxCollider2D>();
		box.Size = { 5.0f, 0.5f };
		TomCat::Entity ball = AddCircleBody(scene, "Ball",
			TomCat::Rigidbody2D::BodyType::Dynamic, { 0.0f, 2.0f });

		int enters = 0;
		scene.AddCollisionEnter2DListener([&](const TomCat::CollisionEnter2D&) { ++enters; });
		scene.OnRuntimeStart();
		Require(box.RuntimeFixture != nullptr,
			"collider without Rigidbody2D did not create a static fixture");
		for (int step = 0; step < 120; ++step)
			scene.OnRuntimeStep();
		Require(enters == 1, "dynamic body did not collide exactly once with collider-only static body");
		auto* body = static_cast<b2Body*>(ball.GetComponent<TomCat::Rigidbody2D>().RuntimeBody);
		Require(body && body->GetPosition().y > -0.1f,
			"dynamic body passed through collider-only static body");
		scene.OnRuntimeStop();
	}

	void TestCircleNonUniformScaleFixture()
	{
		TomCat::Scene scene;
		TomCat::Entity entity = scene.CreateEntity("Scaled circle");
		auto& transform = entity.GetComponent<TomCat::Transform>();
		transform._Translation = { 4.0f, 5.0f, 2.0f };
		transform._Rotation = { 0.0f, 0.0f, 1.57079632679f };
		transform._Scale = { 2.0f, 3.0f, 1.0f };
		entity.AddComponent<TomCat::Rigidbody2D>();
		auto& circle = entity.AddComponent<TomCat::CircleCollider2D>();
		circle.Offset = { 1.0f, -2.0f };
		circle.Radius = 0.5f;

		scene.OnRuntimeStart();
		Require(circle.RuntimeFixture != nullptr, "CircleCollider2D did not create a fixture");
		const auto shapes = scene.GetColliderDebugShapes(true);
		Require(shapes.size() == 1, "runtime did not expose exactly one circle fixture");
		Require(shapes[0].Type == TomCat::ColliderDebugShapeType::Circle,
			"runtime fixture was not reported as a circle");
		Require(Near(shapes[0].Radius, 1.5f),
			"circle radius did not use max(abs(scale.x), abs(scale.y))");
		Require(Near(shapes[0].Center.x, 10.0f) && Near(shapes[0].Center.y, 7.0f),
			"scaled/rotated circle offset did not match the Box2D fixture");
		scene.OnRuntimeStop();
	}

	void TestAuthoringAndRuntimeOutlinesMatch()
	{
		TomCat::Scene scene;
		TomCat::Entity entity = scene.CreateEntity("Outline parity");
		auto& transform = entity.GetComponent<TomCat::Transform>();
		transform._Translation = { 3.0f, -4.0f, 2.0f };
		transform._LocalTranslation = transform._Translation;
		transform._Rotation.z = 0.7f;
		transform._LocalRotation = transform._Rotation;
		transform._Scale = { -2.0f, 3.0f, 1.0f };
		transform._LocalScale = transform._Scale;
		entity.AddComponent<TomCat::Rigidbody2D>();
		auto& box = entity.AddComponent<TomCat::BoxCollider2D>();
		box.Offset = { 1.0f, -0.5f };
		box.Size = { 1.25f, 0.75f };
		box.IsTrigger = true;
		box.CollisionLayer = 0x0004;
		auto& circle = entity.AddComponent<TomCat::CircleCollider2D>();
		circle.Offset = { -0.25f, 1.5f };
		circle.Radius = 0.4f;
		circle.CollisionLayer = 0x0008;

		const auto authoringShapes = scene.GetColliderDebugShapes(false);
		Require(authoringShapes.size() == 2,
			"authoring visualization did not expose both colliders");
		scene.OnRuntimeStart();
		const auto runtimeShapes = scene.GetColliderDebugShapes(true);
		Require(runtimeShapes.size() == authoringShapes.size(),
			"runtime fixture visualization count differs from authoring visualization");
		for (const auto& authoring : authoringShapes)
		{
			const auto runtimeIt = std::find_if(runtimeShapes.begin(), runtimeShapes.end(),
				[&](const auto& runtime)
				{
					return runtime.EntityID == authoring.EntityID && runtime.Type == authoring.Type;
				});
			Require(runtimeIt != runtimeShapes.end(),
				"runtime fixture visualization omitted an authoring collider");
			Require(runtimeIt->Enabled == authoring.Enabled
				&& runtimeIt->IsTrigger == authoring.IsTrigger
				&& runtimeIt->CollisionLayer == authoring.CollisionLayer
				&& Near(runtimeIt->Center, authoring.Center)
				&& Near(runtimeIt->HalfSize, authoring.HalfSize)
				&& Near(runtimeIt->Radius, authoring.Radius)
				&& Near(runtimeIt->Rotation, authoring.Rotation)
				&& Near(runtimeIt->Transform, authoring.Transform),
				"authoring collider outline does not exactly match its Box2D fixture outline");
		}
		scene.OnRuntimeStop();
	}

	void TestTriggerAndManagedCallbacks()
	{
		auto runtime = std::make_shared<ManagedRuntimeProbe>();
		ScriptRuntimeOverride runtimeOverride(runtime);
		TomCat::Scene scene;
		TomCat::Entity trigger = scene.CreateEntity("Trigger");
		auto& triggerCollider = trigger.AddComponent<TomCat::CircleCollider2D>();
		triggerCollider.Radius = 2.0f;
		triggerCollider.IsTrigger = true;
		AttachManagedProbe(trigger, 8103);
		TomCat::Entity body = AddCircleBody(scene, "Trigger visitor",
			TomCat::Rigidbody2D::BodyType::Dynamic, { 0.0f, 0.0f }, 0.5f);
		AttachManagedProbe(body, 8104);
		const TomCat::UUID triggerUUID = trigger.GetUUID();
		const TomCat::UUID bodyUUID = body.GetUUID();

		int collisionEnters = 0;
		int triggerEnters = 0;
		int triggerExits = 0;
		bool invalidListenerPair = false;
		scene.AddCollisionEnter2DListener([&](const TomCat::CollisionEnter2D&) { ++collisionEnters; });
		scene.AddTriggerEnter2DListener([&](const TomCat::TriggerEnter2D& event)
		{
			++triggerEnters;
			invalidListenerPair |= !Contains(event, triggerUUID) || !Contains(event, bodyUUID);
		});
		scene.AddTriggerExit2DListener([&](const TomCat::TriggerExit2D& event)
		{
			++triggerExits;
			invalidListenerPair |= !Contains(event, triggerUUID) || !Contains(event, bodyUUID);
		});
		Require(scene.OnRuntimeStart(), "managed trigger probe did not start");

		auto& scriptEngine = TomCat::Scripting::ScriptEngine::Get();
		std::vector<bool> fixedInputEdges;
		std::vector<bool> physicsInputEdges;
		std::vector<size_t> fixedInputEventCounts;
		std::vector<size_t> physicsInputEventCounts;
		runtime->FixedUpdateAction = [&]()
		{
			fixedInputEdges.push_back(scriptEngine.WasKeyPressed(65)
				&& scriptEngine.WasKeyReleased(65));
			fixedInputEventCounts.push_back(scriptEngine.GetInputEvents().size());
		};
		runtime->PhysicsEventAction = [&]()
		{
			physicsInputEdges.push_back(scriptEngine.WasKeyPressed(65)
				&& scriptEngine.WasKeyReleased(65));
			physicsInputEventCounts.push_back(scriptEngine.GetInputEvents().size());
		};
		TomCat::Input::ClearState();
		TomCat::Input::NotifyKey(65, TomCat::InputEventQueue::Action::Pressed, 50.0);
		TomCat::Input::NotifyKey(65, TomCat::InputEventQueue::Action::Released, 50.1);
		TomCat::Input::BeginFrame();
		scriptEngine.CaptureInputState();

		scene.OnRuntimeStep();
		Require(triggerEnters == 1 && collisionEnters == 0,
			"sensor contact was not routed exclusively as TriggerEnter2D");
		Require(runtime->PhysicsEvents.size() == 1
			&& runtime->PhysicsEvents.front().Kind == static_cast<uint32_t>(
				TomCat::Scripting::NativePhysicsEventKind::TriggerEnter),
			"managed runtime did not receive one TriggerEnter2D event");
		const auto& managedEnter = runtime->PhysicsEvents.front();
		const bool managedPairMatches =
			(managedEnter.EntityA.EntityId == static_cast<uint64_t>(triggerUUID)
				&& managedEnter.EntityB.EntityId == static_cast<uint64_t>(bodyUUID))
			|| (managedEnter.EntityA.EntityId == static_cast<uint64_t>(bodyUUID)
				&& managedEnter.EntityB.EntityId == static_cast<uint64_t>(triggerUUID));
		Require(managedPairMatches && !invalidListenerPair,
			"trigger callbacks received an unrelated entity pair");

		MoveRuntimeBody(body, { 10.0f, 0.0f });
		scene.OnRuntimeStep();
		Require(triggerExits == 1 && runtime->PhysicsEvents.size() == 2
			&& runtime->PhysicsEvents.back().Kind == static_cast<uint32_t>(
				TomCat::Scripting::NativePhysicsEventKind::TriggerExit),
			"TriggerExit2D was not delivered once to listeners and the managed runtime");
		Require(fixedInputEdges.size() == 2 && physicsInputEdges.size() == 2
			&& fixedInputEdges[0] && physicsInputEdges[0]
			&& !fixedInputEdges[1] && !physicsInputEdges[1],
			"Scene fixed-step input edges were not shared then consumed across physics callbacks");
		Require(fixedInputEventCounts.size() == 2
			&& physicsInputEventCounts.size() == 2
			&& fixedInputEventCounts[0] == 2
			&& physicsInputEventCounts[0] == 2
			&& fixedInputEventCounts[1] == 0
			&& physicsInputEventCounts[1] == 0,
			"Scene fixed-step ordered batch was replayed or cleared before physics callbacks");
		scene.OnRuntimeStep();
		Require(triggerEnters == 1 && triggerExits == 1
			&& runtime->PhysicsEvents.size() == 2,
			"persistent separation emitted duplicate trigger events");
		scene.OnRuntimeStop();
		TomCat::Input::ClearState();
	}

	void TestCollisionFilteringAndRuntimeRebuild()
	{
		TomCat::Scene scene;
		TomCat::Entity staticEntity = scene.CreateEntity("Filtered static");
		auto& staticCollider = staticEntity.AddComponent<TomCat::CircleCollider2D>();
		staticCollider.Radius = 2.0f;
		staticCollider.CollisionLayer = 0x0001;
		staticCollider.CollisionMask = 0x0002;
		TomCat::Entity dynamicEntity = AddCircleBody(scene, "Filtered dynamic",
			TomCat::Rigidbody2D::BodyType::Dynamic, { 0.0f, 0.0f }, 1.0f);
		TomCat::Entity dynamicParent = scene.CreateEntity("Filtered parent");
		Require(scene.SetParent(dynamicEntity, dynamicParent),
			"could not create active-hierarchy physics fixture");
		auto& dynamicCollider = dynamicEntity.GetComponent<TomCat::CircleCollider2D>();
		dynamicCollider.CollisionLayer = 0x0002;
		dynamicCollider.CollisionMask = 0x0000;

		int enters = 0;
		scene.AddCollisionEnter2DListener([&](const TomCat::CollisionEnter2D&) { ++enters; });
		scene.OnRuntimeStart();
		scene.OnRuntimeStep();
		Require(enters == 0, "collision layer/mask rejected pair still collided");

		dynamicCollider.CollisionMask = 0x0001;
		scene.OnRuntimeStep();
		Require(enters == 1,
			"runtime filter edit did not rebuild fixtures and enable contact at a safe step boundary");

		dynamicParent.GetComponent<TomCat::Tag>().ActiveSelf = false;
		scene.OnRuntimeStep();
		Require(!scene.IsActiveInHierarchy(dynamicEntity)
			&& dynamicEntity.GetComponent<TomCat::Rigidbody2D>().RuntimeBody == nullptr
			&& dynamicCollider.RuntimeFixture == nullptr,
			"disabling a parent left child physics active");
		dynamicParent.GetComponent<TomCat::Tag>().ActiveSelf = true;
		scene.OnRuntimeStep();
		Require(scene.IsActiveInHierarchy(dynamicEntity)
			&& dynamicEntity.GetComponent<TomCat::Rigidbody2D>().RuntimeBody != nullptr
			&& dynamicCollider.RuntimeFixture != nullptr,
			"re-enabling a parent did not restore child physics");

		dynamicCollider.Radius = 3.0f;
		scene.OnRuntimeStep();
		const auto resized = scene.GetColliderDebugShapes(true);
		const auto resizedIt = std::find_if(resized.begin(), resized.end(), [&](const auto& shape)
		{
			return shape.EntityID == dynamicEntity.GetUUID()
				&& shape.Type == TomCat::ColliderDebugShapeType::Circle;
		});
		Require(resizedIt != resized.end() && Near(resizedIt->Radius, 3.0f),
			"runtime collider data edit did not rebuild the Box2D fixture");

		dynamicEntity.RemoveComponent<TomCat::CircleCollider2D>();
		scene.OnRuntimeStep();
		const auto removed = scene.GetColliderDebugShapes(true);
		Require(std::none_of(removed.begin(), removed.end(), [&](const auto& shape)
		{
			return shape.EntityID == dynamicEntity.GetUUID();
		}), "runtime collider removal left a stale Box2D fixture");

		auto& box = dynamicEntity.AddComponent<TomCat::BoxCollider2D>();
		box.Size = { 0.75f, 1.25f };
		scene.OnRuntimeStep();
		Require(box.RuntimeFixture != nullptr,
			"runtime collider addition did not create a Box2D fixture");
		box.Friction = 1.25f;
		scene.OnRuntimeStep();
		Require(box.RuntimeFixture != nullptr
			&& Near(static_cast<b2Fixture*>(box.RuntimeFixture)->GetFriction(), 1.25f),
			"runtime collider material edit did not rebuild the Box2D fixture");
		box.Enabled = false;
		scene.OnRuntimeStep();
		Require(box.RuntimeFixture == nullptr,
			"disabling a collider at runtime left its Box2D fixture enabled");
		box.Enabled = true;
		scene.OnRuntimeStep();
		Require(box.RuntimeFixture != nullptr,
			"re-enabling a collider at runtime did not recreate its Box2D fixture");

		auto& runtimeRigidbody = dynamicEntity.GetComponent<TomCat::Rigidbody2D>();
		runtimeRigidbody.Type = TomCat::Rigidbody2D::BodyType::Kinematic;
		runtimeRigidbody.FixedRotation = true;
		scene.OnRuntimeStep();
		Require(runtimeRigidbody.RuntimeBody != nullptr
			&& static_cast<b2Body*>(runtimeRigidbody.RuntimeBody)->GetType() == b2_kinematicBody
			&& static_cast<b2Body*>(runtimeRigidbody.RuntimeBody)->IsFixedRotation(),
			"runtime Rigidbody2D type/fixed-rotation edit was not rebuilt");
		runtimeRigidbody.Enabled = false;
		scene.OnRuntimeStep();
		Require(runtimeRigidbody.RuntimeBody == nullptr && box.RuntimeFixture == nullptr,
			"disabling Rigidbody2D did not remove its body and fixtures");
		runtimeRigidbody.Enabled = true;
		runtimeRigidbody.Type = TomCat::Rigidbody2D::BodyType::Dynamic;
		scene.OnRuntimeStep();
		Require(runtimeRigidbody.RuntimeBody != nullptr && box.RuntimeFixture != nullptr,
			"re-enabling Rigidbody2D did not recreate its body and fixtures");

		dynamicEntity.RemoveComponent<TomCat::Rigidbody2D>();
		scene.OnRuntimeStep();
		Require(box.RuntimeFixture != nullptr,
			"removing Rigidbody2D did not retain collider as a static runtime body");
		Require(!scene.ApplyLinearImpulse2D(dynamicEntity.GetUUID(), { 1.0f, 0.0f }),
			"force API accepted a collider-only static body");
		auto& addedRigidbody = dynamicEntity.AddComponent<TomCat::Rigidbody2D>();
		addedRigidbody.Type = TomCat::Rigidbody2D::BodyType::Dynamic;
		scene.OnRuntimeStep();
		Require(addedRigidbody.RuntimeBody != nullptr
			&& static_cast<b2Body*>(addedRigidbody.RuntimeBody)->GetType() == b2_dynamicBody,
			"runtime Rigidbody2D addition did not replace the implicit static body");
		scene.OnRuntimeStop();
	}

	void TestInactiveHierarchyPreservesRuntimeBodyState()
	{
		TomCat::Scene scene;
		TomCat::Entity parent = scene.CreateEntity("Suspended body parent");
		TomCat::Entity child = AddCircleBody(scene, "Suspended body child",
			TomCat::Rigidbody2D::BodyType::Dynamic, { 0.0f, 0.0f }, 0.5f);
		Require(scene.SetParent(child, parent),
			"could not create suspended-body hierarchy fixture");
		Require(scene.OnRuntimeStart(), "suspended-body Scene did not start");

		auto& rigidbody = child.GetComponent<TomCat::Rigidbody2D>();
		auto* body = static_cast<b2Body*>(rigidbody.RuntimeBody);
		Require(body != nullptr, "dynamic hierarchy body was not created");
		body->SetLinearVelocity({ 3.25f, -1.5f });
		body->SetAngularVelocity(2.75f);
		body->SetAwake(true);

		parent.GetComponent<TomCat::Tag>().ActiveSelf = false;
		Require(!scene.GetLinearVelocity2D(child.GetUUID()).has_value()
			&& rigidbody.RuntimeBody == nullptr,
			"inactive hierarchy retained a live dynamic body");
		parent.GetComponent<TomCat::Tag>().ActiveSelf = true;
		const auto restoredVelocity = scene.GetLinearVelocity2D(child.GetUUID());
		body = static_cast<b2Body*>(rigidbody.RuntimeBody);
		Require(restoredVelocity.has_value()
			&& Near(*restoredVelocity, { 3.25f, -1.5f })
			&& body && Near(body->GetAngularVelocity(), 2.75f)
			&& body->IsAwake(),
			"re-enabled hierarchy lost dynamic velocity, angular velocity, or awake state");

		body->SetLinearVelocity({ 0.0f, 0.0f });
		body->SetAngularVelocity(0.0f);
		body->SetAwake(false);
		parent.GetComponent<TomCat::Tag>().ActiveSelf = false;
		Require(!scene.GetLinearVelocity2D(child.GetUUID()).has_value(),
			"sleeping hierarchy body was not suspended");
		parent.GetComponent<TomCat::Tag>().ActiveSelf = true;
		Require(scene.GetLinearVelocity2D(child.GetUUID()).has_value(),
			"sleeping hierarchy body was not restored");
		body = static_cast<b2Body*>(rigidbody.RuntimeBody);
		Require(body && !body->IsAwake(),
			"re-enabled hierarchy woke a previously sleeping body");

		body->SetAwake(true);
		body->SetLinearVelocity({ 6.0f, -4.0f });
		body->SetAngularVelocity(3.0f);
		parent.GetComponent<TomCat::Tag>().ActiveSelf = false;
		Require(!scene.GetLinearVelocity2D(child.GetUUID()).has_value(),
			"body was not suspended before runtime Stop cleanup");
		scene.OnRuntimeStop();
		parent.GetComponent<TomCat::Tag>().ActiveSelf = true;
		Require(scene.OnRuntimeStart(), "Scene did not restart after suspended body Stop");
		const auto restartVelocity = scene.GetLinearVelocity2D(child.GetUUID());
		body = static_cast<b2Body*>(rigidbody.RuntimeBody);
		Require(restartVelocity.has_value()
			&& Near(*restartVelocity, { 0.0f, 0.0f })
			&& body && Near(body->GetAngularVelocity(), 0.0f),
			"runtime Stop leaked suspended motion into the next session");

		body->SetLinearVelocity({ 8.0f, 2.0f });
		body->SetAngularVelocity(5.0f);
		parent.GetComponent<TomCat::Tag>().ActiveSelf = false;
		Require(!scene.GetLinearVelocity2D(child.GetUUID()).has_value(),
			"body was not suspended before entity destruction");
		const TomCat::UUID childID = child.GetUUID();
		scene.DestroyEntity(child);
		TomCat::Entity replacement = scene.CreateEntityWithUUID(childID,
			"Replacement suspended body");
		auto& replacementBody = replacement.AddComponent<TomCat::Rigidbody2D>();
		replacementBody.Type = TomCat::Rigidbody2D::BodyType::Dynamic;
		replacement.AddComponent<TomCat::CircleCollider2D>();
		Require(scene.SetParent(replacement, parent),
			"could not parent replacement body");
		parent.GetComponent<TomCat::Tag>().ActiveSelf = true;
		const auto replacementVelocity = scene.GetLinearVelocity2D(childID);
		body = static_cast<b2Body*>(replacementBody.RuntimeBody);
		Require(replacementVelocity.has_value()
			&& Near(*replacementVelocity, { 0.0f, 0.0f })
			&& body && Near(body->GetAngularVelocity(), 0.0f),
			"destroyed entity leaked suspended motion into a replacement UUID");
		scene.OnRuntimeStop();
	}

	void TestInactiveRuntimeSpriteComponentsStayUninitialized()
	{
		TomCat::Scene scene;
		TomCat::Entity parent = scene.CreateEntity("Inactive sprite parent");
		parent.GetComponent<TomCat::Tag>().ActiveSelf = false;
		TomCat::Entity rendererAddedLater = scene.CreateEntity("Renderer added later");
		rendererAddedLater.AddComponent<TomCat::SpriteAnimator>();
		TomCat::Entity animatorAddedLater = scene.CreateEntity("Animator added later");
		animatorAddedLater.AddComponent<TomCat::SpriteRenderer>();
		Require(scene.SetParent(rendererAddedLater, parent)
			&& scene.SetParent(animatorAddedLater, parent),
			"could not create inactive sprite hierarchy fixture");
		Require(scene.OnRuntimeStart(), "inactive sprite Scene did not start");

		rendererAddedLater.AddComponent<TomCat::SpriteRenderer>();
		animatorAddedLater.AddComponent<TomCat::SpriteAnimator>();
		Require(!rendererAddedLater.GetComponent<TomCat::SpriteAnimator>()
				.RuntimeInitialized
			&& !animatorAddedLater.GetComponent<TomCat::SpriteAnimator>()
				.RuntimeInitialized,
			"runtime component addition initialized an inactive SpriteAnimator");

		parent.GetComponent<TomCat::Tag>().ActiveSelf = true;
		scene.OnRuntimeStep();
		Require(rendererAddedLater.GetComponent<TomCat::SpriteAnimator>()
				.RuntimeInitialized
			&& animatorAddedLater.GetComponent<TomCat::SpriteAnimator>()
				.RuntimeInitialized,
			"reactivated SpriteAnimators did not initialize at the next safe step");
		scene.OnRuntimeStop();
	}

	void TestAudioAutoPlayWaitsForManagedCreate()
	{
		auto runtime = std::make_shared<ManagedRuntimeProbe>();
		ScriptRuntimeOverride runtimeOverride(runtime);
		TomCat::Scene scene;
		TomCat::Entity scripted = scene.CreateEntity("OnCreate disables audio");
		auto& source = scripted.AddComponent<TomCat::AudioSource>();
		source.Clip = TomCat::AssetHandle(777);
		source.PlayOnStart = true;
		TomCat::CSharpScriptEntry entry;
		entry.AttachmentID = TomCat::UUID();
		entry.ScriptAsset = TomCat::AssetHandle(9101);
		entry.LastKnownClassName = "Game.DisableAudioOnCreate";
		scripted.AddComponent<TomCat::CSharpScripts>().Scripts.push_back(entry);
		TomCat::Entity activeControl = scene.CreateEntity("Active autoplay control");
		auto& controlSource = activeControl.AddComponent<TomCat::AudioSource>();
		controlSource.PlayOnStart = true;

		bool observedPreparedSource = false;
		runtime->InvokeCreateAction = [&]
		{
			const auto& liveSource = scripted.GetComponent<TomCat::AudioSource>();
			observedPreparedSource = liveSource.RuntimeVoice == 0
				&& !liveSource.RuntimeAutoPlayEvaluated;
			scripted.GetComponent<TomCat::Tag>().ActiveSelf = false;
		};
		Require(scene.OnRuntimeStart(), "managed audio-order Scene did not start");
		Require(observedPreparedSource,
			"PlayOnStart was evaluated before managed OnCreate");
		Require(!scene.IsActiveInHierarchy(scripted)
			&& scripted.GetComponent<TomCat::AudioSource>().RuntimeVoice == 0
			&& !scripted.GetComponent<TomCat::AudioSource>()
				.RuntimeAutoPlayEvaluated,
			"OnCreate-disabled AudioSource leaked an autoplay attempt");
		Require(controlSource.RuntimeAutoPlayEvaluated,
			"active PlayOnStart source was not reconciled after managed OnCreate");
		scene.OnRuntimeStop();
	}

	void TestRuntimeStepShortCircuitsAfterFixedStop()
	{
		auto runtime = std::make_shared<ManagedRuntimeProbe>();
		ScriptRuntimeOverride runtimeOverride(runtime);
		TomCat::Scene scene;
		TomCat::Entity scripted = scene.CreateEntity("Fixed stop probe");
		TomCat::CSharpScriptEntry entry;
		entry.AttachmentID = TomCat::UUID();
		entry.ScriptAsset = TomCat::AssetHandle(9102);
		scripted.AddComponent<TomCat::CSharpScripts>().Scripts.push_back(entry);
		auto& source = scripted.AddComponent<TomCat::AudioSource>();
		source.PlayOnStart = false;
		Require(scene.OnRuntimeStart(), "fixed-stop Scene did not start");
		source.RuntimeAutoPlayEvaluated = false;
		runtime->FixedUpdateAction = [&] { scene.OnRuntimeStop(); };

		scene.OnRuntimeStep();
		Require(!scene.IsRuntimeRunning()
			&& !source.RuntimeAutoPlayEvaluated,
			"OnRuntimeStep continued into audio/render work after fixed-step failure");
	}

	struct FilterGateResult
	{
		int CollisionEnters = 0;
		int TriggerEnters = 0;
	};

	FilterGateResult RunProjectAndFixtureFilterCase(bool projectAllows,
		bool fixtureAllows, bool trigger)
	{
		TomCat::Scene scene;
		TomCat::Physics2DSettings settings;
		settings.SetLayersCollide(1, 2, projectAllows);
		scene.SetPhysics2DSettings(settings);

		TomCat::Entity staticEntity = scene.CreateEntity("Independent filter static");
		staticEntity.GetComponent<TomCat::EntityMetadata>().Layer = 1;
		auto& staticCollider = staticEntity.AddComponent<TomCat::CircleCollider2D>();
		staticCollider.Radius = 2.0f;
		staticCollider.IsTrigger = trigger;
		// Fixture bits deliberately do not correspond to entity-layer indices.
		staticCollider.CollisionLayer = 0x0040;
		staticCollider.CollisionMask = 0x0200;

		TomCat::Entity dynamicEntity = AddCircleBody(scene, "Independent filter dynamic",
			TomCat::Rigidbody2D::BodyType::Dynamic, { 0.0f, 0.0f }, 1.0f);
		dynamicEntity.GetComponent<TomCat::EntityMetadata>().Layer = 2;
		auto& dynamicCollider = dynamicEntity.GetComponent<TomCat::CircleCollider2D>();
		dynamicCollider.CollisionLayer = 0x0200;
		dynamicCollider.CollisionMask = fixtureAllows ? 0x0040 : 0x0000;

		FilterGateResult result;
		scene.AddCollisionEnter2DListener(
			[&](const TomCat::CollisionEnter2D&) { ++result.CollisionEnters; });
		scene.AddTriggerEnter2DListener(
			[&](const TomCat::TriggerEnter2D&) { ++result.TriggerEnters; });
		scene.OnRuntimeStart();
		auto* staticFixture = static_cast<b2Fixture*>(staticCollider.RuntimeFixture);
		auto* dynamicFixture = static_cast<b2Fixture*>(dynamicCollider.RuntimeFixture);
		Require(staticFixture && dynamicFixture,
			"independent filter fixture pair was not created");
		const b2Filter staticFilter = staticFixture->GetFilterData();
		const b2Filter dynamicFilter = dynamicFixture->GetFilterData();
		Require(staticFilter.categoryBits == 0x0040 && staticFilter.maskBits == 0x0200
			&& dynamicFilter.categoryBits == 0x0200
			&& dynamicFilter.maskBits == (fixtureAllows ? 0x0040 : 0x0000),
			"entity project layers overwrote per-fixture Box2D filter bits");
		scene.OnRuntimeStep();
		scene.OnRuntimeStop();
		return result;
	}

	int RunAsymmetricProjectFilterCase(bool allowOneToTwo, bool reverseCreation)
	{
		TomCat::Scene scene;
		TomCat::Physics2DSettings settings;
		const uint16_t layerOneBit = uint16_t(1) << 1;
		const uint16_t layerTwoBit = uint16_t(1) << 2;
		if (allowOneToTwo)
		{
			settings.CollisionMasks[1] |= layerTwoBit;
			settings.CollisionMasks[2] &= static_cast<uint16_t>(~layerOneBit);
		}
		else
		{
			settings.CollisionMasks[1] &= static_cast<uint16_t>(~layerTwoBit);
			settings.CollisionMasks[2] |= layerOneBit;
		}
		scene.SetPhysics2DSettings(settings);

		TomCat::Entity staticEntity;
		TomCat::Entity dynamicEntity;
		auto createStatic = [&]()
		{
			staticEntity = scene.CreateEntity("Asymmetric filter static");
			staticEntity.GetComponent<TomCat::EntityMetadata>().Layer = 1;
			auto& collider = staticEntity.AddComponent<TomCat::CircleCollider2D>();
			collider.Radius = 2.0f;
		};
		auto createDynamic = [&]()
		{
			dynamicEntity = AddCircleBody(scene, "Asymmetric filter dynamic",
				TomCat::Rigidbody2D::BodyType::Dynamic, { 0.0f, 0.0f }, 1.0f);
			dynamicEntity.GetComponent<TomCat::EntityMetadata>().Layer = 2;
		};
		if (reverseCreation)
		{
			createDynamic();
			createStatic();
		}
		else
		{
			createStatic();
			createDynamic();
		}

		int collisionEnters = 0;
		scene.AddCollisionEnter2DListener(
			[&](const TomCat::CollisionEnter2D&) { ++collisionEnters; });
		scene.OnRuntimeStart();
		Require(staticEntity.GetComponent<TomCat::CircleCollider2D>().RuntimeFixture
			&& dynamicEntity.GetComponent<TomCat::CircleCollider2D>().RuntimeFixture,
			"asymmetric project-filter fixtures were not created");
		scene.OnRuntimeStep();
		scene.OnRuntimeStop();
		return collisionEnters;
	}

	void TestProjectMatrixAndFixtureFilters()
	{
		FilterGateResult result = RunProjectAndFixtureFilterCase(false, true, false);
		Require(result.CollisionEnters == 0 && result.TriggerEnters == 0,
			"project collision matrix did not independently reject a solid contact");
		result = RunProjectAndFixtureFilterCase(true, false, false);
		Require(result.CollisionEnters == 0 && result.TriggerEnters == 0,
			"fixture category/mask did not independently reject a solid contact");
		result = RunProjectAndFixtureFilterCase(true, true, false);
		Require(result.CollisionEnters == 1 && result.TriggerEnters == 0,
			"two enabled independent gates did not admit one solid contact");

		result = RunProjectAndFixtureFilterCase(false, true, true);
		Require(result.CollisionEnters == 0 && result.TriggerEnters == 0,
			"project collision matrix did not reject a trigger contact");
		result = RunProjectAndFixtureFilterCase(true, false, true);
		Require(result.CollisionEnters == 0 && result.TriggerEnters == 0,
			"fixture category/mask did not reject a trigger contact");
		result = RunProjectAndFixtureFilterCase(true, true, true);
		Require(result.CollisionEnters == 0 && result.TriggerEnters == 1,
			"two enabled independent gates did not admit one trigger contact");

		for (bool allowOneToTwo : { false, true })
		{
			for (bool reverseCreation : { false, true })
			{
				Require(RunAsymmetricProjectFilterCase(allowOneToTwo, reverseCreation) == 0,
					"an asymmetric direct Scene collision matrix admitted a contact");
			}
		}
	}

	void TestQueriesAndMotionAPI()
	{
		TomCat::Scene scene;
		TomCat::Entity solid = scene.CreateEntity("Query solid");
		solid.GetComponent<TomCat::EntityMetadata>().Layer = 2;
		auto& solidTransform = solid.GetComponent<TomCat::Transform>();
		solidTransform._Translation.x = 3.0f;
		solidTransform._LocalTranslation = solidTransform._Translation;
		auto& solidBox = solid.AddComponent<TomCat::BoxCollider2D>();
		solidBox.Size = { 0.5f, 0.5f };
		// Raw fixture filters are deliberately unrelated to the entity layer.
		solidBox.CollisionLayer = 0x0040;

		TomCat::Entity trigger = scene.CreateEntity("Query trigger");
		trigger.GetComponent<TomCat::EntityMetadata>().Layer = 3;
		auto& triggerTransform = trigger.GetComponent<TomCat::Transform>();
		triggerTransform._Translation.x = 6.0f;
		triggerTransform._LocalTranslation = triggerTransform._Translation;
		auto& triggerCircle = trigger.AddComponent<TomCat::CircleCollider2D>();
		triggerCircle.Radius = 1.0f;
		triggerCircle.IsTrigger = true;
		triggerCircle.CollisionLayer = 0x0200;

		TomCat::Entity missingMetadata = scene.CreateEntity("Query missing metadata");
		auto& missingTransform = missingMetadata.GetComponent<TomCat::Transform>();
		missingTransform._Translation.x = 9.0f;
		missingTransform._LocalTranslation = missingTransform._Translation;
		missingMetadata.AddComponent<TomCat::BoxCollider2D>().Size = { 0.5f, 0.5f };
		missingMetadata.RemoveComponent<TomCat::EntityMetadata>();

		TomCat::Entity invalidMetadata = scene.CreateEntity("Query invalid metadata");
		invalidMetadata.GetComponent<TomCat::EntityMetadata>().Layer =
			static_cast<uint8_t>(TomCat::Physics2DLayerCount);
		auto& invalidTransform = invalidMetadata.GetComponent<TomCat::Transform>();
		invalidTransform._Translation.x = 11.0f;
		invalidTransform._LocalTranslation = invalidTransform._Translation;
		invalidMetadata.AddComponent<TomCat::CircleCollider2D>().Radius = 0.5f;

		TomCat::Entity dynamicEntity = AddCircleBody(scene, "Motion API body",
			TomCat::Rigidbody2D::BodyType::Dynamic, { 20.0f, 0.0f }, 1.0f);
		scene.OnRuntimeStart();

		const auto rayHit = scene.Raycast2D({ 0.0f, 0.0f }, { 10.0f, 0.0f }, 0x000C, true);
		Require(rayHit && rayHit->EntityID == solid.GetUUID() && !rayHit->IsTrigger
			&& rayHit->CollisionLayer == 0x0004,
			"raycast did not return the nearest matching entity layer");
		const auto triggerRay = scene.Raycast2D({ 4.0f, 0.0f }, { 10.0f, 0.0f }, 0x0008, true);
		Require(triggerRay && triggerRay->EntityID == trigger.GetUUID() && triggerRay->IsTrigger
			&& triggerRay->CollisionLayer == 0x0008,
			"raycast did not include a trigger on the matching entity layer");
		Require(!scene.Raycast2D({ 4.0f, 0.0f }, { 10.0f, 0.0f }, 0x0008, false),
			"raycast includeTriggers=false still returned a sensor");
		Require(!scene.Raycast2D({ 0.0f, 0.0f }, { 12.0f, 0.0f }, 0x0240, true),
			"raycast filtered by raw fixture category bits instead of entity layers");

		const auto allHits = scene.QueryAABB2D({ 1.0f, -2.0f }, { 12.0f, 2.0f }, 0x000C, true);
		Require(allHits.size() == 2, "AABB query did not return both matching entities");
		const auto solidHit = std::find_if(allHits.begin(), allHits.end(), [&](const auto& hit)
		{
			return hit.EntityID == solid.GetUUID();
		});
		const auto triggerHit = std::find_if(allHits.begin(), allHits.end(), [&](const auto& hit)
		{
			return hit.EntityID == trigger.GetUUID();
		});
		Require(solidHit != allHits.end() && solidHit->CollisionLayer == 0x0004
			&& triggerHit != allHits.end() && triggerHit->CollisionLayer == 0x0008,
			"AABB query results did not report entity-layer bits");
		const auto solidHits = scene.QueryAABB2D({ 1.0f, -2.0f }, { 12.0f, 2.0f }, 0x000C, false);
		Require(solidHits.size() == 1 && solidHits[0].EntityID == solid.GetUUID()
			&& solidHits[0].CollisionLayer == 0x0004,
			"AABB query trigger exclusion failed");
		Require(scene.QueryAABB2D({ 8.0f, -2.0f }, { 12.0f, 2.0f }, 0xFFFF, true).empty(),
			"AABB query returned an entity with missing or invalid layer metadata");

		Require(scene.SetLinearVelocity2D(dynamicEntity.GetUUID(), { 3.0f, 4.0f }),
			"SetLinearVelocity2D rejected a dynamic body");
		const auto initialVelocity = scene.GetLinearVelocity2D(dynamicEntity.GetUUID());
		Require(initialVelocity && Near(initialVelocity->x, 3.0f) && Near(initialVelocity->y, 4.0f),
			"GetLinearVelocity2D did not return the assigned velocity");
		Require(scene.ApplyLinearImpulse2D(dynamicEntity.GetUUID(), { 2.0f, 0.0f }),
			"ApplyLinearImpulse2D rejected a dynamic body");
		const auto impulseVelocity = scene.GetLinearVelocity2D(dynamicEntity.GetUUID());
		Require(impulseVelocity && impulseVelocity->x > initialVelocity->x,
			"linear impulse did not change velocity immediately");
		Require(scene.ApplyForce2D(dynamicEntity.GetUUID(), { 60.0f, 0.0f }),
			"ApplyForce2D rejected a dynamic body");
		Require(scene.ApplyForceAtPoint2D(dynamicEntity.GetUUID(), { 0.0f, 20.0f },
			{ 21.0f, 0.0f }), "ApplyForceAtPoint2D rejected a dynamic body");
		Require(scene.ApplyLinearImpulseAtPoint2D(dynamicEntity.GetUUID(), { 0.0f, 2.0f },
			{ 21.0f, 0.0f }), "ApplyLinearImpulseAtPoint2D rejected a dynamic body");
		auto* motionBody = static_cast<b2Body*>(
			dynamicEntity.GetComponent<TomCat::Rigidbody2D>().RuntimeBody);
		Require(motionBody && std::abs(motionBody->GetAngularVelocity()) > 0.0f,
			"at-point impulse did not produce angular velocity");
		const float beforeForceStep = impulseVelocity->x;
		scene.OnRuntimeStep();
		const auto forceVelocity = scene.GetLinearVelocity2D(dynamicEntity.GetUUID());
		Require(forceVelocity && forceVelocity->x > beforeForceStep,
			"force did not change velocity on the next fixed step");
		Require(!scene.SetLinearVelocity2D(solid.GetUUID(), { 1.0f, 0.0f }),
			"velocity API accepted a collider-only static body");
		scene.OnRuntimeStop();
	}

	void TestDistanceJoint()
	{
		TomCat::Scene scene;
		TomCat::Entity bodyA = AddCircleBody(scene, "Joint A",
			TomCat::Rigidbody2D::BodyType::Dynamic, { -2.0f, 5.0f });
		TomCat::Entity bodyB = AddCircleBody(scene, "Joint B",
			TomCat::Rigidbody2D::BodyType::Dynamic, { 2.0f, 5.0f });
		auto& joint = bodyA.AddComponent<TomCat::DistanceJoint2D>();
		joint.ConnectedEntity = bodyB.GetUUID();
		joint.Distance = 2.0f;
		joint.Frequency = 0.0f;
		joint.Damping = 0.0f;

		scene.OnRuntimeStart();
		Require(joint.RuntimeJoint != nullptr, "DistanceJoint2D did not create a Box2D joint");
		for (int step = 0; step < 120; ++step)
			scene.OnRuntimeStep();
		auto* runtimeA = static_cast<b2Body*>(bodyA.GetComponent<TomCat::Rigidbody2D>().RuntimeBody);
		auto* runtimeB = static_cast<b2Body*>(bodyB.GetComponent<TomCat::Rigidbody2D>().RuntimeBody);
		Require(runtimeA && runtimeB, "joint bodies were not created");
		const float distance = (runtimeB->GetPosition() - runtimeA->GetPosition()).Length();
		Require(Near(distance, 2.0f, 2.0e-2f), "DistanceJoint2D did not maintain its configured length");

		joint.Distance = 1.0f;
		scene.OnRuntimeStep();
		Require(joint.RuntimeJoint != nullptr, "runtime joint edit did not recreate the Box2D joint");
		for (int step = 0; step < 120; ++step)
			scene.OnRuntimeStep();
		runtimeA = static_cast<b2Body*>(bodyA.GetComponent<TomCat::Rigidbody2D>().RuntimeBody);
		runtimeB = static_cast<b2Body*>(bodyB.GetComponent<TomCat::Rigidbody2D>().RuntimeBody);
		Require(Near((runtimeB->GetPosition() - runtimeA->GetPosition()).Length(), 1.0f, 2.0e-2f),
			"runtime Distance edit was not reflected in the Box2D joint");

		bodyA.RemoveComponent<TomCat::DistanceJoint2D>();
		scene.OnRuntimeStep();
		auto& replacementJoint = bodyA.AddComponent<TomCat::DistanceJoint2D>();
		replacementJoint.ConnectedEntity = bodyB.GetUUID();
		replacementJoint.Distance = 1.0f;
		scene.OnRuntimeStep();
		Require(replacementJoint.RuntimeJoint != nullptr,
			"runtime DistanceJoint2D removal/addition did not rebuild safely");
		scene.DestroyEntity(bodyB);
		scene.OnRuntimeStep();
		Require(replacementJoint.RuntimeJoint == nullptr
			&& static_cast<uint64_t>(replacementJoint.ConnectedEntity) == 0,
			"deleting a connected entity left a dangling joint reference or runtime joint");
		scene.OnRuntimeStop();
	}

	void RequireSameCircleFields(const TomCat::CircleCollider2D& actual,
		const TomCat::CircleCollider2D& expected, const char* context)
	{
		Require(actual.Enabled == expected.Enabled, context);
		Require(actual.IsTrigger == expected.IsTrigger, context);
		Require(actual.CollisionLayer == expected.CollisionLayer, context);
		Require(actual.CollisionMask == expected.CollisionMask, context);
		Require(Near(actual.Offset.x, expected.Offset.x) && Near(actual.Offset.y, expected.Offset.y), context);
		Require(Near(actual.Radius, expected.Radius), context);
		Require(Near(actual.Density, expected.Density), context);
		Require(Near(actual.Friction, expected.Friction), context);
		Require(Near(actual.Restitution, expected.Restitution), context);
	}

	void RequireSameBoxFields(const TomCat::BoxCollider2D& actual,
		const TomCat::BoxCollider2D& expected, const char* context)
	{
		Require(actual.Enabled == expected.Enabled, context);
		Require(actual.IsTrigger == expected.IsTrigger, context);
		Require(actual.CollisionLayer == expected.CollisionLayer, context);
		Require(actual.CollisionMask == expected.CollisionMask, context);
		Require(Near(actual.Offset.x, expected.Offset.x) && Near(actual.Offset.y, expected.Offset.y), context);
		Require(Near(actual.Size.x, expected.Size.x) && Near(actual.Size.y, expected.Size.y), context);
		Require(Near(actual.Density, expected.Density), context);
		Require(Near(actual.Friction, expected.Friction), context);
		Require(Near(actual.Restitution, expected.Restitution), context);
		Require(Near(actual.RestitutionThreshold, expected.RestitutionThreshold), context);
	}

	void RequireSameJointFields(const TomCat::DistanceJoint2D& actual,
		const TomCat::DistanceJoint2D& expected, const char* context)
	{
		Require(actual.Enabled == expected.Enabled, context);
		Require(actual.ConnectedEntity == expected.ConnectedEntity, context);
		Require(Near(actual.Anchor.x, expected.Anchor.x) && Near(actual.Anchor.y, expected.Anchor.y), context);
		Require(Near(actual.ConnectedAnchor.x, expected.ConnectedAnchor.x)
			&& Near(actual.ConnectedAnchor.y, expected.ConnectedAnchor.y), context);
		Require(Near(actual.Distance, expected.Distance), context);
		Require(Near(actual.Frequency, expected.Frequency), context);
		Require(Near(actual.Damping, expected.Damping), context);
		Require(actual.CollideConnected == expected.CollideConnected, context);
	}

	void RequireSameScriptField(const TomCat::ScriptField& actual,
		const TomCat::ScriptField& expected, const char* context)
	{
		Require(actual.FieldID == expected.FieldID && actual.Name == expected.Name
			&& actual.Type == expected.Type, context);
		Require(TomCat::IsScriptFieldValueCompatible(actual.Type, actual.Value)
			&& TomCat::IsScriptFieldValueCompatible(expected.Type, expected.Value),
			context);
		switch (actual.Type)
		{
			case TomCat::ScriptFieldType::Bool:
				Require(std::get<bool>(actual.Value) == std::get<bool>(expected.Value),
					context);
				break;
			case TomCat::ScriptFieldType::Int32:
				Require(std::get<int32_t>(actual.Value)
					== std::get<int32_t>(expected.Value), context);
				break;
			case TomCat::ScriptFieldType::Int64:
			case TomCat::ScriptFieldType::Enum:
				Require(std::get<int64_t>(actual.Value)
					== std::get<int64_t>(expected.Value), context);
				break;
			case TomCat::ScriptFieldType::Float:
				Require(Near(std::get<float>(actual.Value),
					std::get<float>(expected.Value)), context);
				break;
			case TomCat::ScriptFieldType::Double:
				Require(std::abs(std::get<double>(actual.Value)
					- std::get<double>(expected.Value)) <= 1.0e-10, context);
				break;
			case TomCat::ScriptFieldType::String:
				Require(std::get<std::string>(actual.Value)
					== std::get<std::string>(expected.Value), context);
				break;
			case TomCat::ScriptFieldType::Vector2:
				Require(Near(std::get<glm::vec2>(actual.Value),
					std::get<glm::vec2>(expected.Value)), context);
				break;
			case TomCat::ScriptFieldType::Vector3:
			{
				const glm::vec3& left = std::get<glm::vec3>(actual.Value);
				const glm::vec3& right = std::get<glm::vec3>(expected.Value);
				Require(Near(left.x, right.x) && Near(left.y, right.y)
					&& Near(left.z, right.z), context);
				break;
			}
			case TomCat::ScriptFieldType::Vector4:
			case TomCat::ScriptFieldType::Color:
			{
				const glm::vec4& left = std::get<glm::vec4>(actual.Value);
				const glm::vec4& right = std::get<glm::vec4>(expected.Value);
				Require(Near(left.x, right.x) && Near(left.y, right.y)
					&& Near(left.z, right.z) && Near(left.w, right.w), context);
				break;
			}
			case TomCat::ScriptFieldType::Entity:
			case TomCat::ScriptFieldType::AssetRef:
				Require(std::get<uint64_t>(actual.Value)
					== std::get<uint64_t>(expected.Value), context);
				break;
		}
	}

	void RequireSameCSharpScripts(const TomCat::CSharpScripts& actual,
		const TomCat::CSharpScripts& expected, bool sameAttachmentIDs,
		const char* context)
	{
		Require(actual.Scripts.size() == expected.Scripts.size(), context);
		for (size_t scriptIndex = 0; scriptIndex < actual.Scripts.size();
			++scriptIndex)
		{
			const auto& left = actual.Scripts[scriptIndex];
			const auto& right = expected.Scripts[scriptIndex];
			Require((left.AttachmentID == right.AttachmentID)
				== sameAttachmentIDs, context);
			Require(left.Enabled == right.Enabled
				&& left.ScriptAsset == right.ScriptAsset
				&& left.LastKnownClassName == right.LastKnownClassName
				&& left.Fields.size() == right.Fields.size(), context);
			for (size_t fieldIndex = 0; fieldIndex < left.Fields.size();
				++fieldIndex)
				RequireSameScriptField(left.Fields[fieldIndex],
					right.Fields[fieldIndex], context);
		}
	}

	void TestSchemaV11PersistenceAndCopies()
	{
		TemporaryCookedProject environment;
		std::filesystem::create_directories(environment.Root);
		const std::filesystem::path scenePath = environment.Root / "physics_v11_roundtrip.tomcat";
		auto source = TomCat::CreateRef<TomCat::Scene>();
		source->SetSceneName("Physics v11 roundtrip");
		TomCat::Entity entity = source->CreateEntity("Circle source");
		Require(entity.HasComponent<TomCat::EntityMetadata>(),
			"CreateEntity omitted required EntityMetadata");
		Require(entity.GetComponent<TomCat::EntityMetadata>().GameplayTag == "Untagged"
			&& entity.GetComponent<TomCat::EntityMetadata>().Layer == 0
			&& entity.GetComponent<TomCat::EntityMetadata>().HierarchyIcon
				== TomCat::EntityIconMode::Entity,
			"CreateEntity did not initialize default EntityMetadata");
		entity.GetComponent<TomCat::EntityMetadata>().GameplayTag = "Player";
		entity.GetComponent<TomCat::EntityMetadata>().Layer = 3;
		entity.GetComponent<TomCat::EntityMetadata>().HierarchyIcon = TomCat::EntityIconMode::Sprite;
		const TomCat::UUID sourceUUID = entity.GetUUID();
		auto& circle = entity.AddComponent<TomCat::CircleCollider2D>();
		circle.Enabled = false;
		circle.IsTrigger = true;
		circle.CollisionLayer = 0x0040;
		circle.CollisionMask = 0x0081;
		circle.Offset = { 1.25f, -2.5f };
		circle.Radius = 3.75f;
		circle.Density = 2.25f;
		circle.Friction = 1.25f;
		circle.Restitution = 0.65f;
		circle.RuntimeFixture = reinterpret_cast<void*>(static_cast<uintptr_t>(0x1234));
		const TomCat::CircleCollider2D expectedCircle = circle;

		TomCat::Entity target = source->CreateEntity("Joint target");
		target.GetComponent<TomCat::EntityMetadata>().GameplayTag = "Ground";
		target.GetComponent<TomCat::EntityMetadata>().Layer = 7;
		target.GetComponent<TomCat::EntityMetadata>().HierarchyIcon = TomCat::EntityIconMode::Collider2D;
		const TomCat::UUID targetUUID = target.GetUUID();
		auto& box = target.AddComponent<TomCat::BoxCollider2D>();
		box.Enabled = false;
		box.IsTrigger = true;
		box.CollisionLayer = 0x0200;
		box.CollisionMask = 0x0104;
		box.Offset = { -1.5f, 2.75f };
		box.Size = { 4.0f, 5.0f };
		box.Density = 3.0f;
		box.Friction = 0.25f;
		box.Restitution = 0.8f;
		box.RestitutionThreshold = 1.75f;
		box.RuntimeFixture = reinterpret_cast<void*>(static_cast<uintptr_t>(0x3456));
		const TomCat::BoxCollider2D expectedBox = box;
		constexpr std::array<TomCat::EntityIconMode, 6> iconModes = {
			TomCat::EntityIconMode::Automatic,
			TomCat::EntityIconMode::Entity,
			TomCat::EntityIconMode::Camera,
			TomCat::EntityIconMode::Sprite,
			TomCat::EntityIconMode::Rigidbody2D,
			TomCat::EntityIconMode::Collider2D
		};
		std::array<TomCat::UUID, iconModes.size()> iconModeEntityUUIDs{};
		for (std::size_t index = 0; index < iconModes.size(); ++index)
		{
			TomCat::Entity iconEntity = source->CreateEntity(
				"Icon mode " + std::to_string(index));
			iconEntity.GetComponent<TomCat::EntityMetadata>().HierarchyIcon = iconModes[index];
			iconModeEntityUUIDs[index] = iconEntity.GetUUID();
		}
		auto& joint = entity.AddComponent<TomCat::DistanceJoint2D>();
		joint.Enabled = false;
		joint.ConnectedEntity = targetUUID;
		joint.Anchor = { 1.0f, 2.0f };
		joint.ConnectedAnchor = { -3.0f, 4.0f };
		joint.Distance = 5.0f;
		joint.Frequency = 2.0f;
		joint.Damping = 0.75f;
		joint.CollideConnected = true;
		joint.RuntimeJoint = reinterpret_cast<void*>(static_cast<uintptr_t>(0x5678));
		const TomCat::DistanceJoint2D expectedJoint = joint;

		auto& csharpScripts = entity.AddComponent<TomCat::CSharpScripts>();
		TomCat::CSharpScriptEntry controller;
		controller.Enabled = true;
		controller.ScriptAsset = TomCat::AssetHandle(7001);
		controller.LastKnownClassName = "Game.PlayerController";
		controller.Fields.emplace_back("00000000000000000000000000000001",
			"_enabled", TomCat::ScriptFieldType::Bool, true);
		controller.Fields.emplace_back("00000000000000000000000000000002",
			"_lives", TomCat::ScriptFieldType::Int32, int32_t{ -42 });
		controller.Fields.emplace_back("00000000000000000000000000000003",
			"_score", TomCat::ScriptFieldType::Int64, int64_t{ -5000000000LL });
		controller.Fields.emplace_back("00000000000000000000000000000004",
			"_speed", TomCat::ScriptFieldType::Float, 12.25f);
		controller.Fields.emplace_back("00000000000000000000000000000005",
			"_precision", TomCat::ScriptFieldType::Double, -0.125);
		controller.Fields.emplace_back("00000000000000000000000000000006",
			"_label", TomCat::ScriptFieldType::String,
			std::string("saved even when metadata disappears"));
		controller.Fields.emplace_back("00000000000000000000000000000007",
			"_v2", TomCat::ScriptFieldType::Vector2,
			glm::vec2{ 1.5f, -2.5f });
		controller.Fields.emplace_back("00000000000000000000000000000008",
			"_v3", TomCat::ScriptFieldType::Vector3,
			glm::vec3{ 1.0f, 2.0f, 3.0f });
		controller.Fields.emplace_back("00000000000000000000000000000009",
			"_v4", TomCat::ScriptFieldType::Vector4,
			glm::vec4{ 4.0f, 5.0f, 6.0f, 7.0f });
		controller.Fields.emplace_back("0000000000000000000000000000000a",
			"_color", TomCat::ScriptFieldType::Color,
			glm::vec4{ 0.1f, 0.2f, 0.3f, 0.4f });
		controller.Fields.emplace_back("0000000000000000000000000000000b",
			"_state", TomCat::ScriptFieldType::Enum, int64_t{ -3 });
		controller.Fields.emplace_back("0000000000000000000000000000000c",
			"_target", TomCat::ScriptFieldType::Entity,
			static_cast<uint64_t>(targetUUID));
		controller.Fields.emplace_back("0000000000000000000000000000000d",
			"_prefab", TomCat::ScriptFieldType::AssetRef, uint64_t{ 9001 });
		csharpScripts.Scripts.emplace_back(std::move(controller));

		// This attachment and field intentionally have no current compiler metadata.
		// They must remain as raw authoring data rather than being deleted.
		TomCat::CSharpScriptEntry missing;
		missing.Enabled = false;
		missing.ScriptAsset = TomCat::AssetHandle(7002);
		missing.LastKnownClassName = "Game.RemovedBehaviour";
		missing.Fields.emplace_back("ffffffffffffffffffffffffffffffff",
			"_removedField", TomCat::ScriptFieldType::String,
			std::string("orphan-value"));
		csharpScripts.Scripts.emplace_back(std::move(missing));
		const TomCat::CSharpScripts expectedScripts = csharpScripts;

		auto copiedScene = TomCat::Scene::Copy(source);
		Require(copiedScene != nullptr, "Scene::Copy failed for schema-v11 components");
		TomCat::Entity copiedEntity = copiedScene->FindEntityByUUID(sourceUUID);
		Require(copiedEntity && copiedEntity.HasComponent<TomCat::CircleCollider2D>()
			&& copiedEntity.HasComponent<TomCat::DistanceJoint2D>()
			&& copiedEntity.HasComponent<TomCat::EntityMetadata>()
			&& copiedEntity.HasComponent<TomCat::CSharpScripts>(),
			"Scene::Copy omitted a schema-v11 component");
		Require(copiedEntity.GetComponent<TomCat::EntityMetadata>().GameplayTag == "Player"
			&& copiedEntity.GetComponent<TomCat::EntityMetadata>().Layer == 3
			&& copiedEntity.GetComponent<TomCat::EntityMetadata>().HierarchyIcon
				== TomCat::EntityIconMode::Sprite,
			"Scene::Copy changed EntityMetadata");
		RequireSameCircleFields(copiedEntity.GetComponent<TomCat::CircleCollider2D>(), expectedCircle,
			"Scene::Copy changed CircleCollider2D fields");
		RequireSameJointFields(copiedEntity.GetComponent<TomCat::DistanceJoint2D>(), expectedJoint,
			"Scene::Copy changed DistanceJoint2D fields");
		RequireSameCSharpScripts(copiedEntity.GetComponent<TomCat::CSharpScripts>(),
			expectedScripts, true,
			"Scene::Copy changed C# fields or stable attachment identities");
		Require(copiedEntity.GetComponent<TomCat::CircleCollider2D>().RuntimeFixture == nullptr
			&& copiedEntity.GetComponent<TomCat::DistanceJoint2D>().RuntimeJoint == nullptr,
			"Scene::Copy retained runtime physics pointers");
		TomCat::Entity copiedTarget = copiedScene->FindEntityByUUID(targetUUID);
		Require(copiedTarget && copiedTarget.HasComponent<TomCat::BoxCollider2D>(),
			"Scene::Copy omitted BoxCollider2D");
		Require(copiedTarget.GetComponent<TomCat::EntityMetadata>().GameplayTag == "Ground"
			&& copiedTarget.GetComponent<TomCat::EntityMetadata>().Layer == 7
			&& copiedTarget.GetComponent<TomCat::EntityMetadata>().HierarchyIcon
				== TomCat::EntityIconMode::Collider2D,
			"Scene::Copy changed target EntityMetadata");
		RequireSameBoxFields(copiedTarget.GetComponent<TomCat::BoxCollider2D>(), expectedBox,
			"Scene::Copy changed BoxCollider2D fields");
		Require(copiedTarget.GetComponent<TomCat::BoxCollider2D>().RuntimeFixture == nullptr,
			"Scene::Copy retained BoxCollider2D RuntimeFixture");

		TomCat::Entity duplicate = source->DuplicateEntity(entity);
		Require(duplicate && duplicate.HasComponent<TomCat::CircleCollider2D>()
			&& duplicate.HasComponent<TomCat::DistanceJoint2D>()
			&& duplicate.HasComponent<TomCat::EntityMetadata>()
			&& duplicate.HasComponent<TomCat::CSharpScripts>(),
			"DuplicateEntity omitted a schema-v11 component");
		Require(duplicate.GetComponent<TomCat::EntityMetadata>().GameplayTag == "Player"
			&& duplicate.GetComponent<TomCat::EntityMetadata>().Layer == 3
			&& duplicate.GetComponent<TomCat::EntityMetadata>().HierarchyIcon
				== TomCat::EntityIconMode::Sprite,
			"DuplicateEntity changed EntityMetadata");
		RequireSameCircleFields(duplicate.GetComponent<TomCat::CircleCollider2D>(), expectedCircle,
			"DuplicateEntity changed CircleCollider2D fields");
		RequireSameJointFields(duplicate.GetComponent<TomCat::DistanceJoint2D>(), expectedJoint,
			"DuplicateEntity changed DistanceJoint2D fields");
		RequireSameCSharpScripts(duplicate.GetComponent<TomCat::CSharpScripts>(),
			expectedScripts, false,
			"DuplicateEntity failed to preserve fields with fresh attachment identities");
		Require(duplicate.GetComponent<TomCat::CircleCollider2D>().RuntimeFixture == nullptr
			&& duplicate.GetComponent<TomCat::DistanceJoint2D>().RuntimeJoint == nullptr,
			"DuplicateEntity retained runtime physics pointers");
		TomCat::Entity duplicateTarget = source->DuplicateEntity(target);
		Require(duplicateTarget && duplicateTarget.HasComponent<TomCat::BoxCollider2D>(),
			"DuplicateEntity omitted BoxCollider2D");
		Require(duplicateTarget.GetComponent<TomCat::EntityMetadata>().GameplayTag == "Ground"
			&& duplicateTarget.GetComponent<TomCat::EntityMetadata>().Layer == 7
			&& duplicateTarget.GetComponent<TomCat::EntityMetadata>().HierarchyIcon
				== TomCat::EntityIconMode::Collider2D,
			"DuplicateEntity changed target EntityMetadata");
		RequireSameBoxFields(duplicateTarget.GetComponent<TomCat::BoxCollider2D>(), expectedBox,
			"DuplicateEntity changed BoxCollider2D fields");
		Require(duplicateTarget.GetComponent<TomCat::BoxCollider2D>().RuntimeFixture == nullptr,
			"DuplicateEntity retained BoxCollider2D RuntimeFixture");

		TomCat::SceneSerializer writer(source);
		Require(writer.Serialize(scenePath), "schema-v11 scene serialization failed");
		Require(TomCat::SceneSerializer::ValidateCurrentFormat(scenePath),
			"serialized schema-v11 scene failed strict validation");
		const std::string serialized = ReadTextFile(scenePath);
		Require(serialized.find("SchemaVersion: 11") != std::string::npos,
			"serialized scene did not declare schema v11");
		Require(serialized.find("IsTrigger:") != std::string::npos
			&& serialized.find("CollisionLayer:") != std::string::npos
			&& serialized.find("CollisionMask:") != std::string::npos
			&& serialized.find("DistanceJoint2D:") != std::string::npos
			&& serialized.find("EntityMetadata:") != std::string::npos
			&& serialized.find("GameplayTag: Player") != std::string::npos
			&& serialized.find("Layer: 3") != std::string::npos
			&& serialized.find("HierarchyIcon: Sprite") != std::string::npos
			&& serialized.find("CSharpScripts:") != std::string::npos
			&& serialized.find("Type: AssetRef") != std::string::npos
			&& serialized.find("orphan-value") != std::string::npos,
			"serialized scene omitted schema-v11 script, metadata, or physics fields");

		auto loaded = TomCat::CreateRef<TomCat::Scene>();
		TomCat::SceneSerializer reader(loaded);
		Require(reader.Deserialize(scenePath), "schema-v11 scene deserialization failed");
		TomCat::Entity loadedEntity = loaded->FindEntityByUUID(sourceUUID);
		Require(loadedEntity && loadedEntity.HasComponent<TomCat::CircleCollider2D>()
			&& loadedEntity.HasComponent<TomCat::DistanceJoint2D>()
			&& loadedEntity.HasComponent<TomCat::EntityMetadata>()
			&& loadedEntity.HasComponent<TomCat::CSharpScripts>(),
			"loaded scene omitted a schema-v11 component");
		Require(loadedEntity.GetComponent<TomCat::EntityMetadata>().GameplayTag == "Player"
			&& loadedEntity.GetComponent<TomCat::EntityMetadata>().Layer == 3
			&& loadedEntity.GetComponent<TomCat::EntityMetadata>().HierarchyIcon
				== TomCat::EntityIconMode::Sprite,
			"save/load changed EntityMetadata");
		RequireSameCircleFields(loadedEntity.GetComponent<TomCat::CircleCollider2D>(), expectedCircle,
			"save/load changed CircleCollider2D fields");
		RequireSameJointFields(loadedEntity.GetComponent<TomCat::DistanceJoint2D>(), expectedJoint,
			"save/load changed DistanceJoint2D fields");
		RequireSameCSharpScripts(loadedEntity.GetComponent<TomCat::CSharpScripts>(),
			expectedScripts, true,
			"save/load changed C# fields, orphans, or attachment identities");
		Require(loadedEntity.GetComponent<TomCat::CircleCollider2D>().RuntimeFixture == nullptr
			&& loadedEntity.GetComponent<TomCat::DistanceJoint2D>().RuntimeJoint == nullptr,
			"save/load restored runtime physics pointers");
		TomCat::Entity loadedTarget = loaded->FindEntityByUUID(targetUUID);
		Require(loadedTarget && loadedTarget.HasComponent<TomCat::BoxCollider2D>(),
			"loaded scene omitted BoxCollider2D");
		Require(loadedTarget.GetComponent<TomCat::EntityMetadata>().GameplayTag == "Ground"
			&& loadedTarget.GetComponent<TomCat::EntityMetadata>().Layer == 7
			&& loadedTarget.GetComponent<TomCat::EntityMetadata>().HierarchyIcon
				== TomCat::EntityIconMode::Collider2D,
			"save/load changed target EntityMetadata");
		RequireSameBoxFields(loadedTarget.GetComponent<TomCat::BoxCollider2D>(), expectedBox,
			"save/load changed BoxCollider2D fields");
		Require(loadedTarget.GetComponent<TomCat::BoxCollider2D>().RuntimeFixture == nullptr,
			"save/load restored BoxCollider2D RuntimeFixture");
		for (std::size_t index = 0; index < iconModes.size(); ++index)
		{
			TomCat::Entity loadedIconEntity = loaded->FindEntityByUUID(iconModeEntityUUIDs[index]);
			Require(loadedIconEntity
				&& loadedIconEntity.GetComponent<TomCat::EntityMetadata>().HierarchyIcon
					== iconModes[index],
				"save/load changed one of the supported hierarchy icon tokens");
		}

		YAML::Node legacyV10Node = YAML::Load(serialized);
		legacyV10Node["SchemaVersion"] = 10;
		for (YAML::Node entityNode : legacyV10Node["Entities"])
			entityNode.remove("Components");
		YAML::Emitter legacyV10Emitter;
		legacyV10Emitter << legacyV10Node;
		const std::filesystem::path legacyV10AdapterPath =
			environment.Root / "legacy_v10_accepted.tomcat";
		WriteTextFile(legacyV10AdapterPath, legacyV10Emitter.c_str());
		Require(TomCat::SceneSerializer::ValidateCurrentFormat(legacyV10AdapterPath),
			"schema-v10 adapter input was rejected");
		auto legacyV10Loaded = TomCat::CreateRef<TomCat::Scene>();
		Require(TomCat::SceneSerializer(legacyV10Loaded).Deserialize(legacyV10AdapterPath)
			&& legacyV10Loaded->FindEntityByUUID(sourceUUID)
				.HasComponent<TomCat::CSharpScripts>(),
			"schema-v10 adapter lost CSharpScripts");

		std::string obsolete = serialized;
		const size_t version = obsolete.find("SchemaVersion: 11");
		Require(version != std::string::npos, "could not locate serialized schema version");
		obsolete.replace(version, std::string("SchemaVersion: 11").size(), "SchemaVersion: 8");
		const std::filesystem::path obsoletePath = environment.Root / "schema_v8_rejected.tomcat";
		WriteTextFile(obsoletePath, obsolete);
		Require(!TomCat::SceneSerializer::ValidateCurrentFormat(obsoletePath),
			"strict current-format validation accepted schema v8");
		auto obsoleteTarget = TomCat::CreateRef<TomCat::Scene>();
		TomCat::SceneSerializer obsoleteReader(obsoleteTarget);
		Require(!obsoleteReader.Deserialize(obsoletePath),
			"scene reader accepted schema v8 instead of requiring schema 9 through 11");

		auto legacySource = TomCat::CreateRef<TomCat::Scene>();
		legacySource->SetSceneName("Schema 9 migration");
		legacySource->CreateEntity("Legacy entity");
		const std::filesystem::path legacyV10Path =
			environment.Root / "legacy_source_v11.tomcat";
		TomCat::SceneSerializer legacyWriter(legacySource);
		Require(legacyWriter.Serialize(legacyV10Path),
			"could not create a script-free schema-v11 migration fixture");
		YAML::Node legacyV9Node = YAML::Load(ReadTextFile(legacyV10Path));
		legacyV9Node["SchemaVersion"] = 9;
		for (YAML::Node entityNode : legacyV9Node["Entities"])
			entityNode.remove("Components");
		YAML::Emitter legacyV9Emitter;
		legacyV9Emitter << legacyV9Node;
		std::string legacyV9 = legacyV9Emitter.c_str();
		const std::filesystem::path legacyV9Path =
			environment.Root / "legacy_v9_accepted.tomcat";
		WriteTextFile(legacyV9Path, legacyV9);
		Require(TomCat::SceneSerializer::ValidateCurrentFormat(legacyV9Path),
			"schema-v9 migration input was rejected");
		auto legacyLoaded = TomCat::CreateRef<TomCat::Scene>();
		TomCat::SceneSerializer legacyReader(legacyLoaded);
		Require(legacyReader.Deserialize(legacyV9Path),
			"schema-v9 migration input did not deserialize");
		Require(!legacyLoaded->GetRootEntityUUIDs().empty(),
			"schema-v9 migration input lost its entity");
		const std::filesystem::path migratedV10Path =
			environment.Root / "legacy_resaved_as_v11.tomcat";
		TomCat::SceneSerializer migratedWriter(legacyLoaded);
		Require(migratedWriter.Serialize(migratedV10Path)
			&& ReadTextFile(migratedV10Path).find("SchemaVersion: 11")
				!= std::string::npos,
			"saving schema-v9 migration input did not upgrade it to schema 11");

		YAML::Node illegalV9Node = YAML::Load(serialized);
		illegalV9Node["SchemaVersion"] = 9;
		for (YAML::Node entityNode : illegalV9Node["Entities"])
			entityNode.remove("Components");
		YAML::Emitter illegalV9Emitter;
		illegalV9Emitter << illegalV9Node;
		std::string illegalV9Scripts = illegalV9Emitter.c_str();
		const std::filesystem::path illegalV9ScriptsPath =
			environment.Root / "schema_v9_with_v10_scripts.tomcat";
		WriteTextFile(illegalV9ScriptsPath, illegalV9Scripts);
		Require(!TomCat::SceneSerializer::ValidateCurrentFormat(
			illegalV9ScriptsPath),
			"strict schema-v9 migration accepted a schema-v10 CSharpScripts field");

		auto duplicateAttachmentScene = TomCat::CreateRef<TomCat::Scene>();
		TomCat::CSharpScriptEntry duplicateAttachment;
		duplicateAttachment.AttachmentID = TomCat::UUID(424242);
		duplicateAttachment.ScriptAsset = TomCat::AssetHandle(7001);
		duplicateAttachment.LastKnownClassName = "Game.DuplicateAttachment";
		duplicateAttachmentScene->CreateEntity("First attachment owner")
			.AddComponent<TomCat::CSharpScripts>().Scripts.push_back(
				duplicateAttachment);
		duplicateAttachmentScene->CreateEntity("Second attachment owner")
			.AddComponent<TomCat::CSharpScripts>().Scripts.push_back(
				duplicateAttachment);
		TomCat::SceneSerializer duplicateAttachmentWriter(duplicateAttachmentScene);
		Require(!duplicateAttachmentWriter.Serialize(
			environment.Root / "duplicate_attachment_ids.tomcat"),
			"scene writer accepted one C# AttachmentID on two different entities");

		auto invalidScene = TomCat::CreateRef<TomCat::Scene>();
		TomCat::Entity invalidOwner = invalidScene->CreateEntity("Invalid joint owner");
		invalidOwner.AddComponent<TomCat::DistanceJoint2D>().ConnectedEntity = TomCat::UUID(9999999);
		const std::filesystem::path invalidPath = environment.Root / "invalid_joint.tomcat";
		TomCat::SceneSerializer invalidWriter(invalidScene);
		Require(!invalidWriter.Serialize(invalidPath),
			"scene writer emitted an unresolved DistanceJoint2D reference");

		auto invalidMetadataScene = TomCat::CreateRef<TomCat::Scene>();
		TomCat::Entity invalidMetadata = invalidMetadataScene->CreateEntity("Invalid metadata");
		invalidMetadata.GetComponent<TomCat::EntityMetadata>().Layer =
			static_cast<uint8_t>(TomCat::Physics2DLayerCount);
		TomCat::SceneSerializer invalidMetadataWriter(invalidMetadataScene);
		Require(!invalidMetadataWriter.Serialize(environment.Root / "invalid_metadata_layer.tomcat"),
			"scene writer accepted an out-of-range EntityMetadata layer");
		invalidMetadata.GetComponent<TomCat::EntityMetadata>().Layer = 0;
		invalidMetadata.GetComponent<TomCat::EntityMetadata>().GameplayTag.clear();
		Require(!invalidMetadataWriter.Serialize(environment.Root / "invalid_metadata_tag.tomcat"),
			"scene writer accepted an empty EntityMetadata gameplay tag");
		invalidMetadata.GetComponent<TomCat::EntityMetadata>().GameplayTag = "Untagged";
		invalidMetadata.GetComponent<TomCat::EntityMetadata>().HierarchyIcon =
			static_cast<TomCat::EntityIconMode>(255);
		Require(!invalidMetadataWriter.Serialize(environment.Root / "invalid_metadata_icon.tomcat"),
			"scene writer accepted an invalid EntityMetadata hierarchy icon");

		std::string unknownIcon = serialized;
		const size_t iconToken = unknownIcon.find("HierarchyIcon: Sprite");
		Require(iconToken != std::string::npos, "could not locate serialized hierarchy icon token");
		unknownIcon.replace(iconToken, std::string("HierarchyIcon: Sprite").size(),
			"HierarchyIcon: Unknown");
		const std::filesystem::path unknownIconPath = environment.Root / "unknown_icon.tomcat";
		WriteTextFile(unknownIconPath, unknownIcon);
		Require(!TomCat::SceneSerializer::ValidateCurrentFormat(unknownIconPath),
			"scene validator accepted an unknown hierarchy icon token");

		std::string missingIcon = serialized;
		const size_t missingIconToken = missingIcon.find("HierarchyIcon: Sprite");
		Require(missingIconToken != std::string::npos,
			"could not locate hierarchy icon field for missing-field validation");
		const size_t missingIconLineStart = missingIcon.rfind('\n', missingIconToken);
		const size_t missingIconLineEnd = missingIcon.find('\n', missingIconToken);
		Require(missingIconLineEnd != std::string::npos,
			"serialized hierarchy icon field did not end with a newline");
		missingIcon.erase(missingIconLineStart == std::string::npos ? 0 : missingIconLineStart + 1,
			missingIconLineEnd - (missingIconLineStart == std::string::npos ? 0 : missingIconLineStart + 1) + 1);
		const std::filesystem::path missingIconPath = environment.Root / "missing_icon.tomcat";
		WriteTextFile(missingIconPath, missingIcon);
		Require(!TomCat::SceneSerializer::ValidateCurrentFormat(missingIconPath),
			"scene validator accepted EntityMetadata without HierarchyIcon");
	}

	void TestCookedPlayerPhysicsRoundtrip()
	{
		Require(TomCat::AssetTypeFromPath("Player.cs")
				== TomCat::AssetType::CSharpScript
			&& TomCat::AssetTypeFromPath("Player.cpp")
				== TomCat::AssetType::Other
			&& TomCat::AssetTypeFromPath("Player.h")
				== TomCat::AssetType::Other
			&& TomCat::AssetTypeFromPath("Player.lua")
				== TomCat::AssetType::Other,
			"asset classification still mixes C#, C++, headers, or Lua");
		Require(TomCat::AssetTypeFromPath("Music.wav") == TomCat::AssetType::Audio
			&& TomCat::AssetTypeFromPath("Music.WAV") == TomCat::AssetType::Audio
			&& TomCat::AssetTypeFromPath("Music.ogg") == TomCat::AssetType::Other
			&& TomCat::AssetTypeFromPath("Music.mp3") == TomCat::AssetType::Other
			&& TomCat::AssetTypeFromPath("Music.flac") == TomCat::AssetType::Other
			&& TomCat::AssetTypeFromPath("Text.ttf") == TomCat::AssetType::Font
			&& TomCat::AssetTypeFromPath("Text.TTF") == TomCat::AssetType::Font
			&& TomCat::AssetTypeFromPath("Text.otf") == TomCat::AssetType::Font
			&& TomCat::AssetTypeFromPath("Text.ttc") == TomCat::AssetType::Font
			&& TomCat::AssetTypeFromPath("Text.woff") == TomCat::AssetType::Other
			&& TomCat::AssetTypeFromPath("Text.woff2") == TomCat::AssetType::Other,
			"asset discovery advertises a format without a P0 importer/Player path");

		TemporaryCookedProject environment;
		TomCat::ProjectConfig config;
		config.Name = "Physics Cook Regression";
		config.Template = "2D";
		config.AssetDirectory = "Assets";
		config.StartScene = "Main.tomcat";
		config.StartSceneHandle = TomCat::AssetHandle(0);
		auto project = TomCat::Project::CreateNew(
			environment.Root / "Project.tcproj", config);
		Require(project != nullptr, "could not create temporary project for cooked physics test");
		TomCat::ProjectSettings cookedSettings;
		cookedSettings.TagsAndLayers.Tags = { "Untagged", "Ground", "Player" };
		cookedSettings.TagsAndLayers.LayerNames[1] = "Ground";
		cookedSettings.TagsAndLayers.LayerNames[2] = "Player";
		cookedSettings.Physics2D.SetLayersCollide(1, 2, false);
		Require(project->SetSettings(cookedSettings),
			"could not persist the cooked package's Physics2D matrix");

		const std::filesystem::path scriptPath =
			project->GetAssetPath() / "CookedPlayerProbe.cs";
		const std::filesystem::path iconPath =
			project->GetAssetPath() / "PlayerIcon.tga";
		const std::string sourceMarker =
			"TOMCAT_CSHARP_SOURCE_MUST_NOT_BE_COOKED_6b3a177b";
		const std::string csprojMarker =
			"TOMCAT_CSPROJ_MUST_NOT_BE_COOKED_9f71d0aa";
		const std::string objectMarker =
			"TOMCAT_OBJ_MUST_NOT_BE_COOKED_294cdc32";
		const std::string nestedLibraryMarker =
			"TOMCAT_LIBRARY_MUST_NOT_BE_COOKED_e98e59f1";
		WriteTextFile(scriptPath,
			"using TomCat; // " + sourceMarker
			+ "\npublic sealed class CookedPlayerProbe : TomCatBehaviour {}\n");
		WriteBinaryFile(iconPath, MakeTestTGA());
		std::filesystem::create_directories(project->GetAssetPath() / "obj");
		std::filesystem::create_directories(project->GetAssetPath() / "Library");
		WriteTextFile(project->GetAssetPath() / "Assembly-CSharp.csproj", csprojMarker);
		WriteTextFile(project->GetAssetPath() / "obj" / "authoring.bin", objectMarker);
		WriteTextFile(project->GetAssetPath() / "Library" / "authoring.bin",
			nestedLibraryMarker);

		TomCat::AssetManager& assets = TomCat::AssetManager::Get();
		Require(assets.SetProject(project),
			"could not initialize AssetManager for cooked physics test");
		const TomCat::AssetMetadata* scriptMetadata =
			assets.Registry().GetMetadata(scriptPath);
		Require(scriptMetadata && !scriptMetadata->IsMissing
			&& scriptMetadata->Type == TomCat::AssetType::CSharpScript
			&& static_cast<uint64_t>(scriptMetadata->Handle) != 0,
			".cs source did not receive a stable CSharpScript AssetHandle");
		const TomCat::AssetHandle scriptHandle = scriptMetadata->Handle;
		const TomCat::AssetMetadata* iconMetadata =
			assets.Registry().GetMetadata(iconPath);
		Require(iconMetadata && !iconMetadata->IsMissing
			&& iconMetadata->Type == TomCat::AssetType::Texture2D,
			"Player icon fixture was not imported as Texture2D");
		const TomCat::AssetHandle iconHandle = iconMetadata->Handle;
		TomCat::PlayerSettings cookedPlayerSettings = project->GetPlayerSettings();
		cookedPlayerSettings.ProductName = "Cooked Moon Rabbit";
		cookedPlayerSettings.CompanyName = "TomCat Regression Studio";
		cookedPlayerSettings.Version = "6.0.1";
		cookedPlayerSettings.Icon = iconHandle;
		cookedPlayerSettings.Width = 1366;
		cookedPlayerSettings.Height = 768;
		cookedPlayerSettings.WindowMode = TomCat::PlayerWindowMode::Borderless;
		cookedPlayerSettings.Resizable = false;
		cookedPlayerSettings.VSync = false;
		cookedPlayerSettings.SaveDirectory = "State/Saves";
		cookedPlayerSettings.LogDirectory = "State/Logs";
		cookedPlayerSettings.CrashDirectory = "State/Crashes";
		Require(project->SetPlayerSettings(cookedPlayerSettings),
			"could not persist the cooked package PlayerSettings fixture");

		auto source = TomCat::CreateRef<TomCat::Scene>();
		source->SetSceneName("Cooked physics v11");
		TomCat::Entity ground = source->CreateEntity("Cooked box");
		ground.GetComponent<TomCat::EntityMetadata>().GameplayTag = "Ground";
		ground.GetComponent<TomCat::EntityMetadata>().Layer = 1;
		ground.GetComponent<TomCat::EntityMetadata>().HierarchyIcon = TomCat::EntityIconMode::Collider2D;
		const TomCat::UUID groundUUID = ground.GetUUID();
		auto& cookedHealth = ground.AddComponent<TomCat::HealthComponent>();
		cookedHealth.Maximum = 320;
		cookedHealth.Current = 123;
		cookedHealth.Invulnerable = true;
		auto& scripts = ground.AddComponent<TomCat::CSharpScripts>();
		TomCat::CSharpScriptEntry script;
		script.ScriptAsset = scriptHandle;
		script.LastKnownClassName = "CookedPlayerProbe";
		script.Fields.emplace_back("11111111111111111111111111111111",
			"_orphanedBuildValue", TomCat::ScriptFieldType::Float, 6.5f);
		scripts.Scripts.emplace_back(std::move(script));
		const TomCat::CSharpScripts expectedScripts = scripts;
		ground.AddComponent<TomCat::Rigidbody2D>().Type =
			TomCat::Rigidbody2D::BodyType::Static;
		auto& box = ground.AddComponent<TomCat::BoxCollider2D>();
		box.IsTrigger = true;
		box.CollisionLayer = 0x0010;
		box.CollisionMask = 0x0020;
		box.Offset = { 0.5f, -0.25f };
		box.Size = { 2.0f, 0.75f };
		box.Density = 1.5f;
		box.Friction = 0.35f;
		box.Restitution = 0.45f;
		box.RestitutionThreshold = 1.25f;
		const TomCat::BoxCollider2D expectedBox = box;

		TomCat::Entity ball = source->CreateEntity("Cooked circle");
		ball.GetComponent<TomCat::EntityMetadata>().GameplayTag = "Player";
		ball.GetComponent<TomCat::EntityMetadata>().Layer = 2;
		ball.GetComponent<TomCat::EntityMetadata>().HierarchyIcon = TomCat::EntityIconMode::Sprite;
		const TomCat::UUID ballUUID = ball.GetUUID();
		ball.GetComponent<TomCat::Transform>()._Translation = { 1.25f, -0.5f, 0.0f };
		ball.GetComponent<TomCat::Transform>()._LocalTranslation =
			ball.GetComponent<TomCat::Transform>()._Translation;
		ball.AddComponent<TomCat::Rigidbody2D>().Type =
			TomCat::Rigidbody2D::BodyType::Dynamic;
		auto& circle = ball.AddComponent<TomCat::CircleCollider2D>();
		circle.CollisionLayer = 0x0020;
		circle.CollisionMask = 0x0010;
		circle.Offset = { -0.75f, 0.25f };
		circle.Radius = 0.625f;
		circle.Density = 2.0f;
		circle.Friction = 0.2f;
		circle.Restitution = 0.6f;
		const TomCat::CircleCollider2D expectedCircle = circle;

		auto& joint = ground.AddComponent<TomCat::DistanceJoint2D>();
		joint.ConnectedEntity = ballUUID;
		joint.Anchor = { 0.25f, 0.5f };
		joint.ConnectedAnchor = { -0.5f, -0.25f };
		joint.Distance = 3.0f;
		joint.Frequency = 2.5f;
		joint.Damping = 0.4f;
		joint.CollideConnected = true;
		const TomCat::DistanceJoint2D expectedJoint = joint;

		const std::filesystem::path sourceScenePath = project->GetAssetPath() / "Main.tomcat";
		TomCat::SceneSerializer writer(source);
		Require(writer.Serialize(sourceScenePath),
			"could not serialize/import schema-v11 physics scene for cooking");
		const TomCat::AssetMetadata* sceneMetadata = assets.Registry().GetMetadata(sourceScenePath);
		Require(sceneMetadata && sceneMetadata->Type == TomCat::AssetType::Scene
			&& static_cast<uint64_t>(sceneMetadata->Handle) != 0,
			"serialized physics scene was not registered as a Scene asset");
		const TomCat::AssetHandle sceneHandle = sceneMetadata->Handle;
		auto secondary = TomCat::CreateRef<TomCat::Scene>();
		secondary->SetSceneName("Secondary build scene");
		secondary->CreateEntity("Secondary marker");
		const std::filesystem::path secondaryScenePath =
			project->GetAssetPath() / "Secondary.tomcat";
		TomCat::SceneSerializer secondaryWriter(secondary);
		Require(secondaryWriter.Serialize(secondaryScenePath),
			"could not serialize secondary build-scene fixture");
		const TomCat::AssetMetadata* secondaryMetadata =
			assets.Registry().GetMetadata(secondaryScenePath);
		Require(secondaryMetadata && secondaryMetadata->Type == TomCat::AssetType::Scene,
			"secondary build-scene fixture was not imported");
		const TomCat::AssetHandle secondaryHandle = secondaryMetadata->Handle;

		TomCat::BuildSettings buildSettings;
		buildSettings.EntrySceneHandle = sceneHandle;
		buildSettings.Scenes = {
			{ secondaryHandle, true, "Secondary.tomcat" },
			{ sceneHandle, true, "Main.tomcat" }
		};
		Require(project->SetBuildSettings(buildSettings),
			"could not persist ordered multi-scene BuildSettings");

		TomCat::BuildSettings invalidBuild = buildSettings;
		invalidBuild.Scenes[0].Handle = scriptHandle;
		Require(project->SetBuildSettings(invalidBuild)
			&& !assets.CookToPackage(environment.Root / "Build" / "WrongBuildSceneType.tcpak"),
			"cook accepted a CSharpScript handle in the enabled build-scene list");
		invalidBuild.Scenes[0].Handle = TomCat::AssetHandle(0x7fff001122334455ULL);
		Require(project->SetBuildSettings(invalidBuild)
			&& !assets.CookToPackage(environment.Root / "Build" / "MissingBuildScene.tcpak"),
			"cook accepted a missing enabled build-scene handle");
		Require(project->SetBuildSettings(buildSettings),
			"could not restore valid multi-scene BuildSettings");
		TomCat::PlayerSettings invalidPlayerSettings = cookedPlayerSettings;
		invalidPlayerSettings.Icon = scriptHandle;
		Require(project->SetPlayerSettings(invalidPlayerSettings)
			&& !assets.CookToPackage(environment.Root / "Build" / "WrongPlayerIconType.tcpak"),
			"cook accepted a non-Texture2D PlayerSettings.Icon handle");
		invalidPlayerSettings.Icon = TomCat::AssetHandle(0x7fff001122334455ULL);
		Require(project->SetPlayerSettings(invalidPlayerSettings)
			&& !assets.CookToPackage(environment.Root / "Build" / "MissingPlayerIcon.tcpak"),
			"cook accepted a missing PlayerSettings.Icon handle");
		Require(project->SetPlayerSettings(cookedPlayerSettings),
			"could not restore valid PlayerSettings after icon validation probes");

		const std::filesystem::path missingPayloadPath =
			environment.Root / "Build" / "MissingManagedPayload.tcpak";
		assets.ClearManagedCookPayload();
		Require(!assets.CookToPackage(missingPayloadPath),
			"cook accepted a scene with CSharpScripts but no last-good managed payload");

		std::ostringstream manifestBuilder;
		manifestBuilder
			<< "{\"version\":1,\"scripts\":[{\"assetHandle\":"
			<< static_cast<uint64_t>(scriptHandle)
			<< ",\"typeName\":\"CookedPlayerProbe\",\"executionOrder\":0,"
				"\"disallowMultiple\":false,\"lifecycle\":0,\"fields\":[]}]}";
		const std::string managedManifest = manifestBuilder.str();
		const std::vector<uint8_t> managedAssembly =
			MakeManagedAssemblyFixture(managedManifest);
		const std::vector<uint8_t> managedPdb = { 'B', 'S', 'J', 'B', 1, 0, 0, 0 };
		const std::string buildID = "regression-build-1";
		const std::filesystem::path assemblies =
			project->GetLibraryPath() / "ScriptAssemblies";
		const std::filesystem::path buildDirectory =
			assemblies / "Build" / buildID;
		std::error_code managedDirectoryError;
		std::filesystem::create_directories(buildDirectory, managedDirectoryError);
		std::filesystem::create_directories(
			project->GetLibraryPath() / "ScriptProject", managedDirectoryError);
		Require(!managedDirectoryError,
			"could not create authoring managed-payload fixture directories");
		WriteBinaryFile(buildDirectory / "Assembly-CSharp.dll", managedAssembly);
		WriteBinaryFile(buildDirectory / "Assembly-CSharp.pdb", managedPdb);
		WriteTextFile(assemblies / "last-good.json",
			"{\n"
			"  \"version\": 1,\n"
			"  \"sourceHash\": \"0123456789abcdef\",\n"
			"  \"buildId\": \"" + buildID + "\",\n"
			"  \"assembly\": \"Build/" + buildID + "/Assembly-CSharp.dll\",\n"
			"  \"pdb\": \"Build/" + buildID + "/Assembly-CSharp.pdb\"\n"
			"}\n");
		WriteTextFile(project->GetLibraryPath() / "ScriptProject" / "ScriptAssets.json",
			"{\n  \"version\": 1,\n  \"assets\": {\n"
			"    \"CookedPlayerProbe.cs\": "
			+ std::to_string(static_cast<uint64_t>(scriptHandle))
			+ "\n  }\n}\n");

		const std::filesystem::path packagePath = environment.Root / "Build" / "Game.tcpak";
		Require(assets.CookToPackage(packagePath),
			"schema-v11 physics scene and last-good assembly did not cook into a Player package");
		assets.Shutdown();

		Require(assets.MountCookedPackage(packagePath),
			"Player path could not mount the cooked physics package");
		Require(assets.GetCookedStartSceneHandle() == sceneHandle,
			"cooked package did not preserve its start-scene handle");
		Require(assets.GetCookedEntrySceneHandle() == sceneHandle
			&& assets.GetCookedBuildSceneHandles()
				== std::vector<TomCat::AssetHandle>{ secondaryHandle, sceneHandle }
			&& assets.GetCookedBuildSceneIndex(secondaryHandle) == 0
			&& assets.GetCookedBuildSceneIndex(sceneHandle) == 1
			&& !assets.GetCookedBuildSceneIndex(TomCat::AssetHandle(999999))
			&& assets.GetCookedBuildSceneHandle(0) == secondaryHandle
			&& assets.GetCookedBuildSceneHandle(1) == sceneHandle
			&& static_cast<uint64_t>(assets.GetCookedBuildSceneHandle(2)) == 0,
			"tcpak v7 did not expose its ordered build-scene manifest and index queries");
		Require(assets.GetCookedPackageVersion()
				== TomCat::RuntimeCompatibility::TcpakVersion
			&& assets.GetCookedPlayerSettings() == cookedPlayerSettings,
			"headless pre-window tcpak parsing did not preserve the v6 BootManifest");
		std::vector<uint8_t> mountedIconBytes;
		TomCat::AssetType mountedIconType = TomCat::AssetType::None;
		Require(assets.ReadAssetBytes(iconHandle, mountedIconBytes, &mountedIconType)
			&& mountedIconType == TomCat::AssetType::Texture2D
			&& !mountedIconBytes.empty(),
			"PlayerSettings.Icon was omitted from the strict package dependency closure");
		Require(assets.GetPhysics2DSettings() == cookedSettings.Physics2D,
			"tcpak v7 did not roundtrip the project Physics2D collision matrix");
		const TomCat::ManagedPackagePayload* mountedPayload =
			assets.GetCookedManagedPayload();
		Require(mountedPayload
			&& mountedPayload->NativeApiVersion == 1
			&& mountedPayload->ManagedApiVersion == 3
			&& mountedPayload->ScriptManifestVersion == 1
			&& mountedPayload->TargetFramework == "net10.0"
			&& mountedPayload->RuntimeIdentifier == "win-x64"
			&& mountedPayload->BuildID == buildID
			&& mountedPayload->AssemblySHA256.size() == 64
			&& mountedPayload->ScriptManifestJson == managedManifest
			&& mountedPayload->Assembly == managedAssembly
			&& mountedPayload->Pdb == managedPdb,
			"mounted tcpak did not expose the complete validated managed payload");
		std::vector<uint8_t> forbiddenSourceBytes;
		Require(!assets.ReadAssetBytes(scriptHandle, forbiddenSourceBytes),
			"tcpak v7 exposed a C# source asset entry");
		std::vector<uint8_t> cookedBytes;
		TomCat::AssetType cookedType = TomCat::AssetType::None;
		Require(assets.ReadAssetBytes(sceneHandle, cookedBytes, &cookedType)
			&& cookedType == TomCat::AssetType::Scene
			&& TomCat::SceneSerializer::ValidateCurrentFormat(cookedBytes, "CookedPhysicsRegression"),
			"cooked scene payload was not a complete schema-v11 Scene");

		auto loaded = TomCat::CreateRef<TomCat::Scene>();
		TomCat::SceneSerializer reader(loaded);
		Require(reader.Deserialize(sceneHandle),
			"Cooked Player scene path could not deserialize physics components");
		Require(loaded->GetPhysics2DSettings() == cookedSettings.Physics2D,
			"Cooked Player scene did not receive the mounted package collision matrix");
		TomCat::Entity loadedGround = loaded->FindEntityByUUID(groundUUID);
		TomCat::Entity loadedBall = loaded->FindEntityByUUID(ballUUID);
		Require(loadedGround && loadedBall
			&& loadedGround.HasComponent<TomCat::BoxCollider2D>()
			&& loadedGround.HasComponent<TomCat::DistanceJoint2D>()
			&& loadedGround.HasComponent<TomCat::CSharpScripts>()
			&& loadedGround.HasComponent<TomCat::HealthComponent>()
			&& loadedBall.HasComponent<TomCat::CircleCollider2D>(),
			"Cooked Player scene omitted scripts, physics, or registered components");
		const auto& loadedHealth = loadedGround.GetComponent<TomCat::HealthComponent>();
		Require(loadedHealth.Maximum == 320 && loadedHealth.Current == 123
			&& loadedHealth.Invulnerable,
			"Cooked Player scene changed registered Health data");
		RequireSameCSharpScripts(
			loadedGround.GetComponent<TomCat::CSharpScripts>(),
			expectedScripts, true,
			"Cooked Player scene changed C# attachment data");
		Require(loadedGround.GetComponent<TomCat::EntityMetadata>().GameplayTag == "Ground"
			&& loadedGround.GetComponent<TomCat::EntityMetadata>().Layer == 1
			&& loadedGround.GetComponent<TomCat::EntityMetadata>().HierarchyIcon
				== TomCat::EntityIconMode::Collider2D
			&& loadedBall.GetComponent<TomCat::EntityMetadata>().GameplayTag == "Player"
			&& loadedBall.GetComponent<TomCat::EntityMetadata>().Layer == 2
			&& loadedBall.GetComponent<TomCat::EntityMetadata>().HierarchyIcon
				== TomCat::EntityIconMode::Sprite,
			"Cooked Player scene changed EntityMetadata");
		RequireSameBoxFields(loadedGround.GetComponent<TomCat::BoxCollider2D>(), expectedBox,
			"Cooked Player changed BoxCollider2D fields");
		RequireSameCircleFields(loadedBall.GetComponent<TomCat::CircleCollider2D>(), expectedCircle,
			"Cooked Player changed CircleCollider2D fields");
		RequireSameJointFields(loadedGround.GetComponent<TomCat::DistanceJoint2D>(), expectedJoint,
			"Cooked Player changed DistanceJoint2D fields");
		Require(loadedGround.GetComponent<TomCat::BoxCollider2D>().RuntimeFixture == nullptr
			&& loadedBall.GetComponent<TomCat::CircleCollider2D>().RuntimeFixture == nullptr
			&& loadedGround.GetComponent<TomCat::DistanceJoint2D>().RuntimeJoint == nullptr,
			"cooked deserialization restored runtime physics pointers");

		int triggerEnters = 0;
		loaded->AddTriggerEnter2DListener(
			[&](const TomCat::TriggerEnter2D&) { ++triggerEnters; });
		auto managedRuntime = std::make_shared<ManagedRuntimeProbe>();
		TomCat::Scripting::ScriptEngine::Get().SetRuntime(managedRuntime);
		Require(loaded->OnRuntimeStart(),
			"Cooked Player scene could not start its managed runtime");
		Require(loadedGround.GetComponent<TomCat::BoxCollider2D>().RuntimeFixture != nullptr
			&& loadedBall.GetComponent<TomCat::CircleCollider2D>().RuntimeFixture != nullptr
			&& loadedGround.GetComponent<TomCat::DistanceJoint2D>().RuntimeJoint != nullptr,
			"Cooked Player runtime did not create Box/Circle fixtures and DistanceJoint");
		loaded->OnRuntimeStep();
		Require(triggerEnters == 0,
			"Cooked Player ignored the tcpak v7 project collision matrix");
		loaded->OnRuntimeStop();
		Require(loadedGround.GetComponent<TomCat::BoxCollider2D>().RuntimeFixture == nullptr
			&& loadedBall.GetComponent<TomCat::CircleCollider2D>().RuntimeFixture == nullptr
			&& loadedGround.GetComponent<TomCat::DistanceJoint2D>().RuntimeJoint == nullptr,
			"Cooked Player Stop did not clear runtime physics pointers");

		TomCat::Physics2DSettings allowedSettings = cookedSettings.Physics2D;
		allowedSettings.SetLayersCollide(1, 2, true);
		loaded->SetPhysics2DSettings(allowedSettings);
		Require(loaded->OnRuntimeStart(),
			"Cooked Player scene could not restart its managed runtime");
		loaded->OnRuntimeStep();
		Require(triggerEnters == 1,
			"enabling the project matrix did not admit the cooked trigger pair whose fixture masks match");
		loaded->OnRuntimeStop();
		TomCat::Scripting::ScriptEngine::Get().SetRuntime({});

		assets.Shutdown();
		const std::vector<uint8_t> validPackage = ReadBinaryFile(packagePath);
		const std::string packageText(validPackage.begin(), validPackage.end());
		Require(packageText.find(sourceMarker) == std::string::npos,
			"tcpak v7 contains C# source bytes");
		Require(packageText.find(csprojMarker) == std::string::npos
			&& packageText.find(objectMarker) == std::string::npos
			&& packageText.find(nestedLibraryMarker) == std::string::npos
			&& packageText.find("Assembly-CSharp.csproj") == std::string::npos
			&& packageText.find("ScriptAssets.json") == std::string::npos
			&& packageText.find("last-good.json") == std::string::npos
			&& packageText.find("Library/Script") == std::string::npos
			&& packageText.find(environment.Root.generic_string()) == std::string::npos,
			"tcpak v7 leaked authoring files or absolute project paths");
		uint32_t bootManifestStringBytes = 0;
		for (std::size_t offset = 100; offset < 124; offset += sizeof(uint32_t))
			bootManifestStringBytes += ReadLittleEndian32(validPackage, offset);
		const uint32_t buildSceneOffset =
			TomCat::RuntimeCompatibility::TcpakV6BaseHeaderSize
			+ bootManifestStringBytes;
		const uint32_t expectedHeaderSize = buildSceneOffset
			+ 2 * static_cast<uint32_t>(sizeof(uint64_t));
		Require(validPackage.size() >= expectedHeaderSize,
			"cooked package is smaller than its tcpak v7 variable header");
		Require(ReadLittleEndian32(validPackage, 8)
				== TomCat::RuntimeCompatibility::TcpakVersion
			&& ReadLittleEndian32(validPackage, 12) == expectedHeaderSize
			&& ReadLittleEndian64(validPackage, 24)
				== static_cast<uint64_t>(sceneHandle)
			&& ReadLittleEndian64(validPackage, 32) == 2
			&& ReadLittleEndian32(validPackage, 72)
				== TomCat::RuntimeCompatibility::BootManifestSchemaVersion
			&& ReadLittleEndian32(validPackage, 76)
				== static_cast<uint32_t>(TomCat::PlayerWindowMode::Borderless)
			&& ReadLittleEndian32(validPackage, 80) == 1366
			&& ReadLittleEndian32(validPackage, 84) == 768
			&& ReadLittleEndian64(validPackage, 88)
				== static_cast<uint64_t>(iconHandle)
			&& ReadLittleEndian32(validPackage, 96) == 0
			&& ReadLittleEndian64(validPackage, buildSceneOffset)
				== static_cast<uint64_t>(secondaryHandle)
			&& ReadLittleEndian64(validPackage, buildSceneOffset + sizeof(uint64_t))
				== static_cast<uint64_t>(sceneHandle),
			"cooked package did not declare the tcpak v7 BootManifest and scene header");

		const std::vector<uint8_t> v6Package =
			MakeTcpakV6CompatibilityFixture(validPackage);
		const std::filesystem::path v6PackagePath =
			environment.Root / "Build" / "LegacyV6.tcpak";
		WriteBinaryFile(v6PackagePath, v6Package);
		Require(assets.MountCookedPackage(v6PackagePath)
			&& assets.GetCookedPackageVersion()
				== TomCat::RuntimeCompatibility::TcpakBootManifestVersion
			&& assets.GetCookedBuildSceneHandles()
				== std::vector<TomCat::AssetHandle>{ secondaryHandle, sceneHandle }
			&& assets.GetCookedPlayerSettings() == cookedPlayerSettings,
			"Player failed to read tcpak v6 after the v7 digest-index upgrade");
		assets.UnmountCookedPackage();

		const std::vector<uint8_t> v5Package =
			MakeTcpakV5CompatibilityFixture(validPackage);
		const std::filesystem::path v5PackagePath =
			environment.Root / "Build" / "LegacyV5.tcpak";
		WriteBinaryFile(v5PackagePath, v5Package);
		Require(assets.MountCookedPackage(v5PackagePath)
			&& assets.GetCookedPackageVersion()
				== TomCat::RuntimeCompatibility::OldestSupportedTcpakVersion
			&& assets.GetCookedBuildSceneHandles()
				== std::vector<TomCat::AssetHandle>{ secondaryHandle, sceneHandle }
			&& assets.GetCookedPlayerSettings() == TomCat::PlayerSettings{},
			"Player failed to read tcpak v5 with safe default PlayerSettings");
		assets.UnmountCookedPackage();

		const std::size_t managedIndex = FindManagedPackageIndexEntry(validPackage);
		const uint64_t managedEnvelopeOffset64 =
			ReadLittleEndian64(validPackage, managedIndex + 16);
		const uint64_t managedEnvelopeSize64 =
			ReadLittleEndian64(validPackage, managedIndex + 24);
		Require(managedEnvelopeOffset64 <= validPackage.size()
			&& managedEnvelopeSize64 <= validPackage.size() - managedEnvelopeOffset64,
			"managed envelope index range is outside its package");
		const std::size_t managedEnvelopeOffset =
			static_cast<std::size_t>(managedEnvelopeOffset64);

		std::vector<uint8_t> incompatibleApi = validPackage;
		WriteLittleEndian32(incompatibleApi, managedEnvelopeOffset + 12, 2);
		RefreshTcpakEntryDigest(incompatibleApi, managedIndex);
		const std::filesystem::path incompatibleApiPath =
			environment.Root / "Build" / "IncompatibleManagedApi.tcpak";
		WriteBinaryFile(incompatibleApiPath, incompatibleApi);
		Require(!assets.MountCookedPackage(incompatibleApiPath),
			"tcpak loader accepted an incompatible managed NativeApi version");

		std::vector<uint8_t> missingManagedEntry = validPackage;
		WriteLittleEndian64(missingManagedEntry, managedIndex,
			(std::numeric_limits<uint64_t>::max)() - 1);
		WriteLittleEndian16(missingManagedEntry, managedIndex + 8,
			static_cast<uint16_t>(TomCat::AssetType::Other));
		WriteLittleEndian16(missingManagedEntry, managedIndex + 10, 0);
		WriteLittleEndian32(missingManagedEntry, managedIndex + 12, 0);
		const std::filesystem::path missingManagedEntryPath =
			environment.Root / "Build" / "MissingManagedEntry.tcpak";
		WriteBinaryFile(missingManagedEntryPath, missingManagedEntry);
		Require(!assets.MountCookedPackage(missingManagedEntryPath),
			"tcpak loader accepted CSharpScripts without a managed envelope entry");

		const uint32_t frameworkLength =
			ReadLittleEndian32(validPackage, managedEnvelopeOffset + 24);
		const uint32_t runtimeLength =
			ReadLittleEndian32(validPackage, managedEnvelopeOffset + 28);
		const uint32_t buildLength =
			ReadLittleEndian32(validPackage, managedEnvelopeOffset + 32);
		const uint32_t hashLength =
			ReadLittleEndian32(validPackage, managedEnvelopeOffset + 36);
		const uint64_t manifestLength =
			ReadLittleEndian64(validPackage, managedEnvelopeOffset + 40);
		const uint64_t assemblyLength =
			ReadLittleEndian64(validPackage, managedEnvelopeOffset + 48);
		const uint64_t assemblyOffset64 = managedEnvelopeOffset64 + 64
			+ frameworkLength + runtimeLength + buildLength + hashLength
			+ manifestLength;
		Require(assemblyLength > 2 && assemblyOffset64 <= validPackage.size()
			&& assemblyLength <= validPackage.size() - assemblyOffset64,
			"managed assembly range is outside its envelope");
		std::vector<uint8_t> badAssemblyHash = validPackage;
		badAssemblyHash[static_cast<std::size_t>(assemblyOffset64 + 2)] ^= 0x01;
		RefreshTcpakEntryDigest(badAssemblyHash, managedIndex);
		const std::filesystem::path badAssemblyHashPath =
			environment.Root / "Build" / "BadManagedHash.tcpak";
		WriteBinaryFile(badAssemblyHashPath, badAssemblyHash);
		Require(!assets.MountCookedPackage(badAssemblyHashPath),
			"tcpak loader accepted a managed assembly whose SHA-256 no longer matched");

		std::vector<uint8_t> legacyVersion = validPackage;
		WriteLittleEndian32(legacyVersion, 8, 4);
		const std::filesystem::path legacyPath = environment.Root / "Build" / "LegacyV4.tcpak";
		WriteBinaryFile(legacyPath, legacyVersion);
		Require(!assets.MountCookedPackage(legacyPath),
			"tcpak loader accepted obsolete package version 4");

		std::vector<uint8_t> unknownBootFlags = validPackage;
		WriteLittleEndian32(unknownBootFlags, 96, 4);
		const std::filesystem::path unknownBootFlagsPath =
			environment.Root / "Build" / "UnknownBootFlags.tcpak";
		WriteBinaryFile(unknownBootFlagsPath, unknownBootFlags);
		Require(!assets.MountCookedPackage(unknownBootFlagsPath),
			"tcpak v7 loader accepted unknown BootManifest flags");

		std::vector<uint8_t> wrongBootIcon = validPackage;
		WriteLittleEndian64(wrongBootIcon, 88,
			static_cast<uint64_t>(scriptHandle));
		const std::filesystem::path wrongBootIconPath =
			environment.Root / "Build" / "WrongBootIcon.tcpak";
		WriteBinaryFile(wrongBootIconPath, wrongBootIcon);
		Require(!assets.MountCookedPackage(wrongBootIconPath),
			"tcpak v7 loader accepted a BootManifest icon absent from its Texture2D index");

		std::vector<uint8_t> oversizedBootString = validPackage;
		WriteLittleEndian32(oversizedBootString, 100,
			TomCat::RuntimeCompatibility::MaximumBootManifestStringBytes + 1);
		const std::filesystem::path oversizedBootStringPath =
			environment.Root / "Build" / "OversizedBootString.tcpak";
		WriteBinaryFile(oversizedBootStringPath, oversizedBootString);
		Require(!assets.MountCookedPackage(oversizedBootStringPath),
			"tcpak v7 loader accepted an oversized BootManifest string");

		std::vector<uint8_t> mismatchedSceneCount = validPackage;
		WriteLittleEndian64(mismatchedSceneCount, 32, 1);
		const std::filesystem::path mismatchedSceneCountPath =
			environment.Root / "Build" / "MismatchedBuildSceneCount.tcpak";
		WriteBinaryFile(mismatchedSceneCountPath, mismatchedSceneCount);
		Require(!assets.MountCookedPackage(mismatchedSceneCountPath),
			"tcpak loader accepted a build-scene count inconsistent with headerSize");

		std::vector<uint8_t> duplicateBuildScene = validPackage;
		WriteLittleEndian64(duplicateBuildScene,
			buildSceneOffset,
			static_cast<uint64_t>(sceneHandle));
		const std::filesystem::path duplicateBuildScenePath =
			environment.Root / "Build" / "DuplicateBuildScene.tcpak";
		WriteBinaryFile(duplicateBuildScenePath, duplicateBuildScene);
		Require(!assets.MountCookedPackage(duplicateBuildScenePath),
			"tcpak loader accepted duplicate ordered build-scene handles");

		std::vector<uint8_t> missingBuildScene = validPackage;
		WriteLittleEndian64(missingBuildScene,
			buildSceneOffset,
			0x7fff001122334455ULL);
		const std::filesystem::path missingBuildScenePath =
			environment.Root / "Build" / "MissingBuildSceneManifestAsset.tcpak";
		WriteBinaryFile(missingBuildScenePath, missingBuildScene);
		Require(!assets.MountCookedPackage(missingBuildScenePath),
			"tcpak loader accepted a build-scene handle absent from the asset index");

		std::vector<uint8_t> asymmetric = validPackage;
		constexpr std::size_t matrixOffset = 40;
		const std::size_t rowOneOffset = matrixOffset + sizeof(uint16_t);
		const std::size_t rowTwoOffset = matrixOffset + 2 * sizeof(uint16_t);
		uint16_t rowOne = ReadLittleEndian16(asymmetric, rowOneOffset);
		const uint16_t rowTwo = ReadLittleEndian16(asymmetric, rowTwoOffset);
		Require((rowOne & (uint16_t(1) << 2)) == 0
			&& (rowTwo & (uint16_t(1) << 1)) == 0,
			"cooked matrix fixture did not preserve its disabled symmetric pair");
		rowOne |= uint16_t(1) << 2;
		WriteLittleEndian16(asymmetric, rowOneOffset, rowOne);
		const std::filesystem::path asymmetricPath =
			environment.Root / "Build" / "Asymmetric.tcpak";
		WriteBinaryFile(asymmetricPath, asymmetric);
		Require(!assets.MountCookedPackage(asymmetricPath),
			"tcpak loader accepted an asymmetric Physics2D collision matrix");

		std::vector<uint8_t> truncated = validPackage;
		truncated.resize(TomCat::RuntimeCompatibility::TcpakBaseHeaderSize - 1);
		const std::filesystem::path truncatedPath =
			environment.Root / "Build" / "Truncated.tcpak";
		WriteBinaryFile(truncatedPath, truncated);
		Require(!assets.MountCookedPackage(truncatedPath),
			"tcpak loader accepted a truncated v5 base header");
	}

	void TestTypedAssetReferenceGraphAndCookValidation()
	{
		TemporaryCookedProject environment;
		TomCat::ProjectConfig config;
		config.Name = "Typed Asset Reference Regression";
		config.Template = "2D";
		config.AssetDirectory = "Assets";
		config.StartScene = "Main.tomcat";
		auto project = TomCat::Project::CreateNew(
			environment.Root / "Project.tcproj", config);
		Require(project != nullptr,
			"could not create a temporary typed-reference project");

		const std::filesystem::path scriptPath =
			project->GetAssetPath() / "ReferenceProbe.cs";
		const std::filesystem::path referencedTexturePath =
			project->GetAssetPath() / "Referenced.tga";
		const std::filesystem::path entityValueTexturePath =
			project->GetAssetPath() / "EntityValue.tga";
		const std::filesystem::path unusedTexturePath =
			project->GetAssetPath() / "Unused.tga";
		WriteTextFile(scriptPath,
			"using TomCat; public sealed class ReferenceProbe : TomCatBehaviour {}\n");
		WriteBinaryFile(referencedTexturePath, MakeTestTGA());
		WriteBinaryFile(entityValueTexturePath, MakeTestTGA());
		WriteBinaryFile(unusedTexturePath, MakeTestTGA());

		TomCat::AssetManager& assets = TomCat::AssetManager::Get();
		Require(assets.SetProject(project),
			"could not initialize assets for typed-reference regression");
		const TomCat::AssetMetadata* scriptMetadata =
			assets.Registry().GetMetadata(scriptPath);
		const TomCat::AssetMetadata* referencedTextureMetadata =
			assets.Registry().GetMetadata(referencedTexturePath);
		const TomCat::AssetMetadata* entityValueTextureMetadata =
			assets.Registry().GetMetadata(entityValueTexturePath);
		const TomCat::AssetMetadata* unusedTextureMetadata =
			assets.Registry().GetMetadata(unusedTexturePath);
		Require(scriptMetadata && !scriptMetadata->IsMissing
			&& scriptMetadata->Type == TomCat::AssetType::CSharpScript,
			"typed-reference script fixture was not imported as CSharpScript");
		Require(referencedTextureMetadata && !referencedTextureMetadata->IsMissing
			&& referencedTextureMetadata->Type == TomCat::AssetType::Texture2D
			&& entityValueTextureMetadata && !entityValueTextureMetadata->IsMissing
			&& entityValueTextureMetadata->Type == TomCat::AssetType::Texture2D
			&& unusedTextureMetadata && !unusedTextureMetadata->IsMissing
			&& unusedTextureMetadata->Type == TomCat::AssetType::Texture2D,
			"typed-reference texture fixtures were not imported as Texture2D");
		const TomCat::AssetHandle scriptHandle = scriptMetadata->Handle;
		const TomCat::AssetHandle referencedTextureHandle =
			referencedTextureMetadata->Handle;
		const TomCat::AssetHandle entityValueTextureHandle =
			entityValueTextureMetadata->Handle;
		const TomCat::AssetHandle unusedTextureHandle = unusedTextureMetadata->Handle;

		auto dependencyScene = TomCat::CreateRef<TomCat::Scene>();
		dependencyScene->SetSceneName("Prefab typed Scene dependency");
		dependencyScene->CreateEntity("Dependency scene entity");
		const std::filesystem::path dependencyScenePath =
			project->GetAssetPath() / "PrefabDependency.tomcat";
		Require(TomCat::SceneSerializer(dependencyScene).Serialize(dependencyScenePath),
			"could not serialize Prefab Scene dependency");
		const TomCat::AssetMetadata* dependencySceneMetadata =
			assets.Registry().GetMetadata(dependencyScenePath);
		Require(dependencySceneMetadata
			&& dependencySceneMetadata->Type == TomCat::AssetType::Scene,
			"Prefab Scene dependency was not imported");
		const TomCat::AssetHandle dependencySceneHandle = dependencySceneMetadata->Handle;

		auto prefabSource = TomCat::CreateRef<TomCat::Scene>();
		TomCat::Entity prefabRoot = prefabSource->CreateEntity("Typed dependency Prefab");
		auto& prefabSprite = prefabRoot.AddComponent<TomCat::SpriteRenderer>();
		prefabSprite.SpriteHandle = referencedTextureHandle;
		TomCat::CSharpScriptEntry prefabScript;
		prefabScript.ScriptAsset = scriptHandle;
		prefabScript.LastKnownClassName = "ReferenceProbe";
		prefabScript.Fields.emplace_back("dddddddddddddddddddddddddddddddd",
			"NextScene", TomCat::ScriptFieldType::AssetRef,
			static_cast<uint64_t>(dependencySceneHandle), "TomCat.SceneAsset");
		prefabRoot.AddComponent<TomCat::CSharpScripts>().Scripts.push_back(prefabScript);
		const std::filesystem::path prefabPath =
			project->GetAssetPath() / "Dependency.tcprefab";
		TomCat::AssetHandle prefabHandle{ 0 };
		Require(TomCat::PrefabArchiveCodec::SaveSubtree(prefabSource, prefabRoot,
			prefabPath, &prefabHandle) && static_cast<uint64_t>(prefabHandle) != 0,
			"could not serialize/import typed dependency Prefab");

		auto scene = TomCat::CreateRef<TomCat::Scene>();
		scene->SetSceneName("Typed references");
		TomCat::Entity entity = scene->CreateEntity("Reference owner");
		auto& scripts = entity.AddComponent<TomCat::CSharpScripts>();
		TomCat::CSharpScriptEntry attachment;
		attachment.ScriptAsset = scriptHandle;
		attachment.LastKnownClassName = "ReferenceProbe";
		attachment.Fields.emplace_back("aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa",
			"_notNamedHandle", TomCat::ScriptFieldType::AssetRef,
			static_cast<uint64_t>(referencedTextureHandle),
			"TomCat.AssetRef<TomCat.Texture2DAsset>");
		attachment.Fields.emplace_back("bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb",
			"_looksLikeAssetHandle", TomCat::ScriptFieldType::Entity,
			static_cast<uint64_t>(entityValueTextureHandle));
		attachment.Fields.emplace_back("eeeeeeeeeeeeeeeeeeeeeeeeeeeeeeee",
			"Spawnable", TomCat::ScriptFieldType::AssetRef,
			static_cast<uint64_t>(prefabHandle), "TomCat.PrefabAsset");
		attachment.Fields.emplace_back("ffffffffffffffffffffffffffffffff",
			"GenericScene", TomCat::ScriptFieldType::AssetRef,
			static_cast<uint64_t>(dependencySceneHandle),
			"TomCat.AssetRef<TomCat.SceneAsset>");
		attachment.Fields.emplace_back("99999999999999999999999999999999",
			"GenericPrefab", TomCat::ScriptFieldType::AssetRef,
			static_cast<uint64_t>(prefabHandle),
			"TomCat.AssetRef<TomCat.PrefabAsset>");
		scripts.Scripts.emplace_back(std::move(attachment));

		const std::filesystem::path scenePath =
			project->GetAssetPath() / "Main.tomcat";
		auto saveScene = [&]()
		{
			TomCat::SceneSerializer writer(scene);
			Require(writer.Serialize(scenePath),
				"could not serialize typed-reference scene fixture");
		};
		saveScene();
		const TomCat::AssetMetadata* sceneMetadata =
			assets.Registry().GetMetadata(scenePath);
		Require(sceneMetadata && sceneMetadata->Type == TomCat::AssetType::Scene,
			"typed-reference scene fixture was not imported");
		const TomCat::AssetHandle sceneHandle = sceneMetadata->Handle;
		Require(project->SetStartScene("Main.tomcat"),
			"could not set typed-reference start-scene path");
		project->SetStartSceneHandle(sceneHandle);
		Require(project->Save(),
			"could not save typed-reference start-scene handle");

		const std::vector<TomCat::AssetReference> startSceneReferences =
			assets.FindReferences(sceneHandle);
		Require(std::any_of(startSceneReferences.begin(), startSceneReferences.end(),
			[&](const TomCat::AssetReference& reference)
			{
				return reference.ReferencedAsset == sceneHandle
					&& reference.FilePath == project->GetBuildSettingsPath()
					&& reference.PropertyPath == "BuildSettings.EntrySceneHandle";
			}), "BuildSettings entry scene is absent from the unified asset reference graph");
		std::vector<TomCat::AssetReference> deleteReferences;
		Require(!assets.DeleteAsset(sceneHandle, false, &deleteReferences)
			&& std::filesystem::is_regular_file(scenePath)
			&& std::any_of(deleteReferences.begin(), deleteReferences.end(),
				[](const TomCat::AssetReference& reference)
				{
					return reference.PropertyPath == "BuildSettings.EntrySceneHandle";
				}), "unforced deletion removed the configured entry scene");

		const std::vector<TomCat::AssetReference> typedReferences =
			assets.FindReferences(referencedTextureHandle);
		Require(std::any_of(typedReferences.begin(), typedReferences.end(),
			[&](const TomCat::AssetReference& reference)
			{
				return reference.ReferencedAsset == referencedTextureHandle
					&& reference.ReferencingAsset == sceneHandle
					&& reference.PropertyPath.find(".Fields[0].Value")
						!= std::string::npos;
			}), "AssetRef field was not discovered from its serialized field Type");
		Require(assets.FindReferences(entityValueTextureHandle).empty(),
			"non-AssetRef script field was mistaken for an asset reference by its name/value");
		const std::vector<TomCat::AssetReference> prefabReferences =
			assets.FindReferences(prefabHandle);
		Require(std::any_of(prefabReferences.begin(), prefabReferences.end(),
			[&](const TomCat::AssetReference& reference)
			{
				return reference.ReferencedAsset == prefabHandle
					&& reference.ReferencingAsset == sceneHandle
					&& reference.PropertyPath.find(".Fields[2].Value")
						!= std::string::npos;
			}), "typed PrefabAsset field was absent from the deletion reference graph");
		const std::vector<TomCat::AssetReference> dependencySceneReferences =
			assets.FindReferences(dependencySceneHandle);
		Require(std::any_of(dependencySceneReferences.begin(),
			dependencySceneReferences.end(), [&](const TomCat::AssetReference& reference)
			{
				return reference.ReferencedAsset == dependencySceneHandle
					&& reference.ReferencingAsset == prefabHandle;
			}), "Prefab typed SceneAsset dependency was absent from the reference graph");
		deleteReferences.clear();
		Require(!assets.DeleteAsset(referencedTextureHandle, false, &deleteReferences)
			&& std::filesystem::is_regular_file(referencedTexturePath)
			&& !deleteReferences.empty(),
			"unforced deletion ignored a serialized AssetRef field");

		std::ostringstream manifestBuilder;
		manifestBuilder
			<< "{\"version\":1,\"scripts\":[{\"assetHandle\":"
			<< static_cast<uint64_t>(scriptHandle)
			<< ",\"typeName\":\"ReferenceProbe\",\"executionOrder\":0,"
				"\"disallowMultiple\":false,\"lifecycle\":0,\"fields\":[{"
				"\"id\":\"cccccccccccccccccccccccccccccccc\","
				"\"name\":\"Speed\",\"type\":\"Float\","
				"\"isPublic\":true,\"hidden\":false,\"formerNames\":[],"
				"\"typeName\":null,\"header\":null,\"tooltip\":null,"
				"\"rangeMin\":null,\"rangeMax\":null,\"defaultValue\":1.5}]}]}";
		const std::string managedManifest = manifestBuilder.str();
		std::string invalidDefaultManifest = managedManifest;
		const std::size_t defaultPosition = invalidDefaultManifest.find("\"defaultValue\":1.5");
		Require(defaultPosition != std::string::npos,
			"could not locate manifest defaultValue fixture");
		invalidDefaultManifest.replace(defaultPosition,
			std::string("\"defaultValue\":1.5").size(),
			"\"defaultValue\":\"not-a-float\"");
		Require(!assets.SetManagedCookPayload(
			MakeManagedAssemblyFixture(invalidDefaultManifest), invalidDefaultManifest,
			"typed-reference-invalid-default"),
			"managed payload validator accepted a defaultValue incompatible with its field type");
		std::ostringstream embeddedManifestBuilder;
		embeddedManifestBuilder
			<< "{\"version\":1,\"scripts\":[{\"assetHandle\":"
			<< static_cast<uint64_t>(scriptHandle)
			<< ",\"typeName\":\"ReferenceProbe\",\"executionOrder\":0,"
				"\"disallowMultiple\":false,\"lifecycle\":0,\"fields\":[{"
				"\"id\":\"cccccccccccccccccccccccccccccccc\","
				"\"name\":\"Speed\",\"type\":\"Float\","
				"\"isPublic\":true,\"hidden\":false,\"formerNames\":[]}]}]}";
		const std::string embeddedManifest = embeddedManifestBuilder.str();
		auto withEventMethods = [](std::string manifest, const std::string& methods,
			uint32_t lifecycle = 1024)
		{
			const std::string token = "\"lifecycle\":0";
			const size_t position = manifest.find(token);
			Require(position != std::string::npos, "could not locate manifest lifecycle fixture");
			manifest.replace(position, token.size(), "\"lifecycle\":"
				+ std::to_string(lifecycle) + ",\"methods\":" + methods);
			return manifest;
		};
		const std::string eventEmbeddedManifest = withEventMethods(embeddedManifest, "[\"OnPressed\",\"OnReleased\"]");
		const std::string eventEditorManifest = withEventMethods(managedManifest, "[\"OnReleased\",\"OnPressed\"]");
		Require(assets.SetManagedCookPayload(MakeManagedAssemblyFixture(eventEmbeddedManifest),
			eventEditorManifest, "event-methods-late-update"),
			"managed payload rejected public event methods or OnLateUpdate lifecycle metadata");
		for (const std::string& malformedMethods : std::vector<std::string>{
			"null", "{}", "[\"\"]", "[\" \"]", "[17]", "[\"OnPressed\",\"OnPressed\"]",
			"[\"" + std::string(513, 'x') + "\"]" })
		{
			const std::string invalid = withEventMethods(embeddedManifest, malformedMethods);
			Require(!assets.SetManagedCookPayload(MakeManagedAssemblyFixture(invalid), invalid,
				"invalid-event-methods"), "managed payload accepted malformed event-method metadata");
		}
		const std::string invalidLifecycle = withEventMethods(embeddedManifest, "[]", 2048);
		Require(!assets.SetManagedCookPayload(MakeManagedAssemblyFixture(invalidLifecycle),
			invalidLifecycle, "invalid-lifecycle-bit"), "managed payload accepted an undefined lifecycle bit");
		Require(!assets.SetManagedCookPayload(MakeManagedAssemblyFixture(eventEmbeddedManifest),
			withEventMethods(managedManifest, "[\"MissingMethod\"]"), "stale-event-methods"),
			"managed payload accepted event methods different from its assembly manifest");
		std::string staleEditorManifest = managedManifest;
		const std::size_t fieldNamePosition = staleEditorManifest.find("\"name\":\"Speed\"");
		Require(fieldNamePosition != std::string::npos,
			"could not locate manifest field-identity fixture");
		staleEditorManifest.replace(fieldNamePosition,
			std::string("\"name\":\"Speed\"").size(), "\"name\":\"StaleSpeed\"");
		Require(!assets.SetManagedCookPayload(
			MakeManagedAssemblyFixture(embeddedManifest), staleEditorManifest,
			"typed-reference-stale-metadata"),
			"managed payload accepted stale Editor field identity despite matching script handles");
		Require(assets.SetManagedCookPayload(
			MakeManagedAssemblyFixture(embeddedManifest), managedManifest,
			"typed-reference-build"),
			"could not reconcile enriched Editor metadata with the assembly manifest");

		TomCat::CSharpScriptEntry& savedAttachment = scripts.Scripts.front();
		TomCat::ScriptField& assetField = savedAttachment.Fields.front();
		TomCat::ScriptField& prefabField = savedAttachment.Fields[2];
		TomCat::ScriptField& genericSceneField = savedAttachment.Fields[3];
		TomCat::ScriptField& genericPrefabField = savedAttachment.Fields[4];
		const uint64_t missingHandle = 0x7fff000012345678ULL;

		savedAttachment.ScriptAsset = TomCat::AssetHandle(missingHandle);
		saveScene();
		Require(!assets.CookToPackage(environment.Root / "Build" / "MissingScript.tcpak"),
			"cook accepted a missing nonzero ScriptHandle");

		savedAttachment.ScriptAsset = referencedTextureHandle;
		saveScene();
		Require(!assets.CookToPackage(environment.Root / "Build" / "WrongScriptType.tcpak"),
			"cook accepted a Texture2D as a CSharpScript handle");

		savedAttachment.ScriptAsset = scriptHandle;
		assetField.Value = missingHandle;
		saveScene();
		Require(!assets.CookToPackage(environment.Root / "Build" / "MissingAssetRef.tcpak"),
			"cook accepted an AssetRef whose handle does not exist");

		assetField.Value = static_cast<uint64_t>(scriptHandle);
		saveScene();
		Require(!assets.CookToPackage(environment.Root / "Build" / "WrongAssetRefType.tcpak"),
			"cook accepted an authoring-only CSharpScript as a runtime AssetRef");

		assetField.Value = static_cast<uint64_t>(referencedTextureHandle);
		prefabField.Value = static_cast<uint64_t>(referencedTextureHandle);
		saveScene();
		Require(!assets.CookToPackage(environment.Root / "Build" / "WrongPrefabType.tcpak"),
			"cook accepted a Texture2D for a typed PrefabAsset field");

		prefabField.Value = static_cast<uint64_t>(prefabHandle);
		saveScene();
		genericSceneField.Value = static_cast<uint64_t>(referencedTextureHandle);
		saveScene();
		Require(!assets.CookToPackage(
			environment.Root / "Build" / "WrongGenericSceneType.tcpak"),
			"cook accepted a Texture2D for AssetRef<SceneAsset>");

		genericSceneField.Value = static_cast<uint64_t>(dependencySceneHandle);
		genericPrefabField.Value = static_cast<uint64_t>(referencedTextureHandle);
		saveScene();
		Require(!assets.CookToPackage(
			environment.Root / "Build" / "WrongGenericPrefabType.tcpak"),
			"cook accepted a Texture2D for AssetRef<PrefabAsset>");

		genericPrefabField.Value = static_cast<uint64_t>(prefabHandle);
		genericSceneField.TypeName = "TomCat.AssetRef<Game.UnknownAsset>";
		saveScene();
		Require(!assets.CookToPackage(
			environment.Root / "Build" / "UnknownGenericAssetType.tcpak"),
			"cook accepted an AssetRef<T> with an unsupported asset type");

		genericSceneField.TypeName = "TomCat.AssetRef<TomCat.SceneAsset>";
		saveScene();
		const std::filesystem::path validPackage =
			environment.Root / "Build" / "TypedReferences.tcpak";
		Require(assets.CookToPackage(validPackage),
			"cook rejected valid typed Script and AssetRef handles");
		assets.Shutdown();
		Require(assets.MountCookedPackage(validPackage),
			"package mount rejected a valid typed asset-reference closure");
		Require(assets.GetCookedManagedPayload()
			&& assets.GetCookedManagedPayload()->ScriptManifestJson == embeddedManifest,
			"tcpak did not canonicalize runtime metadata to the assembly-embedded manifest");
		std::vector<uint8_t> dependencyBytes;
		TomCat::AssetType dependencyType = TomCat::AssetType::None;
		Require(assets.ReadAssetBytes(prefabHandle, dependencyBytes, &dependencyType)
			&& dependencyType == TomCat::AssetType::Prefab
			&& assets.ReadAssetBytes(referencedTextureHandle, dependencyBytes,
				&dependencyType)
			&& dependencyType == TomCat::AssetType::Texture2D
			&& assets.ReadAssetBytes(dependencySceneHandle, dependencyBytes,
				&dependencyType)
			&& dependencyType == TomCat::AssetType::Scene,
			"Scene -> Prefab -> Texture/Script/Scene dependency closure was incomplete");
		Require(!assets.ReadAssetBytes(unusedTextureHandle, dependencyBytes),
			"unreferenced runtime asset leaked into the cooked dependency closure");
		assets.Shutdown();
	}

	void TestScriptFreeCookWithoutManagedPayload()
	{
		TemporaryCookedProject environment;
		TomCat::ProjectConfig config;
		config.Name = "Script Free Cook Regression";
		config.Template = "2D";
		config.AssetDirectory = "Assets";
		config.StartScene = "Main.tomcat";
		auto project = TomCat::Project::CreateNew(
			environment.Root / "Project.tcproj", config);
		Require(project != nullptr,
			"could not create a temporary script-free project");

		TomCat::AssetManager& assets = TomCat::AssetManager::Get();
		Require(assets.SetProject(project),
			"could not initialize AssetManager for script-free cook");
		auto scene = TomCat::CreateRef<TomCat::Scene>();
		scene->CreateEntity("No managed runtime required");
		const std::filesystem::path scenePath =
			project->GetAssetPath() / "Main.tomcat";
		TomCat::SceneSerializer writer(scene);
		Require(writer.Serialize(scenePath),
			"could not serialize the script-free start scene");
		const TomCat::AssetMetadata* sceneMetadata =
			assets.Registry().GetMetadata(scenePath);
		Require(sceneMetadata && sceneMetadata->Type == TomCat::AssetType::Scene,
			"script-free start scene was not imported");
		project->SetStartSceneHandle(sceneMetadata->Handle);
		Require(project->Save(), "could not save the script-free start-scene handle");

		assets.ClearManagedCookPayload();
		const std::filesystem::path packagePath =
			environment.Root / "Build" / "ScriptFree.tcpak";
		Require(assets.CookToPackage(packagePath),
			"script-free scene incorrectly required a managed payload");
		assets.Shutdown();
		Require(assets.MountCookedPackage(packagePath)
			&& assets.GetCookedManagedPayload() == nullptr,
			"script-free tcpak unexpectedly exposed a managed payload");
	}

	TomCat::Entity MakeMultiFixtureBody(TomCat::Scene& scene, const char* name,
		TomCat::Rigidbody2D::BodyType type)
	{
		TomCat::Entity entity = scene.CreateEntity(name);
		auto& body = entity.AddComponent<TomCat::Rigidbody2D>();
		body.Type = type;
		auto& box = entity.AddComponent<TomCat::BoxCollider2D>();
		box.Size = { 5.0f, 5.0f };
		entity.AddComponent<TomCat::CircleCollider2D>().Radius = 4.0f;
		return entity;
	}

	void TestCollisionPairDeduplication()
	{
		auto runtime = std::make_shared<ManagedRuntimeProbe>();
		ScriptRuntimeOverride runtimeOverride(runtime);
		TomCat::Scene scene;
		TomCat::Entity entityA = MakeMultiFixtureBody(scene, "Static",
			TomCat::Rigidbody2D::BodyType::Static);
		TomCat::Entity entityB = MakeMultiFixtureBody(scene, "Dynamic",
			TomCat::Rigidbody2D::BodyType::Dynamic);
		AttachManagedProbe(entityA, 8105);
		AttachManagedProbe(entityB, 8106);
		int enters = 0;
		int exits = 0;
		uint64_t lastA = 0;
		uint64_t lastB = 0;
		scene.AddCollisionEnter2DListener([&](const TomCat::CollisionEnter2D& event)
		{
			++enters;
			lastA = static_cast<uint64_t>(event.EntityA);
			lastB = static_cast<uint64_t>(event.EntityB);
		});
		scene.AddCollisionExit2DListener([&](const TomCat::CollisionExit2D&) { ++exits; });

		Require(scene.OnRuntimeStart(), "managed collision de-duplication probe did not start");
		scene.OnRuntimeStep();
		Require(enters == 1, "multiple fixtures emitted more than one entity-pair Enter");
		Require(runtime->PhysicsEvents.size() == 1
			&& runtime->PhysicsEvents.front().Kind == static_cast<uint32_t>(
				TomCat::Scripting::NativePhysicsEventKind::CollisionEnter),
			"managed runtime did not receive one de-duplicated CollisionEnter2D event");
		Require(lastA < lastB, "collision UUID pair was not canonicalized");
		Require(runtime->PhysicsEvents.front().EntityA.EntityId == lastA
			&& runtime->PhysicsEvents.front().EntityB.EntityId == lastB,
			"managed and native listeners received different canonical collision pairs");
		scene.OnRuntimeStep();
		Require(enters == 1 && exits == 0 && runtime->PhysicsEvents.size() == 1,
			"persistent contact emitted duplicate events");

		MoveRuntimeBody(entityB, { 100.0f, 0.0f });
		scene.OnRuntimeStep();
		Require(enters == 1 && exits == 1, "entity-pair Exit was not emitted exactly once");
		Require(runtime->PhysicsEvents.size() == 2
			&& runtime->PhysicsEvents.back().Kind == static_cast<uint32_t>(
				TomCat::Scripting::NativePhysicsEventKind::CollisionExit),
			"managed runtime did not receive one de-duplicated CollisionExit2D event");
		scene.OnRuntimeStop();
	}

	void TestDeletionDuringCollisionDispatch()
	{
		TomCat::Scene scene;
		TomCat::Entity entityA = AddCircleBody(scene, "Delete A",
			TomCat::Rigidbody2D::BodyType::Static, { 0.0f, 0.0f }, 2.0f);
		TomCat::Entity entityB = AddCircleBody(scene, "Delete B",
			TomCat::Rigidbody2D::BodyType::Dynamic, { 0.0f, 0.0f }, 2.0f);

		int destructiveListenerCalls = 0;
		int laterListenerCalls = 0;
		int exitCalls = 0;
		TomCat::UUID deletedUUID(0);
		scene.AddCollisionEnter2DListener([&](const TomCat::CollisionEnter2D& event)
		{
			++destructiveListenerCalls;
			deletedUUID = event.EntityB;
			TomCat::Entity victim = scene.FindEntityByUUID(event.EntityB);
			if (victim)
				scene.DestroyEntity(victim);
		});
		scene.AddCollisionEnter2DListener([&](const TomCat::CollisionEnter2D&)
		{
			++laterListenerCalls;
		});
		scene.AddCollisionExit2DListener([&](const TomCat::CollisionExit2D&) { ++exitCalls; });

		scene.OnRuntimeStart();
		scene.OnRuntimeStep();
		Require(destructiveListenerCalls == 1, "destructive collision listener did not run once");
		Require(laterListenerCalls == 0, "listener received an event after an entity was deleted");
		Require(static_cast<uint64_t>(deletedUUID) != 0 && !scene.FindEntityByUUID(deletedUUID),
			"collision callback did not delete its target");
		scene.OnRuntimeStep();
		Require(exitCalls == 0, "entity deletion leaked a stale Exit event");
		scene.OnRuntimeStop();
	}

	void TestManagedMutationDuringCollisionDispatch()
	{
		auto runMutation = [](ManagedRuntimeProbe::PhysicsMutation action)
		{
			auto runtime = std::make_shared<ManagedRuntimeProbe>();
			runtime->Mutation = action;
			ScriptRuntimeOverride runtimeOverride(runtime);
			TomCat::Scene scene;
			TomCat::Entity scripted = AddCircleBody(scene, "Mutating script",
				TomCat::Rigidbody2D::BodyType::Static, { 0.0f, 0.0f }, 2.0f);
			const TomCat::UUID scriptedUUID = scripted.GetUUID();
			AttachManagedProbe(scripted, 8107);
			AddCircleBody(scene, "Mutation contact",
				TomCat::Rigidbody2D::BodyType::Dynamic, { 0.0f, 0.0f }, 2.0f);

			int laterListenerCalls = 0;
			scene.AddCollisionEnter2DListener(
				[&](const TomCat::CollisionEnter2D&) { ++laterListenerCalls; });
			Require(scene.OnRuntimeStart(),
				"managed collision-mutation probe did not start");
			scene.OnRuntimeStep();

			Require(runtime->PhysicsEventCount == 1
				&& runtime->MutationAttempted && runtime->MutationQueued,
				"managed physics callback did not queue its deferred mutation");

			TomCat::Entity survivingScripted = scene.FindEntityByUUID(scriptedUUID);
			if (action == ManagedRuntimeProbe::PhysicsMutation::DestroyEntity)
			{
				Require(!survivingScripted,
					"DestroyEntity requested by a managed collision callback was not flushed");
				Require(laterListenerCalls == 0,
					"listener dispatch continued after managed code deleted a participant");
				Require(runtime->DestroyedAttachmentCount == 1,
					"managed attachment was not destroyed with its Entity");
			}
			else
			{
				Require(survivingScripted,
					"managed component mutation unexpectedly deleted its Entity");
				Require(laterListenerCalls == 1,
					"component-only managed mutation incorrectly cancelled the Scene event");
				Require(!survivingScripted.HasComponent<TomCat::Rigidbody2D>(),
					"managed Rigidbody2D removal was not committed at callback return");
			}

			scene.OnRuntimeStop();
		};

		runMutation(ManagedRuntimeProbe::PhysicsMutation::RemoveRigidbody2D);
		runMutation(ManagedRuntimeProbe::PhysicsMutation::DestroyEntity);
	}

	void TestPrefabDynamicAttachmentBridge()
	{
		auto runtime = std::make_shared<ManagedRuntimeProbe>();
		ScriptRuntimeOverride runtimeOverride(runtime);

		TomCat::Scene scene;
		TomCat::Entity existing = scene.CreateEntity("Existing script-free entity");

		auto source = TomCat::CreateRef<TomCat::Scene>();
		TomCat::Entity prefabRoot = source->CreateEntity("Dynamic managed Prefab");
		TomCat::Entity prefabChild = source->CreateEntity("Dynamic child");
		Require(source->SetParent(prefabChild, prefabRoot),
			"could not create dynamic Prefab hierarchy");
		TomCat::CSharpScriptEntry dynamicScript;
		dynamicScript.ScriptAsset = TomCat::AssetHandle(7002);
		dynamicScript.LastKnownClassName = "Game.DynamicPrefab";
		dynamicScript.Fields.emplace_back("12121212121212121212121212121212",
			"Seed", TomCat::ScriptFieldType::Int32, int32_t{ 42 });
		dynamicScript.Fields.emplace_back("34343434343434343434343434343434",
			"Target", TomCat::ScriptFieldType::Entity,
			static_cast<uint64_t>(prefabChild.GetUUID()));
		prefabRoot.AddComponent<TomCat::CSharpScripts>().Scripts.push_back(dynamicScript);
		TomCat::PrefabArchive archive;
		std::string error;
		Require(TomCat::PrefabArchiveCodec::CaptureSubtree(source, prefabRoot,
			archive, error), error.c_str());

		Require(scene.OnRuntimeStart(),
			"could not start managed dynamic-Prefab regression Scene");
		Require(!runtime->Active && runtime->Calls.empty(),
			"an initially script-free Scene eagerly created a managed Scene runtime");
		TomCat::PrefabInstantiateOptions options;
		options.ResolveAssets = false;
		TomCat::PrefabInstantiationResult successful;
		Require(TomCat::PrefabArchiveCodec::Instantiate(archive, scene, options,
			successful, error), error.c_str());
		Require(runtime->DynamicInstantiateCount == 0
			&& scene.GetPendingRuntimeEntityCreateCount() == 2,
			"dynamic attachments were instantiated before the Scene safe point");
		scene.OnRuntimeStep();
		Require(runtime->Active && runtime->Calls.size() >= 5
			&& runtime->Calls[0] == "CreateSceneRuntime"
			&& runtime->Calls[1] == "InstantiateAll"
			&& runtime->Calls[2] == "ApplySerializedFields"
			&& runtime->Calls[3] == "InvokeCreateAll"
			&& runtime->Calls[4] == "InstantiateAttachments"
			&& runtime->Attachments.empty()
			&& runtime->LastFields == "{\"attachments\":[]}"
			&& runtime->DynamicInstantiateCount == 1
			&& runtime->DynamicAttachments.size() == 1
			&& runtime->DynamicAttachments.front().Entity.EntityId
				== static_cast<uint64_t>(successful.Root.GetUUID())
			&& runtime->DynamicAttachments.front().Entity.SceneSessionId
				== runtime->LastSceneSession,
			"lazy Scene runtime bootstrap did not keep the Prefab batch on the incremental lifecycle ABI");
		const uint64_t successfulAttachment = static_cast<uint64_t>(
			successful.Root.GetComponent<TomCat::CSharpScripts>().Scripts.front()
				.AttachmentID);
		Require(runtime->DynamicAttachments.front().AttachmentId
				== successfulAttachment
			&& runtime->DynamicFields.find("12121212121212121212121212121212")
				!= std::string::npos
			&& runtime->DynamicFields.find("\"value\":42") != std::string::npos,
			"dynamic attachment fields were missing from the lazy runtime batch");

		runtime->DynamicInstantiateStatus =
			TomCat::Scripting::ScriptStatus::ManagedException;
		TomCat::PrefabInstantiationResult failed;
		Require(TomCat::PrefabArchiveCodec::Instantiate(archive, scene, options,
			failed, error), error.c_str());
		std::vector<TomCat::UUID> failedIDs;
		for (const auto& [localID, sceneID] : failed.LocalToSceneUUID)
		{
			(void)localID;
			failedIDs.push_back(sceneID);
		}
		const uint32_t destroysBeforeFailure = runtime->DestroyedAttachmentCount;
		scene.OnRuntimeStep();
		Require(runtime->DynamicInstantiateCount == 2
			&& runtime->DestroyedAttachmentCount == destroysBeforeFailure + 1,
			"failed dynamic attachment batch was not reported and rolled back");
		Require(std::all_of(failedIDs.begin(), failedIDs.end(),
			[&](TomCat::UUID entityID)
			{
				return !scene.FindEntityByUUID(entityID);
			}), "managed Prefab attachment failure did not roll back the entire entity batch");
		Require(scene.FindEntityByUUID(successful.Root.GetUUID())
			&& scene.FindEntityByUUID(existing.GetUUID()),
			"dynamic rollback removed entities outside its failed batch");
		scene.OnRuntimeStop();

		// A script-free Player package intentionally has no managed runtime. Its
		// pure native batches must still drain, while a scripted batch is rejected
		// atomically instead of accumulating forever or skipping lifecycle calls.
		TomCat::Scripting::ScriptEngine::Get().SetRuntime({});
		TomCat::Scene nativeOnlyScene;
		TomCat::Entity nativeSurvivor = nativeOnlyScene.CreateEntity("Native survivor");
		Require(nativeOnlyScene.OnRuntimeStart(),
			"script-free Scene could not start without a managed runtime");
		// Exercise the defensive seam explicitly as well as the normal lazy
		// callback path used above.
		nativeOnlyScene.SetRuntimeEntityBatchCreatedCallback({});
		TomCat::Entity nativeBatch = nativeOnlyScene.CreateEntity("Native batch");
		nativeOnlyScene.QueueRuntimeEntityBatchCreated({ nativeBatch.GetUUID() });
		nativeOnlyScene.OnRuntimeStep();
		Require(nativeOnlyScene.GetPendingRuntimeEntityCreateCount() == 0
			&& nativeOnlyScene.FindEntityByUUID(nativeBatch.GetUUID()),
			"script-free pending runtime batch was not consumed without CoreCLR");

		TomCat::PrefabInstantiationResult rejected;
		Require(TomCat::PrefabArchiveCodec::Instantiate(archive, nativeOnlyScene,
			options, rejected, error), error.c_str());
		std::vector<TomCat::UUID> rejectedIDs;
		for (const auto& [localID, sceneID] : rejected.LocalToSceneUUID)
		{
			(void)localID;
			rejectedIDs.push_back(sceneID);
		}
		nativeOnlyScene.OnRuntimeStep();
		Require(nativeOnlyScene.GetPendingRuntimeEntityCreateCount() == 0
			&& std::all_of(rejectedIDs.begin(), rejectedIDs.end(),
				[&](TomCat::UUID entityID)
				{
					return !nativeOnlyScene.FindEntityByUUID(entityID);
				})
			&& nativeOnlyScene.FindEntityByUUID(nativeSurvivor.GetUUID()),
			"scripted batch without a managed runtime was not atomically rejected");
		nativeOnlyScene.OnRuntimeStop();
	}

	void TestInitialOnCreatePrefabPhysicsOrdering()
	{
		TemporaryCookedProject environment;
		TomCat::ProjectConfig config;
		config.Name = "Initial OnCreate Prefab Physics Regression";
		config.Template = "2D";
		config.AssetDirectory = "Assets";
		auto project = TomCat::Project::CreateNew(
			environment.Root / "Project.tcproj", config);
		Require(project != nullptr,
			"could not create the initial-OnCreate Prefab test project");

		const std::filesystem::path scriptPath =
			project->GetAssetPath() / "SpawnProbe.cs";
		WriteTextFile(scriptPath,
			"using TomCat; public sealed class SpawnProbe : TomCatBehaviour {}\n");
		TomCat::AssetManager& assets = TomCat::AssetManager::Get();
		Require(assets.SetProject(project),
			"could not initialize assets for initial-OnCreate Prefab test");
		const TomCat::AssetMetadata* scriptMetadata =
			assets.Registry().GetMetadata(scriptPath);
		Require(scriptMetadata && !scriptMetadata->IsMissing
			&& scriptMetadata->Type == TomCat::AssetType::CSharpScript,
			"initial-OnCreate script fixture was not imported");
		const TomCat::AssetHandle scriptHandle = scriptMetadata->Handle;

		auto prefabSource = TomCat::CreateRef<TomCat::Scene>();
		TomCat::Entity prefabRoot = prefabSource->CreateEntity("OnCreate physics Prefab");
		prefabRoot.AddComponent<TomCat::Rigidbody2D>().Type =
			TomCat::Rigidbody2D::BodyType::Dynamic;
		prefabRoot.AddComponent<TomCat::BoxCollider2D>();
		TomCat::CSharpScriptEntry prefabScript;
		prefabScript.ScriptAsset = scriptHandle;
		prefabScript.LastKnownClassName = "SpawnProbe";
		prefabRoot.AddComponent<TomCat::CSharpScripts>().Scripts.push_back(prefabScript);
		const std::filesystem::path prefabPath =
			project->GetAssetPath() / "OnCreatePhysics.tcprefab";
		TomCat::AssetHandle prefabHandle{ 0 };
		Require(TomCat::PrefabArchiveCodec::SaveSubtree(prefabSource, prefabRoot,
			prefabPath, &prefabHandle) && static_cast<uint64_t>(prefabHandle) != 0,
			"could not save/import the initial-OnCreate physics Prefab");

		auto runtime = std::make_shared<ManagedRuntimeProbe>();
		runtime->InspectDynamicPhysics = true;
		ScriptRuntimeOverride runtimeOverride(runtime);
		TomCat::Scene scene;
		TomCat::Entity spawner = scene.CreateEntity("Initial managed spawner");
		TomCat::CSharpScriptEntry spawnerScript;
		spawnerScript.ScriptAsset = scriptHandle;
		spawnerScript.LastKnownClassName = "SpawnProbe";
		spawner.AddComponent<TomCat::CSharpScripts>().Scripts.push_back(spawnerScript);

		bool queuedFromInitialCreate = false;
		runtime->InvokeCreateAction = [&]
		{
			if (runtime->Attachments.empty())
				return;
			queuedFromInitialCreate = TomCat::Scripting::ScriptEngine::Get()
				.QueueInstantiatePrefab(runtime->Attachments.front().Entity,
					static_cast<uint64_t>(prefabHandle),
					TomCat::Scripting::NativeVector3{ 8.0f, 9.0f, 0.0f },
					TomCat::Scripting::EntityHandleV1{});
		};

		Require(scene.OnRuntimeStart(),
			"initial scripted Scene failed to start");
		Require(queuedFromInitialCreate
			&& runtime->DynamicInstantiateCount == 1
			&& runtime->DynamicAttachments.size() == 1
			&& runtime->DynamicPhysicsReady
			&& runtime->FixedUpdateCount == 0
			&& scene.GetPendingRuntimeEntityCreateCount() == 0,
			"initial OnCreate Prefab reached dynamic lifecycle before its physics proxies were ready");
		const auto invokeCreate = std::find(runtime->Calls.begin(),
			runtime->Calls.end(), "InvokeCreateAll");
		const auto instantiateAttachments = std::find(runtime->Calls.begin(),
			runtime->Calls.end(), "InstantiateAttachments");
		Require(invokeCreate != runtime->Calls.end()
			&& instantiateAttachments != runtime->Calls.end()
			&& invokeCreate < instantiateAttachments,
			"initial OnCreate Prefab was not delivered through the safe-point incremental ABI");
		TomCat::Entity spawned = TomCat::Scripting::ScriptEngine::Get().ResolveEntity(
			runtime->DynamicAttachments.front().Entity);
		Require(spawned
			&& spawned.GetComponent<TomCat::Rigidbody2D>().RuntimeBody != nullptr
			&& spawned.GetComponent<TomCat::BoxCollider2D>().RuntimeFixture != nullptr,
			"initial OnCreate Prefab physics proxies did not survive safe-point delivery");
		scene.OnRuntimeStop();
	}

	void TestComponentSchemaCapability()
	{
		using namespace TomCat::Scripting;
		NativeApiV2 envelope = BuildNativeApiV2();
		NativeComponentSchemaApiV1 schema{};
		uint32_t required = 0;
		const std::string capabilityName(ComponentSchemaCapabilityName);
		const NativeUtf8View capabilityView{
			reinterpret_cast<const uint8_t*>(capabilityName.data()),
			capabilityName.size() };
		Require(envelope.QueryCapability(capabilityView, 1, &schema,
			sizeof(schema), &required) == static_cast<int32_t>(ScriptStatus::Success)
			&& required == sizeof(schema) && schema.Version == 1
			&& schema.Size == sizeof(schema) && schema.GetComponentCount
			&& schema.GetComponent && schema.GetPropertyCount && schema.GetProperty,
			"TomCat.ComponentSchemaApiV1 capability table is incomplete");
		Require(envelope.QueryCapability(capabilityView, 2, nullptr, 0, &required)
			== static_cast<int32_t>(ScriptStatus::VersionMismatch)
			&& required == sizeof(schema),
			"Component schema capability did not reject a newer version");

		uint32_t count = 0;
		const auto descriptors = TomCat::ComponentRegistry::Get().GetDescriptors();
		Require(schema.GetComponentCount(&count)
			== static_cast<int32_t>(ScriptStatus::Success)
			&& count == descriptors.size(),
			"Component schema capability did not enumerate every descriptor");
		auto text = [](NativeUtf8View value)
		{
			return std::string_view(reinterpret_cast<const char*>(value.Data),
				static_cast<size_t>(value.Length));
		};
		auto expectedKind = [](TomCat::PropertyKind kind)
		{
			switch (kind)
			{
				case TomCat::PropertyKind::Bool:
					return NativePropertyKindV1::Bool;
				case TomCat::PropertyKind::Int32:
					return NativePropertyKindV1::Int32;
				case TomCat::PropertyKind::Int64:
					return NativePropertyKindV1::Int64;
				case TomCat::PropertyKind::UInt32:
					return NativePropertyKindV1::UInt32;
				case TomCat::PropertyKind::UInt64:
					return NativePropertyKindV1::UInt64;
				case TomCat::PropertyKind::Float:
					return NativePropertyKindV1::Float;
				case TomCat::PropertyKind::Double:
					return NativePropertyKindV1::Double;
				case TomCat::PropertyKind::String:
					return NativePropertyKindV1::String;
				case TomCat::PropertyKind::Vector2:
					return NativePropertyKindV1::Vector2;
				case TomCat::PropertyKind::Vector3:
					return NativePropertyKindV1::Vector3;
				case TomCat::PropertyKind::Vector4:
					return NativePropertyKindV1::Vector4;
			}
			throw std::runtime_error("Unhandled component property kind");
		};

		for (uint32_t componentIndex = 0; componentIndex < count;
			++componentIndex)
		{
			NativeComponentSchemaInfoV1 component{};
			const TomCat::ComponentDescriptor& descriptor =
				descriptors[componentIndex];
			Require(schema.GetComponent(componentIndex, &component)
				== static_cast<int32_t>(ScriptStatus::Success)
				&& component.TypeId == static_cast<uint64_t>(descriptor.TypeId)
				&& component.ProviderId
					== static_cast<uint64_t>(descriptor.ProviderId)
				&& component.SchemaVersion == descriptor.SchemaVersion
				&& component.PropertyCount == descriptor.Properties.size()
				&& text(component.StableName) == descriptor.StableName
				&& text(component.DisplayName) == descriptor.DisplayName,
				"Component schema metadata did not match its registry descriptor");
			const uint32_t expectedFlags =
				(descriptor.ScriptAccessible
					? static_cast<uint32_t>(
						NativeComponentSchemaFlagsV1::ScriptAccessible) : 0u)
				| (descriptor.InspectorVisible
					? static_cast<uint32_t>(
						NativeComponentSchemaFlagsV1::InspectorVisible) : 0u);
			Require(component.Flags == expectedFlags,
				"Component schema flags did not match the registry");

			uint32_t propertyCount = 0;
			Require(schema.GetPropertyCount(component.TypeId, &propertyCount)
				== static_cast<int32_t>(ScriptStatus::Success)
				&& propertyCount == descriptor.Properties.size(),
				"Component schema property count was inconsistent");
			for (uint32_t propertyIndex = 0; propertyIndex < propertyCount;
				++propertyIndex)
			{
				NativeComponentPropertySchemaInfoV1 property{};
				const TomCat::PropertyDescriptor& descriptorProperty =
					descriptor.Properties[propertyIndex];
				Require(schema.GetProperty(component.TypeId, propertyIndex,
					&property) == static_cast<int32_t>(ScriptStatus::Success)
					&& property.ComponentTypeId == component.TypeId
					&& property.PropertyId
						== static_cast<uint64_t>(descriptorProperty.PropertyId)
					&& property.Kind == static_cast<uint32_t>(
						expectedKind(descriptorProperty.Kind))
					&& text(property.StableName)
						== descriptorProperty.StableName
					&& text(property.DisplayName)
						== descriptorProperty.DisplayName,
					"Component property schema did not match its registry descriptor");
				const uint32_t propertyFlags =
					(descriptorProperty.AssetReference
						? static_cast<uint32_t>(
							NativeComponentPropertyFlagsV1::AssetReference) : 0u)
					| (descriptorProperty.EntityReference
						? static_cast<uint32_t>(
							NativeComponentPropertyFlagsV1::EntityReference) : 0u);
				Require(property.Flags == propertyFlags,
					"Component property schema flags did not match the registry");
			}
		}

		NativeComponentSchemaInfoV1 missing{};
		Require(schema.GetComponent(count, &missing)
			== static_cast<int32_t>(ScriptStatus::NotFound)
			&& schema.GetComponent(0, nullptr)
				== static_cast<int32_t>(ScriptStatus::InvalidArgument)
			&& schema.GetPropertyCount(0, &count)
				== static_cast<int32_t>(ScriptStatus::NotFound),
			"Component schema bounds and identity validation were not enforced");
	}

	struct PluginManagedProperties
	{
		EKIT_COMPONENT(PluginManagedProperties);
		int32_t Count = 0;
		std::string Label;
	};

	constexpr uint64_t PluginManagedProviderId = 0xa21ce22d5c7c48d1ULL;
	constexpr uint64_t PluginManagedTypeId = 0xb05cd92505784468ULL;
	constexpr uint64_t PluginManagedCountId = 0xac0a76ef344849fbULL;
	constexpr uint64_t PluginManagedLabelId = 0xb31adab074094282ULL;

	TomCat::ComponentDescriptor MakePluginManagedPropertiesDescriptor()
	{
		TomCat::ComponentDescriptor descriptor;
		descriptor.ProviderId = TomCat::UUID(PluginManagedProviderId);
		descriptor.TypeId = TomCat::UUID(PluginManagedTypeId);
		descriptor.StableName = "Regression.PluginManagedProperties";
		descriptor.RegisterStorage = [](TomCat::Scene& scene) {
			scene.RegisterComponent<PluginManagedProperties>();
		};
		descriptor.DisplayName = "Plugin Managed Properties";
		descriptor.ScriptAccessible = true;
		descriptor.SupportsTransactionalValidation = true;
		descriptor.Has = [](TomCat::Entity entity)
		{
			return entity && entity.HasComponent<PluginManagedProperties>();
		};
		descriptor.Add = [](TomCat::Entity entity, std::string& error)
		{
			if (!entity || entity.HasComponent<PluginManagedProperties>())
			{
				error = "PluginManagedProperties cannot be added";
				return false;
			}
			entity.AddComponent<PluginManagedProperties>();
			return true;
		};
		descriptor.Remove = [](TomCat::Entity entity, std::string& error)
		{
			if (!entity || !entity.HasComponent<PluginManagedProperties>())
			{
				error = "PluginManagedProperties is absent";
				return false;
			}
			entity.RemoveComponent<PluginManagedProperties>();
			return true;
		};
		descriptor.Copy = [](TomCat::Entity source, TomCat::Entity destination,
			std::string& error)
		{
			if (!source || !destination
				|| !source.HasComponent<PluginManagedProperties>())
			{
				error = "Invalid PluginManagedProperties copy";
				return false;
			}
			destination.AddOrReplaceComponent<PluginManagedProperties>(
				source.GetComponent<PluginManagedProperties>());
			return true;
		};

		TomCat::PropertyDescriptor count;
		count.PropertyId = TomCat::UUID(PluginManagedCountId);
		count.StableName = "Count";
		count.DisplayName = "Count";
		count.Kind = TomCat::PropertyKind::Int32;
		count.DefaultValue = PluginManagedProperties{}.Count;
		count.Get = [](TomCat::Entity entity) -> TomCat::PropertyValue
		{
			return entity.GetComponent<PluginManagedProperties>().Count;
		};
		count.Set = [](TomCat::Entity entity, const TomCat::PropertyValue& value,
			std::string&)
		{
			entity.GetComponent<PluginManagedProperties>().Count =
				std::get<int32_t>(value);
			return true;
		};
		descriptor.Properties.push_back(std::move(count));

		TomCat::PropertyDescriptor label;
		label.PropertyId = TomCat::UUID(PluginManagedLabelId);
		label.StableName = "Label";
		label.DisplayName = "Label";
		label.Kind = TomCat::PropertyKind::String;
		label.DefaultValue = PluginManagedProperties{}.Label;
		label.Get = [](TomCat::Entity entity) -> TomCat::PropertyValue
		{
			return entity.GetComponent<PluginManagedProperties>().Label;
		};
		label.Set = [](TomCat::Entity entity, const TomCat::PropertyValue& value,
			std::string&)
		{
			entity.GetComponent<PluginManagedProperties>().Label =
				std::get<std::string>(value);
			return true;
		};
		descriptor.Properties.push_back(std::move(label));
		return descriptor;
	}

	struct ProviderTransactionComponent
	{
		EKIT_COMPONENT(ProviderTransactionComponent);
		int32_t Value = 0;
	};

	struct ProviderTransactionEffects
	{
		uint32_t CommitAdds = 0;
		uint32_t CommitRemoves = 0;
		uint32_t CommitCopies = 0;
		uint32_t CommitSets = 0;
		std::unordered_map<uint64_t, int32_t> Sidecar;

		void Reset()
		{
			CommitAdds = 0;
			CommitRemoves = 0;
			CommitCopies = 0;
			CommitSets = 0;
			Sidecar.clear();
		}
	};

	constexpr uint64_t ProviderTransactionProviderId = 0x35a6062318b24ee1ULL;
	constexpr uint64_t ProviderTransactionTypeId = 0x8629ee6aa9184c50ULL;
	constexpr uint64_t ProviderTransactionRejectedTypeId = 0x8bf882ad41e44d6aULL;
	constexpr uint64_t ProviderTransactionValueId = 0x8840fe9232cc4bc0ULL;

	TomCat::ComponentDescriptor MakeProviderTransactionDescriptor(
		const std::shared_ptr<ProviderTransactionEffects>& effects)
	{
		TomCat::ComponentDescriptor descriptor;
		descriptor.ProviderId = TomCat::UUID(ProviderTransactionProviderId);
		descriptor.TypeId = TomCat::UUID(ProviderTransactionTypeId);
		descriptor.StableName = "Regression.ProviderTransactionComponent";
		descriptor.RegisterStorage = [](TomCat::Scene& scene) {
			scene.RegisterComponent<ProviderTransactionComponent>();
		};
		descriptor.DisplayName = "Provider Transaction Component";
		descriptor.ScriptAccessible = true;
		descriptor.SupportsTransactionalValidation = true;
		descriptor.Has = [](TomCat::Entity entity)
		{
			return entity && entity.HasComponent<ProviderTransactionComponent>();
		};
		descriptor.Add = [effects](TomCat::Entity entity, std::string& error)
		{
			if (!entity || entity.HasComponent<ProviderTransactionComponent>())
			{
				error = "ProviderTransactionComponent cannot be added";
				return false;
			}
			entity.AddComponent<ProviderTransactionComponent>();
			const uint64_t entityId = static_cast<uint64_t>(entity.GetUUID());
			if (TomCat::GetComponentMutationPhase()
				== TomCat::ComponentMutationPhase::Commit)
			{
				++effects->CommitAdds;
				effects->Sidecar[entityId] = 0;
			}
			return true;
		};
		descriptor.Remove = [effects](TomCat::Entity entity, std::string& error)
		{
			if (!entity || !entity.HasComponent<ProviderTransactionComponent>())
			{
				error = "ProviderTransactionComponent is absent";
				return false;
			}
			const uint64_t entityId = static_cast<uint64_t>(entity.GetUUID());
			entity.RemoveComponent<ProviderTransactionComponent>();
			if (TomCat::GetComponentMutationPhase()
				== TomCat::ComponentMutationPhase::Commit)
			{
				++effects->CommitRemoves;
				effects->Sidecar.erase(entityId);
			}
			return true;
		};
		descriptor.Copy = [effects](TomCat::Entity source,
			TomCat::Entity destination, std::string& error)
		{
			if (!source || !destination
				|| !source.HasComponent<ProviderTransactionComponent>())
			{
				error = "Invalid ProviderTransactionComponent copy";
				return false;
			}
			const int32_t value =
				source.GetComponent<ProviderTransactionComponent>().Value;
			destination.AddOrReplaceComponent<ProviderTransactionComponent>().Value =
				value;
			const uint64_t destinationId =
				static_cast<uint64_t>(destination.GetUUID());
			if (TomCat::GetComponentMutationPhase()
				== TomCat::ComponentMutationPhase::Commit)
			{
				++effects->CommitCopies;
				effects->Sidecar[destinationId] = value;
			}
			return true;
		};

		TomCat::PropertyDescriptor value;
		value.PropertyId = TomCat::UUID(ProviderTransactionValueId);
		value.StableName = "Value";
		value.DisplayName = "Value";
		value.Kind = TomCat::PropertyKind::Int32;
		value.DefaultValue = ProviderTransactionComponent{}.Value;
		value.Get = [](TomCat::Entity entity) -> TomCat::PropertyValue
		{
			return entity.GetComponent<ProviderTransactionComponent>().Value;
		};
		value.Set = [effects](TomCat::Entity entity,
			const TomCat::PropertyValue& propertyValue, std::string&)
		{
			const int32_t typedValue = std::get<int32_t>(propertyValue);
			entity.GetComponent<ProviderTransactionComponent>().Value = typedValue;
			const uint64_t entityId = static_cast<uint64_t>(entity.GetUUID());
			if (TomCat::GetComponentMutationPhase()
				== TomCat::ComponentMutationPhase::Commit)
			{
				++effects->CommitSets;
				effects->Sidecar[entityId] = typedValue;
			}
			return true;
		};
		descriptor.Properties.push_back(std::move(value));
		return descriptor;
	}


	void TestDeferredCommandsCapability()
	{
		using namespace TomCat::Scripting;
		const NativeApiV1 native = BuildNativeApiV1();
		const NativeApiV2 envelope = BuildNativeApiV2();
		NativeDeferredCommandsApiV1 deferred{};
		uint32_t required = 0;
		const std::string capabilityName(DeferredCommandsCapabilityName);
		const NativeUtf8View capabilityView{
			reinterpret_cast<const uint8_t*>(capabilityName.data()),
			capabilityName.size() };

		Require(envelope.QueryCapability(capabilityView, 1, nullptr, 0,
			&required) == static_cast<int32_t>(ScriptStatus::BufferTooSmall)
			&& required == sizeof(deferred),
			"Deferred commands capability size probe did not report its ABI size");
		std::array<uint8_t, sizeof(NativeDeferredCommandsApiV1) - 1>
			undersized{};
		required = 0;
		Require(envelope.QueryCapability(capabilityView, 1, undersized.data(),
			static_cast<uint32_t>(undersized.size()), &required)
				== static_cast<int32_t>(ScriptStatus::BufferTooSmall)
			&& required == sizeof(deferred),
			"Deferred commands capability accepted an undersized ABI buffer");
		required = 0;
		Require(envelope.QueryCapability(capabilityView, 2, nullptr, 0,
			&required) == static_cast<int32_t>(ScriptStatus::VersionMismatch)
			&& required == sizeof(deferred),
			"Deferred commands capability did not reject a newer version");
		required = 0;
		Require(envelope.QueryCapability(capabilityView, 1, &deferred,
			sizeof(deferred), &required)
				== static_cast<int32_t>(ScriptStatus::Success)
			&& required == sizeof(deferred) && deferred.Version == 1
			&& deferred.Size == sizeof(deferred) && deferred.AbortBatch,
			"TomCat.DeferredCommandsApiV1 capability table is incomplete");

		NativeDeferredCallbackTransactionsApiV1 callbackTransactions{};
		required = 0;
		const std::string callbackTransactionsName(
			DeferredCallbackTransactionsCapabilityName);
		const NativeUtf8View callbackTransactionsView{
			reinterpret_cast<const uint8_t*>(callbackTransactionsName.data()),
			callbackTransactionsName.size() };
		Require(envelope.QueryCapability(callbackTransactionsView, 1,
			nullptr, 0, &required)
				== static_cast<int32_t>(ScriptStatus::BufferTooSmall)
			&& required == sizeof(callbackTransactions),
			"Deferred callback transactions size probe did not report its ABI size");
		std::array<uint8_t,
			sizeof(NativeDeferredCallbackTransactionsApiV1) - 1>
			callbackTransactionsUndersized{};
		required = 0;
		Require(envelope.QueryCapability(callbackTransactionsView, 1,
			callbackTransactionsUndersized.data(),
			static_cast<uint32_t>(callbackTransactionsUndersized.size()),
			&required) == static_cast<int32_t>(ScriptStatus::BufferTooSmall)
			&& required == sizeof(callbackTransactions),
			"Deferred callback transactions accepted an undersized ABI buffer");
		required = 0;
		Require(envelope.QueryCapability(callbackTransactionsView, 2,
			nullptr, 0, &required)
				== static_cast<int32_t>(ScriptStatus::VersionMismatch)
			&& required == sizeof(callbackTransactions),
			"Deferred callback transactions did not reject a newer version");
		required = 0;
		Require(envelope.QueryCapability(callbackTransactionsView, 1,
			&callbackTransactions, sizeof(callbackTransactions), &required)
				== static_cast<int32_t>(ScriptStatus::Success)
			&& required == sizeof(callbackTransactions)
			&& callbackTransactions.Version == 1
			&& callbackTransactions.Size == sizeof(callbackTransactions)
			&& callbackTransactions.BeginCallback
			&& callbackTransactions.CompleteCallback,
			"TomCat.DeferredCallbackTransactionsApiV1 capability table is incomplete");

		auto runtime = std::make_shared<ManagedRuntimeProbe>();
		ScriptRuntimeOverride runtimeOverride(runtime);
		TomCat::Scene scene;
		TomCat::Entity context = scene.CreateEntity(
			"Deferred capability context");
		constexpr uint64_t Generation = 0xd3f3;
		const uint64_t session = ScriptEngine::Get().StartScene(scene,
			Generation);
		Require(session != 0,
			"could not start deferred capability transaction scene");
		const EntityHandleV1 contextHandle{ session,
			static_cast<uint64_t>(context.GetUUID()), Generation };
		const int32_t boxCollider = static_cast<int32_t>(
			NativeComponentType::BoxCollider2D);

		Require(native.AddComponentDeferred(contextHandle, boxCollider)
				== static_cast<int32_t>(ScriptStatus::Success)
			&& native.HasComponent(contextHandle, boxCollider) == 1
			&& !context.HasComponent<TomCat::BoxCollider2D>(),
			"deferred capability rollback fixture was not queued or projected");
		const std::string reason = "managed callback regression fault";
		const NativeUtf8View reasonView{
			reinterpret_cast<const uint8_t*>(reason.data()), reason.size() };
		Require(deferred.AbortBatch(contextHandle, reasonView)
			== static_cast<int32_t>(ScriptStatus::Success),
			"DeferredCommandsApiV1 AbortBatch rejected a valid Scene context");
		ScriptEngine::Get().FlushDeferredCommands(session);
		Require(!context.HasComponent<TomCat::BoxCollider2D>()
			&& runtime->DeferredBatchCommitCount == 0
			&& runtime->DeferredBatchAbortCount == 1 && runtime->Active,
			"AbortBatch did not poison and roll back the complete deferred batch");

		Require(native.AddComponentDeferred(contextHandle, boxCollider)
			== static_cast<int32_t>(ScriptStatus::Success),
			"deferred capability remained poisoned after the aborted batch");
		ScriptEngine::Get().FlushDeferredCommands(session);
		Require(context.HasComponent<TomCat::BoxCollider2D>()
			&& runtime->DeferredBatchCommitCount == 1
			&& runtime->DeferredBatchAbortCount == 1,
			"a fresh deferred batch did not commit after AbortBatch rollback");

		const uint32_t callbackAbortBaseline =
			runtime->DeferredBatchAbortCount;
		uint64_t callbackToken = 0;
		Require(callbackTransactions.BeginCallback(contextHandle, &callbackToken)
				== static_cast<int32_t>(ScriptStatus::Success)
			&& callbackToken != 0,
			"could not begin an isolated managed callback transaction");
		uint64_t overlappingToken = 99;
		Require(callbackTransactions.BeginCallback(contextHandle,
			&overlappingToken) == static_cast<int32_t>(ScriptStatus::InvalidState)
			&& overlappingToken == 0,
			"callback transaction accepted an overlapping BeginCallback");
		Require(native.RemoveComponentDeferred(contextHandle, boxCollider)
				== static_cast<int32_t>(ScriptStatus::Success)
			&& native.HasComponent(contextHandle, boxCollider) == 0
			&& context.HasComponent<TomCat::BoxCollider2D>(),
			"callback transaction did not expose its own projected removal");
		Require(deferred.AbortBatch(contextHandle, reasonView)
				== static_cast<int32_t>(ScriptStatus::Success)
			&& callbackTransactions.CompleteCallback(callbackToken)
				== static_cast<int32_t>(ScriptStatus::Success),
			"could not complete the aborted callback transaction");
		Require(context.HasComponent<TomCat::BoxCollider2D>()
			&& runtime->DeferredBatchAbortCount
				== callbackAbortBaseline + 1,
			"aborted callback transaction leaked its projected removal");

		const uint32_t callbackCommitBaseline =
			runtime->DeferredBatchCommitCount;
		callbackToken = 0;
		Require(callbackTransactions.BeginCallback(contextHandle, &callbackToken)
				== static_cast<int32_t>(ScriptStatus::Success)
			&& native.RemoveComponentDeferred(contextHandle, boxCollider)
				== static_cast<int32_t>(ScriptStatus::Success)
			&& callbackTransactions.CompleteCallback(callbackToken)
				== static_cast<int32_t>(ScriptStatus::Success),
			"could not commit the callback following an aborted callback");
		Require(!context.HasComponent<TomCat::BoxCollider2D>()
			&& runtime->DeferredBatchCommitCount
				== callbackCommitBaseline + 1,
			"a failed callback poisoned the following callback transaction");

		bool nestedQueued = false;
		bool nestedCallbackFailed = false;
		runtime->ResolveDeferredBatchAction = [&](bool committed)
		{
			if (!committed || nestedQueued)
				return;
			nestedQueued = true;
			uint64_t nestedToken = 0;
			nestedCallbackFailed =
				callbackTransactions.BeginCallback(contextHandle, &nestedToken)
					!= static_cast<int32_t>(ScriptStatus::Success)
				|| native.AddComponentDeferred(contextHandle,
					static_cast<int32_t>(NativeComponentType::CircleCollider2D))
					!= static_cast<int32_t>(ScriptStatus::Success)
				|| callbackTransactions.CompleteCallback(nestedToken)
					!= static_cast<int32_t>(ScriptStatus::Success);
		};
		const uint32_t nestedCommitBaseline =
			runtime->DeferredBatchCommitCount;
		callbackToken = 0;
		Require(callbackTransactions.BeginCallback(contextHandle, &callbackToken)
				== static_cast<int32_t>(ScriptStatus::Success)
			&& native.AddComponentDeferred(contextHandle, boxCollider)
				== static_cast<int32_t>(ScriptStatus::Success)
			&& callbackTransactions.CompleteCallback(callbackToken)
				== static_cast<int32_t>(ScriptStatus::Success),
			"could not complete the parent callback transaction");
		runtime->ResolveDeferredBatchAction = {};
		Require(nestedQueued && !nestedCallbackFailed
			&& context.HasComponent<TomCat::BoxCollider2D>()
			&& context.HasComponent<TomCat::CircleCollider2D>()
			&& runtime->DeferredBatchCommitCount
				== nestedCommitBaseline + 2,
			"nested callback transaction was recursively merged or not drained FIFO");

		const uint32_t emptyCommitBaseline =
			runtime->DeferredBatchCommitCount;
		callbackToken = 0;
		Require(callbackTransactions.BeginCallback(contextHandle, &callbackToken)
				== static_cast<int32_t>(ScriptStatus::Success)
			&& callbackTransactions.CompleteCallback(callbackToken)
				== static_cast<int32_t>(ScriptStatus::Success)
			&& runtime->DeferredBatchCommitCount == emptyCommitBaseline + 1,
			"empty callback transaction did not resolve its managed projection frame");
		ScriptEngine::Get().StopScene(session);
	}

	void TestThirdPartyTransactionalComponentContract()
	{
		using namespace TomCat::Scripting;
		TomCat::ComponentRegistry& registry = TomCat::ComponentRegistry::Get();
		auto effects = std::make_shared<ProviderTransactionEffects>();
		std::string error;

		TomCat::ComponentDescriptor rejected =
			MakeProviderTransactionDescriptor(effects);
		rejected.TypeId = TomCat::UUID(ProviderTransactionRejectedTypeId);
		rejected.StableName = "Regression.NonTransactionalScriptComponent";
		rejected.DisplayName = "Non-Transactional Script Component";
		rejected.SupportsTransactionalValidation = false;
		Require(!registry.Register(std::move(rejected), error)
			&& registry.Find(TomCat::UUID(ProviderTransactionRejectedTypeId))
				== nullptr,
			"third-party ScriptAccessible descriptor bypassed the transactional-validation opt-in");

		error.clear();
		Require(registry.Register(MakeProviderTransactionDescriptor(effects), error),
			error.c_str());

		auto copySource = TomCat::CreateRef<TomCat::Scene>();
		TomCat::Entity source = copySource->CreateEntity("Provider copy source");
		source.AddComponent<ProviderTransactionComponent>().Value = 17;
		TomCat::Ref<TomCat::Scene> validationCopy;
		{
			TomCat::ComponentMutationPhaseScope validationPhase(
				TomCat::ComponentMutationPhase::Validation);
			validationCopy = TomCat::Scene::Copy(copySource);
		}
		TomCat::Entity validationCopyEntity = validationCopy
			? validationCopy->FindEntityByUUID(source.GetUUID()) : TomCat::Entity{};
		Require(validationCopyEntity
			&& validationCopyEntity.HasComponent<ProviderTransactionComponent>()
			&& validationCopyEntity.GetComponent<ProviderTransactionComponent>().Value
				== 17
			&& effects->CommitCopies == 0 && effects->Sidecar.empty(),
			"Scene::Copy validation leaked provider Copy side effects");
		validationCopy.reset();

		TomCat::Entity commitCopy =
			copySource->CreateEntity("Provider commit copy");
		const TomCat::ComponentDescriptor* registered =
			registry.Find(TomCat::UUID(ProviderTransactionTypeId));
		error.clear();
		Require(registered
			&& registered->Copy(source, commitCopy, error)
			&& commitCopy.HasComponent<ProviderTransactionComponent>()
			&& commitCopy.GetComponent<ProviderTransactionComponent>().Value == 17
			&& effects->CommitCopies == 1
			&& effects->Sidecar.size() == 1
			&& effects->Sidecar.at(static_cast<uint64_t>(commitCopy.GetUUID()))
				== 17,
			error.empty() ? "provider Copy did not publish exactly once in Commit phase"
				: error.c_str());
		source.RemoveComponent<ProviderTransactionComponent>();
		commitCopy.RemoveComponent<ProviderTransactionComponent>();
		copySource.reset();
		effects->Reset();

		auto runtime = std::make_shared<ManagedRuntimeProbe>();
		ScriptRuntimeOverride runtimeOverride(runtime);
		TomCat::Scene scene;
		TomCat::Entity target = scene.CreateEntity("Provider transaction target");
		TomCat::Entity cycleA = scene.CreateEntity("Provider cycle A");
		TomCat::Entity cycleB = scene.CreateEntity("Provider cycle B");
		constexpr uint64_t Generation = 0xc017;
		const uint64_t session = ScriptEngine::Get().StartScene(scene, Generation);
		Require(session != 0, "could not start provider transaction scene");
		const EntityHandleV1 targetHandle{ session,
			static_cast<uint64_t>(target.GetUUID()), Generation };
		const EntityHandleV1 cycleAHandle{ session,
			static_cast<uint64_t>(cycleA.GetUUID()), Generation };
		const EntityHandleV1 cycleBHandle{ session,
			static_cast<uint64_t>(cycleB.GetUUID()), Generation };
		NativePropertyValueV1 property{};
		property.Kind = static_cast<uint32_t>(NativePropertyKindV1::Int32);
		property.Integer = 41;

		Require(ScriptEngine::Get().QueueAddRegisteredComponent(
				targetHandle, ProviderTransactionTypeId)
			&& ScriptEngine::Get().QueueSetRegisteredComponentProperty(
				targetHandle, ProviderTransactionTypeId,
				ProviderTransactionValueId, property)
			&& !target.HasComponent<ProviderTransactionComponent>()
			&& effects->CommitAdds == 0 && effects->CommitSets == 0
			&& effects->Sidecar.empty(),
			"queued provider Add/Set published before transaction commit");
		ScriptEngine::Get().FlushDeferredCommands(session);
		const uint64_t targetId = static_cast<uint64_t>(target.GetUUID());
		Require(target.HasComponent<ProviderTransactionComponent>()
			&& target.GetComponent<ProviderTransactionComponent>().Value == 41
			&& effects->CommitAdds == 1 && effects->CommitSets == 1
			&& effects->CommitRemoves == 0 && effects->CommitCopies == 0
			&& effects->Sidecar.size() == 1
			&& effects->Sidecar.at(targetId) == 41,
			"successful deferred provider Add/Set did not publish exactly once");

		Require(ScriptEngine::Get().QueueRemoveRegisteredComponent(
				targetHandle, ProviderTransactionTypeId)
			&& target.HasComponent<ProviderTransactionComponent>()
			&& effects->CommitRemoves == 0
			&& effects->Sidecar.contains(targetId),
			"queued provider Remove published before transaction commit");
		ScriptEngine::Get().FlushDeferredCommands(session);
		Require(!target.HasComponent<ProviderTransactionComponent>()
			&& effects->CommitAdds == 1 && effects->CommitSets == 1
			&& effects->CommitRemoves == 1 && effects->CommitCopies == 0
			&& effects->Sidecar.empty(),
			"successful deferred provider Remove did not publish exactly once");

		const uint32_t commitAddsBeforeAbort = effects->CommitAdds;
		const uint32_t commitSetsBeforeAbort = effects->CommitSets;
		const uint32_t commitRemovesBeforeAbort = effects->CommitRemoves;
		const uint32_t commitCopiesBeforeAbort = effects->CommitCopies;
		const uint32_t abortsBefore = runtime->DeferredBatchAbortCount;
		const bool providerAddQueued =
			ScriptEngine::Get().QueueAddRegisteredComponent(
				targetHandle, ProviderTransactionTypeId);
		const bool providerSetQueued =
			ScriptEngine::Get().QueueSetRegisteredComponentProperty(
				targetHandle, ProviderTransactionTypeId,
				ProviderTransactionValueId, property);
		const bool firstParentQueued =
			ScriptEngine::Get().QueueSetParent(cycleAHandle, cycleBHandle);
		const bool cycleRejected =
			!ScriptEngine::Get().QueueSetParent(cycleBHandle, cycleAHandle);
		Require(providerAddQueued && providerSetQueued && firstParentQueued
			&& cycleRejected,
			"deferred parent cycle was not rejected after queuing provider mutations");
		ScriptEngine::Get().FlushDeferredCommands(session);
		Require(!target.HasComponent<ProviderTransactionComponent>()
			&& !scene.GetParent(cycleA) && !scene.GetParent(cycleB)
			&& effects->CommitAdds == commitAddsBeforeAbort
			&& effects->CommitSets == commitSetsBeforeAbort
			&& effects->CommitRemoves == commitRemovesBeforeAbort
			&& effects->CommitCopies == commitCopiesBeforeAbort
			&& effects->Sidecar.empty()
			&& runtime->DeferredBatchAbortCount == abortsBefore + 1,
			"parent-cycle abort leaked provider counter or sidecar mutations");

		ScriptEngine::Get().StopScene(session);
		const std::array<TomCat::Entity, 3> liveEntities{
			target, cycleA, cycleB };
		error.clear();
		Require(registry.UnregisterProvider(
			TomCat::UUID(ProviderTransactionProviderId), liveEntities, error),
			error.c_str());
	}

	void TestRegisteredComponentStringCapability()
	{
		using namespace TomCat::Scripting;
		NativeApiV2 envelope = BuildNativeApiV2();
		NativeComponentApiV1 components{};
		NativeComponentStringApiV1 strings{};
		NativeComponentSchemaApiV1 schema{};
		uint32_t required = 0;
		auto query = [&](std::string_view name, auto& table)
		{
			const NativeUtf8View view{
				reinterpret_cast<const uint8_t*>(name.data()), name.size() };
			return envelope.QueryCapability(view, 1, &table, sizeof(table),
				&required);
		};
		Require(query(ComponentCapabilityName, components)
			== static_cast<int32_t>(ScriptStatus::Success)
			&& components.GetProperty && components.SetProperty,
			"ComponentApiV1 was unavailable to the plugin property regression");
		Require(query(ComponentStringCapabilityName, strings)
			== static_cast<int32_t>(ScriptStatus::Success)
			&& required == sizeof(strings) && strings.Version == 1
			&& strings.Size == sizeof(strings) && strings.GetProperty
			&& strings.SetProperty,
			"ComponentStringApiV1 capability table is incomplete");
		const NativeUtf8View stringCapabilityView{
			reinterpret_cast<const uint8_t*>(ComponentStringCapabilityName.data()),
			ComponentStringCapabilityName.size() };
		Require(envelope.QueryCapability(stringCapabilityView, 2, nullptr, 0,
			&required) == static_cast<int32_t>(ScriptStatus::VersionMismatch)
			&& required == sizeof(strings),
			"Component string capability did not reject a newer version");
		Require(envelope.QueryCapability(stringCapabilityView, 1, &strings,
			sizeof(strings) - 1, &required)
			== static_cast<int32_t>(ScriptStatus::BufferTooSmall)
			&& required == sizeof(strings),
			"Component string capability did not report its complete table size");
		Require(query(ComponentSchemaCapabilityName, schema)
			== static_cast<int32_t>(ScriptStatus::Success),
			"Component schema API was unavailable to the plugin regression");

		TomCat::ComponentRegistry& registry = TomCat::ComponentRegistry::Get();
		std::string error;
		Require(registry.Register(MakePluginManagedPropertiesDescriptor(), error),
			error.c_str());
		uint32_t componentCount = 0;
		Require(schema.GetComponentCount(&componentCount)
			== static_cast<int32_t>(ScriptStatus::Success),
			"could not enumerate the registered plugin component");
		bool discovered = false;
		for (uint32_t index = 0; index < componentCount; ++index)
		{
			NativeComponentSchemaInfoV1 component{};
			Require(schema.GetComponent(index, &component)
				== static_cast<int32_t>(ScriptStatus::Success),
				"component schema enumeration failed");
			if (component.TypeId != PluginManagedTypeId)
				continue;
			discovered = component.ProviderId == PluginManagedProviderId
				&& (component.Flags & static_cast<uint32_t>(
					NativeComponentSchemaFlagsV1::ScriptAccessible)) != 0
				&& component.PropertyCount == 2;
			NativeComponentPropertySchemaInfoV1 property{};
			discovered = discovered
				&& schema.GetProperty(PluginManagedTypeId, 1, &property)
					== static_cast<int32_t>(ScriptStatus::Success)
				&& property.PropertyId == PluginManagedLabelId
				&& property.Kind == static_cast<uint32_t>(
					NativePropertyKindV1::String);
			break;
		}
		Require(discovered,
			"third-party component string metadata was not discoverable through C# ABI");

		auto runtime = std::make_shared<ManagedRuntimeProbe>();
		ScriptRuntimeOverride runtimeOverride(runtime);
		TomCat::Scene scene;
		TomCat::Entity entity = scene.CreateEntity("Managed plugin properties");
		constexpr uint64_t Generation = 0x7a15;
		const uint64_t session = ScriptEngine::Get().StartScene(scene, Generation);
		Require(session != 0, "could not start plugin property test scene");
		const EntityHandleV1 handle{ session,
			static_cast<uint64_t>(entity.GetUUID()), Generation };

		NativePropertyValueV1 count{};
		count.Kind = static_cast<uint32_t>(NativePropertyKindV1::Int32);
		count.Integer = 73;
		const std::string queuedLabel = "queued plugin \xe6\xa0\x87\xe7\xad\xbe \xf0\x9f\x98\x80";
		const NativeUtf8View queuedLabelView{
			reinterpret_cast<const uint8_t*>(queuedLabel.data()), queuedLabel.size() };
		Require(components.Add(handle, PluginManagedTypeId)
			== static_cast<int32_t>(ScriptStatus::Success),
			"deferred plugin Add was rejected");
		NativePropertyValueV1 defaultCount{};
		uint32_t defaultLabelBytes = 99;
		Require(components.GetProperty(handle, PluginManagedTypeId,
				PluginManagedCountId, &defaultCount)
				== static_cast<int32_t>(ScriptStatus::Success)
			&& defaultCount.Integer == 0
			&& strings.GetProperty(handle, PluginManagedTypeId,
				PluginManagedLabelId, nullptr, 0, &defaultLabelBytes)
				== static_cast<int32_t>(ScriptStatus::Success)
			&& defaultLabelBytes == 0,
			"plugin Add getter did not expose explicit numeric/string defaults");
		Require(components.SetProperty(handle, PluginManagedTypeId,
				PluginManagedCountId, count)
				== static_cast<int32_t>(ScriptStatus::Success)
			&& strings.SetProperty(handle, PluginManagedTypeId,
				PluginManagedLabelId, queuedLabelView)
				== static_cast<int32_t>(ScriptStatus::Success),
			"deferred plugin Add/property chain was rejected");
		NativePropertyValueV1 projectedCount{};
		Require(components.GetProperty(handle, PluginManagedTypeId,
			PluginManagedCountId, &projectedCount)
			== static_cast<int32_t>(ScriptStatus::Success)
			&& projectedCount.Kind
				== static_cast<uint32_t>(NativePropertyKindV1::Int32)
			&& projectedCount.Integer == 73,
			"plugin numeric getter did not observe its queued write before flush");
		uint32_t projectedLabelBytes = 0;
		Require(strings.GetProperty(handle, PluginManagedTypeId,
			PluginManagedLabelId, nullptr, 0, &projectedLabelBytes)
			== static_cast<int32_t>(ScriptStatus::BufferTooSmall)
			&& projectedLabelBytes == queuedLabel.size(),
			"plugin string getter did not observe its queued write before flush");
		std::vector<uint8_t> projectedLabel(projectedLabelBytes);
		Require(strings.GetProperty(handle, PluginManagedTypeId,
			PluginManagedLabelId, projectedLabel.data(), projectedLabelBytes,
			&projectedLabelBytes) == static_cast<int32_t>(ScriptStatus::Success)
			&& std::string(projectedLabel.begin(), projectedLabel.end())
				== queuedLabel,
			"plugin string getter returned the wrong queued UTF-8 value");
		ScriptEngine::Get().FlushDeferredCommands(session);
		Require(entity.HasComponent<PluginManagedProperties>()
			&& entity.GetComponent<PluginManagedProperties>().Count == 73
			&& entity.GetComponent<PluginManagedProperties>().Label == queuedLabel,
			"deferred plugin numeric/string properties were not committed in order");

		const uint32_t removeAbortBefore =
			runtime->DeferredBatchAbortCount;
		Require(components.Remove(handle, PluginManagedTypeId)
				== static_cast<int32_t>(ScriptStatus::Success)
			&& components.SetProperty(handle, PluginManagedTypeId,
				PluginManagedCountId, count)
				== static_cast<int32_t>(ScriptStatus::NotFound)
			&& entity.GetComponent<PluginManagedProperties>().Count == 73,
			"projected plugin Remove allowed a setter to mutate the live component");
		ScriptEngine::Get().FlushDeferredCommands(session);
		Require(entity.HasComponent<PluginManagedProperties>()
			&& entity.GetComponent<PluginManagedProperties>().Count == 73
			&& entity.GetComponent<PluginManagedProperties>().Label == queuedLabel
			&& runtime->DeferredBatchAbortCount == removeAbortBefore + 1,
			"rejected projected plugin setter did not abort its Remove batch");
		Require(components.Remove(handle, PluginManagedTypeId)
				== static_cast<int32_t>(ScriptStatus::Success)
			&& components.Add(handle, PluginManagedTypeId)
				== static_cast<int32_t>(ScriptStatus::Success),
			"plugin Remove+Add reset was rejected");
		defaultCount = {};
		defaultLabelBytes = 99;
		Require(components.GetProperty(handle, PluginManagedTypeId,
				PluginManagedCountId, &defaultCount)
				== static_cast<int32_t>(ScriptStatus::Success)
			&& defaultCount.Integer == 0
			&& strings.GetProperty(handle, PluginManagedTypeId,
				PluginManagedLabelId, nullptr, 0, &defaultLabelBytes)
				== static_cast<int32_t>(ScriptStatus::Success)
			&& defaultLabelBytes == 0,
			"plugin Remove+Add getter leaked stale live property values");
		Require(components.SetProperty(handle, PluginManagedTypeId,
				PluginManagedCountId, count)
				== static_cast<int32_t>(ScriptStatus::Success)
			&& strings.SetProperty(handle, PluginManagedTypeId,
				PluginManagedLabelId, queuedLabelView)
				== static_cast<int32_t>(ScriptStatus::Success)
			&& entity.GetComponent<PluginManagedProperties>().Count == 73
			&& entity.GetComponent<PluginManagedProperties>().Label == queuedLabel,
			"plugin Remove+Add setters bypassed the deferred structural reset");
		ScriptEngine::Get().FlushDeferredCommands(session);

		NativePropertyValueV1 readCount{};
		Require(components.GetProperty(handle, PluginManagedTypeId,
			PluginManagedCountId, &readCount)
			== static_cast<int32_t>(ScriptStatus::Success)
			&& readCount.Kind == static_cast<uint32_t>(NativePropertyKindV1::Int32)
			&& readCount.Integer == 73,
			"plugin numeric property did not round-trip through ComponentApiV1");
		required = 0;
		Require(strings.GetProperty(handle, PluginManagedTypeId,
			PluginManagedLabelId, nullptr, 0, &required)
			== static_cast<int32_t>(ScriptStatus::BufferTooSmall)
			&& required == queuedLabel.size(),
			"plugin string size probe did not report exact UTF-8 bytes");
		std::vector<uint8_t> small(required - 1);
		uint32_t smallRequired = 0;
		Require(strings.GetProperty(handle, PluginManagedTypeId,
			PluginManagedLabelId, small.data(), static_cast<uint32_t>(small.size()),
			&smallRequired) == static_cast<int32_t>(ScriptStatus::BufferTooSmall)
			&& smallRequired == required,
			"plugin string read accepted an undersized buffer");
		std::vector<uint8_t> bytes(required);
		uint32_t actual = 0;
		Require(strings.GetProperty(handle, PluginManagedTypeId,
			PluginManagedLabelId, bytes.data(), static_cast<uint32_t>(bytes.size()),
			&actual) == static_cast<int32_t>(ScriptStatus::Success)
			&& actual == bytes.size()
			&& std::string(bytes.begin(), bytes.end()) == queuedLabel,
			"plugin string UTF-8 bytes did not round-trip exactly");
		Require(strings.GetProperty(handle, PluginManagedTypeId,
			PluginManagedLabelId, nullptr, 0, nullptr)
			== static_cast<int32_t>(ScriptStatus::InvalidArgument)
			&& strings.GetProperty(handle, PluginManagedTypeId,
				PluginManagedCountId, nullptr, 0, &required)
				== static_cast<int32_t>(ScriptStatus::InvalidArgument)
			&& components.GetProperty(handle, PluginManagedTypeId,
				PluginManagedLabelId, &readCount)
				== static_cast<int32_t>(ScriptStatus::Unavailable),
			"string/scalar property transports did not reject incompatible calls");

		const std::array<uint8_t, 2> malformed = { 0xc0, 0xaf };
		const NativeUtf8View malformedView{ malformed.data(), malformed.size() };
		Require(strings.SetProperty(handle, PluginManagedTypeId,
			PluginManagedLabelId, malformedView)
			== static_cast<int32_t>(ScriptStatus::InvalidArgument)
			&& entity.GetComponent<PluginManagedProperties>().Label == queuedLabel,
			"malformed UTF-8 mutated a plugin string property");
		const uint8_t arbitrary = 'x';
		const NativeUtf8View oversizedView{ &arbitrary,
			static_cast<uint64_t>(ComponentStringMaximumBytesV1) + 1 };
		Require(strings.SetProperty(handle, PluginManagedTypeId,
			PluginManagedLabelId, oversizedView)
			== static_cast<int32_t>(ScriptStatus::InvalidArgument),
			"oversized plugin UTF-8 entered the native transport");
		ScriptEngine::Get().FlushDeferredCommands(session);
		entity.GetComponent<PluginManagedProperties>().Label.assign("\xc0\xaf", 2);
		required = 123;
		Require(strings.GetProperty(handle, PluginManagedTypeId,
			PluginManagedLabelId, nullptr, 0, &required)
			== static_cast<int32_t>(ScriptStatus::InvalidState)
			&& required == 0,
			"native plugin emitted malformed UTF-8 through the C# ABI");
		entity.GetComponent<PluginManagedProperties>().Label = queuedLabel;

		std::atomic<int32_t> workerStatus{ 0 };
		std::thread worker([&]()
		{
			uint32_t workerRequired = 0;
			workerStatus.store(strings.GetProperty(handle, PluginManagedTypeId,
				PluginManagedLabelId, nullptr, 0, &workerRequired));
		});
		worker.join();
		Require(workerStatus.load() == static_cast<int32_t>(ScriptStatus::WrongThread),
			"plugin string property access was allowed off the script main thread");

		const std::array<TomCat::Entity, 1> liveEntities = { entity };
		Require(registry.UnregisterProvider(TomCat::UUID(PluginManagedProviderId),
			liveEntities, error), error.c_str());
		required = 456;
		Require(strings.GetProperty(handle, PluginManagedTypeId,
			PluginManagedLabelId, nullptr, 0, &required)
			== static_cast<int32_t>(ScriptStatus::InvalidArgument)
			&& required == 0
			&& components.GetProperty(handle, PluginManagedTypeId,
				PluginManagedCountId, &readCount)
				== static_cast<int32_t>(ScriptStatus::InvalidArgument),
			"provider unload left plugin C# property callbacks reachable");
		ScriptEngine::Get().StopScene(session);
	}

	void TestReservedEntityChainedInitializationCapability()
	{
		using namespace TomCat::Scripting;
		NativeApiV2 envelope = BuildNativeApiV2();
		NativeGameplayApiV1 gameplay{};
		NativeComponentApiV1 components{};
		uint32_t required = 0;
		const std::string capabilityName(GameplayCapabilityName);
		const NativeUtf8View capabilityView{
			reinterpret_cast<const uint8_t*>(capabilityName.data()),
			capabilityName.size() };
		Require(envelope.QueryCapability(capabilityView, 1, &gameplay,
			sizeof(gameplay), &required) == static_cast<int32_t>(ScriptStatus::Success)
			&& required == sizeof(gameplay) && gameplay.CreateEntityDeferred
			&& gameplay.GetComponentProperty && gameplay.SetComponentProperty
			&& gameplay.GetActiveSelf && gameplay.SetActiveSelf
			&& gameplay.GetParent && gameplay.SetParentDeferred,
			"TomCat.GameplayApiV1 capability table is incomplete");
		const std::string componentCapabilityName(ComponentCapabilityName);
		const NativeUtf8View componentCapabilityView{
			reinterpret_cast<const uint8_t*>(componentCapabilityName.data()),
			componentCapabilityName.size() };
		Require(envelope.QueryCapability(componentCapabilityView, 1, &components,
			sizeof(components), &required) == static_cast<int32_t>(ScriptStatus::Success)
			&& components.Has && components.Add && components.SetProperty,
			"TomCat.ComponentApiV1 capability table is incomplete");

		auto runtime = std::make_shared<ManagedRuntimeProbe>();
		ScriptRuntimeOverride runtimeOverride(runtime);
		TomCat::Scene scene;
		TomCat::Entity context = scene.CreateEntity("Reserved entity context");
		constexpr uint64_t Generation = 0x51a7;
		EntityHandleV1 pendingParent{};
		EntityHandleV1 pendingChild{};
		std::string callbackFailure;
		auto record = [&](bool condition, const char* message)
		{
			if (!condition && callbackFailure.empty())
				callbackFailure = message;
			return condition;
		};
		runtime->InvokeCreateAction = [&]()
		{
			const EntityHandleV1 contextHandle{ runtime->LastSceneSession,
				static_cast<uint64_t>(context.GetUUID()), Generation };
			const NativeVector3 parentPosition{ 1.0f, 2.0f, 0.0f };
			const NativeVector3 childPosition{ 4.0f, 5.0f, 0.0f };
			const std::string parentName = "Reserved parent";
			const std::string childName = "Reserved child";
			const NativeUtf8View parentNameView{
				reinterpret_cast<const uint8_t*>(parentName.data()), parentName.size() };
			const NativeUtf8View childNameView{
				reinterpret_cast<const uint8_t*>(childName.data()), childName.size() };
			if (!record(gameplay.CreateEntityDeferred(contextHandle, parentNameView,
				parentPosition, {}, &pendingParent)
				== static_cast<int32_t>(ScriptStatus::Success),
				"could not reserve the pending parent")) return;
			if (!record(gameplay.CreateEntityDeferred(contextHandle, childNameView,
				childPosition, {}, &pendingChild)
				== static_cast<int32_t>(ScriptStatus::Success),
				"could not reserve the pending child")) return;

			if (!record(envelope.V1.AddComponentDeferred(pendingChild,
				static_cast<int32_t>(NativeComponentType::BoxCollider2D))
				== static_cast<int32_t>(ScriptStatus::Success),
				"pending BoxCollider2D add was rejected")) return;
			if (!record(envelope.V1.HasComponent(pendingChild,
				static_cast<int32_t>(NativeComponentType::BoxCollider2D)) == 1,
				"pending BoxCollider2D was not visible to GetComponent")) return;
			NativePropertyValueV1 defaultSize{};
			if (!record(gameplay.GetComponentProperty(pendingChild,
				static_cast<int32_t>(NativeComponentType::BoxCollider2D),
				GameplayPropertyIds::BoxSize, &defaultSize)
					== static_cast<int32_t>(ScriptStatus::Success)
				&& std::abs(defaultSize.Vector.X - 0.5f) < 0.0001f
				&& std::abs(defaultSize.Vector.Y - 0.5f) < 0.0001f,
				"pending builtin Add getter did not expose component defaults")) return;
			NativePropertyValueV1 size{};
			size.Kind = static_cast<uint32_t>(NativePropertyKindV1::Vector2);
			size.Vector = { 2.5f, 3.5f, 0.0f, 0.0f };
			if (!record(gameplay.SetComponentProperty(pendingChild,
				static_cast<int32_t>(NativeComponentType::BoxCollider2D),
				GameplayPropertyIds::BoxSize, size)
				== static_cast<int32_t>(ScriptStatus::Success),
				"pending BoxCollider2D.Size write was rejected")) return;
			NativePropertyValueV1 projectedSize{};
			if (!record(gameplay.GetComponentProperty(pendingChild,
				static_cast<int32_t>(NativeComponentType::BoxCollider2D),
				GameplayPropertyIds::BoxSize, &projectedSize)
				== static_cast<int32_t>(ScriptStatus::Success)
				&& projectedSize.Kind
					== static_cast<uint32_t>(NativePropertyKindV1::Vector2)
				&& std::abs(projectedSize.Vector.X - 2.5f) < 0.0001f
				&& std::abs(projectedSize.Vector.Y - 3.5f) < 0.0001f,
				"pending BoxCollider2D.Size getter missed its queued write")) return;

			if (!record(envelope.V1.AddComponentDeferred(pendingChild,
				static_cast<int32_t>(NativeComponentType::SpriteRenderer))
				== static_cast<int32_t>(ScriptStatus::Success),
				"pending SpriteRenderer add was rejected")) return;
			if (!record(envelope.V1.HasComponent(pendingChild,
				static_cast<int32_t>(NativeComponentType::SpriteRenderer)) == 1,
				"pending SpriteRenderer was not visible to GetComponent")) return;
			NativePropertyValueV1 color{};
			color.Kind = static_cast<uint32_t>(NativePropertyKindV1::Vector4);
			color.Vector = { 0.25f, 0.5f, 0.75f, 1.0f };
			if (!record(gameplay.SetComponentProperty(pendingChild,
				static_cast<int32_t>(NativeComponentType::SpriteRenderer),
				GameplayPropertyIds::SpriteColor, color)
				== static_cast<int32_t>(ScriptStatus::Success),
				"pending SpriteRenderer.Color write was rejected")) return;
			NativePropertyValueV1 projectedColor{};
			if (!record(gameplay.GetComponentProperty(pendingChild,
				static_cast<int32_t>(NativeComponentType::SpriteRenderer),
				GameplayPropertyIds::SpriteColor, &projectedColor)
				== static_cast<int32_t>(ScriptStatus::Success)
				&& projectedColor.Kind
					== static_cast<uint32_t>(NativePropertyKindV1::Vector4)
				&& std::abs(projectedColor.Vector.X - 0.25f) < 0.0001f
				&& std::abs(projectedColor.Vector.Y - 0.5f) < 0.0001f,
				"pending SpriteRenderer.Color getter missed its queued write")) return;

			if (!record(components.Add(pendingChild, TomCat::ComponentIds::Health)
				== static_cast<int32_t>(ScriptStatus::Success),
				"pending registered Health add was rejected")) return;
			if (!record(components.Has(pendingChild, TomCat::ComponentIds::Health) == 1,
				"pending registered Health was not visible to GetComponent")) return;
			NativePropertyValueV1 maximum{};
			maximum.Kind = static_cast<uint32_t>(NativePropertyKindV1::Int32);
			maximum.Integer = 250;
			if (!record(components.SetProperty(pendingChild,
				TomCat::ComponentIds::Health,
				TomCat::ComponentIds::HealthProperties::Maximum, maximum)
				== static_cast<int32_t>(ScriptStatus::Success),
				"pending Health.Maximum write was rejected")) return;
			NativePropertyValueV1 current = maximum;
			current.Integer = 175;
			if (!record(components.SetProperty(pendingChild,
				TomCat::ComponentIds::Health,
				TomCat::ComponentIds::HealthProperties::Current, current)
				== static_cast<int32_t>(ScriptStatus::Success),
				"pending Health.Current write was rejected")) return;
			NativePropertyValueV1 projectedHealth{};
			if (!record(components.GetProperty(pendingChild,
				TomCat::ComponentIds::Health,
				TomCat::ComponentIds::HealthProperties::Current,
				&projectedHealth) == static_cast<int32_t>(ScriptStatus::Success)
				&& projectedHealth.Integer == 175,
				"pending Health.Current getter missed its queued write")) return;

			if (!record(gameplay.SetActiveSelf(pendingChild, 0)
				== static_cast<int32_t>(ScriptStatus::Success),
				"pending ActiveSelf write was rejected")) return;
			if (!record(gameplay.GetActiveSelf(pendingChild) == 0,
				"pending ActiveSelf getter missed its queued write")) return;
			if (!record(gameplay.SetParentDeferred(pendingChild, pendingParent)
				== static_cast<int32_t>(ScriptStatus::Success),
				"pending-to-pending parent assignment was rejected")) return;
			EntityHandleV1 projectedParent{};
			record(gameplay.GetParent(pendingChild, &projectedParent)
					== static_cast<int32_t>(ScriptStatus::Success)
				&& projectedParent.SceneSessionId == pendingParent.SceneSessionId
				&& projectedParent.EntityId == pendingParent.EntityId
				&& projectedParent.RuntimeGeneration == pendingParent.RuntimeGeneration,
				"pending parent getter missed its queued reparent");
		};

		const uint64_t session = ScriptEngine::Get().StartScene(scene, Generation);
		bool committed = session != 0 && callbackFailure.empty();
		TomCat::Entity parent;
		TomCat::Entity child;
		if (committed)
		{
			parent = scene.FindEntityByUUID(TomCat::UUID(pendingParent.EntityId));
			child = scene.FindEntityByUUID(TomCat::UUID(pendingChild.EntityId));
			committed = parent && child
				&& child.HasComponent<TomCat::BoxCollider2D>()
				&& child.HasComponent<TomCat::SpriteRenderer>()
				&& child.HasComponent<TomCat::HealthComponent>()
				&& child.HasComponent<TomCat::Tag>()
				&& !child.GetComponent<TomCat::Tag>().ActiveSelf
				&& scene.GetParent(child) == parent;
		}
		if (committed)
		{
			const auto& box = child.GetComponent<TomCat::BoxCollider2D>();
			const auto& sprite = child.GetComponent<TomCat::SpriteRenderer>();
			const auto& health = child.GetComponent<TomCat::HealthComponent>();
			committed = std::abs(box.Size.x - 2.5f) < 0.0001f
				&& std::abs(box.Size.y - 3.5f) < 0.0001f
				&& std::abs(sprite._Color.r - 0.25f) < 0.0001f
				&& std::abs(sprite._Color.g - 0.5f) < 0.0001f
				&& std::abs(sprite._Color.b - 0.75f) < 0.0001f
				&& std::abs(sprite._Color.a - 1.0f) < 0.0001f
				&& health.Maximum == 250 && health.Current == 175;
		}
		bool pendingPrefabParentAccepted = false;
		if (session != 0)
		{
			const EntityHandleV1 contextHandle{ session,
				static_cast<uint64_t>(context.GetUUID()), Generation };
			const EntityHandleV1 childHandle{ session,
				static_cast<uint64_t>(child.GetUUID()), Generation };
			NativePropertyValueV1 restoredSize{};
			restoredSize.Kind =
				static_cast<uint32_t>(NativePropertyKindV1::Vector2);
			restoredSize.Vector = { 2.5f, 3.5f, 0.0f, 0.0f };
			const glm::vec2 liveSizeBeforeReset =
				child.GetComponent<TomCat::BoxCollider2D>().Size;

			NativePropertyValueV1 invalidSize = restoredSize;
			invalidSize.Vector.X = -1.0f;
			const uint32_t invalidAbortBefore =
				runtime->DeferredBatchAbortCount;
			const bool invalidSetRejected =
				gameplay.SetComponentProperty(childHandle,
					static_cast<int32_t>(NativeComponentType::BoxCollider2D),
					GameplayPropertyIds::BoxSize, invalidSize)
					== static_cast<int32_t>(ScriptStatus::InvalidArgument);
			ScriptEngine::Get().FlushDeferredCommands(session);
			const bool invalidSetAborted = child.HasComponent<TomCat::BoxCollider2D>()
				&& child.GetComponent<TomCat::BoxCollider2D>().Size
					== liveSizeBeforeReset
				&& runtime->DeferredBatchAbortCount == invalidAbortBefore + 1;
			EntityHandleV1 wrongGeneration = childHandle;
			++wrongGeneration.RuntimeGeneration;
			const bool wrongGenerationRejected =
				envelope.V1.AddComponentDeferred(wrongGeneration,
					static_cast<int32_t>(NativeComponentType::Rigidbody2D))
					!= static_cast<int32_t>(ScriptStatus::Success);

			const uint32_t removeAbortBefore =
				runtime->DeferredBatchAbortCount;
			const bool removeRejectedSet =
				envelope.V1.RemoveComponentDeferred(childHandle,
					static_cast<int32_t>(NativeComponentType::BoxCollider2D))
					== static_cast<int32_t>(ScriptStatus::Success)
				&& gameplay.SetComponentProperty(childHandle,
					static_cast<int32_t>(NativeComponentType::BoxCollider2D),
					GameplayPropertyIds::BoxSize, restoredSize)
					== static_cast<int32_t>(ScriptStatus::NotFound);
			ScriptEngine::Get().FlushDeferredCommands(session);
			const bool removeAborted = child.HasComponent<TomCat::BoxCollider2D>()
				&& child.GetComponent<TomCat::BoxCollider2D>().Size
					== liveSizeBeforeReset
				&& runtime->DeferredBatchAbortCount == removeAbortBefore + 1;

			NativePropertyValueV1 resetDefault{};
			const bool removeAddProjected =
				envelope.V1.RemoveComponentDeferred(childHandle,
					static_cast<int32_t>(NativeComponentType::BoxCollider2D))
					== static_cast<int32_t>(ScriptStatus::Success)
				&& envelope.V1.AddComponentDeferred(childHandle,
					static_cast<int32_t>(NativeComponentType::BoxCollider2D))
					== static_cast<int32_t>(ScriptStatus::Success)
				&& gameplay.GetComponentProperty(childHandle,
					static_cast<int32_t>(NativeComponentType::BoxCollider2D),
					GameplayPropertyIds::BoxSize, &resetDefault)
					== static_cast<int32_t>(ScriptStatus::Success)
				&& std::abs(resetDefault.Vector.X - 0.5f) < 0.0001f
				&& std::abs(resetDefault.Vector.Y - 0.5f) < 0.0001f
				&& gameplay.SetComponentProperty(childHandle,
					static_cast<int32_t>(NativeComponentType::BoxCollider2D),
					GameplayPropertyIds::BoxSize, restoredSize)
					== static_cast<int32_t>(ScriptStatus::Success)
				&& child.GetComponent<TomCat::BoxCollider2D>().Size
					== liveSizeBeforeReset;
			ScriptEngine::Get().FlushDeferredCommands(session);
			Require(invalidSetRejected && invalidSetAborted
				&& wrongGenerationRejected && removeRejectedSet
				&& removeAborted && removeAddProjected,
				"builtin rejected setter, Remove abort, or Remove+Add projection failed");
			EntityHandleV1 prefabParent{};
			const NativeVector3 position{};
			pendingPrefabParentAccepted = ScriptEngine::Get().QueueCreateEntity(
				contextHandle, "Pending Prefab parent", position, {}, prefabParent)
				&& envelope.V1.PrefabInstantiateDeferred(contextHandle, 0x424242,
					position, prefabParent) == 1;
			ScriptEngine::Get().StopScene(session);
		}

		TomCat::Scene atomicScene;
		TomCat::Entity atomicContext =
			atomicScene.CreateEntity("Deferred transaction context");
		EntityHandleV1 cycleA{};
		EntityHandleV1 cycleB{};
		std::string atomicCallbackFailure;
		auto atomicRecord = [&](bool condition, const char* message)
		{
			if (!condition && atomicCallbackFailure.empty())
				atomicCallbackFailure = message;
			return condition;
		};
		runtime->InvokeCreateAction = [&]()
		{
			const EntityHandleV1 contextHandle{ runtime->LastSceneSession,
				static_cast<uint64_t>(atomicContext.GetUUID()), Generation };
			const NativeVector3 position{};
			const std::string nameA = "Deferred cycle A";
			const std::string nameB = "Deferred cycle B";
			const NativeUtf8View nameAView{
				reinterpret_cast<const uint8_t*>(nameA.data()), nameA.size() };
			const NativeUtf8View nameBView{
				reinterpret_cast<const uint8_t*>(nameB.data()), nameB.size() };
			if (!atomicRecord(envelope.V1.AddComponentDeferred(contextHandle,
				static_cast<int32_t>(NativeComponentType::BoxCollider2D))
					== static_cast<int32_t>(ScriptStatus::Success),
				"could not queue the mutation before the invalid command")) return;
			if (!atomicRecord(gameplay.CreateEntityDeferred(contextHandle, nameAView,
				position, {}, &cycleA)
					== static_cast<int32_t>(ScriptStatus::Success),
				"could not reserve deferred cycle A")) return;
			if (!atomicRecord(gameplay.CreateEntityDeferred(contextHandle, nameBView,
				position, {}, &cycleB)
					== static_cast<int32_t>(ScriptStatus::Success),
				"could not reserve deferred cycle B")) return;
			if (!atomicRecord(gameplay.SetParentDeferred(cycleB, cycleA)
					== static_cast<int32_t>(ScriptStatus::Success),
				"could not queue the valid half of the deferred cycle")) return;
			atomicRecord(gameplay.SetParentDeferred(cycleA, cycleB)
					!= static_cast<int32_t>(ScriptStatus::Success),
				"late parent cycle was not rejected by the projected transaction view");
		};
		const uint64_t atomicSession =
			ScriptEngine::Get().StartScene(atomicScene, Generation);
		const bool atomicRollback = atomicSession != 0
			&& atomicCallbackFailure.empty()
			&& runtime->DeferredBatchAbortCount >= 1
			&& !atomicScene.FindEntityByUUID(TomCat::UUID(cycleA.EntityId))
			&& !atomicScene.FindEntityByUUID(TomCat::UUID(cycleB.EntityId))
			&& !atomicContext.HasComponent<TomCat::BoxCollider2D>();
		if (atomicSession != 0)
			ScriptEngine::Get().StopScene(atomicSession);

		Require(callbackFailure.empty(), callbackFailure.empty()
			? "reserved entity callback failed" : callbackFailure.c_str());
		Require(committed,
			"reserved entity chain was not committed in Create/Add/property/Active/Parent order");
		Require(runtime->DeferredBatchCommitCount >= 2,
			"successful deferred batches did not acknowledge the managed projection");
		Require(pendingPrefabParentAccepted,
			"Prefab instantiate did not accept a same-session pending parent");
		Require(atomicCallbackFailure.empty(), atomicCallbackFailure.empty()
			? "deferred transaction callback failed"
			: atomicCallbackFailure.c_str());
		Require(atomicRollback,
			"a late parent-cycle failure exposed earlier deferred mutations");
	}


	void TestProjectedTransactionReadYourWritesAndRollback()
	{
		using namespace TomCat::Scripting;
		const NativeApiV1 native = BuildNativeApiV1();
		const NativeApiV2 envelope = BuildNativeApiV2();
		NativeGameplayApiV1 gameplay{};
		NativeComponentApiV1 components{};
		uint32_t required = 0;
		const std::string gameplayCapabilityName(GameplayCapabilityName);
		const NativeUtf8View gameplayCapabilityView{
			reinterpret_cast<const uint8_t*>(gameplayCapabilityName.data()),
			gameplayCapabilityName.size() };
		const std::string componentCapabilityName(ComponentCapabilityName);
		const NativeUtf8View componentCapabilityView{
			reinterpret_cast<const uint8_t*>(componentCapabilityName.data()),
			componentCapabilityName.size() };
		Require(envelope.QueryCapability(gameplayCapabilityView, 1, &gameplay,
			sizeof(gameplay), &required) == static_cast<int32_t>(ScriptStatus::Success)
			&& gameplay.CreateEntityDeferred && gameplay.FindEntityByName
			&& gameplay.QueryEntities && gameplay.GetParent
			&& gameplay.SetParentDeferred && gameplay.GetChildren
			&& gameplay.GetComponentProperty && gameplay.SetComponentProperty,
			"TomCat.GameplayApiV1 transaction projection functions are unavailable");
		Require(envelope.QueryCapability(componentCapabilityView, 1, &components,
			sizeof(components), &required) == static_cast<int32_t>(ScriptStatus::Success)
			&& components.Has && components.Add && components.GetProperty
			&& components.SetProperty,
			"TomCat.ComponentApiV1 transaction projection functions are unavailable");

		auto runtime = std::make_shared<ManagedRuntimeProbe>();
		ScriptRuntimeOverride runtimeOverride(runtime);
		TomCat::Scene scene;
		TomCat::Entity context = scene.CreateEntity("Projection context");
		scene.CreateEntity("Projected duplicate");
		TomCat::Entity parentA = scene.CreateEntity("Projection parent A");
		TomCat::Entity parentB = scene.CreateEntity("Projection parent B");
		TomCat::Entity child = scene.CreateEntity("Projection child");
		TomCat::Entity mutationTarget =
			scene.CreateEntity("Projection mutation target");
		TomCat::Entity removalTarget =
			scene.CreateEntity("Projection removal target");
		removalTarget.AddComponent<TomCat::SpriteRenderer>();
		auto& health = mutationTarget.AddComponent<TomCat::HealthComponent>();
		health.Maximum = 100;
		health.Current = 20;
		const glm::mat4 parentAWorld = TomCat::Math::ComposeTransform(
			{ 1.0f, 0.0f, 0.0f }, { 0.0f, 0.0f, 0.0f },
			{ 1.0f, 1.0f, 1.0f });
		const glm::mat4 parentBWorld = TomCat::Math::ComposeTransform(
			{ 10.0f, 0.0f, 0.0f }, { 0.0f, 0.0f, 0.0f },
			{ 1.0f, 1.0f, 1.0f });
		const glm::mat4 childWorld = TomCat::Math::ComposeTransform(
			{ 3.0f, 0.0f, 0.0f }, { 0.0f, 0.0f, 0.0f },
			{ 1.0f, 1.0f, 1.0f });
		Require(scene.SetWorldTransform(parentA, parentAWorld)
			&& scene.SetWorldTransform(parentB, parentBWorld)
			&& scene.SetWorldTransform(child, childWorld)
			&& scene.SetParent(child, parentA),
			"could not establish projected transaction hierarchy fixture");

		constexpr uint64_t Generation = 0x71a9;
		const uint64_t session = ScriptEngine::Get().StartScene(scene, Generation);
		std::string failure;
		auto check = [&](bool condition, const char* message)
		{
			if (!condition && failure.empty())
				failure = message;
			return condition;
		};
		if (!check(session != 0,
			"could not start projected transaction scene"))
			Require(false, failure.c_str());

		const auto handleFor = [&](TomCat::Entity entity)
		{
			return EntityHandleV1{ session,
				static_cast<uint64_t>(entity.GetUUID()), Generation };
		};
		const EntityHandleV1 contextHandle = handleFor(context);
		const EntityHandleV1 parentAHandle = handleFor(parentA);
		const EntityHandleV1 parentBHandle = handleFor(parentB);
		const EntityHandleV1 childHandle = handleFor(child);
		const EntityHandleV1 mutationHandle = handleFor(mutationTarget);
		const EntityHandleV1 removalHandle = handleFor(removalTarget);
		auto view = [](const std::string& value)
		{
			return NativeUtf8View{
				reinterpret_cast<const uint8_t*>(value.data()), value.size() };
		};
		auto contains = [](const std::vector<EntityHandleV1>& entities,
			const EntityHandleV1& expected)
		{
			return std::any_of(entities.begin(), entities.end(),
				[&](const EntityHandleV1& candidate)
				{
					return candidate.SceneSessionId == expected.SceneSessionId
						&& candidate.EntityId == expected.EntityId
						&& candidate.RuntimeGeneration
							== expected.RuntimeGeneration;
				});
		};
		auto readText = [&](auto getter, const EntityHandleV1& entity,
			std::string& output)
		{
			uint32_t size = 0;
			const int32_t probe = getter(entity, nullptr, 0, &size);
			if (probe != static_cast<int32_t>(ScriptStatus::BufferTooSmall)
				|| size == 0)
				return false;
			std::vector<uint8_t> bytes(size);
			if (getter(entity, bytes.data(), size, &size)
				!= static_cast<int32_t>(ScriptStatus::Success))
				return false;
			output.assign(reinterpret_cast<const char*>(bytes.data()), size);
			return true;
		};
		auto query = [&](int32_t componentType, uint64_t registeredTypeId,
			std::vector<EntityHandleV1>& output)
		{
			uint32_t count = 0;
			const int32_t probe = gameplay.QueryEntities(contextHandle,
				componentType, registeredTypeId, nullptr, 0, &count);
			if ((count == 0
					&& probe != static_cast<int32_t>(ScriptStatus::Success))
				|| (count != 0
					&& probe != static_cast<int32_t>(
						ScriptStatus::BufferTooSmall)))
				return false;
			output.resize(count);
			if (count == 0)
				return true;
			return gameplay.QueryEntities(contextHandle, componentType,
				registeredTypeId, output.data(), count, &count)
					== static_cast<int32_t>(ScriptStatus::Success)
				&& count == output.size();
		};

		[&]()
		{
			const std::string duplicateName = "Projected duplicate";
			EntityHandleV1 pendingFirst{};
			EntityHandleV1 pendingSecond{};
			if (!check(gameplay.CreateEntityDeferred(contextHandle,
				view(duplicateName), { 4.0f, 0.0f, 0.0f }, {},
				&pendingFirst) == static_cast<int32_t>(ScriptStatus::Success),
				"first pending Create was rejected")) return;
			if (!check(gameplay.CreateEntityDeferred(contextHandle,
				view(duplicateName), { 5.0f, 0.0f, 0.0f }, {},
				&pendingSecond) == static_cast<int32_t>(ScriptStatus::Success),
				"second pending Create was rejected")) return;
			if (!check(native.EntityIsAlive(pendingFirst) == 1
				&& native.EntityIsAlive(pendingSecond) == 1,
				"pending Create was not alive in the projected view")) return;

			if (!check(native.AddComponentDeferred(pendingFirst,
				static_cast<int32_t>(NativeComponentType::BoxCollider2D))
					== static_cast<int32_t>(ScriptStatus::Success),
				"pending builtin Add was rejected")) return;
			if (!check(components.Add(pendingSecond,
				TomCat::ComponentIds::Health)
					== static_cast<int32_t>(ScriptStatus::Success),
				"pending registered Add was rejected")) return;
			NativePropertyValueV1 pendingHealth{};
			pendingHealth.Kind =
				static_cast<uint32_t>(NativePropertyKindV1::Int32);
			pendingHealth.Integer = 73;
			if (!check(components.SetProperty(pendingSecond,
				TomCat::ComponentIds::Health,
				TomCat::ComponentIds::HealthProperties::Current,
				pendingHealth) == static_cast<int32_t>(ScriptStatus::Success),
				"pending registered property write was rejected")) return;

			std::string projectedFirstName;
			std::string projectedSecondName;
			if (!check(readText(native.EntityGetName, pendingFirst,
				projectedFirstName)
				&& readText(native.EntityGetName, pendingSecond,
					projectedSecondName)
				&& projectedFirstName == "Projected duplicate (1)"
				&& projectedSecondName == "Projected duplicate (2)",
				"duplicate pending Creates did not reserve deterministic projected names"))
				return;
			EntityHandleV1 found{};
			if (!check(gameplay.FindEntityByName(contextHandle,
				view(projectedFirstName), &found)
					== static_cast<int32_t>(ScriptStatus::Success)
				&& found.EntityId == pendingFirst.EntityId,
				"FindEntityByName could not see the first pending Create"))
				return;
			if (!check(gameplay.FindEntityByName(contextHandle,
				view(projectedSecondName), &found)
					== static_cast<int32_t>(ScriptStatus::Success)
				&& found.EntityId == pendingSecond.EntityId,
				"FindEntityByName could not see the second pending Create"))
				return;

			std::vector<EntityHandleV1> projectedAll;
			if (!check(ScriptEngine::Get().GetProjectedEntities(
				contextHandle, projectedAll)
				&& contains(projectedAll, pendingFirst)
				&& contains(projectedAll, pendingSecond),
				"projected All did not include pending Creates")) return;
			std::vector<EntityHandleV1> queried;
			if (!check(query(0, 0, queried)
				&& contains(queried, pendingFirst)
				&& contains(queried, pendingSecond),
				"unfiltered Query did not include pending Creates")) return;
			if (!check(query(static_cast<int32_t>(
				NativeComponentType::BoxCollider2D), 0, queried)
				&& contains(queried, pendingFirst),
				"builtin component Query missed a projected Add")) return;
			if (!check(query(0, TomCat::ComponentIds::Health, queried)
				&& contains(queried, pendingSecond),
				"registered component Query missed a projected Add")) return;
			NativePropertyValueV1 projectedPendingHealth{};
			if (!check(components.GetProperty(pendingSecond,
				TomCat::ComponentIds::Health,
				TomCat::ComponentIds::HealthProperties::Current,
				&projectedPendingHealth)
					== static_cast<int32_t>(ScriptStatus::Success)
				&& projectedPendingHealth.Integer == 73,
				"registered property getter missed a pending Create write"))
				return;

			const uint32_t commitsBefore = runtime->DeferredBatchCommitCount;
			const uint32_t abortsBefore = runtime->DeferredBatchAbortCount;
			ScriptEngine::Get().FlushDeferredCommands(session);
			TomCat::Entity committedFirst = scene.FindEntityByUUID(
				TomCat::UUID(pendingFirst.EntityId));
			TomCat::Entity committedSecond = scene.FindEntityByUUID(
				TomCat::UUID(pendingSecond.EntityId));
			if (!check(committedFirst && committedSecond
				&& committedFirst.GetName() == projectedFirstName
				&& committedSecond.GetName() == projectedSecondName
				&& committedFirst.HasComponent<TomCat::BoxCollider2D>()
				&& committedSecond.HasComponent<TomCat::HealthComponent>()
				&& committedSecond.GetComponent<TomCat::HealthComponent>().Current
					== 73
				&& runtime->DeferredBatchCommitCount == commitsBefore + 1
				&& runtime->DeferredBatchAbortCount == abortsBefore,
				"committed pending Creates diverged from their projected names or components"))
				return;

			const std::string originalName = mutationTarget.GetName();
			const std::string originalTag = mutationTarget.GetGameplayTag();
			const uint32_t originalLayer = mutationTarget.GetLayer();
			const int32_t originalHealth =
				mutationTarget.GetComponent<TomCat::HealthComponent>().Current;
			const glm::vec3 originalParentB =
				parentB.GetComponent<TomCat::Transform>()._Translation;
			const glm::vec3 originalChild =
				child.GetComponent<TomCat::Transform>()._Translation;
			const uint32_t commitsBeforeAbort =
				runtime->DeferredBatchCommitCount;
			const uint32_t abortsBeforeAbort =
				runtime->DeferredBatchAbortCount;

			EntityHandleV1 rolledBackCreate{};
			const std::string rollbackName = "Projected rollback create";
			if (!check(gameplay.CreateEntityDeferred(contextHandle,
				view(rollbackName), { 8.0f, 0.0f, 0.0f }, {},
				&rolledBackCreate)
					== static_cast<int32_t>(ScriptStatus::Success),
				"rollback fixture Create was rejected")) return;
			if (!check(native.EntitySetName(mutationHandle,
				view(duplicateName))
					== static_cast<int32_t>(ScriptStatus::Success),
				"projected Name write was rejected")) return;
			const std::string projectedTagValue = "ProjectedPlayer";
			if (!check(native.EntitySetTag(mutationHandle,
				view(projectedTagValue))
					== static_cast<int32_t>(ScriptStatus::Success),
				"projected Tag write was rejected")) return;
			if (!check(native.EntitySetLayer(mutationHandle, 7)
					== static_cast<int32_t>(ScriptStatus::Success),
				"projected Layer write was rejected")) return;
			if (!check(native.AddComponentDeferred(mutationHandle,
				static_cast<int32_t>(NativeComponentType::BoxCollider2D))
					== static_cast<int32_t>(ScriptStatus::Success)
				&& native.HasComponent(mutationHandle,
					static_cast<int32_t>(
						NativeComponentType::BoxCollider2D)) == 1,
				"projected builtin Add was not visible")) return;
			NativePropertyValueV1 projectedBoxSize{};
			projectedBoxSize.Kind =
				static_cast<uint32_t>(NativePropertyKindV1::Vector2);
			projectedBoxSize.Vector = { 2.0f, 3.0f, 0.0f, 0.0f };
			if (!check(gameplay.SetComponentProperty(mutationHandle,
				static_cast<int32_t>(NativeComponentType::BoxCollider2D),
				GameplayPropertyIds::BoxSize, projectedBoxSize)
					== static_cast<int32_t>(ScriptStatus::Success),
				"projected builtin property write was rejected")) return;
			NativePropertyValueV1 readBoxSize{};
			if (!check(gameplay.GetComponentProperty(mutationHandle,
				static_cast<int32_t>(NativeComponentType::BoxCollider2D),
				GameplayPropertyIds::BoxSize, &readBoxSize)
					== static_cast<int32_t>(ScriptStatus::Success)
				&& std::abs(readBoxSize.Vector.X - 2.0f) < 0.0001f
				&& std::abs(readBoxSize.Vector.Y - 3.0f) < 0.0001f,
				"projected builtin property read missed its write")) return;
			if (!check(native.RemoveComponentDeferred(removalHandle,
				static_cast<int32_t>(NativeComponentType::SpriteRenderer))
					== static_cast<int32_t>(ScriptStatus::Success)
				&& native.HasComponent(removalHandle,
					static_cast<int32_t>(
						NativeComponentType::SpriteRenderer)) == 0,
				"projected builtin Remove remained visible")) return;

			NativePropertyValueV1 projectedHealth{};
			projectedHealth.Kind =
				static_cast<uint32_t>(NativePropertyKindV1::Int32);
			projectedHealth.Integer = 77;
			if (!check(components.SetProperty(mutationHandle,
				TomCat::ComponentIds::Health,
				TomCat::ComponentIds::HealthProperties::Current,
				projectedHealth) == static_cast<int32_t>(ScriptStatus::Success),
				"projected registered property write was rejected")) return;
			NativePropertyValueV1 readHealth{};
			if (!check(components.GetProperty(mutationHandle,
				TomCat::ComponentIds::Health,
				TomCat::ComponentIds::HealthProperties::Current,
				&readHealth) == static_cast<int32_t>(ScriptStatus::Success)
				&& readHealth.Integer == 77,
				"projected registered property getter missed its write"))
				return;

			if (!check(gameplay.SetParentDeferred(childHandle, parentBHandle)
					== static_cast<int32_t>(ScriptStatus::Success),
				"projected reparent was rejected")) return;
			EntityHandleV1 projectedParent{};
			uint32_t childCount = 0;
			std::array<EntityHandleV1, 1> children{};
			if (!check(gameplay.GetParent(childHandle, &projectedParent)
					== static_cast<int32_t>(ScriptStatus::Success)
				&& projectedParent.EntityId == parentBHandle.EntityId,
				"projected parent getter missed its reparent")) return;
			if (!check(gameplay.GetChildren(parentAHandle, nullptr, 0,
				&childCount) == static_cast<int32_t>(ScriptStatus::Success)
				&& childCount == 0,
				"projected old-parent children retained the reparented child"))
				return;
			if (!check(gameplay.GetChildren(parentBHandle, children.data(),
				static_cast<uint32_t>(children.size()), &childCount)
					== static_cast<int32_t>(ScriptStatus::Success)
				&& childCount == 1
				&& children[0].EntityId == childHandle.EntityId,
				"projected new-parent children missed the reparented child"))
				return;

			const NativeVector3 movedParent{ 20.0f, 0.0f, 0.0f };
			if (!check(native.TransformSetPosition(parentBHandle, movedParent)
					== static_cast<int32_t>(ScriptStatus::Success),
				"projected parent world Transform write was rejected")) return;
			NativeVector3 projectedChildPosition{};
			if (!check(native.TransformGetPosition(childHandle,
				&projectedChildPosition)
					== static_cast<int32_t>(ScriptStatus::Success)
				&& std::abs(projectedChildPosition.X - 13.0f) < 0.0001f
				&& std::abs(projectedChildPosition.Y) < 0.0001f
				&& std::abs(projectedChildPosition.Z) < 0.0001f,
				"projected parent world Transform did not update its child"))
				return;

			std::string projectedName;
			std::string projectedTag;
			uint32_t projectedLayer = 0;
			if (!check(readText(native.EntityGetName, mutationHandle,
				projectedName)
				&& projectedName == "Projected duplicate (3)",
				"projected Name getter missed its duplicate-resolved write"))
				return;
			if (!check(readText(native.EntityGetTag, mutationHandle,
				projectedTag) && projectedTag == projectedTagValue,
				"projected Tag getter missed its write")) return;
			if (!check(native.EntityGetLayer(mutationHandle, &projectedLayer)
					== static_cast<int32_t>(ScriptStatus::Success)
				&& projectedLayer == 7,
				"projected Layer getter missed its write")) return;
			if (!check(mutationTarget.GetName() == originalName
				&& mutationTarget.GetGameplayTag() == originalTag
				&& mutationTarget.GetLayer() == originalLayer
				&& !mutationTarget.HasComponent<TomCat::BoxCollider2D>()
				&& removalTarget.HasComponent<TomCat::SpriteRenderer>()
				&& mutationTarget.GetComponent<TomCat::HealthComponent>().Current
					== originalHealth
				&& scene.GetParent(child) == parentA
				&& parentB.GetComponent<TomCat::Transform>()._Translation
					== originalParentB
				&& child.GetComponent<TomCat::Transform>()._Translation
					== originalChild,
				"projected writes leaked into live state before Flush"))
				return;

			if (!check(gameplay.SetParentDeferred(parentBHandle, childHandle)
					!= static_cast<int32_t>(ScriptStatus::Success),
				"projected hierarchy cycle was not rejected")) return;
			ScriptEngine::Get().FlushDeferredCommands(session);
			if (!check(runtime->DeferredBatchCommitCount == commitsBeforeAbort
				&& runtime->DeferredBatchAbortCount == abortsBeforeAbort + 1
				&& runtime->Active,
				"rejected projected cycle did not abort exactly one batch"))
				return;
			if (!check(!scene.FindEntityByUUID(
					TomCat::UUID(rolledBackCreate.EntityId))
				&& mutationTarget.GetName() == originalName
				&& mutationTarget.GetGameplayTag() == originalTag
				&& mutationTarget.GetLayer() == originalLayer
				&& !mutationTarget.HasComponent<TomCat::BoxCollider2D>()
				&& removalTarget.HasComponent<TomCat::SpriteRenderer>()
				&& mutationTarget.GetComponent<TomCat::HealthComponent>().Current
					== originalHealth
				&& scene.GetParent(child) == parentA
				&& parentB.GetComponent<TomCat::Transform>()._Translation
					== originalParentB
				&& child.GetComponent<TomCat::Transform>()._Translation
					== originalChild,
				"aborted projected transaction changed live state")) return;
		}();

		if (runtime->Active)
			ScriptEngine::Get().StopScene(session);
		Require(failure.empty(), failure.empty()
			? "projected transaction regression failed" : failure.c_str());
	}

	void TestRegisteredAddProjectionMatchesDescriptorSemantics()
	{
		using namespace TomCat::Scripting;
		auto runtime = std::make_shared<ManagedRuntimeProbe>();
		ScriptRuntimeOverride runtimeOverride(runtime);
		TomCat::Scene scene;
		TomCat::Entity primaryCamera =
			scene.CreateEntity("Existing primary camera");
		primaryCamera.AddComponent<TomCat::C_Camera>().Primary = true;
		TomCat::Entity jointTarget =
			scene.CreateEntity("Projected joint target");
		TomCat::Entity animatorTarget =
			scene.CreateEntity("Projected animator target");
		TomCat::Entity cameraTarget =
			scene.CreateEntity("Projected camera target");

		constexpr uint64_t Generation = 0xa66d;
		const uint64_t session = ScriptEngine::Get().StartScene(scene, Generation);
		Require(session != 0,
			"could not start registered Add projection Scene");
		auto handle = [session](TomCat::Entity entity)
		{
			return EntityHandleV1{ session,
				static_cast<uint64_t>(entity.GetUUID()), Generation };
		};
		const EntityHandleV1 jointHandle = handle(jointTarget);
		const EntityHandleV1 animatorHandle = handle(animatorTarget);
		const EntityHandleV1 cameraHandle = handle(cameraTarget);

		Require(ScriptEngine::Get().QueueAddRegisteredComponent(
				jointHandle, TomCat::ComponentIds::DistanceJoint2D),
			"could not queue registered DistanceJoint2D Add");
		bool projectedRigidbody = false;
		Require(ScriptEngine::Get().GetProjectedRegisteredComponentPresence(
				jointHandle, TomCat::ComponentIds::Rigidbody2D,
				projectedRigidbody)
			&& projectedRigidbody
			&& !jointTarget.HasComponent<TomCat::Rigidbody2D>()
			&& !jointTarget.HasComponent<TomCat::DistanceJoint2D>(),
			"DistanceJoint2D descriptor dependency was absent from projection");

		Require(ScriptEngine::Get().QueueAddRegisteredComponent(
				animatorHandle, TomCat::ComponentIds::SpriteAnimator),
			"could not queue registered SpriteAnimator Add");
		bool projectedRenderer = false;
		Require(ScriptEngine::Get().GetProjectedRegisteredComponentPresence(
				animatorHandle, TomCat::ComponentIds::SpriteRenderer,
				projectedRenderer)
			&& projectedRenderer
			&& !animatorTarget.HasComponent<TomCat::SpriteRenderer>()
			&& !animatorTarget.HasComponent<TomCat::SpriteAnimator>(),
			"SpriteAnimator descriptor dependency was absent from projection");

		Require(ScriptEngine::Get().QueueAddRegisteredComponent(
				cameraHandle, TomCat::ComponentIds::Camera),
			"could not queue registered Camera Add");
		NativePropertyValueV1 projectedPrimary{};
		bool useDefault = true;
		Require(ScriptEngine::Get().TryGetProjectedRegisteredComponentProperty(
				cameraHandle, TomCat::ComponentIds::Camera,
				TomCat::ComponentIds::CameraProperties::Primary,
				projectedPrimary, useDefault)
			&& projectedPrimary.Kind
				== static_cast<uint32_t>(NativePropertyKindV1::Bool)
			&& projectedPrimary.Integer == 0 && !useDefault
			&& !cameraTarget.HasComponent<TomCat::C_Camera>(),
			"Camera.Primary projection ignored descriptor Add Scene semantics");

		ScriptEngine::Get().FlushDeferredCommands(session);
		Require(jointTarget.HasComponent<TomCat::DistanceJoint2D>()
			&& jointTarget.HasComponent<TomCat::Rigidbody2D>()
			&& animatorTarget.HasComponent<TomCat::SpriteAnimator>()
			&& animatorTarget.HasComponent<TomCat::SpriteRenderer>()
			&& cameraTarget.HasComponent<TomCat::C_Camera>()
			&& !cameraTarget.GetComponent<TomCat::C_Camera>().Primary,
			"committed registered Adds disagreed with their projected result");
		ScriptEngine::Get().StopScene(session);
	}

	void TestDeferredDisableBeforeSubtreeDestroyOrdering()
	{
		using namespace TomCat::Scripting;
		auto runtime = std::make_shared<ManagedRuntimeProbe>();
		ScriptRuntimeOverride runtimeOverride(runtime);
		TomCat::Scene scene;
		TomCat::Entity parent =
			scene.CreateEntity("Deferred lifecycle parent");
		TomCat::Entity child =
			scene.CreateEntity("Deferred lifecycle child");
		Require(scene.SetParent(child, parent),
			"could not establish deferred lifecycle hierarchy");
		TomCat::CSharpScriptEntry entry;
		entry.AttachmentID = TomCat::UUID();
		entry.ScriptAsset = TomCat::AssetHandle(9300);
		entry.LastKnownClassName = "Game.DeferredLifecycleProbe";
		child.AddComponent<TomCat::CSharpScripts>().Scripts.push_back(entry);

		constexpr uint64_t Generation = 0xd15ab1e;
		const uint64_t session = ScriptEngine::Get().StartScene(scene, Generation);
		Require(session != 0 && runtime->Attachments.size() == 1,
			"could not start deferred lifecycle ordering scene");
		const uint64_t attachmentId =
			static_cast<uint64_t>(entry.AttachmentID);
		const EntityHandleV1 parentHandle{ session,
			static_cast<uint64_t>(parent.GetUUID()), Generation };
		const EntityHandleV1 childHandle{ session,
			static_cast<uint64_t>(child.GetUUID()), Generation };

		bool managedAttachmentDestroyed = false;
		bool setEnabledSawDisable = false;
		bool destroySawLiveEntity = false;
		uint32_t setEnabledObservations = 0;
		uint32_t destroyObservations = 0;
		runtime->SetEnabledAction =
			[&](uint64_t observedAttachmentId, bool enabled)
		{
			++setEnabledObservations;
			setEnabledSawDisable = setEnabledSawDisable
				|| (observedAttachmentId == attachmentId && !enabled);
			return managedAttachmentDestroyed
				? ScriptStatus::NotFound : ScriptStatus::Success;
		};
		runtime->DestroyAttachmentsAction =
			[&](std::span<const uint64_t> attachmentIds)
		{
			for (uint64_t observedAttachmentId : attachmentIds)
			{
				if (observedAttachmentId != attachmentId)
					continue;
				++destroyObservations;
				TomCat::Entity liveChild =
					ScriptEngine::Get().ResolveEntity(childHandle);
				bool attachmentStillPresent = false;
				if (liveChild && liveChild.HasComponent<TomCat::CSharpScripts>())
				{
					const auto& scripts =
						liveChild.GetComponent<TomCat::CSharpScripts>().Scripts;
					attachmentStillPresent = std::any_of(scripts.begin(),
						scripts.end(), [&](const TomCat::CSharpScriptEntry& script)
						{
							return static_cast<uint64_t>(script.AttachmentID)
								== attachmentId;
						});
				}
				destroySawLiveEntity = liveChild
					&& liveChild.HasComponent<TomCat::Tag>()
					&& liveChild.HasComponent<TomCat::CSharpScripts>()
					&& attachmentStillPresent;
				managedAttachmentDestroyed = true;
			}
		};

		const size_t callsBeforeBatch = runtime->Calls.size();
		const bool queued =
			ScriptEngine::Get().QueueBehaviourEnabled(attachmentId, false)
			&& ScriptEngine::Get().QueueDestroyEntity(parentHandle);
		ScriptEngine::Get().FlushDeferredCommands(session);

		const auto batchBegin = runtime->Calls.begin()
			+ static_cast<std::ptrdiff_t>(callsBeforeBatch);
		const auto setEnabledCall = std::find(batchBegin,
			runtime->Calls.end(), "SetEnabled");
		const auto destroyCall = std::find(batchBegin,
			runtime->Calls.end(), "DestroyAttachments");
		const bool lifecycleOrder = setEnabledCall != runtime->Calls.end()
			&& destroyCall != runtime->Calls.end()
			&& setEnabledCall < destroyCall;
		const bool subtreeDestroyed =
			!scene.FindEntityByUUID(TomCat::UUID(parentHandle.EntityId))
			&& !scene.FindEntityByUUID(TomCat::UUID(childHandle.EntityId));
		const bool batchStayedHealthy = runtime->Active
			&& runtime->DeferredBatchCommitCount == 1
			&& runtime->DeferredBatchAbortCount == 0;
		if (runtime->Active)
			ScriptEngine::Get().StopScene(session);

		Require(queued,
			"could not queue disable followed by subtree destruction");
		Require(setEnabledObservations == 1 && setEnabledSawDisable,
			"deferred disable was dropped or delivered more than once");
		Require(destroyObservations == 1 && destroySawLiveEntity,
			"DestroyAttachments ran after the child ECS identity was removed");
		Require(lifecycleOrder,
			"DestroyAttachments was published before the earlier SetEnabled");
		Require(subtreeDestroyed,
			"deferred lifecycle parent destroy did not remove its subtree");
		Require(batchStayedHealthy,
			"trailing SetEnabled publication targeted an already destroyed attachment and faulted the batch");
	}



	void TestDeferredRemoveLastBehaviourBeforeOwnerDestroyOrdering()
	{
		using namespace TomCat::Scripting;
		auto runtime = std::make_shared<ManagedRuntimeProbe>();
		ScriptRuntimeOverride runtimeOverride(runtime);
		TomCat::Scene scene;
		TomCat::Entity owner =
			scene.CreateEntity("Deferred last attachment owner");
		TomCat::CSharpScriptEntry entry;
		entry.AttachmentID = TomCat::UUID();
		entry.ScriptAsset = TomCat::AssetHandle(9301);
		entry.LastKnownClassName = "Game.DeferredLastAttachmentProbe";
		owner.AddComponent<TomCat::CSharpScripts>().Scripts.push_back(entry);

		constexpr uint64_t Generation = 0x1a57;
		const uint64_t session = ScriptEngine::Get().StartScene(scene, Generation);
		Require(session != 0 && runtime->Attachments.size() == 1,
			"could not start last-attachment lifecycle ordering scene");
		const uint64_t attachmentId =
			static_cast<uint64_t>(entry.AttachmentID);
		const EntityHandleV1 ownerHandle{ session,
			static_cast<uint64_t>(owner.GetUUID()), Generation };

		uint32_t matchingDestroyCallbacks = 0;
		bool destroySawOwnerIdentity = false;
		runtime->DestroyAttachmentsAction =
			[&](std::span<const uint64_t> attachmentIds)
		{
			for (uint64_t observedAttachmentId : attachmentIds)
			{
				if (observedAttachmentId != attachmentId)
					continue;
				++matchingDestroyCallbacks;
				TomCat::Entity liveOwner =
					ScriptEngine::Get().ResolveEntity(ownerHandle);
				destroySawOwnerIdentity = liveOwner
					&& liveOwner.HasComponent<TomCat::Tag>()
					&& liveOwner.HasComponent<TomCat::CSharpScripts>();
			}
		};

		const bool queued =
			ScriptEngine::Get().QueueRemoveBehaviour(attachmentId)
			&& ScriptEngine::Get().QueueDestroyEntity(ownerHandle);
		ScriptEngine::Get().FlushDeferredCommands(session);
		const bool ownerDestroyed =
			!scene.FindEntityByUUID(TomCat::UUID(ownerHandle.EntityId));
		const bool batchStayedHealthy = runtime->Active
			&& runtime->DeferredBatchCommitCount == 1
			&& runtime->DeferredBatchAbortCount == 0;
		if (runtime->Active)
			ScriptEngine::Get().StopScene(session);

		Require(queued,
			"could not queue last behaviour removal followed by owner destruction");
		Require(matchingDestroyCallbacks == 1
			&& runtime->DestroyAttachmentsCallCount == 1
			&& runtime->DestroyedAttachmentCount == 1,
			"last behaviour removal did not publish exactly one attachment destroy");
		Require(destroySawOwnerIdentity,
			"last attachment DestroyAttachments ran after owner Entity, Tag, or CSharpScripts teardown");
		Require(ownerDestroyed,
			"deferred last-attachment owner destroy did not remove the entity");
		Require(batchStayedHealthy,
			"last behaviour removal followed by owner destroy faulted the batch");
	}


	void TestProjectedDestroyBatchSemantics()
	{
		using namespace TomCat::Scripting;
		const NativeApiV1 native = BuildNativeApiV1();
		const NativeApiV2 envelope = BuildNativeApiV2();
		NativeGameplayApiV1 gameplay{};
		uint32_t capabilitySize = 0;
		const std::string capabilityName(GameplayCapabilityName);
		const NativeUtf8View capabilityView{
			reinterpret_cast<const uint8_t*>(capabilityName.data()),
			capabilityName.size() };
		Require(envelope.QueryCapability(capabilityView, 1, &gameplay,
			sizeof(gameplay), &capabilitySize)
				== static_cast<int32_t>(ScriptStatus::Success)
			&& gameplay.FindEntityByName && gameplay.QueryEntities
			&& gameplay.GetParent && gameplay.SetParentDeferred
			&& gameplay.GetChildren && gameplay.GetActiveSelf
			&& gameplay.SetActiveSelf && gameplay.GetActiveInHierarchy
			&& gameplay.TransformGetLocalPosition
			&& gameplay.TransformSetLocalPosition,
			"TomCat.GameplayApiV1 hierarchy accessors are unavailable");
		auto runtime = std::make_shared<ManagedRuntimeProbe>();
		ScriptRuntimeOverride runtimeOverride(runtime);
		TomCat::Scene scene;
		TomCat::Entity parent = scene.CreateEntity("Deferred destroy parent");
		TomCat::Entity child = scene.CreateEntity("Deferred destroy child");
		Require(scene.SetParent(child, parent),
			"could not establish deferred-destroy hierarchy");
		TomCat::Entity observer = scene.CreateEntity("Deferred destroy observer");
		TomCat::Entity filteredChild = scene.CreateEntity("Filtered destroyed child");
		Require(scene.SetParent(filteredChild, observer),
			"could not establish filtered child hierarchy");
		std::array<uint64_t, 2> attachmentIds{};
		size_t attachmentIndex = 0;
		for (TomCat::Entity entity : { parent, child })
		{
			TomCat::CSharpScriptEntry entry;
			entry.AttachmentID = TomCat::UUID();
			entry.ScriptAsset = TomCat::AssetHandle(9100);
			entry.LastKnownClassName = "Game.DestroyProjectionProbe";
			entity.AddComponent<TomCat::CSharpScripts>().Scripts.push_back(entry);
			entity.AddComponent<TomCat::AudioSource>();
			attachmentIds[attachmentIndex++] = static_cast<uint64_t>(entry.AttachmentID);
		}

		constexpr uint64_t Generation = 0xd357;
		const uint64_t session = ScriptEngine::Get().StartScene(scene, Generation);
		Require(session != 0, "could not start deferred-destroy scene");
		const EntityHandleV1 parentHandle{ session,
			static_cast<uint64_t>(parent.GetUUID()), Generation };
		const EntityHandleV1 childHandle{ session,
			static_cast<uint64_t>(child.GetUUID()), Generation };
		const EntityHandleV1 observerHandle{ session,
			static_cast<uint64_t>(observer.GetUUID()), Generation };
		const EntityHandleV1 filteredChildHandle{ session,
			static_cast<uint64_t>(filteredChild.GetUUID()), Generation };

		bool destroyCallbacksSawLiveComponents = true;
		uint32_t destroyCallbackAttachmentCount = 0;
		std::vector<uint64_t> destroyCallbackAttachmentIds;
		runtime->DestroyAttachmentsAction =
			[&](std::span<const uint64_t> destroyedIds)
		{
			destroyCallbackAttachmentCount +=
				static_cast<uint32_t>(destroyedIds.size());
			for (uint64_t attachmentId : destroyedIds)
			{
				destroyCallbackAttachmentIds.push_back(attachmentId);
				const auto attachment = std::find_if(runtime->Attachments.begin(),
					runtime->Attachments.end(), [&](const auto& candidate)
					{
						return candidate.AttachmentId == attachmentId;
					});
				if (attachment == runtime->Attachments.end())
				{
					destroyCallbacksSawLiveComponents = false;
					continue;
				}
				TomCat::Entity callbackEntity =
					ScriptEngine::Get().ResolveEntity(attachment->Entity);
				bool attachmentStillPresent = false;
				if (callbackEntity
					&& callbackEntity.HasComponent<TomCat::CSharpScripts>())
				{
					const auto& scripts =
						callbackEntity.GetComponent<TomCat::CSharpScripts>().Scripts;
					attachmentStillPresent = std::any_of(scripts.begin(),
						scripts.end(), [&](const TomCat::CSharpScriptEntry& script)
						{
							return static_cast<uint64_t>(script.AttachmentID)
								== attachmentId;
						});
				}
				destroyCallbacksSawLiveComponents =
					destroyCallbacksSawLiveComponents && callbackEntity
					&& callbackEntity.HasComponent<TomCat::Tag>()
					&& callbackEntity.HasComponent<TomCat::CSharpScripts>()
					&& callbackEntity.HasComponent<TomCat::AudioSource>()
					&& attachmentStillPresent;
			}
		};

		bool parentAlive = true;
		bool childAlive = true;
		bool childBehaviourEnabled = true;
		uint32_t requiredText = 0;
		uint32_t layer = 0;
		NativeVector3 position{};
		EntityHandleV1 projectedParent{};
		uint32_t requiredChildren = 0;
		uint32_t requiredQuery = 0;
		std::array<EntityHandleV1, 4> queryResults{};
		const std::string replacement = "must not leak";
		const std::string destroyedName = "Deferred destroy parent";
		const NativeUtf8View destroyedNameView{
			reinterpret_cast<const uint8_t*>(destroyedName.data()),
			destroyedName.size() };
		std::string projectionFailure;
		auto recordProjection = [&](bool condition, const char* message)
		{
			if (!condition && projectionFailure.empty())
				projectionFailure = message;
			return condition;
		};
		bool projected = true;
		projected = recordProjection(
			ScriptEngine::Get().QueueDestroyEntity(parentHandle),
			"initial parent Destroy was rejected") && projected;
		projected = recordProjection(
			ScriptEngine::Get().QueueDestroyEntity(parentHandle),
			"duplicate parent Destroy was not a projected no-op") && projected;
		projected = recordProjection(
			ScriptEngine::Get().QueueDestroyEntity(childHandle),
			"descendant Destroy was not a projected no-op") && projected;
		projected = recordProjection(
			ScriptEngine::Get().QueueDestroyEntity(filteredChildHandle),
			"independent child Destroy was rejected") && projected;
		projected = recordProjection(
			ScriptEngine::Get().GetProjectedEntityLiveness(
				parentHandle, parentAlive) && !parentAlive,
			"destroyed parent remained alive in the projected snapshot") && projected;
		projected = recordProjection(
			ScriptEngine::Get().GetProjectedEntityLiveness(
				childHandle, childAlive) && !childAlive,
			"destroyed descendant remained alive in the projected snapshot") && projected;
		projected = recordProjection(
			!ScriptEngine::Get().QueueAddComponent(
				childHandle, NativeComponentType::BoxCollider2D),
			"component Add accepted a projected-dead entity") && projected;
		projected = recordProjection(
			!ScriptEngine::Get().GetBehaviourEnabled(
				attachmentIds[1], childBehaviourEnabled),
			"behaviour getter exposed a projected-dead owner") && projected;
		projected = recordProjection(
			!ScriptEngine::Get().QueueBehaviourEnabled(attachmentIds[1], false),
			"behaviour setter accepted a projected-dead owner") && projected;
		projected = recordProjection(
			ScriptEngine::Get().QueueRemoveBehaviour(attachmentIds[1]),
			"behaviour Remove did not accept projected destruction as a no-op")
			&& projected;
		projected = recordProjection(
			ScriptEngine::Get().QueueRemoveBehaviour(attachmentIds[1]),
			"duplicate behaviour Remove was not a projected no-op") && projected;
		projected = recordProjection(
			native.EntityGetName(childHandle, nullptr, 0, &requiredText)
				== static_cast<int32_t>(ScriptStatus::NotFound),
			"name getter exposed a projected-dead entity") && projected;
		projected = recordProjection(
			native.EntityGetTag(childHandle, nullptr, 0, &requiredText)
				== static_cast<int32_t>(ScriptStatus::NotFound),
			"tag getter exposed a projected-dead entity") && projected;
		projected = recordProjection(
			native.EntityGetLayer(childHandle, &layer)
				== static_cast<int32_t>(ScriptStatus::NotFound),
			"layer getter exposed a projected-dead entity") && projected;
		projected = recordProjection(
			native.TransformGetPosition(childHandle, &position)
				== static_cast<int32_t>(ScriptStatus::NotFound),
			"world Transform getter exposed a projected-dead entity") && projected;
		projected = recordProjection(
			gameplay.GetParent(childHandle, &projectedParent)
				== static_cast<int32_t>(ScriptStatus::NotFound),
			"parent getter exposed a projected-dead entity") && projected;
		projected = recordProjection(
			gameplay.GetChildren(childHandle, nullptr, 0, &requiredChildren)
				== static_cast<int32_t>(ScriptStatus::NotFound),
			"children getter exposed a projected-dead entity") && projected;
		projected = recordProjection(
			gameplay.GetChildren(observerHandle, nullptr, 0, &requiredChildren)
				== static_cast<int32_t>(ScriptStatus::Success)
				&& requiredChildren == 0,
			"children getter did not filter a projected-dead child") && projected;
		projected = recordProjection(
			gameplay.QueryEntities(observerHandle, 0, 0,
				queryResults.data(), static_cast<uint32_t>(queryResults.size()),
				&requiredQuery) == static_cast<int32_t>(ScriptStatus::Success)
				&& requiredQuery == 1
				&& queryResults[0].EntityId == observerHandle.EntityId,
			"entity query did not filter projected-dead entities") && projected;
		projected = recordProjection(
			gameplay.FindEntityByName(observerHandle, destroyedNameView,
				&projectedParent) == static_cast<int32_t>(ScriptStatus::NotFound),
			"name query found a projected-dead entity") && projected;
		projected = recordProjection(
			gameplay.GetActiveSelf(childHandle)
				== static_cast<int32_t>(ScriptStatus::NotFound),
			"ActiveSelf getter exposed a projected-dead entity") && projected;
		projected = recordProjection(
			gameplay.GetActiveInHierarchy(childHandle)
				== static_cast<int32_t>(ScriptStatus::NotFound),
			"ActiveInHierarchy getter exposed a projected-dead entity") && projected;
		projected = recordProjection(
			gameplay.TransformGetLocalPosition(childHandle, &position)
				== static_cast<int32_t>(ScriptStatus::NotFound),
			"local Transform getter exposed a projected-dead entity") && projected;
		projected = recordProjection(
			child.GetName() == "Deferred destroy child"
				&& child.GetGameplayTag() != replacement
				&& child.GetLayer() != 7
				&& child.GetComponent<TomCat::Transform>()._Translation
					!= glm::vec3(8.0f, 9.0f, 10.0f),
			"projected-dead writes leaked into the live ECS before commit")
			&& projected;
		ScriptEngine::Get().FlushDeferredCommands(session);

		const bool subtreeDestroyed =
			!scene.FindEntityByUUID(TomCat::UUID(parentHandle.EntityId))
			&& !scene.FindEntityByUUID(TomCat::UUID(childHandle.EntityId))
			&& !scene.FindEntityByUUID(TomCat::UUID(filteredChildHandle.EntityId))
			&& scene.FindEntityByUUID(TomCat::UUID(observerHandle.EntityId));
		const bool attachmentsPublished =
			runtime->DestroyedAttachmentCount == 2;
		const bool callbacksObservedLiveComponents =
			runtime->DestroyAttachmentsCallCount == 2
			&& destroyCallbackAttachmentCount == 2
			&& destroyCallbacksSawLiveComponents
			&& std::all_of(attachmentIds.begin(), attachmentIds.end(),
				[&](uint64_t attachmentId)
				{
					return std::find(destroyCallbackAttachmentIds.begin(),
						destroyCallbackAttachmentIds.end(), attachmentId)
						!= destroyCallbackAttachmentIds.end();
				});
		const bool batchCommitted = runtime->DeferredBatchCommitCount == 1
			&& runtime->DeferredBatchAbortCount == 0;
		ScriptEngine::Get().StopScene(session);

		Require(projected, projectionFailure.empty()
			? "projected subtree destruction failed"
			: projectionFailure.c_str());
		Require(subtreeDestroyed,
			"deferred parent destroy did not remove its projected subtree");
		Require(attachmentsPublished,
			"deferred subtree destroy did not publish every managed attachment");
		Require(callbacksObservedLiveComponents,
			"DestroyAttachments ran after Entity, Tag, CSharpScripts, or AudioSource teardown");
		Require(batchCommitted,
			"successful subtree destroy did not commit its managed projection once");
	}

	void TestManagedScriptLifecycleBackend()
	{
		for (const int frameRate : { 30, 60, 144 })
		{
			auto runtime = std::make_shared<ManagedRuntimeProbe>();
			TomCat::Scripting::ScriptEngine::Get().SetRuntime(runtime);
			TomCat::Scene scene;
			TomCat::Entity scripted = scene.CreateEntity("Managed lifecycle probe");
			TomCat::CSharpScriptEntry entry;
			entry.AttachmentID = TomCat::UUID();
			entry.ScriptAsset = TomCat::AssetHandle(9001);
			entry.LastKnownClassName = "Game.LifecycleProbe";
			scripted.AddComponent<TomCat::CSharpScripts>().Scripts.push_back(entry);

			Require(scene.OnRuntimeStart(),
				"managed fake backend could not start a scripted scene");
			Require(runtime->Calls.size() >= 4
				&& runtime->Calls[0] == "CreateSceneRuntime"
				&& runtime->Calls[1] == "InstantiateAll"
				&& runtime->Calls[2] == "ApplySerializedFields"
				&& runtime->Calls[3] == "InvokeCreateAll",
				"managed scene startup callbacks were not ordered transactionally");
			Require(runtime->Attachments.size() == 1
				&& runtime->Attachments[0].AttachmentId
					== static_cast<uint64_t>(entry.AttachmentID)
				&& runtime->Attachments[0].Entity.SceneSessionId
					== runtime->LastSceneSession,
				"managed attachment identity was not bound to the active scene session");
			Require(!TomCat::Scripting::ScriptEngine::Get().QueueAddComponent(
				runtime->Attachments[0].Entity,
				static_cast<TomCat::Scripting::NativeComponentType>(999))
				&& !TomCat::Scripting::ScriptEngine::Get().QueueRemoveComponent(
					runtime->Attachments[0].Entity,
					static_cast<TomCat::Scripting::NativeComponentType>(999)),
				"invalid managed component types entered the deferred command queue");
			bool projectedEnabled = false;
			const uint64_t attachmentId = runtime->Attachments[0].AttachmentId;
			Require(TomCat::Scripting::ScriptEngine::Get().GetBehaviourEnabled(
					attachmentId, projectedEnabled) && projectedEnabled
				&& TomCat::Scripting::ScriptEngine::Get().QueueBehaviourEnabled(
					attachmentId, false)
				&& TomCat::Scripting::ScriptEngine::Get().GetBehaviourEnabled(
					attachmentId, projectedEnabled) && !projectedEnabled
				&& TomCat::Scripting::ScriptEngine::Get().QueueBehaviourEnabled(
					attachmentId, true)
				&& TomCat::Scripting::ScriptEngine::Get().GetBehaviourEnabled(
					attachmentId, projectedEnabled) && projectedEnabled,
				"Behaviour.Enabled getter missed its queued write");
			const uint64_t missingAttachmentId = attachmentId == 1 ? 2 : 1;
			Require(!TomCat::Scripting::ScriptEngine::Get().QueueBehaviourEnabled(
					missingAttachmentId, false)
				&& !TomCat::Scripting::ScriptEngine::Get().QueueRemoveBehaviour(
					missingAttachmentId),
				"unknown managed attachment entered the deferred command queue");
			Require(TomCat::Scripting::ScriptEngine::Get().QueueRemoveBehaviour(
					attachmentId)
				&& !TomCat::Scripting::ScriptEngine::Get().QueueBehaviourEnabled(
					attachmentId, false)
				&& TomCat::Scripting::ScriptEngine::Get().QueueRemoveBehaviour(
					attachmentId)
				&& !TomCat::Scripting::ScriptEngine::Get().GetBehaviourEnabled(
					attachmentId, projectedEnabled),
				"RemoveBehaviour projection did not reject Enabled or deduplicate Remove");
			TomCat::Scripting::ScriptEngine::Get().FlushDeferredCommands(
				runtime->LastSceneSession);
			Require(scripted.GetComponent<TomCat::CSharpScripts>().Scripts.empty()
				&& runtime->DestroyedAttachmentCount == 1,
				"projected RemoveBehaviour was not published exactly once");

			for (int frame = 0; frame < frameRate; ++frame)
				scene.OnUpdateRuntime(TomCat::Timestep(1.0f
					/ static_cast<float>(frameRate)));
			Require(runtime->UpdateCount == static_cast<uint32_t>(frameRate),
				"managed OnUpdate was not called once per display frame");
			Require(runtime->FixedUpdateCount == 60,
				"managed OnFixedUpdate was not fixed at 60 Hz");
			const uint32_t updatesBeforeStep = runtime->UpdateCount;
			scene.OnRuntimeStep();
			Require(runtime->FixedUpdateCount == 61
				&& runtime->UpdateCount == updatesBeforeStep,
				"managed Step did not perform exactly one fixed update and zero display updates");
			scene.OnRuntimeStop();
			Require(!runtime->Calls.empty() && runtime->Calls.back() == "DestroyAll",
				"managed Stop did not destroy the scene runtime");
			TomCat::Scripting::ScriptEngine::Get().SetRuntime({});
		}

		{
			auto runtime = std::make_shared<ManagedRuntimeProbe>();
			runtime->InvokeCreateStatus = TomCat::Scripting::ScriptStatus::ManagedException;
			TomCat::Scripting::ScriptEngine::Get().SetRuntime(runtime);
			TomCat::Scene scene;
			TomCat::Entity entity = scene.CreateEntity("Managed start failure");
			entity.AddComponent<TomCat::BoxCollider2D>();
			TomCat::CSharpScriptEntry entry;
			entry.AttachmentID = TomCat::UUID();
			entry.ScriptAsset = TomCat::AssetHandle(9002);
			entity.AddComponent<TomCat::CSharpScripts>().Scripts.push_back(entry);
			Require(!scene.OnRuntimeStart(),
				"managed startup failure incorrectly entered Play");
			Require(entity.GetComponent<TomCat::BoxCollider2D>().RuntimeFixture == nullptr
				&& !runtime->Active,
				"managed startup failure did not roll back scripts and physics");
			TomCat::Scripting::ScriptEngine::Get().SetRuntime({});
		}

		{
			auto runtime = std::make_shared<ManagedRuntimeProbe>();
			runtime->UnloadSucceeds = false;
			TomCat::Scripting::ScriptEngine::Get().SetRuntime(runtime);
			TomCat::Scene scene;
			TomCat::CSharpScriptEntry entry;
			entry.AttachmentID = TomCat::UUID();
			entry.ScriptAsset = TomCat::AssetHandle(9003);
			scene.CreateEntity("Managed unload failure")
				.AddComponent<TomCat::CSharpScripts>().Scripts.push_back(entry);
			Require(scene.OnRuntimeStart(),
				"managed unload-failure probe could not start");
			scene.OnRuntimeStop();
			Require(runtime->UnloadFailureCount == 1
				&& !runtime->LastUnloadFailure.empty(),
				"ScriptEngine did not issue the terminal unload-failure notification");
			TomCat::Scripting::ScriptEngine::Get().SetRuntime({});
		}
	}


	void TestManagedDispatchInfrastructureFailuresStopRuntime()
	{
		using namespace TomCat::Scripting;
		enum class DispatchKind
		{
			Update,
			FixedUpdate,
			PhysicsEvents
		};

		auto runCase = [](DispatchKind kind, const char* operation)
		{
			auto runtime = std::make_shared<ManagedRuntimeProbe>();
			ScriptRuntimeOverride runtimeOverride(runtime);
			TomCat::Scene scene;
			TomCat::Entity scripted =
				scene.CreateEntity("Managed dispatch failure probe");
			TomCat::CSharpScriptEntry entry;
			entry.AttachmentID = TomCat::UUID();
			entry.ScriptAsset = TomCat::AssetHandle(9199);
			entry.LastKnownClassName = "Game.DispatchFailureProbe";
			scripted.AddComponent<TomCat::CSharpScripts>().Scripts.push_back(entry);
			Require(scene.OnRuntimeStart(),
				"managed dispatch-failure Scene could not start");
			Require(runtime->Attachments.size() == 1,
				"managed dispatch-failure attachment was not instantiated");

			NativePhysicsEventV1 physicsEvent{};
			physicsEvent.Kind =
				static_cast<uint32_t>(NativePhysicsEventKind::CollisionEnter);
			physicsEvent.EntityA = runtime->Attachments.front().Entity;
			physicsEvent.EntityB = runtime->Attachments.front().Entity;
			const std::array<NativePhysicsEventV1, 1> physicsEvents{
				physicsEvent };

			auto dispatch = [&]
			{
				switch (kind)
				{
					case DispatchKind::Update:
						ScriptEngine::Get().UpdateAll(
							runtime->LastSceneSession, 1.0f / 60.0f);
						break;
					case DispatchKind::FixedUpdate:
						ScriptEngine::Get().FixedUpdateAll(
							runtime->LastSceneSession, 1.0f / 60.0f);
						break;
					case DispatchKind::PhysicsEvents:
						ScriptEngine::Get().DispatchPhysicsEvents(
							runtime->LastSceneSession, physicsEvents);
						break;
				}
			};
			auto dispatchCount = [&]() -> uint32_t
			{
				switch (kind)
				{
					case DispatchKind::Update:
						return runtime->UpdateCount;
					case DispatchKind::FixedUpdate:
						return runtime->FixedUpdateCount;
					case DispatchKind::PhysicsEvents:
						return runtime->PhysicsEventCount;
				}
				return 0;
			};
			auto injectInfrastructureFailure = [&]
			{
				switch (kind)
				{
					case DispatchKind::Update:
						runtime->UpdateAllStatus = ScriptStatus::ManagedException;
						break;
					case DispatchKind::FixedUpdate:
						runtime->FixedUpdateAllStatus =
							ScriptStatus::ManagedException;
						break;
					case DispatchKind::PhysicsEvents:
						runtime->DispatchPhysicsEventsStatus =
							ScriptStatus::ManagedException;
						break;
				}
			};

			const auto destroyAllCount = [&]
			{
				return static_cast<uint32_t>(std::count(runtime->Calls.begin(),
					runtime->Calls.end(), "DestroyAll"));
			};

			// ScriptSceneRuntime contains individual user callback exceptions and
			// returns Success for the dispatch. That contract must keep Play alive.
			dispatch();
			const std::string containedFailure =
				std::string(operation)
				+ " stopped Play after a callback-contained exception";
			Require(dispatchCount() == 1 && scene.IsRuntimeRunning()
				&& runtime->Active && destroyAllCount() == 0,
				containedFailure.c_str());

			// A non-Success status escaping the dispatch boundary represents a host
			// or callback-transaction protocol failure and must tear down Play.
			injectInfrastructureFailure();
			dispatch();
			const std::string stopFailure =
				std::string(operation)
				+ " infrastructure failure did not stop Play and DestroyAll once";
			Require(dispatchCount() == 2 && !scene.IsRuntimeRunning()
				&& !runtime->Active && destroyAllCount() == 1
				&& !ScriptEngine::Get().ResolveEntity(
					runtime->Attachments.front().Entity),
				stopFailure.c_str());
		};

		runCase(DispatchKind::Update, "UpdateAll");
		runCase(DispatchKind::FixedUpdate, "FixedUpdateAll");
		runCase(DispatchKind::PhysicsEvents, "DispatchPhysicsEvents");
	}

	void TestDeferredManagedPublicationFailureStopsRuntime()
	{
		using namespace TomCat::Scripting;
		auto runtime = std::make_shared<ManagedRuntimeProbe>();
		runtime->SetEnabledStatus = ScriptStatus::ManagedException;
		ScriptRuntimeOverride runtimeOverride(runtime);
		TomCat::Scene scene;
		TomCat::Entity scripted = scene.CreateEntity("Managed publication failure");
		TomCat::CSharpScriptEntry entry;
		entry.AttachmentID = TomCat::UUID();
		entry.ScriptAsset = TomCat::AssetHandle(9200);
		entry.LastKnownClassName = "Game.PublicationFailureProbe";
		scripted.AddComponent<TomCat::CSharpScripts>().Scripts.push_back(entry);
		Require(scene.OnRuntimeStart(),
			"managed publication-failure scene could not start");
		Require(runtime->Attachments.size() == 1,
			"managed publication-failure attachment was not instantiated");

		const uint64_t attachmentId = runtime->Attachments.front().AttachmentId;
		Require(ScriptEngine::Get().QueueBehaviourEnabled(attachmentId, false),
			"could not queue the managed publication-failure command");
		Require(!ScriptEngine::Get().FlushDeferredCommands(
				runtime->LastSceneSession),
			"managed publication failure was not returned by the legacy flush");

		Require(!scene.IsRuntimeRunning() && !runtime->Active,
			"managed publication failure did not stop the Play runtime");
		Require(runtime->DeferredBatchAbortCount == 1
			&& runtime->DeferredBatchCommitCount == 0
			&& std::find(runtime->Calls.begin(), runtime->Calls.end(),
				"SetEnabled") != runtime->Calls.end()
			&& std::find(runtime->Calls.begin(), runtime->Calls.end(),
				"DestroyAll") != runtime->Calls.end()
			&& std::count(runtime->Calls.begin(), runtime->Calls.end(),
				"DestroyAll") == 1,
			"managed publication failure was silently continued, not aborted, or destroyed twice");
	}

	TomCat::Scripting::NativeDeferredCallbackTransactionsApiV1
		GetDeferredCallbackTransactionsApi()
	{
		using namespace TomCat::Scripting;
		const NativeApiV2 envelope = BuildNativeApiV2();
		NativeDeferredCallbackTransactionsApiV1 api{};
		const std::string capabilityName(
			DeferredCallbackTransactionsCapabilityName);
		const NativeUtf8View capabilityView{
			reinterpret_cast<const uint8_t*>(capabilityName.data()),
			capabilityName.size() };
		uint32_t required = 0;
		Require(envelope.QueryCapability(capabilityView, 1, &api, sizeof(api),
			&required) == static_cast<int32_t>(ScriptStatus::Success)
			&& required == sizeof(api) && api.BeginCallback && api.CompleteCallback,
			"deferred callback transaction capability is unavailable");
		return api;
	}
	enum class DeferredStartupFailureKind
	{
		EmptyAcknowledgement,
		CommitAcknowledgement,
		AbortAcknowledgement,
		LivePublication
	};

	void TestDeferredCallbackStartupProtocolFailures()
	{
		using namespace TomCat::Scripting;
		const NativeDeferredCallbackTransactionsApiV1 callbackTransactions =
			GetDeferredCallbackTransactionsApi();
		auto runCase = [&](DeferredStartupFailureKind kind)
		{
			auto runtime = std::make_shared<ManagedRuntimeProbe>();
			if (kind == DeferredStartupFailureKind::LivePublication)
				runtime->SetEnabledStatus = ScriptStatus::ManagedException;
			else
				runtime->ResolveDeferredBatchStatus = ScriptStatus::ManagedException;
			ScriptRuntimeOverride runtimeOverride(runtime);
			TomCat::Scene scene;
			TomCat::Entity scripted = scene.CreateEntity(
				"Deferred startup protocol failure");
			TomCat::CSharpScriptEntry entry;
			entry.AttachmentID = TomCat::UUID();
			entry.ScriptAsset = TomCat::AssetHandle(9300
				+ static_cast<uint64_t>(kind));
			entry.LastKnownClassName = "Game.StartupProtocolFailureProbe";
			scripted.AddComponent<TomCat::CSharpScripts>().Scripts.push_back(entry);

			EntityHandleV1 callbackContext{};
			bool callbackBegan = false;
			bool mutationRecorded = false;
			bool callbackCompleted = true;
			int32_t callbackCompletionStatus =
				static_cast<int32_t>(ScriptStatus::Success);
			runtime->InvokeCreateAction = [&]
			{
				if (runtime->Attachments.empty())
				{
					runtime->InvokeCreateStatus = ScriptStatus::ManagedException;
					return;
				}
				callbackContext = runtime->Attachments.front().Entity;
				uint64_t token = 0;
				callbackBegan = callbackTransactions.BeginCallback(
					callbackContext, &token)
					== static_cast<int32_t>(ScriptStatus::Success);
				if (!callbackBegan)
				{
					runtime->InvokeCreateStatus = ScriptStatus::ManagedException;
					return;
				}

				mutationRecorded = true;
				switch (kind)
				{
					case DeferredStartupFailureKind::EmptyAcknowledgement:
						break;
					case DeferredStartupFailureKind::CommitAcknowledgement:
						mutationRecorded = ScriptEngine::Get().QueueAddComponent(
							callbackContext, NativeComponentType::BoxCollider2D);
						break;
					case DeferredStartupFailureKind::AbortAcknowledgement:
						mutationRecorded = ScriptEngine::Get()
							.MarkDeferredCommandBatchFailed(callbackContext,
								"startup callback requested rollback");
						break;
					case DeferredStartupFailureKind::LivePublication:
						mutationRecorded = ScriptEngine::Get().QueueBehaviourEnabled(
							runtime->Attachments.front().AttachmentId, false);
						break;
				}
				callbackCompletionStatus = callbackTransactions.CompleteCallback(token);
				callbackCompleted = callbackCompletionStatus
					== static_cast<int32_t>(ScriptStatus::Success);
				if (!callbackCompleted)
					runtime->InvokeCreateStatus = ScriptStatus::ManagedException;
			};

			const bool started = scene.OnRuntimeStart();
			const uint32_t destroyAllCount = static_cast<uint32_t>(std::count(
				runtime->Calls.begin(), runtime->Calls.end(), "DestroyAll"));
			const bool expectedCommitAcknowledgement =
				kind == DeferredStartupFailureKind::EmptyAcknowledgement
				|| kind == DeferredStartupFailureKind::CommitAcknowledgement;
			Require(callbackBegan && mutationRecorded && !callbackCompleted
				&& callbackCompletionStatus
					== static_cast<int32_t>(ScriptStatus::InvalidState),
				"startup CompleteCallback did not return its protocol/publication failure");
			Require(!started && !scene.IsRuntimeRunning() && !runtime->Active,
				"startup callback protocol failure left the Scene or managed runtime active");
			Require(destroyAllCount == 1,
				"startup callback protocol failure did not call DestroyAll exactly once");
			Require(!ScriptEngine::Get().ResolveEntity(callbackContext),
				"startup callback protocol failure left a resolvable ScriptEngine binding");
			Require(expectedCommitAcknowledgement
					? runtime->DeferredBatchCommitCount == 1
						&& runtime->DeferredBatchAbortCount == 0
					: runtime->DeferredBatchAbortCount == 1
						&& runtime->DeferredBatchCommitCount == 0,
				"startup callback resolved the wrong managed projection frame");
			if (kind == DeferredStartupFailureKind::LivePublication)
				Require(std::find(runtime->Calls.begin(), runtime->Calls.end(),
					"SetEnabled") != runtime->Calls.end(),
					"startup live-publication failure fixture did not reach managed publication");
		};

		runCase(DeferredStartupFailureKind::EmptyAcknowledgement);
		runCase(DeferredStartupFailureKind::CommitAcknowledgement);
		runCase(DeferredStartupFailureKind::AbortAcknowledgement);
		runCase(DeferredStartupFailureKind::LivePublication);
	}

	void TestLazyDeferredCallbackStartupProtocolFailure()
	{
		using namespace TomCat::Scripting;
		const NativeDeferredCallbackTransactionsApiV1 callbackTransactions =
			GetDeferredCallbackTransactionsApi();
		auto runtime = std::make_shared<ManagedRuntimeProbe>();
		runtime->ResolveDeferredBatchStatus = ScriptStatus::ManagedException;
		ScriptRuntimeOverride runtimeOverride(runtime);
		TomCat::Scene scene;
		Require(scene.OnRuntimeStart(),
			"script-free Scene could not start for lazy callback failure regression");

		TomCat::Entity scripted = scene.CreateEntity(
			"Lazy deferred callback protocol failure");
		const TomCat::UUID scriptedId = scripted.GetUUID();
		TomCat::CSharpScriptEntry entry;
		entry.AttachmentID = TomCat::UUID();
		entry.ScriptAsset = TomCat::AssetHandle(9309);
		entry.LastKnownClassName = "Game.LazyProtocolFailureProbe";
		scripted.AddComponent<TomCat::CSharpScripts>().Scripts.push_back(entry);

		EntityHandleV1 callbackContext{};
		bool callbackBegan = false;
		bool mutationRecorded = false;
		bool callbackCompleted = true;
		int32_t callbackCompletionStatus =
			static_cast<int32_t>(ScriptStatus::Success);
		runtime->DynamicInstantiateAction = [&]
		{
			if (runtime->DynamicAttachments.empty())
			{
				runtime->DynamicInstantiateStatus = ScriptStatus::ManagedException;
				return;
			}
			callbackContext = runtime->DynamicAttachments.front().Entity;
			uint64_t token = 0;
			callbackBegan = callbackTransactions.BeginCallback(
				callbackContext, &token)
				== static_cast<int32_t>(ScriptStatus::Success);
			mutationRecorded = callbackBegan
				&& ScriptEngine::Get().QueueAddComponent(callbackContext,
					NativeComponentType::BoxCollider2D);
			if (mutationRecorded)
				callbackCompletionStatus =
					callbackTransactions.CompleteCallback(token);
			callbackCompleted = callbackCompletionStatus
				== static_cast<int32_t>(ScriptStatus::Success);
			if (!callbackCompleted)
				runtime->DynamicInstantiateStatus = ScriptStatus::ManagedException;
		};

		scene.QueueRuntimeEntityBatchCreated({ scriptedId });
		scene.FlushPendingRuntimeEntityCreates();
		const uint32_t destroyAllCount = static_cast<uint32_t>(std::count(
			runtime->Calls.begin(), runtime->Calls.end(), "DestroyAll"));
		Require(callbackBegan && mutationRecorded && !callbackCompleted
			&& callbackCompletionStatus
				== static_cast<int32_t>(ScriptStatus::InvalidState),
			"lazy CompleteCallback did not return its resolver failure");
		Require(!scene.IsRuntimeRunning() && !runtime->Active
			&& destroyAllCount == 1,
			"lazy callback protocol failure left a stale Scene/runtime or destroyed twice");
		Require(!scene.FindEntityByUUID(scriptedId)
			&& !ScriptEngine::Get().ResolveEntity(callbackContext),
			"lazy callback protocol failure published a stale session or retained its batch");
	}

	void TestDeferredCallbackNestedFatalPropagation()
	{
		using namespace TomCat::Scripting;
		const NativeDeferredCallbackTransactionsApiV1 callbackTransactions =
			GetDeferredCallbackTransactionsApi();
		auto runtime = std::make_shared<ManagedRuntimeProbe>();
		ScriptRuntimeOverride runtimeOverride(runtime);
		TomCat::Scene scene;
		TomCat::Entity scripted = scene.CreateEntity(
			"Deferred nested fatal propagation");
		TomCat::CSharpScriptEntry entry;
		entry.AttachmentID = TomCat::UUID();
		entry.ScriptAsset = TomCat::AssetHandle(9310);
		entry.LastKnownClassName = "Game.NestedProtocolFailureProbe";
		scripted.AddComponent<TomCat::CSharpScripts>().Scripts.push_back(entry);
		Require(scene.OnRuntimeStart(),
			"nested callback fatal-propagation Scene could not start");
		Require(runtime->Attachments.size() == 1,
			"nested callback fatal-propagation attachment was not instantiated");
		const EntityHandleV1 context = runtime->Attachments.front().Entity;

		uint32_t resolverCalls = 0;
		bool nestedBegan = false;
		bool nestedMutationRecorded = false;
		bool nestedCompletionAccepted = false;
		bool outerBegan = false;
		bool outerMutationRecorded = false;
		bool outerCompletion = true;
		int32_t outerCompletionStatus =
			static_cast<int32_t>(ScriptStatus::Success);
		runtime->ResolveDeferredBatchAction = [&](bool committed)
		{
			++resolverCalls;
			if (resolverCalls == 1 && committed)
			{
				uint64_t nestedToken = 0;
				nestedBegan = callbackTransactions.BeginCallback(context,
					&nestedToken) == static_cast<int32_t>(ScriptStatus::Success);
				nestedMutationRecorded = nestedBegan
					&& ScriptEngine::Get().QueueAddComponent(context,
						NativeComponentType::CircleCollider2D);
				nestedCompletionAccepted = nestedMutationRecorded
					&& callbackTransactions.CompleteCallback(nestedToken)
						== static_cast<int32_t>(ScriptStatus::Success);
			}
			else if (resolverCalls == 2)
				runtime->ResolveDeferredBatchStatus = ScriptStatus::ManagedException;
		};
		runtime->FixedUpdateAction = [&]
		{
			uint64_t outerToken = 0;
			outerBegan = callbackTransactions.BeginCallback(context, &outerToken)
				== static_cast<int32_t>(ScriptStatus::Success);
			outerMutationRecorded = outerBegan
				&& ScriptEngine::Get().QueueAddComponent(context,
					NativeComponentType::BoxCollider2D);
			if (outerMutationRecorded)
				outerCompletionStatus =
					callbackTransactions.CompleteCallback(outerToken);
			outerCompletion = outerCompletionStatus
				== static_cast<int32_t>(ScriptStatus::Success);
			if (!outerCompletion)
				runtime->FixedUpdateAllStatus = ScriptStatus::ManagedException;
		};

		scene.OnRuntimeStep();
		const uint32_t destroyAllCount = static_cast<uint32_t>(std::count(
			runtime->Calls.begin(), runtime->Calls.end(), "DestroyAll"));
		Require(outerBegan && outerMutationRecorded && nestedBegan
			&& nestedMutationRecorded && nestedCompletionAccepted,
			"reentrant callback was not sealed non-recursively during the outer drain");
		Require(!outerCompletion
			&& outerCompletionStatus
				== static_cast<int32_t>(ScriptStatus::InvalidState)
			&& resolverCalls == 2
			&& runtime->DeferredBatchCommitCount == 2,
			"nested fatal result did not propagate through the FIFO drain to outer CompleteCallback");
		Require(!scene.IsRuntimeRunning() && !runtime->Active
			&& destroyAllCount == 1
			&& !ScriptEngine::Get().ResolveEntity(context),
			"nested callback fatal result did not stop Play exactly once and invalidate its binding");
		ScriptEngine::Get().StopScene(context.SceneSessionId);
		Require(std::count(runtime->Calls.begin(), runtime->Calls.end(),
			"DestroyAll") == 1,
			"repeated stop after deferred protocol failure destroyed the runtime twice");
	}
}

int main(int argc, char** argv)
{
	TomCat::Log::Init();
	int failures = 0;
	const std::string_view filter = argc > 1 && argv[1] ? argv[1] : "";
	auto run = [&](const char* name, auto&& test)
	{
		if (!filter.empty() && std::string_view(name).find(filter) == std::string_view::npos)
			return;
		std::cout << "RUN  " << name << std::endl;
		try
		{
			test();
			std::cout << "PASS " << name << std::endl;
		}
		catch (const std::exception& exception)
		{
			++failures;
			std::cerr << "FAIL " << name << ": " << exception.what() << std::endl;
		}
	};

	run("project settings persistence and validation", TestProjectSettingsPersistenceAndValidation);
	run("PlayerSettings strict schema, roundtrip, and migration",
		TestPlayerSettingsPersistenceAndValidation);
	run("legacy project BuildSettings migration", TestLegacyProjectBuildSettingsMigration);
	run("explicit Player dotnet root is exclusive", TestExplicitDotNetRootIsExclusive);
	run("fixed accumulator and exact Step", TestFixedAccumulatorAndStep);
	run("managed Transform world setters preserve hierarchy",
		TestManagedTransformSettersRespectHierarchy);
	run("30/60/144Hz one- and ten-second consistency", TestFrameRateIndependentPhysics);
	run("incremental physics synchronization preserves object identity",
		TestIncrementalPhysicsSynchronizationPreservesObjectIdentity);
	run("physics synchronization safe-stage coalescing and targeted mutation statistics",
		TestPhysicsSynchronizationStageCoalescing);
	run("fixed-step physics render interpolation", TestPhysicsRenderInterpolation);
	run("incremental fixture contact continuity",
		TestIncrementalFixtureContactContinuity);
	run("Pause render path and exact single-step", TestPauseRenderPathAndSingleStep);
	run("collider-only static runtime body", TestColliderOnlyStaticBody);
	run("CircleCollider2D non-uniform fixture", TestCircleNonUniformScaleFixture);
	run("authoring/runtime collider outline parity", TestAuthoringAndRuntimeOutlinesMatch);
	run("Trigger events and managed callbacks", TestTriggerAndManagedCallbacks);
	run("layer/mask filtering and runtime rebuild", TestCollisionFilteringAndRuntimeRebuild);
	run("inactive hierarchy preserves dynamic body state and clears stale cache",
		TestInactiveHierarchyPreservesRuntimeBodyState);
	run("inactive runtime sprite components wait for reactivation",
		TestInactiveRuntimeSpriteComponentsStayUninitialized);
	run("audio autoplay waits for managed OnCreate",
		TestAudioAutoPlayWaitsForManagedCreate);
	run("single runtime step short-circuits after fixed stop",
		TestRuntimeStepShortCircuitsAfterFixedStop);
	run("project matrix and fixture filters are independent", TestProjectMatrixAndFixtureFilters);
	run("entity-layer raycast/AABB query and motion APIs", TestQueriesAndMotionAPI);
	run("DistanceJoint2D runtime creation and rebuild", TestDistanceJoint);
	run("schema v11 scripts/metadata save/load/copy/duplicate and v9/v10 migration",
		TestSchemaV11PersistenceAndCopies);
	run("component registry Health vertical slice and opaque roundtrip",
		ComponentRegistryRegression::Run);
	run("SceneCommandBuffer create/destroy/reparent transaction",
		SceneCommandBufferRegression::Run);
	run("Prefab LocalID transaction, remap, and runtime safe point",
		PrefabRegression::Run);
	run("typed asset reference graph and Cook validation",
		TestTypedAssetReferenceGraphAndCookValidation);
	run("Cooked Player v5 physics and build-scene roundtrip", TestCookedPlayerPhysicsRoundtrip);
	run("script-free tcpak v5 needs no managed payload", TestScriptFreeCookWithoutManagedPayload);
	run("Font, Text and Runtime UI P0", TomCat::Tests::RunRuntimeUIRegression);
	run("logical window/framebuffer/DPI coordinate contract",
		TomCat::Tests::RunWindowMetricsRegression);
	run("collision Enter/Exit entity-pair de-duplication", TestCollisionPairDeduplication);
	run("collision callback deletion safety", TestDeletionDuringCollisionDispatch);
	run("managed collision mutation safety", TestManagedMutationDuringCollisionDispatch);
	run("Prefab dynamic managed attachment ABI and rollback",
		TestPrefabDynamicAttachmentBridge);
	run("initial OnCreate Prefab physics before dynamic lifecycle",
		TestInitialOnCreatePrefabPhysicsOrdering);
	run("component registry schema discovery ABI",
		TestComponentSchemaCapability);
	run("deferred command abort capability ABI and transaction rollback",
		TestDeferredCommandsCapability);
	run("third-party component transactional validation phase contract",
		TestThirdPartyTransactionalComponentContract);
	run("third-party component numeric and UTF-8 C# property ABI",
		TestRegisteredComponentStringCapability);
	run("reserved C# entity chained initialization capability",
		TestReservedEntityChainedInitializationCapability);
	run("projected transaction read-your-writes and atomic rollback",
		TestProjectedTransactionReadYourWritesAndRollback);
	run("registered Add projection matches descriptor side effects",
		TestRegisteredAddProjectionMatchesDescriptorSemantics);
	run("deferred disable precedes subtree attachment destruction",
		TestDeferredDisableBeforeSubtreeDestroyOrdering);
	run("deferred last behaviour removal precedes owner destruction",
		TestDeferredRemoveLastBehaviourBeforeOwnerDestroyOrdering);
	run("projected duplicate and subtree entity destruction",
		TestProjectedDestroyBatchSemantics);
	run("managed lifecycle backend, timing, rollback, and unload failure",
		TestManagedScriptLifecycleBackend);
	run("managed dispatch infrastructure failures stop runtime",
		TestManagedDispatchInfrastructureFailuresStopRuntime);
	run("startup deferred callback protocol failures stop runtime",
		TestDeferredCallbackStartupProtocolFailures);
	run("lazy startup deferred callback failure does not publish stale session",
		TestLazyDeferredCallbackStartupProtocolFailure);
	run("nested deferred callback fatal propagates to outer Complete",
		TestDeferredCallbackNestedFatalPropagation);
	run("deferred managed publication failure stops runtime",
		TestDeferredManagedPublicationFailureStopsRuntime);
	run("single SceneManager deferred transition, reload, rollback, and generation",
		SceneManagerRegression::Run);
	return failures == 0 ? 0 : 1;
}
