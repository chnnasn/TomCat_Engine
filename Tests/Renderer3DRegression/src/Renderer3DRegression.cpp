#include "TomCat/Core/Log.h"
#include "TomCat/Core/Application.h"
#include "TomCat/RHI/RenderDevice.h"
#include "TomCat/Renderer/Renderer.h"
#include "TomCat/Renderer/Renderer2D.h"
#include "TomCat/Renderer/Renderer3D.h"
#include "TomCat/Renderer/Model.h"
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
#include "TomCat/Project/Project.h"
#include "TomCat/Utils/PathUtils.h"
#include <fstream>
#include <array>
#include <cstring>
#include <functional>
#include "TomCat/Asset/TextureArtifact.h"
#include <yaml-cpp/yaml.h>
#include <iostream>
#include <stdexcept>

using namespace TomCat;
static void Check(bool value, const std::string& message) { if (!value) throw std::runtime_error(message); }

static void TestPrimitives()
{
    auto scene = CreateRef<Scene>();
    for (int type = 1; type <= 6; ++type) {
        std::vector<Vertex> vertices; std::vector<uint32_t> indices;
        Check(Mesh::GeneratePrimitive(static_cast<MeshPrimitive>(type), vertices, indices), "Primitive generation failed");
        Check(!vertices.empty() && !indices.empty() && indices.size()%3 == 0, "Empty or incomplete primitive");
        glm::vec3 minimum(1e6f), maximum(-1e6f);
        for (const auto& vertex : vertices) {
            minimum = glm::min(minimum,vertex.Position); maximum = glm::max(maximum,vertex.Position);
            Check(std::isfinite(vertex.Position.x) && std::isfinite(vertex.Position.y) && std::isfinite(vertex.Position.z), "Nonfinite vertex");
            Check(std::abs(glm::length(vertex.Normal)-1.0f)<.0001f, "Non-unit primitive normal");
            Check(vertex.TexCoord.x>=0 && vertex.TexCoord.x<=1 && vertex.TexCoord.y>=0 && vertex.TexCoord.y<=1, "UV outside unit range");
        }
        for(size_t i=0;i<indices.size();i+=3) {
            Check(indices[i]<vertices.size() && indices[i+1]<vertices.size() && indices[i+2]<vertices.size(), "Invalid primitive index");
            const auto& a=vertices[indices[i]]; const auto& b=vertices[indices[i+1]]; const auto& c=vertices[indices[i+2]];
            glm::vec3 cross=glm::cross(b.Position-a.Position,c.Position-a.Position);
            Check(glm::length(cross)>1e-7f && glm::dot(cross,a.Normal+b.Normal+c.Normal)>0, "Degenerate or inward triangle");
        }
        glm::vec3 expected(.5f);
        if(type==2) expected.z=0;
        if(type==4 || type==5) expected.y=1;
        if(type==6) expected={5,0,5};
        Check(glm::length(maximum-expected)<.0001f && glm::length(minimum+expected)<.0001f, "Primitive bounds incorrect");
        auto entity=scene->CreateEntityWithUUID(UUID(8100+type), "Primitive "+std::to_string(type));
        entity.AddComponent<MeshComponent>();
        auto* descriptor=ComponentRegistry::Get().Find(UUID(ComponentIds::MeshRenderer)); std::string error;
        Check(descriptor->Properties[3].Set(entity,int32_t(type),error),error);
    }
    std::string document,error;Check(SceneArchiveCodec::Encode(scene,document,error),error);
    auto restored=CreateRef<Scene>();Check(SceneArchiveCodec::Decode({document.begin(),document.end()},restored,"primitives.scene",false),"Primitive roundtrip failed");
    auto copy=Scene::Copy(restored);
    for(int type=1;type<=6;++type) {
        auto entity=copy->FindEntityByUUID(UUID(8100+type));
        Check(entity.GetComponent<MeshComponent>().PrimitiveType==type,"Primitive type changed on save/play copy");
        PrefabArchive prefab;Check(PrefabArchiveCodec::CaptureSubtree(copy,entity,prefab,error),error);
        Scene destination;PrefabInstantiateOptions options;options.ResolveAssets=false;PrefabInstantiationResult result;
        Check(PrefabArchiveCodec::Instantiate(prefab,destination,options,result,error),error);
        Check(result.Root.GetComponent<MeshComponent>().PrimitiveType==type,"Prefab lost primitive type");
    }
    std::vector<Vertex> vertices;std::vector<uint32_t> indices;
    Check(!Mesh::GeneratePrimitive(MeshPrimitive::None,vertices,indices),"None generated geometry");
    Check(!Mesh::GeneratePrimitive(static_cast<MeshPrimitive>(99),vertices,indices),"Unknown primitive accepted");
}

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
    mesh.Metallic = .8f; mesh.Roughness = .2f; mesh.EmissionIntensity = 2;
    auto& light = cube.AddComponent<Light3D>(); light.Type = 2; light.Intensity = 12;
    auto& environment = cube.AddComponent<Environment3D>(); environment.Rotation = 45; environment.Panorama = AssetHandle(4244);
    std::string document, error;
    Check(SceneArchiveCodec::Encode(scene, document, error), error);
    auto restored = CreateRef<Scene>();
    Check(SceneArchiveCodec::Decode({document.begin(), document.end()}, restored, "mesh.scene", false), "Mesh scene decode failed");
    auto loaded = restored->FindEntityByUUID(cube.GetUUID());
    Check(loaded.HasComponent<MeshComponent>() && loaded.GetComponent<MeshComponent>().Color == mesh.Color, "Scene lost mesh properties");
    Check(loaded.GetComponent<MeshComponent>().Metallic == .8f && loaded.GetComponent<Light3D>().Intensity == 12
        && loaded.GetComponent<Environment3D>().Rotation == 45, "Scene lost PBR/light/environment properties");
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
    Check(result.Root.GetComponent<Light3D>().Type == 2 && result.Root.GetComponent<Environment3D>().Panorama == environment.Panorama, "Prefab lost 3D lighting");
    Check(scene->FindAssetReferences(environment.Panorama).size() == 1, "Environment live reference missing");
    int references = 0;
    Check(AssetReferenceVisitor::VisitScene(YAML::Load(document), [&](const SerializedAssetReference& reference) {
        if (reference.Handle == mesh.MeshHandle || reference.Handle == mesh.AlbedoHandle || reference.Handle == environment.Panorama) ++references;
        return true;
    }, error), error);
    Check(references == 3, "Cook dependency visitor lost mesh references");
    Check(scene->FindAssetReferences(mesh.MeshHandle).size() == 1 && scene->FindAssetReferences(mesh.AlbedoHandle).size() == 1, "Live asset references lost");
    const auto* descriptor = ComponentRegistry::Get().Find(UUID(ComponentIds::MeshRenderer));
    Check(descriptor && descriptor->ScriptAccessible, "Mesh missing script descriptor");
    const auto& primitive = descriptor->Properties[3];
    Check(!primitive.Set(cube, int32_t(8), error) && mesh.PrimitiveType == 1, "Invalid primitive accepted");
    Check(!descriptor->Properties[7].Set(cube, 0.0f, error), "Invalid roughness accepted");
    YAML::Node legacy = YAML::Load(document);
    std::function<void(YAML::Node)> downgrade = [&](YAML::Node node) {
        if (node.IsMap() && node["StableName"] && node["StableName"].as<std::string>() == "TomCat.MeshRenderer") {
            node["SchemaVersion"] = 1;
            YAML::Node properties(YAML::NodeType::Sequence);
            for (auto property : node["Properties"]) if (property["PropertyId"].as<uint64_t>() < 1606) properties.push_back(property);
            node["Properties"] = properties; return;
        }
        if (node.IsMap()) for(auto child : node) downgrade(child.second);
        else if(node.IsSequence()) for(auto child : node) downgrade(child);
    };
    downgrade(legacy); YAML::Emitter output; output << legacy;
    const std::string oldDocument = output.c_str();
    auto unsupported = CreateRef<Scene>();
    Check(!SceneArchiveCodec::Decode({oldDocument.begin(), oldDocument.end()}, unsupported, "v1.scene", false),
        "Unsupported Mesh schema 1 was accepted");
}

