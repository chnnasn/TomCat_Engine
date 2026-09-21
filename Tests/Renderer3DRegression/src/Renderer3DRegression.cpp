#include "TomCat/Core/Log.h"
#include "TomCat/Renderer/Renderer.h"
#include "TomCat/Renderer/Renderer2D.h"
#include "TomCat/Renderer/Renderer3D.h"
#include "TomCat/Renderer/Framebuffer.h"
#include "TomCat/Renderer/RenderCommand.h"
#include "TomCat/Scene/Scene.h"
#include "TomCat/Scene/Entity.h"
#include "TomCat/Scene/Components.h"
#include "TomCat/Scene/ComponentRegistry.h"
#include "TomCat/Scene/Serialization/SceneArchiveCodec.h"
#include "TomCat/Scene/Serialization/PrefabArchiveCodec.h"
#include "TomCat/Scene/Serialization/AssetReferenceVisitor.h"
#include "TomCat/Asset/MeshArtifact.h"
#include "TomCat/Asset/AssetManager.h"
#include <fstream>
#include <glad/glad.h>
#include <GLFW/glfw3.h>
#include <yaml-cpp/yaml.h>
#include <iostream>
#include <stdexcept>

using namespace TomCat;
static void Check(bool value, const std::string& message) { if (!value) throw std::runtime_error(message); }

static void TestPersistence()
{
    auto scene = CreateRef<Scene>();
    auto cube = scene->CreateEntity("Cube");
    auto& mesh = cube.AddComponent<MeshComponent>();
    mesh.PrimitiveType = 1;
    mesh.Color = {0.2f, 0.4f, 0.6f, 1};
    mesh.MeshHandle = AssetHandle(4242);
    mesh.AlbedoHandle = AssetHandle(4243);
    mesh.UseTexture = true;
    std::string document, error;
    Check(SceneArchiveCodec::Encode(scene, document, error), error);
    auto restored = CreateRef<Scene>();
    Check(SceneArchiveCodec::Decode({document.begin(), document.end()}, restored, "mesh.scene", false), "Mesh scene decode failed");
    auto loaded = restored->FindEntityByUUID(cube.GetUUID());
    Check(loaded.HasComponent<MeshComponent>() && loaded.GetComponent<MeshComponent>().Color == mesh.Color, "Scene lost mesh properties");
    auto copied = Scene::Copy(restored);
    Check(copied->FindEntityByUUID(cube.GetUUID()).GetComponent<MeshComponent>().MeshHandle == mesh.MeshHandle, "Play copy lost mesh handle");
    PrefabArchive prefab;
    Check(PrefabArchiveCodec::CaptureSubtree(scene, cube, prefab, error), error);
    Scene destination;
    PrefabInstantiateOptions options;
    options.ResolveAssets = false;
    PrefabInstantiationResult result;
    Check(PrefabArchiveCodec::Instantiate(prefab, destination, options, result, error), error);
    Check(result.Root.GetComponent<MeshComponent>().AlbedoHandle == mesh.AlbedoHandle, "Prefab lost albedo");
    int references = 0;
    Check(AssetReferenceVisitor::VisitScene(YAML::Load(document), [&](const SerializedAssetReference& reference) {
        if (reference.Handle == mesh.MeshHandle || reference.Handle == mesh.AlbedoHandle) ++references;
        return true;
    }, error), error);
    Check(references == 2, "Cook dependency visitor lost mesh references");
    Check(scene->FindAssetReferences(mesh.MeshHandle).size() == 1 && scene->FindAssetReferences(mesh.AlbedoHandle).size() == 1, "Live asset references lost");
    const auto* descriptor = ComponentRegistry::Get().Find(UUID(ComponentIds::MeshRenderer));
    Check(descriptor && descriptor->ScriptAccessible, "Mesh missing script descriptor");
    const auto& primitive = descriptor->Properties[3];
    Check(!primitive.Set(cube, int32_t(8), error) && mesh.PrimitiveType == 1, "Invalid primitive accepted");
}

