#pragma once

#include "DotNetHost.h"
#include "IScriptRuntime.h"

#include <filesystem>
#include <string>
#include <vector>

namespace TomCat::Scripting {

	class ManagedScriptRuntime final : public IScriptRuntime
	{
	public:
		ManagedScriptRuntime() = default;
		~ManagedScriptRuntime() override;

		bool Initialize(const DotNetHost::Configuration& hostConfiguration,
			const NativeApiV1& nativeApi);
		bool SetProjectAssembly(const std::filesystem::path& assemblyPath,
			const std::filesystem::path& pdbPath = {});
		bool ReadProjectMetadata(std::string& manifestJson) override;
		bool PollUnload() override;
		void OnUnloadFailed(std::string_view reason) override;

		bool IsReady() const override;
		ScriptStatus CreateSceneRuntime(uint64_t sceneSessionId,
			uint64_t runtimeGeneration) override;
		ScriptStatus InstantiateAll(
			std::span<const NativeScriptAttachmentV1> attachments) override;
		ScriptStatus ApplySerializedFields(std::string_view fieldsJson) override;
		ScriptStatus InvokeCreateAll() override;
		ScriptStatus SetEnabled(uint64_t attachmentId, bool enabled) override;
		ScriptStatus UpdateAll(float deltaTime) override;
		ScriptStatus FixedUpdateAll(float fixedDeltaTime) override;
		ScriptStatus DispatchPhysicsEvents(
			std::span<const NativePhysicsEventV1> events) override;
		ScriptStatus DestroyAll() override;
		ScriptStatus DestroyAttachments(std::span<const uint64_t> attachmentIds) override;
		ScriptStatus InstantiateAttachments(
			std::span<const NativeScriptAttachmentV1> attachments,
			std::string_view fieldsJson) override;

		const std::string& GetLastError() const { return m_LastError; }
		uint64_t GetDomainId() const { return m_DomainId; }
		uint64_t GetSceneRuntimeId() const { return m_SceneRuntimeId; }

	private:
		bool ReadBytes(const std::filesystem::path& path, bool optional,
			std::vector<uint8_t>& output);
		ScriptStatus ConvertStatus(int32_t status, const char* operation);
		ScriptStatus EnsureScene(const char* operation) const;
		ScriptStatus BeginPlayDomain();
		bool BeginAndPollMetadataUnload(uint64_t domainId);

	private:
		DotNetHost m_Host;
		NativeApiV1 m_NativeApi{};
		ManagedApiV1 m_ManagedApi{};
		std::vector<uint8_t> m_ProjectAssembly;
		std::vector<uint8_t> m_ProjectPdb;
		std::filesystem::path m_ProjectAssemblyPath;
		uint64_t m_DomainId = 0;
		uint64_t m_SceneRuntimeId = 0;
		bool m_UnloadPending = false;
		std::string m_LastError;
	};

}
