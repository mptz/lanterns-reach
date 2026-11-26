#version 460
#extension GL_GOOGLE_include_directive : require
#include "translucent_common.glsl"
// Both translucency passes draw the translucent mesh with this vertex
// shader. Positions are mesh-relative; the mesh origin is rebased in f64 on
// the CPU, so the outputs here are camera-relative (the eye is the origin).
// The mesh-relative position is passed on for texturing
// (block_texture.glsl).

layout(location = 0) in vec3 inPos;
layout(location = 1) in vec3 inNormal;
layout(location = 2) in uint inLayer;
layout(location = 3) in uint inMaterial;
layout(location = 4) in uint inTexInfo;

layout(location = 0) out vec3 outRel;     // camera-relative position
layout(location = 1) out vec3 outNormal;  // world space (outward)
layout(location = 2) flat out uint outMaterial;
layout(location = 3) out vec3 outMeshPos;
layout(location = 4) flat out uint outLayer;
layout(location = 5) flat out uint outTexInfo;

void main() {
    outRel = inPos + pc.meshRel.xyz;
    outNormal = inNormal;
    outMaterial = inMaterial;
    outMeshPos = inPos;
    outLayer = inLayer;
    outTexInfo = inTexInfo;
    gl_Position = pc.viewProj * vec4(outRel, 1.0);
}
