#version 460
#extension GL_GOOGLE_include_directive : require
#include "translucent_common.glsl"
#include "translucent_surface.glsl"
#include "oit.glsl"
// Translucency pass 1 (render/transparency): accumulate total absorbance and
// 4 moments as BOUNDARY INTEGRALS. Every translucent face fragment adds
// ∓σ·F_k(r) (front-facing: entering its cell's medium; back-facing: leaving),
// with r the eye distance clamped to the opaque surface on this pixel's ray.
// Summed over a pixel this is ∫σ dl and the moments of σ·dl in warped depth,
// at any angle and thickness, in any order. cullMode NONE, no depth test,
// additive blending. A face's connected-glass frame adds a SURFACE absorbance
// −ln(1 − coverage) at its own depth (the paper's original per-surface term;
// coverage = the frame tile's alpha), into the same totals and moments, so
// it commutes too.

layout(set = 0, binding = 0) uniform sampler2D gbDepth;
layout(set = 0, binding = 1, std430) readonly buffer Materials { Material materials[]; };

layout(location = 0) in vec3 inRel;
layout(location = 1) in vec3 inNormal;
layout(location = 2) flat in uint inMaterial;
layout(location = 3) in vec3 inMeshPos;
layout(location = 4) flat in uint inLayer;
layout(location = 5) flat in uint inTexInfo;

layout(location = 0) out vec4 outB0;       // rgb: per-channel absorbance; a: scalar (mean σ)
layout(location = 1) out vec4 outMoments;  // b1..b4 of the scalar absorbance

void main() {
    float r = length(inRel);
    // The opaque surface on this pixel's ray, at the same eye-distance scale:
    // both points lie on one ray, so distances scale with linear view depth.
    float dOpaque = texelFetch(gbDepth, ivec2(gl_FragCoord.xy), 0).r;
    float rOpaque = dOpaque >= 1.0 ? kOitFar : r * linearDepth(dOpaque) / linearDepth(gl_FragCoord.z);
    Material mat = materials[inMaterial];
    bool ghost = mat.pbr.w > 0.5;  // preview ghosts: no surface layer
    vec3 sigma = mat.absorption.rgb;
    float sigmaScalar = (sigma.r + sigma.g + sigma.b) / 3.0;
    OitMoments f = oitAntiderivative(min(r, rOpaque));
    float s = gl_FrontFacing ? -1.0 : 1.0;
    outB0 = vec4(sigma * (s * f.b0), sigmaScalar * s * f.b0);
    outMoments = f.b * (sigmaScalar * s);

    float coverage = translucentSurface(inMeshPos, inNormal, inLayer, inTexInfo, ghost).a;
    if (coverage > 0.0 && r < rOpaque) {
        float a = -log(1.0 - coverage);
        float w = oitWarp(clamp(r, kOitNear, kOitFar));
        float w2 = w * w;
        outB0 += vec4(a, a, a, a);
        outMoments += a * vec4(w, w2, w2 * w, w2 * w2);
    }
}
