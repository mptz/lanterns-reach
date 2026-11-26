// Shared by the two translucency passes (absorbance, shading): the push
// constants and material table layout (the fragment shaders also include
// translucent_surface.glsl).

layout(push_constant) uniform Push {
    mat4 viewProj;  // camera-relative
    vec4 meshRel;   // xyz: camera-relative mesh origin; w: ambient
    vec4 toLight;   // xyz: −L (unit); w: MBOIT moment bias
    vec4 sun;       // rgb: directional radiance; w: MBOIT overestimation
    vec4 params;    // x: camera near, y: camera far, z: point-light count, w: 1 = draw glass frames
} pc;

#include "material.glsl"

// Linear view depth from a [0, 1] depth value (the projection's inverse).
float linearDepth(float d) { return pc.params.x * pc.params.y / (pc.params.y - d * (pc.params.y - pc.params.x)); }
