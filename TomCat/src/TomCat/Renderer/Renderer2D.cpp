#include "tcpch.h"
#include "TomCat/Renderer/Renderer2D.h"

#include "TomCat/Renderer/VertexArray.h"
#include "TomCat/Renderer/Shader.h"
#include "TomCat/Renderer/UniformBuffer.h"
#include "TomCat/Renderer/RenderCommand.h"
#include "TomCat/Asset/AssetManager.h"
#include "TomCat/Asset/SpriteAsset.h"
#include <glm/gtc/type_ptr.hpp>

#include <glm/gtc/matrix_transform.hpp>
#include <cmath>
#include <vector>

namespace TomCat {

	bool Renderer2D::IsQuadVisible(const glm::mat4& worldTransform,
		const glm::mat4& viewProjection)
	{
		const glm::mat4 clipTransform = viewProjection * worldTransform;
		const glm::vec4 corners[] = {
			clipTransform * glm::vec4(-0.5f, -0.5f, 0.0f, 1.0f),
			clipTransform * glm::vec4( 0.5f, -0.5f, 0.0f, 1.0f),
			clipTransform * glm::vec4( 0.5f,  0.5f, 0.0f, 1.0f),
			clipTransform * glm::vec4(-0.5f,  0.5f, 0.0f, 1.0f)
		};
		for (const glm::vec4& corner : corners)
		{
			if (!std::isfinite(corner.x) || !std::isfinite(corner.y)
				|| !std::isfinite(corner.z) || !std::isfinite(corner.w))
				return true;
		}
		auto whollyOutside = [&](auto predicate)
		{
			return std::all_of(std::begin(corners), std::end(corners), predicate);
		};
		return !whollyOutside([](const glm::vec4& point) { return point.x < -point.w; })
			&& !whollyOutside([](const glm::vec4& point) { return point.x > point.w; })
			&& !whollyOutside([](const glm::vec4& point) { return point.y < -point.w; })
			&& !whollyOutside([](const glm::vec4& point) { return point.y > point.w; })
			&& !whollyOutside([](const glm::vec4& point) { return point.z < -point.w; })
			&& !whollyOutside([](const glm::vec4& point) { return point.z > point.w; });
	}

	struct QuadVertex
	{
		glm::vec3 Position;
		glm::vec4 Color;
		glm::vec2 TexCoord;
		float TexIndex;
		float TilingFactor;
        float Lit;
        float NormalIndex;
        glm::vec4 NormalBasis;

		// Editor-only
		int EntityID;

	};

	struct CircleVertex
	{
		glm::vec3 WorldPosition;
		glm::vec3 LocalPosition;
		glm::vec4 Color;
		float Thickness;
		float Fade;

		// Editor-only
		int EntityID;
	};

	struct LineVertex
	{
		glm::vec3 Position;
		glm::vec4 Color;

		// Editor-only
		int EntityID;
	};

	struct Renderer2DData
	{
		static const uint32_t MaxQuads = 20000;
		static const uint32_t MaxVertices = MaxQuads * 4;
		static const uint32_t MaxIndices = MaxQuads * 6;
#ifdef TC_PLATFORM_WEB
		static const uint32_t MaxTextureSlots = 16; // WebGL2 minimum fragment texture units.
#else
		static const uint32_t MaxTextureSlots = 32; // TODO: RenderCaps
#endif

		Ref<VertexArray> QuadVertexArray;
		Ref<VertexBuffer> QuadVertexBuffer;
		Ref<Shader> QuadShader;
		Ref<Texture2D> WhiteTexture;

		uint32_t QuadIndexCount = 0;
		QuadVertex* QuadVertexBufferBase = nullptr;
		QuadVertex* QuadVertexBufferPtr = nullptr;

		std::array<Ref<Texture2D>, MaxTextureSlots> TextureSlots;
		uint32_t TextureSlotIndex = 1; // 0 = white texture

		Ref<VertexArray> CircleVertexArray;
		Ref<VertexBuffer> CircleVertexBuffer;
		Ref<Shader> CircleShader;
		uint32_t CircleIndexCount = 0;
		CircleVertex* CircleVertexBufferBase = nullptr;
		CircleVertex* CircleVertexBufferPtr = nullptr;

		Ref<VertexArray> LineVertexArray;
		Ref<VertexBuffer> LineVertexBuffer;
		Ref<Shader> LineShader;
		uint32_t LineVertexCount = 0;
		LineVertex* LineVertexBufferBase = nullptr;
		LineVertex* LineVertexBufferPtr = nullptr;
		float LineWidth = 2.0f;

		glm::vec4 QuadVertexPositions[4];
		glm::vec3 AmbientLight{ 1.0f };
		std::vector<Renderer2D::PointLightData> PointLights;
        struct LightingData {
            glm::vec4 AmbientCount{1.f, 1.f, 1.f, 0.f};
            glm::vec4 PositionRadius[32]{}, ColorIntensity[32]{}, Falloff[32]{};
            glm::vec4 Edges[64]{};
            glm::ivec4 EdgeOwners[64]{};
            glm::ivec4 EdgeCount{};
        } Lighting;
        Ref<UniformBuffer> LightingBuffer;
        Ref<Texture2D> SpriteNormal;

		Renderer2D::Statistics Stats;

		struct CameraData
		{
			glm::mat4 ViewProjection;
		};
		CameraData CameraBuffer;
		Ref<UniformBuffer> CameraUniformBuffer;
		bool SceneActive = false;
	};

	static Renderer2DData s_Data;

    static float NormalTextureIndex(bool lit) {
        if (!lit || !s_Data.SpriteNormal || !s_Data.SpriteNormal->IsLoaded()) return -1.f;
        for (uint32_t i = 1; i < s_Data.TextureSlotIndex; ++i)
            if (*s_Data.TextureSlots[i] == *s_Data.SpriteNormal) return static_cast<float>(i);
        if (s_Data.TextureSlotIndex >= Renderer2DData::MaxTextureSlots) return -1.f;
        const uint32_t slot = s_Data.TextureSlotIndex++;
        s_Data.TextureSlots[slot] = s_Data.SpriteNormal; return static_cast<float>(slot);
    }
    static glm::vec4 NormalBasis(const glm::mat4& transform) {
        glm::vec2 x(transform[0]), y(transform[1]);
        x = glm::length(x) > 1e-6f ? glm::normalize(x) : glm::vec2(1,0);
        y = glm::length(y) > 1e-6f ? glm::normalize(y) : glm::vec2(0,1);
        return {x.x, x.y, y.x, y.y};
    }

