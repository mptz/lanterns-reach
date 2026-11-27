#include "chunk_world.h"

#include <algorithm>
#include <cmath>
#include <limits>

#include <glm/vector_relational.hpp>

#include "chunk_mesh.h"

ChunkWorld::ChunkWorld(const BlockRegistry& registry) : registry_(registry), chunk_(registry.states(), kChunk) {}

bool ChunkWorld::contains(const CellPos& c) const { return chunkOf(c) == kChunk.coord; }

StateId ChunkWorld::probe(const CellPos& cell) const {
    return contains(cell) ? chunk_.get(localOf(cell)) : kAirState;
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
        if (chunk_.set(localOf(e.cell), e.state)) ++result.changed;
    }
    if (result.changed) ++generation_;
    return result;
}

std::optional<RayHit> ChunkWorld::raycast(const WorldPos& origin, const glm::dvec3& dir, double maxDistance) const {
    return chunk_.raycast(origin, dir, maxDistance);
}

std::vector<Emitter> ChunkWorld::emitters() const { return chunk_.emitters(); }

WorldMesh ChunkWorld::buildMesh(const ChunkId& id) {
    WorldMesh mesh;
    const MeshContext context{registry_, registry_.states()};
    if (id != kChunk) {  // every chunk but the one is air
        Chunk(registry_.states(), id).meshInto(context, mesher_, mesh);
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
