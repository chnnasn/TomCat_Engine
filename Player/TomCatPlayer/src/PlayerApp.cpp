#include <TomCat.h>
#include <TomCat/Module/ModuleSystem.h>
#define TC_APPLICATION_PRODUCT TomCat::ApplicationProduct::Player
#include <TomCat/Core/EntryPoint.h>
#include <TomCat/Core/ApplicationPaths.h>
#include <TomCat/Core/CrashReporter.h>
#include <TomCat/Asset/TextureArtifact.h>
#include <TomCat/Asset/SpriteAsset.h>
#include <TomCat/Utils/PngWriter.h>
#include <TomCat/Runtime/RuntimeCompatibility.h>
#include <TomCat/Scripting/ScriptTypes.h>
#include <TomCat/Utils/FileSystemUtils.h>
#include <TomCat/Utils/PathUtils.h>

#include "PlayerCommandLine.h"
#include "PlayerRuntimeLayer.h"
#ifdef TC_PLATFORM_WINDOWS
#include "WindowsStartupVideo.h"
#endif

#include <array>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <limits>
#include <string_view>

namespace TomCat {
	namespace {
		bool IsHeadlessPlayerSmokeRequested()
		{
			const char* value = std::getenv("TOMCAT_E2E_PLAYER_HEADLESS");
			const std::string_view setting(value ? value : "");
			const bool requested = setting == "1" || setting == "true" || setting == "on" || setting == "yes";
			return requested;
		}

		struct PlayerLaunchPlan
		{
			PlayerCommandLine Command;
			std::filesystem::path PackagePath;
			WindowProps Window;
			uint32_t ViewportWidth = 1280;
			uint32_t ViewportHeight = 720;
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
				case PlayerWindowMode::MaximizedWindow: return WindowDisplayMode::MaximizedWindow;
				case PlayerWindowMode::Borderless:
					return WindowDisplayMode::Borderless;
				case PlayerWindowMode::ExclusiveFullscreen:
					return WindowDisplayMode::ExclusiveFullscreen;
			}
			return WindowDisplayMode::Windowed;
		}

		bool EnsureGameDataDirectories(const GameDataPaths& paths,
			std::string& errorMessage)
		{
			const std::array<std::filesystem::path, 4> directories = {
				paths.Root, paths.Saves, paths.Logs, paths.Crashes
			};
			for (const std::filesystem::path& directory : directories)
			{
				std::error_code error;
				std::filesystem::create_directories(directory, error);
				if (error || !std::filesystem::is_directory(directory, error) || error)
				{
					errorMessage = "could not create game data directory '"
						+ PathToUTF8(directory) + "': "
						+ (error ? error.message() : "path is not a directory");
					return false;
				}
			}
			errorMessage.clear();
			return true;
		}

		std::filesystem::path StageWindowIcon(AssetManager& assets,
			AssetHandle icon, const GameDataPaths& paths, std::string& warning)
		{
			warning.clear();
			if (static_cast<uint64_t>(icon) == 0)
				return {};
			std::vector<uint8_t> bytes;
			AssetType type = AssetType::None;
			if (!assets.ReadAssetBytes(icon, bytes, &type)
				|| type != AssetType::Texture2D || bytes.empty())
			{
				warning = "the cooked window icon could not be read";
				return {};
			}

			const std::filesystem::path cacheDirectory = paths.Root / "Cache";
			std::error_code directoryError;
			std::filesystem::create_directories(cacheDirectory, directoryError);
			if (directoryError)
			{
				warning = "the window icon cache could not be created: "
					+ directoryError.message();
				return {};
			}
			std::string extension = ".image";
			ResolvedSpriteAsset sprite;
            std::span<const uint8_t> textureBytes = bytes;
            std::span<const uint8_t> atlasBytes;
            if (ParseCookedSpriteSubAsset(bytes, sprite, atlasBytes))
                textureBytes = atlasBytes;
            if (IsTextureArtifact(textureBytes))
			{
				TextureArtifactView artifact;
				std::string decodeError;
				if (!ParseTextureArtifact(textureBytes, artifact, decodeError)
					|| artifact.Mips.empty())
				{
					warning = "the cooked window icon artifact is invalid: " + decodeError;
					return {};
				}

				const TextureArtifactMip& mip = artifact.Mips.front();
				std::vector<uint8_t> rgba;
				if (!DecompressTextureMip(mip, artifact.Format, rgba, decodeError))
				{
					warning = "the cooked window icon could not be decoded: " + decodeError;
					return {};
				}

                bytes = EncodePngRGBABottomUp(mip.Width, mip.Height, rgba);
                if (bytes.empty())
                {
                    warning = "the cooked window icon dimensions are invalid";
                    return {};
                }
                extension = ".png";
            }
            else if (textureBytes.data() != bytes.data())
            {
                std::vector<uint8_t> encoded(textureBytes.begin(), textureBytes.end());
                bytes = std::move(encoded);
            }

			const std::filesystem::path iconPath = cacheDirectory /
				("WindowIcon-" + std::to_string(static_cast<uint64_t>(icon)) + extension);
			std::string writeError;
			if (!FileSystem::WriteFileAtomically(iconPath,
				std::string_view(reinterpret_cast<const char*>(bytes.data()), bytes.size()),
				writeError))
			{
				warning = "the cooked window icon could not be staged: " + writeError;
				return {};
			}
			return iconPath;
		}

