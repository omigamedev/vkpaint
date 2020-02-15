#version 450
#extension GL_ARB_separate_shader_objects : enable

layout(binding = 0) uniform values { mat4 mvp; } ubo;

layout(location = 0) in vec3 vpos;
layout(location = 1) in vec3 vcol;
layout(location = 2) in vec2 vtex;

layout(location = 0) out vec3 fcol;
layout(location = 1) out vec2 ftex;

void main()
{
    gl_Position = ubo.mvp * vec4(vpos, 1.0);
    fcol = vcol;
    ftex = vtex;
}
