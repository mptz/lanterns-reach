// Variant-set tile selection (render/block-textures): which tile of a set a
// block face shows. Valid GLSL and valid C++ (with glm, inside a namespace
// that does `using namespace glm;`), so the shaders, the mesher, the ghost
// builder and the tests share one definition — like pbr.glsl.
//
// Set kinds (blocks.h VariantSet; texture info bits 2–4):
//   0 none, 1 random, 2 matched ns, 3 matched nsew, 4 connected.
//
// Face frame: for outward normal n, U ("up" on the face, the image's top) is
// +z on side faces and +y on top/bottom faces, R = U × n (the image's
// right), matching the cube's layout (src/cube.cpp). Face positions are
// numbered N (+U) = 0, E (+R) = 1, S (−U) = 2, W (−R) = 3.
//
// A face shows the image turned `turn` quarter turns counter-clockwise; one
// CCW turn carries the image's N edge to face position W, E to N, and so on:
// image edge e (N = 0, E = 1, S = 2, W = 3) sits at face position
// (e − turn) mod 4.

const uint kSetNone = 0u;
const uint kSetRandom = 1u;
const uint kSetMatchedNS = 2u;
const uint kSetMatchedNSEW = 3u;
const uint kSetConnected = 4u;

ivec3 variantFaceUp(ivec3 n) { return n.z != 0 ? ivec3(0, 1, 0) : ivec3(0, 0, 1); }

ivec3 variantFaceRight(ivec3 n) {
    ivec3 u = variantFaceUp(n);
    return ivec3(u.y * n.z - u.z * n.y, u.z * n.x - u.x * n.z, u.x * n.y - u.y * n.x);  // U × n
}

// Image-edge bits (bit e = image edge e) from face-position bits.
uint variantImageBits(uint facePositionBits, uint turn) {
    uint bits = 0u;
    for (uint e = 0u; e < 4u; ++e)
        if ((facePositionBits & (1u << ((e + 4u - (turn & 3u)) & 3u))) != 0u) bits |= 1u << e;
    return bits;
}

// lowbias32 (Chris Wellons' integer hash): good avalanche, cheap.
uint variantMix(uint x) {
    x ^= x >> 16u;
    x *= 0x7FEB352Du;
    x ^= x >> 15u;
    x *= 0x846CA68Bu;
    x ^= x >> 16u;
    return x;
}

uint variantHash(ivec3 p, uint salt) {
    uint h = variantMix(uint(p.x) ^ 0x9E3779B9u);
    h = variantMix(h ^ uint(p.y));
    h = variantMix(h ^ uint(p.z));
    return variantMix(h ^ salt);
}

// The face's id within its cell (0–5), for hashing: axis and sign of n.
uint variantFaceId(ivec3 n) {
    uint axis = n.x != 0 ? 0u : (n.y != 0 ? 1u : 2u);
    return axis * 2u + ((n.x + n.y + n.z) > 0 ? 1u : 0u);
}

// `random`: one of `count` tiles per face, by a hash of its cell and face.
uint randomTile(ivec3 cell, ivec3 n, uint count) { return variantHash(cell, 0x51ED27u + variantFaceId(n)) % count; }

// The label of one shared edge, from its midpoint in doubled world
// coordinates (exact integers) and the face plane's axis: the two coplanar
// faces sharing the edge compute the same midpoint and axis, so they agree
// without communicating. Faces meeting at a crease don't need to.
uint edgeLabel(ivec3 doubledMidpoint, ivec3 n) {
    uint axis = n.x != 0 ? 0u : (n.y != 0 ? 1u : 2u);
    return variantHash(doubledMidpoint, axis) & 1u;
}

// The label of image edge e (N = 0 … W = 3) of the face of `cell` toward n.
uint imageEdgeLabel(ivec3 cell, ivec3 n, uint turn, uint e) {
    uint p = (e + 4u - (turn & 3u)) & 3u;  // its face position
    ivec3 u = variantFaceUp(n);
    ivec3 r = variantFaceRight(n);
    ivec3 toEdge = p == 0u ? u : (p == 1u ? r : (p == 2u ? -u : -r));
    return edgeLabel(2 * cell + ivec3(1, 1, 1) + n + toEdge, n);
}

// `matched`: the tile index from the labels of the declared edges only —
// ns: N | S << 1 (4 tiles); nsew: N | E << 1 | S << 2 | W << 3 (16 tiles),
// edges in the image's frame.
uint matchedTile(ivec3 cell, ivec3 n, uint turn, bool nsew) {
    uint north = imageEdgeLabel(cell, n, turn, 0u);
    uint south = imageEdgeLabel(cell, n, turn, 2u);
    if (!nsew) return north | (south << 1u);
    return north | (imageEdgeLabel(cell, n, turn, 1u) << 1u) | (south << 2u) | (imageEdgeLabel(cell, n, turn, 3u) << 3u);
}

// The tile of a position-selected set (random or matched); 0 otherwise.
uint variantTile(uint kind, uint randomCount, ivec3 cell, ivec3 n, uint turn) {
    if (kind == kSetRandom) return randomTile(cell, n, randomCount);
    if (kind == kSetMatchedNS) return matchedTile(cell, n, turn, false);
    if (kind == kSetMatchedNSEW) return matchedTile(cell, n, turn, true);
    return 0u;
}
