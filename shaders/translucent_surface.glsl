// Fragment-only part shared by the two translucency passes: the faces'
// surface layer. Include after translucent_common.glsl (and so after
// variants.glsl and quad.glsl); binding 4 of set 0 is the block texture
// array.

layout(set = 0, binding = 4) uniform sampler2DArray blockTextures;
#include "block_texture.glsl"

// The surface layer of a translucent face: its texture's colour and alpha
// = coverage. For a connected set (glass) the frame tile comes from the
// quad's side bits at the fragment's cell; tile 0 is fully transparent, and "frames off" (params.w) forces it. The
// frame is a NEUTRAL surface: the volume already tints correctly
// (Beer–Lambert), so the surface carries only surface attributes — a pale
// grey, lit like any dielectric, whose coverage dims all channels equally.
// Preview ghosts (shading-model bit 1) have no surface layer; their texture
// is their faint emission instead.
vec4 translucentSurface(vec3 meshPos, vec3 normal, uint layer, uint texInfo, uvec2 packedQuad, bool ghost) {
    Quad q = unpackQuad(packedQuad);
    ivec3 own = quadCellAt(q, meshPos);
    FaceTexel t = blockFaceTexel(meshPos, normal, own + pc.originCell.xyz, layer, texInfo, quadFramedPositions(q, own));
    FaceGrad g = blockFaceGrad(t);
    vec4 c = sampleBlockFaceGrad(t, g);
    // An inner corner (the inside of an L, v0.2.5): the corner's square from
    // the fully framed tile, where the bands of the corner's two edges would
    // overlap (the lesser of the two one-edge tiles' coverage), over the
    // face's own tile. The 16 edge tiles serve every face.
    uint corner = quadCornerAt(q, own, meshPos);
    if (corner < 4u && texSet(texInfo) == kSetConnected) {
        uint turn = texTurn(texInfo);
        FaceTexel e1 = t, e2 = t, full = t;
        e1.layer = float(layer + variantImageBits((corner & 1u) != 0u ? 2u : 8u, turn));  // E (+R) or W (−R)
        e2.layer = float(layer + variantImageBits((corner & 2u) != 0u ? 1u : 4u, turn));  // N (+U) or S (−U)
        full.layer = float(layer + 15u);
        float square = min(sampleBlockFaceGrad(e1, g).a, sampleBlockFaceGrad(e2, g).a);
        if (square > c.a) c = vec4(sampleBlockFaceGrad(full, g).rgb, square);
    }
    if (ghost) return vec4(c.rgb, 0.0);
    bool framed = texSet(texInfo) == kSetConnected && frame.params.w > 0.5;
    return vec4(c.rgb, framed ? c.a : 0.0);
}

