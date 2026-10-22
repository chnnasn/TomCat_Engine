#include "tcpch.h"
#include "DotNetHost.h"

#include "TomCat/Core/Log.h"
#include "TomCat/Core/ApplicationPaths.h"
#include "TomCat/Utils/PathUtils.h"

#include <algorithm>
#include <array>
#include <charconv>
#include <cstdlib>
#include <mutex>
#include <sstream>
#include <system_error>
#include <vector>

#ifdef TC_PLATFORM_WINDOWS
	#include <Windows.h>
#elif defined(TC_PLATFORM_LINUX)
	#include <dlfcn.h>
#endif

namespace TomCat {

	namespace {
		struct ProcessHostState
		{
			void* HostFxrLibrary = nullptr;
			void* LoadAssemblyAndGetFunctionPointer = nullptr;
			std::filesystem::path DotNetRoot;
			std::filesystem::path RuntimeConfigPath;
			std::filesystem::path ScriptHostAssemblyPath;
		};

		ProcessHostState& GetProcessHostState()
		{
			static ProcessHostState state;
			return state;
		}

		std::mutex& GetProcessHostMutex()
		{
			static std::mutex mutex;
			return mutex;
		}

		bool PathsReferToSameFile(const std::filesystem::path& left,
			const std::filesystem::path& right)
		{
			std::error_code error;
			const bool equivalent = std::filesystem::equivalent(left, right, error);
			return !error && equivalent;
		}

#if defined(TC_PLATFORM_WINDOWS) || defined(TC_PLATFORM_LINUX)
#ifdef TC_PLATFORM_WINDOWS
        using HostChar = wchar_t;
        #define TC_HOST_CALL __cdecl
        #define TC_DELEGATE_CALL __stdcall
        auto OpenHostLibrary(const std::filesystem::path& path) { return LoadLibraryW(path.c_str()); }
        auto HostSymbol(HMODULE library, const char* name) { return GetProcAddress(library, name); }
        void CloseHostLibrary(HMODULE library) { FreeLibrary(library); }
#else
        using HostChar = char;
        #define TC_HOST_CALL
        #define TC_DELEGATE_CALL
        void* OpenHostLibrary(const std::filesystem::path& path) { return dlopen(path.c_str(), RTLD_NOW | RTLD_LOCAL); }
        void* HostSymbol(void* library, const char* name) { return dlsym(library, name); }
        void CloseHostLibrary(void* library) { dlclose(library); }
#endif
        using hostfxr_handle = void*;
		using hostfxr_initialize_for_runtime_config_fn = int32_t(TC_HOST_CALL*)(
			const HostChar*, const void*, hostfxr_handle*);
		using hostfxr_get_runtime_delegate_fn = int32_t(TC_HOST_CALL*)(
			hostfxr_handle, int32_t, void**);
		using hostfxr_close_fn = int32_t(TC_HOST_CALL*)(hostfxr_handle);
		using hostfxr_error_writer_fn = void(TC_HOST_CALL*)(const HostChar*);
		using hostfxr_set_error_writer_fn = hostfxr_error_writer_fn (TC_HOST_CALL*)(
			hostfxr_error_writer_fn);
		using load_assembly_and_get_function_pointer_fn = int32_t(TC_DELEGATE_CALL*)(
			const HostChar*, const HostChar*, const HostChar*, const HostChar*, void*, void**);

		constexpr int32_t LoadAssemblyAndGetFunctionPointerDelegate = 5;
		const HostChar* const UnmanagedCallersOnlyMethod =
			reinterpret_cast<const HostChar*>(static_cast<intptr_t>(-1));

		void TC_HOST_CALL WriteHostFxrError(const HostChar* message)
		{
			if (!message)
				return;
			try
			{
				TC_Core_Error("CoreCLR host: {0}", PathToUTF8(std::filesystem::path(message)));
			}
			catch (...)
			{
				// hostfxr error writers must never allow an exception to cross the C ABI.
			}
		}
#endif

