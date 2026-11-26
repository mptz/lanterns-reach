// Tests: chunks (world/cubic-chunks, world/brick, world/chunk). Registered
// through sandbox_tests.cpp.

#include "test_support.h"

namespace {

// Floor division and local indices: the cell → (chunk, local) map at negative,
// zero and boundary coordinates, and its inverse.
void testChunkAddressing() {
    CHECK(floorDiv(0, 64) == 0 && floorDiv(63, 64) == 0 && floorDiv(64, 64) == 1);
    CHECK(floorDiv(-1, 64) == -1 && floorDiv(-64, 64) == -1 && floorDiv(-65, 64) == -2);
    CHECK(floorMod(-1, 64) == 63 && floorMod(-64, 64) == 0 && floorMod(-65, 64) == 63 && floorMod(64, 64) == 0);

    CHECK((chunkOf({-1, 0, 63}) == ChunkCoord{-1, 0, 0}));
    CHECK((localOf({-1, 0, 63}) == glm::ivec3(63, 0, 63)));
    CHECK((chunkOf({-64, 64, -65}) == ChunkCoord{-1, 1, -2}));
    CHECK((localOf({-64, 64, -65}) == glm::ivec3(0, 0, 63)));
    CHECK((chunkOf({1'000'000'000, -1'000'000'000, 0}) == ChunkCoord{15'625'000, -15'625'000, 0}));
    CHECK((localOf({1'000'000'000, -1'000'000'001, 0}) == glm::ivec3(0, 63, 0)));
    // Every cell in a range around zero round-trips, local indices in 0–63.
    bool ok = true;
    for (std::int64_t x = -130; x <= 130; ++x) {
        const CellPos c{x, -x, x * 3};
        const glm::ivec3 l = localOf(c);
        ok = ok && l.x >= 0 && l.x < 64 && l.y >= 0 && l.y < 64 && l.z >= 0 && l.z < 64 &&
             cellAt(chunkOf(c), l) == c;
    }
    CHECK(ok);
    CHECK((chunkOrigin({-1, 0, 2}) == CellPos{-64, 0, 128}));
    CHECK(kChunkSize == 64);
    // The layout: x + 64·y + 64²·z.
    CHECK(brickCellIndex(0, 0, 0) == 0 && brickCellIndex(1, 0, 0) == 1 && brickCellIndex(0, 1, 0) == 64 &&
          brickCellIndex(0, 0, 1) == 4096 && brickCellIndex(63, 63, 63) == 64 * 64 * 64 - 1);
    // Chunk ids order by dimension, then coordinate.
    CHECK((ChunkId{0, {0, 0, 1}} < ChunkId{1, {0, 0, 0}}) && (ChunkId{0, {-1, 5, 5}} < ChunkId{0, {0, 0, 0}}));
}

// The chunk data: edits go through it, a state already in the palette reuses
// its entry, a new state appends one.
void testChunkDataPalette() {
    ChunkData d;
    CHECK(d.paletteSize() == 1 && d.get({0, 0, 0}).isAir() && d.get({63, 63, 63}).isAir());
    const BlockState stone = solidState();
    CHECK(d.set({1, 2, 3}, stone) && d.paletteSize() == 2);
    CHECK(d.get({1, 2, 3}) == stone && d.get({3, 2, 1}).isAir());  // x, y, z not mixed up
    CHECK(!d.set({1, 2, 3}, stone));                                // no change
    CHECK(d.set({4, 5, 6}, stone) && d.paletteSize() == 2);         // reused
    CHECK(d.set({1, 2, 3}, BlockState::air()) && d.paletteSize() == 2);  // air is entry 0
    const BlockState rotated{typeOf("furnace"), orient::kIdentity};
    CHECK(d.set({0, 0, 0}, rotated) && d.paletteSize() == 3);       // appended
    CHECK(d.set({9, 9, 9}, rotated) && d.paletteSize() == 3);
    CHECK(d.set({9, 9, 9}, stone) && d.paletteSize() == 3);
    // A copy is independent (it owns its own Brick).
    ChunkData copy = d;
    CHECK(copy.set({20, 20, 20}, stone) && d.get({20, 20, 20}).isAir() && copy.get({4, 5, 6}) == stone);
}

// Probes and edits at every face and corner of the chunk; just outside reads
// as air and is rejected.
void testChunkBoundary() {
    const std::int64_t lo = 0, hi = 63;
    std::vector<CellPos> inside;
    for (std::int64_t x : {lo, hi})  // the 8 corners
        for (std::int64_t y : {lo, hi})
            for (std::int64_t z : {lo, hi}) inside.push_back({x, y, z});
    for (std::int64_t a : {lo, hi}) {  // the 6 face centres
        inside.push_back({a, 31, 31});
        inside.push_back({31, a, 31});
        inside.push_back({31, 31, a});
    }
    ChunkWorld w(registry());
    for (const CellPos& c : inside) {
        CHECK(w.editable(c) && w.probe(c).isAir());
        CHECK(w.apply({{{c, solidState()}}}).changed == 1);
        CHECK(w.probe(c) == solidState());
    }
    for (const CellPos& c : inside) CHECK(w.probe(c) == solidState());  // no edit disturbed another
    CHECK(w.paletteSize() == 2);

    // Just outside, on every face: air, not editable, rejected, nothing changes.
    const std::uint64_t gen = w.generation();
    for (const CellPos& c : inside)
        for (int axis = 0; axis < 3; ++axis)
            for (int dir : {-1, 1}) {
                glm::ivec3 d(0);
                d[axis] = dir;
                const CellPos o = c + d;
                if (chunkOf(o) == ChunkCoord{}) continue;  // still inside
                CHECK(!w.editable(o) && w.probe(o).isAir());
                const ApplyResult r = w.apply({{{o, solidState()}}});
                CHECK(r.changed == 0 && r.rejected.size() == 1 && r.rejected[0] == o);
            }
    CHECK(w.generation() == gen);
    CHECK(!w.editable({-1, 0, 0}) && !w.editable({0, 64, 0}) && !w.editable({0, 0, -64}));
    CHECK(!w.editable({1'000'000'000, -1'000'000'000, 5}));
    // Removing everything again reuses air (entry 0); the palette doesn't grow.
    for (const CellPos& c : inside) CHECK(w.apply({{{c, BlockState::air()}}}).changed == 1);
    CHECK(w.paletteSize() == 2);
}

// The world's mesh covers the whole chunk and nothing outside it: a block on
// each face of the chunk has its outward face (outside is air).
void testChunkMeshAtBoundary() {
    ChunkWorld w(registry());
    w.apply({{{{0, 0, 0}, solidState()}, {{63, 63, 63}, solidState()}}});
    CHECK(w.buildMesh(ChunkWorld::kChunk).indices.size() / 6 == 12);
    // Another chunk is all air: an empty mesh at its own origin.
    const WorldMesh other = w.buildMesh({0, {1, 0, 0}});
    CHECK(other.indices.empty() && other.origin.x == 64.0 && other.origin.y == 0.0);
}

}  // namespace

void runChunkTests() {
    testChunkAddressing();
    testChunkDataPalette();
    testChunkBoundary();
    testChunkMeshAtBoundary();
}
