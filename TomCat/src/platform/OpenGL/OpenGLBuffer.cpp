#include "tcpch.h"
#include"OpenGLBuffer.h"

#include "OpenGLApi.h"

namespace TomCat {

	OpenGLVertexBuffer::OpenGLVertexBuffer(uint32_t size)
		: m_Size(size)
	{
		TC_PROFILE_FUNCTION();

		glCreateBuffers(1, &m_RendererID);

		glBindBuffer(GL_ARRAY_BUFFER, m_RendererID);

		glBufferData(GL_ARRAY_BUFFER, size,nullptr, GL_DYNAMIC_DRAW);
		ProfileResourceTracker::Get().Buffer(1, m_Size);

	}

	OpenGLVertexBuffer::OpenGLVertexBuffer(float* vertices, uint32_t size)
		: m_Size(size)
	{
		TC_PROFILE_FUNCTION();

		glCreateBuffers(1,&m_RendererID);

		glBindBuffer(GL_ARRAY_BUFFER, m_RendererID);

		glBufferData(GL_ARRAY_BUFFER, size, vertices, GL_STATIC_DRAW);
		ProfileResourceTracker::Get().Buffer(1, m_Size);

	}

	OpenGLVertexBuffer::~OpenGLVertexBuffer()
	{
		TC_PROFILE_FUNCTION();

		glDeleteBuffers(1,&m_RendererID);
		ProfileResourceTracker::Get().Buffer(-1, -static_cast<int64_t>(m_Size));
	}


	void OpenGLVertexBuffer::Bind() const
	{
		TC_PROFILE_FUNCTION();

		glBindBuffer(GL_ARRAY_BUFFER, m_RendererID);

	}

	void OpenGLVertexBuffer::Unbind() const
	{
		TC_PROFILE_FUNCTION();

		glBindBuffer(GL_ARRAY_BUFFER,0);

	}


	void OpenGLVertexBuffer::SetData(const void* data, uint32_t size)
	{
		glBindBuffer(GL_ARRAY_BUFFER, m_RendererID);
		glBufferSubData(GL_ARRAY_BUFFER, 0, size, data);
	}


	OpenGLIndexBuffer::OpenGLIndexBuffer(uint32_t* indices, uint32_t  count): m_Count(count)
	{
		TC_PROFILE_FUNCTION();

		glCreateBuffers(1,&m_RendererID);

		glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, m_RendererID);

		glBufferData(GL_ELEMENT_ARRAY_BUFFER, count *sizeof(uint32_t), indices, GL_STATIC_DRAW);
		ProfileResourceTracker::Get().Buffer(1, static_cast<int64_t>(m_Count) * sizeof(uint32_t));

	}

	OpenGLIndexBuffer::~OpenGLIndexBuffer()
	{
		TC_PROFILE_FUNCTION();

		glDeleteBuffers(1,&m_RendererID);
		ProfileResourceTracker::Get().Buffer(-1, -static_cast<int64_t>(m_Count) * sizeof(uint32_t));
	}


	void OpenGLIndexBuffer::Bind() const
	{
		TC_PROFILE_FUNCTION();

		glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, m_RendererID);

	}

	void OpenGLIndexBuffer::Unbind() const
	{
		TC_PROFILE_FUNCTION();

		glBindBuffer(GL_ELEMENT_ARRAY_BUFFER,0);

	}


}
