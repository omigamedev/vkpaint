#version 450
#extension GL_ARB_separate_shader_objects : enable

#ifdef MULTISAMPLE
layout(binding = 1) uniform sampler2DMS tex_bg;
#else
layout(binding = 1) uniform sampler2D   tex_bg;
#endif

layout(binding = 2) uniform sampler2D tex_brush;
layout(binding = 3) uniform frag_values { vec3 col; float pressure; } frag_ubo;

layout(location = 1) in vec2 ftex;

layout(location = 0) out vec4 frag;

layout(constant_id = 0) const int SAMPLES = 8;

#ifdef MULTISAMPLE
// Manual resolve for MSAA samples 
vec4 resolve(sampler2DMS tex, ivec2 uv)
{
    vec4 result = vec4(0.0);
    for (int i = 0; i < SAMPLES; i++)
    {
        vec4 val = texelFetch(tex, uv, i); 
        result += val;
    }    
    // Average resolved samples
    return result / float(SAMPLES);
}
#endif

void main()
{
#ifdef MULTISAMPLE
    ivec2 uvs_pix = ivec2(gl_FragCoord.st);
    vec3 bg = resolve(tex_bg, uvs_pix).rgb;
#else
    vec2 uvs_pix = gl_FragCoord.st / vec2(textureSize(tex_bg, 0));
    vec3 bg = texture(tex_bg, uvs_pix).rgb;
#endif
    float brush_value = 1.0 - texture(tex_brush, ftex).r;
    vec3 rgb = mix(bg, frag_ubo.col, frag_ubo.pressure * brush_value);
    frag = vec4(rgb, 1.0);
}