		std::vector<uint32_t> ParseVersion(const std::filesystem::path& path)
		{
			const std::string value = PathToUTF8(path.filename());
			std::vector<uint32_t> result;
			size_t cursor = 0;
			while (cursor < value.size())
			{
				const size_t end = value.find('.', cursor);
				const size_t count = (end == std::string::npos ? value.size() : end) - cursor;
				uint32_t number = 0;
				const char* begin = value.data() + cursor;
				const char* finish = begin + count;
				auto parsed = std::from_chars(begin, finish, number);
				if (parsed.ec != std::errc{} || parsed.ptr != finish)
					return {};
				result.push_back(number);
				if (end == std::string::npos)
					break;
				cursor = end + 1;
			}
			return result;
		}

		bool VersionLess(const std::filesystem::path& left,
			const std::filesystem::path& right)
		{
			auto leftVersion = ParseVersion(left);
			auto rightVersion = ParseVersion(right);
			const size_t count = std::max(leftVersion.size(), rightVersion.size());
			leftVersion.resize(count);
			rightVersion.resize(count);
			return std::lexicographical_compare(leftVersion.begin(), leftVersion.end(),
				rightVersion.begin(), rightVersion.end());
		}

	}

	DotNetHost::~DotNetHost()
	{
		// CoreCLR cannot be unloaded from the process. Keep hostfxr loaded as well;
		// its delegates remain process-lifetime infrastructure used by ScriptHost.
	}

	void DotNetHost::SetError(std::string message)
	{
		m_LastError = std::move(message);
		TC_Core_Error("{0}", m_LastError);
	}

	bool DotNetHost::ResolveDotNetRoot(const std::filesystem::path& requested,
		std::filesystem::path& resolved)
	{
		std::vector<std::filesystem::path> candidates;
		if (!requested.empty())
		{
			candidates.push_back(requested);
		}
		else
		{

#ifdef TC_PLATFORM_WINDOWS
			std::array<wchar_t, 32768> buffer{};
			if (const DWORD length = GetEnvironmentVariableW(L"DOTNET_ROOT", buffer.data(),
				static_cast<DWORD>(buffer.size())); length > 0 && length < buffer.size())
				candidates.emplace_back(std::wstring(buffer.data(), length));
			if (const DWORD length = GetEnvironmentVariableW(L"ProgramFiles", buffer.data(),
				static_cast<DWORD>(buffer.size())); length > 0 && length < buffer.size())
				candidates.emplace_back(std::filesystem::path(
					std::wstring(buffer.data(), length)) / L"dotnet");
#elif defined(TC_PLATFORM_LINUX)
            for (const char* name : {"DOTNET_ROOT_X64", "DOTNET_ROOT"})
                if (const char* value = std::getenv(name); value && *value)
                    candidates.emplace_back(value);
            if (const char* path = std::getenv("PATH"))
            {
                std::istringstream entries(path);
                std::string entry;
                while (std::getline(entries, entry, ':'))
                {
                    if (entry.empty()) continue;
                    std::error_code error;
                    auto executable = std::filesystem::canonical(std::filesystem::path(entry) / "dotnet", error);
                    if (!error) candidates.push_back(executable.parent_path());
                }
            }
            candidates.emplace_back("/usr/share/dotnet");
            candidates.emplace_back("/usr/lib/dotnet");
            if (const char* home = std::getenv("HOME"))
                candidates.emplace_back(std::filesystem::path(home) / ".dotnet");
#endif
		}

		for (const auto& candidate : candidates)
		{
			std::error_code error;
			const std::filesystem::path absolute = std::filesystem::absolute(candidate, error);
			if (!error && std::filesystem::is_directory(absolute / "host" / "fxr", error) && !error)
			{
				resolved = absolute.lexically_normal();
				return true;
			}
		}
		if (!requested.empty())
			SetError("The explicitly selected private dotnet root is unavailable: "
				+ PathToUTF8(requested));
		else
			SetError("Could not locate a .NET installation. Install .NET 10 or provide a private dotnet root.");
		return false;
	}

