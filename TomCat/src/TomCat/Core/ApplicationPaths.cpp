#include "tcpch.h"
#include "ApplicationPaths.h"

#include "TomCat/Utils/PathUtils.h"

#include <algorithm>
#include <cctype>
#include <cwctype>
#include <mutex>
#include <vector>

#ifdef TC_PLATFORM_WINDOWS
	#include <Windows.h>
	#include <shlobj.h>
	#pragma comment(lib, "Shell32.lib")
#endif

namespace TomCat {
	namespace {

		std::mutex s_RuntimeGameDataMutex;
		std::optional<GameDataPaths> s_RuntimeGameDataPaths;
		std::mutex s_RuntimeEditorRootMutex;
		std::optional<std::filesystem::path> s_RuntimeEditorRoot;
		std::mutex s_RuntimePackageRootMutex;
		std::optional<std::filesystem::path> s_RuntimePackageRoot;

		bool IsSafeIdentitySegment(std::string_view value)
		{
			if (value.empty())
				return false;
			const std::filesystem::path path = UTF8ToPath(value);
			if (path.empty() || path.has_root_name() || path.has_root_directory()
				|| path.filename() != path || path == "." || path == "..")
				return false;
			if (value.back() == ' ' || value.back() == '.')
				return false;
			for (unsigned char character : value)
			{
				if (character < 0x20 || character == 0x7f
					|| character == '<' || character == '>' || character == ':'
					|| character == '"' || character == '/' || character == '\\'
					|| character == '|' || character == '?' || character == '*')
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
			if (base.size() == 4 && (base.rfind("COM", 0) == 0
				|| base.rfind("LPT", 0) == 0)
				&& base[3] >= '1' && base[3] <= '9')
				return false;
			return true;
		}

		bool IsSafeRelativeDirectory(const std::filesystem::path& path)
		{
			if (path.empty() || path.is_absolute() || path.has_root_name()
				|| path.has_root_directory())
				return false;
			const std::filesystem::path normalized = path.lexically_normal();
			if (normalized.empty() || normalized == ".")
				return false;
			for (const auto& part : normalized)
			{
				if (part == ".." || !IsSafeIdentitySegment(PathToUTF8(part)))
					return false;
			}
			return true;
		}

	}

	std::string_view ApplicationPaths::GetProductDirectoryName(ApplicationProduct product)
	{
		switch (product)
		{
			case ApplicationProduct::Editor: return "Editor";
			case ApplicationProduct::Hub: return "Hub";
			case ApplicationProduct::Player: return "Player";
			case ApplicationProduct::Unknown: break;
		}
		return {};
	}

	ApplicationProduct ApplicationPaths::IdentifyExecutable(
		const std::filesystem::path& executablePath)
	{
		std::wstring filename = executablePath.filename().wstring();
		std::transform(filename.begin(), filename.end(), filename.begin(),
			[](wchar_t character) { return static_cast<wchar_t>(std::towlower(character)); });
		if (filename == L"tomcat.exe" || filename == L"tomcatinut.exe")
			return ApplicationProduct::Editor;
		if (filename == L"manager.exe" || filename == L"tomcathub.exe")
			return ApplicationProduct::Hub;
		if (filename == L"tomcatplayer.exe")
			return ApplicationProduct::Player;
		return ApplicationProduct::Unknown;
	}

	ApplicationProduct ApplicationPaths::IdentifyCurrentExecutable()
	{
#ifdef TC_PLATFORM_WINDOWS
		std::vector<wchar_t> buffer(MAX_PATH);
		for (;;)
		{
			const DWORD length = GetModuleFileNameW(nullptr, buffer.data(),
				static_cast<DWORD>(buffer.size()));
			if (length == 0)
				return ApplicationProduct::Unknown;
			if (length < buffer.size() - 1)
				return IdentifyExecutable(std::filesystem::path(
					std::wstring(buffer.data(), length)));
			if (buffer.size() >= 32768)
				return ApplicationProduct::Unknown;
			buffer.resize(buffer.size() * 2);
		}
#else
		return ApplicationProduct::Unknown;
#endif
	}

