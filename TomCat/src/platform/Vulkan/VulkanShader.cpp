#include "tcpch.h"
#include "VulkanShader.h"
#include "TomCat/Asset/ShaderArtifact.h"
#include <spirv_cross/spirv_cross.hpp>
#include <fstream>
#include <stdexcept>
#include <sstream>

namespace TomCat {
VulkanShader::VulkanShader(const std::filesystem::path& path) : m_Device(VulkanDevice::Current()), m_Name(path.stem().string()) {
    std::ifstream input(path, std::ios::binary);
    if (!input) throw std::runtime_error("Cannot read shader " + path.string());
    std::string source((std::istreambuf_iterator<char>(input)), {});
    Compile(source);
}
VulkanShader::VulkanShader(const std::string& name, const std::string& vertex, const std::string& fragment)
    : m_Device(VulkanDevice::Current()), m_Name(name) { Compile("#type vertex\n" + vertex + "\n#type fragment\n" + fragment); }
VulkanShader::VulkanShader(const std::string& name, std::span<const uint8_t> artifact)
    : m_Device(VulkanDevice::Current()), m_Name(name) { Load(artifact); }
void VulkanShader::Compile(const std::string& source) {
    std::vector<uint8_t> artifact; std::string error;
    if (!BuildShaderArtifact(std::span(reinterpret_cast<const uint8_t*>(source.data()), source.size()),
        m_Name + ".glsl", {{"optimize", "false"}}, "vulkan", artifact, error)) throw std::runtime_error(error);
    Load(artifact);
}
void VulkanShader::Load(std::span<const uint8_t> bytes) {
    ShaderArtifactView artifact; std::string error;
    if (!ParseShaderArtifact(bytes, artifact, error)) throw std::runtime_error(error);
    if (artifact.Target != ShaderArtifactTarget::Vulkan) throw std::runtime_error("Shader artifact target does not match Vulkan");
    try {
        for (const auto& stage : artifact.Stages) {
            std::vector<uint32_t> code(stage.Spirv.size() / 4);
            std::memcpy(code.data(), stage.Spirv.data(), stage.Spirv.size());
            VkShaderModuleCreateInfo create{VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO};
            create.codeSize = stage.Spirv.size(); create.pCode = code.data();
            VkShaderModule module{};
            VulkanCheck(vkCreateShaderModule(m_Device->Device, &create, nullptr, &module), "Create shader module");
            m_Modules.push_back(module);
            VkPipelineShaderStageCreateInfo shader{VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO};
            shader.stage = stage.Stage == ShaderArtifactStage::Vertex ? VK_SHADER_STAGE_VERTEX_BIT : VK_SHADER_STAGE_FRAGMENT_BIT;
            shader.module = module; shader.pName = "main"; m_Stages.push_back(shader);
            spirv_cross::Compiler reflection(code);
            auto resources = reflection.get_shader_resources();
            if (!resources.storage_buffers.empty() || !resources.storage_images.empty() || !resources.push_constant_buffers.empty()
                || !resources.separate_images.empty() || !resources.separate_samplers.empty())
                throw std::runtime_error("The graphics RHI supports uniform buffers and combined sampled images; unsupported shader resource");
            auto reflect = [&](const spirv_cross::Resource& resource, VkDescriptorType type) {
                Binding binding;
                binding.Set = reflection.get_decoration(resource.id, spv::DecorationDescriptorSet);
                binding.Slot = reflection.get_decoration(resource.id, spv::DecorationBinding);
                binding.Type = type; binding.Stages = shader.stage;
                const auto& variable = reflection.get_type(resource.type_id);
                if (!variable.array.empty()) binding.Count = variable.array[0];
                if (!binding.Count || binding.Set > 2) throw std::runtime_error("Unsupported shader descriptor set/array size");
                if (type == VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER) {
                    const auto& block = reflection.get_type(resource.base_type_id);
                    binding.Size = static_cast<uint32_t>(reflection.get_declared_struct_size(block));
                    if (binding.Set == 2) {
                        binding.NamedData.resize(binding.Size);
                        for (uint32_t member = 0; member < block.member_types.size(); ++member) {
                            auto memberName = reflection.get_member_name(block.self, member);
                            const auto& memberType = reflection.get_type(block.member_types[member]);
                            binding.Members.emplace(memberName, Binding::Member{
                                reflection.type_struct_member_offset(block, member),
                                static_cast<uint32_t>(reflection.get_declared_struct_member_size(block, member)),
                                memberType.array.empty() ? 0u : reflection.type_struct_member_array_stride(block, member)});
                        }
                    }
                }
                auto existing = std::find_if(m_Bindings.begin(), m_Bindings.end(), [&](const Binding& b) { return b.Set == binding.Set && b.Slot == binding.Slot; });
                if (existing == m_Bindings.end()) m_Bindings.push_back(std::move(binding));
                else {
                    if (existing->Type != binding.Type || existing->Count != binding.Count || existing->Size != binding.Size)
                        throw std::runtime_error("Incompatible shader bindings across stages");
                    existing->Stages |= shader.stage;
                }
            };
            for (auto& resource : resources.uniform_buffers) reflect(resource, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER);
            for (auto& resource : resources.sampled_images) reflect(resource, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER);
        }
        uint32_t sets = 0;
        for (auto& binding : m_Bindings) sets = std::max(sets, binding.Set + 1);
        for (uint32_t set = 0; set < sets; ++set) {
            std::vector<VkDescriptorSetLayoutBinding> bindings;
            for (auto& b : m_Bindings) if (b.Set == set) bindings.push_back({b.Slot, b.Type, b.Count, b.Stages, nullptr});
            VkDescriptorSetLayoutCreateInfo create{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
            create.bindingCount = static_cast<uint32_t>(bindings.size()); create.pBindings = bindings.data();
            VkDescriptorSetLayout layout{};
            VulkanCheck(vkCreateDescriptorSetLayout(m_Device->Device, &create, nullptr, &layout), "Create descriptor layout");
            m_Layouts.push_back(layout);
        }
        VkPipelineLayoutCreateInfo layout{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
        layout.setLayoutCount = static_cast<uint32_t>(m_Layouts.size()); layout.pSetLayouts = m_Layouts.data();
        VulkanCheck(vkCreatePipelineLayout(m_Device->Device, &layout, nullptr, &m_Layout), "Create pipeline layout");
    } catch (...) { Destroy(); throw; }
}
void VulkanShader::Destroy() {
    for (auto& [key, pipeline] : m_Pipelines) vkDestroyPipeline(m_Device->Device, pipeline, nullptr);
    m_Pipelines.clear();
    if (m_Layout) vkDestroyPipelineLayout(m_Device->Device, m_Layout, nullptr);
    m_Layout = VK_NULL_HANDLE;
    for (auto layout : m_Layouts) vkDestroyDescriptorSetLayout(m_Device->Device, layout, nullptr);
    m_Layouts.clear();
    for (auto module : m_Modules) vkDestroyShaderModule(m_Device->Device, module, nullptr);
    m_Modules.clear();
}
VulkanShader::~VulkanShader() { m_Device->DrainForDestruction(); Unbind(); Destroy(); }
void VulkanShader::SetUniform(const std::string& name, const void* value, size_t size) {
    for (auto& binding : m_Bindings) {
        auto found = binding.Members.find(name);
        if (found == binding.Members.end()) continue;
        if (size > found->second.Size) throw std::out_of_range("Named uniform size mismatch: " + name);
        std::memcpy(binding.NamedData.data() + found->second.Offset, value, size);
    }
}
void VulkanShader::SetInt(const std::string& name, int value) { SetUniform(name, &value, sizeof(value)); }
void VulkanShader::SetFloat(const std::string& name, float value) { SetUniform(name, &value, sizeof(value)); }
void VulkanShader::SetFloat2(const std::string& name, const glm::vec2& value) { SetUniform(name, &value, sizeof(value)); }
void VulkanShader::SetFloat3(const std::string& name, const glm::vec3& value) { SetUniform(name, &value, sizeof(value)); }
void VulkanShader::SetFloat4(const std::string& name, const glm::vec4& value) { SetUniform(name, &value, sizeof(value)); }
void VulkanShader::SetMat4(const std::string& name, const glm::mat4& value) { SetUniform(name, &value, sizeof(value)); }
void VulkanShader::SetIntArray(const std::string& name, int* values, uint32_t count) {
    if (!values && count) throw std::invalid_argument("Null uniform array");
    if (name == "u_Textures") {
        for (uint32_t i = 0; i < count; ++i) if (values[i] != static_cast<int>(i)) throw std::invalid_argument("Texture slots must use consecutive bindings");
        return;
    }
    for (auto& binding : m_Bindings) {
        auto found = binding.Members.find(name);
        if (found == binding.Members.end()) continue;
        auto member = found->second;
        const uint32_t stride = member.ArrayStride ? member.ArrayStride : 4;
        if (count && uint64_t(count - 1) * stride + 4 > member.Size) throw std::out_of_range("Uniform array too large");
        for (uint32_t i = 0; i < count; ++i) std::memcpy(binding.NamedData.data() + member.Offset + i * stride, values + i, 4);
    }
}
namespace {
VkFormat VertexFormat(ShaderDataType type) {
    switch (type) {
    case ShaderDataType::Float: return VK_FORMAT_R32_SFLOAT;
    case ShaderDataType::Float2: return VK_FORMAT_R32G32_SFLOAT;
    case ShaderDataType::Float3: return VK_FORMAT_R32G32B32_SFLOAT;
    case ShaderDataType::Float4: return VK_FORMAT_R32G32B32A32_SFLOAT;
    case ShaderDataType::Int: return VK_FORMAT_R32_SINT;
    case ShaderDataType::Int2: return VK_FORMAT_R32G32_SINT;
    case ShaderDataType::Int3: return VK_FORMAT_R32G32B32_SINT;
    case ShaderDataType::Int4: return VK_FORMAT_R32G32B32A32_SINT;
    default: throw std::invalid_argument("Unsupported Vulkan vertex attribute type");
    }
}
}
VkPipeline VulkanShader::Pipeline(const VertexArray& array, VulkanFramebuffer& target, bool indexed) const {
    std::vector<VkVertexInputBindingDescription> bindings;
    std::vector<VkVertexInputAttributeDescription> attributes;
    std::ostringstream key;
    key << indexed << ':' << m_Device->DepthTest << ':';
    uint32_t location = 0;
    for (uint32_t i = 0; i < array.GetVertexBuffers().size(); ++i) {
        const auto& layout = array.GetVertexBuffers()[i]->GetLayout();
        bindings.push_back({i, layout.GetStride(), VK_VERTEX_INPUT_RATE_VERTEX});
        key << layout.GetStride() << '/';
        for (auto& element : layout) {
            const auto format = VertexFormat(element.Type);
            attributes.push_back({location++, i, format, element.Offset});
            key << format << ',' << element.Offset << ';';
        }
    }
    for (auto& color : target.Colors()) key << ':' << color->Format;
    key << ':' << (target.Depth() ? target.Depth()->Format : VK_FORMAT_UNDEFINED);
    if (auto found = m_Pipelines.find(key.str()); found != m_Pipelines.end()) return found->second;
    VkPipelineVertexInputStateCreateInfo vertex{VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO};
    vertex.vertexBindingDescriptionCount = static_cast<uint32_t>(bindings.size()); vertex.pVertexBindingDescriptions = bindings.data();
    vertex.vertexAttributeDescriptionCount = static_cast<uint32_t>(attributes.size()); vertex.pVertexAttributeDescriptions = attributes.data();
    VkPipelineInputAssemblyStateCreateInfo assembly{VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO};
    assembly.topology = indexed ? VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST : VK_PRIMITIVE_TOPOLOGY_LINE_LIST;
    VkPipelineViewportStateCreateInfo viewport{VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO};
    viewport.viewportCount = viewport.scissorCount = 1;
    VkPipelineRasterizationStateCreateInfo raster{VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO};
    raster.polygonMode = VK_POLYGON_MODE_FILL; raster.cullMode = VK_CULL_MODE_NONE;
    raster.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE; raster.lineWidth = 1;
    VkPipelineMultisampleStateCreateInfo multisample{VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO};
    multisample.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;
    VkPipelineDepthStencilStateCreateInfo depth{VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO};
    depth.depthTestEnable = depth.depthWriteEnable = m_Device->DepthTest && target.Depth(); depth.depthCompareOp = VK_COMPARE_OP_LESS;
    std::vector<VkPipelineColorBlendAttachmentState> blends;
    for (auto& color : target.Colors()) {
        VkPipelineColorBlendAttachmentState blend{};
        blend.colorWriteMask = 0xf; blend.blendEnable = color->Format != VK_FORMAT_R32_SINT;
        blend.srcColorBlendFactor = blend.srcAlphaBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
        blend.dstColorBlendFactor = blend.dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
        blend.colorBlendOp = blend.alphaBlendOp = VK_BLEND_OP_ADD;
        blends.push_back(blend);
    }
    VkPipelineColorBlendStateCreateInfo blend{VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO};
    blend.attachmentCount = static_cast<uint32_t>(blends.size()); blend.pAttachments = blends.data();
    VkDynamicState states[] = {VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR, VK_DYNAMIC_STATE_LINE_WIDTH};
    VkPipelineDynamicStateCreateInfo dynamic{VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO};
    dynamic.dynamicStateCount = 3; dynamic.pDynamicStates = states;
    VkGraphicsPipelineCreateInfo create{VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO};
    create.stageCount = static_cast<uint32_t>(m_Stages.size()); create.pStages = m_Stages.data();
    create.pVertexInputState = &vertex; create.pInputAssemblyState = &assembly; create.pViewportState = &viewport;
    create.pRasterizationState = &raster; create.pMultisampleState = &multisample; create.pDepthStencilState = &depth;
    create.pColorBlendState = &blend; create.pDynamicState = &dynamic; create.layout = m_Layout; create.renderPass = target.RenderPass();
    VkPipeline pipeline{};
    VulkanCheck(vkCreateGraphicsPipelines(m_Device->Device, VK_NULL_HANDLE, 1, &create, nullptr, &pipeline), "Create graphics pipeline");
    m_Pipelines.emplace(key.str(), pipeline); return pipeline;
}
void VulkanShader::Draw(const Ref<VertexArray>& array, uint32_t count, bool indexed) const {
    auto* target = m_Device->Target ? m_Device->Target : m_Device->DefaultTarget;
    if (!array || !target || !count) return;
    m_Device->PrepareDraw();
    auto pipeline = Pipeline(*array, *target, indexed);
    std::vector<VkBuffer> buffers;
    std::vector<VkDeviceSize> offsets;
    for (auto& vertex : array->GetVertexBuffers()) {
        auto* source = dynamic_cast<VulkanVertexBuffer*>(vertex.get());
        if (!source) throw std::invalid_argument("Mixed backend vertex buffer");
        auto data = source->Data();
        auto buffer = m_Device->Buffer(data.size(), VK_BUFFER_USAGE_VERTEX_BUFFER_BIT, data.data());
        buffers.push_back(buffer->Buffer); offsets.push_back(0); m_Device->Keep(buffer);
    }
    std::shared_ptr<VulkanBufferAllocation> indices;
    if (indexed) {
        auto* source = dynamic_cast<VulkanIndexBuffer*>(array->GetIndexBuffer().get());
        if (!source || count > source->GetCount()) throw std::invalid_argument("Invalid index buffer/count");
        auto data = source->Data();
        indices = m_Device->Buffer(data.size_bytes(), VK_BUFFER_USAGE_INDEX_BUFFER_BIT, data.data()); m_Device->Keep(indices);
    }
    std::vector<VkDescriptorSet> sets;
    for (auto layout : m_Layouts) sets.push_back(m_Device->AllocateDescriptor(layout));
    for (auto& binding : m_Bindings) {
        VkWriteDescriptorSet write{VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};
        write.dstSet = sets.at(binding.Set); write.dstBinding = binding.Slot;
        write.descriptorCount = binding.Count; write.descriptorType = binding.Type;
        VkDescriptorBufferInfo bufferInfo{};
        std::vector<VkDescriptorImageInfo> images;
        if (binding.Type == VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER) {
            const auto& bytes = binding.NamedData.empty() ? m_Device->Uniforms.at(binding.Slot) : binding.NamedData;
            if (bytes.size() < binding.Size || binding.Count != 1) throw std::runtime_error("Uniform buffer missing or too small");
            auto buffer = m_Device->Buffer(bytes.size(), VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, bytes.data());
            bufferInfo = {buffer->Buffer, 0, binding.Size}; write.pBufferInfo = &bufferInfo; m_Device->Keep(buffer);
        } else {
            for (uint32_t i = 0; i < binding.Count; ++i) {
                const uint32_t slot = binding.Slot + i;
                if (slot >= m_Device->Textures.size()) throw std::runtime_error("Shader texture array exceeds RHI capacity");
                auto texture = m_Device->Textures[slot] ? m_Device->Textures[slot] : m_Device->Textures[0];
                if (!texture || !texture->IsLoaded()) throw std::runtime_error("Shader requires an unbound texture");
                auto image = texture->NativeImage();
                images.push_back({image->Sampler, image->View, VK_IMAGE_LAYOUT_GENERAL}); m_Device->Keep(image);
            }
            write.pImageInfo = images.data();
        }
        vkUpdateDescriptorSets(m_Device->Device, 1, &write, 0, nullptr);
    }
    target->BeginPass();
    auto commands = m_Device->Commands();
    vkCmdBindPipeline(commands, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline);
    vkCmdSetViewport(commands, 0, 1, &m_Device->Viewport);
    VkRect2D scissor{{0, 0}, {target->GetSpecification().Width, target->GetSpecification().Height}};
    vkCmdSetScissor(commands, 0, 1, &scissor);
    const auto& limits = m_Device->Properties.limits;
    vkCmdSetLineWidth(commands, m_Device->Features.wideLines ? std::clamp(m_Device->LineWidth, limits.lineWidthRange[0], limits.lineWidthRange[1]) : 1.0f);
    if (!buffers.empty()) vkCmdBindVertexBuffers(commands, 0, static_cast<uint32_t>(buffers.size()), buffers.data(), offsets.data());
    if (!sets.empty()) vkCmdBindDescriptorSets(commands, VK_PIPELINE_BIND_POINT_GRAPHICS, m_Layout, 0, static_cast<uint32_t>(sets.size()), sets.data(), 0, nullptr);
    if (indexed) { vkCmdBindIndexBuffer(commands, indices->Buffer, 0, VK_INDEX_TYPE_UINT32); vkCmdDrawIndexed(commands, count, 1, 0, 0, 0); }
    else vkCmdDraw(commands, count, 1, 0, 0);
    vkCmdEndRenderPass(commands);
    FrameProfiler::Get().RecordDraw(count);
}
}
