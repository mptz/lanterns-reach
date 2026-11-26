#version 460
#extension GL_GOOGLE_include_directive : require
#include "common.glsl"
// Tonemap (AgX, with exposure) HDR to the sRGB swapchain, or show a debug
// view:
//   1 lit, 2 GB0 albedo, 3 decoded normal (N·0.5+0.5), 4 linearized depth,
//   5 GB2 roughness (R) / metallic (G), 6 GB3 material id (hashed to a
//   color; 0 = none = black), 7 texture variant (the G-buffer pass wrote
//   each face's variant-set tile as a colour into GB0; shown like albedo).
// With the F5 heatmap on, every view is tinted by the number of point lights
// covering the pixel (HDR alpha, accumulated by the lighting pass).
// The lit view first composites translucency (render/transparency):
// C = C_opaque · exp(−total absorbance) + accumulated translucent radiance.
// F9 replaces the image with the OIT data: 1 total absorbance (1 − e^−A per
// channel), 2 normalized moments b1, b2, 3 normalized moments b3, b4.

layout(set = 0, binding = 0) uniform sampler2D hdr;
layout(set = 0, binding = 1) uniform sampler2D gb0;
layout(set = 0, binding = 2) uniform sampler2D gb1;
layout(set = 0, binding = 3) uniform sampler2D gbDepth;
layout(set = 0, binding = 4) uniform usampler2D gb3;
layout(set = 0, binding = 5) uniform sampler2D gb2;
layout(set = 0, binding = 6) uniform sampler2D oitB0;
layout(set = 0, binding = 7) uniform sampler2D oitMoments;
layout(set = 0, binding = 8) uniform sampler2D oitAccum;

layout(push_constant) uniform Push {
    int view;
    float zNear;
    float zFar;
    float depthVisRange;  // linear depth mapped to white
    float exposure;       // linear multiplier (2^EV)
    int heatmap;          // F5: tint by light count
    float maxLights;      // heatmap full scale
    int oitView;          // F9: 0 off, 1 absorbance, 2 moments b1 b2, 3 moments b3 b4
} pc;

layout(location = 0) out vec4 outColor;

// Debug values are data, not colors: pre-linearize with the exact sRGB EOTF
// (the inverse of the swapchain's encode) so the value lands on screen as is.
vec3 asDisplayValue(vec3 v) {
    v = clamp(v, 0.0, 1.0);
    return mix(v / 12.92, pow((v + 0.055) / 1.055, vec3(2.4)), greaterThan(v, vec3(0.04045)));
}

// AgX (Troy Sobotka), the widely used minimal fit: inset matrix, log2
// encoding over [−12.47, +4.03] EV, polynomial sigmoid, outset matrix. The
// result is display-encoded, so it is linearized for the sRGB swapchain.
vec3 agx(vec3 v) {
    const mat3 inset = mat3(0.842479062253094, 0.0423282422610123, 0.0423756549057051,
                            0.0784335999999992, 0.878468636469772, 0.0784336,
                            0.0792237451477643, 0.0791661274605434, 0.879142973793104);
    const mat3 outset = mat3(1.19687900512017, -0.0528968517574562, -0.0529716355144438,
                             -0.0980208811401368, 1.15190312990417, -0.0980434501171241,
                             -0.0990297440797205, -0.0989611768448433, 1.15107367264116);
    const float minEv = -12.47393, maxEv = 4.026069;
    v = inset * v;
    v = clamp((log2(max(v, vec3(1e-10))) - minEv) / (maxEv - minEv), 0.0, 1.0);
    vec3 x2 = v * v, x4 = x2 * x2;
    v = 15.5 * x4 * x2 - 40.14 * x4 * v + 31.96 * x4 - 6.868 * x2 * v + 0.4298 * x2 + 0.1191 * v - 0.00232;
    return asDisplayValue(outset * v);
}

// Heatmap ramp: blue → cyan → green → yellow → red over [0, 1].
#include "heat.glsl"

void main() {
    ivec2 px = ivec2(gl_FragCoord.xy);
    float d = texelFetch(gbDepth, px, 0).r;
    vec4 lit = texelFetch(hdr, px, 0);
    vec3 c;
    if (pc.view == 2 || pc.view == 7) {
        c = texelFetch(gb0, px, 0).rgb;
    } else if (pc.view == 3) {
        c = d >= 1.0 ? vec3(0.0) : asDisplayValue(octDecode(texelFetch(gb1, px, 0).rg) * 0.5 + 0.5);
    } else if (pc.view == 4) {
        // Inverse of the projection's depth: z = n·f / (f − d·(f − n)).
        float z = pc.zNear * pc.zFar / (pc.zFar - d * (pc.zFar - pc.zNear));
        c = asDisplayValue(vec3(d >= 1.0 ? 1.0 : z / pc.depthVisRange));
    } else if (pc.view == 5) {
        c = d >= 1.0 ? vec3(0.0) : asDisplayValue(vec3(texelFetch(gb2, px, 0).rg, 0.0));
    } else if (pc.view == 6) {
        uint id = texelFetch(gb3, px, 0).r;
        c = id == 0u ? vec3(0.0) : asDisplayValue(idColor(id));
    } else {
        vec3 absorbance = max(texelFetch(oitB0, px, 0).rgb, vec3(0.0));
        vec3 composite = lit.rgb * exp(-absorbance) + texelFetch(oitAccum, px, 0).rgb;
        c = agx(composite * pc.exposure);
    }
    if (pc.oitView != 0) {
        vec4 b0 = texelFetch(oitB0, px, 0);
        vec4 m = b0.a > 0.001 ? texelFetch(oitMoments, px, 0) / b0.a : vec4(0.0);
        if (pc.oitView == 1) c = asDisplayValue(vec3(1.0) - exp(-max(b0.rgb, vec3(0.0))));
        else if (pc.oitView == 2) c = b0.a > 0.001 ? asDisplayValue(vec3(m.x * 0.5 + 0.5, m.y, 0.0)) : vec3(0.0);
        else c = b0.a > 0.001 ? asDisplayValue(vec3(m.z * 0.5 + 0.5, m.w, 0.0)) : vec3(0.0);
    }
    if (pc.heatmap != 0 && lit.a > 0.5)
        c = mix(c, asDisplayValue(heat(clamp(lit.a / pc.maxLights, 0.0, 1.0))), 0.6);
    outColor = vec4(c, 1.0);
}