static std::vector<uint8_t> HDRSource()
{
    const std::string header = "#?RADIANCE\nFORMAT=32-bit_rle_rgbe\n\n-Y 2 +X 2\n";
    std::vector<uint8_t> bytes(header.begin(), header.end());
    for(int i=0;i<4;i++) bytes.insert(bytes.end(), {128, 64, 32, 132}); // Linear RGB = 8, 4, 2.
    return bytes;
}
static void TestHDR()
{
    static_assert(static_cast<uint32_t>(TextureArtifactFormat::BC1) == 3);
    static_assert(static_cast<uint32_t>(TextureArtifactFormat::BC5) == 4);
    static_assert(static_cast<uint32_t>(TextureArtifactFormat::ASTC4x4) == 5);
    static_assert(static_cast<uint32_t>(TextureArtifactFormat::ETC2RGBA8) == 6);
    static_assert(static_cast<uint32_t>(TextureArtifactFormat::RGBA32F) == 7);
    std::vector<uint8_t> bytes; std::string error; TextureArtifactView view;
    const AssetImportSettings settings{
        {"filterMode", "nearest"}, {"wrapU", "clamp"}, {"wrapV", "clamp"}};
    Check(BuildTextureArtifact(HDRSource(), settings, "editor", bytes, error), error);
    Check(ParseTextureArtifact(bytes, view, error), error);
    Check(view.Format == TextureArtifactFormat::RGBA32F && !view.SRGB && view.Mips.size() == 2, "HDR format/mips lost");
    Check(view.Sampling.Filter == TextureFilterMode::Nearest && view.Sampling.WrapU == TextureWrapMode::Clamp
        && view.Sampling.WrapV == TextureWrapMode::Clamp, "HDR nearest/clamp sampling settings were lost");
    float red = 0; std::memcpy(&red, view.Mips[0].Bytes.data(), sizeof(red)); Check(red == 8, "HDR highlight clipped");
    std::vector<uint8_t> preview;
    Check(DecompressTextureMip(view.Mips[0], view.Format, preview, error) && preview.size() == 16, "HDR preview failed");
    bytes.pop_back(); Check(!ParseTextureArtifact(bytes, view, error), "Truncated HDR accepted");
}

