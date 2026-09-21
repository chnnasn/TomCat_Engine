#pragma once

#include <filesystem>
#include <string>
#include <string_view>

namespace TomCat {

	// std::filesystem parses the Windows namespace prefix as a root name.
	// Directory traversal must start at the actual drive or UNC share instead.
	inline std::filesystem::path DirectoryChainRoot(const std::filesystem::path& path)
	{
#ifdef _WIN32
		std::wstring value = path.wstring();
		for (auto& character : value)
			if (character == L'/') character = L'\\';
		size_t serverStart = 2;
		if (value.rfind(L"\\\\?\\", 0) == 0)
		{
			if (value.size() >= 6 && value[5] == L':' && (value.size() == 6 || value[6] == L'\\') &&
				((value[4] >= L'A' && value[4] <= L'Z') || (value[4] >= L'a' && value[4] <= L'z')))
				return std::filesystem::path(value.substr(0, 6) + L"\\");
			if (value.size() < 8 ||
				(value.substr(4, 4) != L"UNC\\" && value.substr(4, 4) != L"unc\\"))
				return {};
			serverStart = 8;
		}
		if (value.rfind(L"\\\\", 0) == 0)
		{
			const size_t serverEnd = value.find(L'\\', serverStart);
			if (serverEnd == std::wstring::npos || serverEnd == serverStart || serverEnd + 1 >= value.size())
				return {};
			const size_t shareEnd = value.find(L'\\', serverEnd + 1);
			if (shareEnd == serverEnd + 1)
				return {};
			return std::filesystem::path(value.substr(0, shareEnd));
		}
#endif
		return path.root_path();
	}

	// Compare DOS/UNC paths independently of their extended-length spelling.
	// This is lexical only: callers must still check canonical containment and
	// reparse points. Keep original paths for I/O to retain long-path support.
	inline std::filesystem::path PathForComparison(const std::filesystem::path& path)
	{
#ifdef _WIN32
		const auto preferred = std::filesystem::path(path).make_preferred();
		const auto& value = preferred.native();
		if (value.rfind(L"\\\\?\\", 0) == 0)
		{
			if (value.size() >= 7 && value[5] == L':' && value[6] == L'\\' &&
				((value[4] >= L'A' && value[4] <= L'Z') || (value[4] >= L'a' && value[4] <= L'z')))
				return std::filesystem::path(value.substr(4)).lexically_normal();
			if (value.size() >= 8 && (value.substr(4, 4) == L"UNC\\" || value.substr(4, 4) == L"unc\\"))
				return std::filesystem::path(L"\\\\" + value.substr(8)).lexically_normal();
		}
#endif
		return path.lexically_normal();
	}

	// Text crossing a serialization/UI/process boundary is always UTF-8. Keep
	// filesystem::path native internally so Windows paths never pass through the
	// active ANSI code page.
	inline std::string PathToUTF8(const std::filesystem::path& path)
	{
		const std::u8string value = path.generic_u8string();
		return std::string(reinterpret_cast<const char*>(value.data()), value.size());
	}

	inline std::filesystem::path UTF8ToPath(std::string_view value)
	{
		const std::u8string utf8(
			reinterpret_cast<const char8_t*>(value.data()), value.size());
		return std::filesystem::path(utf8);
	}

}