static void TestModelArtifact()
{
    const std::string source = R"MODEL({"asset":{"version":"2.0"},"buffers":[{"uri":"data:application/octet-stream;base64,AAAAvwAAAL8AAAAAAAAAPwAAAL8AAAAAAAAAAAAAAD8AAAAAAAABAAIA","byteLength":42}],"bufferViews":[{"buffer":0,"byteOffset":0,"byteLength":36},{"buffer":0,"byteOffset":36,"byteLength":6}],"accessors":[{"bufferView":0,"componentType":5126,"count":3,"type":"VEC3","min":[-0.5,-0.5,0],"max":[0.5,0.5,0]},{"bufferView":1,"componentType":5123,"count":3,"type":"SCALAR"}],"materials":[{"pbrMetallicRoughness":{"baseColorFactor":[0.3,0.5,0.7,1]}}],"meshes":[{"primitives":[{"attributes":{"POSITION":0},"indices":1,"material":0}]}],"nodes":[{"mesh":0,"translation":[0,0,2]}],"scenes":[{"nodes":[0]}],"scene":0})MODEL";
    std::vector<uint8_t> packed;
    std::string error;
    Check(BuildMeshArtifact({reinterpret_cast<const uint8_t*>(source.data()), source.size()}, "memory.gltf", packed, error), error);
    MeshArtifact mesh;
    Check(DecodeMeshArtifact(packed, mesh, error), error);
    Check(mesh.GetVertexCount() == 3 && mesh.GetIndexCount() == 3 && mesh.GetParts().size() == 1, "Model geometry/parts lost");
    MeshArtifactVertex vertex;
    Check(mesh.DecodeVertex(0, vertex) && std::abs(vertex.Position[2] - 2.0f) < 0.001f, "Model node transform lost");
    Check(std::abs(mesh.GetParts()[0].Color[0] - 0.3f) < 0.001f, "Model material color lost");

    struct Fixture {
        std::filesystem::path Root = std::filesystem::temp_directory_path() / ("tomcat_3d_" + std::to_string(static_cast<uint64_t>(UUID())));
        ~Fixture() { AssetManager::Get().Shutdown(); std::error_code ignored; std::filesystem::remove_all(Root, ignored); }
    } fixture;
    auto& assets = AssetManager::Get();
    assets.Shutdown();
    std::filesystem::create_directories(fixture.Root / "Assets");
    { std::ofstream file(fixture.Root / "Assets/model.gltf"); file << source; }
    Check(assets.Initialize(fixture.Root / "Assets", fixture.Root / "Library"), "Asset initialization failed");
    const auto modelHandle = assets.ImportAsset(fixture.Root / "Assets/model.gltf");
    Check(static_cast<uint64_t>(modelHandle) != 0, "Model not registered");
    auto scene = CreateRef<Scene>();
    scene->CreateEntity("Packed model").AddComponent<MeshComponent>().MeshHandle = modelHandle;
    std::string document;
    Check(SceneArchiveCodec::Encode(scene, document, error), error);
    { std::ofstream file(fixture.Root / "Assets/entry.tomcat"); file << document; }
    const auto sceneHandle = assets.ImportAsset(fixture.Root / "Assets/entry.tomcat");
    const auto package = fixture.Root / "game.tcpak";
    Check(assets.CookToPackage(package, sceneHandle), "3D scene cook failed");
    assets.Shutdown();
    std::filesystem::rename(fixture.Root / "Assets", fixture.Root / "AuthoringUnavailable");
    Check(assets.MountCookedPackage(package), "3D package mount failed");
    auto cooked = assets.LoadMesh(modelHandle);
    Check(cooked.Succeeded() && cooked.Asset.GetParts().size() == 1
        && std::abs(cooked.Asset.GetParts()[0].Color[0] - 0.3f) < 0.001f, "Cooked model required authoring files or lost material");
    assets.Shutdown();
    packed.pop_back();
    Check(!DecodeMeshArtifact(packed, mesh, error), "Truncated model artifact accepted");
}

static void TestRendering()
{
    Check(glfwInit() == GLFW_TRUE, "GLFW initialization failed");
    glfwWindowHint(GLFW_VISIBLE, GLFW_FALSE);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 4);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 6);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
    GLFWwindow* window = glfwCreateWindow(128, 128, "3D Regression", nullptr, nullptr);
    Check(window != nullptr, "OpenGL 4.6 context unavailable");
    glfwMakeContextCurrent(window);
    Check(gladLoadGLLoader(reinterpret_cast<GLADloadproc>(glfwGetProcAddress)), "GL loader failed");
    Renderer::Init();
    {
        FramebufferSpecification spec;
        spec.Width = spec.Height = 128;
        spec.Attachments = {FramebufferTextureFormat::RGBA8, FramebufferTextureFormat::RED_INTEGER, FramebufferTextureFormat::Depth};
        auto buffer = Framebuffer::Create(spec);
        buffer->Bind();
        auto scene = CreateRef<Scene>();
        auto camera = scene->CreateEntity("Camera");
        camera.AddComponent<C_Camera>()._Camera.SetPerspective(glm::radians(60.0f), 0.1f, 100.0f);
        auto cube = scene->CreateEntity("Cube");
        cube.AddComponent<MeshComponent>().PrimitiveType = 1;
        cube.GetComponent<Transform>()._Translation.z = 3;
        scene->OnViewportResize(128, 128);
        buffer->ClearAttachment(1, -1);
        scene->OnRenderRuntime();
        Check(buffer->ReadPixel(1, 64, 64) == static_cast<int>(static_cast<uint32_t>(cube)), "Game view did not render/pick cube");
        cube.GetComponent<MeshComponent>().Enabled = false;
        buffer->ClearAttachment(1, -1);
        scene->OnRenderRuntime();
        Check(buffer->ReadPixel(1, 64, 64) != static_cast<int>(static_cast<uint32_t>(cube)), "Disabled mesh rendered");
        cube.GetComponent<MeshComponent>().Enabled = true;
        SceneCamera ortho;
        ortho.SetOrthographic(4, 0, 10);
        buffer->ClearAttachment(1, -1);
        RenderCommand::Clear();
        Renderer2D::BeginScene(ortho, glm::mat4(1));
        Renderer2D::DrawQuad(glm::translate(glm::mat4(1), glm::vec3(0,0,2)), glm::vec4(1), 73);
        Renderer2D::EndScene();
        Check(buffer->ReadPixel(1, 64, 64) == 73, "2D camera binding broken after 3D");
        scene->OnRenderRuntime();
        Check(buffer->ReadPixel(1, 64, 64) == static_cast<int>(static_cast<uint32_t>(cube)), "3D camera binding broken after 2D");
        Check(glGetError() == GL_NO_ERROR, "OpenGL error in mixed rendering");
        buffer->Unbind();
    }
    Renderer::Shutdown();
    glfwDestroyWindow(window);
    glfwTerminate();
}

int main(int argc, char** argv)
{
    Log::Init();
    try {
        TestPersistence();
        TestModelArtifact();
        if (argc > 1 && std::string(argv[1]) == "--gpu") TestRendering();
        std::cout << "Renderer3D regressions passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
