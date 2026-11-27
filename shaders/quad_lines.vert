#version 460
#extension GL_GOOGLE_include_directive : require
#extension GL_EXT_buffer_reference : require
// F3 wireframe: each packed quad's four edges (8 line vertices per quad,
// quad gl_VertexIndex / 8, edge k = (gl_VertexIndex % 8) / 2 from corner k to
// corner k + 1), pulled from the mesh's own arrays, so meshes carry no
// outline list. Positions as in lines.vert: mesh-relative plus the pushed
// camera-relative origin, scaled toward the eye by the bias.
#include "variants.glsl"
#include "quad.glsl"

layout(push_constant) uniform Push {
    mat4 viewProj;
    vec4 offsetBias;  // xyz: camera-relative mesh origin; w: bias
    vec4 color;       // rgb: linear
    Quads quads;
} pc;

layout(location = 0) out vec3 outColor;

void main() {
    Quad q = unpackQuad(pc.quads.quads[gl_VertexIndex / 8]);
    uint v = uint(gl_VertexIndex % 8);
    vec3 pos = quadCorner(q, ((v / 2u) + (v & 1u)) % 4u);
    outColor = pc.color.rgb;
    gl_Position = pc.viewProj * vec4((pos + pc.offsetBias.xyz) * (1.0 - pc.offsetBias.w), 1.0);
}
