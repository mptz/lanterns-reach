// v0.2.2 acceptance 4 (digest) and 5 (representations), against the prototype.
#include "../common/check.hpp"
#include "brick.hpp"
#include "test_support.hpp"

#include <random>

using namespace proto;
using namespace proto::test;
using TA = BrickTestAccess;
using B = Brick<State>;

static void testModularEdges() {
    CHECK(reduceMod(kP) == 0);
    CHECK(reduceMod(kP - 1) == kP - 1);
    CHECK(reduceMod(~std::uint64_t{0}) == (~std::uint64_t{0}) % kP);
    CHECK(addMod(kP - 1, 1) == 0);
    CHECK(addMod(kP - 1, kP - 1) == kP - 2);
    CHECK(subMod(0, 1) == kP - 1);
    CHECK(subMod(0, kP - 1) == 1);
    CHECK(mulMod(kP - 1, kP - 1) == 1);  // (-1)^2
    CHECK(mulMod(kP - 1, 2) == kP - 2);
    CHECK(mulMod(kP - 2, kP - 2) == 4);
    std::mt19937_64 rng(7);
    for (int i = 0; i < 100000; ++i) {
        std::uint64_t a = rng() % kP, b = rng() % kP;
        if (i % 4 == 0) a = kP - 1 - (rng() % 16);
        if (i % 8 == 0) b = kP - 1 - (rng() % 16);
        unsigned __int128 z = static_cast<unsigned __int128>(a) * b;
        CHECK(mulMod(a, b) == static_cast<std::uint64_t>(z % kP));
        CHECK(addMod(a, b) == (a + b) % kP);
        CHECK(subMod(a, b) == (a + kP - b) % kP);
    }
}

static void testWeights() {
    std::uint64_t k = 0;
    int zeros = 0;
    for (std::uint32_t c = 0; c < kBrickCells; ++c) {
        std::uint64_t w = digestW(c);
        CHECK(w < kP);
        zeros += w == 0;
        k = addMod(k, w);
        // W is defined on coordinates; the layout index converts back exactly.
        CHECK(w == digestWAt(c & 63u, (c >> 6) & 63u, (c >> 12) & 63u));
    }
    CHECK(zeros == 0);  // nonzero by construction; checked for every cell
    CHECK(k == digestK());
}

static void testUniformDigest() {
    for (std::uint32_t k : {0u, 1u, 77u, 5000u}) {
        State s = stateK(k);
        B u(s);
        CHECK(u.digest() == mulMod(stateHash(s), digestK()));
        Cells cells(kBrickCells, s);
        CHECK(u.digest() == oracleDigest(cells));
        for (unsigned w : kWidths) {  // a forced palette of one entry
            B p(s);
            TA::forceWidth(p, w);
            CHECK(TA::width(p) == w);
            CHECK(p.digest() == u.digest());
        }
    }
}

static void testIncrementalDigest() {
    std::mt19937_64 rng(11);
    std::vector<std::uint32_t> pool;
    Cells cells = makeContent(40, rng, pool);
    B b;
    b.fill(cells);
    (void)b.digest();
    for (int i = 0; i < 20000; ++i) {
        std::uint32_t c = static_cast<std::uint32_t>(rng() % kBrickCells);
        State s = stateK(static_cast<std::uint32_t>(rng() % 200));
        b.setCell(c, s);
        cells[c] = s;
    }
    CHECK(TA::digestComputed(b));
    std::uint64_t inc = b.digest();
    CHECK(inc == oracleDigest(cells));
    TA::dropDigest(b);
    CHECK(b.digest() == inc);
    // Representation and width changes carry the digest over unchanged.
    std::uint32_t live = TA::liveEntries(b);
    for (unsigned w : kWidths) {
        if (paletteCapacity(w) < live) continue;
        TA::forceWidth(b, w);
        CHECK(TA::digestComputed(b));
        CHECK(b.digest() == inc);
        TA::dropDigest(b);
        CHECK(b.digest() == inc);
    }
}