	std::optional<std::filesystem::path> ApplicationPaths::ResolveProductDataRoot(
		const std::filesystem::path& localAppData, ApplicationProduct product)
	{
		const std::string_view directoryName = GetProductDirectoryName(product);
		if (localAppData.empty() || directoryName.empty())
			return std::nullopt;
		return (localAppData / "TomCat" / std::string(directoryName)).lexically_normal();
	}

	std::optional<std::filesystem::path> ApplicationPaths::GetLocalAppDataRoot()
	{
#ifdef TC_PLATFORM_WINDOWS
		PWSTR localAppData = nullptr;
		const HRESULT result = SHGetKnownFolderPath(
			FOLDERID_LocalAppData, KF_FLAG_DEFAULT, nullptr, &localAppData);
		if (FAILED(result) || !localAppData)
		{
			if (localAppData)
				CoTaskMemFree(localAppData);
			return std::nullopt;
		}
		const std::filesystem::path resultPath(localAppData);
		CoTaskMemFree(localAppData);
		return resultPath;
#else
		return std::nullopt;
#endif
	}

	std::optional<std::filesystem::path> ApplicationPaths::GetProductDataRoot(
		ApplicationProduct product)
	{
		const auto localAppData = GetLocalAppDataRoot();
		return localAppData ? ResolveProductDataRoot(*localAppData, product) : std::nullopt;
	}

	std::optional<std::filesystem::path> ApplicationPaths::GetLogFile(
		ApplicationProduct product)
	{
		const auto productRoot = GetProductDataRoot(product);
		return productRoot ? std::optional<std::filesystem::path>(*productRoot / "TomCat.log")
			: std::nullopt;
	}

	std::optional<std::filesystem::path>
		ApplicationPaths::ResolveEditorRuntimeCacheRoot(
			const std::filesystem::path& localAppData)
	{
		const auto editorRoot = ResolveProductDataRoot(
			localAppData, ApplicationProduct::Editor);
		return editorRoot
			? std::optional<std::filesystem::path>(*editorRoot / "Runtime")
			: std::nullopt;
	}

	std::optional<std::filesystem::path>
		ApplicationPaths::GetEditorRuntimeCacheRoot()
	{
		const auto localAppData = GetLocalAppDataRoot();
		return localAppData ? ResolveEditorRuntimeCacheRoot(*localAppData)
			: std::nullopt;
	}

	void ApplicationPaths::SetRuntimeEditorRoot(
		const std::filesystem::path& root)
	{
		std::lock_guard<std::mutex> lock(s_RuntimeEditorRootMutex);
		if (root.empty())
			s_RuntimeEditorRoot.reset();
		else
			s_RuntimeEditorRoot = root.lexically_normal();
	}

	void ApplicationPaths::ClearRuntimeEditorRoot()
	{
		std::lock_guard<std::mutex> lock(s_RuntimeEditorRootMutex);
		s_RuntimeEditorRoot.reset();
	}

	std::optional<std::filesystem::path>
		ApplicationPaths::GetRuntimeEditorRoot()
	{
		std::lock_guard<std::mutex> lock(s_RuntimeEditorRootMutex);
		return s_RuntimeEditorRoot;
	}

	void ApplicationPaths::SetRuntimePackageRoot(
		const std::filesystem::path& root)
	{
		std::lock_guard<std::mutex> lock(s_RuntimePackageRootMutex);
		if (root.empty())
			s_RuntimePackageRoot.reset();
		else
			s_RuntimePackageRoot = root.lexically_normal();
	}

	void ApplicationPaths::ClearRuntimePackageRoot()
	{
		std::lock_guard<std::mutex> lock(s_RuntimePackageRootMutex);
		s_RuntimePackageRoot.reset();
	}

	std::optional<std::filesystem::path>
		ApplicationPaths::GetRuntimePackageRoot()
	{
		std::lock_guard<std::mutex> lock(s_RuntimePackageRootMutex);
		return s_RuntimePackageRoot;
	}

