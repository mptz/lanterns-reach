// Tests: world. Registered through sandbox_tests.cpp.

#include <chrono>

#include "test_support.h"

namespace {

using namespace orient;

void testRaycast() {
    {  // axis-aligned
        const ChunkWorld g = worldWith({{5, 2, 2}});
        const auto hit = g.raycast({0.5, 2.5, 2.5}, {1, 0, 0}, 16.0);
        CHECK(hit && hit->cell == CellPos{5, 2, 2} && hit->normal == glm::ivec3(-1, 0, 0));
        CHECK(hit && near(hit->distance, 4.5) && near(hit->point.x, 5.0) && hit->state == solidState());
        CHECK(!g.raycast({0.5, 2.5, 2.5}, {1, 0, 0}, 4.4));  // beyond reach
    }
    {  // diagonal, non-degenerate: crosses x = 4 at y = 1.95 (cell y 1), then y = 2 at x = 4.1
        const ChunkWorld g = worldWith({{4, 2, 0}});
        const auto hit = g.raycast({0.5, 0.2, 0.5}, {1, 0.5, 0}, 16.0);
        CHECK(hit && hit->cell == CellPos{4, 2, 0} && hit->normal == glm::ivec3(0, -1, 0));
        CHECK(hit && near(hit->point.x, 4.1) && near(hit->point.y, 2.0));
    }
    {  // diagonal through exact lattice corners: ties step x, then y, then z
        const ChunkWorld g = worldWith({{3, 3, 3}});
        const auto hit = g.raycast({0.5, 0.5, 0.5}, {1, 1, 1}, 16.0);
        CHECK(hit && hit->cell == CellPos{3, 3, 3} && hit->normal == glm::ivec3(0, 0, -1));
        CHECK(hit && near(hit->distance, 2.5 * std::sqrt(3.0)));
    }
    {  // grazing an edge: the ray runs exactly along the edge y = 2, z = 2.
        // Cells are half-open, so it belongs to cells (x, 2, 2); a solid cell
        // touching the edge diagonally is not hit.
        const ChunkWorld diag = worldWith({{5, 1, 1}});
        CHECK(!diag.raycast({0.5, 2.0, 2.0}, {1, 0, 0}, 16.0));
        const ChunkWorld onEdge = worldWith({{5, 2, 2}});
        const auto hit = onEdge.raycast({0.5, 2.0, 2.0}, {1, 0, 0}, 16.0);
        CHECK(hit && hit->cell == CellPos{5, 2, 2} && hit->normal == glm::ivec3(-1, 0, 0));
    }
    {  // starting inside a solid cell: that cell, distance 0, no face normal
        const ChunkWorld g = worldWith({{2, 2, 2}});
        const auto hit = g.raycast({2.5, 2.5, 2.5}, {0, 1, 0}, 16.0);
        CHECK(hit && hit->cell == CellPos{2, 2, 2} && hit->normal == glm::ivec3(0) && hit->distance == 0.0);
    }
    {  // leaving the chunk without a hit; a ray that never enters it
        const ChunkWorld g = worldWith({{2, 2, 2}});
        CHECK(!g.raycast({4.5, 4.5, 4.5}, {0, 0, 1}, 100.0));
        CHECK(!g.raycast({-2.0, 20.0, 1.0}, {1, 0, 0}, 100.0));
        CHECK(!g.raycast({4.5, 4.5, 4.5}, {0, 0, 0}, 100.0));  // no direction
    }
    {  // entering from outside, through either face, including from exactly on the far face
        const ChunkWorld g = worldWith({{0, 4, 4}, {63, 5, 5}});
        const auto a = g.raycast({-3.0, 4.5, 4.5}, {1, 0, 0}, 16.0);
        CHECK(a && a->cell == CellPos{0, 4, 4} && a->normal == glm::ivec3(-1, 0, 0) && near(a->distance, 3.0));
        const auto b = g.raycast({67.0, 5.5, 5.5}, {-1, 0, 0}, 16.0);
        CHECK(b && b->cell == CellPos{63, 5, 5} && b->normal == glm::ivec3(1, 0, 0) && near(b->distance, 3.0));
        const auto c = g.raycast({64.0, 5.5, 5.5}, {-1, 0, 0}, 16.0);
        CHECK(c && c->cell == CellPos{63, 5, 5} && c->normal == glm::ivec3(1, 0, 0) && c->distance == 0.0);
        CHECK(!g.raycast({-3.0, 4.5, 4.5}, {1, 0, 0}, 2.9));  // the chunk itself is out of reach
    }
}

void testChunkEditsMeshEmitters() {
    ChunkWorld g(registry());
    const std::uint64_t gen0 = g.generation();
    EditSet e{{{{1, 1, 1}, solidState()}, {{65, 1, 1}, solidState()}}};  // the second is outside
    const ApplyResult r1 = g.apply(e);
    CHECK(r1.changed == 1 && r1.rejected.size() == 1 && r1.rejected[0] == CellPos{65, 1, 1});
    CHECK(g.generation() == gen0 + 1);
    CHECK((g.dirtyChunks() == std::vector<ChunkId>{ChunkWorld::kChunk}));  // never meshed yet
    CHECK(g.apply(e).changed == 0);  // no change: same state again
    CHECK(g.generation() == gen0 + 1);
    // An edit that is ONLY out of bounds is rejected and changes nothing at
    // all: not the chunk, not the generation.
    const WorldMesh before = g.buildMesh(ChunkWorld::kChunk);
    CHECK(g.dirtyChunks().empty());  // meshing clears the chunk's dirtiness
    const ApplyResult r2 = g.apply({{{{-1, 0, 0}, solidState()}, {{0, 64, 0}, solidState()}}});
    CHECK(r2.changed == 0 && r2.rejected.size() == 2);
    CHECK(g.generation() == gen0 + 1 && g.buildMesh(ChunkWorld::kChunk).indices == before.indices);
    CHECK(g.probe({1, 1, 1}) == solidState() && g.probe({65, 1, 1}).isAir() && g.probe({-1, 0, 0}).isAir());

    auto faces = [](const WorldMesh& m) { return m.indices.size() / 6; };
    CHECK(faces(g.buildMesh(ChunkWorld::kChunk)) == 6);
    g.apply({{{{2, 1, 1}, solidState()}}});
    CHECK(faces(g.buildMesh(ChunkWorld::kChunk)) == 10);  // the shared face is culled on both sides
    g.apply({{{{0, 0, 0}, solidState()}}});
    CHECK(faces(g.buildMesh(ChunkWorld::kChunk)) == 16);  // at the chunk's corner: outside counts as air
    const WorldMesh m = g.buildMesh(ChunkWorld::kChunk);
    CHECK(m.vertices.size() == faces(m) * 4 && m.edgeLines.size() == faces(m) * 8);
    g.apply({{{{1, 1, 1}, BlockState::air()}}});
    CHECK(faces(g.buildMesh(ChunkWorld::kChunk)) == 12);

    CHECK(g.emitters().empty());
    const auto lamp = registry().find("lamp");
    CHECK(lamp.has_value());
    if (lamp) {
        g.apply({{{{5, 5, 5}, BlockState{*lamp, orient::kIdentity}}}});
        const auto em = g.emitters();
        CHECK(em.size() == 1 && em[0].cell == CellPos{5, 5, 5} && em[0].emissive.x > 0.0f);
    }
}


}  // namespace

void runWorldTests() {
    testRaycast();
    testChunkEditsMeshEmitters();
}

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
