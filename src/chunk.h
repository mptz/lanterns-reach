#pragma once
// The chunk (world/cubic-chunks, world/chunk): a 64³ cube of cells addressed
// by (dimension_id, chunk_coord). `Chunk` is the chunk actor, here its
// single-threaded stand-in (no Vicinity or turns yet), held by ChunkWorld. It
// owns its cells as a Brick through a pointer, the representation pointer:
// uniform or a palette at some width (brick.h); there is no chunk-data type
// between them (invariant/chunk/content-is-cells-and-time). It is the
// abstraction boundary over its Brick: its code uses the Brick directly, and
// callers ask it semantic questions. Every edit goes through the chunk, never
// straight to the Brick. A change of representation builds the new Brick from the old, swaps
// the pointer and frees the old one directly (single-threaded here: the
// seqlock window and retirement through a grace period arrive with validated
// reads, v0.2.24).

#include <cstdint>
#include <memory>
#include <optional>

#include <glm/vec3.hpp>

#include "blocks.h"
#include <set>
#include <tuple>
#include <vector>
#include "brick.h"
#include "chunk_coord.h"
#include "digest.h"
#include "mesher.h"
#include "reverse_lookup.h"
#include "world_query.h"

struct ChunkTestAccess;
struct MeshContext;
struct WorldMesh;

class Chunk {
public:
    // Every cell air (the `empty` generator): a uniform Brick. The state
    // table must outlive the chunk (the digest and emitters read it).
    explicit Chunk(const StateTable& states, ChunkId id = {});

    const ChunkId& id() const { return id_; }
    Chunk(const Chunk& o);
    Chunk& operator=(const Chunk& o);
    Chunk(Chunk&&) noexcept = default;
    Chunk& operator=(Chunk&&) noexcept = default;

    StateId get(const glm::ivec3& local) const {
        return brick_->at(static_cast<std::uint32_t>(brickCellIndex(local.x, local.y, local.z)));
    }
    // Sets a cell; returns whether its state changed. An edit only ever
    // upgrades the representation (uniform to a palette at 1 bit, a full
    // palette to the next width) and never compacts: the reverse lookup finds
    // the state's entry (dead ones included), or appends one.
    bool set(const glm::ivec3& local, StateId state);

    // The state filling the chunk, if it is uniform.
    std::optional<StateId> uniformState() const;

    // Light-emitting cells (world cells, z, y, x order), maintained per
    // edit, so this reads no cell but the emitters
    // (core/opaque-chunk-queries: O(1) or O(surface), never O(volume)).
    std::vector<Emitter> emitters() const;
    const StateTable& states() const { return *states_; }

    // The content digest (data/content-digest): computed on first request in
    // one pass, then maintained per edit. Equal for equal content, whatever
    // the representation or width.
    std::uint64_t digest() const;

    // Appends the chunk's faces to `out` (positions relative to the chunk's
    // minimum corner, which becomes the mesh origin; the chunk's own id says
    // where that is). Each representation
    // meshes its own way: uniform air emits nothing, a uniform solid its six
    // sides, a palette the bitmask mesher; `PerCell` is the reference over
    // any of them. Only appends: whatever `out` holds is never read, moved or
    // changed. Defined in chunk_mesh.cpp.
    void meshInto(const MeshContext& context, Mesher mesher, WorldMesh& out) const;

    // The first solid cell along a ray within this chunk (world positions
    // and cells; outside the chunk is air), within maxDistance. `dir` need
    // not be normalized.
    std::optional<RayHit> raycast(const WorldPos& origin, const glm::dvec3& dir, double maxDistance) const;

    // Ends a bulk fill (the `empty` generator, a scenario's scene applied as
    // one edit set): compacts, so the chunk is uniform if one state remains,
    // else a palette at the narrowest width under the low-water mark.
    void finishBulkFill();

private:
    friend struct ChunkTestAccess;

    PaletteBrick<StateId>& palette() { return static_cast<PaletteBrick<StateId>&>(*brick_); }
    const PaletteBrick<StateId>& palette() const { return static_cast<const PaletteBrick<StateId>&>(*brick_); }
    // The light a state emits (an opaque block's emissive, a translucent
    // block's emission); zero for air and states with no definition.
    glm::vec3 emission(StateId s) const;

    // Replaces the Brick with one holding the same content: uniform if
    // width is 0, else a palette at that width (with room for its entries). keepDead keeps every palette
    // entry and index (a pure repack); otherwise only the live entries
    // remain, renumbered in ascending old-index order (a compaction). The
    // digest carries over.
    void rebuild(unsigned width, bool keepDead);
    // Compaction: uniform if one state is live, else the narrowest width
    // under the low-water mark.
    void compact();
    std::uint32_t liveEntries() const;

    const StateTable* states_;
    ChunkId id_;
    std::unique_ptr<Brick<StateId>> brick_;
    ReverseLookup<StateId> lookup_;     // the owner's reverse lookup, built lazily
    mutable std::optional<std::uint64_t> digest_;
    std::uint64_t growths_ = 0;            // swaps by an edit that found the palette full (the fallback path)
    std::uint64_t widenings_ = 0;          // of those, the ones that changed the width
    struct ZyxLess {
        bool operator()(const CellPos& a, const CellPos& b) const { return std::tie(a.z, a.y, a.x) < std::tie(b.z, b.y, b.x); }
    };
    std::set<CellPos, ZyxLess> emitters_;  // the emitting cells, world coordinates
};

// Test-only access to the representation (called out in RELEASES.md).
struct ChunkTestAccess {
    static bool isUniform(const Chunk& c) { return c.brick_->width() == 0; }
    static unsigned width(const Chunk& c) { return c.brick_->width(); }  // 0 = uniform
    static std::uint32_t paletteSize(const Chunk& c);                    // 1 if uniform
    static std::uint32_t liveEntries(const Chunk& c) { return c.liveEntries(); }
    static bool paletteFull(const Chunk& c) { return c.brick_->width() != 0 && c.palette().full(); }
    static std::uint32_t paletteCapacity(const Chunk& c);
    // A palette at that width (0: uniform, needs exactly one live state),
    // keeping dead entries when the palette fits, else compacting.
    static void forceWidth(Chunk& c, unsigned width);
    static void compact(Chunk& c) { c.compact(); }
    static void dropDigest(Chunk& c) { c.digest_.reset(); }
    static bool digestComputed(const Chunk& c) { return c.digest_.has_value(); }
    static bool tableBuilt(const Chunk& c) { return c.lookup_.tableBuilt(); }
    static void setScanMax(Chunk& c, std::uint32_t n) { c.lookup_.setScanMax(n); }
    static std::uint64_t growths(const Chunk& c) { return c.growths_; }      // swaps for a full palette
    static std::uint64_t widenings(const Chunk& c) { return c.widenings_; }  // of which the width grew
    // Bytes held: indices, palette and reverse table.
    static std::size_t indexBytes(const Chunk& c) { return c.brick_->indexBytes(); }
    static std::size_t paletteBytes(const Chunk& c) { return c.brick_->paletteBytes(); }
    static std::size_t tableBytes(const Chunk& c) { return c.lookup_.tableBytes(); }
};