	// Engine infrastructure shaders are code, not project assets. Embedding them
	// keeps the shipping player independent from an uncooked Packages/Shaders
	// tree before (and after) its game package is mounted.
	static constexpr const char* s_QuadVertexShader = R"(
#version 450 core

layout(location = 0) in vec3 a_Position;
layout(location = 1) in vec4 a_Color;
layout(location = 2) in vec2 a_TexCoord;
layout(location = 3) in float a_TexIndex;
layout(location = 4) in float a_TilingFactor;
layout(location = 5) in float a_Lit;
layout(location = 6) in float a_NormalIndex;
layout(location = 7) in vec4 a_NormalBasis;
layout(location = 8) in int a_EntityID;

layout(std140, binding = 0) uniform Camera
{
	mat4 u_ViewProjection;
};

struct VertexOutput
{
	vec4 Color;
	vec2 TexCoord;
	float TilingFactor;
};

layout(location = 0) out VertexOutput Output;
layout(location = 3) out flat float v_TexIndex;
layout(location = 4) out flat int v_EntityID;
layout(location = 5) out vec3 v_WorldPosition;
layout(location = 6) out flat float v_Lit;
layout(location = 7) out flat float v_NormalIndex;
layout(location = 8) out flat vec4 v_NormalBasis;

void main()
{
	Output.Color = a_Color;
	Output.TexCoord = a_TexCoord;
	Output.TilingFactor = a_TilingFactor;
	v_TexIndex = a_TexIndex;
	v_EntityID = a_EntityID;
    v_WorldPosition = a_Position; v_Lit = a_Lit;
    v_NormalIndex = a_NormalIndex; v_NormalBasis = a_NormalBasis;
	gl_Position = u_ViewProjection * vec4(a_Position, 1.0);
}
)";

	static constexpr const char* s_QuadFragmentShader = R"(
#version 450 core

layout(location = 0) out vec4 o_Color;
layout(location = 1) out int o_EntityID;

struct VertexOutput
{
	vec4 Color;
	vec2 TexCoord;
	float TilingFactor;
};

layout(location = 0) in VertexOutput Input;
layout(location = 3) in flat float v_TexIndex;
layout(location = 4) in flat int v_EntityID;
layout(location = 5) in vec3 v_WorldPosition;
layout(location = 6) in flat float v_Lit;
layout(location = 7) in flat float v_NormalIndex;
layout(location = 8) in flat vec4 v_NormalBasis;
layout(std140, binding = 1) uniform Lighting {
    vec4 u_AmbientCount;
    vec4 u_PositionRadius[32]; vec4 u_ColorIntensity[32]; vec4 u_Falloff[32];
    vec4 u_Edges[64]; ivec4 u_EdgeOwners[64]; ivec4 u_EdgeCount;
};
float cross2(vec2 a, vec2 b) { return a.x*b.y-a.y*b.x; }
float visibility(vec2 point, vec2 light) {
    vec2 ray = light-point;
    for (int e=0; e<u_EdgeCount.x; ++e) {
        if (v_EntityID != -1 && u_EdgeOwners[e].x == v_EntityID) continue;
        vec2 a=u_Edges[e].xy, edge=u_Edges[e].zw-a;
        float denominator=cross2(ray,edge);
        if (abs(denominator)<0.000001) continue;
        float t=cross2(a-point,edge)/denominator;
        float u=cross2(a-point,ray)/denominator;
        if (t>0.001 && t<0.999 && u>=0.0 && u<=1.0) return 0.0;
    }
    return 1.0;
}

layout(binding = 0) uniform sampler2D u_Textures[32];

vec3 readNormal(int slot) {
 vec2 uv=Input.TexCoord * Input.TilingFactor;
 vec2 xy=vec2(0);
 switch(slot) {
case 0: xy=texture(u_Textures[0],uv).rg*2.0-1.0; break;
case 1: xy=texture(u_Textures[1],uv).rg*2.0-1.0; break;
case 2: xy=texture(u_Textures[2],uv).rg*2.0-1.0; break;
case 3: xy=texture(u_Textures[3],uv).rg*2.0-1.0; break;
case 4: xy=texture(u_Textures[4],uv).rg*2.0-1.0; break;
case 5: xy=texture(u_Textures[5],uv).rg*2.0-1.0; break;
case 6: xy=texture(u_Textures[6],uv).rg*2.0-1.0; break;
case 7: xy=texture(u_Textures[7],uv).rg*2.0-1.0; break;
case 8: xy=texture(u_Textures[8],uv).rg*2.0-1.0; break;
case 9: xy=texture(u_Textures[9],uv).rg*2.0-1.0; break;
case 10: xy=texture(u_Textures[10],uv).rg*2.0-1.0; break;
case 11: xy=texture(u_Textures[11],uv).rg*2.0-1.0; break;
case 12: xy=texture(u_Textures[12],uv).rg*2.0-1.0; break;
case 13: xy=texture(u_Textures[13],uv).rg*2.0-1.0; break;
case 14: xy=texture(u_Textures[14],uv).rg*2.0-1.0; break;
case 15: xy=texture(u_Textures[15],uv).rg*2.0-1.0; break;
case 16: xy=texture(u_Textures[16],uv).rg*2.0-1.0; break;
case 17: xy=texture(u_Textures[17],uv).rg*2.0-1.0; break;
case 18: xy=texture(u_Textures[18],uv).rg*2.0-1.0; break;
case 19: xy=texture(u_Textures[19],uv).rg*2.0-1.0; break;
case 20: xy=texture(u_Textures[20],uv).rg*2.0-1.0; break;
case 21: xy=texture(u_Textures[21],uv).rg*2.0-1.0; break;
case 22: xy=texture(u_Textures[22],uv).rg*2.0-1.0; break;
case 23: xy=texture(u_Textures[23],uv).rg*2.0-1.0; break;
case 24: xy=texture(u_Textures[24],uv).rg*2.0-1.0; break;
case 25: xy=texture(u_Textures[25],uv).rg*2.0-1.0; break;
case 26: xy=texture(u_Textures[26],uv).rg*2.0-1.0; break;
case 27: xy=texture(u_Textures[27],uv).rg*2.0-1.0; break;
case 28: xy=texture(u_Textures[28],uv).rg*2.0-1.0; break;
case 29: xy=texture(u_Textures[29],uv).rg*2.0-1.0; break;
case 30: xy=texture(u_Textures[30],uv).rg*2.0-1.0; break;
case 31: xy=texture(u_Textures[31],uv).rg*2.0-1.0; break;
default: break;
 } return vec3(xy,sqrt(max(0.0,1.0-dot(xy,xy))));
}