static void testPromotion() {
    B b(stateK(3));
    b.set(1, 2, 3, stateK(3));
    CHECK(TA::isUniform(b));
    b.set(1, 2, 3, stateK(4));
    CHECK(TA::width(b) == 1);
    CHECK(TA::paletteSize(b) == 2);
    CHECK(b.get(1, 2, 3) == stateK(4));
    CHECK(b.get(0, 0, 0) == stateK(3));
    // Editing back leaves a dead entry; still width 1, never back to uniform by an edit.
    b.set(1, 2, 3, stateK(3));
    CHECK(TA::width(b) == 1);
    CHECK(TA::liveEntries(b) == 1);
    TA::compact(b);
    CHECK(TA::isUniform(b));
}

// Content with exactly n distinct states, each at >= 2 cells, laid out so
// cell 0 holds state 0 (which appears elsewhere too).
static Cells distinctContent(std::uint32_t n, std::uint32_t base) {
    Cells cells(kBrickCells);
    for (std::uint32_t c = 0; c < kBrickCells; ++c) cells[c] = stateK(base + c % n);
    return cells;
}

static void testGrowthAtEveryBoundary() {
    for (unsigned w : {1u, 2u, 4u, 8u, 16u}) {
        std::uint32_t n = std::uint32_t{1} << w;
        Cells cells = distinctContent(n, 1);
        B b;
        b.fill(cells);
        std::uint64_t d0 = b.digest();
        CHECK(TA::width(b) == w);
        CHECK(TA::paletteFull(b));
        b.setCell(0, stateK(1 + n));  // a new state; every entry live
        CHECK(TA::width(b) == 2 * w);
        CHECK(TA::stats(b).growths == 1);
        CHECK(TA::paletteSize(b) == n + 1);
        cells[0] = stateK(1 + n);
        CHECK(b.digest() == oracleDigest(cells));
        CHECK(b.digest() != d0);
        for (std::uint32_t c = 0; c < kBrickCells; c += 97) CHECK(b.getCell(c) == cells[c]);
    }
}

// A full palette with a dead entry grows; nothing is reclaimed by an edit.
static void testFullWithDeadEntryGrows() {
    Cells cells = distinctContent(4, 10);
    B b;
    b.fill(cells);
    CHECK(TA::width(b) == 2);
    for (std::uint32_t c = 0; c < kBrickCells; ++c)
        if (cells[c] == stateK(13)) {
            b.setCell(c, stateK(10));
            cells[c] = stateK(10);
        }
    CHECK(TA::paletteFull(b));
    CHECK(TA::liveEntries(b) == 3);
    b.setCell(5, stateK(99));
    cells[5] = stateK(99);
    CHECK(TA::width(b) == 4);
    CHECK(TA::stats(b).growths == 1);
    CHECK(TA::paletteSize(b) == 5);
    CHECK(TA::liveEntries(b) == 4);
    CHECK(b.digest() == oracleDigest(cells));
    for (std::uint32_t c = 0; c < kBrickCells; ++c) CHECK(b.getCell(c) == cells[c]);
    CHECK(TA::stats(b).discretionary == 0 && TA::stats(b).explicitCompactions == 0);
}

// An edit to a state with a dead entry reuses that entry.
static void testDeadEntryReuse() {
    Cells cells = distinctContent(3, 20);
    cells[0] = stateK(29);  // state 29 only at cell 0
    B b;
    b.fill(cells);
    CHECK(TA::paletteSize(b) == 4);
    b.setCell(0, stateK(20));  // 29 is now dead
    CHECK(TA::liveEntries(b) == 3);
    b.setCell(7, stateK(29));  // reuses 29's entry
    CHECK(TA::paletteSize(b) == 4);
    CHECK(TA::width(b) == 2);
    CHECK(TA::stats(b).growths == 0);
    CHECK(b.getCell(7) == stateK(29));
}