static const std::string ModelSource = R"MODEL({"asset":{"version":"2.0"},"buffers":[{"uri":"data:application/octet-stream;base64,AAAAvwAAAL8AAAAAAAAAPwAAAL8AAAAAAAAAAAAAAD8AAAAAAAABAAIA","byteLength":42}],"bufferViews":[{"buffer":0,"byteOffset":0,"byteLength":36},{"buffer":0,"byteOffset":36,"byteLength":6}],"accessors":[{"bufferView":0,"componentType":5126,"count":3,"type":"VEC3","min":[-0.5,-0.5,0],"max":[0.5,0.5,0]},{"bufferView":1,"componentType":5123,"count":3,"type":"SCALAR"}],"materials":[{"pbrMetallicRoughness":{"baseColorFactor":[0.3,0.5,0.7,1]}}],"meshes":[{"primitives":[{"attributes":{"POSITION":0},"indices":1,"material":0}]}],"nodes":[{"mesh":0,"translation":[0,0,2]}],"scenes":[{"nodes":[0]}],"scene":0})MODEL";

static void TestModelArtifact()
{
    const std::string& source = ModelSource;
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
        std::filesystem::path Root = std::filesystem::temp_directory_path()
            / UTF8ToPath("tomcat_3d_模型_" + std::to_string(static_cast<uint64_t>(UUID())));
        ~Fixture() { AssetManager::Get().Shutdown(); std::error_code ignored; std::filesystem::remove_all(Root, ignored); }
    } fixture;
    auto& assets = AssetManager::Get();
    assets.Shutdown();
    std::filesystem::create_directories(fixture.Root / "Assets");
    const auto modelPath = fixture.Root / "Assets" / UTF8ToPath("模型.gltf");
    std::string externalSource = source;
    const size_t bufferUri = externalSource.find("data:application/octet-stream;base64,");
    const size_t bufferUriEnd = externalSource.find('"', bufferUri);
    externalSource.replace(bufferUri, bufferUriEnd - bufferUri, "几何.bin");
    { std::ofstream file(modelPath); file << externalSource; }
    const auto writeGeometry = [&](float z)
    {
        const std::array<float, 9> positions{-0.5f, -0.5f, z, 0.5f, -0.5f, z, 0, 0.5f, z};
        const std::array<uint16_t, 3> indices{0, 1, 2};
        std::ofstream file(fixture.Root / "Assets" / UTF8ToPath("几何.bin"), std::ios::binary);
        file.write(reinterpret_cast<const char*>(positions.data()), sizeof(positions));
        file.write(reinterpret_cast<const char*>(indices.data()), sizeof(indices));
    };
    writeGeometry(0);
    Check(assets.Initialize(fixture.Root / "Assets", fixture.Root / "Library"), "Asset initialization failed");
    const auto modelHandle = assets.ImportAsset(modelPath);
    Check(static_cast<uint64_t>(modelHandle) != 0, "Model not registered");
    auto imported = assets.LoadMesh(modelHandle);
    Check(imported.Succeeded() && imported.Asset.DecodeVertex(0, vertex)
        && std::abs(vertex.Position[2] - 2.0f) < 0.001f,
        "Unicode model path or external buffer failed to import");
    writeGeometry(1);
    auto refreshed = assets.LoadMesh(modelHandle);
    Check(refreshed.Succeeded() && refreshed.Asset.DecodeVertex(0, vertex)
        && std::abs(vertex.Position[2] - 3.0f) < 0.001f,
        "External model buffer change reused stale geometry");
    auto hdr = HDRSource();
    { std::ofstream file(fixture.Root / "Assets/sky.hdr", std::ios::binary); file.write(reinterpret_cast<const char*>(hdr.data()), hdr.size()); }
    const auto skyHandle = assets.ImportAsset(fixture.Root / "Assets/sky.hdr");
    Check(static_cast<uint64_t>(skyHandle) != 0, "HDR registration failed");
    auto scene = CreateRef<Scene>();
    scene->CreateEntity("Sky").AddComponent<Environment3D>().Panorama = skyHandle;
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
        && std::abs(cooked.Asset.GetParts()[0].Color[0] - 0.3f) < 0.001f
        && cooked.Asset.DecodeVertex(0, vertex) && std::abs(vertex.Position[2] - 3.0f) < 0.001f,
        "Cooked model required authoring files or lost material/updated geometry");
    auto cookedSky = assets.LoadImportedArtifact(skyHandle); TextureArtifactView skyView;
    Check(cookedSky.Succeeded() && ParseTextureArtifact(cookedSky.Artifact.Bytes, skyView, error)
        && skyView.Format == TextureArtifactFormat::RGBA32F, "Cook lost HDR panorama dependency");
    assets.Shutdown();
    packed.pop_back();
    Check(!DecodeMeshArtifact(packed, mesh, error), "Truncated model artifact accepted");
}

