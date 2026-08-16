// 3D Lit Shader (Blinn-Phong, directional light)

#type vertex
#version 450 core

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
	vec4 u_Params; // x = useTexture, y = ambientStrength, z = shininess, w = entityID
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
	v_EntityID = int(u_Params.w);

	gl_Position = u_ViewProjection * worldPos;
}

#type fragment
#version 450 core

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
}
