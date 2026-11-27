// Tests: the bitmask mesher (versions.md v0.2.3, acceptance 5–8): faces at the
// chunk's boundary, runs and rows, what merges and what doesn't, the uniform
// fast paths, the working state, append-only output. Registered through
// sandbox_tests.cpp.

#include <thread>

#include "test_support.h"

namespace {

using namespace orient;
using TA = ChunkTestAccess;

MeshContext context() { return testContext(); }

bool wellFormed(const std::vector<UnitFace>& faces) {
    return std::none_of(faces.begin(), faces.end(), [](const UnitFace& f) { return f.dir < 0; });
}

// The greedy and unmerged meshes decompose into exactly the per-cell rule's
// unit faces (payloads and frame bits included), and the translucent quads
// are identical to its (cell order, then face order). Returns the per-cell mesh's unit-face count.
std::size_t faceSetsMatch(ChunkWorld& w) {
    const WorldMesh ref = meshWith(w, Mesher::PerCell);
    const auto want = unitFaces(ref);
    CHECK(wellFormed(want));
    for (Mesher m : {Mesher::Greedy, Mesher::Unmerged}) {
        const WorldMesh got = meshWith(w, m);
        const auto faces = unitFaces(got);
        CHECK(wellFormed(faces) && faces == want);
        CHECK(got.translucentQuads == ref.translucentQuads);
    }
    return want.size();
}

ChunkWorld worldOf(const StateId& s, std::initializer_list<CellPos> cells) {
    ChunkWorld w(registry());
    EditSet e;
    for (const CellPos& c : cells) e.edits.push_back({c, s});
    w.apply(e);
    return w;
}

StateId kGlass() { return registry().state(typeOf("glass_red"), orient::kIdentity); }

// --- the packed quad (v0.2.5 acceptance 5) ------------------------------------

// Pack and unpack round-trip at every field's extremes, and each field lands
// in its own bits (packed_quad.h).
void testPackedQuad() {
    for (int cell : {0, 63})
        for (int ext : {1, 64})
            for (int d = 0; d < 6; ++d)
                for (std::uint32_t bits : {0u, 5u, 10u, 15u})
                    for (std::uint32_t app : {0u, 1u, quad::kMaxAppearance}) {
                        const quad::Quad q{.cell = {cell, 63 - cell, cell}, .w = ext, .h = 65 - ext,
                                           .face = static_cast<Face>(d), .sides = bits, .corners = 15u - bits, .appearance = app};
                        CHECK(quad::unpack(quad::pack(q)) == q);
                    }
    CHECK(quad::pack({.cell = {63, 0, 0}, .w = 1, .h = 1}) == 63u);
    CHECK(quad::pack({.cell = {0, 0, 63}, .w = 1, .h = 1}) == std::uint64_t{63} << 12);
    CHECK(quad::pack({.w = 64, .h = 1}) == std::uint64_t{63} << 18);
    CHECK(quad::pack({.w = 1, .h = 64}) == std::uint64_t{63} << 24);
    CHECK(quad::pack({.face = NegZ}) == std::uint64_t{5} << 30);
    CHECK(quad::pack({.sides = 15}) == std::uint64_t{15} << 33);
    CHECK(quad::pack({.corners = 15}) == std::uint64_t{15} << 37);
    CHECK(quad::pack({.appearance = quad::kMaxAppearance}) == ~std::uint64_t{0} << 41);
    // R runs along the bit axis exactly where variants.glsl says it does.
    for (int d = 0; d < 6; ++d) {
        const glm::ivec3 n = orient::faceNormal(static_cast<Face>(d));
        const glm::ivec3 up = n.z != 0 ? glm::ivec3(0, 1, 0) : glm::ivec3(0, 0, 1);
        const glm::ivec3 right(up.y * n.z - up.z * n.y, up.z * n.x - up.x * n.z, up.x * n.y - up.y * n.x);
        CHECK(right[quad::kBitAxis[d]] == (quad::kRightAlongBit[d] ? 1 : -1) && up[quad::kRowAxis[d]] == 1);
    }
}

// --- the bit-matrix transpose ------------------------------------------------

void testTranspose() {
    std::mt19937_64 rng(5);
    for (int rep = 0; rep < 50; ++rep) {
        std::uint64_t a[64], b[64];
        for (auto& row : a) row = rng();
        std::memcpy(b, a, sizeof a);
        transpose64(b);
        bool ok = true;
        for (int r = 0; r < 64; ++r)
            for (int c = 0; c < 64; ++c) ok = ok && ((a[r] >> c) & 1) == ((b[c] >> r) & 1);
        CHECK(ok);
        transpose64(b);
        CHECK(std::memcmp(a, b, sizeof a) == 0);  // an involution
    }
    std::uint64_t one[64] = {};
    one[3] = std::uint64_t{1} << 60;
    transpose64(one);
    bool onlyOne = one[60] == std::uint64_t{1} << 3;
    for (int i = 0; i < 64; ++i) onlyOne = onlyOne && (i == 60 || one[i] == 0);
    CHECK(onlyOne);
}

// --- faces at the boundary ---------------------------------------------------

// Every corner and face centre of the chunk, alone: each cell has all six
// faces, the chunk's boundary ones emitted against outside air.
void testBoundaryFaces() {
    std::vector<CellPos> cells;
    for (std::int64_t x : {0, 63})
        for (std::int64_t y : {0, 63})
            for (std::int64_t z : {0, 63}) cells.push_back({x, y, z});
    for (std::int64_t a : {0, 63}) {
        cells.push_back({a, 31, 31});
        cells.push_back({31, a, 31});
        cells.push_back({31, 31, a});
    }
    for (const StateId& s : {solidState(), kGlass()}) {
        ChunkWorld w = worldOf(s, {});
        EditSet e;
        for (const CellPos& c : cells) e.edits.push_back({c, s});
        w.apply(e);
        CHECK(faceSetsMatch(w) == cells.size() * 6);
    }
}

// Runs: at bit 0, at bit 63 and of the full width, in rows 0 and 63.
void testRunsAndRows() {
    auto row = [](std::int64_t y, std::int64_t z, int from, int to) {
        std::vector<CellPos> cells;
        for (int x = from; x < to; ++x) cells.push_back({x, y, z});
        return cells;
    };
    struct Case {
        std::int64_t y, z;
        int from, to;
    };
    for (const Case& c : {Case{5, 5, 0, 64}, Case{5, 5, 0, 10}, Case{5, 5, 54, 64}, Case{0, 0, 0, 64}, Case{63, 63, 0, 64},
                          Case{0, 63, 20, 40}, Case{63, 0, 63, 64}, Case{9, 9, 0, 1}}) {
        const auto cells = row(c.y, c.z, c.from, c.to);
        const int n = c.to - c.from;
        ChunkWorld w = worldOf(solidState(), {});
        EditSet e;
        for (const CellPos& p : cells) e.edits.push_back({p, solidState()});
        w.apply(e);
        CHECK(faceSetsMatch(w) == static_cast<std::size_t>(4 * n + 2));  // ±y and ±z along the row, the two ends
        // A row of n cells: ±y and ±z are each one n×1 quad, the two ends 1×1.
        auto sizes = quadSizes(w.buildMesh(ChunkWorld::kChunk));
        CHECK(sizes.size() == 6);
        int merged = 0;
        for (const QuadSize& q : sizes)
            if (q.dir >= 2) merged += q.w == n && q.h == 1;
        CHECK(merged == 4);
        // Glass doesn't merge: one quad per face.
        ChunkWorld g = worldOf(kGlass(), {});
        EditSet ge;
        for (const CellPos& p : cells) ge.edits.push_back({p, kGlass()});
        g.apply(ge);
        faceSetsMatch(g);
        CHECK(g.buildMesh(ChunkWorld::kChunk).translucentQuads.size() == static_cast<std::size_t>(4 * n + 2));
    }
}

// A full 64×64 plane meshes to one quad; a checkerboard to one quad per face.
void testPlanesAndCheckerboard() {
    ChunkWorld w(registry());
    EditSet e;
    for (std::int64_t y = 0; y < 64; ++y)
        for (std::int64_t x = 0; x < 64; ++x) e.edits.push_back({{x, y, 0}, solidState()});
    w.apply(e);
    faceSetsMatch(w);
    const auto sizes = quadSizes(w.buildMesh(ChunkWorld::kChunk));
    CHECK(sizes.size() == 6);
    CHECK((std::count(sizes.begin(), sizes.end(), QuadSize{4, 64, 64}) == 1) && (std::count(sizes.begin(), sizes.end(), QuadSize{5, 64, 64}) == 1));

    ChunkWorld c(registry());
    EditSet ce;
    for (std::int64_t z = 0; z < 16; ++z)
        for (std::int64_t y = 0; y < 16; ++y)
            for (std::int64_t x = 0; x < 16; ++x)
                if ((x + y + z) % 2 == 0) ce.edits.push_back({{x, y, z}, solidState()});
    c.apply(ce);
    const std::size_t faces = faceSetsMatch(c);
    CHECK(faces == 6 * 2048);
    CHECK(c.buildMesh(ChunkWorld::kChunk).quads.size() == faces);  // every face its own quad, even greedy
}

// --- what merges -------------------------------------------------------------

// Cells (0..n−1, 0, 0) with the given states, one per cell: the quads on `face`.
std::vector<QuadSize> quadsOnFace(const std::vector<StateId>& states, int face) {
    ChunkWorld w(registry());
    EditSet e;
    for (std::size_t i = 0; i < states.size(); ++i) e.edits.push_back({{static_cast<std::int64_t>(i), 0, 0}, states[i]});
    w.apply(e);
    faceSetsMatch(w);
    std::vector<QuadSize> out;
    for (const QuadSize& q : quadSizes(w.buildMesh(ChunkWorld::kChunk)))
        if (q.dir == face) out.push_back(q);
    return out;
}

void testMergeRules() {
    const StateTable& table = testStates();
    const BlockTypeId log = typeOf("log");
    // Orientation: a log along x and one along y show the same bark on top but
    // turned differently, so they never merge; two along x do.
    const StateId alongX = registry().state(log, rotationAbout({0, 1, 0}, 90));
    const StateId alongY = registry().state(log, rotationAbout({1, 0, 0}, 90));
    const FaceTexture tx = faceTexture(testStates(), alongX, orient::PosZ), ty = faceTexture(testStates(), alongY, orient::PosZ);
    CHECK(tx.layer == ty.layer && tx.turn != ty.turn);
    CHECK(table.appearance(alongX, orient::PosZ) != table.appearance(alongY, orient::PosZ));
    CHECK((quadsOnFace({alongX, alongY}, orient::PosZ).size() == 2));
    CHECK((quadsOnFace({alongX, alongX, alongX}, orient::PosZ) == std::vector<QuadSize>{{orient::PosZ, 3, 1}}));

    // Two distinct states with identical payload on a face merge there (cells in
    // a row along x touch along ±y and ±z).
    bool foundSame = false;
    for (BlockTypeId t = 0; t < registry().defs().size() && !foundSame; ++t)
        for (int a = 0; a < orient::kRotCount && !foundSame; ++a)
            for (int b = a + 1; b < orient::kRotCount && !foundSame; ++b) {
                const StateId sa = registry().state(t, static_cast<orient::Rot>(a)), sb = registry().state(t, static_cast<orient::Rot>(b));
                if (sa == sb || registry().def(t).translucent()) continue;
                for (int f = 2; f < 6 && !foundSame; ++f)
                    if (table.appearance(sa, static_cast<orient::Face>(f)) == table.appearance(sb, static_cast<orient::Face>(f))) {
                        foundSame = true;
                        CHECK((quadsOnFace({sa, sb}, f) == std::vector<QuadSize>{{f, 2, 1}}));
                    }
            }
    if (!foundSame) std::printf("  (no two distinct shipped states share a face payload: that merge is untested on shipped blocks)\n");

    // Equal texture and turn but different materials (block types) never merge.
    bool foundPair = false;
    for (BlockTypeId a = 0; a < registry().defs().size() && !foundPair; ++a)
        for (BlockTypeId b = a + 1; b < registry().defs().size() && !foundPair; ++b) {
            if (registry().def(a).translucent() || registry().def(b).translucent()) continue;
            for (int f = 0; f < 6 && !foundPair; ++f) {
                const FacePayload pa = table.payload(table.appearance(registry().state(a, orient::kIdentity), static_cast<orient::Face>(f)));
                const FacePayload pb = table.payload(table.appearance(registry().state(b, orient::kIdentity), static_cast<orient::Face>(f)));
                if (pa.layer == pb.layer && pa.texInfo == pb.texInfo && pa.material != pb.material) {
                    foundPair = true;
                    CHECK(table.appearance(registry().state(a, orient::kIdentity), static_cast<orient::Face>(f)) != table.appearance(registry().state(b, orient::kIdentity), static_cast<orient::Face>(f)));
                    if (f >= 2) CHECK(quadsOnFace({registry().state(a, orient::kIdentity), registry().state(b, orient::kIdentity)}, f).size() == 2);
                }
            }
        }
    if (!foundPair) std::printf("  (no two block types share a face texture: the material split is untested on shipped blocks)\n");

    // Random and matched variant sets merge freely: the tile is chosen per fragment.
    std::optional<BlockTypeId> random, matched;
    for (BlockTypeId t = 0; t < registry().defs().size(); ++t) {
        if (registry().def(t).translucent()) continue;
        const VariantSet set = registry().def(t).faceSet[orient::PosZ];
        if (set == VariantSet::Random && !random) random = t;
        if ((set == VariantSet::MatchedNS || set == VariantSet::MatchedNSEW) && !matched) matched = t;
    }
    CHECK(random.has_value() && matched.has_value());
    for (const auto t : {random, matched})
        if (t) CHECK((quadsOnFace(std::vector<StateId>(8, registry().state(*t, orient::kIdentity)), orient::PosZ) == std::vector<QuadSize>{{orient::PosZ, 8, 1}}));
}

// Glass against stone keeps its face (it closes the volume for absorbance),
// and the stone keeps its face toward the glass.
void testGlassAgainstStone() {
    ChunkWorld w(registry());
    w.apply({{{{5, 5, 5}, kGlass()}, {{6, 5, 5}, solidState()}}});
    CHECK(faceSetsMatch(w) == 12);
    const WorldMesh m = w.buildMesh(ChunkWorld::kChunk);
    CHECK(m.translucentQuads.size() == 6 && m.quads.size() == 6);
    const auto faces = unitFaces(m);
    CHECK(std::any_of(faces.begin(), faces.end(), [](const UnitFace& f) { return f.translucent && f.dir == orient::PosX && f.x == 5; }));
}

// The connected-frame bits from masks equal the per-cell rule's: every
// translucent face of the v0.1 scene (and of a glass blob with holes) has the
// same bytes, tiles included.
void testFrameBitsFromMasks() {
    std::string err;
    const auto scene = loadScenario(SANDBOX_SCENARIO_DIR "/v0.1-scene.json", err);
    CHECK(scene.has_value());
    ChunkWorld w(registry());
    if (scene) {
        std::vector<std::string> warnings;
        w.applyBulk(sceneEdits(*scene, registry(), &warnings));
        CHECK(faceSetsMatch(w) > 0);
        CHECK(!w.buildMesh(ChunkWorld::kChunk).translucentQuads.empty());
    }
    std::mt19937_64 rng(9);
    ChunkWorld blob(registry());
    EditSet e;
    const StateId glass[] = {registry().state(typeOf("glass_red"), orient::kIdentity), registry().state(typeOf("glass_blue"), orient::kIdentity), registry().state(typeOf("glass_green"), orient::kIdentity)};
    for (std::int64_t z = 0; z < 8; ++z)
        for (std::int64_t y = 0; y < 8; ++y)
            for (std::int64_t x = 0; x < 8; ++x) {
                const auto r = rng() % 10;
                if (r < 5) e.edits.push_back({{x, y, z}, glass[rng() % 3]});
                else if (r < 6) e.edits.push_back({{x, y, z}, solidState()});
            }
    blob.apply(e);
    CHECK(faceSetsMatch(blob) > 0);
}

// --- uniform fast paths --------------------------------------------------------

void testUniformFastPaths() {
    // Uniform air emits nothing and touches no working state.
    ChunkWorld air(registry());
    const std::size_t before = meshWorkingAllocations();
    const WorldMesh none = air.buildMesh(ChunkWorld::kChunk);
    CHECK(none.quads.empty() && none.translucentQuads.empty());
    CHECK(meshWorkingAllocations() == before);

    // Uniform opaque: exactly six 64×64 quads, no masks built (the opaque path reads no working state).
    ChunkWorld solid(registry());
    fillChunk(ChunkWorldTestAccess::chunk(solid), Cells(kBrickCells, solidState()), true);
    CHECK(TA::isUniform(ChunkWorldTestAccess::chunk(solid)));
    const std::size_t beforeSolid = meshWorkingAllocations();
    const WorldMesh sm = solid.buildMesh(ChunkWorld::kChunk);
    const auto sizes = quadSizes(sm);
    CHECK(sizes.size() == 6 && std::all_of(sizes.begin(), sizes.end(), [](const QuadSize& q) { return q.w == 64 && q.h == 64; }));
    CHECK(meshWorkingAllocations() == beforeSolid);
    CHECK(unitFaces(sm) == unitFaces(meshWith(solid, Mesher::PerCell)));
    CHECK(meshWith(solid, Mesher::Unmerged).quads.size() == 6 * 64 * 64);
    // Its palette form (forced to every width) meshes to the same bytes.
    for (unsigned width : kBrickWidths) {
        ChunkWorld p(registry());
        ChunkWorldTestAccess::chunk(p) = ChunkWorldTestAccess::chunk(solid);
        TA::forceWidth(ChunkWorldTestAccess::chunk(p), width);
        CHECK(TA::width(ChunkWorldTestAccess::chunk(p)) == width);
        for (Mesher m : {Mesher::Greedy, Mesher::Unmerged, Mesher::PerCell}) CHECK(sameMesh(meshWith(solid, m), meshWith(p, m)));
    }

    // Uniform translucent: the translucent pass with an all-ones group mask,
    // 24,576 unit quads, byte for byte what its palette form gives.
    ChunkWorld glass(registry());
    fillChunk(ChunkWorldTestAccess::chunk(glass), Cells(kBrickCells, kGlass()), true);
    CHECK(TA::isUniform(ChunkWorldTestAccess::chunk(glass)));
    const WorldMesh gm = glass.buildMesh(ChunkWorld::kChunk);
    CHECK(gm.translucentQuads.size() == 6 * 64 * 64 && gm.quads.empty());
    for (unsigned width : {1u, 8u, 32u}) {
        ChunkWorld p(registry());
        ChunkWorldTestAccess::chunk(p) = ChunkWorldTestAccess::chunk(glass);
        TA::forceWidth(ChunkWorldTestAccess::chunk(p), width);
        CHECK(sameMesh(gm, meshWith(p, Mesher::Greedy)));
    }
    const WorldMesh gref = meshWith(glass, Mesher::PerCell);
    CHECK(unitFaces(gm) == unitFaces(gref) && gm.translucentQuads == gref.translucentQuads);
}

// --- the working state ----------------------------------------------------------

Cells randomMixed(std::mt19937_64& rng, std::uint32_t states) {
    Cells cells(kBrickCells, kAirState);
    std::vector<StateId> pool;
    for (std::uint32_t k = 0; k < states; ++k) pool.push_back(testState(1 + static_cast<std::uint32_t>(rng() % (validStateCount() - 1))));
    for (const char* g : {"glass_red", "glass_blue", "glass_green"}) pool.push_back(registry().state(typeOf(g), orient::kIdentity));  // always the same groups
    for (std::uint32_t c = 0; c < kBrickCells; ++c) {
        const std::uint32_t z = c >> 12;
        if (z < 12 || rng() % 9 == 0) cells[c] = pool[rng() % pool.size()];
    }
    return cells;
}

void testWorkingState() {
    std::mt19937_64 rng(11);
    ChunkWorld big(registry());
    fillChunk(ChunkWorldTestAccess::chunk(big), randomMixed(rng, 60), true);
    (void)big.buildMesh(ChunkWorld::kChunk);  // this thread's first mesh sizes the working state
    const std::size_t after = meshWorkingAllocations();
    CHECK(after > 0 && meshWorkingBytes() > 0);
    // Further meshes of chunks no larger (palette entries, translucent groups) allocate nothing.
    for (int i = 0; i < 4; ++i) {
        ChunkWorld w(registry());
        fillChunk(ChunkWorldTestAccess::chunk(w), randomMixed(rng, 10 + static_cast<std::uint32_t>(i) * 8), true);
        for (Mesher m : {Mesher::Greedy, Mesher::Unmerged}) (void)meshWith(w, m);
    }
    (void)big.buildMesh(ChunkWorld::kChunk);
    CHECK(meshWorkingAllocations() == after);

    // A mesh made on a second thread is byte-identical to the first's (and
    // that thread has a working state of its own).
    const WorldMesh here = big.buildMesh(ChunkWorld::kChunk);
    WorldMesh there;
    std::thread([&] { there = big.buildMesh(ChunkWorld::kChunk); }).join();
    CHECK(sameMesh(here, there));
}

// Meshing into a non-empty output leaves what is there byte-identical and
// appends exactly the mesh it would have made.
void testAppendOnly() {
    std::mt19937_64 rng(13);
    ChunkWorld w(registry());
    fillChunk(ChunkWorldTestAccess::chunk(w), randomMixed(rng, 25), true);
    const Chunk& chunk = ChunkWorldTestAccess::chunk(w);
    for (Mesher m : {Mesher::Greedy, Mesher::Unmerged, Mesher::PerCell}) {
        WorldMesh fresh;
        chunk.meshInto(context(), m, fresh);
        WorldMesh out;  // a previous chunk's quads, in both arrays
        for (int i = 0; i < 3; ++i) {
            out.quads.push_back(0x1234 + static_cast<std::uint64_t>(i));
            out.translucentQuads.push_back(0x5678 + static_cast<std::uint64_t>(i));
        }
        const WorldMesh prefix = out;
        chunk.meshInto(context(), m, out);
        // What was there is untouched; the appended tail is the fresh mesh.
        bool shifted = out.quads.size() == 3 + fresh.quads.size() && out.translucentQuads.size() == 3 + fresh.translucentQuads.size();
        shifted = shifted && std::equal(prefix.quads.begin(), prefix.quads.end(), out.quads.begin()) &&
                  std::equal(prefix.translucentQuads.begin(), prefix.translucentQuads.end(), out.translucentQuads.begin()) &&
                  std::equal(fresh.quads.begin(), fresh.quads.end(), out.quads.begin() + 3) &&
                  std::equal(fresh.translucentQuads.begin(), fresh.translucentQuads.end(), out.translucentQuads.begin() + 3);
        CHECK(shifted);
    }
}

}  // namespace

void runMeshingTests() {
    testPackedQuad();
    testTranspose();
    testBoundaryFaces();
    testRunsAndRows();
    testPlanesAndCheckerboard();
    testMergeRules();
    testGlassAgainstStone();
    testFrameBitsFromMasks();
    testUniformFastPaths();
    testWorkingState();
    testAppendOnly();
}
