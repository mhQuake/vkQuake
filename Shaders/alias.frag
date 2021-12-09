#version 450
#extension GL_ARB_separate_shader_objects : enable
#extension GL_ARB_shading_language_420pack : enable
#extension GL_GOOGLE_include_directive : enable

layout(push_constant) uniform PushConsts {
	mat4 mvp;
	vec3 fog_color;
	float fog_density;
} push_constants;

layout(set = 0, binding = 0) uniform sampler2D diffuse_tex;
layout(set = 1, binding = 0) uniform sampler2D fullbright_tex;

layout (set = 2, binding = 0) uniform UBO
{
	mat4 model_matrix;
	vec3 shade_vector;
	float blend_factor;
	vec3 light_color;
	float entalpha;
	uint flags;
} ubo;

layout (location = 0) in vec2 in_texcoord;
layout (location = 1) in vec4 in_color;
layout (location = 2) in float in_fog_frag_coord;

layout (location = 0) out vec4 out_frag_color;

vec3 rgb2hsv (vec3 c)
{
	vec4 K = vec4 (0.0, -1.0 / 3.0, 2.0 / 3.0, -1.0);
	vec4 p = mix (vec4 (c.bg, K.wz), vec4 (c.gb, K.xy), step (c.b, c.g));
	vec4 q = mix (vec4 (p.xyw, c.r), vec4 (c.r, p.yzx), step (p.x, c.r));

	float d = q.x - min (q.w, q.y);
	float e = 1.0e-10;
	return vec3 (abs (q.z + (q.w - q.y) / (6.0 * d + e)), d / (q.x + e), q.x);
}

vec3 hsv2rgb (vec3 c)
{
	vec4 K = vec4 (1.0, 2.0 / 3.0, 1.0 / 3.0, 3.0);
	vec3 p = abs (fract (c.xxx + K.xyz) * 6.0 - K.www);
	return c.z * mix (K.xxx, clamp (p - K.xxx, 0.0, 1.0), c.y);
}

vec3 Desaturate (vec3 RGB, float lightsaturation)
{
	return hsv2rgb (rgb2hsv (RGB) * vec3 (1.0, lightsaturation, 1.0));
}

void main()
{
	vec4 result = texture(diffuse_tex, in_texcoord.xy);
	result.rgb *= Desaturate (in_color.rgb, 0.666) * 2.0;

	if ((ubo.flags & 0x1) != 0)
		result.rgb = max (result.rgb, texture(fullbright_tex, in_texcoord.xy).rgb);

	result.a = ubo.entalpha;

	float fog = exp(-push_constants.fog_density * push_constants.fog_density * in_fog_frag_coord * in_fog_frag_coord);
	fog = clamp(fog, 0.0, 1.0);
	result.rgb = mix(push_constants.fog_color, result.rgb, fog);

	out_frag_color = result;
}
