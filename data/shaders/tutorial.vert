#version 450

layout(binding = 0) uniform View
{
	mat4 mat_pv;
	mat4 mat_v;
} view;

layout(push_constant) uniform PushConsts
{
	mat4 mat_m;
} pushConsts;

layout(location = 0) in vec2 inPosition;
layout(location = 1) in vec3 inColour;

layout (location = 0) out vec3 fragColour;

void main()
{
	gl_Position = view.mat_pv * pushConsts.mat_m * vec4(inPosition, 0.0, 1.0);
	fragColour = inColour;
	// fragColour = vec3(1.0);
}