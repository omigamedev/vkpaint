#version 450
#extension GL_ARB_separate_shader_objects : enable

layout(binding = 1) uniform sampler2D tex_bg;
layout(binding = 2) uniform sampler2D tex_brush;
layout(binding = 3) uniform frag_values { vec3 col; } frag_ubo;

layout(location = 1) in vec2 ftex;

layout(location = 0) out vec4 frag;

void main()
{
    vec2 uvs_pix = gl_FragCoord.st / vec2(textureSize(tex_bg, 0));
    float brush_value = 1.0 - texture(tex_brush, ftex).r;
    vec3 bg = texture(tex_bg, uvs_pix).rgb;
    vec3 rgb = mix(bg, frag_ubo.col, 0.0001 * brush_value);
    frag = vec4(rgb, 1.0);
}
