#version 450
#extension GL_ARB_separate_shader_objects : enable

layout(binding = 0) uniform values { mat4 mvp; } ubo;
layout(location = 0) in vec2 vpos;
layout(location = 1) in vec3 vcol;
layout(location = 0) out vec3 fcol;

void main()
{
    gl_Position = ubo.mvp * vec4(vpos, 0.0, 1.0);
    fcol = vcol;
}
