#pragma once

#include <filesystem>
#include <string>

namespace TomCat {

	// Process-wide CoreCLR bootstrap. CoreCLR is initialized at most once; project
	// reloads are handled by collectible AssemblyLoadContexts in ScriptHost.
	class DotNetHost
	{
	public:
		struct Configuration
		{
			std::filesystem::path RuntimeConfigPath;
			std::filesystem::path ScriptHostAssemblyPath;
			// Player builds pass their private "dotnet" directory explicitly. An
			// empty path falls back to DOTNET_ROOT and then the machine installation.
			std::filesystem::path DotNetRoot;
		};

		DotNetHost() = default;
		~DotNetHost();

		DotNetHost(const DotNetHost&) = delete;
		DotNetHost& operator=(const DotNetHost&) = delete;

		bool Initialize(const Configuration& configuration);
		void* GetUnmanagedFunction(const wchar_t* assemblyQualifiedType,
			const wchar_t* methodName);

		bool IsInitialized() const { return m_LoadAssemblyAndGetFunctionPointer != nullptr; }
		const std::string& GetLastError() const { return m_LastError; }
		const std::filesystem::path& GetDotNetRoot() const { return m_DotNetRoot; }

	private:
		bool ResolveDotNetRoot(const std::filesystem::path& requested,
			std::filesystem::path& resolved);
		bool ResolveHostFxr(const std::filesystem::path& dotnetRoot,
			std::filesystem::path& hostFxrPath);
		void SetError(std::string message);

	private:
		void* m_HostFxrLibrary = nullptr;
		void* m_LoadAssemblyAndGetFunctionPointer = nullptr;
		std::filesystem::path m_DotNetRoot;
		std::filesystem::path m_ScriptHostAssemblyPath;
		std::string m_LastError;
	};

}
