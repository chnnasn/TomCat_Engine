#include <TomCat.h>
#include <TomCat/Core/EntryPoint.h>
#include <TomCat/Project/ProjectManager.h>
#include <TomCat/Utils/PathUtils.h>

#include "CookedPlayerLayer.h"
#include "EditorLayer.h"

#include <string_view>
#include <vector>

#ifdef TC_PLATFORM_WINDOWS
#include <Windows.h>
#endif

namespace TomCat {

	

	class TomCatInput : public Application
	{
	public:
		TomCatInput(ApplicationCommandLineArgs args)
			: Application(IsCookedPlayer(args) ? "TomCatPlayer" : "TomCatEditor",
				IsCookedPlayer(args) ? std::filesystem::path{} :
					std::filesystem::path("Packages/Resources/Icons/Logo.ico"),
				!IsCookedPlayer(args))
		{
			if (IsCookedPlayer(args))
			{
				if (args.Count != 3)
				{
					TC_Core_Error("Usage: TomCatInut --play-cooked <package.tcpak>");
					Close(2);
					return;
				}
				PushLayer(new CookedPlayerLayer(ResolveCookedPackagePath(args[2])));
				return;
			}

			if (args.Count > 1)
			{
				const std::filesystem::path projectPath = UTF8ToPath(args[1]);
				ProjectManager::Get().LoadProject(projectPath);
			}
			
			PushLayer(new EditorLayer());
		}

	private:
		static bool IsCookedPlayer(ApplicationCommandLineArgs args)
		{
			return args.Count > 1 && args[1] &&
				std::string_view(args[1]) == "--play-cooked";
		}

		static std::filesystem::path ResolveCookedPackagePath(const char* argument)
		{
			std::filesystem::path packagePath = UTF8ToPath(argument ? argument : "");
			if (packagePath.empty() || packagePath.is_absolute())
				return packagePath.lexically_normal();

#ifdef TC_PLATFORM_WINDOWS
			std::vector<wchar_t> modulePath(MAX_PATH);
			for (;;)
			{
				const DWORD length = GetModuleFileNameW(nullptr, modulePath.data(),
					static_cast<DWORD>(modulePath.size()));
				if (length == 0)
					break;
				if (length < modulePath.size() - 1)
				{
					const std::filesystem::path executable(
						std::wstring(modulePath.data(), length));
					return (executable.parent_path() / packagePath).lexically_normal();
				}
				if (modulePath.size() >= 32768)
					break;
				modulePath.resize(modulePath.size() * 2);
			}
#endif

			std::error_code error;
			const std::filesystem::path absolute =
				std::filesystem::absolute(packagePath, error);
			return (error ? packagePath : absolute).lexically_normal();
		}
	};

	Application* CreateApplication(ApplicationCommandLineArgs args)
	{
		return new TomCatInput(args);
	}

}
