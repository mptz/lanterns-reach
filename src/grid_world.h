#pragma once
// v0.1.5's throwaway world: a dense in-memory grid of BlockStates behind the
// WorldQuery seam. Cell (0,0,0) has its minimum corner at the grid origin;
// cells outside [0, size)³ read as air and ignore edits. v0.2 replaces it
// with chunks; nothing outside this file may depend on the dense layout.

#include <cstdint>
#include <vector>

#include "world_query.h"

class DenseGrid final : public WorldQuery {
public:
    // The registry must outlive the grid (meshing reads its face tables and
    // materials).
    DenseGrid(const BlockRegistry& registry, int size, WorldPos origin = {});

    std::optional<RayHit> raycast(const WorldPos& origin, const glm::dvec3& dir, double maxDistance) const override;
    BlockState probe(const CellPos& cell) const override;
    bool editable(const CellPos& cell) const override { return contains(cell); }
    ApplyResult apply(const EditSet& edits) override;
    WorldMesh buildMesh() const override;
    std::vector<Emitter> emitters() const override;
    std::uint64_t generation() const override { return generation_; }

    // Grid-specific (setup and the F10 bounds overlay only).
    int size() const { return size_; }
    WorldPos origin() const { return origin_; }

private:
    bool contains(const CellPos& c) const;

    std::size_t index(const CellPos& c) const;
    std::uint32_t frameBits(const CellPos& c, BlockTypeId type, glm::ivec3 n) const;

    const BlockRegistry& registry_;
    int size_;
    WorldPos origin_;
    std::vector<BlockState> cells_;
    std::uint64_t generation_ = 0;
};
