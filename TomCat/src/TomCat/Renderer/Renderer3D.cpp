#include "tcpch.h"
#include "TomCat/Renderer/Renderer3D.h"
#include "TomCat/Renderer/RenderCommand.h"
#include "TomCat/Renderer/Shader.h"
#include "TomCat/Renderer/UniformBuffer.h"
#include "TomCat/Renderer/Model.h"
#include "platform/OpenGL/OpenGLApi.h"
#include <glm/gtc/matrix_inverse.hpp>
#include <glm/gtc/matrix_transform.hpp>

namespace TomCat
{
    namespace
    {
        constexpr int MaxLights = 16, ShadowSize = 2048;
        struct CameraData
        {
            glm::mat4 VP{1}, InverseVP{1};
            glm::vec4 Position{0};
        };
        struct MaterialData
        {
            glm::mat4 Model{1}, Normal{1};
            glm::vec4 Color{1}, Params{0, .5f, 1, 0}, Emission{0};
            glm::ivec4 Entity{-1, 1, 0, 0};
        };
        struct GPULight
        {
            glm::vec4 PositionType, DirectionRange, ColorIntensity, Cone;
        };
        struct LightingData
        {
            glm::mat4 ShadowVP{1};
            glm::vec4 Sky{.3f, .5f, .8f, 1}, Ground{.08f, .07f, .06f, .3f};
            glm::vec4 Environment{0, 1, 0, 0};                // rotation, exposure, panorama, unused
            glm::vec4 Shadow{0, .002f, 1.0f / ShadowSize, 0}; // enabled, bias, texel, light index
            glm::ivec4 Counts{0};
            GPULight Lights[MaxLights]{};
        };
        struct Submission
        {
            Ref<Mesh> Geometry;
            Ref<Texture2D> Texture;
            MaterialData Material;
            bool Cast;
        };
        struct Data
        {
            Ref<Shader> Lit, Depth, Sky;
            Ref<UniformBuffer> CameraUBO, MaterialUBO, LightingUBO;
            CameraData Camera;
            LightingData Lighting;
            Renderer3D::Environment Environment;
            Renderer3D::Surface Surface;
            std::vector<Renderer3D::Light> Lights;
            std::vector<Submission> Queue;
            Renderer3D::Statistics Stats;
            GLuint ShadowFBO = 0, ShadowTexture = 0, EmptyVAO = 0;
        } s;
        const std::string Blocks = R"GLSL(
layout(std140, binding=0) uniform Camera { mat4 u_VP; mat4 u_InverseVP; vec4 u_CameraPosition; };
layout(std140, binding=1) uniform Material {
 mat4 u_Model; mat4 u_Normal; vec4 u_Color; vec4 u_Params; vec4 u_Emission; ivec4 u_Entity;
};
struct Light { vec4 positionType; vec4 directionRange; vec4 colorIntensity; vec4 cone; };
layout(std140, binding=2) uniform Lighting {
 mat4 u_ShadowVP; vec4 u_Sky; vec4 u_Ground; vec4 u_Environment; vec4 u_Shadow;
 ivec4 u_Counts; Light u_Lights[16];
};
)GLSL";
        const std::string EnvironmentFunctions = R"GLSL(
layout(binding=1) uniform sampler2D u_Panorama;
const float PI=3.14159265359;
vec3 environment(vec3 d) {
 float c=cos(u_Environment.x), s=sin(u_Environment.x);
 d=vec3(c*d.x+s*d.z,d.y,-s*d.x+c*d.z);
 if(u_Environment.z>0.5) {
  vec2 uv=vec2(atan(d.z,d.x)/(2.0*PI)+0.5,asin(clamp(d.y,-1.0,1.0))/PI+0.5);
  return textureLod(u_Panorama,uv,0.0).rgb*u_Sky.a;
 }
 return mix(u_Ground.rgb,u_Sky.rgb,smoothstep(-0.2,0.6,d.y))*u_Sky.a;
}
vec3 displayColor(vec3 linearColor) {
 vec3 x=max(linearColor*u_Environment.y,vec3(0));
 // ACES fitted tone curve, followed by explicit display gamma (scene target is RGBA8).
 x=clamp((x*(2.51*x+0.03))/(x*(2.43*x+0.59)+0.14),0.0,1.0);
 return pow(x,vec3(1.0/2.2));
}
vec3 basisDirection(vec3 n,vec3 v) {
 vec3 up=abs(n.y)<0.99?vec3(0,1,0):vec3(1,0,0);
 vec3 t=normalize(cross(up,n)); return t*v.x+cross(n,t)*v.y+n*v.z;
}
)GLSL";
        const std::string MeshVertex = R"GLSL(
