#pragma once

#include "TomCat/Core/Base.h"

#include <atomic>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <utility>
#include <vector>

namespace TomCat {

	class Project;

	enum class ScriptBuildState : uint8_t
	{
		Unconfigured = 0,
		Dirty,
		Building,
		Succeeded,
		Failed
	};

	struct ScriptCompilerDiagnostic
	{
		enum class Severity : uint8_t
		{
			Info = 0,
			Warning,
			Error
		};

		Severity Level = Severity::Info;
		std::string Code;
		std::string Message;
		std::filesystem::path File;
		uint32_t Line = 0;
		uint32_t Column = 0;
	};

	struct ScriptBuildResult
	{
		bool Succeeded = false;
		bool SourceChangedDuringBuild = false;
		int ExitCode = -1;
		std::string SourceHash;
		std::string BuildID;
		std::filesystem::path AssemblyPath;
		std::filesystem::path PdbPath;
		std::vector<ScriptCompilerDiagnostic> Diagnostics;
	};

	class ScriptProjectCompiler
	{
	public:
		using DiagnosticCallback = std::function<void(const ScriptCompilerDiagnostic&)>;

		void SetDiagnosticCallback(DiagnosticCallback callback)
		{
			m_DiagnosticCallback = std::move(callback);
		}

		// Explicit references are primarily for a packaged Editor. When omitted,
		// development builds discover either Managed/*.dll beside the executable or
		// the Managed project files in a repository ancestor.
		bool Configure(const Ref<Project>& project,
			std::filesystem::path managedApiReference = {},
			std::filesystem::path generatorReference = {});
		void Reset();

		bool RefreshSourceState();
		ScriptBuildResult CompileNow();
		bool EnsureCurrentBuild();
		// Starts an isolated worker build from a main-thread source snapshot. The
		// completed candidate is only promoted by PollCompile on the configuring
		// thread, after checking that the project generation and source hash still
		// match.
		bool StartCompile(bool force = false);
		bool PollCompile(ScriptBuildResult& result);
		bool IsCompileInProgress() const { return m_AsyncJob != nullptr; }

		ScriptBuildState GetState() const { return m_State; }
		bool IsCurrentSourceBuilt() const;
		const std::string& GetCurrentSourceHash() const { return m_CurrentSourceHash; }
		const std::string& GetLastGoodSourceHash() const { return m_LastGoodSourceHash; }
		const std::filesystem::path& GetLastGoodAssemblyPath() const
		{
			return m_LastGoodAssemblyPath;
		}
		const std::string& GetLastGoodBuildID() const { return m_LastGoodBuildID; }
		std::filesystem::path GetManagedRuntimeDirectory() const;

	private:
		struct ScriptSource
		{
			uint64_t Handle = 0;
			std::filesystem::path AbsolutePath;
			std::filesystem::path ProjectRelativePath;
		};

		struct AsyncCompileJob
		{
			uint64_t ConfigurationGeneration = 0;
			std::atomic<bool> Complete = false;
			std::mutex ResultMutex;
			ScriptBuildResult Result;
			std::vector<ScriptSource> Sources;
		};

		bool EnumerateSources(std::vector<ScriptSource>& sources) const;
		std::string ComputeSourceHash(const std::vector<ScriptSource>& sources) const;
		bool ResolveManagedReferences(bool bootstrapIfMissing = false,
			std::vector<ScriptCompilerDiagnostic>* diagnostics = nullptr);
		ScriptBuildResult CompileCandidate(std::vector<ScriptSource> sources,
			std::string sourceHash, std::string buildID);
		bool FinalizeCandidate(ScriptBuildResult& result,
			const std::vector<ScriptSource>& sources);
		bool WriteGeneratedProject(const std::vector<ScriptSource>& sources,
			const std::string& buildID, const std::filesystem::path& buildDirectory,
			std::string& errorMessage) const;
		bool WriteScriptAssetMap(const std::vector<ScriptSource>& sources,
			std::string& errorMessage) const;
		bool LoadLastGood();
		bool StoreLastGood(const ScriptBuildResult& result, std::string& errorMessage);
		void Emit(const ScriptCompilerDiagnostic& diagnostic) const;

	private:
		Ref<Project> m_Project;
		std::filesystem::path m_ScriptProjectDirectory;
		std::filesystem::path m_AssembliesDirectory;
		std::filesystem::path m_ManagedApiReference;
		std::filesystem::path m_GeneratorReference;
		std::filesystem::path m_ManagedSolution;
		std::filesystem::path m_ManagedRuntimeDirectory;
		bool m_ManagedApiIsProject = false;
		bool m_GeneratorIsProject = false;
		ScriptBuildState m_State = ScriptBuildState::Unconfigured;
		std::string m_CurrentSourceHash;
		std::string m_LastGoodSourceHash;
		std::string m_LastGoodBuildID;
		std::filesystem::path m_LastGoodAssemblyPath;
		DiagnosticCallback m_DiagnosticCallback;
		std::shared_ptr<AsyncCompileJob> m_AsyncJob;
		uint64_t m_ConfigurationGeneration = 0;
	};

}
