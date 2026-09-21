#include "PlayerCommandLine.h"

#include <TomCat/Utils/PathUtils.h>

#include <string_view>
#include <vector>

#ifdef TC_PLATFORM_WINDOWS
	#include <Windows.h>
#endif

namespace TomCat {

	PlayerCommandLine ParsePlayerCommandLine(ApplicationCommandLineArgs args)
	{
		PlayerCommandLine result;
		if (args.Count <= 1)
		{
			result.PackagePath = "Game.tcpak";
			return result;
		}

		const std::string_view option = args[1] ? args[1] : "";
		if (args.Count == 2 && option == "--help")
		{
			result.Mode = PlayerCommandMode::ShowHelp;
			return result;
		}
		if (args.Count == 2 && option == "--version")
		{
			result.Mode = PlayerCommandMode::ShowVersion;
			return result;
		}
		if (args.Count == 3 && (option == "--package" || option == "--validate-package"))
		{
			result.Mode = option == "--package"
				? PlayerCommandMode::Run : PlayerCommandMode::ValidatePackage;
			result.PackagePath = UTF8ToPath(args[2] ? args[2] : "");
			if (!result.PackagePath.empty())
				return result;
			result.Error = "the package path cannot be empty";
		}
		else
			result.Error = "unsupported or incomplete command line";

		result.Mode = PlayerCommandMode::Invalid;
		return result;
	}

	std::filesystem::path GetPlayerExecutableDirectory()
	{
#ifdef TC_PLATFORM_WINDOWS
		std::vector<wchar_t> buffer(MAX_PATH);
		for (;;)
		{
			const DWORD length = GetModuleFileNameW(nullptr, buffer.data(),
				static_cast<DWORD>(buffer.size()));
			if (length == 0)
				break;
			if (length < buffer.size() - 1)
				return std::filesystem::path(
					std::wstring(buffer.data(), length)).parent_path().lexically_normal();
			if (buffer.size() >= 32768)
				break;
			buffer.resize(buffer.size() * 2);
		}
		return {};
#else
		std::error_code error;
		const std::filesystem::path current = std::filesystem::current_path(error);
		return error ? std::filesystem::path{} : current.lexically_normal();
#endif
	}

	std::filesystem::path ResolvePlayerPackagePath(const std::filesystem::path& path)
	{
		if (path.empty())
			return {};
		if (path.is_absolute())
			return path.lexically_normal();
		const std::filesystem::path executableDirectory =
			GetPlayerExecutableDirectory();
		return executableDirectory.empty()
			? std::filesystem::path{}
			: (executableDirectory / path).lexically_normal();
	}

	const char* GetPlayerHelpText()
	{
		return
			"TomCatPlayer - TomCat Engine Windows x64 runtime\n"
			"\n"
			"Usage:\n"
			"  TomCatPlayer.exe                          Run Game.tcpak beside the executable\n"
			"  TomCatPlayer.exe --package <path>         Run a cooked package\n"
			"  TomCatPlayer.exe --validate-package <path> Validate package and private runtime, then exit\n"
			"  TomCatPlayer.exe --version                Print compatibility versions\n"
			"  TomCatPlayer.exe --help                   Show this help\n";
	}

}
