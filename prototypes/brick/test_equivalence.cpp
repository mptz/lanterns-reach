// v0.2.2's randomized equivalence test: the same content, stored uniform or
// as a palette at every width that holds it, through the same random edit
// sequence, answers probes, raycasts, digest and per-cell mesh identically,
// and agrees with a plain-array oracle.
//
// Usage: test_equivalence [--long]   (ctest runs the short form)
#include "../common/check.hpp"
#include "brick.hpp"
#include "test_support.hpp"

#include <chrono>
#include <cstring>
#include <random>
#include <string>
#include <vector>

using namespace proto;
using namespace proto::test;
using TA = BrickTestAccess;
using B = Brick<State>;

namespace {

struct Copy {
    std::string name;
    B brick;
    unsigned forcedWidth = 0;  // 0: not forced
    bool lazyDigest = false;   // digest asked for only at checkpoints
    CompactionPolicy policy{}; // discretionary compaction at every 8th edit's turn end
};

struct Run {
    std::mt19937_64 rng;
    Cells oracle;
    std::uint64_t oracleD = 0;
    std::vector<Copy> copies;
    std::vector<std::uint32_t> pool;  // states (registry k) introduced so far
    std::uint32_t nextK = 0;
    std::vector<Ray> rays;
    std::uint64_t edits = 0, checkpoints = 0, discretionary = 0;
    unsigned seed;

    explicit Run(unsigned s) : rng(s), seed(s) {}

    void fail(const char* what, const std::string& copy, std::uint32_t cell) {
        std::fprintf(stderr, "seed %u: %s differs in copy %s (cell %u, edit %llu)\n", seed, what, copy.c_str(), cell,
                     static_cast<unsigned long long>(edits));
        std::exit(1);
    }

