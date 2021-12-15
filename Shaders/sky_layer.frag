#version 450
#extension GL_ARB_separate_shader_objects : enable
#extension GL_ARB_shading_language_420pack : enable

layout(push_constant) uniform PushConsts {
	mat4 mvp;
	vec3 fog_color;
	float fog_density;
	vec3 viewOrigin;
	float cl_time;
	float sky_alpha;
} push_constants;

layout(set = 0, binding = 0) uniform sampler2D solid_tex;
layout(set = 1, binding = 0) uniform sampler2D alpha_tex;

layout (location = 0) in vec4 in_texcoord;
layout (location = 0) out vec4 out_frag_color;

void main() 
{
	vec3 skycoord = normalize (in_texcoord.xyz) * 2.953125f;

	vec4 solid_layer = texture (solid_tex, skycoord.xy + fract (push_constants.cl_time * 0.0625f));
	vec4 alpha_layer = texture (alpha_tex, skycoord.xy + fract (push_constants.cl_time * 0.125f));

	out_frag_color = vec4((solid_layer.rgb * (1.0f - alpha_layer.a) + alpha_layer.rgb * alpha_layer.a), push_constants.sky_alpha);

	if (push_constants.fog_density > 0.0f)
		out_frag_color.rgb = (out_frag_color.rgb * (1.0f - push_constants.fog_density)) + (push_constants.fog_color * push_constants.fog_density);
}


