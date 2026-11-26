#include "cube.h"

#include <glm/geometric.hpp>

namespace {

struct Face {
    glm::vec3 n;  // outward normal
    glm::vec3 t;  // texture "up" direction on the face; s = t × n so s × t = n
};

// Side faces keep texture-up = world +z; top/bottom use +y (north).
constexpr Face kFaces[6] = {
    {{+1, 0, 0}, {0, 0, 1}},  // east
    {{-1, 0, 0}, {0, 0, 1}},  // west
    {{0, +1, 0}, {0, 0, 1}},  // north
    {{0, -1, 0}, {0, 0, 1}},  // south
    {{0, 0, +1}, {0, 1, 0}},  // top
    {{0, 0, -1}, {0, 1, 0}},  // bottom
};

std::array<Vertex, cube::kVertexCount> buildVertices(glm::vec3 size) {
    std::array<Vertex, cube::kVertexCount> v{};
    constexpr float h = 0.5f;
    // Corners in (s, t) order: (-,-) (+,-) (+,+) (-,+) — CCW about n.
    constexpr float cs[4] = {-1, +1, +1, -1};
    constexpr float ct[4] = {-1, -1, +1, +1};
    for (int f = 0; f < 6; ++f) {
        const glm::vec3 n = kFaces[f].n;
        const glm::vec3 t = kFaces[f].t;
        const glm::vec3 s = glm::cross(t, n);
        const float es = glm::dot(glm::abs(s), size), et = glm::dot(glm::abs(t), size);  // face extents
        for (int c = 0; c < 4; ++c) {
            Vertex& vx = v[static_cast<std::size_t>(f * 4 + c)];
            vx.pos = (n + s * cs[c] + t * ct[c]) * h * size;
            vx.normal = n;
            // Image rows go downward, so texture v runs opposite to t.
            vx.uv = {(cs[c] + 1.0f) * 0.5f * es, (1.0f - (ct[c] + 1.0f) * 0.5f) * et};
        }
    }
    return v;
}

std::array<std::uint16_t, cube::kIndexCount> buildIndices() {
    std::array<std::uint16_t, cube::kIndexCount> idx{};
    for (std::uint16_t f = 0; f < 6; ++f) {
        const std::uint16_t b = static_cast<std::uint16_t>(f * 4);
        const std::uint16_t quad[6] = {b, static_cast<std::uint16_t>(b + 1), static_cast<std::uint16_t>(b + 2),
                                       b, static_cast<std::uint16_t>(b + 2), static_cast<std::uint16_t>(b + 3)};
        for (int i = 0; i < 6; ++i) idx[static_cast<std::size_t>(f * 6 + i)] = quad[i];
    }
    return idx;
}

std::array<std::uint16_t, cube::kEdgeIndexCount> buildEdgeIndices() {
    // Each face quad contributes its 4 sides; every edge is shared by two
    // faces, so keep the first occurrence (matched by corner positions).
    const auto v = buildVertices(glm::vec3(1.0f));
    std::array<std::uint16_t, cube::kEdgeIndexCount> idx{};
    std::size_t n = 0;
    auto same = [&](std::uint16_t a, std::uint16_t b) { return v[a].pos == v[b].pos; };
    for (std::uint16_t f = 0; f < 6; ++f) {
        for (std::uint16_t c = 0; c < 4; ++c) {
            const auto a = static_cast<std::uint16_t>(f * 4 + c);
            const auto b = static_cast<std::uint16_t>(f * 4 + (c + 1) % 4);
            bool dup = false;
            for (std::size_t i = 0; i < n && !dup; i += 2)
                dup = (same(idx[i], a) && same(idx[i + 1], b)) || (same(idx[i], b) && same(idx[i + 1], a));
            if (dup) continue;
            idx[n++] = a;
            idx[n++] = b;
        }
    }
    return idx;  // n == 24: a cube has 12 edges
}

}  // namespace

const std::array<Vertex, cube::kVertexCount>& cube::vertices() {
    static const auto v = buildVertices(glm::vec3(1.0f));
    return v;
}

const std::array<std::uint16_t, cube::kIndexCount>& cube::indices() {
    static const auto i = buildIndices();
    return i;
}

const std::array<std::uint16_t, cube::kEdgeIndexCount>& cube::edgeIndices() {
    static const auto e = buildEdgeIndices();
    return e;
}
