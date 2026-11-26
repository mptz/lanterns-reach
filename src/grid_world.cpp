#include "grid_world.h"

#include <algorithm>
#include <cmath>
#include <limits>

#include <glm/vector_relational.hpp>

#include "block_mesh.h"

namespace {

constexpr double kInf = std::numeric_limits<double>::infinity();

}  // namespace

DenseGrid::DenseGrid(const BlockRegistry& registry, int size, WorldPos origin)
    : registry_(registry), size_(size), origin_(origin),
      cells_(static_cast<std::size_t>(size) * static_cast<std::size_t>(size) * static_cast<std::size_t>(size)) {}

bool DenseGrid::contains(const CellPos& c) const {
    return c.x >= 0 && c.y >= 0 && c.z >= 0 && c.x < size_ && c.y < size_ && c.z < size_;
}

std::size_t DenseGrid::index(const CellPos& c) const {
    const auto s = static_cast<std::size_t>(size_);
    return (static_cast<std::size_t>(c.z) * s + static_cast<std::size_t>(c.y)) * s + static_cast<std::size_t>(c.x);
}

BlockState DenseGrid::probe(const CellPos& cell) const {
    return contains(cell) ? cells_[index(cell)] : BlockState::air();
}

ApplyResult DenseGrid::apply(const EditSet& edits) {
    ApplyResult result;
    for (const Edit& e : edits.edits) {
        if (!contains(e.cell)) {
            result.rejected.push_back(e.cell);
            continue;
        }
        BlockState& slot = cells_[index(e.cell)];
        if (slot == e.state) continue;
        slot = e.state;
        ++result.changed;
    }
    if (result.changed) ++generation_;
    return result;
}

// Amanatides–Woo DDA in grid-local f64 coordinates. A ray starting outside
// the grid is first clipped to its box (slab test) and enters through a face.
std::optional<RayHit> DenseGrid::raycast(const WorldPos& worldOrigin, const glm::dvec3& rawDir,
                                         double maxDistance) const {
    const double len = std::sqrt(rawDir.x * rawDir.x + rawDir.y * rawDir.y + rawDir.z * rawDir.z);
    if (!(len > 0.0)) return std::nullopt;
    const glm::dvec3 d = rawDir / len;
    const glm::dvec3 o = worldOrigin - origin_;  // grid-local
    const double s = static_cast<double>(size_);

    // Entry into [0, s]³.
    double tEnter = 0.0, tExit = kInf;
    int enterAxis = -1;
    for (int a = 0; a < 3; ++a) {
        if (d[a] == 0.0) {
            if (o[a] < 0.0 || o[a] >= s) return std::nullopt;  // parallel and outside this slab
            continue;
        }
        double t0 = (0.0 - o[a]) / d[a], t1 = (s - o[a]) / d[a];
        if (t0 > t1) std::swap(t0, t1);
        // An origin exactly on a face but outside the half-open cell range
        // (e.g. x == size) enters through that face at t = 0.
        const bool outsideSlab = o[a] < 0.0 || o[a] >= s;
        if (t0 > tEnter || (enterAxis < 0 && outsideSlab && t0 >= tEnter)) {
            tEnter = t0;
            enterAxis = a;
        }
        tExit = std::min(tExit, t1);
    }
    if (tEnter > tExit || tEnter > maxDistance || tExit <= 0.0) return std::nullopt;

    glm::dvec3 p = o + d * tEnter;
    glm::ivec3 step(0), normal(0);
    glm::i64vec3 cell;
    for (int a = 0; a < 3; ++a) {
        step[a] = d[a] > 0.0 ? 1 : (d[a] < 0.0 ? -1 : 0);
        if (a == enterAxis) p[a] = d[a] > 0.0 ? 0.0 : s;  // exactly on the entered face
        cell[a] = std::clamp(static_cast<std::int64_t>(std::floor(p[a])), std::int64_t{0},
                             static_cast<std::int64_t>(size_ - 1));
    }
    if (enterAxis >= 0) normal[enterAxis] = -step[enterAxis];

    auto hitAt = [&](double t) {
        const CellPos c{cell.x, cell.y, cell.z};
        return RayHit{c, normal, origin_ + o + d * t, probe(c), t};
    };
    if (!probe({cell.x, cell.y, cell.z}).isAir()) return hitAt(tEnter);  // inside (normal 0) or the entered cell

    glm::dvec3 tMax, tDelta;
    for (int a = 0; a < 3; ++a) {
        if (step[a] == 0) {
            tMax[a] = kInf;
            tDelta[a] = kInf;
            continue;
        }
        const double boundary = static_cast<double>(cell[a] + (step[a] > 0 ? 1 : 0));
        tMax[a] = tEnter + (boundary - p[a]) / d[a];
        tDelta[a] = 1.0 / std::abs(d[a]);
    }
    for (;;) {
        // Smallest tMax; exact ties step x before y before z.
        int a = 0;
        if (tMax[1] < tMax[a]) a = 1;
        if (tMax[2] < tMax[a]) a = 2;
        const double t = tMax[a];
        if (t > maxDistance) return std::nullopt;
        cell[a] += step[a];
        if (cell[a] < 0 || cell[a] >= size_) return std::nullopt;  // left the grid
        tMax[a] += tDelta[a];
        normal = glm::ivec3(0);
        normal[a] = -step[a];
        if (!probe({cell.x, cell.y, cell.z}).isAir()) return hitAt(t);
    }
}