// At 32 bits, with every cell distinct, new states append past 2^18.
static void testThirtyTwoBitsAppendsPastCells() {
    Cells all(kBrickCells);
    for (std::uint32_t c = 0; c < kBrickCells; ++c) all[c] = stateK(1 + c);
    B f;
    f.fill(all);
    CHECK(TA::width(f) == 32);
    CHECK(TA::paletteSize(f) == kBrickCells);
    for (std::uint32_t i = 0; i < 1000; ++i) {
        std::uint32_t c = (i * 7919u) % kBrickCells;
        f.setCell(c, stateK(1 + kBrickCells + i));
        all[c] = stateK(1 + kBrickCells + i);
    }
    CHECK(TA::width(f) == 32);
    CHECK(TA::paletteSize(f) == kBrickCells + 1000);
    CHECK(TA::liveEntries(f) == kBrickCells);
    CHECK(f.digest() == oracleDigest(all));
    TA::compact(f);
    CHECK(TA::paletteSize(f) == kBrickCells);
    for (std::uint32_t c = 0; c < kBrickCells; c += 13) CHECK(f.getCell(c) == all[c]);
}

// The reverse lookup agrees on either side of its scan threshold, and the
// table is built only above it; compaction drops it.
static void testReverseLookupThreshold() {
    B b;
    const std::uint32_t top = 3 * gReverseScanMax;
    for (std::uint32_t k = 1; k <= top; ++k) {
        b.setCell(1000 + k, stateK(k));
        b.setCell(5, stateK(1 + k / 2));  // a lookup of an existing state
        CHECK(TA::tableBuilt(b) == (TA::paletteSize(b) > gReverseScanMax));
    }
    for (std::uint32_t k = 1; k <= top; ++k) CHECK(b.getCell(1000 + k) == stateK(k));
    std::uint32_t before = TA::paletteSize(b);
    b.setCell(9, stateK(7));  // existing: no append
    CHECK(TA::paletteSize(b) == before);
    TA::compact(b);
    CHECK(!TA::tableBuilt(b));
}

// possibleDeaths is an upper bound on dead entries since the last compaction.
static void testWitnessUpperBound() {
    std::mt19937_64 rng(5);
    std::vector<std::uint32_t> pool;
    Cells cells = makeContent(30, rng, pool);
    B b;
    b.fill(cells);
    std::uint32_t next = 5000;
    for (int i = 0; i < 40000; ++i) {
        std::uint32_t c = static_cast<std::uint32_t>(rng() % kBrickCells);
        State s = (i % 3 == 0) ? stateK(next++) : stateK(pool[rng() % pool.size()]);
        b.setCell(c, s);
        if (i % 4000 == 0) CHECK(b.signals().possibleDeaths >= TA::paletteSize(b) - TA::liveEntries(b));
    }
    std::uint32_t dead = TA::paletteSize(b) - TA::liveEntries(b);
    CHECK(b.signals().possibleDeaths >= dead);
    CHECK(dead > 0);
}

// turnEnd: never compacts under None; the witness policy compacts once its
// bound is reached, and each compaction leaves no dead entry.
static void testTurnEndPolicies() {
    Cells cells = distinctContent(4, 1);
    CompactionPolicy none;
    CompactionPolicy wit;
    wit.kind = CompactionPolicy::Kind::Witness;
    wit.floor = 64;
    wit.fraction = 0.5;
    B a, b;
    a.fill(cells);
    b.fill(cells);
    std::uint32_t next = 1000;
    std::uint64_t compactions = 0;
    for (int i = 0; i < 5000; ++i) {
        std::uint32_t c = cellIndex(1 + 2 * (i % 8), 1, 1);  // painting: lone cells, no neighbour shares a state
        a.setCell(c, stateK(next));
        b.setCell(c, stateK(next));
        ++next;
        CHECK(!a.turnEnd(none));
        if (b.turnEnd(wit)) {
            ++compactions;
            CHECK(TA::paletteSize(b) == TA::liveEntries(b));
            CHECK(b.signals().possibleDeaths == 0);
        }
        CHECK(b.signals().possibleDeaths < 64);
    }
    CHECK(TA::paletteSize(a) == 4 + 5000);
    CHECK(TA::paletteSize(b) <= 12 + 64);
    CHECK(compactions >= 70 && compactions <= 80);  // about one per 64 possible deaths
    for (std::uint32_t c = 0; c < kBrickCells; c += 31) CHECK(a.getCell(c) == b.getCell(c));
    for (int i = 0; i < 8; ++i) CHECK(a.getCell(cellIndex(1 + 2 * i, 1, 1)) == b.getCell(cellIndex(1 + 2 * i, 1, 1)));
    CHECK(a.digest() == b.digest());
    CHECK(TA::width(b) <= 8);
}

