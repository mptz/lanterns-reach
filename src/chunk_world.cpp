#include "chunk_world.h"

#include <algorithm>
#include <cmath>
#include <limits>

#include <glm/vector_relational.hpp>

#include "chunk_mesh.h"

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

std::optional<RayHit> ChunkWorld::raycast(const WorldPos& origin, const glm::dvec3& dir, double maxDistance) const {
    return chunk_.raycast(origin, dir, maxDistance);
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
    WorldMesh mesh;
    const MeshContext context{registry_, appearance_};
    if (id != kChunk) {  // every chunk but the one is air
        Chunk(id).meshInto(context, mesher_, mesh);
        return mesh;
    }
    meshedGeneration_ = generation_;
    chunk_.meshInto(context, mesher_, mesh);
    return mesh;
}

ApplyResult ChunkWorld::applyBulk(const EditSet& edits) {
    const ApplyResult r = apply(edits);
    chunk_.finishBulkFill();
    return r;
}
