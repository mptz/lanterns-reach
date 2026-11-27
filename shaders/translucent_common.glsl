// Shared by the two translucency passes (absorbance, shading), vertex and
// fragment: the push constants (per draw), the frame's constants (set 1,
// binding 1) and the material table layout (the fragment shaders also
// include translucent_surface.glsl). Include after variants.glsl and
// quad.glsl.

layout(push_constant) uniform Push {
    mat4 viewProj;      // camera-relative
    vec4 meshRel;       // xyz: camera-relative mesh origin; w: ambient
    ivec4 originCell;   // xyz: the mesh origin's cell (variant hashing)
    Quads quads;        // the world mesh's translucent quads (unused by ghosts)
    Payloads payloads;  // the appearance table
} pc;

layout(set = 1, binding = 1, std140) uniform TranslucentFrame {
    vec4 toLight;  // xyz: −L (unit); w: MBOIT moment bias
    vec4 sun;      // rgb: directional radiance; w: MBOIT overestimation
    vec4 params;   // x: camera near, y: camera far, z: point-light count, w: 1 = draw glass frames
} frame;

#include "material.glsl"

// Linear view depth from a [0, 1] depth value (the projection's inverse).
float linearDepth(float d) { return frame.params.x * frame.params.y / (frame.params.y - d * (frame.params.y - frame.params.x)); }
