#version 450
#extension GL_ARB_separate_shader_objects : enable

layout(location = 0) out vec4 frag;
layout(location = 0) in vec3 fcol;

void main()
{
    frag = vec4(fcol, 1.0);
}
