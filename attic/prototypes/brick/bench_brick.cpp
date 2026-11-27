// v0.2.2's measurements, on the prototype (build Release), under brick.md's
// growth rule (no compaction on edits) and the reverse lookup's scan/table.
#include "brick.hpp"
#include "test_support.hpp"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <random>
#include <vector>

using namespace proto;
using namespace proto::test;
using TA = BrickTestAccess;
using B = Brick<State>;
using Clock = std::chrono::steady_clock;

static double nsSince(Clock::time_point t0) {
    return std::chrono::duration<double, std::nano>(Clock::now() - t0).count();
}

static Cells distinctContent(std::uint32_t n) {
    Cells cells(kBrickCells);
    for (std::uint32_t c = 0; c < kBrickCells; ++c) cells[c] = stateK(1 + c % n);
    return cells;
}

static double median(std::vector<double> v) {
    std::sort(v.begin(), v.end());
    return v[v.size() / 2];
}

static volatile std::uint64_t gSink;

// Random edits to states already in the palette, ns/edit.
static double editCost(B& b, std::uint32_t n, std::mt19937_64& rng, int kEdits = 2000000) {
    std::vector<std::uint32_t> cs(kEdits);
    std::vector<State> ss(kEdits);
    for (int i = 0; i < kEdits; ++i) {
        cs[i] = static_cast<std::uint32_t>(rng() % kBrickCells);
        ss[i] = stateK(1 + static_cast<std::uint32_t>(rng() % n));
    }
    auto t0 = Clock::now();
    for (int i = 0; i < kEdits; ++i) b.setCell(cs[i], ss[i]);
    return nsSince(t0) / kEdits;
}

