#version 460
#extension GL_GOOGLE_include_directive : require
#include "common.glsl"
#include "pbr.glsl"
// Lighting pass: emission + ambient + directional + every point light (brute
// force, no clustering) through the PBR BRDF, written to the HDR target
// (every pixel, no blending). Emission is the material's emissive factor ×
// the albedo (standing in for glTF's emissive texture, so a lamp's pattern
// shows through its glow), looked up by material id rather than stored per
// pixel (render/gbuffer-layout). Alpha counts the point lights whose radius
// covers the pixel (the F5 heatmap).

layout(set = 0, binding = 0) uniform sampler2D gb0;
layout(set = 0, binding = 1) uniform sampler2D gb1;
layout(set = 0, binding = 2) uniform sampler2D gb2;
layout(set = 0, binding = 3) uniform sampler2D gbDepth;
layout(set = 0, binding = 4) uniform usampler2D gb3;

#include "material.glsl"
layout(set = 0, binding = 5, std430) readonly buffer Materials { Material materials[]; };

struct PointLight {
    vec4 posRadius;  // xyz camera-relative (rebased in f64 on the CPU), w = radius
    vec4 color;      // rgb = linear color × intensity
};
layout(set = 1, binding = 0, std430) readonly buffer Lights { PointLight lights[]; };

layout(push_constant) uniform Push {
    mat4 invViewProj;  // clip → camera-relative position
    vec4 toLight;      // xyz = −L (unit, world space)
    vec4 sun;          // rgb = directional radiance
    vec4 background;   // rgb = linear background color
    float ambient;
    uint lightCount;
} pc;

layout(location = 0) out vec4 outHDR;

void main() {
    ivec2 px = ivec2(gl_FragCoord.xy);
    float d = texelFetch(gbDepth, px, 0).r;
    if (d >= 1.0) {
        outHDR = vec4(pc.background.rgb, 0.0);
        return;
    }
    vec3 albedo = texelFetch(gb0, px, 0).rgb;
    vec3 n = octDecode(texelFetch(gb1, px, 0).rg);
    vec4 m = texelFetch(gb2, px, 0);  // roughness, metallic, reflectance, shading-model bits
    vec2 ndc = (vec2(px) + 0.5) / vec2(textureSize(gbDepth, 0)) * 2.0 - 1.0;
    vec4 p = pc.invViewProj * vec4(ndc, d, 1.0);
    vec3 pos = p.xyz / p.w;  // camera at the origin
    vec3 v = normalize(-pos);

    // Ambient: a crude stand-in for image-based light (none until IBL) —
    // diffuse color plus F0 as the specular response.
    vec3 color = materials[texelFetch(gb3, px, 0).r].emissive.rgb * albedo;
    color += pc.ambient * (albedo * (1.0 - m.g) + fresnelF0(albedo, m.g, m.b));
    color += shadePbr(n, v, pc.toLight.xyz, albedo, m.r, m.g, m.b) * pc.sun.rgb;

    float covered = 0.0;
    for (uint i = 0u; i < pc.lightCount; ++i) {
        vec3 toL = lights[i].posRadius.xyz - pos;
        float dist = length(toL);
        float r = lights[i].posRadius.w;
        if (dist >= r) continue;
        covered += 1.0;
        color += shadePbr(n, v, toL / dist, albedo, m.r, m.g, m.b) * lights[i].color.rgb *
                 lightFalloff(1.0, dist, r);
    }
    outHDR = vec4(color, covered);
}
