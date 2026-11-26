#pragma once
// The one hardcoded v0.1 cube: 24 vertices (4 per face, per-face normals,
// hard edges), unit size, centered on its model origin. Triangles wind
// counter-clockwise seen from outside (right-hand rule gives the outward
// normal) in the z-up right-handed world frame.

#include <array>
#include <cstdint>

#include <glm/vec2.hpp>
#include <glm/vec3.hpp>

struct Vertex {
    glm::vec3 pos;
    glm::vec3 normal;
    glm::vec2 uv;
};

namespace cube {
inline constexpr std::size_t kVertexCount = 24;
inline constexpr std::size_t kIndexCount = 36;
inline constexpr std::size_t kEdgeIndexCount = 24;  // 12 edges as a line list

const std::array<Vertex, kVertexCount>& vertices();
const std::array<std::uint16_t, kIndexCount>& indices();
// The cube's 12 edges, indexing into vertices() (one line per edge, each
// shared face edge listed once) — the F3 wireframe overlay.
const std::array<std::uint16_t, kEdgeIndexCount>& edgeIndices();
}  // namespace cube
