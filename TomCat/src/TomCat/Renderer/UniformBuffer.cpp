#include "tcpch.h"
#include "UniformBuffer.h"
#include "TomCat/RHI/RenderDevice.h"
namespace TomCat {
Ref<UniformBuffer> UniformBuffer::Create(uint32_t size, uint32_t binding) { return RHI::RenderDevice::Get().CreateUniformBuffer(size, binding); }
}
