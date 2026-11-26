#version 460
// Geometry pass over the world mesh (WorldQuery::buildMesh). Positions are
// relative to the mesh origin, which is rebased in f64 on the CPU and folded
// into the matrix, so the GPU only ever sees small f32 values. The fragment
// shader derives texel positions from the mesh-relative position
// (block_texture.glsl), so there is no UV.

layout(location = 0) in vec3 inPos;
layout(location = 1) in vec3 inNormal;  // world space
layout(location = 2) in uint inLayer;
layout(location = 3) in uint inMaterial;
layout(location = 4) in uint inTexInfo;

layout(push_constant) uniform Push {
    mat4 mvp;          // proj * view * translate(camera-relative mesh origin)
    ivec4 originCell;  // xyz: the mesh origin's cell; w: flags (gbuffer.frag)
} pc;

layout(location = 0) out vec3 outNormal;
layout(location = 1) out vec3 outMeshPos;
layout(location = 2) flat out uint outLayer;
layout(location = 3) flat out uint outMaterial;
layout(location = 4) flat out uint outTexInfo;

void main() {
    outNormal = inNormal;
    outMeshPos = inPos;
    outLayer = inLayer;
    outMaterial = inMaterial;
    outTexInfo = inTexInfo;
    gl_Position = pc.mvp * vec4(inPos, 1.0);
}
