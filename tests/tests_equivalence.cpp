// Tests: the randomized equivalence test (v0.2.2). The same content, stored
// uniform where possible and as a palette at every width that holds it,
// through the same random edit sequence, answers probes and raycasts, digests
// and meshes identically, and agrees with a plain-array oracle.
//
// For each seed: build content with n distinct states; keep copies built by
// a bulk fill, by single edits from uniform air (promotion and every growth
// on the way), and forced to each width that holds them; apply the same
// random edits to all; after every edit compare probes and digests, and at
// checkpoints every face and corner, every cell, a fixed set of raycasts and
// the per-cell mesh (as a sorted list of quads). The edit sequences cross
// every width boundary, fill a palette with dead entries so a new state grows
// it, reuse dead entries by editing back to their states, and include
// explicit compactions down to narrower widths and to uniform.
//
// `sandbox_tests` runs a fixed set of seeds; `sandbox_tests --long` adds more
// seeds and longer edit sequences.

#include <chrono>
#include <cmath>
#include <memory>

#include "test_support.h"

namespace {

using TA = ChunkTestAccess;

struct Abort {};  // a seed's run stops at its first difference

struct Ray {
    double o[3];
    double d[3];
};

// The oracle's raycast: Amanatides–Woo from an origin inside the chunk to the
// first non-air cell, as a cell index, or −1. Independent of ChunkWorld's.
int oracleRay(const Cells& cells, const Ray& r) {
    int i[3], step[3];
    double tMax[3], tDelta[3];
    for (int a = 0; a < 3; ++a) {
        i[a] = static_cast<int>(std::floor(r.o[a]));
        if (r.d[a] > 0) {
            step[a] = 1;
            tDelta[a] = 1.0 / r.d[a];
            tMax[a] = (std::floor(r.o[a]) + 1.0 - r.o[a]) * tDelta[a];
        } else if (r.d[a] < 0) {
            step[a] = -1;
            tDelta[a] = -1.0 / r.d[a];
            tMax[a] = (r.o[a] - std::floor(r.o[a])) * tDelta[a];
        } else {
            step[a] = 0;
            tDelta[a] = INFINITY;
            tMax[a] = INFINITY;
        }
    }
    for (int n = 0; n < 256; ++n) {
        if (i[0] < 0 || i[1] < 0 || i[2] < 0 || i[0] >= 64 || i[1] >= 64 || i[2] >= 64) return -1;
        const auto c = static_cast<std::uint32_t>(brickCellIndex(i[0], i[1], i[2]));
        if (cells[c] != kAirState) return static_cast<int>(c);
        const int a = tMax[0] < tMax[1] ? (tMax[0] < tMax[2] ? 0 : 2) : (tMax[1] < tMax[2] ? 1 : 2);
        i[a] += step[a];
        tMax[a] += tDelta[a];
    }
    return -1;
}

using QuadKey = std::pair<int, std::uint64_t>;  // (array: 0 opaque, 1 translucent, 2 cutout; packed quad)

// A mesh as a sorted list of quads (opaque then translucent), so equal
// content gives equal lists whatever order the faces were emitted in.
std::vector<QuadKey> quadList(const WorldMesh& m) {
    std::vector<QuadKey> out;
    for (const std::uint64_t q : m.quads) out.push_back({0, q});
    for (const std::uint64_t q : m.translucentQuads) out.push_back({1, q});
    for (const std::uint64_t q : m.cutoutQuads) out.push_back({2, q});
    std::sort(out.begin(), out.end());
    return out;
}

struct Copy {
    std::string name;
    std::unique_ptr<ChunkWorld> world;  // reads go through the world's seam
    unsigned forcedWidth = 0;           // 0: not forced
    bool lazyDigest = false;            // digest asked for only at checkpoints
    Chunk& data() { return ChunkWorldTestAccess::chunk(*world); }
};

struct Params {
    std::uint32_t distinct;    // initial distinct states
    std::uint32_t randomEdits;
    std::uint32_t newStates;   // growth phase
};

struct Run {
    std::mt19937_64 rng;
    Cells oracle;
    std::uint64_t oracleD = 0;
    std::vector<std::unique_ptr<Copy>> copies;
    std::vector<std::uint32_t> pool;  // test states introduced so far
    std::uint32_t nextK = 0;
    std::vector<Ray> rays;
    std::uint64_t edits = 0, checkpoints = 0;
    unsigned seed;
    bool withMesh;  // compare meshes at checkpoints (the long run does it less often)
    int meshEvery;
    std::uint64_t meshCounter = 0;

