#include "tcpch.h"
#include "TomCat/Renderer/Renderer3D.h"

#include "TomCat/Renderer/RenderCommand.h"
#include "TomCat/Renderer/Shader.h"
#include "TomCat/Renderer/UniformBuffer.h"
#include "TomCat/Renderer/Model.h"

#include "platform/OpenGL/OpenGLApi.h"
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
		glm::vec4 Params; // x = useTexture, y = ambientStrength, z = shininess
		glm::ivec4 Entity;
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

		if (s_Data.LitShader) return;
		s_Data.LitShader = Shader::Create("TomCat.Renderer3D.Lit",
R"GLSL(#version 450 core

layout(location = 0) in vec3 a_Position;
layout(location = 1) in vec3 a_Normal;
layout(location = 2) in vec2 a_TexCoord;

layout(std140, binding = 0) uniform Camera
{
	mat4 u_ViewProjection;
	vec4 u_ViewPosition;
};

layout(std140, binding = 1) uniform Material
{
	mat4 u_Model;
	mat4 u_NormalMatrix;
	vec4 u_AlbedoColor;
	vec4 u_LightDirection;
	vec4 u_LightColor;
	vec4 u_Params;
	ivec4 u_Entity;
};

layout(location = 0) out vec3 v_FragPos;
layout(location = 1) out vec3 v_Normal;
layout(location = 2) out vec2 v_TexCoord;
layout(location = 3) out flat int v_EntityID;

void main()
{
	vec4 worldPos = u_Model * vec4(a_Position, 1.0);
	v_FragPos = worldPos.xyz;
	v_Normal = normalize(mat3(u_NormalMatrix) * a_Normal);
	v_TexCoord = a_TexCoord;
	v_EntityID = u_Entity.x;

	gl_Position = u_ViewProjection * worldPos;
})GLSL",
R"GLSL(#version 450 core

layout(location = 0) out vec4 color;
layout(location = 1) out int color2;

layout(location = 0) in vec3 v_FragPos;
layout(location = 1) in vec3 v_Normal;
layout(location = 2) in vec2 v_TexCoord;
layout(location = 3) in flat int v_EntityID;

layout(std140, binding = 0) uniform Camera
{
	mat4 u_ViewProjection;
	vec4 u_ViewPosition;
};

layout(std140, binding = 1) uniform Material
{
	mat4 u_Model;
	mat4 u_NormalMatrix;
	vec4 u_AlbedoColor;
	vec4 u_LightDirection;
	vec4 u_LightColor;
	vec4 u_Params;
	ivec4 u_Entity;
};

layout(binding = 2) uniform sampler2D u_AlbedoTexture;

void main()
{
	vec3 albedo = u_AlbedoColor.rgb;
	if (u_Params.x > 0.5)
		albedo *= texture(u_AlbedoTexture, v_TexCoord).rgb;

	vec3 N = normalize(v_Normal);
	vec3 L = normalize(-u_LightDirection.xyz);
	vec3 V = normalize(u_ViewPosition.xyz - v_FragPos);

	vec3 ambient = u_Params.y * u_LightColor.rgb;

	float diff = max(dot(N, L), 0.0);
	vec3 diffuse = diff * u_LightColor.rgb;

	vec3 H = normalize(L + V);
	float spec = pow(max(dot(N, H), 0.0), u_Params.z);
	vec3 specular = spec * u_LightColor.rgb;

	vec3 result = (ambient + diffuse + specular) * albedo;
	color = vec4(result, u_AlbedoColor.a);
	color2 = v_EntityID;
})GLSL");
		s_Data.CameraUniformBuffer = UniformBuffer::Create(sizeof(CameraData), 0);
		s_Data.MaterialUniformBuffer = UniformBuffer::Create(sizeof(MaterialData), 1);
	}

	void Renderer3D::Shutdown()
	{
		s_Data = {};
	}

	void Renderer3D::BeginScene(const EditorCamera& camera)
	{
		Init();
		s_Data.CameraBuffer.ViewProjection = camera.GetViewProjection();
		s_Data.CameraBuffer.ViewPosition = glm::vec4(camera.GetPosition(), 1.0f);
		s_Data.CameraUniformBuffer->SetData(&s_Data.CameraBuffer, sizeof(CameraData));

		glEnable(GL_DEPTH_TEST);
		glDepthFunc(GL_LESS);
		glDisable(GL_CULL_FACE);
	}

	void Renderer3D::BeginScene(const Camera& camera, const glm::mat4& cameraTransform)
	{
		Init();
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
		const glm::mat3 basis(transform);
		material.NormalMatrix = std::abs(glm::determinant(basis)) > 1e-8f
			? glm::mat4(glm::inverseTranspose(basis)) : glm::mat4(1.0f);
		material.AlbedoColor = color;
		material.LightDirection = glm::vec4(-0.5f, -1.0f, -0.5f, 0.0f);
		material.LightColor = glm::vec4(1.0f, 1.0f, 1.0f, 1.0f);
		material.Params = glm::vec4((useTexture && albedoTexture) ? 1.0f : 0.0f, 0.25f, 32.0f, 0.0f);
		material.Entity = glm::ivec4(entityID, 0, 0, 0);
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

	void Renderer3D::DrawModel(const Ref<Model>& model, const glm::mat4& transform, int entityID, const glm::vec4& tint, const Ref<Texture2D>& texture)
	{
		if (!model)
			return;

		for (const auto& submesh : model->GetSubmeshes())
		{
			if (!submesh.Mesh)
				continue;

			DrawMesh(submesh.Mesh, transform, texture ? texture : submesh.DiffuseTexture, submesh.DiffuseColor * tint, texture || submesh.UseTexture, entityID);
		}
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
