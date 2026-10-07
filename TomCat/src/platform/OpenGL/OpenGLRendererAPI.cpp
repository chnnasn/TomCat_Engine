#include "tcpch.h"

#include"OpenGLRendererAPI.h"

#include "OpenGLApi.h"

#include <cmath>

namespace TomCat {

    void OpenGLRendererAPI::ReleaseColorGrade() {
        m_GradeShader.reset(); m_GradeUniform.reset();
        if (m_GradeTexture) glDeleteTextures(1,&m_GradeTexture);
        if (m_GradeFramebuffer) glDeleteFramebuffers(1,&m_GradeFramebuffer);
        if (m_GradeVAO) glDeleteVertexArrays(1,&m_GradeVAO);
        m_GradeTexture=m_GradeFramebuffer=m_GradeVAO=0; m_GradeWidth=m_GradeHeight=0;
    }
    void OpenGLRendererAPI::ApplyColorGrade(float exposure,float saturation,float vignette) {
        if (!std::isfinite(exposure) || !std::isfinite(saturation) || !std::isfinite(vignette)) return;
        if (exposure==0 && saturation==1 && vignette==0) return;
        GLint viewport[4]{},draw=0,read=0,readBuffer=0,drawReadBuffer=0,program=0,vao=0,activeTexture=0,texture=0;
        glGetIntegerv(GL_VIEWPORT,viewport); if (viewport[2]<=0 || viewport[3]<=0) return;
        glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING,&draw); glGetIntegerv(GL_READ_FRAMEBUFFER_BINDING,&read);
        glGetIntegerv(GL_READ_BUFFER,&readBuffer); glGetIntegerv(GL_CURRENT_PROGRAM,&program);
        glBindFramebuffer(GL_READ_FRAMEBUFFER,draw); glGetIntegerv(GL_READ_BUFFER,&drawReadBuffer);
        glBindFramebuffer(GL_READ_FRAMEBUFFER,read);
        glGetIntegerv(GL_VERTEX_ARRAY_BINDING,&vao); glGetIntegerv(GL_ACTIVE_TEXTURE,&activeTexture);
        glActiveTexture(GL_TEXTURE0); glGetIntegerv(GL_TEXTURE_BINDING_2D,&texture);
        const bool depth=glIsEnabled(GL_DEPTH_TEST), blend=glIsEnabled(GL_BLEND), scissor=glIsEnabled(GL_SCISSOR_TEST), stencil=glIsEnabled(GL_STENCIL_TEST);
        GLint maxBuffers=0; glGetIntegerv(GL_MAX_DRAW_BUFFERS,&maxBuffers);
        std::vector<GLenum> buffers(static_cast<size_t>(draw ? maxBuffers : 1));
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
            m_GradeUniform=UniformBuffer::Create(sizeof(glm::vec4),2);
            glGenTextures(1,&m_GradeTexture); glGenFramebuffers(1,&m_GradeFramebuffer); glGenVertexArrays(1,&m_GradeVAO);
        }
        glBindTexture(GL_TEXTURE_2D,m_GradeTexture);
        if(m_GradeWidth!=viewport[2] || m_GradeHeight!=viewport[3]) {
            glTexImage2D(GL_TEXTURE_2D,0,GL_RGBA8,viewport[2],viewport[3],0,GL_RGBA,GL_UNSIGNED_BYTE,nullptr);
            glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,GL_LINEAR);glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,GL_LINEAR);
            glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_S,GL_CLAMP_TO_EDGE);glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_T,GL_CLAMP_TO_EDGE);
            m_GradeWidth=viewport[2];m_GradeHeight=viewport[3];
        }
        glBindFramebuffer(GL_DRAW_FRAMEBUFFER,m_GradeFramebuffer);
        glFramebufferTexture2D(GL_DRAW_FRAMEBUFFER,GL_COLOR_ATTACHMENT0,GL_TEXTURE_2D,m_GradeTexture,0);
        const GLenum destination=GL_COLOR_ATTACHMENT0;glDrawBuffers(1,&destination);
        if(glCheckFramebufferStatus(GL_DRAW_FRAMEBUFFER)!=GL_FRAMEBUFFER_COMPLETE) return;
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

	void OpenGLRendererAPI::Init()
	{
		TC_PROFILE_FUNCTION();

		glEnable(GL_BLEND);
		glBlendFunc(GL_SRC_ALPHA,GL_ONE_MINUS_SRC_ALPHA);

		glEnable(GL_DEPTH_TEST);
	}

	void OpenGLRendererAPI::SetViewport(uint32_t x, uint32_t y, uint32_t width, uint32_t height)
	{
		glViewport(x, y, width, height);
	}

	void OpenGLRendererAPI::SetClearColor(const glm::vec4& color)
	{
		glClearColor(color.r, color.g, color.b, color.a);
	}

	void OpenGLRendererAPI::Clear()
	{
#ifdef __EMSCRIPTEN__
        // The editor's framebuffer mixes RGBA and integer picking attachments.
        // WebGL rejects a floating-point glClear across both attachment types.
        // Picking is cleared explicitly by the framebuffer owner.
        GLfloat color[4]; glGetFloatv(GL_COLOR_CLEAR_VALUE, color);
        glClearBufferfv(GL_COLOR, 0, color);
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
