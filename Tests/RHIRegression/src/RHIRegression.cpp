#include "tcpch.h"
#include "TomCat/Core/Application.h"
#include "TomCat/Renderer/Renderer.h"
#include "TomCat/Renderer/Renderer2D.h"
#include "TomCat/Renderer/Framebuffer.h"
#include "TomCat/Renderer/Shader.h"
#include "TomCat/RHI/RenderDevice.h"
#include "TomCat/Asset/ShaderArtifact.h"
#include "TomCat/Asset/TextureArtifact.h"
#include "platform/Vulkan/VulkanDevice.h"
#include <fstream>
#include <functional>
#include <imgui.h>
#include <GLFW/glfw3.h>
#include <glm/gtc/matrix_transform.hpp>
#include <iostream>
#include <cmath>
#include <stdexcept>
#include <string_view>

using namespace TomCat;
namespace {
void Require(bool value, const char* message) { if (!value) throw std::runtime_error(message); }
struct BackendOptions {
    std::string Expected;
    bool SelectionOnly = false;
    bool InitializationOnly = false;
    bool SimulateLateVulkanUnavailable = false;
};
BackendOptions ParseBackendOptions(int argc, char** argv) {
    BackendOptions options;
    for (int i = 1; i < argc; ++i) {
        const std::string_view argument(argv[i]);
        if (argument == "--expect-backend" && i + 1 < argc) {
            options.Expected = argv[++i];
            if (options.Expected != "opengl" && options.Expected != "vulkan")
                throw std::invalid_argument("--expect-backend must be opengl or vulkan");
        }
        else if (argument == "--backend-selection-only") options.SelectionOnly = true;
        else if (argument == "--backend-initialization-only") options.InitializationOnly = true;
        else if (argument == "--simulate-late-vulkan-unavailable") options.SimulateLateVulkanUnavailable = true;
        else throw std::invalid_argument("Unknown or incomplete RHI regression option: " + std::string(argument));
    }
    if (options.SelectionOnly && options.InitializationOnly)
        throw std::invalid_argument("Backend selection-only and initialization-only modes are mutually exclusive");
    if (options.SelectionOnly && options.SimulateLateVulkanUnavailable)
        throw std::invalid_argument("Late Vulkan unavailability requires window initialization");
    return options;
}
void CheckBackend(const BackendOptions& options) {
    const std::string_view backend = RendererAPI::GetAPI() == RendererAPI::API::Vulkan ? "vulkan" : "opengl";
    if (!options.Expected.empty() && options.Expected != backend)
        throw std::runtime_error("Expected backend " + options.Expected + ", selected " + std::string(backend));
    std::cout << "Selected backend: " << backend << '\n';
}
glm::mat4 Quad(float x, float y, float width, float height) {
    return glm::translate(glm::mat4(1), glm::vec3(x, y, 0)) * glm::scale(glm::mat4(1), glm::vec3(width, height, 1));
}
std::array<uint8_t, 4> ReadColor(const Ref<Framebuffer>& target, int x, int y) {
    const auto& spec = target->GetSpecification();
    Require(x >= 0 && y >= 0 && uint32_t(x) < spec.Width && uint32_t(y) < spec.Height,
        "Color sample is outside the framebuffer");
    std::vector<uint8_t> pixels;
    Require(target->ReadColorAttachment(0, pixels), "RGBA8 framebuffer readback failed");
    Require(pixels.size() == size_t(spec.Width) * spec.Height * 4,
        "Framebuffer readback byte count does not match its dimensions");
    std::array<uint8_t, 4> pixel{};
    const size_t offset = (size_t(y) * spec.Width + size_t(x)) * 4;
    std::copy_n(pixels.data() + offset, pixel.size(), pixel.data());
    return pixel;
}
void CheckTextureSampling(const Ref<Framebuffer>& target, const Camera& camera) {
    // Top and left edges are transparent; bottom and right edges are opaque.
    // Magnification with linear/repeat used to leak the opposite edge into
    // cropped sprites. Exercise the real importer, GPU upload and draw path.
    std::vector<uint8_t> tga(18 + 16 * 16 * 4, 0);
    tga[2] = 2; tga[12] = 16; tga[14] = 16; tga[16] = 32; tga[17] = 0x28;
    for (size_t y = 0; y < 16; ++y)
        for (size_t x = 0; x < 16; ++x)
            if (y == 15 || x == 15)
                std::fill_n(tga.data() + 18 + (y * 16 + x) * 4, 4, uint8_t(255));
    auto draw = [&](const AssetImportSettings& sampling) {
        auto settings = sampling;
        settings["compression"] = "rgba8"; settings["generateMipmaps"] = "false"; settings["sRGB"] = "false";
        std::vector<uint8_t> artifact; std::string error;
        Require(BuildTextureArtifact(tga, settings, "editor", artifact, error), error.c_str());
        auto texture = Texture2D::Create(artifact.data(), artifact.size());
        Require(texture && texture->IsLoaded(), "sampling texture upload failed");
        target->Bind(); RenderCommand::SetClearColor({0, 0, 0, 1}); RenderCommand::Clear();
        Renderer2D::BeginScene(camera, glm::mat4(1));
        Renderer2D::DrawQuad(glm::mat4(1), texture, 1, glm::vec4(1), 125);
        Renderer2D::EndScene();
        return std::array{ReadColor(target, 48, 71)[0], ReadColor(target, 24, 48)[0], ReadColor(target, 48, 26)[0]};
    };
    const auto legacy = draw({});
    Require(legacy[0] > 5 && legacy[1] > 5, "legacy repeat sampling no longer wraps opposite edges");
    const auto clampV = draw({{"wrapV", "clamp"}});
    Require(clampV[0] == 0 && clampV[1] > 5, "vertical clamp changed U wrapping or leaked the bottom edge");
    const auto clamp = draw({{"wrapU", "clamp"}, {"wrapV", "clamp"}});
    Require(clamp[0] == 0 && clamp[1] == 0 && clamp[2] > 5 && clamp[2] < 245,
        "clamp failed to remove edge bleed while retaining linear interpolation");
    const auto nearest = draw({{"filterMode", "nearest"}, {"wrapU", "clamp"}, {"wrapV", "clamp"}});
    Require(nearest[0] == 0 && nearest[1] == 0 && nearest[2] > 250,
        "nearest/clamp did not preserve opaque pixel-art texels without edge bleed");
}
class UILayer final : public Layer {
public:
    Ref<Framebuffer> Target;
    Ref<Texture2D> Texture;
    bool Detached = false;
    ImVec2 DetachedPosition{1000, 100};
    ImVec2 DetachedSize{180, 120};
    float RequestedUiScale = 0.0f;
    std::function<void()> Update;
    void OnUpdate(Timestep) override { if (Update) Update(); }
    void OnImGuiRender() override {
        ImGui::SetNextWindowSize({220, 160}, ImGuiCond_Always);
        ImGui::Begin("RHI regression");
        ImGui::Image(reinterpret_cast<ImTextureID>(Target->GetColorAttachmentUITextureID()), {64, 64}, {0, 1}, {1, 0});
        ImGui::Image(reinterpret_cast<ImTextureID>(Texture->GetUITextureID()), {16, 16});
        if (RequestedUiScale > 0.0f) {
            auto* gui = Application::Get().GetImGuiLayer();
            const float previousFontSize = gui->GetBakedFontSize();
            Require(gui->SetUiScale(RequestedUiScale), "UI scale was not accepted");
            Require(gui->GetBakedFontSize() == previousFontSize, "Font atlas rebuilt during an active UI frame");
            RequestedUiScale = 0.0f;
        }
        ImGui::TextUnformatted("Font atlas rebuild regression");
        ImGui::End();
        if (Detached) {
            ImGui::SetNextWindowPos(DetachedPosition, ImGuiCond_Always);
            ImGui::SetNextWindowSize(DetachedSize, ImGuiCond_Always);
            ImGui::Begin("Detached RHI viewport");
            ImGui::Image(reinterpret_cast<ImTextureID>(Target->GetColorAttachmentUITextureID()), {64, 64}, {0, 1}, {1, 0});
            ImGui::End();
        }
    }
};
std::vector<uint8_t> SolidPixels(uint32_t width, uint32_t height, const std::array<uint8_t, 4>& color) {
    std::vector<uint8_t> pixels(size_t(width) * height * color.size());
    for (size_t i = 0; i < pixels.size(); i += color.size())
        std::copy(color.begin(), color.end(), pixels.begin() + i);
    return pixels;
}
void RequireColor(const std::array<uint8_t, 4>& actual, const std::array<uint8_t, 4>& expected, const char* message) {
    for (size_t i = 0; i < actual.size(); ++i)
        Require(std::abs(int(actual[i]) - int(expected[i])) <= 1, message);
}
void RunVulkanSubmissionRegression(Application& app, UILayer& ui, const Camera& camera,
    const FramebufferSpecification& spec) {
    if (Renderer::GetAPI() != RendererAPI::API::Vulkan) return;
    const auto device = VulkanDevice::Current();
    constexpr uint32_t frames = VulkanDevice::FramesInFlight * 4;
    std::array<Ref<Framebuffer>, frames> targets;
    for (auto& target : targets) target = Framebuffer::Create(spec);
    auto texture = Texture2D::Create(3, 5);

    // Every target survives several complete turns of the submission ring. Read
    // back only after both runs: per-frame readback would hide premature reuse of
    // vertex/uniform snapshots, descriptors, and texture staging memory.
    const auto before = device->SubmissionStats();
    VulkanSubmissionStats warmed;
    for (uint32_t pass = 0; pass < 2; ++pass) {
        for (uint32_t frame = 0; frame < frames; ++frame) {
            auto& target = targets[frame];
            target->Bind(); RenderCommand::SetClearColor({0, 0, 0, 1}); RenderCommand::Clear();
            auto pixels = SolidPixels(3, 5, {uint8_t(32 + frame * 7), 80, 160, 255});
            texture->SetData(pixels.data(), static_cast<uint32_t>(pixels.size()));
            std::fill(pixels.begin(), pixels.end(), 0);
            Renderer2D::BeginScene(camera, glm::mat4(1));
            Renderer2D::DrawQuad(Quad(-0.65f, 0, 0.55f, 0.7f), texture, 1, glm::vec4(1), 200 + frame);
            Renderer2D::Flush();
            texture->SetData(pixels.data(), static_cast<uint32_t>(pixels.size()));
            Renderer2D::DrawQuad(Quad(0, 0, 0.55f, 0.7f), texture, 1, glm::vec4(1), 300 + frame);
            Renderer2D::Flush();
            pixels = SolidPixels(3, 5, {180, uint8_t(48 + frame * 5), 32, 255});
            texture->SetData(pixels.data(), static_cast<uint32_t>(pixels.size()));
            std::fill(pixels.begin(), pixels.end(), 0);
            Renderer2D::DrawQuad(Quad(0.65f, 0, 0.55f, 0.7f), texture, 1, glm::vec4(1), 400 + frame);
            Renderer2D::Flush();
            // Small uploads between draw batches force repeated vertex/index/UBO
            // suballocations, including UBO alignment after 60-byte texture data.
            for (int batch = 0; batch < 40; ++batch) {
                Renderer2D::DrawQuad(Quad(-0.9f + (batch % 10) * 0.2f, -0.85f, 0.08f, 0.08f),
                    glm::vec4(1), 500 + batch);
                Renderer2D::Flush();
            }
            Renderer2D::EndScene();
            target->Unbind();
            Require(app.Tick(1.0f / 60), "Application stopped during asynchronous submission stress");
            Require(device->SubmissionStats().PeakInFlight <= VulkanDevice::FramesInFlight,
                "Vulkan exceeded its bounded submission ring");
        }
        if (pass == 0) warmed = device->SubmissionStats();
    }
    const auto steady = device->SubmissionStats();
    Require(steady.SubmissionCount >= before.SubmissionCount + frames * 2,
        "Asynchronous frame stress did not submit every frame");
    Require(steady.ExplicitWaitCount == before.ExplicitWaitCount,
        "Normal Vulkan drawing or presentation performed an explicit queue drain");
    Require(steady.UploadPageAllocations == warmed.UploadPageAllocations,
        "Steady-state small uploads did not reuse the warmed upload arenas");
    Require(steady.UploadBytes > warmed.UploadBytes, "Submission stress did not exercise upload arenas");
    std::cout << "Vulkan asynchronous stress: " << steady.SubmissionCount - before.SubmissionCount
        << " submissions, " << steady.ExplicitWaitCount - before.ExplicitWaitCount
        << " explicit waits, " << steady.UploadPageAllocations - warmed.UploadPageAllocations
        << " steady-state upload allocations, peak " << steady.PeakInFlight << " submission slots\n";

    for (uint32_t frame = 0; frame < frames; ++frame) {
        auto& target = targets[frame]; target->Bind();
        Require(target->ReadPixel(1, 16, 48) == 200 + int(frame), "Earlier frame vertex/descriptor snapshot was overwritten");
        Require(target->ReadPixel(1, 48, 48) == -1, "Transparent texture snapshot wrote picking data");
        Require(target->ReadPixel(1, 80, 48) == 400 + int(frame), "Later frame vertex/descriptor snapshot was overwritten");
        RequireColor(ReadColor(target, 16, 48), {uint8_t(32 + frame * 7), 80, 160, 255},
            "Texture update or upload arena reuse changed an earlier draw");
        RequireColor(ReadColor(target, 80, 48), {180, uint8_t(48 + frame * 5), 32, 255},
            "Texture upload did not snapshot its CPU source before returning");
        target->Unbind();
    }

    // A full Renderer2D batch uploads more than 5 MiB of vertices. Its pages
    // must also stay warm; a small-draw-only test misses large vertex churn.
    VulkanSubmissionStats fullBatchWarmed;
    for (uint32_t frame = 0; frame < 12; ++frame) {
        targets[0]->Bind(); RenderCommand::Clear();
        Renderer2D::BeginScene(camera, glm::mat4(1));
        for (int row = 0; row < 100; ++row)
            for (int column = 0; column < 200; ++column)
                Renderer2D::DrawQuad(Quad(-1.0f + (float(column) + 0.5f) * 0.01f,
                    -1.0f + (float(row) + 0.5f) * 0.02f, 0.0101f, 0.0201f), glm::vec4(1), 900 + frame);
        Renderer2D::EndScene(); targets[0]->Unbind();
        app.Tick(1.0f / 60);
        if (frame == 5) fullBatchWarmed = device->SubmissionStats();
    }
    Require(device->SubmissionStats().UploadPageAllocations == fullBatchWarmed.UploadPageAllocations,
        "Full Renderer2D batches reallocated large vertex pages after warmup");
    Require(device->SubmissionStats().ExplicitWaitCount == fullBatchWarmed.ExplicitWaitCount,
        "Full Renderer2D batches performed an explicit queue drain");
    targets[0]->Bind();
    Require(targets[0]->ReadPixel(1, 48, 48) == 911, "Full Renderer2D batch failed after submission slot reuse");
    targets[0]->Unbind();

    // More than 4 MiB exceeds the normal arena page; follow it with a small
    // transfer and aligned uniforms, then destroy the CPU payload before submit.
    auto largeTexture = Texture2D::Create(1025, 1025);
    auto largePixels = SolidPixels(1025, 1025, {24, 192, 64, 255});
    largeTexture->SetData(largePixels.data(), static_cast<uint32_t>(largePixels.size()));
    std::fill(largePixels.begin(), largePixels.end(), 0);
    auto smallPixels = SolidPixels(3, 5, {208, 48, 128, 255});
    texture->SetData(smallPixels.data(), static_cast<uint32_t>(smallPixels.size()));
    targets[0]->Bind(); RenderCommand::Clear(); Renderer2D::BeginScene(camera, glm::mat4(1));
    Renderer2D::DrawQuad(Quad(-0.5f, 0, 0.7f, 0.7f), largeTexture, 1, glm::vec4(1), 601);
    Renderer2D::Flush();
    Renderer2D::DrawQuad(Quad(0.5f, 0, 0.7f, 0.7f), texture, 1, glm::vec4(1), 602);
    Renderer2D::EndScene(); targets[0]->Unbind();
    for (uint32_t frame = 0; frame < frames; ++frame) app.Tick(1.0f / 60);
    targets[0]->Bind();
    RequireColor(ReadColor(targets[0], 24, 48), {24, 192, 64, 255}, "Oversized upload payload was corrupted");
    RequireColor(ReadColor(targets[0], 72, 48), {208, 48, 128, 255}, "Small upload after oversized allocation was corrupted");
    Require(targets[0]->ReadPixel(1, 24, 48) == 601 && targets[0]->ReadPixel(1, 72, 48) == 602,
        "Draw snapshots following oversized uploads failed");
    targets[0]->Unbind();

    // A deliberate mid-frame readback splits timestamp recording across command
    // buffers. The completed GPU time must still belong to this exact CPU frame.
    uint64_t splitFrame = 0;
    ui.Update = [&] {
        splitFrame = FrameProfiler::Get().ActiveFrame();
        targets[0]->Bind(); RenderCommand::Clear();
        Renderer2D::BeginScene(camera, glm::mat4(1));
        Renderer2D::DrawQuad(Quad(-0.5f, 0, 0.7f, 0.7f), glm::vec4(1), 701);
        Renderer2D::EndScene();
        Require(targets[0]->ReadPixel(1, 24, 48) == 701, "Mid-profile readback failed");
        Renderer2D::BeginScene(camera, glm::mat4(1));
        Renderer2D::DrawQuad(Quad(0.5f, 0, 0.7f, 0.7f), glm::vec4(1), 702);
        Renderer2D::EndScene(); targets[0]->Unbind();
    };
    app.Tick(1.0f / 60); ui.Update = {};
    for (uint32_t frame = 0; frame < frames; ++frame) app.Tick(1.0f / 60);
    RHI::RenderDevice::Get().WaitIdle(); app.Tick(1.0f / 60);
    if (Renderer::SupportsGpuProfiling()) {
        FrameProfile profile;
        Require(splitFrame != 0 && FrameProfiler::Get().ReadFrame(splitFrame, profile)
            && std::isfinite(profile.GpuMilliseconds) && profile.GpuMilliseconds >= 0,
            "Split GPU timestamp queries were lost or assigned to another frame");
    }
    targets[0]->Bind();
    Require(targets[0]->ReadPixel(1, 24, 48) == 701 && targets[0]->ReadPixel(1, 72, 48) == 702,
        "Drawing after mid-profile readback failed");
    targets[0]->Unbind();

    // Resize and destruction must protect work that was submitted without a
    // caller wait. These are permitted drain points, unlike the steady loop.
    auto disposable = Framebuffer::Create(spec);
    disposable->Bind(); RenderCommand::Clear(); Renderer2D::BeginScene(camera, glm::mat4(1));
    Renderer2D::DrawQuad(glm::mat4(1), largeTexture, 1, glm::vec4(1), 801);
    Renderer2D::EndScene(); disposable->Unbind(); app.Tick(1.0f / 60);
    Require(disposable->Resize(128, 128), "Resize while a submission was pending failed");
    disposable->Bind(); RenderCommand::Clear(); Renderer2D::BeginScene(camera, glm::mat4(1));
    Renderer2D::DrawQuad(glm::mat4(1), largeTexture, 1, glm::vec4(1), 802);
    Renderer2D::EndScene(); disposable->Unbind(); app.Tick(1.0f / 60);
    disposable.reset();
    targets[0]->Bind(); Renderer2D::BeginScene(camera, glm::mat4(1));
    Renderer2D::DrawQuad(glm::mat4(1), largeTexture, 1, glm::vec4(1), 803);
    Renderer2D::EndScene(); targets[0]->Unbind(); app.Tick(1.0f / 60);
    // Clear Renderer2D's CPU-side texture references before the final release.
    targets[0]->Bind(); Renderer2D::BeginScene(camera, glm::mat4(1)); Renderer2D::EndScene(); targets[0]->Unbind();
    largeTexture.reset(); texture.reset();
    for (uint32_t frame = 0; frame < frames; ++frame) app.Tick(1.0f / 60);
    targets[0]->Bind();
    Require(targets[0]->ReadPixel(1, 48, 48) == 803, "Destroying a submitted texture invalidated a retained draw");
    RequireColor(ReadColor(targets[0], 48, 48), {24, 192, 64, 255}, "Destroyed texture was released before its GPU consumers");
    targets[0]->Unbind();
}
void Run(const BackendOptions& options) {
    if (std::getenv("TC_RHI_TEST_VIEWPORTS")) _putenv_s("TC_IMGUI_VIEWPORTS", "1");
    WindowProps props("TomCat RHI regression", 320, 240);
    props.Visible = false; props.VSync = false;
    Application app(props, true);
    CheckBackend(options);
    ImGui::GetIO().IniFilename = nullptr;
    if (std::getenv("TC_RHI_TEST_VIEWPORTS")) ImGui::GetIO().ConfigFlags |= ImGuiConfigFlags_ViewportsEnable;
    else ImGui::GetIO().ConfigFlags &= ~ImGuiConfigFlags_ViewportsEnable;
    if (options.InitializationOnly) {
        for (int frame = 0; frame < 3; ++frame)
            Require(app.Tick(1.0f / 60), "Application stopped during backend initialization regression");
        RHI::RenderDevice::Get().WaitIdle();
        Require(RHI::RenderDevice::Get().ValidationErrors() == 0, "Vulkan validation reported initialization errors");
        std::cout << "Backend initialization regression passed\n";
        return;
    }
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
    Require(ReadColor(framebuffer, 0, 0) == std::array<uint8_t, 4>{0, 0, 0, 255},
        "Color readback did not return the cleared RGBA8 pixel");
    std::vector<uint8_t> invalidReadback{1, 2, 3, 4};
    Require(!framebuffer->ReadColorAttachment(1, invalidReadback) && invalidReadback.empty(),
        "Integer attachment readback must fail and clear its output");
    invalidReadback = {1, 2, 3, 4};
    Require(!framebuffer->ReadColorAttachment(2, invalidReadback) && invalidReadback.empty(),
        "Out-of-range color attachment readback must fail and clear its output");
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
    RunVulkanSubmissionRegression(app, *ui, camera, spec);
    // A display-scale change can arrive while UI draw commands still reference
    // the previous font atlas. Exercise deferred replacement on both backends,
    // then render again to use the new GPU font texture and its descriptors.
    auto* gui = app.GetImGuiLayer();
    const float originalUiScale = gui->GetUiScale();
    for (float scale : {1.0f, 1.5f, 2.0f, 1.0f}) {
        ui->RequestedUiScale = scale;
        app.Tick(1.0f / 60);
        app.Tick(1.0f / 60);
        const float expectedFontSize = std::round(gui->GetBaseFontSize() * scale);
        Require(std::abs(gui->GetBakedFontSize() - expectedFontSize) < 0.01f, "Display scale did not rebuild the font atlas");
        Require(ImGui::GetIO().Fonts->IsBuilt() && ImGui::GetIO().Fonts->TexID != 0, "Rebuilt font atlas has no GPU texture");
        Require(ImGui::GetIO().FontDefault && std::abs(ImGui::GetIO().FontDefault->FontSize - expectedFontSize) < 0.01f,
            "Default UI font did not follow display scale");
    }
    gui->SetUiScale(originalUiScale);
    app.Tick(1.0f / 60);
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
        Require(ImGui::GetPlatformIO().Viewports.Size > 1, "Detached platform viewport was not created");
        if (Renderer::GetAPI() == RendererAPI::API::Vulkan) {
            const auto device = VulkanDevice::Current();
            const auto beforeDetached = device->SubmissionStats();
            const bool wasRecording = FrameProfiler::Get().IsRecording();
            FrameProfiler::Get().SetRecording(false);
            // Keep the viewport dimensions fixed here: resize is an intentional
            // drain point and must not hide waits during ordinary platform UI.
            for (int i = 0; i < 12; ++i) app.Tick(1.0f / 60);
            FrameProfiler::Get().SetRecording(wasRecording);
            Require(device->SubmissionStats().ExplicitWaitCount == beforeDetached.ExplicitWaitCount,
                "Steady detached viewport frames performed an explicit queue drain");
            Require(device->SubmissionStats().SubmissionCount >= beforeDetached.SubmissionCount + 12,
                "Detached viewport frames without GPU profiling did not submit");
        }
        for (int i = 0; i < 24; ++i) {
            ui->DetachedPosition = {1000.0f + float(i * 2), 100.0f};
            if (i % 6 == 0) ui->DetachedSize = {180.0f + float((i / 6) % 2) * 40.0f, 120.0f + float((i / 6) % 3) * 24.0f};
            app.Tick(1.0f / 60);
        }
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
    source.insert(source.find("uniform mat4"), "layout(location=0) ");
    source.insert(source.find("uniform int"), "layout(location=4) ");
    std::vector<uint8_t> artifact; std::string error;
    if (!BuildShaderArtifact(std::span(reinterpret_cast<const uint8_t*>(source.data()), source.size()),
        "RHI.glsl", {}, "desktop", artifact, error))
        throw std::runtime_error(error);
    Require(IsShaderArtifactBundle(artifact), "Desktop shader cook did not produce a backend bundle");
    source.clear(); // Runtime consumes only the cooked bytes, without source compilation.
    std::span<const uint8_t> selected;
    if (!SelectShaderArtifact(artifact,
        Renderer::GetAPI() == RendererAPI::API::Vulkan ? ShaderArtifactTarget::Vulkan : ShaderArtifactTarget::OpenGL,
        selected, error)) throw std::runtime_error(error);
    auto shader = Shader::CreateFromArtifact("RHI.Artifact", selected, &error);
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
    for (const char* compression : {"BC3", "ETC2RGBA8", "ASTC4x4"}) {
        if (!BuildTextureArtifact(encoded, {{"compression",compression},{"generateMipmaps","true"}}, "windows-x64", artifact, error))
            throw std::runtime_error(std::string(compression) + " artifact build failed: " + error);
        auto importedTexture = Texture2D::Create(artifact.data(), artifact.size());
        Require(importedTexture && importedTexture->IsLoaded(), (std::string(compression) + " mipmapped texture artifact upload failed").c_str());
        RenderCommand::Clear(); Renderer2D::BeginScene(camera, glm::mat4(1));
        Renderer2D::DrawQuad(glm::mat4(1), importedTexture, 1, glm::vec4(1), 111); Renderer2D::EndScene();
        Require(framebuffer->ReadPixel(1,48,48) == 111, (std::string(compression) + " imported texture did not render").c_str());
    }

    CheckTextureSampling(framebuffer, camera);

    // Shared scene postprocessing must transform actual pixels on each backend,
    // preserve the picking attachment, and leave normal drawing usable afterward.
    RenderCommand::SetClearColor({0.8f, 0.2f, 0.1f, 0.5f}); RenderCommand::Clear();
    framebuffer->ClearAttachment(1, 122);
    RenderCommand::ApplyColorGrade(-1, 0, 0);
    const auto gray = ReadColor(framebuffer, 48, 48);
    Require(std::abs(int(gray[0]) - int(gray[1])) <= 1 && std::abs(int(gray[1]) - int(gray[2])) <= 1
        && gray[0] > 38 && gray[0] < 45, "Color grading exposure/saturation pixel mismatch");
    Require(std::abs(int(gray[3]) - 128) <= 1, "Color grading changed alpha");
    Require(framebuffer->ReadPixel(1,48,48) == 122, "Color grading overwrote entity picking");
    RenderCommand::SetClearColor({0.8f, 0.8f, 0.8f, 1}); RenderCommand::Clear();
    framebuffer->ClearAttachment(1, 123);
    RenderCommand::ApplyColorGrade(0, 1, 1);
    Require(ReadColor(framebuffer, 48, 48)[0] > 195 && ReadColor(framebuffer, 0, 0)[0] < 8,
        "Color grading vignette did not darken the corners");
    Require(framebuffer->ReadPixel(1,0,0) == 123, "Vignette overwrote entity picking");
    Renderer2D::BeginScene(camera, glm::mat4(1));
    Renderer2D::DrawQuad(glm::mat4(1), glm::vec4(1), 124); Renderer2D::EndScene();
    Require(framebuffer->ReadPixel(1,48,48) == 124 && ReadColor(framebuffer, 48, 48)[0] > 250,
        "Color grading did not restore normal color/picking writes");
    framebuffer->Unbind();
    FrameProfiler::Get().SetRecording(false);
    RHI::RenderDevice::Get().WaitIdle();
    Require(RHI::RenderDevice::Get().ValidationErrors() == 0, "Vulkan validation reported errors");
    std::cout << "RHI regression passed: " << RHI::RenderDevice::Get().Capabilities().Name << '\n';
}
}
int main(int argc, char** argv) {
    Log::Init();
    try {
        const auto options = ParseBackendOptions(argc, argv);
        if (options.SimulateLateVulkanUnavailable) {
            Require(RendererAPI::GetAPI() == RendererAPI::API::Vulkan,
                "Late Vulkan unavailability test requires a supported Vulkan device");
            // Selection has already succeeded, but device initialization must
            // now fail. This exercises window cleanup/recreation without a
            // production test hook or changing any system driver settings.
            Require(_putenv_s("TC_VULKAN_DEVICE", "TomCat-RHI-regression-no-such-device-9eaa4f2e") == 0,
                "Could not simulate late Vulkan device unavailability");
        }
        if (options.SelectionOnly) CheckBackend(options);
        else { std::filesystem::current_path("Editor/TomCatInut"); Run(options); }
        return 0;
    }
    catch (const std::exception& e) { std::cerr << "RHI regression failed: " << e.what() << '\n'; return 1; }
}
