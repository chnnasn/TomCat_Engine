#include "PlayerCommandLine.h"

#include <TomCat/Utils/PathUtils.h>

#include <cmath>
#include <cstdlib>
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
		if (option == "--validate-package")
		{
			if (args.Count == 3)
			{
				result.Mode = PlayerCommandMode::ValidatePackage;
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

		// Run mode. Each option appears at most once except --test-arg.
		bool sawPackage = false;
		bool sawFrames = false;
		bool sawFixedDelta = false;
		const auto parsePositive = [](std::string_view text, double maximum,
			double& value)
		{
			if (text.empty() || text.size() > 32)
				return false;
			const std::string copy(text);
			char* end = nullptr;
			value = std::strtod(copy.c_str(), &end);
			return end == copy.c_str() + copy.size() && std::isfinite(value)
				&& value > 0.0 && value <= maximum;
		};
		for (int index = 1; index < args.Count && result.Error.empty(); ++index)
		{
			const std::string_view name = args[index] ? args[index] : "";
			if (name == "--headless")
			{
				if (result.Headless)
					result.Error = "--headless was given more than once";
				result.Headless = true;
				continue;
			}
			const bool takesValue = name == "--package" || name == "--render-diag"
				|| name == "--audio-diag" || name == "--frame-trace"
				|| name == "--screenshot" || name == "--frames"
				|| name == "--fixed-delta" || name == "--test-arg";
			if (!takesValue || index + 1 >= args.Count)
			{
				result.Error = "unsupported or incomplete command line";
				break;
			}
			++index;
			const std::string_view text = args[index] ? args[index] : "";
			if (text.empty())
			{
				result.Error = name == "--package"
					? "the package path cannot be empty"
					: std::string(name) + " requires a non-empty value";
				break;
			}
			if (name == "--frames")
			{
				double frames = 0.0;
				if (sawFrames || !parsePositive(text, 1000000.0, frames)
					|| frames != static_cast<double>(static_cast<uint32_t>(frames)))
					result.Error = sawFrames ? "--frames was given more than once"
						: "--frames requires a whole number from 1 to 1000000";
				result.Frames = static_cast<uint32_t>(frames);
				sawFrames = true;
				continue;
			}
			if (name == "--fixed-delta")
			{
				double seconds = 0.0;
				if (sawFixedDelta || !parsePositive(text, 1.0, seconds))
					result.Error = sawFixedDelta ? "--fixed-delta was given more than once"
						: "--fixed-delta requires seconds in (0, 1]";
				result.FixedDelta = static_cast<float>(seconds);
				sawFixedDelta = true;
				continue;
			}
			if (name == "--test-arg")
			{
				const size_t equals = text.find('=');
				const std::string key(text.substr(0, equals));
				if (key.empty() || key.size() > 128)
				{
					result.Error = "--test-arg requires key or key=value with a key of 1-128 bytes";
					break;
				}
				if (!result.TestArguments.emplace(key, equals == std::string_view::npos
					? std::string() : std::string(text.substr(equals + 1))).second)
					result.Error = "--test-arg " + key + " was given more than once";
				continue;
			}
			std::filesystem::path& target = name == "--package" ? result.PackagePath
				: name == "--render-diag" ? result.RenderDiagnosticsPath
				: name == "--audio-diag" ? result.AudioDiagnosticsPath
				: name == "--frame-trace" ? result.FrameTracePath : result.ScreenshotPath;
			if (!target.empty())
			{
				result.Error = std::string(name) + " was given more than once";
				break;
			}
			target = UTF8ToPath(std::string(text));
			sawPackage = sawPackage || name == "--package";
		}
		if (result.Error.empty())
		{
			if (!sawPackage)
				result.PackagePath = "Game.tcpak";
			if (!result.ScreenshotPath.empty() && result.Frames == 0)
				result.Frames = 60;
			return result;
		}

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
#elif defined(TC_PLATFORM_LINUX)
            std::error_code error;
            const auto executable = std::filesystem::read_symlink("/proc/self/exe", error);
            return error ? std::filesystem::path{} : executable.parent_path();
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
			"TomCatPlayer - TomCat Engine desktop runtime\n"
			"\n"
			"Usage:\n"
			"  TomCatPlayer                          Run Game.tcpak beside the executable\n"
			"  TomCatPlayer --package <path>         Run a cooked package\n"
			"\n"
			"Run options (combine with --package; any order):\n"
			"  --render-diag <file.json>   Write 2D render diagnostics about once a second\n"
			"  --screenshot <out.png>      Save frame --frames N (default 60) as PNG, then exit\n"
			"  --audio-diag <file.json>    Snapshot source clip, playback state, bus and volume\n"
			"  --frame-trace <file.json>   Export the last 240 CPU/GPU frames on exit\n"
			"  --frames <N>                Exit after N frames\n"
			"  --headless                  Render into a hidden window (no focus or occlusion)\n"
			"  --fixed-delta <seconds>     Advance every frame by a fixed time step\n"
			"  --test-arg <key[=value]>    Test hook read by Application.GetTestArgument\n"
			"                              (repeatable)\n"
			"  TomCatPlayer --validate-package <path> Validate package and private runtime, then exit\n"
			"  TomCatPlayer --version                Print compatibility versions\n"
			"  TomCatPlayer --help                   Show this help\n";
	}

}
