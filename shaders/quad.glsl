// The packed quad (src/packed_quad.h, which defines the bits; this file
// mirrors it) and the appearance payloads, read through buffer device
// addresses. Include after variants.glsl; the including shader enables
// GL_EXT_buffer_reference.
//
// A quad is 8 bytes, read as uvec2 (x = bits 0–31, y = bits 32–63):
//   0–17 x, y, z of the minimum cell; 18–23 width − 1 (bit axis); 24–29
//   height − 1 (row axis); 30–32 face direction; 33–36 side bits (−R, +R,
//   −U, +U framed); 37–40 corner bits; 41–63 appearance id.

layout(buffer_reference, std430, buffer_reference_align = 8) readonly buffer Quads { uvec2 quads[]; };
// Per appearance id: x layer, y material, z texture info (block_texture.glsl), w unused.
layout(buffer_reference, std430, buffer_reference_align = 16) readonly buffer Payloads { uvec4 payloads[]; };

struct Quad {
    ivec3 cell;   // minimum cell, mesh-relative
    ivec2 size;   // extents along the bit and row axes
    uint face;    // orient::Face: +x −x +y −y +z −z
    uint sides;   // −R, +R, −U, +U
    uint corners; // (−R−U), (+R−U), (−R+U), (+R+U)
    uint appearance;
};

Quad unpackQuad(uvec2 v) {
    Quad q;
    q.cell = ivec3(int(v.x & 63u), int((v.x >> 6u) & 63u), int((v.x >> 12u) & 63u));
    q.size = ivec2(int((v.x >> 18u) & 63u) + 1, int((v.x >> 24u) & 63u) + 1);
    q.face = (v.x >> 30u) | ((v.y & 1u) << 2u);
    q.sides = (v.y >> 1u) & 15u;
    q.corners = (v.y >> 5u) & 15u;
    q.appearance = v.y >> 9u;
    return q;
}

ivec3 quadNormal(uint face) {
    ivec3 n = ivec3(0);
    n[face / 2u] = (face & 1u) == 0u ? 1 : -1;
    return n;
}
int quadBitAxis(uint face) { return face < 2u ? 1 : 0; }
int quadRowAxis(uint face) { return face < 4u ? 2 : 1; }
// Whether the face frame's R runs along +bit axis (packed_quad.h kRightAlongBit).
bool quadRightAlongBit(uint face) { return face == 0u || face == 3u || face == 4u; }

// Corner k (0–3) of the quad, mesh-relative, in the unit cube's corner order
// (src/cube.cpp: (−R,−U) (+R,−U) (+R,+U) (−R,+U), counter-clockwise from
// outside), so a 1×1 quad is exactly a cube face.
vec3 quadCorner(Quad q, uint k) {
    ivec3 n = quadNormal(q.face);
    ivec3 r = variantFaceRight(n);
    ivec3 u = variantFaceUp(n);
    int cs = (k == 1u || k == 2u) ? 1 : -1;
    int ct = k >= 2u ? 1 : -1;
    // Each component of n + cs·R + ct·U is ±1; (that + 1)/2 is the unit corner.
    ivec3 unit = (n + cs * r + ct * u + ivec3(1)) / 2;
    int b = quadBitAxis(q.face), w = quadRowAxis(q.face);
    unit[b] *= q.size.x;
    unit[w] *= q.size.y;
    return vec3(q.cell + unit);
}

// The face positions (N = +U bit 0, E = +R bit 1, S = −U bit 2, W = −R bit 3;
// variants.glsl) of mesh-relative cell `cell`'s edges that lie on a framed
// side of the quad: the connected tile's frame edges.
uint quadFramedPositions(Quad q, ivec3 cell) {
    if (q.sides == 0u) return 0u;
    int b = quadBitAxis(q.face), w = quadRowAxis(q.face);
    int i = cell[b] - q.cell[b], j = cell[w] - q.cell[w];
    bool lowBit = i == 0, highBit = i == q.size.x - 1;
    bool along = quadRightAlongBit(q.face);
    bool negR = along ? lowBit : highBit, posR = along ? highBit : lowBit;
    uint p = 0u;
    if (j == q.size.y - 1 && (q.sides & 8u) != 0u) p |= 1u;  // +U: N
    if (posR && (q.sides & 2u) != 0u) p |= 2u;               // +R: E
    if (j == 0 && (q.sides & 4u) != 0u) p |= 4u;             // −U: S
    if (negR && (q.sides & 1u) != 0u) p |= 8u;               // −R: W
    return p;
}

// The mesh-relative cell of a fragment at `pos` on the quad: the cell behind
// the face (floor(pos − n/2)), clamped to the quad, so a fragment on a cell
// edge never picks a neighbour outside it.
ivec3 quadCellAt(Quad q, vec3 pos) {
    ivec3 n = quadNormal(q.face);
    ivec3 c = ivec3(floor(pos - 0.5 * vec3(n)));
    int b = quadBitAxis(q.face), w = quadRowAxis(q.face);
    c[b] = clamp(c[b], q.cell[b], q.cell[b] + q.size.x - 1);
    c[w] = clamp(c[w], q.cell[w], q.cell[w] + q.size.y - 1);
    c[q.face / 2u] = q.cell[q.face / 2u];
    return c;
}
