#include "tcpch.h"
#include "Framebuffer.h"
#include "TomCat/RHI/RenderDevice.h"
namespace TomCat {
Ref<Framebuffer> Framebuffer::Create(const FramebufferSpecification& spec) { return RHI::RenderDevice::Get().CreateFramebuffer(spec); }
}
