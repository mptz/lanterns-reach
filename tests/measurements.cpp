// Measurements (not tests): `sandbox_tests --time-mesh` and `--time-chunk`.
// Run them on a Release build; they print medians, and results vary by about
// ±30% run to run. versions.md lists what each release measures.

#include <chrono>

#include "test_support.h"

// `sandbox_tests --time-mesh`: the v0.2.1 measurement. Time to mesh the full
// 64³ chunk with the per-cell rule: the median of seven runs, for a chunk of
// solid stone (only the shell has faces), a 3-D checkerboard of stone and air
// (every solid cell has six faces: the most faces a chunk can have), and the
// v0.1 scene. Run it on a Release build.
void timeChunkMesh() {
    auto timeIt = [](ChunkWorld& w, std::size_t& faces) {
        std::vector<double> ms;
        for (int i = 0; i < 7; ++i) {
            const auto t0 = std::chrono::steady_clock::now();
            const WorldMesh m = w.buildMesh(ChunkWorld::kChunk);
            const auto t1 = std::chrono::steady_clock::now();
            faces = m.indices.size() / 6;
            ms.push_back(std::chrono::duration<double, std::milli>(t1 - t0).count());
        }
        std::sort(ms.begin(), ms.end());
        return ms[ms.size() / 2];
    };
    ChunkWorld solid(registry()), checker(registry());
    EditSet all, alt;
    for (int z = 0; z < kChunkSize; ++z)
        for (int y = 0; y < kChunkSize; ++y)
            for (int x = 0; x < kChunkSize; ++x) {
                all.edits.push_back({{x, y, z}, solidState()});
                if ((x + y + z) % 2 == 0) alt.edits.push_back({{x, y, z}, solidState()});
            }
    solid.apply(all);
    checker.apply(alt);
    std::size_t faces = 0;
    double ms = timeIt(solid, faces);
    std::printf("mesh 64^3 chunk, solid stone:   %8.2f ms, %zu faces\n", ms, faces);
    ms = timeIt(checker, faces);
    std::printf("mesh 64^3 chunk, checkerboard:  %8.2f ms, %zu faces\n", ms, faces);
    std::string err;
    const auto scene = loadScenario(SANDBOX_SCENARIO_DIR "/v0.1-scene.json", err);
    if (scene) {
        ChunkWorld w(registry());
        std::vector<std::string> warnings;
        w.apply(sceneEdits(*scene, registry(), &warnings));
        ms = timeIt(w, faces);
        std::printf("mesh 64^3 chunk, v0.1 scene:    %8.2f ms, %zu faces\n", ms, faces);
    }
}

// `sandbox_tests --time-chunk` (versions.md v0.2.2, measurements): run on a
// Release build. Medians; nanoseconds per edit, milliseconds per operation.
namespace {

using TA = ChunkTestAccess;
using Clock = std::chrono::steady_clock;

double msSince(Clock::time_point t0) { return std::chrono::duration<double, std::milli>(Clock::now() - t0).count(); }

double median(std::vector<double> v) {
    std::sort(v.begin(), v.end());
    return v[v.size() / 2];
}

// A chunk at width w holding `entries` distinct live states (air first).
ChunkData chunkWith(std::uint32_t entries, unsigned w) {
    ChunkData d;
    for (std::uint32_t k = 1; k < entries; ++k) d.set(localOfIndex(k * 3u % kBrickCells), testState(k));
    // Every state live (2 cells each is not needed: one cell each, air elsewhere).
    if (w != 0 && ChunkTestAccess::width(d) != w) ChunkTestAccess::forceWidth(d, w);
    return d;
}

}  // namespace