// Connected-glass frame bits of translucent cell c's face toward world
// normal n (block_mesh.h connectedTile turns them into a set tile). Bit 4: the face carries the surface layer
// — against air, or against a different translucent type when its own type
// id is lower (so an interface pair is framed once); never against opaque.
// Bits 0–3 (−R, +R, −U, +U): the edge is a boundary unless the in-plane
// neighbour is the same type with an identical face (emitted, same surface
// state) toward n — i.e. the region's surface continues flat across it.
std::uint32_t DenseGrid::frameBits(const CellPos& c, BlockTypeId type, glm::ivec3 n) const {
    auto surfaceOn = [&](const CellPos& cell) -> int {  // −1: no face; 0: face, no surface layer; 1: framed face
        const BlockState across = probe(cell + n);
        if (across.type == type) return -1;
        if (across.isAir()) return 1;
        if (!registry_.def(across.type).translucent()) return 0;
        return type < across.type ? 1 : 0;
    };
    const int mine = surfaceOn(c);
    if (mine != 1) return 0;
    const glm::ivec3 up = n.z != 0 ? glm::ivec3(0, 1, 0) : glm::ivec3(0, 0, 1);
    const glm::ivec3 right(up.y * n.z - up.z * n.y, up.z * n.x - up.x * n.z, up.x * n.y - up.y * n.x);
    const glm::ivec3 sides[4] = {-right, right, -up, up};
    std::uint32_t bits = 16u;
    for (int i = 0; i < 4; ++i) {
        const CellPos e = c + sides[i];
        const bool continues = probe(e).type == type && surfaceOn(e) == mine;
        if (!continues) bits |= 1u << i;
    }
    return bits;
}

// Naive culled mesh. An opaque cell emits a face wherever its neighbour isn't
// opaque (air, translucent, or outside the grid). A translucent cell emits a
// face (into the translucent mesh) wherever its neighbour isn't the same
// group — block type — so a run of one tint has no internal faces, an
// interface between two tints is two coplanar faces, and glass against
// stone keeps its face (it closes the volume for absorbance). Face textures
// come from block_mesh.h (faceTexture); a connected set's tile is chosen
// here from the frame bits; random and matched tiles by the fragment shader
// (opaque) or here (translucent).
WorldMesh DenseGrid::buildMesh() const {
    WorldMesh mesh;
    mesh.origin = origin_;
    for (int z = 0; z < size_; ++z)
        for (int y = 0; y < size_; ++y)
            for (int x = 0; x < size_; ++x) {
                const CellPos c{x, y, z};
                const BlockState st = cells_[index(c)];
                if (st.isAir()) continue;
                const BlockDef& def = registry_.def(st.type);
                const bool translucent = def.translucent();
                auto& verts = translucent ? mesh.translucentVertices : mesh.vertices;
                auto& idx = translucent ? mesh.translucentIndices : mesh.indices;
                const glm::vec3 corner(static_cast<float>(x), static_cast<float>(y), static_cast<float>(z));
                for (int w = 0; w < orient::kFaceCount; ++w) {
                    const auto face = static_cast<orient::Face>(w);
                    const BlockState n = probe(c + orient::faceNormal(face));
                    const bool hidden = translucent ? n.type == st.type
                                                    : !n.isAir() && !registry_.def(n.type).translucent();
                    if (hidden) continue;
                    const auto base = static_cast<std::uint32_t>(verts.size());
                    // Translucent faces are drawn per cell, so a random or
                    // matched tile is chosen here; opaque faces choose theirs
                    // per fragment (merged quads, v0.2).
                    FaceTexture tex = faceTexture(registry_, st, face);
                    if (translucent) tex = resolvePositional(tex, c, face);
                    // A connected set's tile comes from the frame rule
                    // (absorbing translucent faces only).
                    if (tex.set == VariantSet::Connected)
                        tex.layer += translucent && glm::any(glm::greaterThan(def.translucency->absorption, glm::vec3(0.0f)))
                                         ? connectedTile(frameBits(c, st.type, orient::faceNormal(face)), tex.turn)
                                         : 0u;
                    appendFace(verts, idx, tex, face, corner, BlockRegistry::materialId(st.type));
                    for (int k = 0; k < 4; ++k) {
                        mesh.edgeLines.push_back(verts[base + static_cast<std::uint32_t>(k)].pos);
                        mesh.edgeLines.push_back(verts[base + static_cast<std::uint32_t>((k + 1) % 4)].pos);
                    }
                }
            }
    return mesh;
}

std::vector<Emitter> DenseGrid::emitters() const {
    std::vector<Emitter> out;
    for (int z = 0; z < size_; ++z)
        for (int y = 0; y < size_; ++y)
            for (int x = 0; x < size_; ++x) {
                const BlockState st = cells_[index({x, y, z})];
                if (st.isAir()) continue;
                const BlockDef& def = registry_.def(st.type);
                const glm::vec3 e = def.translucent() ? def.translucency->emission : def.material.emissive;
                if (e.x > 0.0f || e.y > 0.0f || e.z > 0.0f) out.push_back({{x, y, z}, e});
            }
    return out;
}
