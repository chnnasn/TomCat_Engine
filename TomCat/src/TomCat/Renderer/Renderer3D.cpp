#include "tcpch.h"
#include "TomCat/Renderer/Renderer3D.h"

#include "TomCat/Renderer/RenderCommand.h"
#include "TomCat/Renderer/Shader.h"
#include "TomCat/Renderer/UniformBuffer.h"

#include <glad/glad.h>
#include <glm/gtc/matrix_inverse.hpp>

namespace TomCat {

	struct CameraData
	{
		glm::mat4 ViewProjection;
		glm::vec4 ViewPosition;
	};

	struct MaterialData
	{
		glm::mat4 Model;
		glm::mat4 NormalMatrix;
		glm::vec4 AlbedoColor;
		glm::vec4 LightDirection;
		glm::vec4 LightColor;
		glm::vec4 Params; // x = useTexture, y = ambientStrength, z = shininess, w = entityID
	};

	struct Renderer3DData
	{
		Ref<Shader> LitShader;
		Ref<UniformBuffer> CameraUniformBuffer;
		Ref<UniformBuffer> MaterialUniformBuffer;
		CameraData CameraBuffer;
		Renderer3D::Statistics Stats;
	};

	static Renderer3DData s_Data;

	void Renderer3D::Init()
	{
		TC_PROFILE_FUNCTION();

		s_Data.LitShader = Shader::Create("Packages/shaders/Lit3D.glsl");
		s_Data.CameraUniformBuffer = UniformBuffer::Create(sizeof(CameraData), 0);
		s_Data.MaterialUniformBuffer = UniformBuffer::Create(sizeof(MaterialData), 1);
	}

	void Renderer3D::Shutdown()
	{
	}

	void Renderer3D::BeginScene(const EditorCamera& camera)
	{
		s_Data.CameraBuffer.ViewProjection = camera.GetViewProjection();
		s_Data.CameraBuffer.ViewPosition = glm::vec4(camera.GetPosition(), 1.0f);
		s_Data.CameraUniformBuffer->SetData(&s_Data.CameraBuffer, sizeof(CameraData));

		glEnable(GL_DEPTH_TEST);
		glDepthFunc(GL_LESS);
		glDisable(GL_CULL_FACE);
	}

	void Renderer3D::BeginScene(const Camera& camera, const glm::mat4& cameraTransform)
	{
		s_Data.CameraBuffer.ViewProjection = camera.GetProjection() * glm::inverse(cameraTransform);
		s_Data.CameraBuffer.ViewPosition = glm::vec4(glm::vec3(cameraTransform[3]), 1.0f);
		s_Data.CameraUniformBuffer->SetData(&s_Data.CameraBuffer, sizeof(CameraData));

		glEnable(GL_DEPTH_TEST);
		glDepthFunc(GL_LESS);
		glDisable(GL_CULL_FACE);
	}

	void Renderer3D::EndScene()
	{
	}

	void Renderer3D::Flush()
	{
	}

	void Renderer3D::DrawMesh(const Ref<Mesh>& mesh, const glm::mat4& transform,
		const Ref<Texture2D>& albedoTexture, const glm::vec4& color, bool useTexture, int entityID)
	{
		if (!mesh || !mesh->GetVertexArray() || !s_Data.LitShader)
			return;

		MaterialData material;
		material.Model = transform;
		material.NormalMatrix = glm::mat4(glm::inverseTranspose(glm::mat3(transform)));
		material.AlbedoColor = color;
		material.LightDirection = glm::vec4(-0.5f, -1.0f, -0.5f, 0.0f);
		material.LightColor = glm::vec4(1.0f, 1.0f, 1.0f, 1.0f);
		material.Params = glm::vec4(useTexture ? 1.0f : 0.0f, 0.25f, 32.0f, (float)entityID);
		s_Data.MaterialUniformBuffer->SetData(&material, sizeof(MaterialData));

		s_Data.LitShader->Bind();
		s_Data.LitShader->SetInt("u_AlbedoTexture", 0);

		if (useTexture && albedoTexture)
			albedoTexture->Bind(0);

		mesh->GetVertexArray()->Bind();
		RenderCommand::DrawIndexed(mesh->GetVertexArray(), mesh->GetIndexCount());

		s_Data.Stats.DrawCalls++;
		s_Data.Stats.MeshCount++;
	}

	void Renderer3D::ResetStats()
	{
		s_Data.Stats = Statistics{};
	}

	Renderer3D::Statistics Renderer3D::GetStats()
	{
		return s_Data.Stats;
	}

}
