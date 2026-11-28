// Measurements (not tests): `sandbox_tests --time-mesh` and `--time-chunk`.
// Run them on a Release build; they print medians, and results vary by about
// ±30% run to run. versions.md lists what each release measures.

#include <chrono>
#include <cmath>
#include <map>
#include <tuple>

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
            faces = m.quads.size();
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
Chunk chunkWith(std::uint32_t entries, unsigned w) {
    Chunk d(testStates());
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
        Chunk d = chunkWith(r.entries, r.w);
        if (TA::width(d) != r.w || TA::paletteSize(d) != r.entries) {
            std::printf("  (setup failed: width %u entries %u)\n", r.w, r.entries);
            continue;
        }
        constexpr std::uint32_t kEdits = 400000;
        std::vector<std::pair<glm::ivec3, StateId>> work(kEdits);
        for (auto& wk : work) wk = {localOfIndex(static_cast<std::uint32_t>(rng() % kBrickCells)), testState(static_cast<std::uint32_t>(rng() % r.entries))};
        std::vector<double> ns;
        for (int rep = 0; rep < 5; ++rep) {
            Chunk c = d;
            const auto t0 = Clock::now();
            for (const auto& [cell, state] : work) c.set(cell, state);
            ns.push_back(msSince(t0) * 1e6 / kEdits);
        }
        std::printf("  %-6u %8u %-8s %10.1f\n", r.w, r.entries, r.entries > kReverseScanMax ? "table" : "scan", median(ns));
    }

    std::printf("\nScan against table, width 8 (ns per edit; the threshold is the crossover):\n");
    std::printf("  %8s %10s %10s\n", "entries", "scan", "table");
    for (std::uint32_t entries : {2u, 4u, 8u, 12u, 16u, 20u, 24u, 32u, 48u, 64u}) {
        Chunk d = chunkWith(entries, 8);
        constexpr std::uint32_t kEdits = 400000;
        std::vector<std::pair<glm::ivec3, StateId>> work(kEdits);
        for (auto& wk : work) wk = {localOfIndex(static_cast<std::uint32_t>(rng() % kBrickCells)), testState(static_cast<std::uint32_t>(rng() % entries))};
        double result[2] = {0, 0};
        for (int table = 0; table < 2; ++table) {
            std::vector<double> ns;
            for (int rep = 0; rep < 5; ++rep) {
                Chunk c = d;
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
        Chunk d = chunkWith(n, w);
        std::vector<double> ms;
        for (int rep = 0; rep < 7; ++rep) {
            Chunk c = d;
            const auto t0 = Clock::now();
            c.set({1, 1, 1}, testState(n + 5));
            ms.push_back(msSince(t0));
            if (TA::width(c) != 2 * w) std::printf("  (no growth at %u)\n", w);
        }
        std::printf("  %2u -> %2u bits (%5u entries): %7.3f ms\n", w, 2 * w, n, median(ms));
    }

    std::printf("\nCapacity doubling within a width (a full palette below its width's limit), ms:\n");
    for (const Row& r : {Row{8, 32}, Row{8, 128}, Row{16, 1024}, Row{16, 32768}, Row{32, 131072}}) {
        Chunk d = chunkWith(r.entries, r.w);
        std::vector<double> ms;
        for (int rep = 0; rep < 7; ++rep) {
            Chunk c = d;
            const auto t0 = Clock::now();
            c.set({1, 1, 1}, testState(r.entries + 5));
            ms.push_back(msSince(t0));
            if (TA::width(c) != r.w || TA::paletteCapacity(c) != 2 * r.entries) std::printf("  (unexpected result at %u)\n", r.entries);
        }
        std::printf("  width %2u, %6u -> %6u entries: %7.3f ms\n", r.w, r.entries, 2 * r.entries, median(ms));
    }

    std::printf("\nExplicit compaction, ms (dead entries dropped, narrowest width):\n");
    for (const Row& r : {Row{2, 3}, Row{4, 14}, Row{8, 200}, Row{16, 5000}, Row{16, 50000}, Row{32, 70000}}) {
        Chunk d = chunkWith(r.entries, r.w);
        std::vector<double> ms;
        for (int rep = 0; rep < 5; ++rep) {
            Chunk c = d;
            const auto t0 = Clock::now();
            TA::compact(c);
            ms.push_back(msSince(t0));
        }
        Chunk c = d;
        TA::compact(c);
        std::printf("  width %2u, %6u entries -> width %2u: %7.3f ms\n", r.w, r.entries, TA::width(c), median(ms));
    }

    std::printf("\nFull digest of one chunk, ms:\n");
    {
        Chunk d = chunkWith(40, 8);
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
        Chunk u(testStates());
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
        Chunk d = chunkWith(r.entries, r.w);
        if (TA::width(d) != r.w) {
            std::printf("  (setup failed: width %u entries %u)\n", r.w, r.entries);
            continue;
        }
        d.set({0, 0, 0}, testState(1));  // a lookup, so the table is built if the palette is above the threshold
        d.set({0, 0, 0}, kAirState);
        std::printf("  width %2u, %6u entries: %8.1f + %8.1f + %8.1f = %8.1f\n", r.w, r.entries,
                    static_cast<double>(TA::indexBytes(d)) / 1024, static_cast<double>(TA::paletteBytes(d)) / 1024,
                    static_cast<double>(TA::tableBytes(d)) / 1024,
                    static_cast<double>(TA::indexBytes(d) + TA::paletteBytes(d) + TA::tableBytes(d)) / 1024);
    }
    {
        Chunk u(testStates());
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
            const Chunk& d = ChunkWorldTestAccess::chunk(w);
            std::printf("  width %u, palette %u, live %u\n", TA::width(d), TA::paletteSize(d), TA::liveEntries(d));
        }
    }
}

