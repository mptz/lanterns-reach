#version 460
#extension GL_GOOGLE_include_directive : require
#extension GL_EXT_buffer_reference : require
#include "variants.glsl"
#include "quad.glsl"
#include "translucent_common.glsl"
// Both translucency passes draw the world's translucent mesh with this vertex
// shader, pulling its packed quads (gbuffer.vert). Positions are
// mesh-relative; the mesh origin is rebased in f64 on the CPU, so the
// outputs here are camera-relative (the eye is the origin). The
// mesh-relative position is passed on for texturing (block_texture.glsl).
// Preview ghosts have their own vertex shader (ghost.vert), same outputs.

layout(location = 0) out vec3 outRel;     // camera-relative position
layout(location = 1) out vec3 outNormal;  // world space (outward)
layout(location = 2) flat out uint outMaterial;
layout(location = 3) out vec3 outMeshPos;
layout(location = 4) flat out uint outLayer;
layout(location = 5) flat out uint outTexInfo;
layout(location = 6) flat out uvec2 outQuad;

void main() {
    uvec2 raw = pc.quads.quads[gl_VertexIndex / 4];
    Quad q = unpackQuad(raw);
    uvec4 p = pc.payloads.payloads[q.appearance];
    vec3 pos = quadCorner(q, uint(gl_VertexIndex % 4));
    outRel = pos + pc.meshRel.xyz;
    outNormal = vec3(quadNormal(q.face));
    outMaterial = p.y;
    outMeshPos = pos;
    outLayer = p.x;
    outTexInfo = p.z;
    outQuad = raw;
    gl_Position = pc.viewProj * vec4(outRel, 1.0);
}
