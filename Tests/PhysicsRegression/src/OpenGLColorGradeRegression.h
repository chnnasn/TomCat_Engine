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
    GLint Program=0, VAO=0, ActiveTexture=0, Texture0=0, Texture1=0;
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
        glActiveTexture(GL_TEXTURE1);
        glGetIntegerv(GL_TEXTURE_BINDING_2D,&result.Texture1);
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
        glActiveTexture(GL_TEXTURE0); glBindTexture(GL_TEXTURE_2D,Texture0);
        glActiveTexture(GL_TEXTURE1); glBindTexture(GL_TEXTURE_2D,Texture1);
        glActiveTexture(ActiveTexture);
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

// Palette-swap and mask-transition passes follow the same contract as the
// color grade: exact pixel output, untouched caller state, lazily allocated
// private copy targets that resize once and are not revalidated per frame.
inline void RunPaletteAndMaskPasses() {
    using namespace TomCat;
    GLint maxBuffers=0; glGetIntegerv(GL_MAX_DRAW_BUFFERS,&maxBuffers);
    struct RestoreState { State Saved; ~RestoreState(){Saved.Restore();} } restore{State::Capture(maxBuffers)};
    FramebufferSpecification specification;
    specification.Width=specification.Height=64;
    specification.Attachments={FramebufferTextureFormat::RGBA8};
    auto target=Framebuffer::Create(specification);
    target->Bind(); glReadBuffer(GL_COLOR_ATTACHMENT0);
    glViewport(0,0,64,64);

    Calls calls;
    struct PassRenderer : OpenGLRendererAPI { ~PassRenderer(){ReleaseColorGrade();} } renderer;
    renderer.Init();

    const auto clear=[&](float r,float g,float b,float a) {
        const GLenum output=GL_COLOR_ATTACHMENT0; glDrawBuffers(1,&output);
        const GLfloat color[]{r,g,b,a}; glClearBufferfv(GL_COLOR,0,color);
    };
    const auto pixel=[&] {
        std::vector<uint8_t> pixels;
        Require(target->ReadColorAttachment(0,pixels),"post-process test GPU readback failed");
        constexpr size_t offset=(16*64+16)*4;
        return std::array<uint8_t,4>{pixels[offset],pixels[offset+1],pixels[offset+2],pixels[offset+3]};
    };
    const auto preserve=[&](auto&& pass, const char* message) {
        const auto before=State::Capture(maxBuffers);
        pass();
        Require(before==State::Capture(maxBuffers),message);
    };

    // Palette swap: matched source colors repaint, everything else passes through.
    const glm::vec4 oldColors[4]={{0.8f,0.2f,0.1f,1.0f},{0.15f,0.25f,0.35f,1.0f},
        {0.4f,0.5f,0.6f,1.0f},{0.9f,0.8f,0.7f,1.0f}};
    const glm::vec4 newColors[4]={{0.1f,0.9f,0.3f,1.0f},{0.05f,0.05f,0.6f,1.0f},
        {0.7f,0.15f,0.2f,1.0f},{0.25f,0.5f,0.75f,1.0f}};
    clear(0.8f,0.2f,0.1f,1.0f);
    preserve([&]{ renderer.ApplyPaletteSwap(oldColors,newColors,0.02f); },
        "palette pass changed caller GL state");
    {
        const auto rgba=pixel();
        Require(std::abs(int(rgba[0])-26)<=1 && std::abs(int(rgba[1])-230)<=1
            && std::abs(int(rgba[2])-77)<=1,
            "palette pass did not repaint the matched source color");
        Require(glGetError()==GL_NO_ERROR,"palette pass generated a GL error");
    }
    clear(0.5f,0.5f,0.5f,1.0f);
    renderer.ApplyPaletteSwap(oldColors,newColors,0.02f);
    {
        const auto rgba=pixel();
        Require(std::abs(int(rgba[0])-128)<=1 && std::abs(int(rgba[1])-128)<=1,
            "palette pass repainted a color outside the palette");
    }
    // Tolerance: slightly drifted source color still matches.
    clear(0.81f,0.19f,0.09f,1.0f);
    renderer.ApplyPaletteSwap(oldColors,newColors,0.02f);
    Require(std::abs(int(pixel()[1])-230)<=1,"palette threshold did not absorb the drift");
    // Steady state: one storage allocation, one attachment, one completeness check.
    Require(calls.Attachments==1 && calls.Checks==1 && calls.Images==1,
        "first palette pass did not allocate exactly one copy target");
    const unsigned paletteBaseAttachments=calls.Attachments, paletteBaseChecks=calls.Checks;
    renderer.ApplyPaletteSwap(oldColors,newColors,0.02f);
    Require(calls.Attachments==paletteBaseAttachments && calls.Checks==paletteBaseChecks,
        "steady palette pass revalidated its copy target");
    // Resize: storage revalidated once, then reused.
    glViewport(0,0,48,40);
    renderer.ApplyPaletteSwap(oldColors,newColors,0.02f);
    Require(calls.Attachments==2 && calls.Checks==2 && calls.Images==2,
        "resized palette pass did not revalidate storage");
    glViewport(0,0,64,64);

    // Mask transition: mask alpha selects which pixels progress covers.
    const auto mask=Texture2D::Create(64,64);
    std::vector<uint8_t> maskBytes(64*64*4);
    for (size_t i=0;i<64*64;++i) {
        maskBytes[i*4+0]=255; maskBytes[i*4+1]=255; maskBytes[i*4+2]=255;
        maskBytes[i*4+3]=128; // ~0.502
    }
    mask->SetData(maskBytes.data(),static_cast<uint32_t>(maskBytes.size()));
    clear(0.8f,0.2f,0.1f,1.0f);
    preserve([&]{ renderer.ApplyMaskTransition(mask,0.4f,glm::vec4(0.0f,0.0f,0.0f,1.0f)); },
        "mask pass changed caller GL state");
    {
        const auto rgba=pixel();
        Require(std::abs(int(rgba[0])-204)<=2 && std::abs(int(rgba[1])-51)<=2,
            "mask pass covered pixels its mask kept visible (progress below mask alpha)");
        Require(glGetError()==GL_NO_ERROR,"mask pass generated a GL error");
    }
    renderer.ApplyMaskTransition(mask,0.7f,glm::vec4(0.0f,0.0f,0.0f,1.0f));
    {
        const auto rgba=pixel();
        Require(rgba[0]<=2 && rgba[1]<=2 && rgba[2]<=2,
            "mask pass did not cover pixels below its progress");
    }
    // Neutral progress leaves the source untouched.
    clear(0.25f,0.5f,0.75f,1.0f);
    renderer.ApplyMaskTransition(mask,0.0f,glm::vec4(1.0f,1.0f,1.0f,1.0f));
    {
        const auto rgba=pixel();
        Require(std::abs(int(rgba[0])-64)<=2 && std::abs(int(rgba[2])-191)<=2,
            "zero-progress mask pass modified the source");
    }
    // A missing mask never runs the pass.
    clear(0.25f,0.5f,0.75f,1.0f);
    renderer.ApplyMaskTransition(nullptr,1.0f,glm::vec4(0.0f,0.0f,0.0f,1.0f));
    {
        const auto rgba=pixel();
        Require(std::abs(int(rgba[0])-64)<=2,"missing-mask pass modified the source");
    }
    // Steady state: the mask copy target is allocated once and then reused.
    const unsigned steadyAttachments=calls.Attachments, steadyChecks=calls.Checks, steadyImages=calls.Images;
    renderer.ApplyMaskTransition(mask,0.7f,glm::vec4(0.0f,0.0f,0.0f,1.0f));
    Require(calls.Attachments==steadyAttachments && calls.Checks==steadyChecks
        && calls.Images==steadyImages,
        "steady mask pass revalidated its copy target");
    Require(steadyAttachments>paletteBaseAttachments && steadyChecks>paletteBaseChecks,
        "first masked pass did not allocate its own copy target");
    // Zero progress and the missing mask never touch GL resources.
    renderer.ApplyMaskTransition(mask,0.0f,glm::vec4(1.0f,1.0f,1.0f,1.0f));
    renderer.ApplyMaskTransition(nullptr,1.0f,glm::vec4(0.0f,0.0f,0.0f,1.0f));
    Require(calls.Attachments==steadyAttachments && calls.Checks==steadyChecks
        && calls.Images==steadyImages,
        "neutral mask passes allocated or revalidated resources");
    Require(glGetError()==GL_NO_ERROR,"palette/mask regression left a GL error");
}

} // namespace OpenGLColorGradeRegression