	bool DotNetHost::ResolveHostFxr(const std::filesystem::path& dotnetRoot,
		std::filesystem::path& hostFxrPath)
	{
#if defined(TC_PLATFORM_WINDOWS) || defined(TC_PLATFORM_LINUX)
		const std::filesystem::path fxrRoot = dotnetRoot / "host" / "fxr";
		std::vector<std::filesystem::path> versions;
		std::error_code error;
		for (std::filesystem::directory_iterator iterator(fxrRoot, error), end;
			!error && iterator != end; iterator.increment(error))
		{
			if (iterator->is_directory(error) && !error && !ParseVersion(iterator->path()).empty())
				versions.push_back(iterator->path());
		}
		if (error || versions.empty())
		{
			SetError("The selected dotnet root has no host/fxr runtime: " + PathToUTF8(dotnetRoot));
			return false;
		}
		std::sort(versions.begin(), versions.end(), VersionLess);
		for (auto iterator = versions.rbegin(); iterator != versions.rend(); ++iterator)
		{
			#ifdef TC_PLATFORM_WINDOWS
            const std::filesystem::path candidate = *iterator / "hostfxr.dll";
#else
            const std::filesystem::path candidate = *iterator / "libhostfxr.so";
#endif
			if (std::filesystem::is_regular_file(candidate, error) && !error)
			{
				hostFxrPath = candidate;
				return true;
			}
			error.clear();
		}
		SetError("The selected dotnet root has no usable hostfxr library: " + PathToUTF8(dotnetRoot));
		return false;
#else
		(void)dotnetRoot;
		(void)hostFxrPath;
		SetError("CoreCLR hosting is currently available on Windows and Linux");
		return false;
#endif
	}

