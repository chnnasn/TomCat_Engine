#pragma once

#include <filesystem>
#include <string>
#include <string_view>

namespace TomCat {

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
