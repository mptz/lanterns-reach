#version 460
#extension GL_GOOGLE_include_directive : require
#extension GL_EXT_buffer_reference : require
// Geometry pass over the world mesh (WorldQuery::buildMesh), pulled from its
// packed quads (quad.glsl): quad gl_VertexIndex / 4, corner gl_VertexIndex % 4
// (one static index buffer, 0 1 2 0 2 3 per quad, serves every mesh), its
// payload from the appearance table. Positions are relative to the mesh
// origin, which is rebased in f64 on the CPU and folded into the matrix, so
// the GPU only ever sees small f32 values. The fragment shader derives texel
// positions from the mesh-relative position (block_texture.glsl), so there
// is no UV.
#include "variants.glsl"
#include "quad.glsl"

layout(push_constant) uniform Push {
    mat4 mvp;          // proj * view * translate(camera-relative mesh origin)
    ivec4 originCell;  // xyz: the mesh origin's cell; w: flags (gbuffer.frag)
    Quads quads;
    Payloads payloads;
} pc;

layout(location = 0) out vec3 outNormal;
layout(location = 1) out vec3 outMeshPos;
layout(location = 2) flat out uint outLayer;
layout(location = 3) flat out uint outMaterial;
layout(location = 4) flat out uint outTexInfo;
layout(location = 5) flat out uvec2 outQuad;

void main() {
    uvec2 raw = pc.quads.quads[gl_VertexIndex / 4];
    Quad q = unpackQuad(raw);
    uvec4 p = pc.payloads.payloads[q.appearance];
    vec3 pos = quadCorner(q, uint(gl_VertexIndex % 4));
    outNormal = vec3(quadNormal(q.face));
    outMeshPos = pos;
    outLayer = p.x;
    outMaterial = p.y;
    outTexInfo = p.z;
    outQuad = raw;
    gl_Position = pc.mvp * vec4(pos, 1.0);
}