    Run(unsigned s, bool mesh, int every) : rng(s), seed(s), withMesh(mesh), meshEvery(every) {}

    [[noreturn]] void fail(const char* what, const std::string& copy, std::uint32_t cell) {
        std::fprintf(stderr, "equivalence seed %u: %s differs in copy %s (cell %u, edit %llu)\n", seed, what,
                     copy.c_str(), cell, static_cast<unsigned long long>(edits));
        ++check::failures();
        throw Abort{};
    }

    Copy& add(const std::string& name, unsigned forced = 0, bool lazy = false) {
        copies.push_back(std::make_unique<Copy>());
        Copy& c = *copies.back();
        c.name = name;
        c.world = std::make_unique<ChunkWorld>(registry());
        c.forcedWidth = forced;
        c.lazyDigest = lazy;
        return c;
    }

    void build(std::uint32_t n) {
        oracle = makeContent(n, rng, pool);
        oracleD = oracleDigest(oracle);
        nextK = 1;
        for (std::uint32_t k : pool) nextK = std::max(nextK, k + 1);
        copies.clear();
        fillChunk(add("fill").data(), oracle, true);
        add("fill-lazy", 0, true).data() = copies[0]->data();
        fillChunk(add("edits").data(), oracle, false);  // promotion and every growth on the way
        for (unsigned w : kBrickWidths) {
            if (brickIndexLimit(w) < n) continue;
            Copy& c = add("w" + std::to_string(w), w);
            c.data() = copies[0]->data();
            TA::forceWidth(c.data(), w);
        }
        for (auto& c : copies)
            if (!c->lazyDigest) (void)c->data().digest();
        rays.clear();
        for (int i = 0; i < 24; ++i) {
            Ray r{};
            for (int a = 0; a < 3; ++a) {
                r.o[a] = 0.5 + static_cast<double>(rng() % 6300) / 100.0;
                r.d[a] = static_cast<double>(static_cast<int>(rng() % 2001) - 1000) / 1000.0;
            }
            if (r.d[0] == 0 && r.d[1] == 0 && r.d[2] == 0) r.d[2] = 1;
            rays.push_back(r);
        }
        checkpoint(true);
    }

    void probe(std::uint32_t cell) {
        for (auto& c : copies)
            if (!(c->world->probe(cellAt({}, localOfIndex(cell))) == oracle[cell])) fail("probe", c->name, cell);
    }

    // `probes`: also probe the cell and four random ones in every copy.
    void edit(std::uint32_t cell, const StateId& s, bool probes = true) {
        oracleD = addMod(oracleD, mulMod(digestWeight(cell), subMod(testStates().digestTerm(s),
                                                                    testStates().digestTerm(oracle[cell]))));
        oracle[cell] = s;
        ++edits;
        for (auto& c : copies) {
            c->data().set(localOfIndex(cell), s);
            if (!c->lazyDigest && c->data().digest() != oracleD) fail("digest", c->name, cell);
        }
        if (!probes) return;
        probe(cell);
        for (int i = 0; i < 4; ++i) probe(randomCell());
    }

    std::uint32_t randomCell() { return static_cast<std::uint32_t>(rng() % kBrickCells); }
    StateId newState() {
        pool.push_back(nextK);
        return testState(nextK++);
    }
    StateId poolState() { return testState(pool[rng() % pool.size()]); }

    bool allMeshable() const {
        const auto types = static_cast<BlockTypeId>(registry().defs().size());
        return std::all_of(oracle.begin(), oracle.end(), [&](const StateId& s) { return s == kAirState || testStates().type(s) < types; });
    }

