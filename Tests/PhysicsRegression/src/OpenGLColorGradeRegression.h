#pragma once

#include "platform/OpenGL/OpenGLRendererAPI.h"
#include "TomCat/Renderer/Framebuffer.h"

#include <glad/glad.h>
#include <array>
#include <cmath>
#include <stdexcept>
#include <vector>

namespace OpenGLColorGradeRegression {

inline void Require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}

// Observe the production GL calls, while forwarding them to the real driver.
// Dropping one fresh texture's storage models allocation failure without leaving
// a process-wide GL error or depending on exhausting the test machine's GPU.
class Calls final {
public:
    Calls() {
        Require(!s_Current, "nested color-grade GL probe");
        s_Current=this;
        glad_glGetIntegerv=GetInteger;
        glad_glFramebufferTexture2D=Attach;
        glad_glCheckFramebufferStatus=Check;
        glad_glTexImage2D=Image;
    }
    ~Calls() {
        glad_glGetIntegerv=m_GetInteger;
        glad_glFramebufferTexture2D=m_Attach;
        glad_glCheckFramebufferStatus=m_Check;
        glad_glTexImage2D=m_Image;
        s_Current=nullptr;
    }
    Calls(const Calls&)=delete;
    Calls& operator=(const Calls&)=delete;

    unsigned CapQueries=0, Attachments=0, Checks=0, Images=0;
    bool DropNextImage=false;

private:
    static void APIENTRY GetInteger(GLenum name, GLint* value) {
        if (name==GL_MAX_DRAW_BUFFERS) ++s_Current->CapQueries;
        s_Current->m_GetInteger(name,value);
    }
    static void APIENTRY Attach(GLenum target, GLenum attachment, GLenum imageTarget, GLuint texture, GLint level) {
        ++s_Current->Attachments;
        s_Current->m_Attach(target,attachment,imageTarget,texture,level);
    }
    static GLenum APIENTRY Check(GLenum target) {
        ++s_Current->Checks;
        return s_Current->m_Check(target);
    }
    static void APIENTRY Image(GLenum target, GLint level, GLint format, GLsizei width, GLsizei height,
        GLint border, GLenum dataFormat, GLenum dataType, const void* pixels) {
        ++s_Current->Images;
        if (s_Current->DropNextImage) { s_Current->DropNextImage=false; return; }
        s_Current->m_Image(target,level,format,width,height,border,dataFormat,dataType,pixels);
    }

    inline static Calls* s_Current=nullptr;
    PFNGLGETINTEGERVPROC m_GetInteger=glad_glGetIntegerv;
    PFNGLFRAMEBUFFERTEXTURE2DPROC m_Attach=glad_glFramebufferTexture2D;
    PFNGLCHECKFRAMEBUFFERSTATUSPROC m_Check=glad_glCheckFramebufferStatus;
    PFNGLTEXIMAGE2DPROC m_Image=glad_glTexImage2D;
};

struct State {
    std::array<GLint,4> Viewport{}, ScissorBox{};
    GLint Draw=0, Read=0, ReadBuffer=0, DrawReadBuffer=0;
    GLint Program=0, VAO=0, ActiveTexture=0, Texture0=0;
    bool Depth=false, Blend=false, Scissor=false, Stencil=false;
    std::vector<GLenum> DrawBuffers;

