#version 450
#extension GL_ARB_separate_shader_objects : enable

// winding   -->
// (-1, 1) B -- C (1, 1)
//         |  / |
// (-1,-1) A -- D (1,-1)
//          <--
const vec2 vert_pos[6] = {
    // triangle ABC
    vec2(-1.0,  1.0),
    vec2(-1.0, -1.0),
    vec2( 1.0, -1.0),
    // triangle ACD
    vec2(-1.0,  1.0),
    vec2( 1.0, -1.0),
    vec2( 1.0,  1.0),
};
const vec2 vert_uvs[6] = {
    // triangle ABC
    vec2(0.0, 0.0),
    vec2(0.0, 1.0),
    vec2(1.0, 1.0),
    // triangle ACD
    vec2(0.0, 0.0),
    vec2(1.0, 1.0),
    vec2(1.0, 0.0),
};

layout(binding = 0) uniform values { mat4 mvp; } ubo;
layout(location = 1) out vec2 ftex;

void main()
{
    gl_Position = ubo.mvp * vec4(vert_pos[gl_VertexIndex], 0.0, 1.0);
    ftex = vert_uvs[gl_VertexIndex];
}
