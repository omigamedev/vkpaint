#version 450
#extension GL_ARB_separate_shader_objects : enable

#ifdef MULTISAMPLE
layout(binding = 1) uniform sampler2DMS tex;
#else
layout(binding = 1) uniform sampler2D tex;
#endif
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
    frag = resolve(tex, ivec2(ftex * textureSize(tex)));
#else
    frag = texture(tex, ftex);
#endif
}
