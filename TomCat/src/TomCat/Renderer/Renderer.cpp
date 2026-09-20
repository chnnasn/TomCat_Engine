#include "tcpch.h"
#include "Renderer.h"
#include "Renderer2D.h"
#include "TomCat/RHI/RenderDevice.h"

namespace TomCat {

	void Renderer::Init()
	{
		TC_PROFILE_FUNCTION();

		try
		{
			RenderCommand::Init();
			Renderer2D::Init();
		}
		catch (...)
		{
			Renderer2D::Shutdown();
			RenderCommand::Shutdown();
			RHI::RenderDevice::Shutdown();
			throw;
		}
	}

	void Renderer::Shutdown()
	{
		RHI::RenderDevice::Get().ShutdownProfiling();
		Renderer2D::Shutdown();
		RenderCommand::Shutdown();
		RHI::RenderDevice::Shutdown();
	}

	void Renderer::BeginProfileFrame(uint64_t frame) { RHI::RenderDevice::Get().BeginProfileFrame(frame); }
	void Renderer::EndProfileFrame() { RHI::RenderDevice::Get().EndProfileFrame(); }
	bool Renderer::SupportsGpuProfiling() { return RHI::RenderDevice::Get().Capabilities().GpuTimestamps; }


	void Renderer::OnWindowResize(uint32_t width, uint32_t height)
	{
		RenderCommand::SetViewport(0, 0, width, height);
	}
}
