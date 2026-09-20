#include "tcpch.h"
#include "TomCat/Core/Application.h"
#include "TomCat/Renderer/Renderer.h"
#include "TomCat/Renderer/Renderer2D.h"
#include "TomCat/Renderer/Framebuffer.h"
#include "TomCat/Renderer/Shader.h"
#include "TomCat/RHI/RenderDevice.h"
#include "TomCat/Asset/ShaderArtifact.h"
#include "TomCat/Asset/TextureArtifact.h"
#include <fstream>
#include <imgui.h>
#include <GLFW/glfw3.h>
#include <glm/gtc/matrix_transform.hpp>
#include <iostream>
#include <stdexcept>

using namespace TomCat;
namespace {
void Require(bool value, const char* message) { if (!value) throw std::runtime_error(message); }
glm::mat4 Quad(float x, float y, float width, float height) {
    return glm::translate(glm::mat4(1), glm::vec3(x, y, 0)) * glm::scale(glm::mat4(1), glm::vec3(width, height, 1));
}
class UILayer final : public Layer {
public:
    Ref<Framebuffer> Target;
    Ref<Texture2D> Texture;
    bool Detached = false;
    void OnImGuiRender() override {
        ImGui::SetNextWindowSize({220, 160}, ImGuiCond_Always);
        ImGui::Begin("RHI regression");
        ImGui::Image(reinterpret_cast<ImTextureID>(Target->GetColorAttachmentUITextureID()), {64, 64}, {0, 1}, {1, 0});
        ImGui::Image(reinterpret_cast<ImTextureID>(Texture->GetUITextureID()), {16, 16});
        ImGui::End();
        if (Detached) {
            ImGui::SetNextWindowPos({1000, 100}, ImGuiCond_Always);
            ImGui::SetNextWindowSize({180, 120}, ImGuiCond_Always);
            ImGui::Begin("Detached RHI viewport");
            ImGui::Image(reinterpret_cast<ImTextureID>(Target->GetColorAttachmentUITextureID()), {64, 64}, {0, 1}, {1, 0});
            ImGui::End();
        }
    }
};
void Run() {
    if (std::getenv("TC_RHI_TEST_VIEWPORTS")) _putenv_s("TC_IMGUI_VIEWPORTS", "1");
    WindowProps props("TomCat RHI regression", 320, 240);
    props.Visible = false; props.VSync = false;
    Application app(props, true);
    ImGui::GetIO().IniFilename = nullptr;
    if (std::getenv("TC_RHI_TEST_VIEWPORTS")) ImGui::GetIO().ConfigFlags |= ImGuiConfigFlags_ViewportsEnable;
    else ImGui::GetIO().ConfigFlags &= ~ImGuiConfigFlags_ViewportsEnable;
    FramebufferSpecification spec;
    spec.Width = 96; spec.Height = 96;
    spec.Attachments = {FramebufferTextureFormat::RGBA8, FramebufferTextureFormat::RED_INTEGER, FramebufferTextureFormat::DEPTH24STENCIL8};
    auto framebuffer = Framebuffer::Create(spec);
    auto second = Framebuffer::Create(spec);
    Camera camera(glm::mat4(1));
    auto texture = Texture2D::Create(1, 1);
    uint32_t opaque = 0xffffffff;
    texture->SetData(&opaque, 4);
    framebuffer->Bind();
    RenderCommand::SetDepthTest(false);
    RenderCommand::SetClearColor({0, 0, 0, 1}); RenderCommand::Clear();
    framebuffer->ClearAttachment(1, -1);
    Renderer2D::BeginScene(camera, glm::mat4(1));
    Renderer2D::DrawQuad(Quad(-0.5f, 0, 0.7f, 0.7f), texture, 1, glm::vec4(1), 11);
    Renderer2D::Flush();
    Renderer2D::DrawQuad(Quad(0.5f, 0, 0.7f, 0.7f), glm::vec4(1), 22);
    Renderer2D::EndScene();
    Require(framebuffer->ReadPixel(1, 24, 48) == 11, "First batch was overwritten");
    Require(framebuffer->ReadPixel(1, 72, 48) == 22, "Second batch missing");
    Require(framebuffer->ReadPixel(1, 0, 0) == -1, "Integer clear incorrect");
    Require(framebuffer->ReadPixel(1, -1, 0) == -1, "Out-of-bounds picking incorrect");
    framebuffer->ClearAttachment(1, -1);
    Renderer2D::BeginScene(camera, glm::mat4(1));
    Renderer2D::DrawQuad(Quad(-0.5f, 0, 0.7f, 0.7f), texture, 1, glm::vec4(1), 33);
    Renderer2D::Flush();
    uint32_t transparent = 0; texture->SetData(&transparent, 4);
    Renderer2D::DrawQuad(Quad(0.5f, 0, 0.7f, 0.7f), texture, 1, glm::vec4(1), 44);
    Renderer2D::EndScene();
    Require(framebuffer->ReadPixel(1, 24, 48) == 33, "Texture update changed earlier draw");
    Require(framebuffer->ReadPixel(1, 72, 48) == -1, "Transparent texel wrote entity ID");
    texture->SetData(&opaque, 4);
    second->Bind(); RenderCommand::Clear();
    Renderer2D::BeginScene(camera, glm::translate(glm::mat4(1), glm::vec3(0.5f, 0, 0)));
    Renderer2D::DrawCircle(Quad(0.5f, 0, 0.8f, 0.8f), glm::vec4(1), 1, 0.005f, 55);
    Renderer2D::EndScene();
    Require(second->ReadPixel(1, 48, 48) == 55, "Circle/camera failed");
    framebuffer->Bind();
    Require(framebuffer->ReadPixel(1, 24, 48) == 33, "Targets share attachment data");
    second->Bind();
    Renderer2D::BeginScene(camera, glm::mat4(1)); Renderer2D::SetLineWidth(2);
    Renderer2D::DrawLine({-0.8f, 0.5f, 0}, {0.8f, 0.5f, 0}, glm::vec4(1), 66);
    Renderer2D::EndScene();
    Require(second->ReadPixel(1, 48, 72) == 66 || second->ReadPixel(1, 48, 71) == 66, "Line failed");
    second->Unbind();
    auto* ui = new UILayer; ui->Target = framebuffer; ui->Texture = texture; app.PushLayer(ui);
    FrameProfiler::Get().SetRecording(true);
    for (int i = 0; i < 4; ++i) app.Tick(1.0f / 60);
    RHI::RenderDevice::Get().WaitIdle(); app.Tick(1.0f / 60);
    Require(framebuffer->GetColorAttachmentUITextureID() != 0, "Missing framebuffer UI texture");
    Require(texture->GetUITextureID() != 0, "Missing texture UI descriptor");
    if (Renderer::SupportsGpuProfiling()) {
        auto summaries = FrameProfiler::Get().Summaries();
        Require(std::any_of(summaries.begin(), summaries.end(), [](auto& s) { return s.GpuMilliseconds >= 0; }), "GPU timestamp never completed");
    }
    for (uint32_t size : {128u, 64u, 96u}) {
        Require(framebuffer->Resize(size, size), "Resize failed");
        framebuffer->Bind(); RenderCommand::Clear(); Renderer2D::BeginScene(camera, glm::mat4(1));
        Renderer2D::DrawQuad(glm::mat4(1), glm::vec4(1), 77); Renderer2D::EndScene();
        Require(framebuffer->ReadPixel(1, size / 2, size / 2) == 77, "Resized target picking failed");
        framebuffer->Unbind(); app.Tick(1.0f / 60);
    }
    Require(!framebuffer->Resize(0, 64), "Invalid resize must preserve attachments");
    for (int size : {400, 280, 320}) {
        glfwSetWindowSize(static_cast<GLFWwindow*>(app.GetWindow().GetNativeWindow()), size, 240); app.Tick(1.0f / 60);
    }
    app.GetWindow().SetVSync(true); app.Tick(1.0f / 60);
    app.GetWindow().SetVSync(false); app.Tick(1.0f / 60);
    if (std::getenv("TC_RHI_TEST_VIEWPORTS")) {
        ImGui::GetIO().ConfigFlags |= ImGuiConfigFlags_ViewportsEnable; ui->Detached = true;
        for (int i = 0; i < 4; ++i) app.Tick(1.0f / 60);
        ui->Detached = false;
        for (int i = 0; i < 3; ++i) app.Tick(1.0f / 60);
    }
    if (Renderer::GetAPI() == RendererAPI::API::Vulkan) {
        auto flat = Shader::Create("Packages/Shaders/FlatColor.glsl");
        flat->SetMat4("u_ViewProjection", glm::mat4(1)); flat->SetFloat4("u_Color", glm::vec4(1));
    }
    // Exercise the optimized offline artifact path, including named uniform
    // reflection and different uniform snapshots in the same submission.
    std::string source =
        "#type vertex\n#version 450 core\nlayout(location=0) in vec3 a_Position;\n"
        "uniform mat4 u_Transform;\nvoid main(){gl_Position=u_Transform*vec4(a_Position,1);}\n"
        "#type fragment\n#version 450 core\nlayout(location=0) out vec4 color;\n"
        "layout(location=1) out int entity;\nuniform int u_Entity;\n"
        "void main(){color=vec4(1);entity=u_Entity;}\n";
    // OpenGL plain uniform locations share a namespace across shader stages.
    // Assign them explicitly instead of letting each stage start at location 0.
    if (Renderer::GetAPI() == RendererAPI::API::OpenGL) {
        source.insert(source.find("uniform mat4"), "layout(location=0) ");
        source.insert(source.find("uniform int"), "layout(location=4) ");
    }
    std::vector<uint8_t> artifact; std::string error;
    if (!BuildShaderArtifact(std::span(reinterpret_cast<const uint8_t*>(source.data()), source.size()),
        "RHI.glsl", {}, Renderer::GetAPI() == RendererAPI::API::Vulkan ? "vulkan" : "opengl", artifact, error))
        throw std::runtime_error(error);
    auto shader = Shader::CreateFromArtifact("RHI.Artifact", artifact, &error);
    Require(shader != nullptr, error.c_str());
    float vertices[] = {-0.5f,-0.5f,0, 0.5f,-0.5f,0, 0.5f,0.5f,0, -0.5f,0.5f,0};
    uint32_t indices[] = {0,1,2,2,3,0};
    auto vertexBuffer = VertexBuffer::Create(vertices, sizeof(vertices));
    vertexBuffer->SetLayout({{ShaderDataType::Float3, "a_Position"}});
    auto array = VertexArray::Create(); array->AddVertexBuffer(vertexBuffer);
    array->SetIndexBuffer(IndexBuffer::Create(indices, 6));
    framebuffer->Bind(); RenderCommand::Clear(); shader->Bind();
    shader->SetMat4("u_Transform", Quad(-0.5f,0,0.7f,0.7f)); shader->SetInt("u_Entity", 88);
    RenderCommand::DrawIndexed(array);
    shader->SetMat4("u_Transform", Quad(0.5f,0,0.7f,0.7f)); shader->SetInt("u_Entity", 99);
    RenderCommand::DrawIndexed(array);
    Require(framebuffer->ReadPixel(1,24,48) == 88 && framebuffer->ReadPixel(1,72,48) == 99, "Offline shader/named uniform snapshots failed");
    std::ifstream encodedFile("Packages/Resources/Sprites/TomCat/Square.tga", std::ios::binary);
    std::vector<uint8_t> encoded((std::istreambuf_iterator<char>(encodedFile)), {});
    if (!BuildTextureArtifact(encoded, {{"compression","BC3"},{"generateMipmaps","true"}}, "windows-x64", artifact, error))
        throw std::runtime_error(error);
    auto importedTexture = Texture2D::Create(artifact.data(), artifact.size());
    Require(importedTexture && importedTexture->IsLoaded(), "BC3 mipmapped texture artifact upload failed");
    RenderCommand::Clear(); Renderer2D::BeginScene(camera, glm::mat4(1));
    Renderer2D::DrawQuad(glm::mat4(1), importedTexture, 1, glm::vec4(1), 111); Renderer2D::EndScene();
    Require(framebuffer->ReadPixel(1,48,48) == 111, "Imported texture did not render");
    framebuffer->Unbind();
    FrameProfiler::Get().SetRecording(false);
    Require(RHI::RenderDevice::Get().ValidationErrors() == 0, "Vulkan validation reported errors");
    std::cout << "RHI regression passed: " << RHI::RenderDevice::Get().Capabilities().Name << '\n';
}
}
int main() {
    Log::Init();
    try { std::filesystem::current_path("Editor/TomCatInut"); Run(); return 0; }
    catch (const std::exception& e) { std::cerr << "RHI regression failed: " << e.what() << '\n'; return 1; }
}