static void TestSamples()
{
    struct Fixture
    {
        std::filesystem::path Root = std::filesystem::temp_directory_path()
            / ("tomcat_3d_samples_" + std::to_string(static_cast<uint64_t>(UUID())));
        ~Fixture()
        {
            AssetManager::Get().Shutdown();
            std::error_code ignored;
            std::filesystem::remove_all(Root, ignored);
        }
    } fixture;
    auto& assets = AssetManager::Get();
    for (const char* name : {"PBRLighting", "Primitives3D"})
    {
        const auto project = Project::Inspect(std::filesystem::path("Samples") / name / "Project.tcproj");
        Check(project && project->IsValid(), std::string("Sample project failed to load: ") + name);
        const auto root = fixture.Root / name;
        std::filesystem::create_directories(root);
        std::filesystem::copy(project->GetAssetPath(), root / "Assets", std::filesystem::copy_options::recursive);
        Check(assets.Initialize(root / "Assets", root / "Library"), "Sample asset initialization failed");
        const auto sceneHandle = project->GetBuildSettings().EntrySceneHandle;
        const auto source = assets.LoadImportedArtifact(sceneHandle);
        Check(source.Succeeded(), "Sample scene import failed: " + source.Error);
        auto scene = CreateRef<Scene>();
        Check(SceneArchiveCodec::Decode(source.Artifact.Bytes, scene, root / "sample.tomcat", false),
            std::string("Sample current-schema decode failed: ") + name);
        Check(!scene->GetRegistry().view<MeshComponent>().empty()
            && !scene->GetRegistry().view<Light3D>().empty()
            && !scene->GetRegistry().view<Environment3D>().empty(), "Sample lost its 3D components");
        const auto entityCount = scene->GetEntityOrder().size();
        const auto package = root / "sample.tcpak";
        Check(assets.CookToPackage(package, sceneHandle), "Sample cook failed");
        assets.Shutdown();
        std::filesystem::rename(root / "Assets", root / "AuthoringUnavailable");
        Check(assets.MountCookedPackage(package), "Sample package mount failed");
        const auto cooked = assets.LoadImportedArtifact(sceneHandle);
        Check(cooked.Succeeded(), "Sample packaged scene unavailable");
        auto restored = CreateRef<Scene>();
        Check(SceneArchiveCodec::Decode(cooked.Artifact.Bytes, restored, "packaged-sample.tomcat", false)
            && restored->GetEntityOrder().size() == entityCount, "Sample cook/load lost scene entities");
        assets.Shutdown();
    }
}