	std::filesystem::path ApplicationPaths::ResolveRuntimePackageAsset(
		const std::filesystem::path& packageRelativePath)
	{
		if (packageRelativePath.empty() || packageRelativePath.is_absolute()
			|| packageRelativePath.has_root_name()
			|| packageRelativePath.has_root_directory())
			return {};
		const std::filesystem::path relative = packageRelativePath.lexically_normal();
		for (const std::filesystem::path& part : relative)
		{
			if (part == "..")
				return {};
		}
		const std::filesystem::path packagePath =
			(std::filesystem::path("Packages") / relative).lexically_normal();
		if (const auto runtimeRoot = GetRuntimeEditorRoot())
			return (*runtimeRoot / packagePath).lexically_normal();
		if (const auto packageRoot = GetRuntimePackageRoot())
			return (*packageRoot / packagePath).lexically_normal();
		return packagePath;
	}

	std::optional<GameDataPaths> ApplicationPaths::ResolveGameDataPaths(
		const std::filesystem::path& localAppData,
		std::string_view companyName,
		std::string_view productName,
		const std::filesystem::path& saveDirectory,
		const std::filesystem::path& logDirectory,
		const std::filesystem::path& crashDirectory)
	{
		if (localAppData.empty() || !IsSafeIdentitySegment(companyName)
			|| !IsSafeIdentitySegment(productName)
			|| !IsSafeRelativeDirectory(saveDirectory)
			|| !IsSafeRelativeDirectory(logDirectory)
			|| !IsSafeRelativeDirectory(crashDirectory))
			return std::nullopt;

		GameDataPaths paths;
		paths.Root = (localAppData / "TomCat" / "Games" / UTF8ToPath(companyName)
			/ UTF8ToPath(productName)).lexically_normal();
		paths.Saves = (paths.Root / saveDirectory).lexically_normal();
		paths.Logs = (paths.Root / logDirectory).lexically_normal();
		paths.Crashes = (paths.Root / crashDirectory).lexically_normal();
		return paths;
	}

	std::optional<GameDataPaths> ApplicationPaths::GetGameDataPaths(
		std::string_view companyName,
		std::string_view productName,
		const std::filesystem::path& saveDirectory,
		const std::filesystem::path& logDirectory,
		const std::filesystem::path& crashDirectory)
	{
		const auto localAppData = GetLocalAppDataRoot();
		return localAppData ? ResolveGameDataPaths(*localAppData, companyName,
			productName, saveDirectory, logDirectory, crashDirectory) : std::nullopt;
	}

	void ApplicationPaths::SetRuntimeGameDataPaths(const GameDataPaths& paths)
	{
		std::lock_guard<std::mutex> lock(s_RuntimeGameDataMutex);
		s_RuntimeGameDataPaths = paths;
	}

	void ApplicationPaths::ClearRuntimeGameDataPaths()
	{
		std::lock_guard<std::mutex> lock(s_RuntimeGameDataMutex);
		s_RuntimeGameDataPaths.reset();
	}

	std::optional<GameDataPaths> ApplicationPaths::GetRuntimeGameDataPaths()
	{
		std::lock_guard<std::mutex> lock(s_RuntimeGameDataMutex);
		return s_RuntimeGameDataPaths;
	}

	std::optional<std::filesystem::path> ApplicationPaths::GetRuntimeSaveDirectory()
	{
		const auto paths = GetRuntimeGameDataPaths();
		return paths ? std::optional<std::filesystem::path>(paths->Saves) : std::nullopt;
	}

	std::optional<std::filesystem::path> ApplicationPaths::GetRuntimeLogDirectory()
	{
		const auto paths = GetRuntimeGameDataPaths();
		return paths ? std::optional<std::filesystem::path>(paths->Logs) : std::nullopt;
	}

	std::optional<std::filesystem::path> ApplicationPaths::GetRuntimeCrashDirectory()
	{
		const auto paths = GetRuntimeGameDataPaths();
		return paths ? std::optional<std::filesystem::path>(paths->Crashes) : std::nullopt;
	}

}
