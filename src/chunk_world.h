#pragma once
// The v0.2.1 world: one chunk, (dimension 0, chunk (0,0,0)), holding cells
// 0–63 per axis, behind the WorldQuery seam. Cells outside it read as air and
// reject edits. v0.2.9 puts octree terrain around it; v0.2.24 adds several
// chunks. ChunkWorld is the world's stand-in (cells to chunks, edits outside
// the chunk rejected, the generation counter, dirtiness, the mesher setting);
// the chunk answers the semantic queries.

#include <cstdint>
#include <optional>
#include <set>
#include <tuple>
#include <vector>

#include "appearance.h"
#include "chunk.h"
#include "world_query.h"

class ChunkWorld final : public WorldQuery {
public:
    // The registry must outlive the world (meshing reads its face tables and
    // materials). The world starts as air (the `empty` generator).
    explicit ChunkWorld(const BlockRegistry& registry);

    // How `buildMesh` meshes (the scenario's `render.mesher`): greedy by default.
    void setMesher(Mesher m) { mesher_ = m; }
    Mesher mesher() const { return mesher_; }

    std::optional<RayHit> raycast(const WorldPos& origin, const glm::dvec3& dir, double maxDistance) const override;
    BlockState probe(const CellPos& cell) const override;
    bool editable(const CellPos& cell) const override { return contains(cell); }
    ApplyResult apply(const EditSet& edits) override;
    // Setup only: `apply`, then ends a bulk fill (chunk.h finishBulkFill), so
    // the chunk is uniform or a palette at the narrowest width.
    ApplyResult applyBulk(const EditSet& edits);
    std::vector<ChunkId> dirtyChunks() const override;
    WorldMesh buildMesh(const ChunkId& chunk) override;
    std::vector<Emitter> emitters() const override;
    std::uint64_t generation() const override { return generation_; }

    static constexpr ChunkId kChunk{};  // the world: dimension 0, chunk (0,0,0)

private:
    friend struct ChunkWorldTestAccess;
    bool contains(const CellPos& c) const;
    // The light a cell's state emits (an opaque block's emissive, a
    // translucent block's emission); zero if none.
    glm::vec3 emission(const BlockState& s) const;

    const BlockRegistry& registry_;
    AppearanceTable appearance_;  // the merge key: every face's appearance id
    Mesher mesher_ = Mesher::Greedy;
    Chunk chunk_;
    // The cells that emit, maintained per edit (core/opaque-chunk-queries:
    // a public answer is O(1) or O(surface), never O(volume)), in z, y, x
    // order, so `emitters()` reads no cell but the emitters.
    struct ZyxLess {
        bool operator()(const CellPos& a, const CellPos& b) const {
            return std::tie(a.z, a.y, a.x) < std::tie(b.z, b.y, b.x);
        }
    };
    std::set<CellPos, ZyxLess> emitters_;
    std::uint64_t generation_ = 0;
    std::optional<std::uint64_t> meshedGeneration_;  // the generation buildMesh last meshed
};

// Test-only access to the world's chunk data.
struct ChunkWorldTestAccess {
    static Chunk& chunk(ChunkWorld& w) { return w.chunk_; }
    static const Chunk& chunk(const ChunkWorld& w) { return w.chunk_; }
};
