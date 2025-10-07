#pragma once

#include <string>

namespace TomCat {

	class FileDialogs
	{
	public:

		static std::string OpenFile(const char* filter);
		static std::string SaveFile(const char* filter);

	private:

	};

}