    // Every face and corner, every cell, rays, digest and (when asked) mesh.
    // `everyCell`: compare every cell (else 4096 random ones: the digest already
    // vouches for the rest, and the full pass is the slow part in Debug).
    void checkpoint(bool mesh, bool everyCell = false) {
        ++checkpoints;
        for (std::uint32_t z : {0u, 63u})
            for (std::uint32_t y : {0u, 63u})
                for (std::uint32_t x : {0u, 63u}) probe(static_cast<std::uint32_t>(brickCellIndex(static_cast<int>(x), static_cast<int>(y), static_cast<int>(z))));
        for (auto& c : copies) {
            const std::uint32_t n = everyCell ? kBrickCells : 4096;
            for (std::uint32_t i = 0; i < n; ++i) {
                const std::uint32_t cell = everyCell ? i : randomCell();
                if (!(c->data().get(localOfIndex(cell)) == oracle[cell])) fail("cell", c->name, cell);
            }
        }
        const std::uint64_t full = oracleDigest(oracle);
        if (full != oracleD) fail("oracle digest", "oracle", 0);
        std::vector<int> hits;
        for (const Ray& r : rays) hits.push_back(oracleRay(oracle, r));
        const bool doMesh = withMesh && mesh && allMeshable() && (meshCounter++ % static_cast<std::uint64_t>(meshEvery) == 0);
        std::vector<QuadKey> reference;
        // A mesh per copy is the slow part: compare the bulk fill with the
        // widest copy (the last).
        for (std::size_t ci = 0; ci < copies.size(); ++ci) {
            auto& c = copies[ci];
            if (c->data().digest() != full) fail("digest (checkpoint)", c->name, 0);
            if (c->lazyDigest) TA::dropDigest(c->data());
            for (std::size_t i = 0; i < rays.size(); ++i) {
                const auto hit = c->world->raycast({rays[i].o[0], rays[i].o[1], rays[i].o[2]},
                                                   {rays[i].d[0], rays[i].d[1], rays[i].d[2]}, 400.0);
                const int cell = hit ? static_cast<int>(brickCellIndex(static_cast<int>(hit->cell.x), static_cast<int>(hit->cell.y), static_cast<int>(hit->cell.z))) : -1;
                if (cell != hits[i]) fail("raycast", c->name, static_cast<std::uint32_t>(i));
            }
            if (doMesh && (ci == 0 || ci + 1 == copies.size())) {
                std::vector<QuadKey> q = quadList(c->world->buildMesh(ChunkWorld::kChunk));
                if (ci == 0) reference = std::move(q);
                else if (q != reference) fail("mesh", c->name, 0);
            }
            if (c->forcedWidth != 0 && TA::width(c->data()) < c->forcedWidth) fail("width shrank by an edit", c->name, 0);
        }
    }