void main()
{
	vec4 texColor = Input.Color;
	switch (int(v_TexIndex))
	{
		case  0: texColor *= texture(u_Textures[ 0], Input.TexCoord * Input.TilingFactor); break;
		case  1: texColor *= texture(u_Textures[ 1], Input.TexCoord * Input.TilingFactor); break;
		case  2: texColor *= texture(u_Textures[ 2], Input.TexCoord * Input.TilingFactor); break;
		case  3: texColor *= texture(u_Textures[ 3], Input.TexCoord * Input.TilingFactor); break;
		case  4: texColor *= texture(u_Textures[ 4], Input.TexCoord * Input.TilingFactor); break;
		case  5: texColor *= texture(u_Textures[ 5], Input.TexCoord * Input.TilingFactor); break;
		case  6: texColor *= texture(u_Textures[ 6], Input.TexCoord * Input.TilingFactor); break;
		case  7: texColor *= texture(u_Textures[ 7], Input.TexCoord * Input.TilingFactor); break;
		case  8: texColor *= texture(u_Textures[ 8], Input.TexCoord * Input.TilingFactor); break;
		case  9: texColor *= texture(u_Textures[ 9], Input.TexCoord * Input.TilingFactor); break;
		case 10: texColor *= texture(u_Textures[10], Input.TexCoord * Input.TilingFactor); break;
		case 11: texColor *= texture(u_Textures[11], Input.TexCoord * Input.TilingFactor); break;
		case 12: texColor *= texture(u_Textures[12], Input.TexCoord * Input.TilingFactor); break;
		case 13: texColor *= texture(u_Textures[13], Input.TexCoord * Input.TilingFactor); break;
		case 14: texColor *= texture(u_Textures[14], Input.TexCoord * Input.TilingFactor); break;
		case 15: texColor *= texture(u_Textures[15], Input.TexCoord * Input.TilingFactor); break;
		case 16: texColor *= texture(u_Textures[16], Input.TexCoord * Input.TilingFactor); break;
		case 17: texColor *= texture(u_Textures[17], Input.TexCoord * Input.TilingFactor); break;
		case 18: texColor *= texture(u_Textures[18], Input.TexCoord * Input.TilingFactor); break;
		case 19: texColor *= texture(u_Textures[19], Input.TexCoord * Input.TilingFactor); break;
		case 20: texColor *= texture(u_Textures[20], Input.TexCoord * Input.TilingFactor); break;
		case 21: texColor *= texture(u_Textures[21], Input.TexCoord * Input.TilingFactor); break;
		case 22: texColor *= texture(u_Textures[22], Input.TexCoord * Input.TilingFactor); break;
		case 23: texColor *= texture(u_Textures[23], Input.TexCoord * Input.TilingFactor); break;
		case 24: texColor *= texture(u_Textures[24], Input.TexCoord * Input.TilingFactor); break;
		case 25: texColor *= texture(u_Textures[25], Input.TexCoord * Input.TilingFactor); break;
		case 26: texColor *= texture(u_Textures[26], Input.TexCoord * Input.TilingFactor); break;
		case 27: texColor *= texture(u_Textures[27], Input.TexCoord * Input.TilingFactor); break;
		case 28: texColor *= texture(u_Textures[28], Input.TexCoord * Input.TilingFactor); break;
		case 29: texColor *= texture(u_Textures[29], Input.TexCoord * Input.TilingFactor); break;
		case 30: texColor *= texture(u_Textures[30], Input.TexCoord * Input.TilingFactor); break;
		case 31: texColor *= texture(u_Textures[31], Input.TexCoord * Input.TilingFactor); break;
	}
	if (texColor.a < 0.1)
		discard;
    if (v_Lit > 0.5) {
        vec3 lighting = max(u_AmbientCount.rgb, vec3(0));
        vec3 normal = readNormal(int(v_NormalIndex));
        normal.xy = mat2(v_NormalBasis.xy, v_NormalBasis.zw) * normal.xy;
        normal = normalize(normal);
        for (int i=0; i<int(u_AmbientCount.w); ++i) {
            vec3 delta=u_PositionRadius[i].xyz-v_WorldPosition;
            float attenuation=pow(max(0.0,1.0-length(delta.xy)/u_PositionRadius[i].w),u_Falloff[i].x);
            float diffuse=v_NormalIndex < 0.0 ? 1.0 : max(0.0,dot(normal,normalize(vec3(delta.xy, max(abs(delta.z), 1.0)))));
            lighting += u_ColorIntensity[i].rgb*u_ColorIntensity[i].w*attenuation*diffuse*visibility(v_WorldPosition.xy,u_PositionRadius[i].xy);
        }
        texColor.rgb *= lighting;
    }

	o_Color = texColor;
	o_EntityID = v_EntityID;
}
)";

	static constexpr const char* s_CircleVertexShader = R"(
#version 450 core

layout(location = 0) in vec3 a_WorldPosition;
layout(location = 1) in vec3 a_LocalPosition;
layout(location = 2) in vec4 a_Color;
layout(location = 3) in float a_Thickness;
layout(location = 4) in float a_Fade;
layout(location = 5) in int a_EntityID;

layout(std140, binding = 0) uniform Camera
{
	mat4 u_ViewProjection;
};

struct VertexOutput
{
	vec3 LocalPosition;
	vec4 Color;
	float Thickness;
	float Fade;
};

layout(location = 0) out VertexOutput Output;
layout(location = 4) out flat int v_EntityID;

void main()
{
	Output.LocalPosition = a_LocalPosition;
	Output.Color = a_Color;
	Output.Thickness = a_Thickness;
	Output.Fade = a_Fade;
	v_EntityID = a_EntityID;
	gl_Position = u_ViewProjection * vec4(a_WorldPosition, 1.0);
}
)";

	static constexpr const char* s_CircleFragmentShader = R"(
#version 450 core

layout(location = 0) out vec4 o_Color;
layout(location = 1) out int o_EntityID;

struct VertexOutput
{
	vec3 LocalPosition;
	vec4 Color;
	float Thickness;
	float Fade;
};

layout(location = 0) in VertexOutput Input;
layout(location = 4) in flat int v_EntityID;

void main()
{
	float distance = 1.0 - length(Input.LocalPosition);
	float circle = smoothstep(0.0, Input.Fade, distance);
	circle *= 1.0 - smoothstep(Input.Thickness, Input.Thickness + Input.Fade, distance);

	if (circle == 0.0)
		discard;

	o_Color = Input.Color;
	o_Color.a *= circle;
	if (o_Color.a < 0.1)
		discard;
	o_EntityID = v_EntityID;
}
)";

	static constexpr const char* s_LineVertexShader = R"(