    static State Capture(GLint maxBuffers) {
        State result;
        glGetIntegerv(GL_VIEWPORT,result.Viewport.data());
        glGetIntegerv(GL_SCISSOR_BOX,result.ScissorBox.data());
        glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING,&result.Draw);
        glGetIntegerv(GL_READ_FRAMEBUFFER_BINDING,&result.Read);
        glGetIntegerv(GL_READ_BUFFER,&result.ReadBuffer);
        glBindFramebuffer(GL_READ_FRAMEBUFFER,result.Draw);
        glGetIntegerv(GL_READ_BUFFER,&result.DrawReadBuffer);
        glBindFramebuffer(GL_READ_FRAMEBUFFER,result.Read);
        glGetIntegerv(GL_CURRENT_PROGRAM,&result.Program);
        glGetIntegerv(GL_VERTEX_ARRAY_BINDING,&result.VAO);
        glGetIntegerv(GL_ACTIVE_TEXTURE,&result.ActiveTexture);
        glActiveTexture(GL_TEXTURE0);
        glGetIntegerv(GL_TEXTURE_BINDING_2D,&result.Texture0);
        glActiveTexture(result.ActiveTexture);
        result.Depth=glIsEnabled(GL_DEPTH_TEST); result.Blend=glIsEnabled(GL_BLEND);
        result.Scissor=glIsEnabled(GL_SCISSOR_TEST); result.Stencil=glIsEnabled(GL_STENCIL_TEST);
        result.DrawBuffers.resize(result.Draw ? maxBuffers : 1);
        for (size_t i=0;i<result.DrawBuffers.size();++i) {
            GLint value=0; glGetIntegerv(GL_DRAW_BUFFER0+static_cast<GLenum>(i),&value);
            result.DrawBuffers[i]=static_cast<GLenum>(value);
        }
        return result;
    }

    void Restore() const {
        glViewport(Viewport[0],Viewport[1],Viewport[2],Viewport[3]);
        glScissor(ScissorBox[0],ScissorBox[1],ScissorBox[2],ScissorBox[3]);
        glBindFramebuffer(GL_DRAW_FRAMEBUFFER,Draw);
        glDrawBuffers(static_cast<GLsizei>(DrawBuffers.size()),DrawBuffers.data());
        glBindFramebuffer(GL_READ_FRAMEBUFFER,Draw); glReadBuffer(DrawReadBuffer);
        glBindFramebuffer(GL_READ_FRAMEBUFFER,Read); glReadBuffer(ReadBuffer);
        glUseProgram(Program); glBindVertexArray(VAO);
        glActiveTexture(GL_TEXTURE0); glBindTexture(GL_TEXTURE_2D,Texture0); glActiveTexture(ActiveTexture);
        if (Depth) glEnable(GL_DEPTH_TEST); else glDisable(GL_DEPTH_TEST);
        if (Blend) glEnable(GL_BLEND); else glDisable(GL_BLEND);
        if (Scissor) glEnable(GL_SCISSOR_TEST); else glDisable(GL_SCISSOR_TEST);
        if (Stencil) glEnable(GL_STENCIL_TEST); else glDisable(GL_STENCIL_TEST);
    }

    bool operator==(const State&) const=default;
};

