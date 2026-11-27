#pragma once
// The packed quad (versions.md v0.2.5; world/chunk-meshing, render/gpu-driven):
// a world mesh is arrays of these, 8 bytes each, which the vertex shaders
// pull by gl_VertexIndex (shaders/quad.glsl mirrors this file; the bits are
// defined here and nowhere else in C++). Provisional layout:
//
//   bits  0–17  x, y, z of the quad's minimum cell in the chunk, 6 bits each
//   bits 18–23  width − 1 along the face's bit axis (1–64)
//   bits 24–29  height − 1 along the face's row axis (1–64)
//   bits 30–32  face direction (orient::Face)
//   bits 33–36  side bits: which of the quad's sides are framed, in the face
//               frame (variants.glsl): −R, +R, −U, +U; translucent faces only
//   bits 37–40  corner bits: which corners carry an inner-corner frame,
//               (−R−U), (+R−U), (−R+U), (+R+U); translucent faces only
//   bits 41–63  appearance id (state_table.h), 23 bits
//
// A face's axes: ±x faces run their bit axis along y and rows along z; ±y
// faces bits along x, rows along z; ±z faces bits along x, rows along y. The
// face frame's U is the row axis (always positive); R is the bit axis,
// negative on −x and +y faces (variantFaceRight).

#include <cstdint>

#include <glm/vec3.hpp>

#include "orientation.h"

namespace quad {

inline constexpr std::uint32_t kAppearanceBits = 23;
inline constexpr std::uint32_t kMaxAppearance = (1u << kAppearanceBits) - 1u;

inline constexpr int kBitAxis[6] = {1, 1, 0, 0, 0, 0};
inline constexpr int kRowAxis[6] = {2, 2, 2, 2, 1, 1};
// Whether R runs along +bit axis (else −bit axis), per direction.
inline constexpr bool kRightAlongBit[6] = {true, false, false, true, true, false};

// Side bits (the face frame's edges, as connectedTile's frame bits 0–3).
inline constexpr std::uint32_t kSideNegR = 1, kSidePosR = 2, kSideNegU = 4, kSidePosU = 8;

struct Quad {
    glm::ivec3 cell{0};  // minimum cell, chunk-local, 0–63 each
    int w = 1, h = 1;    // extents along the bit and row axes, 1–64
    orient::Face face = orient::PosX;
    std::uint32_t sides = 0;    // 4 bits
    std::uint32_t corners = 0;  // 4 bits
    std::uint32_t appearance = 0;  // 23 bits
    constexpr bool operator==(const Quad&) const = default;
};

constexpr std::uint64_t pack(const Quad& q) {
    using U = std::uint64_t;
    return U(q.cell.x & 63) | U(q.cell.y & 63) << 6 | U(q.cell.z & 63) << 12 | U((q.w - 1) & 63) << 18 |
           U((q.h - 1) & 63) << 24 | U(static_cast<unsigned>(q.face) & 7u) << 30 | U(q.sides & 15u) << 33 |
           U(q.corners & 15u) << 37 | U(q.appearance & kMaxAppearance) << 41;
}

constexpr Quad unpack(std::uint64_t v) {
    auto field = [v](int at, int bits) { return static_cast<int>((v >> at) & ((std::uint64_t{1} << bits) - 1)); };
    Quad q;
    q.cell = {field(0, 6), field(6, 6), field(12, 6)};
    q.w = field(18, 6) + 1;
    q.h = field(24, 6) + 1;
    q.face = static_cast<orient::Face>(field(30, 3));
    q.sides = static_cast<std::uint32_t>(field(33, 4));
    q.corners = static_cast<std::uint32_t>(field(37, 4));
    q.appearance = static_cast<std::uint32_t>(v >> 41);
    return q;
}

}  // namespace quad