layout(location=0) in vec3 a_Position;
layout(location=1) in vec3 a_Normal;
layout(location=2) in vec2 a_UV;
layout(location=0) out vec3 v_Position;
layout(location=1) out vec3 v_Normal;
layout(location=2) out vec2 v_UV;
void main() {
 vec4 w=u_Model*vec4(a_Position,1);
 v_Position=w.xyz; v_Normal=mat3(u_Normal)*a_Normal; v_UV=a_UV;
 gl_Position=u_VP*w;
}
)GLSL";
        const std::string LitFragment = R"GLSL(
layout(location=0) in vec3 v_Position;
layout(location=1) in vec3 v_Normal;
layout(location=2) in vec2 v_UV;
layout(location=0) out vec4 color;
layout(location=1) out int entity;
layout(binding=0) uniform sampler2D u_Albedo;
layout(binding=2) uniform sampler2D u_ShadowMap;
vec3 fresnel(float h,vec3 f0) { return f0+(1.0-f0)*pow(1.0-clamp(h,0.0,1.0),5.0); }
float visibility(vec3 n,vec3 l) {
 if(u_Shadow.x<0.5 || u_Entity.y==0) return 1.0;
 vec4 clip=u_ShadowVP*vec4(v_Position,1);
 vec3 p=clip.xyz/clip.w*0.5+0.5;
 if(any(lessThan(p,vec3(0)))||any(greaterThan(p,vec3(1)))) return 1.0;
 float bias=u_Shadow.y*max(0.2,1.0-dot(n,l)), sum=0.0;
 for(int y=-1;y<=1;y++) for(int x=-1;x<=1;x++) {
  vec2 uv=p.xy+vec2(x,y)*u_Shadow.z;
  sum += (any(lessThan(uv,vec2(0)))||any(greaterThan(uv,vec2(1)))) ? 1.0 :
    (p.z-bias<=texture(u_ShadowMap,uv).r ? 1.0:0.0);
 }
 return sum/9.0;
}
void main() {
 vec4 base=u_Color;
 if(u_Params.w>0.5) base*=texture(u_Albedo,v_UV);
 vec3 n=normalize(v_Normal), v=normalize(u_CameraPosition.xyz-v_Position);
 float nv=max(dot(n,v),0.001), metal=clamp(u_Params.x,0.0,1.0), rough=clamp(u_Params.y,0.045,1.0);
 float a=rough*rough, a2=a*a;
 vec3 f0=mix(vec3(0.04),base.rgb,metal), result=vec3(0);
 for(int i=0;i<16;i++) {
  if(i>=u_Counts.x) break;
  Light light=u_Lights[i]; vec3 l=-light.directionRange.xyz; float atten=1.0;
  if(light.positionType.w>0.5) {
   vec3 delta=light.positionType.xyz-v_Position; float dist=length(delta);
   l=delta/max(dist,0.0001);
   float cutoff=pow(clamp(1.0-pow(dist/light.directionRange.w,4.0),0.0,1.0),2.0);
   atten=cutoff/max(dist*dist,0.01);
   if(light.positionType.w>1.5)
    atten*=smoothstep(light.cone.y,light.cone.x,dot(-l,light.directionRange.xyz));
  }
  float nl=max(dot(n,l),0.0); vec3 h=normalize(v+l);
  float nh=max(dot(n,h),0.0), hv=max(dot(h,v),0.0);
  float denom=nh*nh*(a2-1.0)+1.0;
  float d=a2/max(PI*denom*denom,0.000001);
  float k=(rough+1.0)*(rough+1.0)/8.0;
  float g=(nv/(nv*(1.0-k)+k))*(nl/(nl*(1.0-k)+k));
  vec3 f=fresnel(hv,f0);
  vec3 brdf=(1.0-f)*(1.0-metal)*base.rgb/PI + d*g*f/max(4.0*nv*nl,0.0001);
  float shadow=i==int(u_Shadow.w)?visibility(n,l):1.0;
  result+=brdf*light.colorIntensity.rgb*light.colorIntensity.a*nl*atten*shadow;
 }
 // Deterministic cosine / GGX importance sampling of the environment. No stale probe cache
 // when a panorama, its import revision, rotation or procedural sky changes in the editor.
 vec3 diffuse=vec3(0), specular=vec3(0);
 for(int i=0;i<32;i++) {
  float x=(float(i)+0.5)/32.0, y=fract(float(i)*0.61803398875);
  float phi=2.0*PI*y;
  vec3 l=basisDirection(n,vec3(sqrt(x)*cos(phi),sqrt(x)*sin(phi),sqrt(1.0-x)));
  diffuse+=environment(l);
  float ct=sqrt((1.0-x)/(1.0+(a2-1.0)*x)), st=sqrt(max(0.0,1.0-ct*ct));
  vec3 h=basisDirection(n,vec3(st*cos(phi),st*sin(phi),ct));
  l=reflect(-v,h);
  float nl=max(dot(n,l),0.0), nh=max(dot(n,h),0.0001), vh=max(dot(v,h),0.0);
  if(nl>0.0) {
   float k=a*0.5;
   float g=(nv/(nv*(1.0-k)+k))*(nl/(nl*(1.0-k)+k));
   specular+=environment(l)*fresnel(vh,f0)*g*vh/max(nh*nv,0.0001);
  }
 }
 vec3 f=fresnel(nv,f0);
 result+=((1.0-f)*(1.0-metal)*base.rgb*diffuse+specular)/32.0*u_Ground.a*u_Params.z;
 result+=u_Emission.rgb;
 color=vec4(displayColor(result),base.a); entity=u_Entity.x;
}
)GLSL";
        struct GLState
        {
            GLint DrawFBO, ReadFBO, Viewport[4], DepthFunc, Program, VAO, ActiveTexture, Textures[3];
            GLboolean Depth, Cull, Blend, DepthMask;
            GLState()
            {
                glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING, &DrawFBO);
                glGetIntegerv(GL_READ_FRAMEBUFFER_BINDING, &ReadFBO);
                glGetIntegerv(GL_VIEWPORT, Viewport);
                glGetIntegerv(GL_DEPTH_FUNC, &DepthFunc);
                glGetIntegerv(GL_CURRENT_PROGRAM, &Program);
                glGetIntegerv(GL_VERTEX_ARRAY_BINDING, &VAO);
                glGetIntegerv(GL_ACTIVE_TEXTURE, &ActiveTexture);
                for (int i = 0; i < 3; i++)
                {
                    glActiveTexture(GL_TEXTURE0 + i);
                    glGetIntegerv(GL_TEXTURE_BINDING_2D, &Textures[i]);
                }
                Depth = glIsEnabled(GL_DEPTH_TEST);
                Cull = glIsEnabled(GL_CULL_FACE);
                Blend = glIsEnabled(GL_BLEND);
                glGetBooleanv(GL_DEPTH_WRITEMASK, &DepthMask);
            }
            ~GLState()
            {
                glBindFramebuffer(GL_DRAW_FRAMEBUFFER, DrawFBO);
                glBindFramebuffer(GL_READ_FRAMEBUFFER, ReadFBO);
                glViewport(Viewport[0], Viewport[1], Viewport[2], Viewport[3]);
                glDepthFunc(DepthFunc);
                glDepthMask(DepthMask);
                auto restore = [](GLenum cap, GLboolean enabled) {
                    if (enabled)
                        glEnable(cap);
                    else
                        glDisable(cap);
                };
                restore(GL_DEPTH_TEST, Depth);
                restore(GL_CULL_FACE, Cull);
                restore(GL_BLEND, Blend);
                glUseProgram(Program);
                glBindVertexArray(VAO);
                for (int i = 0; i < 3; i++)
                {
                    glActiveTexture(GL_TEXTURE0 + i);
                    glBindTexture(GL_TEXTURE_2D, Textures[i]);
                }
                glActiveTexture(ActiveTexture);
            }
        };
        void Begin(const glm::mat4 &vp, const glm::vec3 &position)
        {
            s.Camera = {vp, glm::inverse(vp), glm::vec4(position, 1)};
            s.Queue.clear();
            s.Lights = {Renderer3D::Light{}};
            s.Environment = {};
            s.Surface = {};
        }
        void BindEnvironment(const Ref<Shader> &shader)
        {
            shader->SetInt("u_Panorama", 1);
            glActiveTexture(GL_TEXTURE1);
            glBindTexture(GL_TEXTURE_2D, s.Environment.Panorama ? s.Environment.Panorama->GetRendererID() : 0);
        }
    } // namespace
    void Renderer3D::Init()
    {
        if (s.Lit)
            return;
        GLState state;
        const std::string header = "#version 450 core\n";
        s.Lit = Shader::Create("TomCat.Renderer3D.PBR", header + Blocks + MeshVertex,
                               header + Blocks + EnvironmentFunctions + LitFragment);
        s.Depth = Shader::Create("TomCat.Renderer3D.Shadow", header + Blocks + R"GLSL(
 layout(location=0) in vec3 a_Position;
 void main(){gl_Position=u_ShadowVP*u_Model*vec4(a_Position,1);}
 )GLSL",
                                 header + "void main(){}\n");
        s.Sky = Shader::Create("TomCat.Renderer3D.Sky", header + R"GLSL(
 layout(location=0) out vec2 v_NDC;
 void main(){ vec2 p=vec2(float((gl_VertexIndex<<1)&2),float(gl_VertexIndex&2));
 v_NDC=p*2.0-1.0;gl_Position=vec4(v_NDC,1,1); }
 )GLSL",
                               header + Blocks + EnvironmentFunctions + R"GLSL(
 layout(location=0) in vec2 v_NDC;
 layout(location=0) out vec4 color; layout(location=1) out int entity;
 void main(){vec4 a=u_InverseVP*vec4(v_NDC,-1,1), b=u_InverseVP*vec4(v_NDC,1,1);
 vec3 d=normalize(b.xyz/b.w-a.xyz/a.w);color=vec4(displayColor(environment(d)),1);entity=-1;}
 )GLSL");
        s.CameraUBO = UniformBuffer::Create(sizeof(CameraData), 0);
        s.MaterialUBO = UniformBuffer::Create(sizeof(MaterialData), 1);
        s.LightingUBO = UniformBuffer::Create(sizeof(LightingData), 2);
        glGenVertexArrays(1, &s.EmptyVAO);
        glGenTextures(1, &s.ShadowTexture);
        glBindTexture(GL_TEXTURE_2D, s.ShadowTexture);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_DEPTH_COMPONENT24, ShadowSize, ShadowSize, 0, GL_DEPTH_COMPONENT,
                     GL_UNSIGNED_INT, nullptr);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        glGenFramebuffers(1, &s.ShadowFBO);
        glBindFramebuffer(GL_FRAMEBUFFER, s.ShadowFBO);
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_TEXTURE_2D, s.ShadowTexture, 0);
        const GLenum none = GL_NONE;
        glDrawBuffers(1, &none);
        glReadBuffer(GL_NONE);
        if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE)
            throw std::runtime_error("3D shadow framebuffer is incomplete");
    }
    void Renderer3D::Shutdown()
    {
        if (s.ShadowFBO)
            glDeleteFramebuffers(1, &s.ShadowFBO);
        if (s.ShadowTexture)
            glDeleteTextures(1, &s.ShadowTexture);
        if (s.EmptyVAO)
            glDeleteVertexArrays(1, &s.EmptyVAO);
        s = {};
    }
    void Renderer3D::BeginScene(const EditorCamera &camera)
    {
        Init();
        Begin(camera.GetViewProjection(), camera.GetPosition());
    }
    void Renderer3D::BeginScene(const Camera &camera, const glm::mat4 &transform)
    {
        Init();
        Begin(camera.GetProjection() * glm::inverse(transform), glm::vec3(transform[3]));
    }
    void Renderer3D::SetLighting(const std::vector<Light> &lights, const Environment &environment)
    {
        s.Lights = lights;
        s.Environment = environment;
    }
    void Renderer3D::SetSurface(const Surface &surface)
    {
        s.Surface = surface;
    }
    void Renderer3D::EndScene()
    {
        Flush();
    }
    void Renderer3D::Flush()
    {
        if (!s.Lit)
            return;
        GLState state;
        glEnable(GL_DEPTH_TEST);
        glDepthFunc(GL_LESS);
        glDepthMask(GL_TRUE);
        glDisable(GL_CULL_FACE);
        glDisable(GL_BLEND);
        s.Lighting = {};
        auto &b = s.Lighting;
        const auto &env = s.Environment;
        b.Sky = glm::vec4(env.SkyColor, env.Intensity);
        b.Ground = glm::vec4(env.GroundColor, env.AmbientIntensity);
        b.Environment = {glm::radians(env.Rotation), env.Exposure, env.Panorama ? 1.0f : 0.0f, 0};
        b.Counts.x = static_cast<int>(std::min(s.Lights.size(), size_t(MaxLights)));
        int shadow = -1;
        for (int i = 0; i < b.Counts.x; i++)
        {
            const auto &l = s.Lights[i];
            auto &g = b.Lights[i];
            glm::vec3 direction = glm::length(l.Direction) > 1e-6f ? glm::normalize(l.Direction) : glm::vec3(0, -1, 0);
            g.PositionType = glm::vec4(l.Position, float(l.Type));
            g.DirectionRange = glm::vec4(direction, std::max(l.Range, 0.01f));
            g.ColorIntensity = glm::vec4(l.Color, std::max(l.Intensity, 0.0f));
            float outer = glm::clamp(l.OuterAngle, 0.1f, 89.0f), inner = glm::clamp(l.InnerAngle, 0.0f, outer - 0.01f);
            g.Cone = {cos(glm::radians(inner)), cos(glm::radians(outer)), 0, 0};
            if (shadow < 0 && l.Type == 0 && l.CastShadows && l.Intensity > 0)
            {
                shadow = i;
                float extent = std::max(l.ShadowExtent, 1.0f);
                glm::vec3 center = glm::vec3(s.Camera.Position);
                glm::vec3 up = std::abs(direction.y) > .99f ? glm::vec3(1, 0, 0) : glm::vec3(0, 1, 0);
                b.ShadowVP = glm::ortho(-extent, extent, -extent, extent, 0.1f, extent * 4.0f) *
                             glm::lookAt(center - direction * extent * 2.0f, center, up);
                b.Shadow = {1, std::max(l.ShadowBias, 0.0f), 1.0f / ShadowSize, float(i)};
            }
        }
        s.CameraUBO->SetData(&s.Camera, sizeof(s.Camera));
        s.LightingUBO->SetData(&b, sizeof(b));
        if (shadow >= 0 && !s.Queue.empty())
        {
            glBindFramebuffer(GL_FRAMEBUFFER, s.ShadowFBO);
            glViewport(0, 0, ShadowSize, ShadowSize);
            const float clearDepth = 1.0f;
            glClearBufferfv(GL_DEPTH, 0, &clearDepth);
            s.Depth->Bind();
            for (const auto &item : s.Queue)
                if (item.Cast)
                {
                    s.MaterialUBO->SetData(&item.Material, sizeof(MaterialData));
                    item.Geometry->GetVertexArray()->Bind();
                    RenderCommand::DrawIndexed(item.Geometry->GetVertexArray(), item.Geometry->GetIndexCount());
                    s.Stats.DrawCalls++;
                }
        }
        glBindFramebuffer(GL_DRAW_FRAMEBUFFER, state.DrawFBO);
        glBindFramebuffer(GL_READ_FRAMEBUFFER, state.ReadFBO);
        glViewport(state.Viewport[0], state.Viewport[1], state.Viewport[2], state.Viewport[3]);
        if (env.ShowSky)
        {
            glDepthFunc(GL_LEQUAL);
            glDepthMask(GL_FALSE);
            s.Sky->Bind();
            BindEnvironment(s.Sky);
            glBindVertexArray(s.EmptyVAO);
            glDrawArrays(GL_TRIANGLES, 0, 3);
            s.Stats.DrawCalls++;
            glDepthFunc(GL_LESS);
            glDepthMask(GL_TRUE);
        }
        s.Lit->Bind();
        BindEnvironment(s.Lit);
        s.Lit->SetInt("u_Albedo", 0);
        s.Lit->SetInt("u_ShadowMap", 2);
        glActiveTexture(GL_TEXTURE2);
        glBindTexture(GL_TEXTURE_2D, s.ShadowTexture);
        for (const auto &item : s.Queue)
        {
            s.MaterialUBO->SetData(&item.Material, sizeof(MaterialData));
            glActiveTexture(GL_TEXTURE0);
            glBindTexture(GL_TEXTURE_2D, item.Texture ? item.Texture->GetRendererID() : 0);
            item.Geometry->GetVertexArray()->Bind();
            RenderCommand::DrawIndexed(item.Geometry->GetVertexArray(), item.Geometry->GetIndexCount());
            s.Stats.DrawCalls++;
            s.Stats.MeshCount++;
        }
        s.Queue.clear();
    }
    void Renderer3D::DrawMesh(const Ref<Mesh> &mesh, const glm::mat4 &transform, const Ref<Texture2D> &texture,
                              const glm::vec4 &color, bool useTexture, int entityID)
    {
        if (!mesh || !mesh->GetVertexArray())
            return;
        Submission item;
        item.Geometry = mesh;
        item.Texture = useTexture ? texture : nullptr;
        item.Cast = s.Surface.CastShadows;
        auto &m = item.Material;
        m.Model = transform;
        const glm::mat3 basis(transform);
        m.Normal = std::abs(glm::determinant(basis)) > 1e-8f ? glm::mat4(glm::inverseTranspose(basis)) : glm::mat4(1);
        m.Color = color;
        m.Params = {s.Surface.Metallic, s.Surface.Roughness, s.Surface.AmbientOcclusion, item.Texture ? 1.0f : 0.0f};
        m.Emission = glm::vec4(s.Surface.Emission, 0);
        m.Entity = {entityID, s.Surface.ReceiveShadows ? 1 : 0, 0, 0};
        s.Queue.push_back(std::move(item));
    }
    void Renderer3D::DrawModel(const Ref<Model> &model, const glm::mat4 &transform, int entityID, const glm::vec4 &tint,
                               const Ref<Texture2D> &texture)
    {
        if (model)
            for (const auto &part : model->GetSubmeshes())
                DrawMesh(part.Mesh, transform, texture ? texture : part.DiffuseTexture, part.DiffuseColor * tint,
                         texture || part.UseTexture, entityID);
    }
    void Renderer3D::ResetStats()
    {
        s.Stats = {};
    }
    Renderer3D::Statistics Renderer3D::GetStats()
    {
        return s.Stats;
    }
} // namespace TomCat