static void TestMeshBounds()
{
    glm::vec3 minimum, maximum;
    auto cube = Mesh::CreateCube();
    Check(cube->GetBounds(minimum, maximum) && minimum == glm::vec3(-0.5f) && maximum == glm::vec3(0.5f),
        "Cube local bounds are incorrect");
    auto plane = Mesh::CreateGroundPlane();
    Check(plane->GetBounds(minimum, maximum) && minimum == glm::vec3(-5, 0, -5)
        && maximum == glm::vec3(5, 0, 5), "Plane local bounds are incorrect");
    const std::vector<Vertex> vertices{
        {{-1, -2, 3}, {0, 0, 1}, {0, 0}},
        {{2, -2, 3}, {0, 0, 1}, {1, 0}},
        {{0, 4, 3}, {0, 0, 1}, {0, 1}},
        {{1000, 1000, 1000}, {0, 0, 1}, {0, 0}}};
    auto subset = Mesh::Create(vertices, {0, 1, 2});
    Check(subset->GetBounds(minimum, maximum) && minimum == glm::vec3(-1, -2, 3)
        && maximum == glm::vec3(2, 4, 3), "Unused vertices expanded the drawable mesh bounds");
    std::vector<uint8_t> packed;
    std::string error;
    Check(BuildMeshArtifact({reinterpret_cast<const uint8_t*>(ModelSource.data()), ModelSource.size()},
        "bounds.gltf", packed, error), error);
    MeshArtifact artifact;
    Check(DecodeMeshArtifact(packed, artifact, error), error);
    auto model = Model::FromArtifact(artifact);
    Check(model && model->GetBounds(minimum, maximum)
        && glm::length(minimum - glm::vec3(-0.5f, -0.5f, 2)) < 0.0001f
        && glm::length(maximum - glm::vec3(0.5f, 0.5f, 2)) < 0.0001f,
        "Imported model bounds lost the baked node transform");
}

