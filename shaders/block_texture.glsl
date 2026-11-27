// Block face texturing (render/block-textures), shared by the G-buffer and
// both translucency passes. Include after variants.glsl and after declaring
// `blockTextures` (a sampler2DArray with mips; linear filtering, clamp to
// edge, anisotropic where supported).
//
// The texel position comes from the fragment's MESH-RELATIVE position, not a
// vertex UV, so a quad spanning several cells (v0.2's merged faces) still
// samples per cell: the in-face coordinates (along R, along U; variants.glsl)
// are continuous over the plane, and the image is turned per cell. Mesh origins are on the integer lattice, so
// fract() of mesh-relative coordinates is the in-cell position.
//
// Texture info (an appearance payload's, state_table.h): bits 0–1 the image's CCW
// quarter turns on this face, bits 2–4 the set kind (variants.glsl), bits
// 5–9 a random set's tile count − 1. Random and matched faces pick their
// tile here, per fragment, from the face's cell; a connected face's from
// which of its cell's edges lie on a framed side of its quad.

const float kTexelsPerFace = 16.0;  // blocks.h kTextureSize

uint texTurn(uint texInfo) { return texInfo & 3u; }
uint texSet(uint texInfo) { return (texInfo >> 2u) & 7u; }
uint texRandomCount(uint texInfo) { return ((texInfo >> 5u) & 31u) + 1u; }

// The image turned k quarter turns CCW is sampled at the point turned the
// other way, about the cell centre (0.5, 0.5); a 90° turn about a cell centre
// maps the unit lattice to itself, so this commutes with fract().
vec2 turnImageUv(vec2 uv, uint k) {
    vec2 p = vec2(uv.x - 0.5, 0.5 - uv.y);
    for (uint i = 0u; i < (k & 3u); ++i) p = vec2(p.y, -p.x);
    return vec2(p.x + 0.5, 0.5 - p.y);
}

// The texture-array layer and continuous (un-fract'ed) image UV for a
// fragment at mesh-relative position `pos` on a face with outward normal
// `normal`.
struct FaceTexel {
    vec2 uvRaw;  // continuous over the plane: derivatives are well defined
    float layer;
};

// `cell` is the world cell the face belongs to (random and matched sets hash
// it); `framed` the face positions of its connected-frame edges
// (quad.glsl quadFramedPositions; 0 without frames).
FaceTexel blockFaceTexel(vec3 pos, vec3 normal, ivec3 cell, uint layer, uint texInfo, uint framed) {
    ivec3 n = ivec3(round(normal));
    vec3 r = vec3(variantFaceRight(n));
    vec3 u = vec3(variantFaceUp(n));
    // Image u runs along R, image v down from the top (+U) edge.
    vec2 raw = vec2(dot(pos, r), 1.0 - dot(pos, u));
    FaceTexel t;
    t.uvRaw = turnImageUv(raw, texTurn(texInfo));
    uint l = layer;
    uint kind = texSet(texInfo);
    if (kind == kSetRandom || kind == kSetMatchedNS || kind == kSetMatchedNSEW)
        l += variantTile(kind, texRandomCount(texInfo), cell, n, texTurn(texInfo));
    else if (kind == kSetConnected)
        l += variantImageBits(framed, texTurn(texInfo));
    t.layer = float(l);
    return t;
}

// Anti-aliased nearest: each texel a flat colour, the edge between two
// texels blended over about one screen pixel (fwidth), with mip selection
// and anisotropy from the true UV derivatives (textureGrad), so distant faces
// use the mip chain.
vec4 sampleBlockFace(FaceTexel t) {
    vec2 texel = fract(t.uvRaw) * kTexelsPerFace;
    vec2 width = max(fwidth(t.uvRaw * kTexelsPerFace), vec2(1e-5));
    vec2 seam = floor(texel + 0.5);
    vec2 aa = (seam + clamp((texel - seam) / width, -0.5, 0.5)) / kTexelsPerFace;
    return textureGrad(blockTextures, vec3(aa, t.layer), dFdx(t.uvRaw), dFdy(t.uvRaw));
}