// `sandbox_tests --time-meshers` (versions.md v0.2.3, measurements): for each
// case and mesher, the median time per mesh and, for the bitmask path, where
// it went; quads, vertices and bytes; the merge ratio (unit faces per quad);
// the working state's size; and, in the v0.1 scene, how many coplanar
// neighbouring faces could not merge because of orientation or material.
void timeMeshers() {
    struct Shape {
        const char* name;
        Cells cells;
    };
    std::vector<Shape> shapes;
    std::mt19937_64 rng(77);
    {
        std::string err;
        const auto scene = loadScenario(SANDBOX_SCENARIO_DIR "/v0.1-scene.json", err);
        ChunkWorld w(registry());
        if (scene) {
            std::vector<std::string> warnings;
            w.applyBulk(sceneEdits(*scene, registry(), &warnings));
        }
        Cells c(kBrickCells);
        for (std::uint32_t i = 0; i < kBrickCells; ++i) c[i] = ChunkWorldTestAccess::chunk(w).get(localOfIndex(i));
        shapes.push_back({"v0.1 scene", c});
    }
    shapes.push_back({"uniform solid", Cells(kBrickCells, solidState())});
    {
        Cells c(kBrickCells, kAirState);  // a heightmap: stone, dirt, grass
        const StateId stone = solidState(), dirt = registry().state(typeOf("dirt"), orient::kIdentity), grass = registry().state(typeOf("grass"), orient::kIdentity);
        for (int y = 0; y < 64; ++y)
            for (int x = 0; x < 64; ++x) {
                const int top = 24 + static_cast<int>(8.0 * std::sin(x * 0.15) * std::cos(y * 0.11)) + static_cast<int>(rng() % 2);
                for (int z = 0; z <= top; ++z)
                    c[static_cast<std::size_t>(brickCellIndex(x, y, z))] = z == top ? grass : z > top - 3 ? dirt : stone;
            }
        shapes.push_back({"heightmap", c});
    }
    {
        Cells c = shapes.back().cells;  // a lake: tinted glass (water's stand-in) over the heightmap, up to z = 26
        const StateId water = registry().state(typeOf("glass_blue"), orient::kIdentity);
        for (int y = 0; y < 64; ++y)
            for (int x = 0; x < 64; ++x)
                for (int z = 0; z <= 26; ++z) {
                    auto& cell = c[static_cast<std::size_t>(brickCellIndex(x, y, z))];
                    if (cell == kAirState) cell = water;
                }
        shapes.push_back({"glass lake", c});
    }
    {
        Cells c(kBrickCells, kAirState);
        for (std::uint32_t i = 0; i < kBrickCells; ++i) {
            const glm::ivec3 l = localOfIndex(i);
            if ((l.x + l.y + l.z) % 2 == 0) c[i] = solidState();
        }
        shapes.push_back({"checkerboard", c});
    }
    {
        Cells c(kBrickCells, kAirState);  // random, many states, opaque and translucent
        for (auto& s : c) s = rng() % 3 == 0 ? testState(1 + static_cast<std::uint32_t>(rng() % (validStateCount() - 1))) : kAirState;
        shapes.push_back({"random, many states", c});
    }

    MeshTimings phases;
    std::printf("Mesh time per chunk (ms, median of 7) and size, by mesher:\n");
    std::printf("  %-20s %-9s %9s %8s %8s %9s %11s %8s\n", "case", "mesher", "ms", "quads", "transl.", "KiB", "v0.2.4 KiB", "faces/q");
    for (const Shape& sh : shapes) {
        Chunk d(testStates());
        fillChunk(d, sh.cells, true);
        for (Mesher m : {Mesher::Greedy, Mesher::Unmerged, Mesher::PerCell}) {
            std::vector<double> ms;
            WorldMesh last;
            for (int rep = 0; rep < 7; ++rep) {
                WorldMesh out;
                const auto t0 = Clock::now();
                d.meshInto(testContext(), m, out);
                ms.push_back(msSince(t0));
                last = std::move(out);
            }
            const std::size_t quads = last.quads.size(), tq = last.translucentQuads.size();
            const double kib = static_cast<double>((quads + tq) * sizeof(std::uint64_t)) / 1024.0;
            // The same quads in v0.2.4's layout: 4 vertices of 36 bytes, 6
            // 4-byte indices and an outline of 8 12-byte points each.
            const double oldKib = static_cast<double>((quads + tq) * (4 * 36 + 6 * 4 + 8 * 12)) / 1024.0;
            const std::size_t faces = unitFaces(last).size();
            std::printf("  %-20s %-9s %9.3f %8zu %8zu %9.1f %11.1f %8.2f\n", sh.name, mesherName(m), median(ms), quads, tq, kib, oldKib,
                        quads + tq ? static_cast<double>(faces) / static_cast<double>(quads + tq) : 0.0);
        }
        // Where the greedy mesh's time goes (one more mesh, timed by phase; the
        // timers themselves cost a little).
        phases = {};
        setMeshTimings(&phases);
        for (int rep = 0; rep < 7; ++rep) {
            WorldMesh out;
            d.meshInto(testContext(), Mesher::Greedy, out);
        }
        setMeshTimings(nullptr);
        std::printf("  %-20s   greedy phases (ms, mean): masks %.3f, culling %.3f, transposes %.3f, merging %.3f, emission %.3f\n",
                    "", phases.masks / 7, phases.culling / 7, phases.transposes / 7, phases.merging / 7, phases.emission / 7);
    }
    // Translucent quads: one per face (unmerged), merged by key alone (the
    // side constraint off: counted, not drawn), and merged with it.
    std::printf("\nTranslucent quads: unmerged, merged without the side constraint, with it:\n");
    for (const Shape& sh : shapes) {
        Chunk d(testStates());
        fillChunk(d, sh.cells, true);
        auto count = [&](Mesher m) {
            WorldMesh out;
            d.meshInto(testContext(), m, out);
            return out.translucentQuads.size();
        };
        const std::size_t unmerged = count(Mesher::Unmerged), constrained = count(Mesher::Greedy);
        setTranslucentSideConstraint(false);
        const std::size_t free = count(Mesher::Greedy);
        setTranslucentSideConstraint(true);
        if (unmerged > 0)
            std::printf("  %-20s %8zu %8zu %8zu  (the constraint costs %+.1f%%)\n", sh.name, unmerged, free, constrained,
                        free ? 100.0 * (static_cast<double>(constrained) - static_cast<double>(free)) / static_cast<double>(free) : 0.0);
    }
    std::printf("\nWorking state on this thread after the cases: %zu bytes (%zu allocations)\n", meshWorkingBytes(),
                meshWorkingAllocations());
    std::printf("Appearance table: %zu payloads (largest id %zu), %zu bytes on the GPU\n", testStates().payloads().size(),
                testStates().payloads().size() - 1, testStates().payloads().size() * 16);

    // Merges split by orientation or material in the v0.1 scene: coplanar
    // neighbouring opaque faces (the next cell along either in-plane axis has
    // a face in the same direction) whose payloads differ.
    {
        Chunk d(testStates());
        fillChunk(d, shapes[0].cells, true);
        WorldMesh out;
        d.meshInto(testContext(), Mesher::PerCell, out);
        std::map<std::tuple<int, int, int, int>, FacePayload> faces;
        for (const UnitFace& f : unitFaces(out))
            if (!f.translucent) faces[{f.x, f.y, f.z, f.dir}] = f.payload;
        constexpr int kBit[6] = {1, 1, 0, 0, 0, 0}, kRow[6] = {2, 2, 2, 2, 1, 1};
        std::size_t same = 0, byTurn = 0, byMaterial = 0, other = 0;
        for (const auto& [key, p] : faces) {
            const auto [x, y, z, dir] = key;
            for (int axis : {kBit[dir], kRow[dir]}) {
                int n[3] = {x, y, z};
                ++n[axis];
                const auto it = faces.find({n[0], n[1], n[2], dir});
                if (it == faces.end()) continue;
                const FacePayload& q = it->second;
                if (p == q) ++same;
                else if (p.layer == q.layer && p.material == q.material && p.texInfo != q.texInfo) ++byTurn;
                else if (p.layer == q.layer && p.texInfo == q.texInfo && p.material != q.material) ++byMaterial;
                else ++other;
            }
        }
        std::printf("\nv0.1 scene, coplanar neighbouring opaque faces: %zu with equal payload (mergeable), %zu split by "
                    "orientation (same layer and material, different turn or set), %zu by material only, %zu different textures\n",
                    same, byTurn, byMaterial, other);
    }
}

