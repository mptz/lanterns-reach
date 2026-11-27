// Tests: the randomized face-set equivalence test and rep-swap invariance for
// meshing (versions.md v0.2.3, acceptance 3 and 4). Content from sparse to full
// in every registry type and canonical orientation (variant sets and several
// translucent groups included), stored in every representation and width that
// holds it. For each:
//   - the greedy and unmerged meshes, decomposed into unit faces, equal the
//     per-cell rule's multiset exactly (an overlapping or missing quad shows
//     as a mismatch), and the translucent mesh equals its bytes;
//   - the same content meshes to byte-identical output, order included, in
//     every representation: uniform, a palette at each width that holds it,
//     one built by edits (dead entries), one with its entries in another
//     order; and again after a random edit sequence (growth included).
// Registered through sandbox_tests.cpp (the `sandbox_equivalence` ctest entry).

#include <chrono>
#include <cstdlib>

#include "test_support.h"

namespace {

using TA = ChunkTestAccess;

enum class Shape { Sparse, Medium, Dense, Terrain, Boxes, Uniform };

struct Case {
    unsigned seed;
    Shape shape;
    std::uint32_t states;  // distinct states in play (Uniform: 1)
    bool glass;            // uniform content of a translucent state
};

std::vector<StateId> makePool(std::mt19937_64& rng, std::uint32_t n, bool withGlass) {
    std::vector<StateId> pool;
    const std::uint32_t valid = validStateCount();
    n = std::min(n, valid - 1);  // the registry has only so many distinct states
    while (pool.size() < n) {
        const StateId s = testState(1 + static_cast<std::uint32_t>(rng() % (valid - 1)));
        if (std::find(pool.begin(), pool.end(), s) == pool.end()) pool.push_back(s);
    }
    if (withGlass)  // several translucent groups, whatever the draw gave
        for (const char* g : {"glass_red", "glass_blue", "glass_green"}) {
            const StateId s = registry().state(typeOf(g), orient::kIdentity);
            if (std::find(pool.begin(), pool.end(), s) == pool.end()) pool.push_back(s);
        }
    return pool;
}

Cells makeCells(const Case& c, std::mt19937_64& rng) {
    Cells cells(kBrickCells, kAirState);
    if (c.shape == Shape::Uniform) {
        const StateId s = c.glass ? registry().state(typeOf("glass_green"), orient::kIdentity) : solidState();
        std::fill(cells.begin(), cells.end(), s);
        return cells;
    }
    const std::vector<StateId> pool = makePool(rng, c.states, c.states > 1);
    auto pick = [&] { return pool[rng() % pool.size()]; };
    switch (c.shape) {
        case Shape::Sparse:
            for (int i = 0; i < 1500; ++i) cells[rng() % kBrickCells] = pick();
            break;
        case Shape::Medium:  // 30% of a 40³ corner region
            for (std::uint32_t cell = 0; cell < kBrickCells; ++cell)
                if ((cell & 63u) < 40 && ((cell >> 6) & 63u) < 40 && (cell >> 12) < 40 && rng() % 10 < 3) cells[cell] = pick();
            break;
        case Shape::Dense:  // every cell of a 20³ corner region
            for (std::uint32_t cell = 0; cell < kBrickCells; ++cell)
                if ((cell & 63u) < 20 && ((cell >> 6) & 63u) < 20 && (cell >> 12) < 20) cells[cell] = pick();
            break;
        case Shape::Terrain: {
            std::vector<int> height(64 * 64);
            for (int y = 0; y < 64; ++y)
                for (int x = 0; x < 64; ++x) height[static_cast<std::size_t>(y * 64 + x)] = 10 + (x / 8 + y / 8) % 7 + static_cast<int>(rng() % 3);
            for (std::uint32_t cell = 0; cell < kBrickCells; ++cell) {
                const int x = static_cast<int>(cell & 63), y = static_cast<int>((cell >> 6) & 63), z = static_cast<int>(cell >> 12);
                if (z < height[static_cast<std::size_t>(y * 64 + x)]) cells[cell] = pool[static_cast<std::size_t>(z / 3) % pool.size()];
                else if (z < 14 && rng() % 4 != 0) cells[cell] = pool[rng() % pool.size()];  // "water" and clutter above the ground
            }
            break;
        }
        case Shape::Boxes:
            for (int i = 0; i < 60; ++i) {
                const StateId s = pick();
                const int x0 = static_cast<int>(rng() % 60), y0 = static_cast<int>(rng() % 60), z0 = static_cast<int>(rng() % 60);
                const int dx = 1 + static_cast<int>(rng() % 20), dy = 1 + static_cast<int>(rng() % 20), dz = 1 + static_cast<int>(rng() % 20);
                for (int z = z0; z < std::min(64, z0 + dz); ++z)
                    for (int y = y0; y < std::min(64, y0 + dy); ++y)
                        for (int x = x0; x < std::min(64, x0 + dx); ++x) cells[static_cast<std::size_t>(brickCellIndex(x, y, z))] = s;
            }
            break;
        case Shape::Uniform: break;
    }
    return cells;
}

struct Copy {
    std::string name;
    Chunk data{testStates()};
};

WorldMesh meshOf(const Chunk& d, Mesher m) {
    WorldMesh out;
    d.meshInto(testContext(), m, out);
    return out;
}

bool sameVertices(const std::vector<MeshVertex>& a, const std::vector<MeshVertex>& b) {
    return a.size() == b.size() && (a.empty() || std::memcmp(a.data(), b.data(), a.size() * sizeof(MeshVertex)) == 0);
}

struct Abort {};  // a case stops at its first difference

struct Run {
    unsigned seed;
    std::uint64_t meshes = 0;
    [[noreturn]] void fail(const char* what, const std::string& copy, Mesher m);
};

[[noreturn]] void Run::fail(const char* what, const std::string& copy, Mesher m) {
    std::fprintf(stderr, "mesh equivalence seed %u: %s in copy %s with the %s mesher\n", seed, what, copy.c_str(), mesherName(m));
    ++check::failures();
    throw Abort{};
}

// The checks for the current content of the copies.
void compare(Run& run, std::vector<Copy>& copies, bool everyCopyPerCell) {
    // The reference: the per-cell rule over the bulk-filled copy.
    const WorldMesh ref = meshOf(copies[0].data, Mesher::PerCell);
    const std::vector<UnitFace> want = unitFaces(ref);
    if (!std::none_of(want.begin(), want.end(), [](const UnitFace& f) { return f.dir < 0; })) run.fail("malformed per-cell quad", copies[0].name, Mesher::PerCell);
    for (Mesher m : {Mesher::Greedy, Mesher::Unmerged, Mesher::PerCell}) {
        const WorldMesh base = m == Mesher::PerCell ? ref : meshOf(copies[0].data, m);
        ++run.meshes;
        if (m != Mesher::PerCell) {
            if (unitFaces(base) != want) run.fail("unit faces differ from the per-cell rule", copies[0].name, m);
            if (!sameVertices(base.translucentVertices, ref.translucentVertices) || base.translucentIndices != ref.translucentIndices)
                run.fail("translucent mesh differs from the per-cell rule's bytes", copies[0].name, m);
        }
        for (std::size_t i = 1; i < copies.size(); ++i) {
            if (m == Mesher::PerCell && !everyCopyPerCell && i + 1 != copies.size()) continue;  // the per-cell mesh is the slow one
            const WorldMesh other = meshOf(copies[i].data, m);
            ++run.meshes;
            if (!sameMesh(base, other)) run.fail("mesh bytes differ from the bulk fill's", copies[i].name, m);
        }
    }
}

void runCase(const Case& c, bool longRun) {
    Run run{c.seed};
    std::mt19937_64 rng(c.seed);
    Cells cells = makeCells(c, rng);
    const bool verbose = std::getenv("SANDBOX_TEST_VERBOSE") != nullptr;
    const auto t0 = std::chrono::steady_clock::now();
    if (verbose) {
        Chunk probe(testStates());
        fillChunk(probe, cells, true);
        const WorldMesh m = meshOf(probe, Mesher::Greedy);
        std::fprintf(stderr, "mesh case %u: shape %d, %u live states, greedy %zu quads + %zu translucent, per-cell %zu faces\n", c.seed,
                     static_cast<int>(c.shape), TA::liveEntries(probe), m.indices.size() / 6, m.translucentIndices.size() / 6,
                     unitFaces(m).size());
    }
    try {
        std::vector<Copy> copies;
        copies.push_back({"fill", Chunk(testStates())});
        fillChunk(copies.back().data, cells, true);
        const std::uint32_t live = TA::liveEntries(copies[0].data);
        // The widest width that holds it, forced: the palette at another width.
        for (unsigned w : {16u, 32u}) {
            if (brickIndexLimit(w) < live) continue;
            copies.push_back({"w" + std::to_string(w), copies[0].data});
            TA::forceWidth(copies.back().data, w);
        }
        if (longRun)
            for (unsigned w : {1u, 2u, 4u, 8u}) {
                if (brickIndexLimit(w) < live) continue;
                copies.push_back({"w" + std::to_string(w), copies[0].data});
                TA::forceWidth(copies.back().data, w);
            }
        copies.push_back({"edits", Chunk(testStates())});  // promotion and growth on the way, a dead air entry
        fillChunk(copies.back().data, cells, false);
        // Entries in another order: cells set in reverse, then compacted (renumbered by first appearance).
        copies.push_back({"reordered", Chunk(testStates())});
        for (std::uint32_t i = kBrickCells; i-- > 0;) copies.back().data.set(localOfIndex(i), cells[i]);
        TA::compact(copies.back().data);
        compare(run, copies, longRun);

        // After a random edit sequence: edits among existing states and new ones (growth).
        {
            const std::uint32_t edits = longRun ? 4000 : 600;
            for (std::uint32_t i = 0; i < edits; ++i) {
                const auto cell = static_cast<std::uint32_t>(rng() % kBrickCells);
                const StateId s = i % 5 == 0 ? testState(1 + static_cast<std::uint32_t>(rng() % (validStateCount() - 1)))
                                                : cells[rng() % kBrickCells];
                cells[cell] = s;
                for (Copy& cp : copies) cp.data.set(localOfIndex(cell), s);
            }
            compare(run, copies, longRun);
        }
    } catch (const Abort&) {
    }
    if (verbose)
        std::fprintf(stderr, "  %.1f s, %llu meshes\n", std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count(),
                     static_cast<unsigned long long>(run.meshes));
}

}  // namespace

void runMeshEquivalenceTests(bool longRun, int onlyCase) {
    const Case fixed[] = {{1, Shape::Sparse, 3, false},  {2, Shape::Medium, 17, false}, {3, Shape::Boxes, 5, false},
                          {4, Shape::Terrain, 40, false}, {5, Shape::Uniform, 1, false}, {6, Shape::Uniform, 1, true},
                          {7, Shape::Dense, 300, false},  {8, Shape::Medium, 2, false}};
    for (const Case& c : fixed)
        if (onlyCase < 0 || static_cast<int>(c.seed) == onlyCase) runCase(c, false);
    if (onlyCase >= 0) return;
    if (longRun) {
        std::mt19937_64 rng(2027);
        for (unsigned i = 0; i < 24; ++i) {
            const Shape shapes[] = {Shape::Sparse, Shape::Medium, Shape::Dense, Shape::Terrain, Shape::Boxes};
            const std::uint32_t counts[] = {1, 2, 3, 5, 17, 40, 120, 300};
            runCase({100 + i, shapes[rng() % 5], counts[rng() % 8], false}, true);
        }
    }
}
