#include "tcpch.h"
#include "VertexArray.h"
#include "TomCat/RHI/RenderDevice.h"
namespace TomCat {
Ref<VertexArray> VertexArray::Create() { return RHI::RenderDevice::Get().CreateVertexArray(); }
}