		PlayerLaunchPlan PreparePlayerLaunch(PlayerCommandLine command)
		{
			PlayerLaunchPlan plan;
			plan.Command = std::move(command);
			if (plan.Command.Mode != PlayerCommandMode::Run)
				return plan;
			ApplicationPaths::ClearRuntimeGameDataPaths();

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

			const auto dataPaths = ApplicationPaths::GetGameDataPaths(
				settings.CompanyName, settings.ProductName, settings.SaveDirectory,
				settings.LogDirectory, settings.CrashDirectory);
			std::string dataError;
			if (!dataPaths)
				dataError = "LocalAppData or the packaged game data paths are invalid";
			else
				EnsureGameDataDirectories(*dataPaths, dataError);
			if (!dataError.empty())
			{
				assets.UnmountCookedPackage();
				plan.Error = "game data initialization failed: " + dataError;
				plan.ErrorCode = 6;
				return plan;
			}

			ApplicationPaths::SetRuntimeGameDataPaths(*dataPaths);
			const bool fileLogging = Log::InitFile(dataPaths->Logs / "Player.log");
			if (!CrashReporter::Configure(dataPaths->Crashes,
				settings.CompanyName, settings.ProductName, settings.Version))
				TC_Core_Warn("Crash reporting is unavailable for '{0}'",
					settings.ProductName);
			if (!fileLogging)
				TC_Core_Warn("Per-game file logging is unavailable for '{0}'",
					settings.ProductName);

			std::string iconWarning;
			const std::filesystem::path iconPath = StageWindowIcon(
				assets, settings.Icon, *dataPaths, iconWarning);
			if (!iconWarning.empty())
				TC_Core_Warn("TomCat Player: {0}", iconWarning);
			assets.UnmountCookedPackage();

			plan.Window = WindowProps(settings.ProductName,
				settings.Width, settings.Height, iconPath);
			plan.ViewportWidth = settings.Width;
			plan.ViewportHeight = settings.Height;
			plan.Window.DefaultIsNativeResolution = settings.DefaultIsNativeResolution;
			plan.Window.DisplayMode = ToWindowDisplayMode(settings.WindowMode);
			plan.Window.Resizable = settings.Resizable;
			plan.Window.VSync = settings.VSync;
			TC_Core_Info("Launching {0} {1} by {2}; data root: {3}",
				settings.ProductName, settings.Version, settings.CompanyName,
				PathToUTF8(dataPaths->Root));
			plan.CreateWindow = !IsHeadlessPlayerSmokeRequested();
			// --headless keeps a real graphics context in a hidden window, so
			// rendering and screenshots work without focus or occlusion issues.
			if (plan.Command.Headless)
				plan.Window.Visible = false;
			if (!plan.CreateWindow && !plan.Command.ScreenshotPath.empty())
			{
				plan.Error = "--screenshot needs a graphics context; use --headless "
					"instead of TOMCAT_E2E_PLAYER_HEADLESS";
				plan.ErrorCode = 2;
			}
			if (!plan.CreateWindow)
				TC_Core_Info("Running the Player end-to-end smoke without a graphics window");
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
				#ifdef TC_PLATFORM_WINDOWS
				if (plan.Command.Mode == PlayerCommandMode::Run && plan.CreateWindow && !plan.Command.Headless)
				{
					const auto video = plan.PackagePath.parent_path() / "Startup.mp4";
					TC_Core_Info("Playing startup video: {0}", PathToUTF8(video));
					const auto result = PlayStartupVideo(static_cast<GLFWwindow*>(GetWindow().GetNativeWindow()), video);
					if (result != StartupResult::Completed)
					{
						if (result == StartupResult::Failed)
							MessageBoxW(nullptr, L"The startup animation could not be played. Check Startup.mp4 and Windows media support.",
								L"Startup animation", MB_OK | MB_ICONERROR);
						Close(result == StartupResult::Closed ? 0 : 7);
						return;
					}
					TC_Core_Info("Startup video completed");
				}
#endif
				const PlayerCommandLine& command = plan.Command;
				switch (command.Mode)
				{
					case PlayerCommandMode::Run:
					{
						// Resolve against the launch directory, not the executable's.
						std::filesystem::path diagnosticsPath =
							command.RenderDiagnosticsPath;
						if (!diagnosticsPath.empty() && diagnosticsPath.is_relative())
						{
							std::error_code currentError;
							const auto current = std::filesystem::current_path(currentError);
							if (!currentError)
								diagnosticsPath = (current / diagnosticsPath).lexically_normal();
						}
						const auto resolveFromLaunchDirectory =
							[](std::filesystem::path path)
						{
							if (!path.empty() && path.is_relative())
							{
								std::error_code currentError;
								const auto current = std::filesystem::current_path(currentError);
								if (!currentError)
									path = (current / path).lexically_normal();
							}
							return path;
						};
						PlayerAutomationOptions automation;
						automation.RenderDiagnosticsPath = std::move(diagnosticsPath);
						automation.ScreenshotPath =
							resolveFromLaunchDirectory(command.ScreenshotPath);
						automation.Frames = command.Frames;
						SetTestArguments(command.TestArguments);
						SetFixedFrameDelta(command.FixedDelta);
						PushLayer(new PlayerRuntimeLayer(plan.PackagePath,
							plan.ViewportWidth, plan.ViewportHeight,
							std::move(automation)));
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
						if (!ModuleSystem::Get().LoadCookedModules(AssetManager::Get().GetCookedModulePayload(), runtimeError)
                            || !ValidateMountedPlayerManagedRuntime(runtimeError))
						{
							std::string moduleCleanup;
                            (void)ModuleSystem::Get().UnloadAllModules(moduleCleanup);
                            TC_Core_Error("TomCat Player: packaged runtime validation failed: {0}",
								runtimeError);
							AssetManager::Get().UnmountCookedPackage();
							Close(5);
							break;
						}
						std::string moduleCleanup;
                        if (!ModuleSystem::Get().UnloadAllModules(moduleCleanup)) { Close(5); break; }
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
							<< " template=" << RuntimeCompatibility::PlayerTemplateSchemaVersion
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
