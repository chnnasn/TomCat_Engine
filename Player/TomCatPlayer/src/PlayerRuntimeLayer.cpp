#include "PlayerRuntimeLayer.h"

#include "PlayerCommandLine.h"

#include <TomCat/Scripting/ManagedRuntimeFactory.h>
#include <TomCat/Scripting/ScriptEngine.h>
#include <TomCat/Module/ModuleSystem.h>
#include <TomCat/Utils/FileSystemUtils.h>
#include <TomCat/Utils/PathUtils.h>
#include <TomCat/Utils/PngWriter.h>
#include <TomCat/Renderer/Framebuffer.h>

#include <system_error>

namespace TomCat {
	namespace {

		class ScopedShadowDirectory
		{
		public:
			~ScopedShadowDirectory()
			{
				if (m_Path.empty())
					return;
				std::error_code error;
				std::filesystem::remove_all(m_Path, error);
			}

			bool Create(std::string& errorMessage)
			{
				std::error_code error;
				const std::filesystem::path root =
					std::filesystem::temp_directory_path(error);
				if (error || root.empty())
				{
					errorMessage = "the operating-system temporary directory is unavailable";
					return false;
				}
				for (uint32_t attempt = 0; attempt < 64; ++attempt)
				{
					const std::filesystem::path candidate = root /
						("TomCatPlayer-" + std::to_string(static_cast<uint64_t>(UUID())));
					error.clear();
					if (std::filesystem::create_directory(candidate, error))
					{
						m_Path = candidate;
						return true;
					}
					if (error && error != std::errc::file_exists)
					{
						errorMessage =
							"could not create a private managed Shadow directory: " +
							error.message();
						return false;
					}
				}
				errorMessage = "could not allocate a unique managed Shadow directory";
				return false;
			}

			const std::filesystem::path& GetPath() const { return m_Path; }

		private:
			std::filesystem::path m_Path;
		};

		bool WritePayload(const std::filesystem::path& path,
			const std::vector<uint8_t>& bytes, std::string& errorMessage)
		{
			if (bytes.empty())
			{
				errorMessage = "managed payload file is empty";
				return false;
			}
			return FileSystem::WriteFileAtomically(path,
				std::string_view(reinterpret_cast<const char*>(bytes.data()), bytes.size()),
				errorMessage);
		}

		bool CreateMountedManagedRuntime(
			std::shared_ptr<Scripting::IScriptRuntime>& runtime,
			std::string& errorMessage)
		{
			runtime.reset();
			AssetManager& assets = AssetManager::Get();
			if (!assets.IsCookedPackageMounted())
			{
				errorMessage = "no cooked package is mounted";
				return false;
			}
			const ManagedPackagePayload* payload = assets.GetCookedManagedPayload();
			if (!payload)
			{
				errorMessage.clear();
				return true;
			}

#ifdef __EMSCRIPTEN__
			auto candidate = Scripting::CreateWebManagedScriptRuntime(
				payload->Assembly, payload->Pdb, &errorMessage);
			if (!candidate)
				return false;
			std::string manifest;
			if (!candidate->ReadProjectMetadata(manifest) || manifest.empty())
			{
				errorMessage = "packaged C# metadata could not be loaded by the bundled WebAssembly runtime";
				return false;
			}
			runtime = std::move(candidate);
			errorMessage.clear();
			return true;
#else

			ScopedShadowDirectory shadow;
			if (!shadow.Create(errorMessage))
			{
				errorMessage = "could not stage the packaged C# assembly: " + errorMessage;
				return false;
			}

			const std::filesystem::path assembly =
				shadow.GetPath() / "Assembly-CSharp.dll";
			if (!WritePayload(assembly, payload->Assembly, errorMessage))
			{
				errorMessage = "could not stage the packaged C# assembly: " + errorMessage;
				return false;
			}
			std::filesystem::path pdb;
			if (!payload->Pdb.empty())
			{
				pdb = shadow.GetPath() / "Assembly-CSharp.pdb";
				if (!WritePayload(pdb, payload->Pdb, errorMessage))
				{
					errorMessage = "could not stage the packaged C# symbols: " + errorMessage;
					return false;
				}
			}

			const std::filesystem::path playerRoot = GetPlayerExecutableDirectory();
			if (playerRoot.empty())
			{
				errorMessage = "could not resolve the TomCatPlayer executable directory";
				return false;
			}
			auto candidate = Scripting::CreateManagedScriptRuntime(
				playerRoot / "Managed", assembly, pdb, playerRoot / "dotnet",
				&errorMessage);
			if (!candidate)
			{
				if (errorMessage.empty())
					errorMessage = "managed host initialization failed";
				return false;
			}
			std::string manifest;
			if (!candidate->ReadProjectMetadata(manifest) || manifest.empty())
			{
				errorMessage =
					"packaged C# metadata could not be loaded from Assembly-CSharp.dll";
				return false;
			}
			runtime = std::move(candidate);
			errorMessage.clear();
			return true;
#endif
		}

	}