// The appends policy bounds the palette by live-at-compaction: P < L0 +
// max(floor, fraction * L0) + 1 at every turn end, under painting.
static void testAppendsBound() {
    CompactionPolicy pol = CompactionPolicy::recommended();
    pol.floor = 128;
    B b;
    b.fill(distinctContent(40, 1));
    std::uint32_t next = 1000;
    std::uint64_t compactions = 0;
    for (int i = 0; i < 20000; ++i) {
        b.setCell(cellIndex(3 + (i % 5), 9, 9), stateK(next++));
        std::uint32_t l0 = b.signals().liveAtCompaction;
        CHECK(TA::paletteSize(b) <= l0 + std::max<double>(pol.floor, pol.fraction * l0) + 1);
        if (b.turnEnd(pol)) {
            ++compactions;
            CHECK(TA::paletteSize(b) == TA::liveEntries(b));
            CHECK(b.signals().appends == 0);
        }
    }
    CHECK(compactions >= 20000 / 129 - 1 && compactions <= 20000 / 128 + 1);
    // Recurring states append nothing, so they never trigger it.
    B r;
    r.fill(distinctContent(40, 1));
    for (int i = 0; i < 20000; ++i) {
        r.setCell(cellIndex(3 + (i % 5), 9, 9), stateK(1 + (i % 40)));
        CHECK(!r.turnEnd(pol));
    }
}

static void testCompactToSmallestAndUniform() {
    Cells cells = distinctContent(300, 1);  // width 16
    B b;
    b.fill(cells);
    CHECK(TA::width(b) == 16);
    std::uint64_t d = b.digest();
    for (std::uint32_t c = 0; c < kBrickCells; ++c)
        if (c % 300 >= 5) b.setCell(c, stateK(1));  // leave 5 live
    CHECK(TA::width(b) == 16);  // edits never narrow
    TA::compact(b);
    CHECK(TA::width(b) == 4);
    CHECK(TA::paletteSize(b) == 5);
    CHECK(TA::liveEntries(b) == 5);
    (void)d;
    for (std::uint32_t c = 0; c < kBrickCells; ++c) b.setCell(c, stateK(2));
    TA::compact(b);
    CHECK(TA::isUniform(b));
    CHECK(b.digest() == mulMod(stateHash(stateK(2)), digestK()));
}

static void testBulkFillChoosesSmallest() {
    // Stand-in for v0.1's scene: air, a floor and three placed block kinds.
    Cells cells(kBrickCells, State{});
    for (std::uint32_t y = 0; y < 32; ++y)
        for (std::uint32_t x = 0; x < 32; ++x) cells[cellIndex(x, y, 0)] = stateK(1);
    cells[cellIndex(3, 3, 1)] = stateK(2);
    cells[cellIndex(4, 3, 1)] = stateK(3);
    cells[cellIndex(5, 3, 1)] = stateK(4);
    B b;
    b.fill(cells);
    CHECK(TA::width(b) == 4);  // 5 states -> 3 bits -> aligned to 4
    CHECK(TA::paletteSize(b) == 5);
    CHECK(TA::liveEntries(b) == 5);
    b.fill(Cells(kBrickCells, stateK(1)));
    CHECK(TA::isUniform(b));
    B a;
    CHECK(TA::isUniform(a));
    CHECK(meshFaces([&](std::uint32_t c) { return a.getCell(c); }).empty());  // uniform air meshes to nothing
}

int main() {
    testModularEdges();
    testWeights();
    testUniformDigest();
    testIncrementalDigest();
    testPromotion();
    testGrowthAtEveryBoundary();
    testFullWithDeadEntryGrows();
    testDeadEntryReuse();
    testThirtyTwoBitsAppendsPastCells();
    testReverseLookupThreshold();
    testWitnessUpperBound();
    testTurnEndPolicies();
    testAppendsBound();
    testCompactToSmallestAndUniform();
    testBulkFillChoosesSmallest();
    return check::result("test_brick");
}