// Called inside the existing native GPU regression's live hidden GL context.
inline void Run() {
    using namespace TomCat;
    GLint maxBuffers=0; glGetIntegerv(GL_MAX_DRAW_BUFFERS,&maxBuffers);
    struct RestoreState { State Saved; ~RestoreState(){Saved.Restore();} } restore{State::Capture(maxBuffers)};
    FramebufferSpecification specification;
    specification.Width=specification.Height=64;
    specification.Attachments={FramebufferTextureFormat::RGBA8};
    auto target=Framebuffer::Create(specification);
    auto separateRead=Framebuffer::Create(specification);
    separateRead->Bind();
    GLint readFramebuffer=0; glGetIntegerv(GL_READ_FRAMEBUFFER_BINDING,&readFramebuffer);
    glReadBuffer(GL_NONE);
    target->Bind(); glReadBuffer(GL_COLOR_ATTACHMENT0);
    glBindFramebuffer(GL_READ_FRAMEBUFFER,readFramebuffer);
    glActiveTexture(GL_TEXTURE0); glBindTexture(GL_TEXTURE_2D,separateRead->GetColorAttachmentRendererID());
    glActiveTexture(GL_TEXTURE3);
    glScissor(0,0,64,64);
    glEnable(GL_DEPTH_TEST); glEnable(GL_BLEND); glEnable(GL_SCISSOR_TEST); glEnable(GL_STENCIL_TEST);

    Calls calls;
    struct GradeRenderer : OpenGLRendererAPI { ~GradeRenderer(){ReleaseColorGrade();} } renderer;
    renderer.Init();
    Require(calls.CapQueries==1,"color grade did not cache context draw-buffer capability at Init");
    const auto clear=[&] {
        const GLenum output=GL_COLOR_ATTACHMENT0; glDrawBuffers(1,&output);
        const GLfloat color[]{.8f,.2f,.1f,.5f}; glClearBufferfv(GL_COLOR,0,color);
        // A nondefault source draw-buffer state must survive even a failed pass.
        const GLenum disabled=GL_NONE; glDrawBuffers(1,&disabled);
    };
    const auto pixel=[&] {
        std::vector<uint8_t> pixels;
        Require(target->ReadColorAttachment(0,pixels),"color-grade test GPU readback failed");
        constexpr size_t offset=(16*64+16)*4;
        return std::array<uint8_t,4>{pixels[offset],pixels[offset+1],pixels[offset+2],pixels[offset+3]};
    };
    const auto grade=[&](bool expectGraded=true) {
        clear();
        const auto before=State::Capture(maxBuffers);
        renderer.ApplyColorGrade(-1,0,0);
        Require(before==State::Capture(maxBuffers),"color grade changed caller GL state");
        const auto rgba=pixel();
        if (expectGraded) {
            Require(std::abs(int(rgba[0])-int(rgba[1]))<=1 && std::abs(int(rgba[1])-int(rgba[2]))<=1
                && rgba[0]>38 && rgba[0]<45,"cached/resized color-grade GPU pixel mismatch");
        } else {
            Require(std::abs(int(rgba[0])-204)<=1 && std::abs(int(rgba[1])-51)<=1,
                "incomplete color-grade framebuffer modified the source");
        }
        Require(std::abs(int(rgba[3])-128)<=1,"cached color grade changed source alpha");
        Require(glGetError()==GL_NO_ERROR,"color-grade cache/restore generated a GL error");
    };

    grade();
    Require(calls.Attachments==1 && calls.Checks==1 && calls.Images==1,"first color grade did not prepare one framebuffer");
    for (unsigned frame=0;frame<8;++frame) grade();
    Require(calls.Attachments==1 && calls.Checks==1 && calls.Images==1 && calls.CapQueries==1,
        "steady color grade repeated attachment, storage, completeness or capability queries");

    glViewport(0,0,48,40); grade();
    Require(calls.Attachments==2 && calls.Checks==2 && calls.Images==2,"resized color grade did not revalidate storage");
    grade();
    Require(calls.Checks==2,"resized color grade did not reuse completeness");

    glViewport(0,0,32,32); calls.DropNextImage=true; grade(false);
    Require(!calls.DropNextImage && calls.Checks==3 && calls.Images==3,"allocation-failure test did not execute");
    grade();
    Require(calls.Attachments==4 && calls.Checks==4 && calls.Images==4,"incomplete color grade was cached instead of retried");
    grade();
    Require(calls.Checks==4,"recovered color grade did not reuse completeness");

    renderer.ReleaseColorGrade(); grade();
    Require(calls.Attachments==5 && calls.Checks==5 && calls.Images==5 && calls.CapQueries==1,
        "released color grade reused stale resources/completeness or requeried the same context capability");

    // Exercise the common read==draw path as well as the distinct read bindings above.
    target->Bind(); glReadBuffer(GL_COLOR_ATTACHMENT0); grade(); grade();
    Require(calls.Checks==6 && calls.CapQueries==1,"same-framebuffer grading did not cache resized storage");
    renderer.ReleaseColorGrade();
    renderer.Init();
    Require(calls.CapQueries==2,"renderer reinitialization did not refresh context capabilities");
}

} // namespace OpenGLColorGradeRegression