    Copy& fillCopy() { return *copies[0]; }
};

void runSeed(unsigned seed, const Params& p, bool mesh, int meshEvery, bool longRun) {
    Run r(seed, mesh, meshEvery);
    try {
        r.build(p.distinct);

        // A: random edits among states already in play.
        for (std::uint32_t i = 0; i < p.randomEdits; ++i) r.edit(r.randomCell(), r.poolState());
        r.checkpoint(true);

        // B: growth, crossing width boundaries; mixed with ordinary edits.
        const unsigned w0 = TA::width(r.fillCopy().data());
        for (std::uint32_t i = 0; i < p.newStates; ++i) {
            // New states at distinct cells (an odd stride is a bijection mod 2¹⁸), so they stay live and widths grow.
            r.edit((i * 40503u + 17u) % kBrickCells, r.newState());
            if (i % 3 == 0) r.edit(r.randomCell(), r.poolState());
            if (longRun && p.newStates > 8 && i % (p.newStates / 4 + 1) == 0) r.checkpoint(false);
        }
        r.checkpoint(true, true);
        CHECK(TA::width(r.fillCopy().data()) >= w0);

        // C: fill the fill-copy's palette, kill one entry, and check a new
        // state grows the palette (an edit reclaims nothing); then edit back
        // to the killed state and check its dead entry is reused.
        {
            Chunk& d = r.fillCopy().data();
            if (!TA::isUniform(d) && TA::width(d) < 32) {
                const std::uint32_t need = brickIndexLimit(TA::width(d)) - TA::paletteSize(d);
                if (need <= 70000) {
                    while (TA::paletteSize(d) < brickIndexLimit(TA::width(d))) r.edit(r.randomCell(), r.newState(), false);
                    const StateId victim = r.oracle[r.randomCell()];
                    StateId keep = victim;
                    while (keep == victim) keep = r.oracle[r.randomCell()];
                    for (std::uint32_t c = 0; c < kBrickCells; ++c)
                        if (r.oracle[c] == victim) r.edit(c, keep, false);
                    const unsigned w = TA::width(d);
                    const std::uint64_t grew = TA::growths(d);
                    r.edit(r.randomCell(), r.newState());
                    CHECK(TA::width(d) == 2 * w && TA::growths(d) == grew + 1);
                    const std::uint32_t size = TA::paletteSize(d);
                    r.edit(r.randomCell(), victim);  // its dead entry is reused
                    CHECK(TA::paletteSize(d) == size);
                    r.checkpoint(true);
                }
            }
        }

        // D: explicit compaction of a random half of the copies (forced-width
        // copies may narrow; they are no longer forced afterwards).
        for (auto& c : r.copies)
            if (r.rng() & 1) {
                TA::compact(c->data());
                c->forcedWidth = 0;
                const std::uint32_t live = TA::liveEntries(c->data());
                CHECK(live == 1 ? TA::isUniform(c->data()) : TA::width(c->data()) == brickCompactedWidth(live));
                CHECK(TA::paletteSize(c->data()) == live);
            }
        r.checkpoint(true);
        for (std::uint32_t i = 0; i < p.randomEdits / 2; ++i) r.edit(r.randomCell(), r.poolState());

        // E: a bulk fill of new content into every copy, forced widths re-forced.
        {
            const std::uint32_t m = 1 + static_cast<std::uint32_t>(r.rng() % 40);
            std::vector<std::uint32_t> pool;
            r.oracle = makeContent(m, r.rng, pool);
            r.oracleD = oracleDigest(r.oracle);
            for (std::uint32_t k : pool) {
                r.pool.push_back(k);
                r.nextK = std::max(r.nextK, k + 1);
            }
            for (auto& c : r.copies) {
                fillChunk(c->data(), r.oracle, true);
                CHECK(m == 1 ? TA::isUniform(c->data()) : TA::width(c->data()) == brickCompactedWidth(m));
                if (c->forcedWidth != 0 && brickIndexLimit(c->forcedWidth) >= m) TA::forceWidth(c->data(), c->forcedWidth);
                if (!c->lazyDigest) (void)c->data().digest();
            }
            r.checkpoint(true);
            for (std::uint32_t i = 0; i < p.randomEdits / 4; ++i) r.edit(r.randomCell(), r.poolState());
        }

        // F: collapse to one state by edits, compact to uniform, then promote again.
        {
            const StateId s = r.poolState();
            for (std::uint32_t c = 0; c < kBrickCells; ++c) r.edit(c, s, false);
            for (auto& c : r.copies) {
                TA::compact(c->data());
                CHECK(TA::isUniform(c->data()));
                c->forcedWidth = 0;
            }
            r.checkpoint(true);
            r.edit(r.randomCell(), r.newState());
            for (auto& c : r.copies) CHECK(TA::width(c->data()) == 1);
            for (std::uint32_t i = 0; i < 200; ++i) r.edit(r.randomCell(), r.newState());
            r.checkpoint(true, true);
        }
    } catch (const Abort&) {
        return;
    }
}

}  // namespace

void runEquivalenceTests(bool longRun) {
    // Fixed seeds: one state, a handful, a few hundred, and growth through
    // 16 → 32 bits. Meshes are compared at every fourth checkpoint.
    const Params fixed[] = {{1, 1500, 3}, {2, 1500, 20}, {3, 2000, 300}, {17, 1500, 1000}, {250, 1500, 70000}, {40, 2000, 5000}};
    unsigned seed = 1;
    for (const Params& p : fixed) runSeed(seed++, p, true, 4, longRun);
    if (longRun) {
        std::mt19937_64 rng(2026);
        for (int i = 0; i < 24; ++i) {
            const Params p{1 + static_cast<std::uint32_t>(rng() % 3000), 20000, static_cast<std::uint32_t>(rng() % 80000)};
            runSeed(100 + static_cast<unsigned>(i), p, true, 1, longRun);
        }
    }
}