#version 450 core

layout(location = 0) in vec3 a_Position;
layout(location = 1) in vec4 a_Color;
layout(location = 2) in int a_EntityID;

layout(std140, binding = 0) uniform Camera
{
	mat4 u_ViewProjection;
};

struct VertexOutput
{
	vec4 Color;
};

layout(location = 0) out VertexOutput Output;
layout(location = 1) out flat int v_EntityID;

void main()
{
	Output.Color = a_Color;
	v_EntityID = a_EntityID;
	gl_Position = u_ViewProjection * vec4(a_Position, 1.0);
}
)";

	static constexpr const char* s_LineFragmentShader = R"(
#version 450 core

layout(location = 0) out vec4 o_Color;
layout(location = 1) out int o_EntityID;

struct VertexOutput
{
	vec4 Color;
};

layout(location = 0) in VertexOutput Input;
layout(location = 1) in flat int v_EntityID;

void main()
{
	if (Input.Color.a < 0.1)
		discard;

	o_Color = Input.Color;
	o_EntityID = v_EntityID;
}
)";

	void Renderer2D::Init()
	{
		TC_PROFILE_FUNCTION();

		s_Data.QuadVertexArray = VertexArray::Create();

		s_Data.QuadVertexBuffer = VertexBuffer::Create(s_Data.MaxVertices * sizeof(QuadVertex));
		s_Data.QuadVertexBuffer->SetLayout({
			{ ShaderDataType::Float3, "a_Position"     },
			{ ShaderDataType::Float4, "a_Color"        },
			{ ShaderDataType::Float2, "a_TexCoord"     },
			{ ShaderDataType::Float,  "a_TexIndex"     },
			{ ShaderDataType::Float,  "a_TilingFactor" },
			{ ShaderDataType::Float, "a_Lit" },
            { ShaderDataType::Float, "a_NormalIndex" },
            { ShaderDataType::Float4, "a_NormalBasis" },
            { ShaderDataType::Int, "a_EntityID" }
			});

		s_Data.QuadVertexArray->AddVertexBuffer(s_Data.QuadVertexBuffer);

		s_Data.QuadVertexBufferBase = new QuadVertex[s_Data.MaxVertices];

		std::vector<uint32_t> quadIndices(s_Data.MaxIndices);

		uint32_t offset = 0;
		for (uint32_t i = 0; i < s_Data.MaxIndices; i += 6)
		{
			quadIndices[i + 0] = offset + 0;
			quadIndices[i + 1] = offset + 1;
			quadIndices[i + 2] = offset + 2;

			quadIndices[i + 3] = offset + 2;
			quadIndices[i + 4] = offset + 3;
			quadIndices[i + 5] = offset + 0;

			offset += 4;
		}

		Ref<IndexBuffer> quadIB = IndexBuffer::Create(quadIndices.data(), s_Data.MaxIndices);
		s_Data.QuadVertexArray->SetIndexBuffer(quadIB);

		s_Data.CircleVertexArray = VertexArray::Create();
		s_Data.CircleVertexBuffer = VertexBuffer::Create(s_Data.MaxVertices * sizeof(CircleVertex));
		s_Data.CircleVertexBuffer->SetLayout({
			{ ShaderDataType::Float3, "a_WorldPosition" },
			{ ShaderDataType::Float3, "a_LocalPosition" },
			{ ShaderDataType::Float4, "a_Color"         },
			{ ShaderDataType::Float,  "a_Thickness"     },
			{ ShaderDataType::Float,  "a_Fade"          },
			{ ShaderDataType::Int,    "a_EntityID"      }
			});
		s_Data.CircleVertexArray->AddVertexBuffer(s_Data.CircleVertexBuffer);
		s_Data.CircleVertexArray->SetIndexBuffer(quadIB);
		s_Data.CircleVertexBufferBase = new CircleVertex[s_Data.MaxVertices];

		s_Data.LineVertexArray = VertexArray::Create();
		s_Data.LineVertexBuffer = VertexBuffer::Create(s_Data.MaxVertices * sizeof(LineVertex));
		s_Data.LineVertexBuffer->SetLayout({
			{ ShaderDataType::Float3, "a_Position" },
			{ ShaderDataType::Float4, "a_Color"    },
			{ ShaderDataType::Int,    "a_EntityID" }
			});
		s_Data.LineVertexArray->AddVertexBuffer(s_Data.LineVertexBuffer);
		s_Data.LineVertexBufferBase = new LineVertex[s_Data.MaxVertices];

		s_Data.WhiteTexture = Texture2D::Create(1, 1);
		uint32_t whiteTextureData = 0xffffffff;
		s_Data.WhiteTexture->SetData(&whiteTextureData, sizeof(uint32_t));

		int32_t samplers[s_Data.MaxTextureSlots];
		for (uint32_t i = 0; i < s_Data.MaxTextureSlots; i++)
			samplers[i] = i;

		s_Data.QuadShader = Shader::Create("TomCat.Renderer2D.Quad",
			s_QuadVertexShader, s_QuadFragmentShader);
		s_Data.QuadShader->Bind();
		s_Data.QuadShader->SetIntArray("u_Textures", samplers, s_Data.MaxTextureSlots);

		s_Data.CircleShader = Shader::Create("TomCat.Renderer2D.Circle",
			s_CircleVertexShader, s_CircleFragmentShader);
		s_Data.LineShader = Shader::Create("TomCat.Renderer2D.Line",
			s_LineVertexShader, s_LineFragmentShader);


		// Set first texture slot to 0
		s_Data.TextureSlots[0] = s_Data.WhiteTexture;

		s_Data.QuadVertexPositions[0] = { -0.5f, -0.5f, 0.0f, 1.0f };
		s_Data.QuadVertexPositions[1] = { 0.5f, -0.5f, 0.0f, 1.0f };
		s_Data.QuadVertexPositions[2] = { 0.5f,  0.5f, 0.0f, 1.0f };
		s_Data.QuadVertexPositions[3] = { -0.5f,  0.5f, 0.0f, 1.0f };

		s_Data.LightingBuffer = UniformBuffer::Create(sizeof(Renderer2DData::LightingData), 1);
        s_Data.CameraUniformBuffer = UniformBuffer::Create(sizeof(Renderer2DData::CameraData), 0);
	}

	void Renderer2D::Shutdown()
	{
		TC_PROFILE_FUNCTION();

		delete[] s_Data.QuadVertexBufferBase;
		s_Data.QuadVertexBufferBase = nullptr;
		s_Data.QuadVertexBufferPtr = nullptr;
		s_Data.QuadIndexCount = 0;
		delete[] s_Data.CircleVertexBufferBase;
		s_Data.CircleVertexBufferBase = nullptr;
		s_Data.CircleVertexBufferPtr = nullptr;
		s_Data.CircleIndexCount = 0;
		delete[] s_Data.LineVertexBufferBase;
		s_Data.LineVertexBufferBase = nullptr;
		s_Data.LineVertexBufferPtr = nullptr;
		s_Data.LineVertexCount = 0;
		s_Data.LineWidth = 2.0f;
		memset(&s_Data.Stats, 0, sizeof(Statistics));
		s_Data.SceneActive = false;
		s_Data.TextureSlotIndex = 1;
		s_Data.TextureSlots.fill(nullptr);
		s_Data.AmbientLight = glm::vec3(1.0f);
		s_Data.PointLights.clear();
        s_Data.LightingBuffer.reset(); s_Data.SpriteNormal.reset();
		s_Data.CameraUniformBuffer.reset();
		s_Data.LineShader.reset();
		s_Data.CircleShader.reset();
		s_Data.QuadShader.reset();
		s_Data.WhiteTexture.reset();
		s_Data.LineVertexBuffer.reset();
		s_Data.LineVertexArray.reset();
		s_Data.CircleVertexBuffer.reset();
		s_Data.CircleVertexArray.reset();
		s_Data.QuadVertexBuffer.reset();
		s_Data.QuadVertexArray.reset();
	}

	void Renderer2D::Set2DLighting(const glm::vec3& ambient,
		std::span<const PointLightData> pointLights, std::span<const ShadowEdge> edges)
	{
        if (s_Data.SceneActive) { Flush(); StartBatch(); }
		s_Data.AmbientLight = glm::max(ambient, glm::vec3(0.0f));
		s_Data.Lighting = {};
        auto& data = s_Data.Lighting;
        data.AmbientCount = glm::vec4(s_Data.AmbientLight, 0);
        uint32_t count=0;
        for (const auto& light : pointLights) {
            if (count == 32) break;
            if (!(light.Radius > 0) || !(light.Intensity > 0)) continue;
            data.PositionRadius[count] = glm::vec4(light.Position, light.Radius);
            data.ColorIntensity[count] = glm::vec4(glm::max(light.Color, glm::vec3(0)), light.Intensity);
            data.Falloff[count] = glm::vec4(std::max(light.Falloff, .0001f), 0,0,0); ++count;
        }
        data.AmbientCount.w = static_cast<float>(count);
        data.EdgeCount.x = static_cast<int>(std::min<size_t>(64, edges.size()));
        for (int i=0; i<data.EdgeCount.x; ++i) { data.Edges[i] = glm::vec4(edges[i].A, edges[i].B); data.EdgeOwners[i].x=edges[i].EntityID; }
        if (s_Data.LightingBuffer) s_Data.LightingBuffer->SetData(&data, sizeof(data));
	}

	void Renderer2D::BeginScene(const Camera& camera, const glm::mat4& transform)
	{
		TC_PROFILE_FUNCTION();

		s_Data.CameraBuffer.ViewProjection = camera.GetProjection() * glm::inverse(transform);
		s_Data.CameraUniformBuffer->SetData(&s_Data.CameraBuffer, sizeof(Renderer2DData::CameraData));

		StartBatch();
		s_Data.SceneActive = true;
	}

	void Renderer2D::BeginScene(const EditorCamera& camera)
	{
		TC_PROFILE_FUNCTION();

		s_Data.CameraBuffer.ViewProjection = camera.GetViewProjection();
		s_Data.CameraUniformBuffer->SetData(&s_Data.CameraBuffer, sizeof(Renderer2DData::CameraData));

		StartBatch();
		s_Data.SceneActive = true;
	}

	void Renderer2D::EndScene()
	{
		TC_PROFILE_FUNCTION();

		Flush();
		s_Data.SceneActive = false;
	}

	void Renderer2D::StartBatch()
	{
		s_Data.QuadIndexCount = 0;
		s_Data.QuadVertexBufferPtr = s_Data.QuadVertexBufferBase;
		s_Data.CircleIndexCount = 0;
		s_Data.CircleVertexBufferPtr = s_Data.CircleVertexBufferBase;
		s_Data.LineVertexCount = 0;
		s_Data.LineVertexBufferPtr = s_Data.LineVertexBufferBase;

		for (uint32_t slot = 1; slot < Renderer2DData::MaxTextureSlots; ++slot)
			s_Data.TextureSlots[slot].reset();
		s_Data.TextureSlotIndex = 1;
	}

	void Renderer2D::Flush()
	{
		if (s_Data.QuadIndexCount > 0)
		{
			const uint32_t dataSize = static_cast<uint32_t>(
				reinterpret_cast<uint8_t*>(s_Data.QuadVertexBufferPtr) -
				reinterpret_cast<uint8_t*>(s_Data.QuadVertexBufferBase));
			s_Data.QuadVertexBuffer->SetData(s_Data.QuadVertexBufferBase, dataSize);

			for (uint32_t i = 0; i < s_Data.TextureSlotIndex; i++)
				s_Data.TextureSlots[i]->Bind(i);

			s_Data.QuadShader->Bind();
			RenderCommand::DrawIndexed(s_Data.QuadVertexArray, s_Data.QuadIndexCount);
			s_Data.Stats.DrawCalls++;
		}

		if (s_Data.CircleIndexCount > 0)
		{
			const uint32_t dataSize = static_cast<uint32_t>(
				reinterpret_cast<uint8_t*>(s_Data.CircleVertexBufferPtr) -
				reinterpret_cast<uint8_t*>(s_Data.CircleVertexBufferBase));
			s_Data.CircleVertexBuffer->SetData(s_Data.CircleVertexBufferBase, dataSize);

			s_Data.CircleShader->Bind();
			RenderCommand::DrawIndexed(s_Data.CircleVertexArray, s_Data.CircleIndexCount);
			s_Data.Stats.DrawCalls++;
		}

		if (s_Data.LineVertexCount > 0)
		{
			const uint32_t dataSize = static_cast<uint32_t>(
				reinterpret_cast<uint8_t*>(s_Data.LineVertexBufferPtr) -
				reinterpret_cast<uint8_t*>(s_Data.LineVertexBufferBase));
			s_Data.LineVertexBuffer->SetData(s_Data.LineVertexBufferBase, dataSize);

			s_Data.LineShader->Bind();
			RenderCommand::SetLineWidth(s_Data.LineWidth);
			RenderCommand::DrawLines(s_Data.LineVertexArray, s_Data.LineVertexCount);
			s_Data.Stats.DrawCalls++;
		}

		// Flush is public, so leave all CPU staging buffers ready for additional
		// submissions instead of allowing EndScene to draw the same data again.
		s_Data.QuadIndexCount = 0;
		s_Data.QuadVertexBufferPtr = s_Data.QuadVertexBufferBase;
		s_Data.CircleIndexCount = 0;
		s_Data.CircleVertexBufferPtr = s_Data.CircleVertexBufferBase;
		s_Data.LineVertexCount = 0;
		s_Data.LineVertexBufferPtr = s_Data.LineVertexBufferBase;
	}

	void Renderer2D::NextBatch()
	{
		Flush();
		StartBatch();
	}

	void Renderer2D::DrawQuad(const glm::vec2& position, const glm::vec2& size, const glm::vec4& color)
	{
		DrawQuad({ position.x, position.y, 0.0f }, size, color);
	}

	void Renderer2D::DrawQuad(const glm::vec3& position, const glm::vec2& size, const glm::vec4& color)
	{
		TC_PROFILE_FUNCTION();

		glm::mat4 transform = glm::translate(glm::mat4(1.0f), position)
			* glm::scale(glm::mat4(1.0f), { size.x, size.y, 1.0f });

		DrawQuad(transform, color);
	}

	void Renderer2D::DrawQuad(const glm::vec2& position, const glm::vec2& size, const Ref<Texture2D>& texture, float tilingFactor, const glm::vec4& tintColor)
	{
		DrawQuad({ position.x, position.y, 0.0f }, size, texture, tilingFactor, tintColor);
	}

	void Renderer2D::DrawQuad(const glm::vec3& position, const glm::vec2& size, const Ref<Texture2D>& texture, float tilingFactor, const glm::vec4& tintColor)
	{
		TC_PROFILE_FUNCTION();

		glm::mat4 transform = glm::translate(glm::mat4(1.0f), position)
			* glm::scale(glm::mat4(1.0f), { size.x, size.y, 1.0f });

		DrawQuad(transform, texture, tilingFactor, tintColor);
	}

	void Renderer2D::DrawQuad(const glm::mat4& transform, const glm::vec4& color,
		int entityID, bool lit)
	{
		TC_PROFILE_FUNCTION();

		constexpr size_t quadVertexCount = 4;
		const float textureIndex = 0.0f; // White Texture
		constexpr glm::vec2 textureCoords[] = { { 0.0f, 0.0f }, { 1.0f, 0.0f }, { 1.0f, 1.0f }, { 0.0f, 1.0f } };
		const float tilingFactor = 1.0f;

		if (s_Data.QuadIndexCount >= Renderer2DData::MaxIndices || (s_Data.SpriteNormal && s_Data.TextureSlotIndex + 2 >= Renderer2DData::MaxTextureSlots))
			NextBatch();

		for (size_t i = 0; i < quadVertexCount; i++)
		{
			const glm::vec3 worldPosition = glm::vec3(
				transform * s_Data.QuadVertexPositions[i]);
			s_Data.QuadVertexBufferPtr->Position = worldPosition;
			s_Data.QuadVertexBufferPtr->Color = color;
            s_Data.QuadVertexBufferPtr->Lit = lit ? 1.f : 0.f;
            s_Data.QuadVertexBufferPtr->NormalIndex = NormalTextureIndex(lit);
            s_Data.QuadVertexBufferPtr->NormalBasis = NormalBasis(transform);
			s_Data.QuadVertexBufferPtr->TexCoord = textureCoords[i];
			s_Data.QuadVertexBufferPtr->TexIndex = textureIndex;
			s_Data.QuadVertexBufferPtr->TilingFactor = tilingFactor;
			s_Data.QuadVertexBufferPtr->EntityID = entityID;
			s_Data.QuadVertexBufferPtr++;
		}

		s_Data.QuadIndexCount += 6;

		s_Data.Stats.QuadCount++;
	}

	void Renderer2D::DrawLitQuad(const glm::mat4& transform,
		const glm::vec4& color, int entityID)
	{
		DrawQuad(transform, color, entityID, true);
	}

	void Renderer2D::DrawQuad(const glm::mat4& transform,
		const Ref<Texture2D>& texture, float tilingFactor,
		const glm::vec4& tintColor, int entityID, bool lit)
	{
		if (tilingFactor == 1.0f)
		{
			DrawTexturedQuadRegion(transform, texture, { 0.0f, 0.0f },
				{ 1.0f, 1.0f }, tintColor, entityID, lit);
			return;
		}
		TC_PROFILE_FUNCTION();

		constexpr size_t quadVertexCount = 4;
		constexpr glm::vec2 textureCoords[] = { { 0.0f, 0.0f }, { 1.0f, 0.0f }, { 1.0f, 1.0f }, { 0.0f, 1.0f } };
		const Ref<Texture2D>& resolvedTexture = texture && texture->IsLoaded() ? texture : s_Data.WhiteTexture;

		if (s_Data.QuadIndexCount >= Renderer2DData::MaxIndices || (s_Data.SpriteNormal && s_Data.TextureSlotIndex + 2 >= Renderer2DData::MaxTextureSlots))
			NextBatch();

		float textureIndex = 0.0f;
		const bool usesWhiteTexture = *resolvedTexture == *s_Data.WhiteTexture;
		for (uint32_t i = 1; !usesWhiteTexture && i < s_Data.TextureSlotIndex; i++)
		{
			if (*s_Data.TextureSlots[i] == *resolvedTexture)
			{
				textureIndex = (float)i;
				break;
			}
		}

		if (!usesWhiteTexture && textureIndex == 0.0f)
		{
			if (s_Data.TextureSlotIndex >= Renderer2DData::MaxTextureSlots)
				NextBatch();

			textureIndex = (float)s_Data.TextureSlotIndex;
			s_Data.TextureSlots[s_Data.TextureSlotIndex] = resolvedTexture;
			s_Data.TextureSlotIndex++;
		}

		for (size_t i = 0; i < quadVertexCount; i++)
		{
			const glm::vec3 worldPosition = glm::vec3(
				transform * s_Data.QuadVertexPositions[i]);
			s_Data.QuadVertexBufferPtr->Position = worldPosition;
			s_Data.QuadVertexBufferPtr->Color = tintColor;
            s_Data.QuadVertexBufferPtr->Lit = lit ? 1.f : 0.f;
            s_Data.QuadVertexBufferPtr->NormalIndex = NormalTextureIndex(lit);
            s_Data.QuadVertexBufferPtr->NormalBasis = NormalBasis(transform);
			s_Data.QuadVertexBufferPtr->TexCoord = textureCoords[i];
			s_Data.QuadVertexBufferPtr->TexIndex = textureIndex;
			s_Data.QuadVertexBufferPtr->TilingFactor = tilingFactor;
			s_Data.QuadVertexBufferPtr->EntityID = entityID;
			s_Data.QuadVertexBufferPtr++;
		}

		s_Data.QuadIndexCount += 6;

		s_Data.Stats.QuadCount++;
	}

	void Renderer2D::DrawTexturedQuadRegion(const glm::mat4& transform,
		const Ref<Texture2D>& texture, const glm::vec2& uvMin,
		const glm::vec2& uvMax, const glm::vec4& tintColor, int entityID,
		bool lit)
	{
		TC_PROFILE_FUNCTION();
		std::array<glm::vec3, 4> positions{};
		for (size_t index = 0; index < positions.size(); ++index)
			positions[index] = glm::vec3(
				transform * s_Data.QuadVertexPositions[index]);
		const std::array<glm::vec2, 4> textureCoordinates = {{
			{ uvMin.x, uvMin.y }, { uvMax.x, uvMin.y },
			{ uvMax.x, uvMax.y }, { uvMin.x, uvMax.y }
		}};
		DrawTexturedQuadVertices(positions, texture, textureCoordinates,
			tintColor, entityID, lit);
	}

	void Renderer2D::DrawTexturedQuadVertices(
		const std::array<glm::vec3, 4>& positions,
		const Ref<Texture2D>& texture,
		const std::array<glm::vec2, 4>& textureCoordinates,
		const glm::vec4& tintColor, int entityID, bool lit)
	{
		TC_PROFILE_FUNCTION();
		const Ref<Texture2D>& resolvedTexture = texture && texture->IsLoaded()
			? texture : s_Data.WhiteTexture;
		if (s_Data.QuadIndexCount >= Renderer2DData::MaxIndices || (s_Data.SpriteNormal && s_Data.TextureSlotIndex + 2 >= Renderer2DData::MaxTextureSlots))
			NextBatch();

		float textureIndex = 0.0f;
		const bool usesWhiteTexture = *resolvedTexture == *s_Data.WhiteTexture;
		for (uint32_t index = 1; !usesWhiteTexture
			&& index < s_Data.TextureSlotIndex; ++index)
		{
			if (*s_Data.TextureSlots[index] == *resolvedTexture)
			{
				textureIndex = static_cast<float>(index);
				break;
			}
		}
		if (!usesWhiteTexture && textureIndex == 0.0f)
		{
			if (s_Data.TextureSlotIndex >= Renderer2DData::MaxTextureSlots)
				NextBatch();
			textureIndex = static_cast<float>(s_Data.TextureSlotIndex);
			s_Data.TextureSlots[s_Data.TextureSlotIndex++] = resolvedTexture;
		}

		for (size_t index = 0; index < 4; ++index)
		{
			const glm::vec3 worldPosition = positions[index];
			s_Data.QuadVertexBufferPtr->Position = worldPosition;
			s_Data.QuadVertexBufferPtr->Color = tintColor;
            s_Data.QuadVertexBufferPtr->Lit = lit ? 1.f : 0.f;
            s_Data.QuadVertexBufferPtr->NormalIndex = NormalTextureIndex(lit);
            s_Data.QuadVertexBufferPtr->NormalBasis = NormalBasis(glm::mat4(glm::vec4(positions[1] - positions[0], 0), glm::vec4(positions[3] - positions[0], 0), glm::vec4(0,0,1,0), glm::vec4(0,0,0,1)));
			s_Data.QuadVertexBufferPtr->TexCoord = textureCoordinates[index];
			s_Data.QuadVertexBufferPtr->TexIndex = textureIndex;
			s_Data.QuadVertexBufferPtr->TilingFactor = 1.0f;
			s_Data.QuadVertexBufferPtr->EntityID = entityID;
			++s_Data.QuadVertexBufferPtr;
		}
		s_Data.QuadIndexCount += 6;
		++s_Data.Stats.QuadCount;
	}

	void Renderer2D::DrawCircle(const glm::mat4& transform, const glm::vec4& color,
		float thickness, float fade, int entityID)
	{
		TC_PROFILE_FUNCTION();

		if (!std::isfinite(thickness) || thickness < 0.0f || thickness > 1.0f ||
			!std::isfinite(fade) || fade <= 0.0f)
		{
			TC_Core_Warn("DrawCircle requires thickness in [0, 1] and a finite fade greater than zero");
			return;
		}

		if (s_Data.CircleIndexCount + 6 > Renderer2DData::MaxIndices)
			NextBatch();

		for (size_t i = 0; i < 4; i++)
		{
			const glm::vec4 worldPosition = transform * s_Data.QuadVertexPositions[i];
			s_Data.CircleVertexBufferPtr->WorldPosition = glm::vec3(worldPosition);
			s_Data.CircleVertexBufferPtr->LocalPosition =
				glm::vec3(s_Data.QuadVertexPositions[i]) * 2.0f;
			s_Data.CircleVertexBufferPtr->Color = color;
			s_Data.CircleVertexBufferPtr->Thickness = thickness;
			s_Data.CircleVertexBufferPtr->Fade = fade;
			s_Data.CircleVertexBufferPtr->EntityID = entityID;
			s_Data.CircleVertexBufferPtr++;
		}

		s_Data.CircleIndexCount += 6;
		s_Data.Stats.CircleCount++;
	}

	void Renderer2D::DrawLine(const glm::vec3& start, const glm::vec3& end,
		const glm::vec4& color, int entityID)
	{
		TC_PROFILE_FUNCTION();

		if (s_Data.LineVertexCount + 2 > Renderer2DData::MaxVertices)
			NextBatch();

		s_Data.LineVertexBufferPtr->Position = start;
		s_Data.LineVertexBufferPtr->Color = color;
		s_Data.LineVertexBufferPtr->EntityID = entityID;
		s_Data.LineVertexBufferPtr++;

		s_Data.LineVertexBufferPtr->Position = end;
		s_Data.LineVertexBufferPtr->Color = color;
		s_Data.LineVertexBufferPtr->EntityID = entityID;
		s_Data.LineVertexBufferPtr++;

		s_Data.LineVertexCount += 2;
		s_Data.Stats.LineCount++;
	}

	void Renderer2D::DrawRect(const glm::vec2& position, const glm::vec2& size,
		const glm::vec4& color, int entityID)
	{
		DrawRect(glm::vec3(position, 0.0f), size, color, entityID);
	}

	void Renderer2D::DrawRect(const glm::vec3& position, const glm::vec2& size,
		const glm::vec4& color, int entityID)
	{
		const glm::mat4 transform = glm::translate(glm::mat4(1.0f), position) *
			glm::scale(glm::mat4(1.0f), glm::vec3(size, 1.0f));
		DrawRect(transform, color, entityID);
	}

	void Renderer2D::DrawRect(const glm::mat4& transform, const glm::vec4& color,
		int entityID)
	{
		glm::vec3 lineVertices[4];
		for (size_t i = 0; i < 4; i++)
			lineVertices[i] = glm::vec3(transform * s_Data.QuadVertexPositions[i]);

		DrawLine(lineVertices[0], lineVertices[1], color, entityID);
		DrawLine(lineVertices[1], lineVertices[2], color, entityID);
		DrawLine(lineVertices[2], lineVertices[3], color, entityID);
		DrawLine(lineVertices[3], lineVertices[0], color, entityID);
	}

	float Renderer2D::GetLineWidth()
	{
		return s_Data.LineWidth;
	}

	void Renderer2D::SetLineWidth(float width)
	{
		if (!std::isfinite(width) || width <= 0.0f)
		{
			TC_Core_Warn("Renderer2D line width must be finite and greater than zero");
			return;
		}

		if (width == s_Data.LineWidth)
			return;

		// glLineWidth is global draw state. Preserve the width of lines already
		// queued in this batch before switching to the new value.
		if (s_Data.SceneActive && s_Data.LineVertexCount > 0)
			NextBatch();

		s_Data.LineWidth = width;
	}

	void Renderer2D::DrawRotatedQuad(const glm::vec2& position, const glm::vec2& size, float rotation, const glm::vec4& color)
	{
		DrawRotatedQuad({ position.x, position.y, 0.0f }, size, rotation, color);
	}

	void Renderer2D::DrawRotatedQuad(const glm::vec3& position, const glm::vec2& size, float rotation, const glm::vec4& color)
	{
		TC_PROFILE_FUNCTION();

		glm::mat4 transform = glm::translate(glm::mat4(1.0f), position)
			* glm::rotate(glm::mat4(1.0f), glm::radians(rotation), { 0.0f, 0.0f, 1.0f })
			* glm::scale(glm::mat4(1.0f), { size.x, size.y, 1.0f });

		DrawQuad(transform, color);
	}

	void Renderer2D::DrawRotatedQuad(const glm::vec2& position, const glm::vec2& size, float rotation, const Ref<Texture2D>& texture, float tilingFactor, const glm::vec4& tintColor)
	{
		DrawRotatedQuad({ position.x, position.y, 0.0f }, size, rotation, texture, tilingFactor, tintColor);
	}

	void Renderer2D::DrawRotatedQuad(const glm::vec3& position, const glm::vec2& size, float rotation, const Ref<Texture2D>& texture, float tilingFactor, const glm::vec4& tintColor)
	{
		TC_PROFILE_FUNCTION();

		glm::mat4 transform = glm::translate(glm::mat4(1.0f), position)
			* glm::rotate(glm::mat4(1.0f), glm::radians(rotation), { 0.0f, 0.0f, 1.0f })
			* glm::scale(glm::mat4(1.0f), { size.x, size.y, 1.0f });

		DrawQuad(transform, texture, tilingFactor, tintColor);
	}

	void Renderer2D::DrawSprite(const glm::mat4& transform, SpriteRenderer& src, int entityID)
    {
        struct ResetNormal { ~ResetNormal() { s_Data.SpriteNormal.reset(); } } reset;
        AssetHandle normal = src.NormalMap;
        glm::vec4 tint = src._Color;
        bool lit = true;
        if (const auto material=AssetManager::Get().GetRuntimeMaterial(src.MaterialHandle)) {
            if (static_cast<uint64_t>(material->Shader)==BuiltinSprite2DShader) {
                for (const auto& binding : material->Textures) if (binding.Name=="NormalMap" && static_cast<uint64_t>(normal)==0) normal=binding.Texture;
                for (const auto& parameter : material->Parameters) {
                    if (parameter.Name=="Tint" && parameter.Type==MaterialParameterType::Float4) tint*=glm::vec4(parameter.AsFloat(0),parameter.AsFloat(1),parameter.AsFloat(2),parameter.AsFloat(3));
                    if (parameter.Name=="Lit" && parameter.Type==MaterialParameterType::Bool) lit=parameter.AsBool();
                }
            }
        }
        s_Data.SpriteNormal = static_cast<uint64_t>(normal) ? AssetManager::Get().LoadTexture(normal) : nullptr;
		const AssetHandle spriteHandle = src.RuntimeSpriteOverrideActive
			? src.RuntimeSpriteOverrideHandle : src.SpriteHandle;
		if (static_cast<uint64_t>(spriteHandle) == 0)
		{
			src.Sprite.reset();
			return;
		}

		AssetManager& assets = AssetManager::Get();
		src.Sprite = assets.LoadTexture(spriteHandle);
		if (!src.Sprite)
			return;
		ResolvedSpriteAsset resolved;
		if (!assets.ResolveSpriteAsset(spriteHandle, resolved)
			|| !resolved.IsSubAsset)
		{
			DrawQuad(transform, src.Sprite, src.TilingFactor, tint, entityID,
				lit);
			return;
		}
		SpriteRenderGeometry geometry;
		if (!BuildSpriteRenderGeometry(resolved.Data, src.Sprite->GetWidth(),
			src.Sprite->GetHeight(), geometry))
			return;
		const glm::mat4 spriteTransform = transform
			* glm::translate(glm::mat4(1.0f),
				{ geometry.OffsetX, geometry.OffsetY, 0.0f })
			* glm::scale(glm::mat4(1.0f),
				{ geometry.Width, geometry.Height, 1.0f });
		DrawTexturedQuadRegion(spriteTransform, src.Sprite,
			{ geometry.UMin, geometry.VMin }, { geometry.UMax, geometry.VMax },
			tint, entityID, lit);
	}

	void Renderer2D::ResetStats()
	{
		memset(&s_Data.Stats, 0, sizeof(Statistics));
	}

	Renderer2D::Statistics Renderer2D::GetStats()
	{
		return s_Data.Stats;
	}

}