	bool DotNetHost::Initialize(const Configuration& configuration)
	{
		std::lock_guard<std::mutex> lock(GetProcessHostMutex());
		if (IsInitialized())
			return true;
		m_LastError.clear();

#if defined(TC_PLATFORM_WINDOWS) || defined(TC_PLATFORM_LINUX)
		std::error_code error;
		const std::filesystem::path runtimeConfigPath = std::filesystem::absolute(
			configuration.RuntimeConfigPath, error).lexically_normal();
		if (configuration.RuntimeConfigPath.empty() || error
			|| !std::filesystem::is_regular_file(runtimeConfigPath, error) || error)
		{
			SetError("TomCat.ScriptHost runtimeconfig is missing: "
				+ PathToUTF8(configuration.RuntimeConfigPath));
			return false;
		}
		error.clear();
		const std::filesystem::path scriptHostAssemblyPath = std::filesystem::absolute(
			configuration.ScriptHostAssemblyPath, error).lexically_normal();
		if (configuration.ScriptHostAssemblyPath.empty() || error
			|| !std::filesystem::is_regular_file(scriptHostAssemblyPath, error) || error)
		{
			SetError("TomCat.ScriptHost assembly is missing: "
				+ PathToUTF8(configuration.ScriptHostAssemblyPath));
			return false;
		}

		ProcessHostState& process = GetProcessHostState();
		if (process.LoadAssemblyAndGetFunctionPointer)
		{
			if (!PathsReferToSameFile(runtimeConfigPath, process.RuntimeConfigPath)
				|| !PathsReferToSameFile(scriptHostAssemblyPath,
					process.ScriptHostAssemblyPath))
			{
				SetError("CoreCLR is already initialized with a different TomCat.ScriptHost. Restart the process before changing managed host files.");
				return false;
			}
			if (!configuration.DotNetRoot.empty())
			{
				std::filesystem::path requestedRoot;
				if (!ResolveDotNetRoot(configuration.DotNetRoot, requestedRoot)
					|| !PathsReferToSameFile(requestedRoot, process.DotNetRoot))
				{
					SetError("CoreCLR is already initialized from a different dotnet root");
					return false;
				}
			}
			m_HostFxrLibrary = process.HostFxrLibrary;
			m_LoadAssemblyAndGetFunctionPointer =
				process.LoadAssemblyAndGetFunctionPointer;
			m_DotNetRoot = process.DotNetRoot;
			m_ScriptHostAssemblyPath = process.ScriptHostAssemblyPath;
			return true;
		}

		std::filesystem::path dotnetRoot;
		if (!ResolveDotNetRoot(configuration.DotNetRoot, dotnetRoot))
			return false;
		std::filesystem::path hostFxrPath;
		if (!ResolveHostFxr(dotnetRoot, hostFxrPath))
			return false;

		auto library = OpenHostLibrary(hostFxrPath);
		if (!library)
		{
			SetError("Could not load hostfxr from " + PathToUTF8(hostFxrPath));
			return false;
		}
		auto initialize = reinterpret_cast<hostfxr_initialize_for_runtime_config_fn>(
			HostSymbol(library, "hostfxr_initialize_for_runtime_config"));
		auto getDelegate = reinterpret_cast<hostfxr_get_runtime_delegate_fn>(
			HostSymbol(library, "hostfxr_get_runtime_delegate"));
		auto close = reinterpret_cast<hostfxr_close_fn>(
			HostSymbol(library, "hostfxr_close"));
		auto setErrorWriter = reinterpret_cast<hostfxr_set_error_writer_fn>(
			HostSymbol(library, "hostfxr_set_error_writer"));
		if (!initialize || !getDelegate || !close)
		{
			CloseHostLibrary(library);
			SetError("hostfxr does not expose the required .NET hosting functions");
			return false;
		}
		if (setErrorWriter)
			setErrorWriter(&WriteHostFxrError);

		struct InitializeParameters
        {
            size_t Size;
            const HostChar* HostPath;
            const HostChar* DotNetRoot;
        };
        const auto executablePath = ApplicationPaths::GetExecutablePath().value_or(std::filesystem::path{}).native();
        const auto dotnetRootString = dotnetRoot.native();
        const InitializeParameters parameters{sizeof(InitializeParameters),
            executablePath.empty() ? nullptr : executablePath.c_str(), dotnetRootString.c_str()};
        hostfxr_handle context = nullptr;
        const auto runtimeConfig = runtimeConfigPath.native();
		const int32_t initializeResult = initialize(runtimeConfig.c_str(), &parameters, &context);
		if (initializeResult < 0 || !context)
		{
			CloseHostLibrary(library);
			SetError("hostfxr failed to initialize TomCat.ScriptHost (status "
				+ std::to_string(initializeResult) + ")");
			return false;
		}

		void* loadAssembly = nullptr;
		const int32_t delegateResult = getDelegate(context,
			LoadAssemblyAndGetFunctionPointerDelegate, &loadAssembly);
		close(context);
		if (delegateResult < 0 || !loadAssembly)
		{
			CloseHostLibrary(library);
			SetError("hostfxr could not provide load_assembly_and_get_function_pointer (status "
				+ std::to_string(delegateResult) + ")");
			return false;
		}

		m_HostFxrLibrary = library;
		m_LoadAssemblyAndGetFunctionPointer = loadAssembly;
		m_DotNetRoot = std::move(dotnetRoot);
		m_ScriptHostAssemblyPath = scriptHostAssemblyPath;
		process.HostFxrLibrary = m_HostFxrLibrary;
		process.LoadAssemblyAndGetFunctionPointer =
			m_LoadAssemblyAndGetFunctionPointer;
		process.DotNetRoot = m_DotNetRoot;
		process.RuntimeConfigPath = runtimeConfigPath;
		process.ScriptHostAssemblyPath = m_ScriptHostAssemblyPath;
		TC_Core_Info("Initialized .NET 10 CoreCLR from {0}", PathToUTF8(m_DotNetRoot));
		return true;
#else
		(void)configuration;
		SetError("CoreCLR hosting is currently available on Windows and Linux");
		return false;
#endif
	}

	void* DotNetHost::GetUnmanagedFunction(const char* assemblyQualifiedType,
		const char* methodName)
	{
		std::lock_guard<std::mutex> lock(GetProcessHostMutex());
		if (!IsInitialized() || !assemblyQualifiedType || !methodName)
		{
			SetError("CoreCLR must be initialized before resolving a managed entry point");
			return nullptr;
		}

#if defined(TC_PLATFORM_WINDOWS) || defined(TC_PLATFORM_LINUX)
		auto loadAssembly = reinterpret_cast<load_assembly_and_get_function_pointer_fn>(
			m_LoadAssemblyAndGetFunctionPointer);
		void* function = nullptr;
		const auto assemblyPath = m_ScriptHostAssemblyPath.native();
        const auto type = UTF8ToPath(assemblyQualifiedType).native();
        const auto method = UTF8ToPath(methodName).native();
		const int32_t result = loadAssembly(assemblyPath.c_str(), type.c_str(),
			method.c_str(), UnmanagedCallersOnlyMethod, nullptr, &function);
		if (result != 0 || !function)
		{
			SetError("Could not resolve the managed entry point (status "
				+ std::to_string(result) + ")");
			return nullptr;
		}
		return function;
#else
		return nullptr;
#endif
	}

}