static void TestLightingPixels(const Ref<Framebuffer>& buffer)
{
    SceneCamera camera; camera.SetViewportSize(128,128); camera.SetPerspective(glm::radians(60.0f),.1f,100);
    auto plane = Mesh::CreatePlane(), cube = Mesh::CreateCube();
    Renderer3D::Environment environment; environment.ShowSky = true; environment.AmbientIntensity = .03f;
    Renderer3D::Light light; light.Direction = glm::normalize(glm::vec3(.6f,.2f,1)); light.ShadowExtent = 8;
    Renderer3D::Surface surface;
    auto planeTransform = glm::translate(glm::mat4(1),glm::vec3(0,0,5)) * glm::rotate(glm::mat4(1),glm::pi<float>(),glm::vec3(0,1,0)) * glm::scale(glm::mat4(1),glm::vec3(4));
    auto render = [&](bool blocker, const std::vector<Renderer3D::Light>& lights) {
        buffer->Bind(); RenderCommand::Clear(); buffer->ClearAttachment(1,-1);
        Renderer3D::BeginScene(camera,glm::mat4(1)); Renderer3D::SetLighting(lights,environment); Renderer3D::SetSurface(surface);
        Renderer3D::DrawMesh(plane,planeTransform,nullptr,glm::vec4(.7f,.4f,.2f,1),false,200);
        if(blocker) Renderer3D::DrawMesh(cube,glm::translate(glm::mat4(1),glm::vec3(0,0,3)),nullptr,glm::vec4(1),false,201);
        Renderer3D::EndScene();
        std::vector<uint8_t> pixels;
        Check(buffer->ReadColorAttachment(0, pixels) && pixels.size() == 128 * 128 * 4,
            "3D color attachment readback failed");
        return pixels;
    };
    auto shadowed = render(true,{light}); light.CastShadows = false; auto unshadowed = render(true,{light});
    int shadowPixels = 0;
    for (size_t pixel = 0; pixel < 128 * 128; ++pixel)
    {
        if (int(unshadowed[pixel * 4]) - int(shadowed[pixel * 4]) > 15
            && buffer->ReadPixel(1, static_cast<int>(pixel % 128), static_cast<int>(pixel / 128)) == 200)
            ++shadowPixels;
    }
    Check(shadowPixels>20,"Directional shadow failed to darken receiver");
    surface.ReceiveShadows=false;light.CastShadows=true;auto noReceive=render(true,{light});
    Check(noReceive==unshadowed,"ReceiveShadows toggle failed");surface.ReceiveShadows=true;
    auto lit=render(false,{light}), dark=render(false,{});
    const size_t center=(64*128+64)*4;
    Check(lit[center]>dark[center]+20,"Directional light has no effect");
    light.Type=1; light.Position={0,0,2}; light.Intensity=30; auto point=render(false,{light});
    Check(point[center]>dark[center]+20,"Point light has no effect");
    light.Type=2;light.Direction={0,0,1};auto spot=render(false,{light});light.Direction={0,0,-1};auto away=render(false,{light});
    Check(spot[center]>away[center]+20,"Spot cone direction has no effect");
    environment.AmbientIntensity=1;surface.Metallic=0;auto dielectric=render(false,{});
    surface.Metallic=1;auto metal=render(false,{});Check(dielectric!=metal,"Metallic BRDF has no effect");
    surface.Roughness=.08f;auto smooth=render(false,{});surface.Roughness=.9f;auto rough=render(false,{});
    Check(smooth!=rough,"Environment roughness has no effect");
    environment.ShowSky=false;auto noSky=render(false,{});environment.ShowSky=true;auto sky=render(false,{});
    Check(sky!=noSky && buffer->ReadPixel(1, 0, 0)==-1,"Sky background/picking failed");
    std::vector<uint8_t> hdr;std::string error;Check(BuildTextureArtifact(HDRSource(),{},"windows-x64",hdr,error),error);
    environment.Panorama=Texture2D::Create(hdr.data(),static_cast<uint32_t>(hdr.size()));
    Check(environment.Panorama && environment.Panorama->IsLoaded(),"HDR GPU upload failed");
    auto panorama=render(false,{});Check(panorama!=sky,"Panorama not sampled");
    std::cout<<"Sky, HDR, directional/point/spot lights, shadow ("<<shadowPixels<<" pixels), PBR tests passed\n";
}

