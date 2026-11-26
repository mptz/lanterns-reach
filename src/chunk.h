#pragma once
// Chunks (world/cubic-chunks, world/chunk). A chunk is a 64³ cube of cells
// addressed by (dimension_id, chunk_coord). Chunk data owns its Brick through
// a pointer and every edit goes through the chunk data, never straight to the
// Brick: v0.2.2's copy-and-swap and representation pointer build on that shape.

#include <compare>
#include <cstdint>
#include <memory>

#include <glm/vec3.hpp>

#include "blocks.h"
#include "brick.h"
#include "coords.h"

// Floor division and the matching non-negative remainder (cell −1 is chunk −1,
// local 63).
constexpr std::int64_t floorDiv(std::int64_t a, std::int64_t b) {
    const std::int64_t q = a / b;
    return (a % b != 0 && ((a < 0) != (b < 0))) ? q - 1 : q;
}
constexpr std::int64_t floorMod(std::int64_t a, std::int64_t b) { return a - floorDiv(a, b) * b; }

struct ChunkCoord {
    std::int64_t x = 0, y = 0, z = 0;
    constexpr auto operator<=>(const ChunkCoord&) const = default;
};

using DimensionId = std::uint32_t;

struct ChunkId {
    DimensionId dimension = 0;  // always 0 until dimensions exist
    ChunkCoord coord;
    constexpr auto operator<=>(const ChunkId&) const = default;
};

// The chunk holding a cell, and the cell's local indices (0–63 per axis).
constexpr ChunkCoord chunkOf(const CellPos& c) {
    return {floorDiv(c.x, kChunkSize), floorDiv(c.y, kChunkSize), floorDiv(c.z, kChunkSize)};
}
constexpr glm::ivec3 localOf(const CellPos& c) {
    return {static_cast<int>(floorMod(c.x, kChunkSize)), static_cast<int>(floorMod(c.y, kChunkSize)),
            static_cast<int>(floorMod(c.z, kChunkSize))};
}
// The cell at a chunk's minimum corner.
constexpr CellPos chunkOrigin(const ChunkCoord& c) { return {c.x * kChunkSize, c.y * kChunkSize, c.z * kChunkSize}; }
constexpr CellPos cellAt(const ChunkCoord& c, const glm::ivec3& local) {
    const CellPos o = chunkOrigin(c);
    return {o.x + local.x, o.y + local.y, o.z + local.z};
}

class ChunkData {
public:
    // Every cell air.
    ChunkData();
    ChunkData(const ChunkData& o);
    ChunkData& operator=(const ChunkData& o);
    ChunkData(ChunkData&&) noexcept = default;
    ChunkData& operator=(ChunkData&&) noexcept = default;

    BlockState get(const glm::ivec3& local) const { return brick_->at(local); }
    // Sets a cell; returns whether its state changed. Finds the state's
    // palette index by a linear scan, appending an entry if it is absent (the
    // scan threshold and the reverse table above it are v0.2.2's).
    bool set(const glm::ivec3& local, const BlockState& state);

    std::size_t paletteSize() const { return brick_->paletteSize(); }

private:
    std::unique_ptr<Brick<BlockState>> brick_;
};
