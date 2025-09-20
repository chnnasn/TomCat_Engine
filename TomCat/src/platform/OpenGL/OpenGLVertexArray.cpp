#include "tcpch.h"
#include "OpenGLVertexArray.h"
#include <glad/glad.h>

namespace TomCat {



	static GLenum ShaderDataTypeToOpenGLBaseType(ShaderDataType type)
	{
		switch (type)
		{
			case TomCat::ShaderDataType::Float:    return GL_FLOAT;
			case TomCat::ShaderDataType::Float2:   return GL_FLOAT;
			case TomCat::ShaderDataType::Float3:   return GL_FLOAT;
			case TomCat::ShaderDataType::Float4:   return GL_FLOAT;
			case TomCat::ShaderDataType::Mat3:     return GL_FLOAT;
			case TomCat::ShaderDataType::Mat4:     return GL_FLOAT;
			case TomCat::ShaderDataType::Int:      return GL_INT;
			case TomCat::ShaderDataType::Int2:     return GL_INT;
			case TomCat::ShaderDataType::Int3:     return GL_INT;
			case TomCat::ShaderDataType::Int4:     return GL_INT;
			case TomCat::ShaderDataType::Bool:     return GL_BOOL;
		}

		TC_Core_Assert(false, "Unknown ShaderDataType!");
		return 0;
	}



	OpenGLVertexArray::OpenGLVertexArray()
	{

		glCreateVertexArrays(1, &m_RendererID);
	
	}

	OpenGLVertexArray::~OpenGLVertexArray()
	{

		glDeleteVertexArrays(1,&m_RendererID);
	}

	void OpenGLVertexArray::Bind() const
	{
		glBindVertexArray(m_RendererID);
	}

	void OpenGLVertexArray::Unbind() const
	{
		glBindVertexArray(0);
	}

	void OpenGLVertexArray::AddVertexBuffer(const Ref<VertexBuffer>& vertexBuffer)
	{
		TC_Core_Assert("Vertex Buffer has no layout");

		glBindVertexArray(m_RendererID);
		vertexBuffer->Bind();

		uint32_t index = 0;
		const auto& layout = vertexBuffer->GetLayout();
		for (const auto& element : layout)
		{
			glEnableVertexAttribArray(index);
			glVertexAttribPointer(index,
				element.GetComponentCount(),
				ShaderDataTypeToOpenGLBaseType(element.Type),
				element.Normalized ? GL_TRUE : GL_FALSE,
				layout.GetStride(),
				(const void*)element.Offset);
			index++;
		}
		m_VertexBuffers.push_back(vertexBuffer);

	}

	void OpenGLVertexArray::SetIndexBuffer(const Ref<IndexBuffer>& indexBuffer)
	{
		glBindVertexArray(m_RendererID);
		indexBuffer->Bind();

		m_IndexBuffer = indexBuffer;
	}

}