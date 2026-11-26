#include "tcpch.h"

#include"OpenGLRendererAPI.h"

#include "OpenGLApi.h"

#include <array>
#include <cmath>
#include <vector>

namespace TomCat {

    namespace {
        class OpenGLRenderState final : public RenderState {
        public:
            OpenGLRenderState() {
                glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING, &m_Draw);
                glGetIntegerv(GL_READ_FRAMEBUFFER_BINDING, &m_Read);
                glGetIntegerv(GL_VIEWPORT, m_Viewport);
                glGetIntegerv(GL_DEPTH_FUNC, &m_DepthFunction);
                glGetIntegerv(GL_CURRENT_PROGRAM, &m_Program);
                glGetIntegerv(GL_VERTEX_ARRAY_BINDING, &m_VertexArray);
                glGetIntegerv(GL_ACTIVE_TEXTURE, &m_ActiveTexture);
                glGetIntegerv(GL_UNIFORM_BUFFER_BINDING, &m_UniformBuffer);
                glGetFloatv(GL_COLOR_CLEAR_VALUE, m_ClearColor);
                glGetBooleanv(GL_DEPTH_WRITEMASK, &m_DepthWrite);
                m_Depth = glIsEnabled(GL_DEPTH_TEST);
                m_Blend = glIsEnabled(GL_BLEND);
                m_Cull = glIsEnabled(GL_CULL_FACE);
                for (uint32_t slot = 0; slot < 3; ++slot) {
                    glActiveTexture(GL_TEXTURE0 + slot);
                    glGetIntegerv(GL_TEXTURE_BINDING_2D, &m_Textures[slot]);
                    glGetIntegeri_v(GL_UNIFORM_BUFFER_BINDING, slot, &m_Uniforms[slot]);
                }
                glActiveTexture(m_ActiveTexture);
            }
            void RestoreTarget() override {
                glBindFramebuffer(GL_DRAW_FRAMEBUFFER, m_Draw);
                glBindFramebuffer(GL_READ_FRAMEBUFFER, m_Read);
                glViewport(m_Viewport[0], m_Viewport[1], m_Viewport[2], m_Viewport[3]);
            }
            ~OpenGLRenderState() override {
                RestoreTarget();
                glDepthFunc(m_DepthFunction);
                glDepthMask(m_DepthWrite);
                SetEnabled(GL_DEPTH_TEST, m_Depth);
                SetEnabled(GL_BLEND, m_Blend);
                SetEnabled(GL_CULL_FACE, m_Cull);
                glClearColor(m_ClearColor[0], m_ClearColor[1], m_ClearColor[2], m_ClearColor[3]);
                glUseProgram(m_Program);
                glBindVertexArray(m_VertexArray);
                for (uint32_t slot = 0; slot < 3; ++slot) {
                    glActiveTexture(GL_TEXTURE0 + slot);
                    glBindTexture(GL_TEXTURE_2D, m_Textures[slot]);
                    glBindBufferBase(GL_UNIFORM_BUFFER, slot, m_Uniforms[slot]);
                }
                glBindBuffer(GL_UNIFORM_BUFFER, m_UniformBuffer);
                glActiveTexture(m_ActiveTexture);
            }
        private:
            static void SetEnabled(GLenum capability, bool enabled) {
                if (enabled) glEnable(capability);
                else glDisable(capability);
            }
            GLint m_Draw{}, m_Read{}, m_Viewport[4]{}, m_DepthFunction{}, m_Program{}, m_VertexArray{};
            GLint m_ActiveTexture{}, m_UniformBuffer{}, m_Textures[3]{}, m_Uniforms[3]{};
            GLfloat m_ClearColor[4]{};
            GLboolean m_Depth{}, m_Blend{}, m_Cull{}, m_DepthWrite{};
        };

