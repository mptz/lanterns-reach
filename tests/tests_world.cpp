// Tests: world. Registered through sandbox_tests.cpp.

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


// emitters() reads no cell but the emitters: it follows every edit (opaque and
// translucent emitters, replaced, removed, edits that change nothing, bulk
// applies, copies), always in z, y, x order, and equals a brute-force scan.
void testEmittersFollowEdits() {
    const BlockState states[] = {BlockState::air(), solidState(), {typeOf("lamp"), kIdentity}, {typeOf("glow"), kIdentity},
                                 {typeOf("glass_red"), kIdentity}, {typeOf("gold"), kIdentity}};
    auto scan = [](const ChunkWorld& w) {  // every cell of the region the edits use, in z, y, x order
        std::vector<Emitter> out;
        for (std::int64_t z = 0; z < 12; ++z)
            for (std::int64_t y = 0; y < 12; ++y)
                for (std::int64_t x = 0; x < 12; ++x) {
                    const BlockState st = w.probe({x, y, z});
                    if (st.isAir()) continue;
                    const BlockDef& def = registry().def(st.type);
                    const glm::vec3 e = def.translucent() ? def.translucency->emission : def.material.emissive;
                    if (e.x > 0.0f || e.y > 0.0f || e.z > 0.0f) out.push_back({{x, y, z}, e});
                }
        return out;
    };
    auto same = [](const std::vector<Emitter>& a, const std::vector<Emitter>& b) {
        if (a.size() != b.size()) return false;
        for (std::size_t i = 0; i < a.size(); ++i)
            if (!(a[i].cell == b[i].cell) || a[i].emissive != b[i].emissive) return false;
        return true;
    };
    std::mt19937_64 rng(3);
    ChunkWorld w(registry());
    CHECK(w.emitters().empty());
    bool ok = true;
    for (int i = 0; i < 4000; ++i) {
        EditSet e;
        for (int k = 0, n = 1 + static_cast<int>(rng() % 4); k < n; ++k)
            e.edits.push_back({{static_cast<std::int64_t>(rng() % 12), static_cast<std::int64_t>(rng() % 12),
                                static_cast<std::int64_t>(rng() % 12)},
                               states[rng() % std::size(states)]});
        if (i % 7 == 0) w.applyBulk(e);
        else w.apply(e);
        if (i % 20 == 0) {
            const ChunkWorld copy = w;  // a copy carries its emitters
            ok = ok && same(w.emitters(), scan(w)) && same(copy.emitters(), scan(w));
        }
    }
    CHECK(ok);
    CHECK(!w.emitters().empty());
    // The lamp's light is what the registry says; replacing it with stone removes it.
    ChunkWorld one(registry());
    one.apply({{{{3, 4, 5}, BlockState{typeOf("lamp"), kIdentity}}}});
    CHECK(one.emitters().size() == 1 && one.emitters()[0].cell == CellPos{3, 4, 5} && one.emitters()[0].emissive.x > 0.0f);
    one.apply({{{{3, 4, 5}, solidState()}}});
    CHECK(one.emitters().empty());
    one.apply({{{{70, 0, 0}, BlockState{typeOf("lamp"), kIdentity}}}});  // rejected: outside the chunk
    CHECK(one.emitters().empty());
}

}  // namespace

void runWorldTests() {
    testRaycast();
    testChunkEditsMeshEmitters();
    testEmittersFollowEdits();
}
