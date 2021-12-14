#version 450
#extension GL_ARB_separate_shader_objects : enable
#extension GL_ARB_shading_language_420pack : enable

layout(push_constant) uniform PushConsts {
	mat4 mvp;
	vec3 fog_color;
	float fog_density;

	// because push constants need to be 4-float aligned we'll split off each component of r_origin as the 4th float then recompose it in the shader
	vec3 vpn;
	float r_origin_x;
	vec3 vright;
	float r_origin_y;
	vec3 vup;
	float r_origin_z;
} push_constants;

layout (location = 0) in vec3 in_position;
layout (location = 1) in vec4 in_color;

layout (location = 0) out vec4 out_texcoord;
layout (location = 1) out vec4 out_color;
layout (location = 2) out float out_fog_frag_coord;

out gl_PerVertex {
	vec4 gl_Position;
};

void main() 
{
	// recompose the corner and origin vectors
	vec2 corners[4] = {vec2 (-1.0, -1.0), vec2 (-1.0, 1.0), vec2 (1.0, -1.0), vec2 (1.0, 1.0)};
	vec2 corner = corners[gl_VertexIndex % 4];
	vec3 r_origin = vec3 (push_constants.r_origin_x, push_constants.r_origin_y, push_constants.r_origin_z);

	// hack a scale up to keep particles from disapearing (0.004f changed to 0.002f which looks better with this code; 0.666f approximates the original particle size)
	float scale = (1.0 + dot (in_position - r_origin, push_constants.vpn) * 0.002) * 0.666;

	gl_Position = push_constants.mvp * vec4 (in_position + (push_constants.vright * corner.x + push_constants.vup * corner.y) * scale, 1.0f);
	out_texcoord = vec4 (corner, 0.0f, 0.0f);
	out_color = in_color;
	out_fog_frag_coord = gl_Position.w;
}
