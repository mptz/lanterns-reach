#version 460
#extension GL_GOOGLE_include_directive : require
#include "common.glsl"

layout(set = 0, binding = 0) uniform sampler2DArray blockTextures;

#include "variants.glsl"
#include "block_texture.glsl"
#include "material.glsl"
layout(set = 0, binding = 1, std430) readonly buffer Materials { Material materials[]; };

layout(push_constant) uniform Push {
    mat4 mvp;
    ivec4 originCell;  // xyz: the mesh origin's cell; w: bit 0 texture-variant view, bit 1 variants off
} pc;

layout(location = 0) in vec3 inNormal;
layout(location = 1) in vec3 inMeshPos;
layout(location = 2) flat in uint inLayer;
layout(location = 3) flat in uint inMaterial;
layout(location = 4) flat in uint inTexInfo;

layout(location = 0) out vec4 outGB0;  // albedo (sRGB target), A = 1
layout(location = 1) out vec2 outGB1;  // octahedral normal
layout(location = 2) out vec4 outGB2;  // roughness, metallic, reflectance, shading-model bits (0)
layout(location = 3) out uint outGB3;  // material id

void main() {
    Material m = materials[inMaterial];
    // Variants off (timing A/B): position-selected faces show tile 0.
    uint kind = texSet(inTexInfo);
    bool positional = kind == kSetRandom || kind == kSetMatchedNS || kind == kSetMatchedNSEW;
    uint texInfo = (pc.originCell.w & 2) != 0 && positional ? (inTexInfo & 3u) : inTexInfo;
    FaceTexel t = blockFaceTexel(inMeshPos, inNormal, pc.originCell.xyz, inLayer, texInfo);
    vec3 albedo = sampleBlockFace(t).rgb;
    if ((pc.originCell.w & 1) != 0) {
        // Texture-variant view (F2): the tile index as a colour; faces
        // without a set are mid grey.
        uint tile = uint(t.layer) - inLayer;
        albedo = texSet(texInfo) == kSetNone ? vec3(0.2) : idColor(tile + 1u);
    }
    outGB0 = vec4(albedo, 1.0);
    outGB1 = octEncode(normalize(inNormal));
    outGB2 = vec4(m.pbr.xyz, 0.0);
    outGB3 = inMaterial;
    // No emission here: the lighting pass computes it from the material id
    // and this albedo (render/gbuffer-layout).
}
