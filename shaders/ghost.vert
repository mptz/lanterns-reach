#version 460
#extension GL_GOOGLE_include_directive : require
#extension GL_EXT_buffer_reference : require
#include "variants.glsl"
#include "quad.glsl"
#include "translucent_common.glsl"
// Preview ghosts on both translucency passes: a few faces rebuilt per frame
// (renderer.cpp), so they keep a vertex format (block_mesh.h MeshVertex)
// with their tiles resolved on the CPU. Same outputs as translucent.vert;
// no quad (no frames: ghosts have no surface layer).

layout(location = 0) in vec3 inPos;
layout(location = 1) in vec3 inNormal;
layout(location = 2) in uint inLayer;
layout(location = 3) in uint inMaterial;
layout(location = 4) in uint inTexInfo;

layout(location = 0) out vec3 outRel;
layout(location = 1) out vec3 outNormal;
layout(location = 2) flat out uint outMaterial;
layout(location = 3) out vec3 outMeshPos;
layout(location = 4) flat out uint outLayer;
layout(location = 5) flat out uint outTexInfo;
layout(location = 6) flat out uvec2 outQuad;

void main() {
    outRel = inPos + pc.meshRel.xyz;
    outNormal = inNormal;
    outMaterial = inMaterial;
    outMeshPos = inPos;
    outLayer = inLayer;
    outTexInfo = inTexInfo;
    outQuad = uvec2(0u);
    gl_Position = pc.viewProj * vec4(outRel, 1.0);
}
