#pragma once

#include "platform/OpenGL/OpenGLRendererAPI.h"
#include "TomCat/Renderer/Framebuffer.h"

#include <glad/glad.h>
#include <array>
#include <cmath>
#include <stdexcept>
#include <vector>

namespace OpenGLClearRegression {

// Native reference only: the Web branch is tested by web-clear-regression.html.
// In particular, do not turn native Clear into a cache of SetClearColor calls.
inline void Run() {
    using namespace TomCat;
    const auto require=[](bool value,const char* message){if(!value)throw std::runtime_error(message);};
    struct Restore {
        GLint Draw=0,Read=0;
        std::array<GLint,4> Viewport{};
        std::array<GLfloat,4> Color{};
        Restore(){
            glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING,&Draw);glGetIntegerv(GL_READ_FRAMEBUFFER_BINDING,&Read);
            glGetIntegerv(GL_VIEWPORT,Viewport.data());glGetFloatv(GL_COLOR_CLEAR_VALUE,Color.data());
        }
        ~Restore(){
            glBindFramebuffer(GL_DRAW_FRAMEBUFFER,Draw);glBindFramebuffer(GL_READ_FRAMEBUFFER,Read);
            glViewport(Viewport[0],Viewport[1],Viewport[2],Viewport[3]);glClearColor(Color[0],Color[1],Color[2],Color[3]);
        }
    } restore;
    FramebufferSpecification specification;
    specification.Width=specification.Height=16;
    specification.Attachments={FramebufferTextureFormat::RGBA8};
    auto target=Framebuffer::Create(specification);target->Bind();
    const auto pixel=[&] {
        std::vector<uint8_t> pixels;
        require(target->ReadColorAttachment(0,pixels),"native clear GPU readback failed");
        constexpr size_t offset=(8*16+8)*4;
        return std::array<uint8_t,4>{pixels[offset],pixels[offset+1],pixels[offset+2],pixels[offset+3]};
    };
    const auto expect=[&](std::array<int,4> expected) {
        const auto value=pixel();
        for(size_t index=0;index<4;++index)
            require(std::abs(int(value[index])-expected[index])<=1,"native clear changed direct GL/clamp semantics");
        require(glGetError()==GL_NO_ERROR,"native clear regression generated a GL error");
    };
    glClearColor(.2f,.4f,.6f,1.f);
    OpenGLRendererAPI renderer;renderer.Init();renderer.Clear();
    expect({51,102,153,255});
    renderer.SetClearColor({.9f,.1f,.2f,1.f});
    glClearColor(.1f,.6f,.3f,.5f); // Native caller intentionally bypasses RendererAPI.
    renderer.Clear();expect({26,153,77,128});
    renderer.SetClearColor({-3.f,.5f,5.f,2.f});
    renderer.Clear();expect({0,128,255,255});
}

} // namespace OpenGLClearRegression
