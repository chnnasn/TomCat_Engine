#pragma once

#include <filesystem>

namespace TomCat {

	class FileDialogs
	{
	public:

		static std::filesystem::path OpenFile(const char* filter);
		static std::filesystem::path SaveFile(const char* filter);
		static std::filesystem::path OpenFolder();

	private:

	};

}
