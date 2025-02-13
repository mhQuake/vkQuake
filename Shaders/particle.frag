#version 450
#extension GL_ARB_separate_shader_objects : enable
#extension GL_ARB_shading_language_420pack : enable

layout(push_constant) uniform PushConsts {
	mat4 mvp;
	vec3 fog_color;
	float fog_density;
} push_constants;

layout (location = 0) in vec4 in_texcoord;
layout (location = 1) in vec4 in_color;
layout (location = 2) in float in_fog_frag_coord;

layout (location = 0) out vec4 out_frag_color;

void main() 
{
	// procedurally generate the particle dot for good speed and per-pixel accuracy at any scale
	float alpha = 1.5 * (1.0 - dot (in_texcoord, in_texcoord));

	out_frag_color.rgb = in_color.rgb;
	out_frag_color.a = in_color.a * alpha;

	float fog = exp(-push_constants.fog_density * push_constants.fog_density * in_fog_frag_coord * in_fog_frag_coord);
	fog = clamp(fog, 0.0, 1.0);
	out_frag_color.rgb = mix(push_constants.fog_color, out_frag_color.rgb, fog);
}