static void WriteDemo(const std::filesystem::path& path, bool primitives = false)
{
    auto scene = CreateRef<Scene>(); scene->SetSceneName("PBR Lighting");
    auto camera = scene->CreateEntityWithUUID(UUID(9001), "Camera");
    camera.AddComponent<C_Camera>()._Camera.SetPerspective(glm::radians(55.0f),.1f,100);
    camera.GetComponent<Transform>()._Translation = {0,3,-8};
    camera.GetComponent<Transform>()._Rotation = {glm::radians(15.0f),0,0};
    auto environment = scene->CreateEntityWithUUID(UUID(9002), "Sky and environment");
    environment.AddComponent<Environment3D>().AmbientIntensity = .7f;
    auto sun = scene->CreateEntityWithUUID(UUID(9003), "Sun - directional shadows");
    sun.AddComponent<Light3D>().ShadowExtent = 14;
    sun.GetComponent<Transform>()._Rotation = glm::radians(glm::vec3(50,-35,0));
    auto ground = scene->CreateEntityWithUUID(UUID(9004), "Ground");
    auto& floor = ground.AddComponent<MeshComponent>(); floor.PrimitiveType=2;floor.Roughness=.85f;floor.Color={.35f,.38f,.42f,1};
    ground.GetComponent<Transform>()._Rotation.x=-glm::half_pi<float>();
    ground.GetComponent<Transform>()._Translation={0,-.65f,3}; ground.GetComponent<Transform>()._Scale={18,18,1};
    for(int row=0;row<2;row++) for(int i=0;i<5;i++) {
        auto cube=scene->CreateEntityWithUUID(UUID(9010+row*5+i),std::string(row?"Metal ":"Dielectric ")+std::to_string(i+1));
        auto& mesh=cube.AddComponent<MeshComponent>();mesh.PrimitiveType=1;mesh.Metallic=float(row);mesh.Roughness=.08f+float(i)*.22f;
        mesh.Color=row?glm::vec4(.85f,.56f,.22f,1):glm::vec4(.13f,.38f,.65f,1);
        cube.GetComponent<Transform>()._Translation={float(i-2)*1.65f,0,float(row)*2.5f+1.0f};
        cube.GetComponent<Transform>()._Rotation.y=glm::radians(20.0f);
    }
    if (primitives) {
        scene->SetSceneName("Six Primitives");
        for(int row=0;row<2;++row) for(int i=0;i<5;++i) scene->DestroyEntity(scene->FindEntityByUUID(UUID(9010+row*5+i)));
        const char* names[]={"Cube","Sphere","Capsule","Cylinder","Plane","Quad"};
        const int types[]={1,3,4,5,6,2};
        for(int i=0;i<6;++i) {
            auto entity=scene->CreateEntityWithUUID(UUID(9050+i),names[i]);auto& mesh=entity.AddComponent<MeshComponent>();
            mesh.PrimitiveType=types[i];mesh.Roughness=.25f;mesh.Metallic=.25f;
            mesh.Color=glm::vec4(.15f+float(i)*.1f,.4f,.7f-float(i)*.06f,1);
            auto& t=entity.GetComponent<Transform>();t._Translation={float(i)*1.8f-4.5f,.4f,2};
            if(types[i]==6)t._Scale={.13f,1,.13f};
            if(types[i]==2)t._Rotation.y=glm::pi<float>();
        }
        camera.GetComponent<Transform>()._Translation={0,4,-10};
        camera.GetComponent<Transform>()._Rotation.x=glm::radians(18.0f);
    }
    std::string document,error; Check(SceneArchiveCodec::Encode(scene,document,error),error);
    std::filesystem::create_directories(path.parent_path()); {std::ofstream file(path);file<<document;}
    FramebufferSpecification spec;spec.Width=960;spec.Height=540;
    spec.Attachments={FramebufferTextureFormat::RGBA8,FramebufferTextureFormat::RED_INTEGER,FramebufferTextureFormat::Depth};
    auto buffer=Framebuffer::Create(spec);buffer->Bind();scene->OnViewportResize(960,540);scene->OnRenderRuntime();
    std::vector<uint8_t> pixels;
    Check(buffer->ReadColorAttachment(0, pixels), "Demo color readback failed");
    std::ofstream image(path.string()+".ppm",std::ios::binary);image<<"P6\n960 540\n255\n";
    for (int y = 539; y >= 0; --y)
        for (int x = 0; x < 960; ++x)
            image.write(reinterpret_cast<const char*>(pixels.data() + (y * 960 + x) * 4), 3);
    buffer->Unbind();
}

