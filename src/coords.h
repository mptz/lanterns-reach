#pragma once
// Typed positions (coords/typed-positions, coords/f64-authoritative,
// core/f32-origin-relative).
//
// WorldPos is authoritative, absolute, f64. RenderPos is f32 and ONLY ever
// relative to a render origin (the camera). The single downcast from f64 to
// f32 lives in rebase() below — nowhere else.
//
// Axis convention (coords/axis-convention): x = east, y = north, z = up;
// right-handed. Lengths are in units (one full-detail cell); a unit is about a
// foot in the game world (world/voxel-scale).

#include <cmath>
#include <cstdint>

#include <glm/vec3.hpp>

struct WorldPos {
    double x = 0.0, y = 0.0, z = 0.0;

    constexpr WorldPos operator+(const glm::dvec3& d) const { return {x + d.x, y + d.y, z + d.z}; }
    constexpr WorldPos& operator+=(const glm::dvec3& d) { x += d.x; y += d.y; z += d.z; return *this; }
    // Difference of two absolute positions is an f64 displacement.
    constexpr glm::dvec3 operator-(const WorldPos& o) const { return {x - o.x, y - o.y, z - o.z}; }
};

struct RenderPos {
    glm::vec3 v{0.0f};
};

// Subtract in f64, THEN downcast the (small) result. Never downcast the
// operands first.
inline RenderPos rebase(const WorldPos& world, const WorldPos& origin) {
    const glm::dvec3 rel = world - origin;  // f64
    return RenderPos{glm::vec3(rel)};        // the one f64 -> f32 downcast
}

// A lattice cell (one block). Cell (i, j, k) occupies the unit cube
// [i, i+1) × [j, j+1) × [k, k+1) in world units, so cell (0, 0, 0) has its
// minimum corner at WorldPos (0, 0, 0). 64-bit: the ±10⁹ world bound fits
// with room for arithmetic.
struct CellPos {
    std::int64_t x = 0, y = 0, z = 0;

    constexpr bool operator==(const CellPos&) const = default;
    constexpr CellPos operator+(const glm::ivec3& d) const { return {x + d.x, y + d.y, z + d.z}; }
};

inline CellPos cellContaining(const WorldPos& p) {
    return {static_cast<std::int64_t>(std::floor(p.x)), static_cast<std::int64_t>(std::floor(p.y)),
            static_cast<std::int64_t>(std::floor(p.z))};
}
inline WorldPos cellCorner(const CellPos& c) {
    return {static_cast<double>(c.x), static_cast<double>(c.y), static_cast<double>(c.z)};
}
inline WorldPos cellCenter(const CellPos& c) { return cellCorner(c) + glm::dvec3(0.5); }
