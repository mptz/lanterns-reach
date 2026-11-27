// Tests: the content digest (data/content-digest). Registered through
// sandbox_tests.cpp.

#include "test_support.h"

namespace {

using TA = ChunkTestAccess;

void testModularEdges() {
    constexpr std::uint64_t p = kDigestP;
    CHECK(reduceMod(p) == 0 && reduceMod(p - 1) == p - 1 && reduceMod(p + 1) == 1);
    CHECK(reduceMod(~std::uint64_t{0}) == (~std::uint64_t{0}) % p);
    CHECK(addMod(p - 1, 1) == 0 && addMod(p - 1, p - 1) == p - 2);
    CHECK(subMod(0, 1) == p - 1 && subMod(0, p - 1) == 1 && subMod(5, 5) == 0);
    CHECK(mulMod(p - 1, p - 1) == 1);  // (−1)²
    CHECK(mulMod(p - 1, 2) == p - 2 && mulMod(p - 2, p - 2) == 4 && mulMod(0, p - 1) == 0);
    std::mt19937_64 rng(7);
    for (int i = 0; i < 100000; ++i) {
        std::uint64_t a = rng() % p, b = rng() % p;
        if (i % 4 == 0) a = p - 1 - (rng() % 16);  // near p
        if (i % 8 == 0) b = p - 1 - (rng() % 16);
        const unsigned __int128 z = static_cast<unsigned __int128>(a) * b;
        CHECK(mulMod(a, b) == static_cast<std::uint64_t>(z % p));
        CHECK(addMod(a, b) == (a + b) % p);
        CHECK(subMod(a, b) == (a + p - b) % p);
    }
}

// W is nonzero for every one of the 2¹⁸ cells, defined on coordinates (the
// layout index converts back exactly), and K is their sum.
void testWeights() {
    std::uint64_t k = 0;
    int zeros = 0;
    bool onCoordinates = true;
    for (std::uint32_t c = 0; c < kBrickCells; ++c) {
        const std::uint64_t w = digestWeight(c);
        zeros += w == 0 || w >= kDigestP;
        onCoordinates = onCoordinates && w == digestWeightAt(c & 63u, (c >> 6) & 63u, c >> 12);
        k = addMod(k, w);
    }
    CHECK(zeros == 0 && onCoordinates);
    CHECK(k == digestK() && k == kDigestK);  // the literal is the sum
    // Pinned values: a change to W or H is a visible decision (digests are
    // compared across processes later).
    CHECK(digestWeightAt(0, 0, 0) == 1 + mix64(0) % (kDigestP - 1));
    CHECK(mix64(0) == 0xE220A8397B1DCDAFull);
}

void testUniformDigest() {
    for (std::uint32_t k : {0u, 1u, 77u, 300u}) {
        const BlockState s = testState(k);
        Chunk u;
        u.set({0, 0, 0}, s);
        Cells cells(kBrickCells, BlockState::air());
        cells[0] = s;
        CHECK(u.digest() == oracleDigest(cells));
        // A chunk filled with one state: H(s) · K, whatever the representation.
        Cells all(kBrickCells, s);
        Chunk f;
        fillChunk(f, all, true);
        CHECK((k == 0 || TA::isUniform(f)) && f.digest() == mulMod(digestStateHash(canonicalEncoding(s)), digestK()));
        CHECK(f.digest() == oracleDigest(all));
        for (unsigned w : kBrickWidths) {  // a forced palette of one entry
            Chunk p = f;
            TA::dropDigest(p);
            TA::forceWidth(p, w);
            CHECK(TA::width(p) == w);
            CHECK(p.digest() == f.digest());
        }
    }
}

void testIncrementalDigest() {
    std::mt19937_64 rng(11);
    std::vector<std::uint32_t> pool;
    Cells cells = makeContent(40, rng, pool);
    Chunk d;
    fillChunk(d, cells, true);
    (void)d.digest();
    for (int i = 0; i < 20000; ++i) {
        const auto c = static_cast<std::uint32_t>(rng() % kBrickCells);
        const BlockState s = testState(static_cast<std::uint32_t>(rng() % 200));
        d.set(localOfIndex(c), s);
        cells[c] = s;
    }
    CHECK(TA::digestComputed(d));
    const std::uint64_t incremental = d.digest();
    CHECK(incremental == oracleDigest(cells));
    TA::dropDigest(d);
    CHECK(d.digest() == incremental);  // the full pass agrees
    // Representation and width changes carry the digest over unchanged.
    const std::uint32_t live = TA::liveEntries(d);
    for (unsigned w : kBrickWidths) {
        if (brickIndexLimit(w) < live) continue;
        TA::forceWidth(d, w);
        CHECK(TA::digestComputed(d) && d.digest() == incremental);
        TA::dropDigest(d);
        CHECK(d.digest() == incremental);
    }
    TA::compact(d);
    CHECK(d.digest() == incremental);
}

}  // namespace

void runDigestTests() {
    testModularEdges();
    testWeights();
    testUniformDigest();
    testIncrementalDigest();
}
