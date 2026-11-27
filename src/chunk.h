#pragma once
// Chunks (world/cubic-chunks, world/chunk). A chunk is a 64³ cube of cells
// addressed by (dimension_id, chunk_coord). Chunk data owns its Brick through
// a pointer, the representation pointer: uniform or a palette at some width
// (brick.h). Every edit goes through the chunk data, never straight to the
// Brick. A change of representation builds the new Brick from the old, swaps
// the pointer and frees the old one directly (single-threaded here: the
// seqlock window and retirement through a grace period arrive with validated
// reads, v0.2.24).

#include <compare>
#include <cstdint>
#include <memory>
#include <optional>

#include <glm/vec3.hpp>

#include "blocks.h"
#include "brick.h"
#include "coords.h"
#include "digest.h"
#include "reverse_lookup.h"

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

struct ChunkTestAccess;

class ChunkData {
public:
    // Every cell air (the `empty` generator): a uniform Brick.
    ChunkData();
    ChunkData(const ChunkData& o);
    ChunkData& operator=(const ChunkData& o);
    ChunkData(ChunkData&&) noexcept = default;
    ChunkData& operator=(ChunkData&&) noexcept = default;

    BlockState get(const glm::ivec3& local) const {
        return brick_->at(static_cast<std::uint32_t>(brickCellIndex(local.x, local.y, local.z)));
    }
    // Sets a cell; returns whether its state changed. An edit only ever
    // upgrades the representation (uniform to a palette at 1 bit, a full
    // palette to the next width) and never compacts: the reverse lookup finds
    // the state's entry (dead ones included), or appends one.
    bool set(const glm::ivec3& local, const BlockState& state);

    // The state filling the chunk, if it is uniform.
    std::optional<BlockState> uniformState() const;

    // The content digest (data/content-digest): computed on first request in
    // one pass, then maintained per edit. Equal for equal content, whatever
    // the representation or width.
    std::uint64_t digest() const;

    // Ends a bulk fill (the `empty` generator, a scenario's scene applied as
    // one edit set): compacts, so the chunk is uniform if one state remains,
    // else a palette at the narrowest width under the low-water mark.
    void finishBulkFill();

private:
    friend struct ChunkTestAccess;

    PaletteBrick<BlockState>& palette() { return static_cast<PaletteBrick<BlockState>&>(*brick_); }
    const PaletteBrick<BlockState>& palette() const { return static_cast<const PaletteBrick<BlockState>&>(*brick_); }

    // Replaces the Brick with one holding the same content: uniform if
    // width is 0, else a palette at that width. keepDead keeps every palette
    // entry and index (a pure repack); otherwise only the live entries
    // remain, renumbered in ascending old-index order (a compaction). The
    // digest carries over.
    void rebuild(unsigned width, bool keepDead);
    // Compaction: uniform if one state is live, else the narrowest width
    // under the low-water mark.
    void compact();
    std::uint32_t liveEntries() const;

    std::unique_ptr<Brick<BlockState>> brick_;
    ReverseLookup<BlockState> lookup_;     // the owner's reverse lookup, built lazily
    mutable std::optional<std::uint64_t> digest_;
    std::uint64_t growths_ = 0;            // widenings by an edit (the fallback path)
};

// Test-only access to the representation (called out in RELEASES.md).
struct ChunkTestAccess {
    static bool isUniform(const ChunkData& c) { return c.brick_->width() == 0; }
    static unsigned width(const ChunkData& c) { return c.brick_->width(); }  // 0 = uniform
    static std::uint32_t paletteSize(const ChunkData& c);                    // 1 if uniform
    static std::uint32_t liveEntries(const ChunkData& c) { return c.liveEntries(); }
    static bool paletteFull(const ChunkData& c) { return c.brick_->width() != 0 && c.palette().full(); }
    static std::uint32_t paletteCapacity(const ChunkData& c);
    // A palette at that width (0: uniform, needs exactly one live state),
    // keeping dead entries when the palette fits, else compacting.
    static void forceWidth(ChunkData& c, unsigned width);
    static void compact(ChunkData& c) { c.compact(); }
    static void dropDigest(ChunkData& c) { c.digest_.reset(); }
    static bool digestComputed(const ChunkData& c) { return c.digest_.has_value(); }
    static bool tableBuilt(const ChunkData& c) { return c.lookup_.tableBuilt(); }
    static std::uint64_t growths(const ChunkData& c) { return c.growths_; }
    // Bytes held: indices, palette and reverse table.
    static std::size_t indexBytes(const ChunkData& c) { return c.brick_->indexBytes(); }
    static std::size_t paletteBytes(const ChunkData& c) { return c.brick_->paletteBytes(); }
    static std::size_t tableBytes(const ChunkData& c) { return c.lookup_.tableBytes(); }
};
