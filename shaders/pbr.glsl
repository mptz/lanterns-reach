// Shared BRDF and light falloff (glTF 2.0 metallic-roughness reference).
//
// This file is valid GLSL AND valid C++ with glm: sandbox_tests includes it
// inside a namespace with `using namespace glm;` and tests these exact
// functions. Keep to plain float / vec3 math: no swizzles, no `inout`, no
// integer/float mixing, f-suffixed literals.

const float kPi = 3.14159265f;

// Point-light falloff: inverse-square, windowed smoothly to exactly 0 at the
// radius r — I · saturate(1 − (d/r)⁴)² / (d² + 1).
float lightFalloff(float intensity, float d, float r) {
    float x = d / r;
    float w = clamp(1.0f - x * x * x * x, 0.0f, 1.0f);
    return intensity * w * w / (d * d + 1.0f);
}

// Dielectric F0 = 0.16 · reflectance²; metals use the albedo.
vec3 fresnelF0(vec3 albedo, float metallic, float reflectance) {
    return mix(vec3(0.16f * reflectance * reflectance), albedo, metallic);
}

// GGX / Trowbridge-Reitz normal distribution, α = roughness².
float distributionGgx(float NoH, float a) {
    float a2 = a * a;
    float f = NoH * NoH * (a2 - 1.0f) + 1.0f;
    return a2 / (kPi * f * f);
}

// Height-correlated Smith-GGX visibility (includes the 1 / (4·NoL·NoV)).
float visibilitySmithGgx(float NoV, float NoL, float a) {
    float a2 = a * a;
    float gv = NoL * sqrt(NoV * NoV * (1.0f - a2) + a2);
    float gl = NoV * sqrt(NoL * NoL * (1.0f - a2) + a2);
    return 0.5f / (gv + gl);
}

vec3 fresnelSchlick(float VoH, vec3 f0) {
    float f = pow(1.0f - VoH, 5.0f);
    return f0 + (vec3(1.0f) - f0) * f;
}

// BRDF × cosθ for unit normal n, view direction v and light direction l
// (both pointing away from the surface). Multiply by the light's radiance.
vec3 shadePbr(vec3 n, vec3 v, vec3 l, vec3 albedo, float roughness, float metallic, float reflectance) {
    vec3 h = normalize(v + l);
    float NoV = abs(dot(n, v)) + 1e-5f;
    float NoL = clamp(dot(n, l), 0.0f, 1.0f);
    float NoH = clamp(dot(n, h), 0.0f, 1.0f);
    float LoH = clamp(dot(l, h), 0.0f, 1.0f);
    float r = max(roughness, 0.045f);  // keeps α² away from 0 (no fp16/fp32 blow-up)
    float a = r * r;
    vec3 f0 = fresnelF0(albedo, metallic, reflectance);
    vec3 specular = distributionGgx(NoH, a) * visibilitySmithGgx(NoV, NoL, a) * fresnelSchlick(LoH, f0);
    vec3 diffuse = albedo * ((1.0f - metallic) / kPi);  // Lambert
    return (diffuse + specular) * NoL;
}