// `sandbox_tests --time-states` (versions.md v0.2.4, measurements): the cost
// of interning a state, of loading the definitions with every state interned,
// and the state table's size.
void timeStates() {
    std::vector<double> load;
    std::size_t states = 0, defs = 0;
    for (int rep = 0; rep < 5; ++rep) {
        const auto t0 = Clock::now();
        const BlockRegistry r = BlockRegistry::load(SANDBOX_ASSET_DIR "/blocks");
        load.push_back(msSince(t0));
        states = r.states().size();
        defs = r.defs().size();
    }
    std::printf("Loading %zu definitions (textures included) and interning their %zu states: %.2f ms (median of 5)\n", defs,
                states, median(load));
    const StateTable& shipped = registry().states();
    std::printf("States per shipped definition:");
    for (std::uint32_t d = 1; d < shipped.definitionCount() && d <= registry().defs().size(); ++d)
        std::printf(" %s %u", shipped.definitionAt(d).name.c_str(), shipped.definitionAt(d).stateCount);
    std::printf("\n");

    // Interning alone: fresh tables, 2¹⁰ states per definition, hashed as they are first seen.
    for (int rep = 0; rep < 3; ++rep) {
        StateTable t;
        PropertyDecl v{"v", {}, true, 0};
        for (std::uint32_t i = 0; i < StateTable::kMaxStatesPerDefinition; ++i) v.values.push_back("v" + std::to_string(i));
        constexpr std::uint32_t kDefs = 64;
        std::vector<std::uint32_t> ids;
        for (std::uint32_t d = 0; d < kDefs; ++d)
            ids.push_back(t.addDefinition(Uid::fromParts(0xBE7C000000000000ull, d), "m" + std::to_string(d), 0, nullptr,
                                          orient::Symmetry::Uniform, {v}, false));
        const auto t0 = Clock::now();
        for (std::uint32_t d : ids)
            for (std::uint32_t i = 0; i < StateTable::kMaxStatesPerDefinition; ++i) t.intern(d, orient::kIdentity, std::span<const std::uint32_t>(&i, 1));
        const double ms = msSince(t0);
        const auto n = static_cast<double>(t.size() - 1);
        // A lookup of a state already interned (no hash).
        const auto t1 = Clock::now();
        std::uint64_t sink = 0;
        for (std::uint32_t d : ids)
            for (std::uint32_t i = 0; i < StateTable::kMaxStatesPerDefinition; ++i)
                sink += t.intern(d, orient::kIdentity, std::span<const std::uint32_t>(&i, 1)).value;
        const double lookupMs = msSince(t1);
        std::printf("Interning %.0f new states: %.0f ns each; an existing one: %.0f ns; table %.0f bytes per state (sink %llu)\n", n,
                    ms * 1e6 / n, lookupMs * 1e6 / n, static_cast<double>(t.bytes()) / n, static_cast<unsigned long long>(sink & 1));
    }
}
