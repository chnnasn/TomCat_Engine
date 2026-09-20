#pragma once
#include "TomCat/Core/Base.h"
#include "TomCat/Renderer/Shader.h"
#include "VulkanResources.h"
#include <map>

namespace TomCat {
class VulkanShader final : public Shader {
public:
    explicit VulkanShader(const std::filesystem::path& path);
    VulkanShader(const std::string& name, const std::string& vertex, const std::string& fragment);
    VulkanShader(const std::string& name, std::span<const uint8_t> artifact);
    ~VulkanShader() override;
    void Bind() const override { m_Device->Shader = this; }
    void Unbind() const override { if (m_Device->Shader == this) m_Device->Shader = nullptr; }
    void SetInt(const std::string& name, int value) override;
    void SetIntArray(const std::string& name, int* values, uint32_t count) override;
    void SetFloat(const std::string& name, float value) override;
    void SetFloat2(const std::string& name, const glm::vec2& value) override;
    void SetFloat3(const std::string& name, const glm::vec3& value) override;
    void SetFloat4(const std::string& name, const glm::vec4& value) override;
    void SetMat4(const std::string& name, const glm::mat4& value) override;
    const std::string& GetName() const override { return m_Name; }
    void Draw(const Ref<VertexArray>& array, uint32_t count, bool indexed) const;
private:
    struct Binding {
        uint32_t Set = 0, Slot = 0, Count = 1, Size = 0;
        VkDescriptorType Type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        VkShaderStageFlags Stages = 0;
        std::vector<uint8_t> NamedData;
        struct Member { uint32_t Offset, Size, ArrayStride; };
        std::map<std::string, Member> Members;
    };
    void Compile(const std::string& source);
    void Load(std::span<const uint8_t> artifact);
    void Destroy();
    void SetUniform(const std::string& name, const void* value, size_t size);
    VkPipeline Pipeline(const VertexArray& array, VulkanFramebuffer& target, bool indexed) const;
    std::shared_ptr<VulkanDevice> m_Device;
    std::string m_Name;
    std::vector<VkShaderModule> m_Modules;
    std::vector<VkPipelineShaderStageCreateInfo> m_Stages;
    std::vector<VkDescriptorSetLayout> m_Layouts;
    std::vector<Binding> m_Bindings;
    VkPipelineLayout m_Layout = VK_NULL_HANDLE;
    mutable std::map<std::string, VkPipeline> m_Pipelines;
};
}
