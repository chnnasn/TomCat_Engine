#include <TomCat.h>
#define TC_APPLICATION_PRODUCT TomCat::ApplicationProduct::Player
#include <TomCat/Core/EntryPoint.h>
#include <TomCat/Runtime/RuntimeCompatibility.h>
#include <TomCat/Scripting/ScriptTypes.h>
#include <TomCat/Utils/PathUtils.h>

#include "PlayerCommandLine.h"
#include "PlayerRuntimeLayer.h"

#include <filesystem>
#include <iostream>

namespace TomCat {
	namespace {
		struct PlayerLaunchPlan
		{
			PlayerCommandLine Command;
			std::filesystem::path PackagePath;
			WindowProps Window;
			bool CreateWindow = false;
			int ErrorCode = 0;
			std::string Error;
		};

		WindowDisplayMode ToWindowDisplayMode(PlayerWindowMode mode)
		{
			switch (mode)
			{
				case PlayerWindowMode::Windowed:
					return WindowDisplayMode::Windowed;
				case PlayerWindowMode::Borderless:
					return WindowDisplayMode::Borderless;
				case PlayerWindowMode::ExclusiveFullscreen:
					return WindowDisplayMode::ExclusiveFullscreen;
			}
			return WindowDisplayMode::Windowed;
		}

		PlayerLaunchPlan PreparePlayerLaunch(PlayerCommandLine command)
		{
			PlayerLaunchPlan plan;
			plan.Command = std::move(command);
			if (plan.Command.Mode != PlayerCommandMode::Run)
				return plan;

			plan.PackagePath = ResolvePlayerPackagePath(plan.Command.PackagePath);
			std::error_code error;
			if (!std::filesystem::is_regular_file(plan.PackagePath, error) || error)
			{
				plan.Error = "package is missing: " + PathToUTF8(plan.PackagePath);
				plan.ErrorCode = 3;
				return plan;
			}

			AssetManager& assets = AssetManager::Get();
			if (!assets.MountCookedPackage(plan.PackagePath))
			{
				plan.Error = "package validation failed before window creation: "
					+ PathToUTF8(plan.PackagePath);
				plan.ErrorCode = 4;
				return plan;
			}
			const PlayerSettings settings = assets.GetCookedPlayerSettings();
			assets.UnmountCookedPackage();

			plan.Window = WindowProps(settings.ProductName,
				settings.Width, settings.Height);
			plan.Window.DisplayMode = ToWindowDisplayMode(settings.WindowMode);
			plan.Window.Resizable = settings.Resizable;
			plan.Window.VSync = settings.VSync;
			plan.CreateWindow = true;
			return plan;
		}

		class PlayerApplication final : public Application
		{
		public:
			explicit PlayerApplication(ApplicationCommandLineArgs args)
				: PlayerApplication(PreparePlayerLaunch(ParsePlayerCommandLine(args)))
			{
			}

		private:
			explicit PlayerApplication(PlayerLaunchPlan plan)
				: Application(std::move(plan.Window), false, plan.CreateWindow)
			{
				if (!plan.Error.empty())
				{
					TC_Core_Error("TomCat Player: {0}", plan.Error);
					Close(plan.ErrorCode);
					return;
				}
				const PlayerCommandLine& command = plan.Command;
				switch (command.Mode)
				{
					case PlayerCommandMode::Run:
					{
						PushLayer(new PlayerRuntimeLayer(plan.PackagePath));
						break;
					}
					case PlayerCommandMode::ValidatePackage:
					{
						const std::filesystem::path package =
							ResolvePlayerPackagePath(command.PackagePath);
						std::error_code error;
						if (!std::filesystem::is_regular_file(package, error) || error)
						{
							TC_Core_Error("TomCat Player: package is missing: {0}",
								PathToUTF8(package));
							Close(3);
							break;
						}
						if (!AssetManager::Get().MountCookedPackage(package))
						{
							TC_Core_Error("TomCat Player: package validation failed: {0}",
								PathToUTF8(package));
							Close(4);
							break;
						}
						std::string runtimeError;
						if (!ValidateMountedPlayerManagedRuntime(runtimeError))
						{
							TC_Core_Error("TomCat Player: packaged runtime validation failed: {0}",
								runtimeError);
							AssetManager::Get().UnmountCookedPackage();
							Close(5);
							break;
						}
						TC_Core_Info("Package and packaged runtime are valid: {0}",
							PathToUTF8(package));
						AssetManager::Get().UnmountCookedPackage();
						Close(0);
						break;
					}
					case PlayerCommandMode::ShowVersion:
						std::cout << "TomCatPlayer " << Version::ProductVersion
							<< " build=" << RuntimeCompatibility::EngineBuildID
							<< " tcpak=" << RuntimeCompatibility::TcpakVersion
							<< " abi=" << RuntimeCompatibility::PlayerAbiVersion
							<< " native-api=" << Scripting::NativeApiVersion << '\n';
						Close(0);
						break;
					case PlayerCommandMode::ShowHelp:
						std::cout << GetPlayerHelpText();
						Close(0);
						break;
					case PlayerCommandMode::Invalid:
						std::cerr << "TomCatPlayer: " << command.Error << "\n\n"
							<< GetPlayerHelpText();
						Close(2);
						break;
				}
			}
		};

	}

	Application* CreateApplication(ApplicationCommandLineArgs args)
	{
		return new PlayerApplication(args);
	}

}
