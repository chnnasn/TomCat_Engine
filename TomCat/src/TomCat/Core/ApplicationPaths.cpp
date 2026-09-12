#include "tcpch.h"
#include "ApplicationPaths.h"

#include <algorithm>
#include <cwctype>
#include <vector>

#ifdef TC_PLATFORM_WINDOWS
	#include <Windows.h>
	#include <shlobj.h>
	#pragma comment(lib, "Shell32.lib")
#endif

namespace TomCat {

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

}
