// Fragment-only part shared by the two translucency passes: the faces'
// surface layer. Include after translucent_common.glsl; binding 4 of set 0
// is the block texture array.

layout(set = 0, binding = 4) uniform sampler2DArray blockTextures;
#include "variants.glsl"
#include "block_texture.glsl"

// The surface layer of a translucent face: its texture's colour and alpha
// = coverage. For a connected set (glass) the mesher chose the frame tile;
// tile 0 is fully transparent, and "frames off" (params.w) forces it. The
// frame is a NEUTRAL surface: the volume already tints correctly
// (Beer–Lambert), so the surface carries only surface attributes — a pale
// grey, lit like any dielectric, whose coverage dims all channels equally.
// Preview ghosts (shading-model bit 1) have no surface layer; their texture
// is their faint emission instead.
vec4 translucentSurface(vec3 meshPos, vec3 normal, uint layer, uint texInfo, bool ghost) {
    FaceTexel t = blockFaceTexel(meshPos, normal, ivec3(0), layer, texInfo);
    vec4 c = sampleBlockFace(t);
    if (ghost) return vec4(c.rgb, 0.0);
    bool framed = texSet(texInfo) == kSetConnected && pc.params.w > 0.5;
    return vec4(c.rgb, framed ? c.a : 0.0);
}

