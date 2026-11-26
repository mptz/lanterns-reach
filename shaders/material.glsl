// The material table entry, std430 (64 bytes): the ONE definition, included
// by every shader that reads the table. It must match GpuMaterial in
// src/renderer.cpp (static_assert there on the size).
struct Material {
    vec4 pbr;                  // roughness, metallic, reflectance, shading-model bits (1 = preview ghost)
    vec4 emissive;             // rgb: opaque emission (G-buffer pass)
    vec4 absorption;           // rgb: translucent σ per block of path length
    vec4 translucentEmission;  // rgb: translucent front-face emission
};