void timeChunkRepresentations() {
    std::mt19937_64 rng(42);
    std::printf("Per-edit cost (random cell, random existing state; ns per set), by width and palette entries:\n");
    std::printf("  %-6s %8s %-8s %10s\n", "width", "entries", "lookup", "ns/edit");
    struct Row { unsigned w; std::uint32_t entries; };
    const Row rows[] = {{1, 2}, {2, 4}, {4, 16}, {8, 32}, {8, 33}, {8, 200}, {16, 32}, {16, 33}, {16, 200}, {16, 5000},
                        {16, 65000}, {32, 32}, {32, 200}, {32, 5000}, {32, 70000}};
    for (const Row& r : rows) {
        ChunkData d = chunkWith(r.entries, r.w);
        if (TA::width(d) != r.w || TA::paletteSize(d) != r.entries) {
            std::printf("  (setup failed: width %u entries %u)\n", r.w, r.entries);
            continue;
        }
        constexpr std::uint32_t kEdits = 400000;
        std::vector<std::pair<glm::ivec3, BlockState>> work(kEdits);
        for (auto& wk : work) wk = {localOfIndex(static_cast<std::uint32_t>(rng() % kBrickCells)), testState(static_cast<std::uint32_t>(rng() % r.entries))};
        std::vector<double> ns;
        for (int rep = 0; rep < 5; ++rep) {
            ChunkData c = d;
            const auto t0 = Clock::now();
            for (const auto& [cell, state] : work) c.set(cell, state);
            ns.push_back(msSince(t0) * 1e6 / kEdits);
        }
        std::printf("  %-6u %8u %-8s %10.1f\n", r.w, r.entries, r.entries > kReverseScanMax ? "table" : "scan", median(ns));
    }

    std::printf("\nScan against table, width 8 (ns per edit; the threshold is the crossover):\n");
    std::printf("  %8s %10s %10s\n", "entries", "scan", "table");
    for (std::uint32_t entries : {2u, 4u, 8u, 12u, 16u, 20u, 24u, 32u, 48u, 64u}) {
        ChunkData d = chunkWith(entries, 8);
        constexpr std::uint32_t kEdits = 400000;
        std::vector<std::pair<glm::ivec3, BlockState>> work(kEdits);
        for (auto& wk : work) wk = {localOfIndex(static_cast<std::uint32_t>(rng() % kBrickCells)), testState(static_cast<std::uint32_t>(rng() % entries))};
        double result[2] = {0, 0};
        for (int table = 0; table < 2; ++table) {
            std::vector<double> ns;
            for (int rep = 0; rep < 5; ++rep) {
                ChunkData c = d;
                TA::setScanMax(c, table ? 0 : 1000);
                const auto t0 = Clock::now();
                for (const auto& [cell, state] : work) c.set(cell, state);
                ns.push_back(msSince(t0) * 1e6 / kEdits);
            }
            result[table] = median(ns);
        }
        std::printf("  %8u %10.1f %10.1f\n", entries, result[0], result[1]);
    }

    std::printf("\nGrowth (the one edit that repacks a full palette to the next width), ms:\n");
    for (unsigned w : {1u, 2u, 4u, 8u, 16u}) {
        const std::uint32_t n = std::uint32_t{1} << w;
        ChunkData d = chunkWith(n, w);
        std::vector<double> ms;
        for (int rep = 0; rep < 7; ++rep) {
            ChunkData c = d;
            const auto t0 = Clock::now();
            c.set({1, 1, 1}, testState(n + 5));
            ms.push_back(msSince(t0));
            if (TA::width(c) != 2 * w) std::printf("  (no growth at %u)\n", w);
        }
        std::printf("  %2u -> %2u bits (%5u entries): %7.3f ms\n", w, 2 * w, n, median(ms));
    }

    std::printf("\nCapacity doubling within a width (a full palette below its width's limit), ms:\n");
    for (const Row& r : {Row{8, 32}, Row{8, 128}, Row{16, 1024}, Row{16, 32768}, Row{32, 131072}}) {
        ChunkData d = chunkWith(r.entries, r.w);
        std::vector<double> ms;
        for (int rep = 0; rep < 7; ++rep) {
            ChunkData c = d;
            const auto t0 = Clock::now();
            c.set({1, 1, 1}, testState(r.entries + 5));
            ms.push_back(msSince(t0));
            if (TA::width(c) != r.w || TA::paletteCapacity(c) != 2 * r.entries) std::printf("  (unexpected result at %u)\n", r.entries);
        }
        std::printf("  width %2u, %6u -> %6u entries: %7.3f ms\n", r.w, r.entries, 2 * r.entries, median(ms));
    }

    std::printf("\nExplicit compaction, ms (dead entries dropped, narrowest width):\n");
    for (const Row& r : {Row{2, 3}, Row{4, 14}, Row{8, 200}, Row{16, 5000}, Row{16, 50000}, Row{32, 70000}}) {
        ChunkData d = chunkWith(r.entries, r.w);
        std::vector<double> ms;
        for (int rep = 0; rep < 5; ++rep) {
            ChunkData c = d;
            const auto t0 = Clock::now();
            TA::compact(c);
            ms.push_back(msSince(t0));
        }
        ChunkData c = d;
        TA::compact(c);
        std::printf("  width %2u, %6u entries -> width %2u: %7.3f ms\n", r.w, r.entries, TA::width(c), median(ms));
    }

    std::printf("\nFull digest of one chunk, ms:\n");
    {
        ChunkData d = chunkWith(40, 8);
        std::vector<double> ms;
        for (int rep = 0; rep < 9; ++rep) {
            TA::dropDigest(d);
            const auto t0 = Clock::now();
            (void)d.digest();
            ms.push_back(msSince(t0));
        }
        std::printf("  palette chunk, W computed per cell:            %7.3f ms\n", median(ms));
        // The alternative: a precomputed 2 MiB W table. Measured against the
        // cost of the weights alone (the digest also walks the indices).
        std::vector<std::uint64_t> table(kBrickCells);
        for (std::uint32_t c = 0; c < kBrickCells; ++c) table[c] = digestWeight(c);
        std::vector<double> wCompute, wTable;
        std::uint64_t sink = 0;
        for (int rep = 0; rep < 9; ++rep) {
            auto t0 = Clock::now();
            std::uint64_t acc = 0;
            for (std::uint32_t c = 0; c < kBrickCells; ++c) acc = addMod(acc, digestWeight(c));
            wCompute.push_back(msSince(t0));
            sink ^= acc;
            t0 = Clock::now();
            acc = 0;
            for (std::uint32_t c = 0; c < kBrickCells; ++c) acc = addMod(acc, table[c]);
            wTable.push_back(msSince(t0));
            sink ^= acc;
        }
        std::printf("  weights alone, computed (262144 × W):          %7.3f ms\n", median(wCompute));
        std::printf("  weights alone, from a 2 MiB table (sweep):     %7.3f ms   (sink %llu)\n", median(wTable),
                    static_cast<unsigned long long>(sink & 1));
        ChunkData u;
        TA::dropDigest(u);
        const auto t0 = Clock::now();
        (void)u.digest();
        std::printf("  uniform chunk (H(s) · K, K cached):             %7.3f ms\n", msSince(t0));
        const auto t1 = Clock::now();
        std::uint64_t k = 0;
        for (std::uint32_t c = 0; c < kBrickCells; ++c) k = addMod(k, digestWeight(c));
        std::printf("  K itself, once per process:                     %7.3f ms   (%llu)\n", msSince(t1),
                    static_cast<unsigned long long>(k & 1));
    }

    std::printf("\nMemory of a chunk at each width (KiB): indices + palette + reverse table:\n");
    for (const Row& r : {Row{1, 2}, Row{2, 4}, Row{4, 16}, Row{8, 32}, Row{8, 256}, Row{16, 300}, Row{16, 65536}, Row{32, 70000}}) {
        ChunkData d = chunkWith(r.entries, r.w);
        if (TA::width(d) != r.w) {
            std::printf("  (setup failed: width %u entries %u)\n", r.w, r.entries);
            continue;
        }
        d.set({0, 0, 0}, testState(1));  // a lookup, so the table is built if the palette is above the threshold
        d.set({0, 0, 0}, BlockState::air());
        std::printf("  width %2u, %6u entries: %8.1f + %8.1f + %8.1f = %8.1f\n", r.w, r.entries,
                    static_cast<double>(TA::indexBytes(d)) / 1024, static_cast<double>(TA::paletteBytes(d)) / 1024,
                    static_cast<double>(TA::tableBytes(d)) / 1024,
                    static_cast<double>(TA::indexBytes(d) + TA::paletteBytes(d) + TA::tableBytes(d)) / 1024);
    }
    {
        ChunkData u;
        std::printf("  uniform: %zu bytes\n", TA::paletteBytes(u));
    }

    std::printf("\nThe v0.1 scene chunk after its bulk fill:\n");
    {
        std::string err;
        const auto scene = loadScenario(SANDBOX_SCENARIO_DIR "/v0.1-scene.json", err);
        if (scene) {
            ChunkWorld w(registry());
            std::vector<std::string> warnings;
            w.applyBulk(sceneEdits(*scene, registry(), &warnings));
            const ChunkData& d = ChunkWorldTestAccess::chunk(w);
            std::printf("  width %u, palette %u, live %u\n", TA::width(d), TA::paletteSize(d), TA::liveEntries(d));
        }
    }
}