    void build(std::uint32_t n) {
        oracle = makeContent(n, rng, pool);
        oracleD = oracleDigest(oracle);
        nextK = 1;
        for (std::uint32_t k : pool) nextK = std::max(nextK, k + 1);
        copies.clear();
        copies.push_back({"fill", B{}, 0, false});
        copies.back().brick.fill(oracle);
        copies.push_back({"fill-lazy", B{}, 0, true});
        copies.back().brick.fill(oracle);
        if (n == 1) copies.push_back({"uniform", B{oracle[0]}, 0, false});
        // Built by single edits from uniform air: promotion and every growth on the way.
        copies.push_back({"edits", B{}, 0, false});
        for (std::uint32_t c = 0; c < kBrickCells; ++c) copies.back().brick.setCell(c, oracle[c]);
        // Discretionary compaction between edits, with small thresholds so it runs often.
        {
            CompactionPolicy wit;
            wit.kind = CompactionPolicy::Kind::Witness;
            wit.floor = 16;
            wit.fraction = 0.25;
            wit.widthClause = true;
            copies.push_back({"disc-witness", B{}, 0, false, wit});
            copies.back().brick.fill(oracle);
            CompactionPolicy app;
            app.kind = CompactionPolicy::Kind::Appends;
            app.floor = 32;
            app.fraction = 0.5;
            copies.push_back({"disc-appends", B{}, 0, true, app});
            copies.back().brick.fill(oracle);
        }
        for (unsigned w : kWidths) {
            if (paletteCapacity(w) < n) continue;
            copies.push_back({"w" + std::to_string(w), B{}, w, false});
            copies.back().brick.fill(oracle);
            TA::forceWidth(copies.back().brick, w);
        }
        for (Copy& c : copies)
            if (!c.lazyDigest) (void)c.brick.digest();
        rays.clear();
        for (int i = 0; i < 24; ++i) {
            Ray r;
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
        for (Copy& c : copies)
            if (!(c.brick.getCell(cell) == oracle[cell])) fail("probe", c.name, cell);
    }

    void edit(std::uint32_t cell, const State& s) {
        oracleD = addMod(oracleD, mulMod(digestW(cell), subMod(stateHash(s), stateHash(oracle[cell]))));
        oracle[cell] = s;
        ++edits;
        for (Copy& c : copies) {
            c.brick.setCell(cell, s);
            if (edits % 8 == 0 && c.brick.turnEnd(c.policy)) ++discretionary;
            if (!c.lazyDigest && c.brick.digest() != oracleD) fail("digest", c.name, cell);
        }
        probe(cell);
        for (int i = 0; i < 4; ++i) probe(static_cast<std::uint32_t>(rng() % kBrickCells));
    }

    std::uint32_t randomCell() { return static_cast<std::uint32_t>(rng() % kBrickCells); }
    State newState() {
        pool.push_back(nextK);
        return stateK(nextK++);
    }
    State poolState() { return stateK(pool[rng() % pool.size()]); }

    // Every face and corner, every cell, rays, digest and mesh.
    void checkpoint(bool withMesh) {
        ++checkpoints;
        for (std::uint32_t z : {0u, 63u})
            for (std::uint32_t y : {0u, 63u})
                for (std::uint32_t x : {0u, 63u}) probe(cellIndex(x, y, z));
        for (Copy& c : copies)
            for (std::uint32_t cell = 0; cell < kBrickCells; ++cell)
                if (!(c.brick.getCell(cell) == oracle[cell])) fail("cell", c.name, cell);
        std::uint64_t full = oracleDigest(oracle);
        if (full != oracleD) fail("oracle digest", "oracle", 0);
        auto og = [&](std::uint32_t cell) { return oracle[cell]; };
        std::vector<Hit> hits;
        for (const Ray& r : rays) hits.push_back(raycast(og, r));
        std::vector<Face> mesh;
        if (withMesh) mesh = meshFaces(og);
        for (Copy& c : copies) {
            if (c.brick.digest() != full) fail("digest (checkpoint)", c.name, 0);
            if (c.lazyDigest) TA::dropDigest(c.brick);
            auto g = [&](std::uint32_t cell) { return c.brick.getCell(cell); };
            for (std::size_t i = 0; i < rays.size(); ++i)
                if (!(raycast(g, rays[i]) == hits[i])) fail("raycast", c.name, 0);
            if (withMesh && meshFaces(g) != mesh) fail("mesh", c.name, 0);
            if (c.forcedWidth != 0 && TA::width(c.brick) < c.forcedWidth) fail("width shrank by edit", c.name, 0);
        }
    }

    Copy& fillCopy() { return copies[0]; }
};

struct Params {
    std::uint32_t distinct;   // initial distinct states
    std::uint32_t randomEdits;
    std::uint32_t newStates;  // growth phase
};

void runSeed(unsigned seed, const Params& p) {
    Run r(seed);
    r.build(p.distinct);

    // A: random edits among states already in play.
    for (std::uint32_t i = 0; i < p.randomEdits; ++i) r.edit(r.randomCell(), r.poolState());
    r.checkpoint(true);

    // B: growth, crossing width boundaries; mixed with ordinary edits.
    unsigned w0 = TA::width(r.fillCopy().brick);
    unsigned maxW = 0;
    for (std::uint32_t i = 0; i < p.newStates; ++i) {
        // New states at distinct cells (an odd stride is a bijection mod 2^18), so they stay live and widths grow.
        r.edit((i * 40503u + 17u) % kBrickCells, r.newState());
        maxW = std::max(maxW, TA::width(r.fillCopy().brick));
        if (i % 3 == 0) r.edit(r.randomCell(), r.poolState());
        if (p.newStates > 8 && i % (p.newStates / 4 + 1) == 0) r.checkpoint(false);
    }
    r.checkpoint(true);
    CHECK(TA::width(r.fillCopy().brick) >= w0);

    int phaseC = -1;  // width at which the dead-entry growth was exercised
    // C: fill the fill-copy's palette, kill one entry, and check a new state
    // grows the palette (nothing reclaimed by an edit); then edit back to the
    // killed state and check its dead entry is reused.
    {
        B& b = r.fillCopy().brick;
        if (!TA::isUniform(b) && TA::width(b) < 32) {
            std::uint32_t cap = paletteCapacity(TA::width(b));
            std::uint32_t need = cap - TA::paletteSize(b);
            if (need <= 70000) {
                while (!TA::paletteFull(b)) r.edit(r.randomCell(), r.newState());
                State victim = r.oracle[r.randomCell()];
                State keep = victim;
                while (keep == victim) keep = r.oracle[r.randomCell()];
                for (std::uint32_t c = 0; c < kBrickCells; ++c)
                    if (r.oracle[c] == victim) r.edit(c, keep);
                unsigned w = TA::width(b);
                auto before = TA::stats(b);
                r.edit(r.randomCell(), r.newState());
                CHECK(TA::width(b) == 2 * w);
                CHECK(TA::stats(b).growths == before.growths + 1);
                std::uint32_t size = TA::paletteSize(b);
                r.edit(r.randomCell(), victim);  // its dead entry is reused
                CHECK(TA::paletteSize(b) == size);
                phaseC = static_cast<int>(w);
                r.checkpoint(true);
            }
        }
    }

    // D: explicit compaction of a random half of the copies (forced-width
    // copies may narrow; they are no longer forced afterwards).
    for (Copy& c : r.copies)
        if (r.rng() & 1) {
            TA::compact(c.brick);
            c.forcedWidth = 0;
            std::uint32_t live = TA::liveEntries(c.brick);
            CHECK(live == 1 ? TA::isUniform(c.brick) : TA::width(c.brick) == smallestWidth(live));
        }
    r.checkpoint(true);
    for (std::uint32_t i = 0; i < p.randomEdits / 2; ++i) r.edit(r.randomCell(), r.poolState());

    // E: a bulk fill of new content into every copy, forced widths re-forced.
    {
        std::uint32_t m = 1 + static_cast<std::uint32_t>(r.rng() % 40);
        std::vector<std::uint32_t> pool;
        r.oracle = makeContent(m, r.rng, pool);
        r.oracleD = oracleDigest(r.oracle);
        for (std::uint32_t k : pool) {
            r.pool.push_back(k);
            r.nextK = std::max(r.nextK, k + 1);
        }
        for (Copy& c : r.copies) {
            c.brick.fill(r.oracle);
            if (m == 1) CHECK(TA::isUniform(c.brick));
            else CHECK(TA::width(c.brick) == smallestWidth(m));
            if (c.forcedWidth != 0 && paletteCapacity(c.forcedWidth) >= m) TA::forceWidth(c.brick, c.forcedWidth);
            if (!c.lazyDigest) (void)c.brick.digest();
        }
        r.checkpoint(true);
        for (std::uint32_t i = 0; i < p.randomEdits / 4; ++i) r.edit(r.randomCell(), r.poolState());
    }

    // F: collapse to one state by edits, compact to uniform, then promote again.
    {
        State s = r.poolState();
        for (std::uint32_t c = 0; c < kBrickCells; ++c) r.edit(c, s);
        for (Copy& c : r.copies) {
            TA::compact(c.brick);
            CHECK(TA::isUniform(c.brick));
            c.forcedWidth = 0;
        }
        r.checkpoint(true);
        r.edit(r.randomCell(), r.newState());
        for (Copy& c : r.copies) CHECK(TA::width(c.brick) == 1);
        for (std::uint32_t i = 0; i < 200; ++i) r.edit(r.randomCell(), r.newState());
        r.checkpoint(true);
    }
    std::printf("seed %u: %zu copies, %llu edits, %llu checkpoints, %llu discretionary compactions, grew to width %u, dead-entry growth at width %d, ok\n",
                seed, r.copies.size(), static_cast<unsigned long long>(r.edits),
                static_cast<unsigned long long>(r.checkpoints), static_cast<unsigned long long>(r.discretionary), maxW, phaseC);
    CHECK(r.discretionary > 0);
}

}  // namespace

int main(int argc, char** argv) {
    bool longRun = argc > 1 && std::strcmp(argv[1], "--long") == 0;
    auto t0 = std::chrono::steady_clock::now();
    // Fixed seeds: one state, a handful, a few hundred, and one growth run
    // through 16 -> 32 bits.
    const Params shortSet[] = {{1, 2000, 3}, {2, 2000, 20}, {3, 3000, 300}, {17, 2000, 1000},
                               {250, 2000, 80000}, {40, 3000, 5000}};
    unsigned seed = 1;
    for (const Params& p : shortSet) runSeed(seed++, p);
    if (longRun) {
        std::mt19937_64 rng(2026);
        for (int i = 0; i < 24; ++i) {
            Params p{1 + static_cast<std::uint32_t>(rng() % 3000), 20000, static_cast<std::uint32_t>(rng() % 80000)};
            runSeed(100 + static_cast<unsigned>(i), p);
        }
    }
    double s = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
    std::printf("elapsed %.1f s\n", s);
    return check::result("test_equivalence");
}
