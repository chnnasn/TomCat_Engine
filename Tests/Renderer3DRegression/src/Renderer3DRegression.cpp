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
#include <array>
#include <cstring>
#include <functional>
#include "TomCat/Asset/TextureArtifact.h"
#include <glad/glad.h>
#include <GLFW/glfw3.h>
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
    std::string oldDocument = output.c_str(); auto migrated = CreateRef<Scene>();
    Check(SceneArchiveCodec::Decode({oldDocument.begin(),oldDocument.end()}, migrated, "v1.scene", false), "Mesh v1 migration failed");
    const auto& oldMesh = migrated->FindEntityByUUID(cube.GetUUID()).GetComponent<MeshComponent>();
    Check(oldMesh.Metallic == 0 && oldMesh.Roughness == .5f && oldMesh.CastShadows, "Mesh migration defaults incorrect");
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
    std::vector<uint8_t> bytes; std::string error; TextureArtifactView view;
    Check(BuildTextureArtifact(HDRSource(), {}, "windows-x64", bytes, error), error);
    Check(ParseTextureArtifact(bytes, view, error), error);
    Check(view.Format == TextureArtifactFormat::RGBA32F && !view.SRGB && view.Mips.size() == 2, "HDR format/mips lost");
    float red = 0; std::memcpy(&red, view.Mips[0].Bytes.data(), sizeof(red)); Check(red == 8, "HDR highlight clipped");
    std::vector<uint8_t> preview;
    Check(DecompressTextureMip(view.Mips[0], view.Format, preview, error) && preview.size() == 16, "HDR preview failed");
    bytes.pop_back(); Check(!ParseTextureArtifact(bytes, view, error), "Truncated HDR accepted");
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
        && std::abs(cooked.Asset.GetParts()[0].Color[0] - 0.3f) < 0.001f, "Cooked model required authoring files or lost material");
    auto cookedSky = assets.LoadImportedArtifact(skyHandle); TextureArtifactView skyView;
    Check(cookedSky.Succeeded() && ParseTextureArtifact(cookedSky.Artifact.Bytes, skyView, error)
        && skyView.Format == TextureArtifactFormat::RGBA32F, "Cook lost HDR panorama dependency");
    assets.Shutdown();
    packed.pop_back();
    Check(!DecodeMeshArtifact(packed, mesh, error), "Truncated model artifact accepted");
}

static void TestLightingPixels(const Ref<Framebuffer>& buffer)
{
    SceneCamera camera; camera.SetViewportSize(128,128); camera.SetPerspective(glm::radians(60.0f),.1f,100);
    auto plane = Mesh::CreatePlane(), cube = Mesh::CreateCube();
    Renderer3D::Environment environment; environment.ShowSky = true; environment.AmbientIntensity = .03f;
    Renderer3D::Light light; light.Direction = glm::normalize(glm::vec3(.6f,.2f,1)); light.ShadowExtent = 8;
    Renderer3D::Surface surface;
    auto planeTransform = glm::translate(glm::mat4(1),glm::vec3(0,0,5)) * glm::rotate(glm::mat4(1),glm::pi<float>(),glm::vec3(0,1,0)) * glm::scale(glm::mat4(1),glm::vec3(4));
    std::vector<int> ids(128*128);
    auto render = [&](bool blocker, const std::vector<Renderer3D::Light>& lights) {
        buffer->Bind(); RenderCommand::Clear(); buffer->ClearAttachment(1,-1);
        Renderer3D::BeginScene(camera,glm::mat4(1)); Renderer3D::SetLighting(lights,environment); Renderer3D::SetSurface(surface);
        Renderer3D::DrawMesh(plane,planeTransform,nullptr,glm::vec4(.7f,.4f,.2f,1),false,200);
        if(blocker) Renderer3D::DrawMesh(cube,glm::translate(glm::mat4(1),glm::vec3(0,0,3)),nullptr,glm::vec4(1),false,201);
        Renderer3D::EndScene();
        glReadBuffer(GL_COLOR_ATTACHMENT1); glReadPixels(0,0,128,128,GL_RED_INTEGER,GL_INT,ids.data());
        std::vector<uint8_t> pixels(128*128*4); glReadBuffer(GL_COLOR_ATTACHMENT0);glReadPixels(0,0,128,128,GL_RGBA,GL_UNSIGNED_BYTE,pixels.data());
        return pixels;
    };
    auto shadowed = render(true,{light}); light.CastShadows = false; auto unshadowed = render(true,{light});
    int shadowPixels = 0;
    for(size_t i=0;i<ids.size();i++) if(ids[i]==200 && int(unshadowed[i*4])-int(shadowed[i*4])>15) shadowPixels++;
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
    Check(sky!=noSky && ids[0]==-1,"Sky background/picking failed");
    std::vector<uint8_t> hdr;std::string error;Check(BuildTextureArtifact(HDRSource(),{},"windows-x64",hdr,error),error);
    environment.Panorama=Texture2D::Create(hdr.data(),static_cast<uint32_t>(hdr.size()));
    Check(environment.Panorama && environment.Panorama->IsLoaded(),"HDR GPU upload failed");
    auto panorama=render(false,{});Check(panorama!=sky,"Panorama not sampled");
    glEnable(GL_BLEND);glEnable(GL_CULL_FACE);glDisable(GL_DEPTH_TEST);glDepthMask(GL_FALSE);glDepthFunc(GL_GREATER);
    render(false,{});
    GLboolean mask;GLint func;glGetBooleanv(GL_DEPTH_WRITEMASK,&mask);glGetIntegerv(GL_DEPTH_FUNC,&func);
    Check(glIsEnabled(GL_BLEND)&&glIsEnabled(GL_CULL_FACE)&&!glIsEnabled(GL_DEPTH_TEST)&&!mask&&func==GL_GREATER,"3D pass leaked GL state");
    glDisable(GL_CULL_FACE);glEnable(GL_DEPTH_TEST);glDepthMask(GL_TRUE);glDepthFunc(GL_LESS);
    Check(glGetError()==GL_NO_ERROR,"Lighting pass OpenGL error");
    std::cout<<"Sky, HDR, directional/point/spot lights, shadow ("<<shadowPixels<<" pixels), PBR and state tests passed\n";
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
    std::vector<uint8_t> pixels(960*540*3);glReadBuffer(GL_COLOR_ATTACHMENT0);glReadPixels(0,0,960,540,GL_RGB,GL_UNSIGNED_BYTE,pixels.data());
    std::ofstream image(path.string()+".ppm",std::ios::binary);image<<"P6\n960 540\n255\n";
    for(int y=539;y>=0;y--)image.write(reinterpret_cast<const char*>(pixels.data()+y*960*3),960*3);
    buffer->Unbind();Check(glGetError()==GL_NO_ERROR,"Demo rendering error");
}

static void TestRendering(const std::filesystem::path& demoPath = {}, bool primitives = false)
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
    Renderer::Shutdown();
    glfwDestroyWindow(window);
    glfwTerminate();
}

int main(int argc, char** argv)
{
    Log::Init();
    try {
        TestPrimitives();
        TestPersistence();
        TestModelArtifact();
        TestHDR();
        if (argc > 1 && std::string(argv[1]) == "--gpu") TestRendering();
        if (argc > 2 && std::string(argv[1]) == "--demo") TestRendering(argv[2]);
        if (argc > 2 && std::string(argv[1]) == "--primitives-demo") TestRendering(argv[2], true);
        std::cout << "Renderer3D regressions passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
