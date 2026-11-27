#version 460
#extension GL_GOOGLE_include_directive : require
#extension GL_EXT_buffer_reference : require
#include "variants.glsl"
#include "quad.glsl"
#include "translucent_common.glsl"
#include "translucent_surface.glsl"
#include "common.glsl"
#include "pbr.glsl"
#include "oit.glsl"
// Translucency pass 2: shade each camera-facing translucent face (cullMode
// BACK; depth-tested against the opaque depth here, read-only) with the same
// BRDF and light loop as the opaque lighting pass, reconstruct the
// transmittance IN FRONT of it from the moments, and ADD (emission + lit
// color) × that transmittance. Translucent surfaces are specular-only
// dielectrics (their volume absorbs; they have no diffuse albedo), plus
// front-face emission, plus — where the connected-glass frame covers the
// face — the frame's own lit color (its texture), weighted by its coverage
// (the texture's alpha). Preview ghosts (shading-model bit 1) emit their
// block's own face texture, faintly, so the ghost shows the block's type and
// orientation; they have no frames.

layout(set = 0, binding = 0) uniform sampler2D gbDepth;
layout(set = 0, binding = 1) uniform sampler2D oitB0;
layout(set = 0, binding = 2) uniform sampler2D oitMoments;
layout(set = 0, binding = 3, std430) readonly buffer Materials { Material materials[]; };

struct PointLight {
    vec4 posRadius;  // camera-relative xyz, radius
    vec4 color;      // rgb = color × intensity
};
layout(set = 1, binding = 0, std430) readonly buffer Lights { PointLight lights[]; };

layout(location = 0) in vec3 inRel;
layout(location = 1) in vec3 inNormal;
layout(location = 2) flat in uint inMaterial;
layout(location = 3) in vec3 inMeshPos;
layout(location = 4) flat in uint inLayer;
layout(location = 5) flat in uint inTexInfo;
layout(location = 6) flat in uvec2 inQuad;

layout(location = 0) out vec4 outAccum;

void main() {
    ivec2 px = ivec2(gl_FragCoord.xy);
    // The surface is sampled BEFORE the depth discard: its anti-aliased
    // nearest sampling takes derivatives (fwidth, textureGrad), which are
    // undefined in non-uniform control flow, i.e. after a discard. Lavapipe
    // tolerated it; Intel showed the face's triangle diagonal (v0.1.9).
    Material m = materials[inMaterial];
    bool ghost = m.pbr.w > 0.5;
    vec4 surface = translucentSurface(inMeshPos, inNormal, inLayer, inTexInfo, inQuad, ghost);
    if (gl_FragCoord.z > texelFetch(gbDepth, px, 0).r) discard;  // behind opaque geometry

    float r = length(inRel);
    vec4 b0 = texelFetch(oitB0, px, 0);
    float normalized = 0.0;
    if (b0.a > 0.001)
        normalized = oitNormalizedAbsorbance(texelFetch(oitMoments, px, 0) / b0.a, oitWarp(clamp(r, kOitNear, kOitFar)),
                                             frame.toLight.w, frame.sun.w);
    vec3 transmittance = exp(-max(b0.rgb, vec3(0.0)) * normalized);

    vec3 n = normalize(inNormal);
    vec3 v = normalize(-inRel);
    float rough = m.pbr.r, refl = m.pbr.b;
    vec3 noAlbedo = vec3(0.0);  // specular-only
    float coverage = surface.a;
    vec3 frameAlbedo = surface.rgb;
    const float kFrameRoughness = 0.6;  // the frame band's surface: a matte-ish pale grey; chosen by eye (v0.1.6)
    vec3 emission = ghost ? surface.rgb * m.translucentEmission.rgb : m.translucentEmission.rgb;
    vec3 color = emission + pc.meshRel.w * fresnelF0(noAlbedo, 0.0, refl);
    color += coverage * pc.meshRel.w * frameAlbedo;
    color += shadePbr(n, v, frame.toLight.xyz, noAlbedo, rough, 0.0, refl) * frame.sun.rgb;
    color += coverage * shadePbr(n, v, frame.toLight.xyz, frameAlbedo, kFrameRoughness, 0.0, 0.5) * frame.sun.rgb;
    uint count = uint(frame.params.z);
    for (uint i = 0u; i < count; ++i) {
        vec3 toL = lights[i].posRadius.xyz - inRel;
        float dist = length(toL);
        float lr = lights[i].posRadius.w;
        if (dist >= lr) continue;
        vec3 radiance = lights[i].color.rgb * lightFalloff(1.0, dist, lr);
        color += shadePbr(n, v, toL / dist, noAlbedo, rough, 0.0, refl) * radiance;
        color += coverage * shadePbr(n, v, toL / dist, frameAlbedo, kFrameRoughness, 0.0, 0.5) * radiance;
    }
    outAccum = vec4(color * transmittance, 0.0);
}
