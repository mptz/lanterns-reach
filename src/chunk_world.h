#pragma once
// The v0.2.1 world: one chunk, (dimension 0, chunk (0,0,0)), holding cells
// 0–63 per axis, behind the WorldQuery seam. Cells outside it read as air and
// reject edits. v0.2.9 puts octree terrain around it; v0.2.24 adds several
// chunks. Nothing outside the chunk code (brick.h, chunk.*) sees cells,
// indices or the palette.

#include <cstdint>
#include <optional>
#include <vector>

#include "chunk.h"
#include "world_query.h"

class ChunkWorld final : public WorldQuery {
public:
    // The registry must outlive the world (meshing reads its face tables and
    // materials). The world starts as air (the `empty` generator).
    explicit ChunkWorld(const BlockRegistry& registry);

    std::optional<RayHit> raycast(const WorldPos& origin, const glm::dvec3& dir, double maxDistance) const override;
    BlockState probe(const CellPos& cell) const override;
    bool editable(const CellPos& cell) const override { return contains(cell); }
    ApplyResult apply(const EditSet& edits) override;
    std::vector<ChunkId> dirtyChunks() const override;
    WorldMesh buildMesh(const ChunkId& chunk) override;
    std::vector<Emitter> emitters() const override;
    std::uint64_t generation() const override { return generation_; }

    // The palette size of the (only) chunk, for tests.
    std::size_t paletteSize() const { return chunk_.paletteSize(); }

    static constexpr ChunkId kChunk{};  // the world: dimension 0, chunk (0,0,0)

private:
    bool contains(const CellPos& c) const;
    std::uint32_t frameBits(const CellPos& c, BlockTypeId type, glm::ivec3 n) const;

    const BlockRegistry& registry_;
    ChunkData chunk_;
    std::uint64_t generation_ = 0;
    std::optional<std::uint64_t> meshedGeneration_;  // the generation buildMesh last meshed
};
