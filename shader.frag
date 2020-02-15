#version 450
#extension GL_ARB_separate_shader_objects : enable

layout(binding = 1) uniform sampler2D tex;
layout(binding = 2) uniform frag_values { vec3 col; } frag_ubo;

layout(location = 0) in vec3 fcol;
layout(location = 1) in vec2 ftex;

layout(location = 0) out vec4 frag;

void main()
{
    frag = vec4(frag_ubo.col, 1.0 - texture(tex, ftex).r);
}
