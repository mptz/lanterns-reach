#include "chunk_world.h"

#include <algorithm>
#include <cmath>
#include <limits>

#include <glm/vector_relational.hpp>

#include "chunk_mesh.h"

namespace {

constexpr double kInf = std::numeric_limits<double>::infinity();

}  // namespace

ChunkWorld::ChunkWorld(const BlockRegistry& registry) : registry_(registry), appearance_(registry) {}

bool ChunkWorld::contains(const CellPos& c) const { return chunkOf(c) == kChunk.coord; }

BlockState ChunkWorld::probe(const CellPos& cell) const {
    return contains(cell) ? chunk_.get(localOf(cell)) : BlockState::air();
}

std::vector<ChunkId> ChunkWorld::dirtyChunks() const {
    if (meshedGeneration_ == generation_) return {};
    return {kChunk};
}

ApplyResult ChunkWorld::apply(const EditSet& edits) {
    ApplyResult result;
    for (const Edit& e : edits.edits) {
        if (!contains(e.cell)) {
            result.rejected.push_back(e.cell);
            continue;
        }
        const glm::ivec3 local = localOf(e.cell);
        const BlockState old = chunk_.get(local);
        if (!chunk_.set(local, e.state)) continue;
        ++result.changed;
        if (const glm::vec3 was = emission(old); was.x > 0.0f || was.y > 0.0f || was.z > 0.0f) emitters_.erase(e.cell);
        if (const glm::vec3 now = emission(e.state); now.x > 0.0f || now.y > 0.0f || now.z > 0.0f) emitters_.insert(e.cell);
    }
    if (result.changed) ++generation_;
    return result;
}

// Amanatides–Woo DDA in chunk-local f64 coordinates. A ray starting outside
// the chunk is first clipped to its box (slab test) and enters through a face.
std::optional<RayHit> ChunkWorld::raycast(const WorldPos& worldOrigin, const glm::dvec3& rawDir,
                                         double maxDistance) const {
    const double len = std::sqrt(rawDir.x * rawDir.x + rawDir.y * rawDir.y + rawDir.z * rawDir.z);
    if (!(len > 0.0)) return std::nullopt;
    const glm::dvec3 d = rawDir / len;
    const glm::dvec3 o = worldOrigin - cellCorner(chunkOrigin(kChunk.coord));  // chunk-local
    const double s = static_cast<double>(kChunkSize);

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
                             static_cast<std::int64_t>(kChunkSize - 1));
    }
    if (enterAxis >= 0) normal[enterAxis] = -step[enterAxis];

    auto hitAt = [&](double t) {
        const CellPos c{cell.x, cell.y, cell.z};
        return RayHit{c, normal, cellCorner(chunkOrigin(kChunk.coord)) + o + d * t, probe(c), t};
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
        if (cell[a] < 0 || cell[a] >= kChunkSize) return std::nullopt;  // left the chunk
        tMax[a] += tDelta[a];
        normal = glm::ivec3(0);
        normal[a] = -step[a];
        if (!probe({cell.x, cell.y, cell.z}).isAir()) return hitAt(t);
    }
}

glm::vec3 ChunkWorld::emission(const BlockState& s) const {
    if (s.isAir()) return glm::vec3(0.0f);
    const BlockDef& def = registry_.def(s.type);
    return def.translucent() ? def.translucency->emission : def.material.emissive;
}

std::vector<Emitter> ChunkWorld::emitters() const {
    std::vector<Emitter> out;
    out.reserve(emitters_.size());
    for (const CellPos& c : emitters_) out.push_back({c, emission(chunk_.get(localOf(c)))});
    return out;
}

WorldMesh ChunkWorld::buildMesh(const ChunkId& id) {
    static const ChunkData kAir;  // every chunk but the one is air
    WorldMesh mesh;
    const MeshContext context{registry_, appearance_};
    if (id != kChunk) {
        kAir.meshInto(context, id, mesher_, mesh);
        return mesh;
    }
    meshedGeneration_ = generation_;
    chunk_.meshInto(context, id, mesher_, mesh);
    return mesh;
}

ApplyResult ChunkWorld::applyBulk(const EditSet& edits) {
    const ApplyResult r = apply(edits);
    chunk_.finishBulkFill();
    return r;
}
