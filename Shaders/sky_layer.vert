#version 450
#extension GL_ARB_separate_shader_objects : enable
#extension GL_ARB_shading_language_420pack : enable

layout(push_constant) uniform PushConsts {
	mat4 mvp;
	vec3 fog_color;
	float fog_density;
	vec3 vieworigin;
	float cl_time;
	float sky_alpha;
} push_constants;

layout (location = 0) in vec3 in_position;
layout (location = 0) out vec4 out_texcoord;

out gl_PerVertex {
	vec4 gl_Position;
};

void main() 
{
	// orient the sky
	mat4 skyMatrix = mat4 (1.0, 0.0, 0.0, 0.0, 0.0, 1.0, 0.0, 0.0, 0.0, 0.0, 3.0, 0.0, -push_constants.vieworigin.x, -push_constants.vieworigin.y, -push_constants.vieworigin.z * 3.0, 1.0);

	gl_Position = push_constants.mvp * vec4 (in_position, 1.0f);
	out_texcoord = vec4 (in_position, 1.0f) * skyMatrix;
}