	bool ValidateMountedPlayerManagedRuntime(std::string& errorMessage)
	{
		std::shared_ptr<Scripting::IScriptRuntime> runtime;
		return CreateMountedManagedRuntime(runtime, errorMessage);
	}

	PlayerRuntimeLayer::PlayerRuntimeLayer(std::filesystem::path packagePath,
		uint32_t viewportWidth, uint32_t viewportHeight,
		PlayerAutomationOptions automation)
		: Layer("PlayerRuntimeLayer"), m_PackagePath(std::move(packagePath)),
		m_ViewportWidth(viewportWidth), m_ViewportHeight(viewportHeight),
		m_RenderDiagnosticsPath(std::move(automation.RenderDiagnosticsPath)),
		m_ScreenshotPath(std::move(automation.ScreenshotPath)),
		m_ExitAfterFrames(automation.Frames)
	{
	}

	void PlayerRuntimeLayer::OnAttach()
	{
		AssetManager& assets = AssetManager::Get();
		if (!assets.MountCookedPackage(m_PackagePath))
		{
			Fail("could not mount cooked package '" + PathToUTF8(m_PackagePath) + "'", 4);
			return;
		}
		const bool hasWindow = Application::Get().HasWindow();
		std::string moduleError;
		if (!ModuleSystem::Get().LoadCookedModules(assets.GetCookedModulePayload(), moduleError))
		{ Fail("could not load packaged native modules: " + moduleError, 5); return; }
		std::string shaderError;
		if (hasWindow && !assets.PreloadCookedShaders(shaderError))
		{
			Fail("could not publish packaged shaders: " + shaderError);
			return;
		}
		const size_t texturePreloadCount = hasWindow
			? assets.BeginCookedTexturePreload() : 0;
		if (texturePreloadCount != 0)
			TC_Core_Info("Queued {0} packaged textures for bounded preload",
				texturePreloadCount);

		Scripting::ScriptEngine::Get().SetRuntime({});
		std::shared_ptr<Scripting::IScriptRuntime> runtime;
		std::string runtimeError;
		if (!CreateMountedManagedRuntime(runtime, runtimeError))
		{
			Fail("could not initialize the packaged C# runtime: " + runtimeError);
			return;
		}
		if (runtime)
			Scripting::ScriptEngine::Get().SetRuntime(std::move(runtime));

		m_SceneManager = CreateScope<SceneManager>();
		if (hasWindow)
		{
			m_NativeResolution = assets.GetCookedPlayerSettings().DefaultIsNativeResolution;
			UpdatePresentation(Application::Get().GetWindow());
		}
		else
		{
			m_SceneManager->SetViewportSize(m_ViewportWidth, m_ViewportHeight);
			m_SceneManager->SetRuntimeUIViewportMetrics(glm::vec2(0.0f), 1.0f);
		}
		if (!m_SceneManager->ConfigureCookedPackage()
			|| !m_SceneManager->ActivateRuntime()
			|| !m_SceneManager->LoadEntryScene())
		{
			Fail("could not start the entry Scene runtime: "
				+ m_SceneManager->GetLastError());
			return;
		}
		if (!m_RenderDiagnosticsPath.empty())
		{
			Renderer2D::SetSceneDiagnosticsEnabled(true);
			TC_Core_Info("Writing 2D render diagnostics to '{0}'",
				PathToUTF8(m_RenderDiagnosticsPath));
		}
		const AssetHandle entryScene = m_SceneManager->GetActiveSceneHandle();
		TC_Core_Info("TomCat Player started '{0}' (entry Scene {1})",
			PathToUTF8(assets.GetCookedPackagePath()),
			static_cast<uint64_t>(entryScene));
	}

