#include "tcpch.h"
#include "Buffer.h"
#include "TomCat/RHI/RenderDevice.h"
namespace TomCat {
Ref<VertexBuffer> VertexBuffer::Create(uint32_t size) { return RHI::RenderDevice::Get().CreateVertexBuffer(size, nullptr); }
Ref<VertexBuffer> VertexBuffer::Create(float* data, uint32_t size) { return RHI::RenderDevice::Get().CreateVertexBuffer(size, data); }
Ref<IndexBuffer> IndexBuffer::Create(uint32_t* data, uint32_t count) { return RHI::RenderDevice::Get().CreateIndexBuffer(data, count); }
}