int main() {
    std::mt19937_64 rng(1);
    const std::uint32_t scanDefault = gReverseScanMax;
    std::printf("== per-edit cost (random cell, state already in the palette), ns/edit; scan threshold %u\n",
                scanDefault);
    std::printf("| width | entries | no digest, no witness | no digest, block witness | no digest, block+faces witness | digest | get |\n|---:|---:|---:|---:|---:|---:|---:|\n");
    for (unsigned w : kWidths) {
        std::uint32_t n = w == 32 ? 100000 : paletteCapacity(w);
        B b;
        b.fill(distinctContent(n));
        TA::forceWidth(b, w);
        gWitnessMode = 0;
        double bare = editCost(b, n, rng);
        gWitnessMode = 1;
        double line = editCost(b, n, rng);
        gWitnessMode = 2;
        double plain = editCost(b, n, rng);
        (void)b.digest();
        double dig = editCost(b, n, rng);
        std::vector<std::uint32_t> cs(2000000);
        for (auto& c : cs) c = static_cast<std::uint32_t>(rng() % kBrickCells);
        auto t0 = Clock::now();
        std::uint64_t acc = 0;
        for (std::uint32_t c : cs) acc += b.getCell(c).type;
        double get = nsSince(t0) / static_cast<double>(cs.size());
        gSink = acc;
        if (TA::width(b) != w) std::printf("(width changed!)\n");
        std::printf("| %u | %u | %.1f | %.1f | %.1f | %.1f | %.1f |\n", w, n, bare, line, plain, dig, get);
    }

    std::printf("\n== per-edit cost at 8 bits by palette size: scan (threshold above n) against table (threshold 0), ns/edit\n");
    std::printf("(the prototype's scan is scalar over a 4-byte struct; see bench_lookup for the SIMD scan over state ids)\n");
    std::printf("| entries | scan | table |\n|---:|---:|---:|\n");
    for (std::uint32_t n : {8u, 16u, 32u, 64u, 128u, 256u}) {
        B b;
        b.fill(distinctContent(n));
        TA::forceWidth(b, 8);
        gReverseScanMax = 1u << 20;
        double scan = editCost(b, n, rng, 1000000);
        gReverseScanMax = 0;
        B t;
        t.fill(distinctContent(n));
        TA::forceWidth(t, 8);
        double table = editCost(t, n, rng, 1000000);
        std::printf("| %u | %.1f | %.1f |\n", n, scan, table);
    }
    gReverseScanMax = scanDefault;

    std::printf("\n== growth: the edit that repacks w -> 2w, us, median of 15\n");
    std::printf("| growth | us |\n|---|---:|\n");
    for (unsigned w : {1u, 2u, 4u, 8u, 16u}) {
        std::uint32_t n = std::uint32_t{1} << w;
        Cells cells = distinctContent(n);
        std::vector<double> ts;
        for (int rep = 0; rep < 15; ++rep) {
            B b;
            b.fill(cells);
            auto t0 = Clock::now();
            b.setCell(0, stateK(1 + n));
            ts.push_back(nsSince(t0) / 1000.0);
            if (TA::width(b) != 2 * w) std::printf("(no growth!)\n");
        }
        std::printf("| %u -> %u | %.1f |\n", w, 2 * w, median(ts));
    }

    std::printf("\n== compaction (one cell pass to export, mark, remap, build), us, median of 9\n");
    std::printf("| from | live | to | us |\n|---:|---:|---:|---:|\n");
    struct Case {
        unsigned from;
        std::uint32_t live;
    };
    for (Case k : {Case{32, 2}, Case{32, 16}, Case{32, 256}, Case{32, 4096}, Case{8, 200}, Case{16, 200}, Case{16, 3000},
                   Case{32, 70000}, Case{32, kBrickCells}}) {
        Cells cells = distinctContent(k.live);
        std::vector<double> ts;
        unsigned to = 0;
        for (int rep = 0; rep < 9; ++rep) {
            B b;
            b.fill(cells);
            TA::forceWidth(b, k.from);
            auto t0 = Clock::now();
            TA::compact(b);
            ts.push_back(nsSince(t0) / 1000.0);
            to = TA::width(b);
        }
        std::printf("| %u | %u | %u | %.1f |\n", k.from, k.live, to, median(ts));
    }

    std::printf("\n== full digest (one chunk), us, median of 15\n");
    {
        std::vector<double> ts;
        for (int rep = 0; rep < 15; ++rep) {
            B u(stateK(5));
            auto t0 = Clock::now();
            gSink = u.digest();
            ts.push_back(nsSince(t0) / 1000.0);
        }
        std::printf("uniform: %8.3f us\n", median(ts));
    }
    for (unsigned w : kWidths) {
        std::uint32_t n = std::min<std::uint32_t>(paletteCapacity(w), 100000);
        B b;
        b.fill(distinctContent(n));
        TA::forceWidth(b, w);
        std::vector<double> ts;
        for (int rep = 0; rep < 15; ++rep) {
            TA::dropDigest(b);
            auto t0 = Clock::now();
            gSink = b.digest();
            ts.push_back(nsSince(t0) / 1000.0);
        }
        std::printf("w=%2u (%6u entries): %8.1f us\n", w, n, median(ts));
    }

    std::printf("\n== memory per width (indices + palette + reverse table once an edit has built it), bytes; entry = %zu bytes\n",
                sizeof(State));
    std::printf("| width | entries | indices | palette | table | total |\n|---:|---:|---:|---:|---:|---:|\n");
    for (unsigned w : kWidths) {
        std::uint32_t n = w == 32 ? kBrickCells : paletteCapacity(w);
        B b;
        b.fill(distinctContent(n));
        TA::forceWidth(b, w);
        b.setCell(0, stateK(1));  // a lookup: builds the table above the threshold
        b.setCell(0, stateK(2));
        std::size_t idx = static_cast<std::size_t>(kBrickCells) * w / 8;
        std::size_t table = TA::lookupBytes(b);
        std::printf("| %u | %u | %zu | %zu | %zu | %zu |\n", w, n, idx, b.memoryBytes() - idx - table, table,
                    b.memoryBytes());
    }
    return 0;
}
