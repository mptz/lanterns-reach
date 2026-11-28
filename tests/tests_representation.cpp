// Tests: chunk representations (world/brick): uniform, palette widths,
// promotion, growth, compaction, the reverse lookup. Registered through
// sandbox_tests.cpp.

#include "test_support.h"

namespace {

using TA = ChunkTestAccess;

void testPromotion() {
    Chunk d(testStates());
    CHECK(TA::isUniform(d) && d.uniformState() == kAirState);
    CHECK(!d.set({1, 2, 3}, kAirState) && TA::isUniform(d));  // no change
    CHECK(d.set({1, 2, 3}, testState(4)));
    CHECK(TA::width(d) == 1 && TA::paletteSize(d) == 2 && !d.uniformState());  // the first second state: 1 bit
    CHECK(d.get({1, 2, 3}) == testState(4) && d.get({0, 0, 0}) == kAirState);
    // Editing back leaves a dead entry: still a palette (an edit never goes back to uniform).
    CHECK(d.set({1, 2, 3}, kAirState));
    CHECK(TA::width(d) == 1 && TA::liveEntries(d) == 1 && TA::paletteSize(d) == 2);
    TA::compact(d);
    CHECK(TA::isUniform(d) && d.uniformState() == kAirState);
}

// Content with exactly n distinct states fills a palette at width w exactly
// when n = 2^w; one more state grows it to the next width.
void testGrowthAtEveryBoundary() {
    for (unsigned w : {1u, 2u, 4u, 8u}) {
        const std::uint32_t n = std::uint32_t{1} << w;
        Cells cells = distinctContent(n, 1);
        Chunk d(testStates());
        fillChunk(d, cells, true);
        TA::forceWidth(d, w);  // a repack: the n entries fill a palette at width w exactly
        CHECK(TA::width(d) == w && TA::paletteSize(d) == n && TA::paletteFull(d));
        const std::uint64_t grew = TA::growths(d);
        const std::uint64_t digest = d.digest();
        d.set({0, 0, 0}, testState(1 + n));  // a new state: every entry live
        cells[0] = testState(1 + n);
        CHECK(TA::width(d) == 2 * w && TA::growths(d) == grew + 1 && TA::paletteSize(d) == n + 1);
        CHECK(d.digest() == oracleDigest(cells) && d.digest() != digest);
        bool same = true;
        for (std::uint32_t c = 0; c < kBrickCells; c += 97) same = same && d.get(localOfIndex(c)) == cells[c];
        CHECK(same);
    }
}

// 16 → 32 bits: 2¹⁶ entries fill a 16-bit palette, one more state grows it.
// The synthetic states are fine for storage.
void testGrowthTo32Bits() {
    Chunk d(testStates());
    for (std::uint32_t k = 1; k < 65536; ++k) d.set(localOfIndex(k), testState(k));
    CHECK(TA::width(d) == 16 && TA::paletteSize(d) == 65536 && TA::paletteFull(d));
    const std::uint64_t grew = TA::growths(d);
    d.set(localOfIndex(70000), testState(65536));
    CHECK(TA::width(d) == 32 && TA::growths(d) == grew + 1 && TA::paletteSize(d) == 65537);
    CHECK(TA::paletteCapacity(d) == 131072);  // twice the 2¹⁶ it was full at
    CHECK(d.get(localOfIndex(70000)) == testState(65536) && d.get(localOfIndex(65535)) == testState(65535));
    CHECK((d.get(localOfIndex(65536)) == kAirState));
}

// A full palette with a dead entry grows; nothing is reclaimed by an edit.
void testFullWithDeadEntryGrows() {
    Cells cells = distinctContent(4, 10);
    Chunk d(testStates());
    fillChunk(d, cells, true);
    CHECK(TA::width(d) == 4 && TA::paletteSize(d) == 4);  // 4 entries > 7/8 of a 2-bit palette: 4 bits
    TA::forceWidth(d, 2);  // a 2-bit palette is full at 4 entries (a repack: entries fit)
    CHECK(TA::width(d) == 2 && TA::paletteFull(d));
    for (std::uint32_t c = 0; c < kBrickCells; ++c)
        if (cells[c] == testState(13)) {
            d.set(localOfIndex(c), testState(10));
            cells[c] = testState(10);
        }
    CHECK(TA::paletteFull(d) && TA::liveEntries(d) == 3 && TA::width(d) == 2);
    const std::uint64_t grew = TA::growths(d);
    d.set({5, 0, 0}, testState(99));
    cells[5] = testState(99);
    CHECK(TA::width(d) == 4 && TA::growths(d) == grew + 1);  // grew; the dead entry stays
    CHECK(TA::paletteSize(d) == 5 && TA::liveEntries(d) == 4);
    CHECK(d.digest() == oracleDigest(cells));
}

// An edit to a state that has a dead entry reuses it.
void testDeadEntryReuse() {
    Cells cells = distinctContent(3, 20);
    cells[0] = testState(29);  // state 29 only at cell 0
    Chunk d(testStates());
    fillChunk(d, cells, true);
    CHECK(TA::paletteSize(d) == 4);
    d.set({0, 0, 0}, testState(20));  // 29 is now dead
    CHECK(TA::liveEntries(d) == 3 && TA::paletteSize(d) == 4);
    const std::uint64_t grew = TA::growths(d);
    d.set({7, 0, 0}, testState(29));  // reuses 29's entry
    CHECK(TA::paletteSize(d) == 4 && TA::growths(d) == grew && TA::liveEntries(d) == 4);
    CHECK(d.get({7, 0, 0}) == testState(29) && TA::paletteDistinct(d));
}

// No edit compacts or narrows: a chunk edited down to one state stays a
// palette at its width until an explicit compaction.
void testEditsNeverCompact() {
    Cells cells = distinctContent(300, 1);  // 16 bits
    Chunk d(testStates());
    fillChunk(d, cells, true);
    CHECK(TA::width(d) == 16);
    for (std::uint32_t c = 0; c < kBrickCells; ++c)
        if (c % 300 >= 5) d.set(localOfIndex(c), testState(1));  // leave 5 live
    CHECK(TA::width(d) == 16 && TA::paletteSize(d) == 300);
    TA::compact(d);
    CHECK(TA::width(d) == 4 && TA::paletteSize(d) == 5 && TA::liveEntries(d) == 5);  // 5 ≤ 7/8 · 16
    for (std::uint32_t c = 0; c < kBrickCells; ++c) d.set(localOfIndex(c), testState(2));
    CHECK(!TA::isUniform(d) && TA::width(d) == 4);
    TA::compact(d);
    CHECK(TA::isUniform(d) && d.digest() == mulMod(testStates().digestTerm(testState(2)), digestK()));
}

// Compaction picks the narrowest width at which the live entries sit at or
// below 7/8 of what the indices can name; a rebuilt palette gets room for
// them (a power of two, at least 16, at most the width's limit).
void testLowWaterMark() {
    CHECK(brickCompactedWidth(2) == 2 && brickCompactedWidth(3) == 2);  // 3 ≤ 3.5; 2 > 7/8 · 2
    CHECK(brickCompactedWidth(4) == 4 && brickCompactedWidth(14) == 4 && brickCompactedWidth(15) == 8);
    CHECK(brickCompactedWidth(224) == 8 && brickCompactedWidth(225) == 16);
    CHECK(brickCompactedWidth(57344) == 16 && brickCompactedWidth(57345) == 32);
    CHECK(brickCompactedWidth(500000) == 32);
    CHECK(brickBuildCapacity(1, 2) == 2 && brickBuildCapacity(2, 3) == 4 && brickBuildCapacity(4, 5) == 16);
    CHECK(brickBuildCapacity(8, 20) == 32 && brickBuildCapacity(8, 33) == 64 && brickBuildCapacity(8, 256) == 256);
    CHECK(brickBuildCapacity(16, 300) == 512 && brickBuildCapacity(16, 65536) == 65536);
    CHECK(brickBuildCapacity(32, 100000) == 131072 && brickBuildCapacity(32, 3) == 16);
}

// A full palette below its width's limit doubles its capacity at the same
// width; at the limit it moves to the next width, with twice the capacity.
void testGrowthWithinWidth() {
    Cells cells = distinctContent(20, 1);
    Chunk d(testStates());
    fillChunk(d, cells, true);
    CHECK(TA::width(d) == 8 && TA::paletteSize(d) == 20 && TA::paletteCapacity(d) == 32);
    std::uint32_t k = 21;
    for (std::uint32_t capacity : {32u, 64u, 128u}) {
        while (TA::paletteSize(d) < capacity) d.set(localOfIndex(k), testState(k)), ++k;
        CHECK(TA::paletteFull(d) && TA::paletteCapacity(d) == capacity && TA::width(d) == 8);
        const std::uint64_t grew = TA::growths(d), wide = TA::widenings(d);
        d.set(localOfIndex(k), testState(k)), ++k;
        CHECK(TA::growths(d) == grew + 1 && TA::widenings(d) == wide);  // a bigger palette, the same width
        CHECK(TA::width(d) == 8 && TA::paletteCapacity(d) == 2 * capacity);
    }
    while (TA::paletteSize(d) < 256) d.set(localOfIndex(k), testState(k)), ++k;
    CHECK(TA::paletteFull(d) && TA::paletteCapacity(d) == 256 && TA::width(d) == 8);
    const std::uint64_t wide = TA::widenings(d);
    d.set(localOfIndex(k), testState(k));
    CHECK(TA::widenings(d) == wide + 1 && TA::width(d) == 16 && TA::paletteCapacity(d) == 512);
}

// A palette's array is allocated once at its capacity and never moves: an
// append doesn't, and growth builds another Brick, leaving the old one intact
// (what a reader holding it during the grace period relies on).
void testPaletteNeverMoves() {
    auto p = makePaletteBrick<StateId>(8, 32);
    for (std::uint32_t k = 0; k < 20; ++k) p->append(testState(k));
    const StateId* first = &p->entry(0);
    for (std::uint32_t k = 20; k < 32; ++k) p->append(testState(k));
    CHECK(&p->entry(0) == first && p->full());
    p->setIndex(5, 7);
    auto g = p->grown();
    CHECK(g->width() == 8 && g->capacity() == 64 && g->entries() == 32 && !g->full());
    CHECK(&g->entry(0) != first);  // a new array
    bool same = g->indexAt(5) == 7;
    for (std::uint32_t k = 0; k < 32; ++k) same = same && g->entry(k) == p->entry(k);
    CHECK(same);
    g->append(testState(40));  // the new Brick is written; the old one never is
    CHECK(p->entries() == 32 && p->at(5) == testState(7) && &p->entry(0) == first);
}

// After a bulk fill the chunk is uniform if one state remains, else a
// palette at the narrowest width; the v0.1 scene's fill included.
void testBulkFill() {
    ChunkWorld w(registry());
    CHECK(TA::isUniform(ChunkWorldTestAccess::chunk(w)));  // the empty generator
    std::string err;
    const auto scene = loadScenario(SANDBOX_SCENARIO_DIR "/v0.1-scene.json", err);
    CHECK(scene.has_value());
    if (!scene) return;
    std::vector<std::string> warnings;
    w.applyBulk(sceneEdits(*scene, registry(), &warnings));
    const Chunk& d = ChunkWorldTestAccess::chunk(w);
    const std::uint32_t live = TA::liveEntries(d);
    CHECK(!TA::isUniform(d) && TA::paletteSize(d) == live);  // compacted: no dead entries
    CHECK(TA::width(d) == brickCompactedWidth(live) && live > 1);
    // One state remains: uniform.
    Cells all(kBrickCells, testState(3));
    Chunk f(testStates());
    fillChunk(f, all, true);
    CHECK(TA::isUniform(f));
    Cells two = all;
    two[7] = testState(4);
    Chunk g(testStates());
    fillChunk(g, two, true);
    CHECK(TA::width(g) == 2 && TA::paletteSize(g) == 2);  // 2 > 7/8 of a 1-bit palette
}

// Uniform air meshes to nothing; a uniform solid meshes through the
// per-cell rule like any other content.
void testUniformMesh() {
    ChunkWorld w(registry());
    CHECK(w.buildMesh(ChunkWorld::kChunk).quads.empty() && w.emitters().empty());
    Cells all(kBrickCells, solidState());
    fillChunk(ChunkWorldTestAccess::chunk(w), all, true);
    CHECK(TA::isUniform(ChunkWorldTestAccess::chunk(w)));
    CHECK(w.buildMesh(ChunkWorld::kChunk).quads.size() == 6);  // six merged quads, 64×64
    CHECK(unitFaces(w.buildMesh(ChunkWorld::kChunk)).size() == 6 * 64 * 64);  // the shell
    CHECK(w.probe({0, 0, 0}) == solidState() && w.probe({63, 63, 63}) == solidState());
}

// The reverse lookup agrees on either side of its scan threshold, and the
// table is built only above it.
void testReverseLookupThreshold() {
    Chunk d(testStates());
    const std::uint32_t top = 3 * kReverseScanMax;
    for (std::uint32_t k = 1; k <= top; ++k) {
        d.set(localOfIndex(1000 + k), testState(k));
        d.set({5, 0, 0}, testState(1 + k / 2));  // a lookup of an existing state
        CHECK(TA::tableBuilt(d) == (TA::paletteSize(d) > kReverseScanMax));
    }
    bool same = true;
    for (std::uint32_t k = 1; k <= top; ++k) same = same && d.get(localOfIndex(1000 + k)) == testState(k);
    CHECK(same);
    const std::uint32_t before = TA::paletteSize(d);
    d.set({9, 0, 0}, testState(7));  // existing: no append
    CHECK(TA::paletteSize(d) == before);
    TA::compact(d);
    CHECK(!TA::tableBuilt(d));
}

// A palette past 2¹⁸ entries at 32 bits (dead ones included): the capacity
// doubles by swapping Bricks, the width stays. One cell, rewritten.
void testThirtyTwoBits() {
    Chunk d(testStates());
    d.set({0, 0, 0}, testState(1));
    d.set({1, 0, 0}, testState(2));
    TA::forceWidth(d, 32);
    CHECK(TA::width(d) == 32 && TA::paletteCapacity(d) == 16);  // room for 3 entries, the minimum
    const std::uint64_t grew = TA::growths(d), wide = TA::widenings(d);
    const std::uint32_t target = (1u << 19) + 100;
    std::uint32_t last = 0;
    for (std::uint32_t k = 3; TA::paletteSize(d) < target; ++k) {
        d.set({2, 0, 0}, testState(k));
        last = k;
    }
    CHECK(TA::width(d) == 32 && TA::widenings(d) == wide);
    CHECK(TA::paletteCapacity(d) == (1u << 20) && TA::growths(d) == grew + 16);  // 16 → 2²⁰: sixteen doublings
    CHECK(TA::paletteSize(d) == target);
    CHECK(d.get({2, 0, 0}) == testState(last) && d.get({0, 0, 0}) == testState(1) && d.get({1, 0, 0}) == testState(2));
    CHECK(TA::liveEntries(d) == 4);  // air, 1, 2, the last one
    d.set({2, 0, 0}, testState(5));  // a state with a dead entry
    CHECK(TA::paletteSize(d) == target && TA::paletteDistinct(d));  // reused, across sixteen doublings
    TA::compact(d);
    CHECK(TA::width(d) == 4 && TA::paletteSize(d) == 4 && d.get({2, 0, 0}) == testState(5));
}

// The checks that stay on in every build fire: an index past the palette, an
// append to a full palette.
void testFatalChecks() {
    const FatalHandler saved = fatalHandler();
    fatalHandler() = throwingFatalHandler;
    auto p = makePaletteBrick<StateId>(1);
    p->append(testState(0));
    p->append(testState(1));
    bool pastPalette = false, fullAppend = false;
    try {
        p->setIndex(0, 2);
    } catch (const FatalError&) {
        pastPalette = true;
    }
    try {
        p->append(testState(2));
    } catch (const FatalError&) {
        fullAppend = true;
    }
    CHECK(pastPalette && fullAppend);
    fatalHandler() = saved;
}

}  // namespace

void runRepresentationTests() {
    testPromotion();
    testGrowthAtEveryBoundary();
    testGrowthTo32Bits();
    testGrowthWithinWidth();
    testPaletteNeverMoves();
    testFullWithDeadEntryGrows();
    testDeadEntryReuse();
    testEditsNeverCompact();
    testLowWaterMark();
    testBulkFill();
    testUniformMesh();
    testReverseLookupThreshold();
    testThirtyTwoBits();
    testFatalChecks();
}