        // Captures and restores the GL state one full-screen post-process pass
        // mutates. ApplyColorGrade keeps its own bespoke capture; this scope is
        // shared by the palette-swap and mask-transition passes, whose needs
        // match (draw target, draw buffers, program, VAO, texture units 0-1,
        // and the toggled caps).
        class FullscreenPassScope final {
        public:
            explicit FullscreenPassScope(int maxDrawBuffers)
            {
                glGetIntegerv(GL_VIEWPORT, m_Viewport);
                m_Valid = m_Viewport[2] > 0 && m_Viewport[3] > 0;
                glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING, &m_Draw);
                glGetIntegerv(GL_READ_FRAMEBUFFER_BINDING, &m_Read);
                glGetIntegerv(GL_READ_BUFFER, &m_ReadBuffer);
                m_DrawReadBuffer = m_ReadBuffer;
                if (m_Draw != m_Read) {
                    glBindFramebuffer(GL_READ_FRAMEBUFFER, m_Draw);
                    glGetIntegerv(GL_READ_BUFFER, &m_DrawReadBuffer);
                    glBindFramebuffer(GL_READ_FRAMEBUFFER, m_Read);
                }
                glGetIntegerv(GL_CURRENT_PROGRAM, &m_Program);
                glGetIntegerv(GL_VERTEX_ARRAY_BINDING, &m_VertexArray);
                glGetIntegerv(GL_ACTIVE_TEXTURE, &m_ActiveTexture);
                glActiveTexture(GL_TEXTURE0);
                glGetIntegerv(GL_TEXTURE_BINDING_2D, &m_Textures[0]);
                glActiveTexture(GL_TEXTURE1);
                glGetIntegerv(GL_TEXTURE_BINDING_2D, &m_Textures[1]);
                glActiveTexture(m_ActiveTexture);
                m_Depth = glIsEnabled(GL_DEPTH_TEST);
                m_Blend = glIsEnabled(GL_BLEND);
                m_Scissor = glIsEnabled(GL_SCISSOR_TEST);
                m_Stencil = glIsEnabled(GL_STENCIL_TEST);
                m_Buffers.assign(static_cast<size_t>(m_Draw ? maxDrawBuffers : 1),
                    GL_NONE);
                for (size_t i = 0; i < m_Buffers.size(); ++i) {
                    GLint value = 0;
                    glGetIntegerv(GL_DRAW_BUFFER0 + static_cast<GLenum>(i), &value);
                    m_Buffers[i] = static_cast<GLenum>(value);
                }
            }
            ~FullscreenPassScope() {
                glBindFramebuffer(GL_DRAW_FRAMEBUFFER, m_Draw);
                glDrawBuffers(static_cast<GLsizei>(m_Buffers.size()), m_Buffers.data());
                glBindFramebuffer(GL_READ_FRAMEBUFFER, m_Draw);
                glReadBuffer(m_DrawReadBuffer);
                glBindFramebuffer(GL_READ_FRAMEBUFFER, m_Read);
                glReadBuffer(m_ReadBuffer);
                glUseProgram(m_Program);
                glBindVertexArray(m_VertexArray);
                glActiveTexture(GL_TEXTURE0);
                glBindTexture(GL_TEXTURE_2D, m_Textures[0]);
                glActiveTexture(GL_TEXTURE1);
                glBindTexture(GL_TEXTURE_2D, m_Textures[1]);
                glActiveTexture(m_ActiveTexture);
                SetEnabled(GL_DEPTH_TEST, m_Depth);
                SetEnabled(GL_BLEND, m_Blend);
                SetEnabled(GL_SCISSOR_TEST, m_Scissor);
                SetEnabled(GL_STENCIL_TEST, m_Stencil);
            }
            bool Valid() const { return m_Valid; }
            const GLint* Viewport() const { return m_Viewport; }
            GLint DrawFramebuffer() const { return m_Draw; }
        private:
            static void SetEnabled(GLenum capability, bool enabled) {
                if (enabled) glEnable(capability);
                else glDisable(capability);
            }
            GLint m_Viewport[4]{}, m_Draw{}, m_Read{}, m_ReadBuffer{}, m_DrawReadBuffer{};
            GLint m_Program{}, m_VertexArray{}, m_ActiveTexture{}, m_Textures[2]{};
            bool m_Depth{}, m_Blend{}, m_Scissor{}, m_Stencil{}, m_Valid = false;
            std::vector<GLenum> m_Buffers;
        };
    }

    Scope<RenderState> OpenGLRendererAPI::CaptureState() { return CreateScope<OpenGLRenderState>(); }
    void OpenGLRendererAPI::SetDepthWrite(bool enabled) { glDepthMask(enabled ? GL_TRUE : GL_FALSE); }
    void OpenGLRendererAPI::SetDepthLessEqual(bool enabled) { glDepthFunc(enabled ? GL_LEQUAL : GL_LESS); }
    void OpenGLRendererAPI::SetBlending(bool enabled) {
        if (enabled) glEnable(GL_BLEND);
        else glDisable(GL_BLEND);
    }
    void OpenGLRendererAPI::SetCullFaces(bool enabled) {
        if (enabled) glEnable(GL_CULL_FACE);
        else glDisable(GL_CULL_FACE);
    }

    void OpenGLRendererAPI::ReleaseColorGrade() {
        m_GradeShader.reset(); m_GradeUniform.reset();
        if (m_GradeTexture) glDeleteTextures(1,&m_GradeTexture);
        if (m_GradeFramebuffer) glDeleteFramebuffers(1,&m_GradeFramebuffer);
        if (m_GradeVAO) glDeleteVertexArrays(1,&m_GradeVAO);
        m_GradeTexture=m_GradeFramebuffer=m_GradeVAO=0; m_GradeWidth=m_GradeHeight=0;
        m_GradeFramebufferComplete=false;
        // Releases every embedded full-screen pass resource, not just the
        // original color grade.
        m_PaletteShader.reset(); m_MaskShader.reset();
        m_PaletteUniform.reset(); m_MaskUniform.reset();
        if (m_PaletteTexture) glDeleteTextures(1,&m_PaletteTexture);
        if (m_PaletteFramebuffer) glDeleteFramebuffers(1,&m_PaletteFramebuffer);
        if (m_MaskTexture) glDeleteTextures(1,&m_MaskTexture);
        if (m_MaskFramebuffer) glDeleteFramebuffers(1,&m_MaskFramebuffer);
        m_PaletteTexture=m_PaletteFramebuffer=0;
        m_PaletteWidth=m_PaletteHeight=0;
        m_PaletteFramebufferComplete=false;
        m_MaskTexture=m_MaskFramebuffer=0;
        m_MaskWidth=m_MaskHeight=0;
        m_MaskFramebufferComplete=false;
    }
    void OpenGLRendererAPI::ApplyColorGrade(float exposure,float saturation,float vignette) {
        if (!std::isfinite(exposure) || !std::isfinite(saturation) || !std::isfinite(vignette)) return;
        if (exposure==0 && saturation==1 && vignette==0) return;
        GLint viewport[4]{},draw=0,read=0,readBuffer=0,drawReadBuffer=0,program=0,vao=0,activeTexture=0,texture=0;
        glGetIntegerv(GL_VIEWPORT,viewport); if (viewport[2]<=0 || viewport[3]<=0) return;
        glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING,&draw); glGetIntegerv(GL_READ_FRAMEBUFFER_BINDING,&read);
        glGetIntegerv(GL_READ_BUFFER,&readBuffer); glGetIntegerv(GL_CURRENT_PROGRAM,&program);
        drawReadBuffer=readBuffer;
        if (draw!=read) {
            glBindFramebuffer(GL_READ_FRAMEBUFFER,draw); glGetIntegerv(GL_READ_BUFFER,&drawReadBuffer);
            glBindFramebuffer(GL_READ_FRAMEBUFFER,read);
        }
        glGetIntegerv(GL_VERTEX_ARRAY_BINDING,&vao); glGetIntegerv(GL_ACTIVE_TEXTURE,&activeTexture);
        glActiveTexture(GL_TEXTURE0); glGetIntegerv(GL_TEXTURE_BINDING_2D,&texture);
        const bool depth=glIsEnabled(GL_DEPTH_TEST), blend=glIsEnabled(GL_BLEND), scissor=glIsEnabled(GL_SCISSOR_TEST), stencil=glIsEnabled(GL_STENCIL_TEST);
        std::vector<GLenum> buffers(static_cast<size_t>(draw ? m_MaxDrawBuffers : 1));
        for (size_t i=0;i<buffers.size();++i) { GLint value=0; glGetIntegerv(GL_DRAW_BUFFER0+static_cast<GLenum>(i),&value); buffers[i]=static_cast<GLenum>(value); }
        struct Restore { std::function<void()> Fn; ~Restore(){Fn();} } restore{[&] {
            glBindFramebuffer(GL_DRAW_FRAMEBUFFER,draw); glDrawBuffers(static_cast<GLsizei>(buffers.size()),buffers.data());
            glBindFramebuffer(GL_READ_FRAMEBUFFER,draw); glReadBuffer(drawReadBuffer);
            glBindFramebuffer(GL_READ_FRAMEBUFFER,read); glReadBuffer(readBuffer);
            glUseProgram(program); glBindVertexArray(vao); glBindTexture(GL_TEXTURE_2D,texture); glActiveTexture(activeTexture);
            if(depth)glEnable(GL_DEPTH_TEST);else glDisable(GL_DEPTH_TEST);
            if(blend)glEnable(GL_BLEND);else glDisable(GL_BLEND);
            if(scissor)glEnable(GL_SCISSOR_TEST);else glDisable(GL_SCISSOR_TEST);
            if(stencil)glEnable(GL_STENCIL_TEST);else glDisable(GL_STENCIL_TEST);
        }};
        if (!m_GradeShader) {
            m_GradeShader=Shader::Create("TomCat.ColorGrade", R"(#version 450 core
layout(location=0) out vec2 v_UV;
void main(){vec2 p=vec2((gl_VertexIndex<<1)&2,gl_VertexIndex&2);v_UV=p;gl_Position=vec4(p*2.0-1.0,0,1);}
)", R"(#version 450 core
layout(location=0) in vec2 v_UV;
layout(location=0) out vec4 o_Color;
layout(binding=0) uniform sampler2D u_Source;
layout(std140,binding=2) uniform ColorGrade { vec4 u_Grade; };
void main(){vec4 color=texture(u_Source,v_UV);vec3 rgb=color.rgb*exp2(u_Grade.x);float luma=dot(rgb,vec3(.2126,.7152,.0722));rgb=mix(vec3(luma),rgb,u_Grade.y);vec2 d=v_UV*2.0-1.0;rgb*=1.0-u_Grade.z*smoothstep(.2,1.4,dot(d,d));o_Color=vec4(rgb,color.a);}
)");
        }
        if (!m_GradeUniform) m_GradeUniform=UniformBuffer::Create(sizeof(glm::vec4),2);
        if (!m_GradeFramebuffer) {
            m_GradeFramebufferComplete=false;
            glGenFramebuffers(1,&m_GradeFramebuffer);
        }
        if (!m_GradeVAO) glGenVertexArrays(1,&m_GradeVAO);
        if (!m_GradeShader || !m_GradeUniform || !m_GradeFramebuffer || !m_GradeVAO) return;
        if(!m_GradeTexture || m_GradeWidth!=viewport[2] || m_GradeHeight!=viewport[3]) {
            m_GradeFramebufferComplete=false;
            // A fresh image also makes allocation failure incomplete, instead of
            // accidentally accepting a complete texture with the previous size.
            if (m_GradeTexture) glDeleteTextures(1,&m_GradeTexture);
            m_GradeTexture=0;
            glGenTextures(1,&m_GradeTexture);
            if (!m_GradeTexture) return;
            glBindTexture(GL_TEXTURE_2D,m_GradeTexture);
            glTexImage2D(GL_TEXTURE_2D,0,GL_RGBA8,viewport[2],viewport[3],0,GL_RGBA,GL_UNSIGNED_BYTE,nullptr);
            glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,GL_LINEAR);glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,GL_LINEAR);
            glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_S,GL_CLAMP_TO_EDGE);glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_T,GL_CLAMP_TO_EDGE);
            m_GradeWidth=viewport[2];m_GradeHeight=viewport[3];
        }
        else glBindTexture(GL_TEXTURE_2D,m_GradeTexture);
        glBindFramebuffer(GL_DRAW_FRAMEBUFFER,m_GradeFramebuffer);
        // This framebuffer and its draw buffers are private to this pass. Only
        // resource/storage changes invalidate completeness; ordinary frames do not.
        if (!m_GradeFramebufferComplete) {
            glFramebufferTexture2D(GL_DRAW_FRAMEBUFFER,GL_COLOR_ATTACHMENT0,GL_TEXTURE_2D,m_GradeTexture,0);
            const GLenum destination=GL_COLOR_ATTACHMENT0;glDrawBuffers(1,&destination);
            if(glCheckFramebufferStatus(GL_DRAW_FRAMEBUFFER)!=GL_FRAMEBUFFER_COMPLETE) {
                m_GradeWidth=m_GradeHeight=0; // retry storage allocation next time
                return;
            }
            m_GradeFramebufferComplete=true;
        }
        glBindFramebuffer(GL_READ_FRAMEBUFFER,draw);glReadBuffer(draw ? GL_COLOR_ATTACHMENT0 : GL_BACK);
        glDisable(GL_SCISSOR_TEST);
        glBlitFramebuffer(viewport[0],viewport[1],viewport[0]+viewport[2],viewport[1]+viewport[3],0,0,viewport[2],viewport[3],GL_COLOR_BUFFER_BIT,GL_NEAREST);
        glBindFramebuffer(GL_DRAW_FRAMEBUFFER,draw);const GLenum output=draw ? GL_COLOR_ATTACHMENT0 : GL_BACK;glDrawBuffers(1,&output);
        glDisable(GL_DEPTH_TEST);glDisable(GL_BLEND);glDisable(GL_STENCIL_TEST);
        m_GradeShader->Bind();
        const glm::vec4 grade{std::clamp(exposure,-10.f,10.f),std::clamp(saturation,0.f,2.f),std::clamp(vignette,0.f,1.f),0};
        m_GradeUniform->SetData(&grade,sizeof(grade));
        glBindVertexArray(m_GradeVAO);glDrawArrays(GL_TRIANGLES,0,3);
    }

    bool OpenGLRendererAPI::PrepareFullscreenCopyTarget(uint32_t& texture,
        uint32_t& framebuffer, bool& framebufferComplete, int& width,
        int& height, const int32_t viewport[4], bool nearest) {
        if(!texture || width!=viewport[2] || height!=viewport[3]) {
            framebufferComplete=false;
            // A fresh image also makes allocation failure incomplete, instead of
            // accidentally accepting a complete texture with the previous size.
            if (texture) glDeleteTextures(1,&texture);
            texture=0;
            glGenTextures(1,&texture);
            if (!texture) return false;
            glBindTexture(GL_TEXTURE_2D,texture);
            glTexImage2D(GL_TEXTURE_2D,0,GL_RGBA8,viewport[2],viewport[3],0,GL_RGBA,GL_UNSIGNED_BYTE,nullptr);
            const GLint filter=nearest?GL_NEAREST:GL_LINEAR;
            glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,filter);
            glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,filter);
            glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_S,GL_CLAMP_TO_EDGE);
            glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_T,GL_CLAMP_TO_EDGE);
            width=viewport[2];height=viewport[3];
        }
        else glBindTexture(GL_TEXTURE_2D,texture);
        if (!framebuffer) {
            framebufferComplete=false;
            glGenFramebuffers(1,&framebuffer);
        }
        if (!framebuffer) return false;
        glBindFramebuffer(GL_DRAW_FRAMEBUFFER,framebuffer);
        // This framebuffer and its draw buffers are private to this pass. Only
        // resource/storage changes invalidate completeness; ordinary frames do not.
        if (!framebufferComplete) {
            glFramebufferTexture2D(GL_DRAW_FRAMEBUFFER,GL_COLOR_ATTACHMENT0,GL_TEXTURE_2D,texture,0);
            const GLenum destination=GL_COLOR_ATTACHMENT0;glDrawBuffers(1,&destination);
            if(glCheckFramebufferStatus(GL_DRAW_FRAMEBUFFER)!=GL_FRAMEBUFFER_COMPLETE) {
                width=height=0; // retry storage allocation next time
                return false;
            }
            framebufferComplete=true;
        }
        return true;
    }

    void OpenGLRendererAPI::ApplyPaletteSwap(const glm::vec4 (&oldColors)[4],
        const glm::vec4 (&newColors)[4], float threshold) {
        for (int index = 0; index < 4; ++index)
            if (!std::isfinite(oldColors[index].x) || !std::isfinite(oldColors[index].y)
                || !std::isfinite(oldColors[index].z) || !std::isfinite(oldColors[index].w)
                || !std::isfinite(newColors[index].x) || !std::isfinite(newColors[index].y)
                || !std::isfinite(newColors[index].z) || !std::isfinite(newColors[index].w))
                return;
        if (!std::isfinite(threshold)) return;
        FullscreenPassScope scope(m_MaxDrawBuffers);
        if (!scope.Valid()) return;
        const GLint* const viewport = scope.Viewport();
        if (!m_PaletteShader) {
            // Desktop GL compiles embedded sources through the Vulkan SPIR-V
            // dialect, so pass parameters ride a std140 block like the grade.
            m_PaletteShader=Shader::Create("TomCat.PaletteSwap", R"(#version 450 core
layout(location=0) out vec2 v_UV;
void main(){vec2 p=vec2((gl_VertexIndex<<1)&2,gl_VertexIndex&2);v_UV=p;gl_Position=vec4(p*2.0-1.0,0,1);}
)", R"(#version 450 core
layout(location=0) in vec2 v_UV;
layout(location=0) out vec4 o_Color;
layout(binding=0) uniform sampler2D u_Source;
layout(std140,binding=2) uniform PaletteSwap {
    vec4 u_OldDark,u_OldLight,u_OldPrimary,u_OldSecondary;
    vec4 u_NewDark,u_NewLight,u_NewPrimary,u_NewSecondary;
    vec4 u_Parameters;
};
void main(){
    vec4 color=texture(u_Source,v_UV);
    vec4 replaced=color;
    vec4 tolerance=vec4(max(u_Parameters.x,0.0));
    if (all(lessThanEqual(abs(color-u_OldDark),tolerance))) replaced=u_NewDark;
    if (all(lessThanEqual(abs(color-u_OldLight),tolerance))) replaced=u_NewLight;
    if (all(lessThanEqual(abs(color-u_OldPrimary),tolerance))) replaced=u_NewPrimary;
    if (all(lessThanEqual(abs(color-u_OldSecondary),tolerance))) replaced=u_NewSecondary;
    o_Color=replaced;
}
)");
        }
        if (!m_PaletteShader) return;
        if (!m_PaletteUniform)
            m_PaletteUniform=UniformBuffer::Create(9*sizeof(glm::vec4),2);
        if (!m_PaletteUniform) return;
        if (!m_GradeVAO) glGenVertexArrays(1,&m_GradeVAO);
        if (!m_GradeVAO) return;
        glActiveTexture(GL_TEXTURE0);
        // Exact palette matching requires the undeformed source pixel.
        if (!PrepareFullscreenCopyTarget(m_PaletteTexture,m_PaletteFramebuffer,
            m_PaletteFramebufferComplete,m_PaletteWidth,m_PaletteHeight,viewport,true))
            return;
        const GLint draw=scope.DrawFramebuffer();
        glBindFramebuffer(GL_READ_FRAMEBUFFER,draw);
        glReadBuffer(draw ? GL_COLOR_ATTACHMENT0 : GL_BACK);
        glDisable(GL_SCISSOR_TEST);
        glBlitFramebuffer(viewport[0],viewport[1],viewport[0]+viewport[2],viewport[1]+viewport[3],0,0,viewport[2],viewport[3],GL_COLOR_BUFFER_BIT,GL_NEAREST);
        glBindFramebuffer(GL_DRAW_FRAMEBUFFER,draw);
        const GLenum output=draw ? GL_COLOR_ATTACHMENT0 : GL_BACK;
        glDrawBuffers(1,&output);
        glDisable(GL_DEPTH_TEST);glDisable(GL_BLEND);glDisable(GL_STENCIL_TEST);
        m_PaletteShader->Bind();
        const std::array<glm::vec4,9> palettePayload={
            oldColors[0],oldColors[1],oldColors[2],oldColors[3],
            newColors[0],newColors[1],newColors[2],newColors[3],
            glm::vec4(std::clamp(threshold,0.0f,1.0f),0.0f,0.0f,0.0f)};
        m_PaletteUniform->SetData(palettePayload.data(),
            palettePayload.size()*sizeof(glm::vec4));
        glBindVertexArray(m_GradeVAO);glDrawArrays(GL_TRIANGLES,0,3);
    }

    void OpenGLRendererAPI::ApplyMaskTransition(const Ref<Texture2D>& mask,
        float progress, const glm::vec4& coverColor, bool rotatingIris) {
        if (!mask || !mask->IsLoaded()) return;
        if (!std::isfinite(progress) || !std::isfinite(coverColor.x)
            || !std::isfinite(coverColor.y) || !std::isfinite(coverColor.z)
            || !std::isfinite(coverColor.w))
            return;
        // Progress 0 leaves the target untouched, matching the neutral grade.
        const float clampedProgress = std::clamp(progress,0.0f,1.0f);
        if (clampedProgress <= 0.0f) return;
        FullscreenPassScope scope(m_MaxDrawBuffers);
        if (!scope.Valid()) return;
        const GLint* const viewport = scope.Viewport();
        if (!m_MaskShader) {
            m_MaskShader=Shader::Create("TomCat.MaskTransition", R"(#version 450 core
layout(location=0) out vec2 v_UV;
void main(){vec2 p=vec2((gl_VertexIndex<<1)&2,gl_VertexIndex&2);v_UV=p;gl_Position=vec4(p*2.0-1.0,0,1);}
)", R"(#version 450 core
layout(location=0) in vec2 v_UV;
layout(location=0) out vec4 o_Color;
layout(binding=0) uniform sampler2D u_Source;
layout(binding=1) uniform sampler2D u_Mask;
layout(std140,binding=2) uniform MaskTransition {
    vec4 u_Parameters;
    vec4 u_Cover;
};
void main(){
    vec4 source=texture(u_Source,v_UV);
    float masked=texture(u_Mask,v_UV).a;
    float cover=u_Parameters.x<=0.0 ? 0.0 : step(masked,u_Parameters.x);
    float progress = u_Parameters.x;
    if (u_Parameters.y > 0.5) {
        vec2 size = vec2(textureSize(u_Source, 0));
        vec2 p = (v_UV - 0.5) * vec2(size.x / size.y, 1.0);
        float angle = progress * 6.2831853;
        float c = cos(angle), s = sin(angle);
        vec2 uv = mat2(c,-s,s,c) * p / max(0.0001,2.0*(1.0-progress)) + 0.5;
        cover = progress >= 1.0 || any(lessThan(uv,vec2(0))) || any(greaterThan(uv,vec2(1))) ? 1.0 : 1.0-texture(u_Mask,uv).a;
    }
    o_Color=mix(source,u_Cover,cover);
}
)");
        }
        if (!m_MaskShader) return;
        if (!m_MaskUniform)
            m_MaskUniform=UniformBuffer::Create(2*sizeof(glm::vec4),2);
        if (!m_MaskUniform) return;
        if (!m_GradeVAO) glGenVertexArrays(1,&m_GradeVAO);
        if (!m_GradeVAO) return;
        glActiveTexture(GL_TEXTURE0);
        if (!PrepareFullscreenCopyTarget(m_MaskTexture,m_MaskFramebuffer,
            m_MaskFramebufferComplete,m_MaskWidth,m_MaskHeight,viewport,false))
            return;
        const GLint draw=scope.DrawFramebuffer();
        glBindFramebuffer(GL_READ_FRAMEBUFFER,draw);
        glReadBuffer(draw ? GL_COLOR_ATTACHMENT0 : GL_BACK);
        glDisable(GL_SCISSOR_TEST);
        glBlitFramebuffer(viewport[0],viewport[1],viewport[0]+viewport[2],viewport[1]+viewport[3],0,0,viewport[2],viewport[3],GL_COLOR_BUFFER_BIT,GL_NEAREST);
        glBindFramebuffer(GL_DRAW_FRAMEBUFFER,draw);
        const GLenum output=draw ? GL_COLOR_ATTACHMENT0 : GL_BACK;
        glDrawBuffers(1,&output);
        glDisable(GL_DEPTH_TEST);glDisable(GL_BLEND);glDisable(GL_STENCIL_TEST);
        mask->Bind(1);
        m_MaskShader->Bind();
        const std::array<glm::vec4,2> maskPayload={
            glm::vec4(clampedProgress,rotatingIris ? 1.f : 0.f,0.0f,0.0f), coverColor};
        m_MaskUniform->SetData(maskPayload.data(),
            maskPayload.size()*sizeof(glm::vec4));
        glBindVertexArray(m_GradeVAO);glDrawArrays(GL_TRIANGLES,0,3);
    }

	void OpenGLRendererAPI::Init()
	{
		TC_PROFILE_FUNCTION();

        glGetIntegerv(GL_MAX_DRAW_BUFFERS,&m_MaxDrawBuffers);
        m_MaxDrawBuffers=std::max(m_MaxDrawBuffers,1);
#ifdef TC_PLATFORM_WEB
        // Application creates/makes current its WebWindow before Renderer::Init.
        // Seed from this context once, including after a renderer restart.
        glGetFloatv(GL_COLOR_CLEAR_VALUE,&m_ClearColor.x);
#endif

		glEnable(GL_BLEND);
		// Straight-color textures need source-over coverage, not alpha squared.
		glBlendFuncSeparate(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA, GL_ONE, GL_ONE_MINUS_SRC_ALPHA);

		glEnable(GL_DEPTH_TEST);
	}

	void OpenGLRendererAPI::SetViewport(uint32_t x, uint32_t y, uint32_t width, uint32_t height)
	{
		glViewport(x, y, width, height);
	}

	void OpenGLRendererAPI::SetClearColor(const glm::vec4& color)
	{
		glClearColor(color.r, color.g, color.b, color.a);
#ifdef TC_PLATFORM_WEB
        if (std::isfinite(color.r) && std::isfinite(color.g)
            && std::isfinite(color.b) && std::isfinite(color.a)) {
            // glClearColor clamps its stored state; clearBufferfv does not.
            m_ClearColor={std::clamp(color.r,0.0f,1.0f),std::clamp(color.g,0.0f,1.0f),
                std::clamp(color.b,0.0f,1.0f),std::clamp(color.a,0.0f,1.0f)};
        } else {
            // Keep the existing GL/API behavior for NaN/Infinity, rather than
            // introducing a different validation or guessed NaN conversion.
            glGetFloatv(GL_COLOR_CLEAR_VALUE,&m_ClearColor.x);
        }
#endif
	}

	void OpenGLRendererAPI::Clear()
	{
#ifdef TC_PLATFORM_WEB
        // The editor's framebuffer mixes RGBA and integer picking attachments.
        // WebGL rejects a floating-point glClear across both attachment types.
        // Picking is cleared explicitly by the framebuffer owner.
        glClearBufferfv(GL_COLOR, 0, &m_ClearColor.x);
        glClear(GL_DEPTH_BUFFER_BIT);
#else
		glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
#endif
	}

	void OpenGLRendererAPI::SetDepthTest(bool enabled)
	{
		if (enabled)
			glEnable(GL_DEPTH_TEST);
		else
			glDisable(GL_DEPTH_TEST);
	}

	void OpenGLRendererAPI::DrawIndexed(const Ref<VertexArray>& vertexArray, uint32_t indexCount)
	{
		if (!vertexArray || !vertexArray->GetIndexBuffer())
		{
			TC_Core_Error("DrawIndexed requires a vertex array with an index buffer");
			return;
		}

		const uint32_t count = indexCount ? indexCount : vertexArray->GetIndexBuffer()->GetCount();
		if (count == 0)
			return;

		vertexArray->Bind();
		glDrawElements(GL_TRIANGLES, count, GL_UNSIGNED_INT, nullptr);
		FrameProfiler::Get().RecordDraw(count);
	}

	void OpenGLRendererAPI::DrawLines(const Ref<VertexArray>& vertexArray, uint32_t vertexCount)
	{
		if (!vertexArray)
		{
			TC_Core_Error("DrawLines requires a vertex array");
			return;
		}

		if (vertexCount == 0)
			return;

		vertexArray->Bind();
		glDrawArrays(GL_LINES, 0, vertexCount);
		FrameProfiler::Get().RecordDraw(vertexCount);
	}

	void OpenGLRendererAPI::SetLineWidth(float width)
	{
		if (!std::isfinite(width) || width <= 0.0f)
		{
			TC_Core_Error("SetLineWidth requires a finite width greater than zero");
			return;
		}

		glLineWidth(width);
	}

}