	void PlayerRuntimeLayer::OnDetach()
	{
		if (!m_RenderDiagnosticsPath.empty())
		{
			WriteRenderDiagnostics();
			Renderer2D::SetSceneDiagnosticsEnabled(false);
		}
		if (m_SceneManager)
			m_SceneManager->Stop();
		m_SceneManager.reset();
		Scripting::ScriptEngine::Get().SetRuntime({});
		AssetManager::Get().UnmountCookedPackage();
		std::string moduleError;
		if (!ModuleSystem::Get().UnloadAllModules(moduleError))
			TC_Core_Error("Packaged module cleanup failed: {0}", moduleError);
	}

	void PlayerRuntimeLayer::OnUpdate(Timestep timestep)
	{
		if (!m_SceneManager)
			return;
		const bool hasWindow = Application::Get().HasWindow();
		if (hasWindow)
		{
			UpdatePresentation(Application::Get().GetWindow());
		}
		if (const Ref<Scene> scene = m_SceneManager->GetActiveScene())
			{
            if (m_PresentationTarget) m_PresentationTarget->Bind();
            scene->OnUpdateRuntime(timestep, hasWindow);
            if (m_PresentationTarget)
                m_PresentationTarget->PresentToWindow(m_PresentX, m_PresentY, m_PresentWidth, m_PresentHeight);
        }
		if (!m_RenderDiagnosticsPath.empty() && (m_Frame % 60) == 0)
			WriteRenderDiagnostics();
		++m_Frame;
		if (m_ExitAfterFrames != 0 && m_Frame >= m_ExitAfterFrames
			&& m_SceneManager)
		{
			int exitCode = 0;
			if (!m_ScreenshotPath.empty())
			{
				std::string error;
				if (WriteScreenshot(error))
					TC_Core_Info("Screenshot of frame {0} written to '{1}'", m_Frame,
						PathToUTF8(m_ScreenshotPath));
				else
				{
					TC_Core_Error("TomCat Player: screenshot failed: {0}", error);
					exitCode = 8;
				}
			}
			if (!m_RenderDiagnosticsPath.empty())
				WriteRenderDiagnostics();
			m_ExitAfterFrames = 0;
			Application::Get().Close(exitCode);
		}
		if (!m_SceneManager->CommitPendingTransition()
			&& !m_SceneManager->GetActiveScene())
			Fail("Scene transition failed without a recoverable active Scene: "
				+ m_SceneManager->GetLastError());
	}

	void PlayerRuntimeLayer::OnEvent(Event& event)
	{
		EventDispatcher dispatcher(event);
		dispatcher.Dispatch<WindowResizeEvent>(
			TC_Bind_Event_Fn(PlayerRuntimeLayer::OnWindowResize));
	}

	void PlayerRuntimeLayer::UpdatePresentation(Window& window)
    {
        const uint32_t w = window.GetFramebufferWidth(), h = window.GetFramebufferHeight();
        if (!w || !h) return;
        const auto mode = window.GetDisplayMode();
        const bool scale = !m_NativeResolution &&
            (mode == WindowDisplayMode::Borderless || mode == WindowDisplayMode::MaximizedWindow);
        const auto rw = scale ? m_ViewportWidth : w, rh = scale ? m_ViewportHeight : h;
        if (rw != m_RenderWidth || rh != m_RenderHeight)
        {
            m_RenderWidth = rw;
            m_RenderHeight = rh;
            m_SceneManager->SetViewportSize(rw, rh);
        }
        if (rw != w || rh != h)
        {
            if (!m_PresentationTarget)
            {
                FramebufferSpecification spec;
                spec.Width = rw; spec.Height = rh;
                spec.Attachments = {FramebufferTextureFormat::RGBA8, FramebufferTextureFormat::Depth};
                m_PresentationTarget = Framebuffer::Create(spec);
            }
        }
        else if (m_PresentationTarget)
        {
            m_PresentationTarget.reset();
            RenderCommand::SetViewport(0, 0, w, h);
        }
        const double factor = std::min(double(w)/rw, double(h)/rh);
        m_PresentWidth = std::clamp(uint32_t(std::lround(rw*factor)), 1u, w);
        m_PresentHeight = std::clamp(uint32_t(std::lround(rh*factor)), 1u, h);
        m_PresentX = (w-m_PresentWidth)/2;
        m_PresentY = (h-m_PresentHeight)/2;
        const float sx = window.GetScreenToFramebufferScaleX(), sy = window.GetScreenToFramebufferScaleY();
        m_SceneManager->SetRuntimeUIViewportMetrics({m_PresentX/sx, (h-m_PresentHeight-m_PresentY)/sy},
            window.GetDPIScale(), {sx*rw/m_PresentWidth, sy*rh/m_PresentHeight});
    }

