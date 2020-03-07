glslc -O -o .\shader.frag.spv .\shader.frag
glslc -DMULTISAMPLE -O -o .\shader.frag.ms.spv .\shader.frag
glslc -O -o .\shader.vert.spv .\shader.vert

glslc -O -o .\shader-fill.frag.spv .\shader-fill.frag
glslc -DMULTISAMPLE -O -o .\shader-fill.frag.ms.spv .\shader-fill.frag
glslc -O -o .\shader-fill.vert.spv .\shader-fill.vert