static void TestRendering(const std::filesystem::path& demoPath = {}, bool primitives = false,
    const std::string& expectedBackend = {})
{
    WindowProps props("3D Regression", 128, 128);
    props.Visible = false;
    props.VSync = false;
    Application application(props, false);
    const std::string backend = Renderer::GetAPI() == RendererAPI::API::Vulkan ? "vulkan" : "opengl";
    Check(expectedBackend.empty() || backend == expectedBackend,
        "Expected backend " + expectedBackend + ", selected " + backend);
    std::cout << "Selected backend: " << backend << '\n';
    TestMeshBounds();
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
        Check(cube.GetComponent<MeshComponent>().MeshAsset != nullptr, "Original scene did not resolve primitive GPU data");
        auto playCopy = Scene::Copy(scene);
        auto copiedCube = playCopy->FindEntityByUUID(cube.GetUUID());
        const auto& copiedMesh = copiedCube.GetComponent<MeshComponent>();
        Check(!copiedMesh.MeshAsset && !copiedMesh.Model && !copiedMesh.AlbedoTexture
            && static_cast<uint64_t>(copiedMesh.ResolvedHandle) == 0
            && copiedMesh.ResolvedPrimitive == -1 && copiedMesh.ImportRevision == ~uint64_t(0),
            "Scene copy retained mesh runtime caches");
        playCopy->OnViewportResize(128, 128);
        buffer->ClearAttachment(1, -1);
        playCopy->OnRenderRuntime();
        Check(buffer->ReadPixel(1, 64, 64) == static_cast<int>(static_cast<uint32_t>(copiedCube)),
            "Copied scene did not rebuild and render primitive GPU data");
        playCopy.reset();
        scene->OnRenderRuntime();
        Check(buffer->ReadPixel(1, 64, 64) == static_cast<int>(static_cast<uint32_t>(cube)),
            "Destroying the play copy invalidated the original mesh");
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
        RenderCommand::Clear();
        Renderer2D::BeginScene(ortho, glm::mat4(1));
        Renderer2D::DrawQuad(glm::translate(glm::mat4(1), glm::vec3(0, 0, 2)), glm::vec4(0, 1, 0, 1), 74);
        Renderer2D::EndScene();
        Check(buffer->ReadPixel(1, 64, 64) == 74, "Repeated 3D pass broke the following 2D draw");
        std::vector<uint8_t> mixedPixels;
        Check(buffer->ReadColorAttachment(0, mixedPixels), "Mixed 2D/3D color readback failed");
        const size_t center = (64 * 128 + 64) * 4;
        Check(mixedPixels[center] == 0 && mixedPixels[center + 1] == 255 && mixedPixels[center + 2] == 0,
            "3D state altered the following 2D color");
        for(int type=1;type<=6;++type) {
            cube.GetComponent<MeshComponent>().PrimitiveType=type;
            cube.GetComponent<Transform>()._Rotation.x=type==6?glm::half_pi<float>():0;
            scene->OnRenderRuntime();
            Check(buffer->ReadPixel(1,64,64)==static_cast<int>(static_cast<uint32_t>(cube)),"Primitive "+std::to_string(type)+" GPU rendering/picking failed");
        }
        TestLightingPixels(buffer);
        buffer->Unbind();
    }
    if (!demoPath.empty()) WriteDemo(demoPath, primitives);
    RHI::RenderDevice::Get().WaitIdle();
    Check(RHI::RenderDevice::Get().ValidationErrors() == 0, "3D rendering produced Vulkan validation errors");
}

int main(int argc, char** argv)
{
    Log::Init();
    try {
        TestPrimitives();
        TestPersistence();
        TestModelArtifact();
        TestHDR();
        TestSamples();
        bool gpu = false;
        bool primitives = false;
        std::filesystem::path demoPath;
        std::string expectedBackend;
        for (int index = 1; index < argc; ++index)
        {
            const std::string argument(argv[index]);
            if (argument == "--gpu")
                gpu = true;
            else if ((argument == "--demo" || argument == "--primitives-demo") && index + 1 < argc)
            {
                gpu = true;
                primitives = argument == "--primitives-demo";
                demoPath = argv[++index];
            }
            else if (argument == "--expect-backend" && index + 1 < argc)
            {
                expectedBackend = argv[++index];
                Check(expectedBackend == "opengl" || expectedBackend == "vulkan",
                    "--expect-backend must be opengl or vulkan");
            }
            else
                throw std::invalid_argument("Unknown or incomplete 3D regression option: " + argument);
        }
        Check(expectedBackend.empty() || gpu, "--expect-backend requires GPU rendering");
        if (gpu)
            TestRendering(demoPath, primitives, expectedBackend);
        std::cout << "Renderer3D regressions passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
