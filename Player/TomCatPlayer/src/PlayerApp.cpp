#include <TomCat.h>
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

		class PlayerApplication final : public Application
		{
		public:
			explicit PlayerApplication(ApplicationCommandLineArgs args)
				: PlayerApplication(ParsePlayerCommandLine(args))
			{
			}

		private:
			explicit PlayerApplication(PlayerCommandLine command)
				: Application("TomCatPlayer", {}, false,
					command.Mode == PlayerCommandMode::Run)
			{
				switch (command.Mode)
				{
					case PlayerCommandMode::Run:
					{
						const std::filesystem::path package =
							ResolvePlayerPackagePath(command.PackagePath);
						std::error_code error;
						if (!std::filesystem::is_regular_file(package, error) || error)
						{
							TC_Core_Error("TomCat Player: package is missing: {0}",
								PathToUTF8(package));
							Close(3);
							return;
						}
						PushLayer(new PlayerRuntimeLayer(package));
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
						std::cout << "TomCatPlayer " << RuntimeCompatibility::EngineBuildID
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