    bool PlayerRuntimeLayer::OnWindowResize(WindowResizeEvent& event)
	{
		if (m_SceneManager)
		{
			UpdatePresentation(Application::Get().GetWindow());
		}
		return false;
	}

	bool PlayerRuntimeLayer::WriteScreenshot(std::string& error)
	{
		const Ref<Scene> scene = m_SceneManager ? m_SceneManager->GetActiveScene() : nullptr;
		if (!scene || !Application::Get().HasWindow())
		{
			error = "no active scene or graphics context";
			return false;
		}
		const uint32_t width = m_RenderWidth;
		const uint32_t height = m_RenderHeight;
		if (width == 0 || height == 0)
		{
			error = "the window framebuffer is empty (minimized?)";
			return false;
		}
		// Re-render the frame that was just simulated into an offscreen target:
		// back-buffer readback is not portable across OpenGL and Vulkan, and an
		// offscreen target is immune to window occlusion and DPI capture issues.
		FramebufferSpecification specification;
		specification.Attachments = { FramebufferTextureFormat::RGBA8,
			FramebufferTextureFormat::Depth };
		specification.Width = width;
		specification.Height = height;
		const Ref<Framebuffer> target = Framebuffer::Create(specification);
		if (!target)
		{
			error = "could not create a capture framebuffer";
			return false;
		}
		std::vector<uint8_t> pixels;
		target->Bind();
		bool read = false;
		try
		{
			scene->OnRenderRuntime();
			read = target->ReadColorAttachment(0, pixels);
		}
		catch (...)
		{
			target->Unbind();
			throw;
		}
		target->Unbind();
		if (!read)
		{
			error = "framebuffer readback failed";
			return false;
		}
		// Screenshots are opaque, like what the window shows.
		for (size_t alpha = 3; alpha < pixels.size(); alpha += 4)
			pixels[alpha] = 255;
		const std::vector<uint8_t> png = EncodePngRGBABottomUp(width, height, pixels);
		if (png.empty())
		{
			error = "PNG encoding failed";
			return false;
		}
		std::error_code directoryError;
		if (m_ScreenshotPath.has_parent_path())
			std::filesystem::create_directories(m_ScreenshotPath.parent_path(), directoryError);
		return FileSystem::WriteFileAtomically(m_ScreenshotPath, std::string_view(
			reinterpret_cast<const char*>(png.data()), png.size()), error);
	}

	void PlayerRuntimeLayer::WriteRenderDiagnostics()
	{
		const std::string json = Renderer2D::GetSceneDiagnostics().ToJson(m_Frame);
		std::string error;
		if (!FileSystem::WriteFileAtomically(m_RenderDiagnosticsPath, json, error))
		{
			TC_Core_Warn("Render diagnostics could not be written: {0}", error);
			// Stop retrying every second after a persistent failure.
			m_RenderDiagnosticsPath.clear();
			Renderer2D::SetSceneDiagnosticsEnabled(false);
		}
	}

	void PlayerRuntimeLayer::Fail(const std::string& message, int exitCode)
	{
		TC_Core_Error("TomCat Player: {0}", message);
		if (m_SceneManager)
			m_SceneManager->Stop();
		m_SceneManager.reset();
		Scripting::ScriptEngine::Get().SetRuntime({});
		AssetManager::Get().UnmountCookedPackage();
		Application::Get().Close(exitCode);
	}

}
