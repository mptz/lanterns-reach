// Tests: the cutout visibility class (versions.md v0.2.5, acceptance 7;
// world/chunk-meshing). A fixture registry (tests/blocks_cutout/) has one
// opaque block, one glass and two cutout types, so both "two of one type
// cull" and "two types don't" can be checked. Registered through
// sandbox_tests.cpp.

#include "test_support.h"

namespace {

using orient::Face;

const BlockRegistry& cutoutRegistry() {
    static const BlockRegistry reg = BlockRegistry::load(SANDBOX_TEST_DIR "/blocks_cutout");
    return reg;
}

StateId fixture(const char* name) { return cutoutRegistry().state(*cutoutRegistry().find(name), orient::kIdentity); }

// The unit faces of every mesher, checked against the per-cell rule's.
std::vector<UnitFace> facesOf(ChunkWorld& w) {
    const StateTable& t = cutoutRegistry().states();
    const auto ref = unitFaces(meshWith(w, Mesher::PerCell), t);
    for (Mesher m : {Mesher::Greedy, Mesher::Unmerged}) CHECK(unitFaces(meshWith(w, m), t) == ref);
    return ref;
}

bool hasFace(const std::vector<UnitFace>& faces, glm::ivec3 c, int dir) {
    return std::any_of(faces.begin(), faces.end(), [&](const UnitFace& f) { return f.x == c.x && f.y == c.y && f.z == c.z && f.dir == dir; });
}

// The culling rule on every side, against each class: a cutout cell's face
// toward a neighbour is hidden by an opaque cell or one of its own type
// only; the neighbour's face toward it is always kept except between two of
// one type. Cutout faces go to the cutout array.
void testCullingRule() {
    const StateId a = fixture("grate_a");
    const struct {
        const char* name;
        StateId state;
        bool hidesCutout;  // the cutout face toward it is culled
        bool keepsOwn;     // its own face toward the cutout cell is emitted
    } classes[] = {{"air", kAirState, false, false},
                   {"opaque", fixture("opaque"), true, true},
                   {"glass", fixture("glass"), false, true},
                   {"same cutout type", a, true, false},
                   {"another cutout type", fixture("grate_b"), false, true}};
    const glm::ivec3 c(10, 10, 10);
    for (const auto& k : classes)
        for (int d = 0; d < 6; ++d) {
            const glm::ivec3 n = orient::faceNormal(static_cast<Face>(d));
            ChunkWorld w(cutoutRegistry());
            EditSet e;
            e.edits.push_back({{c.x, c.y, c.z}, a});
            if (k.state != kAirState) e.edits.push_back({{c.x + n.x, c.y + n.y, c.z + n.z}, k.state});
            w.apply(e);
            const auto faces = facesOf(w);
            const bool cutoutFace = hasFace(faces, c, d), neighbourFace = hasFace(faces, c + n, d ^ 1);
            if (cutoutFace == k.hidesCutout || neighbourFace != k.keepsOwn)
                std::fprintf(stderr, "cutout against %s toward face %d: cutout face %d, neighbour face %d\n", k.name, d, cutoutFace,
                             neighbourFace);
            CHECK(cutoutFace != k.hidesCutout && neighbourFace == k.keepsOwn);
            const bool neighbourCutout = cutoutRegistry().states().cutout(k.state);
            for (const UnitFace& f : faces) {
                const glm::ivec3 at(f.x, f.y, f.z);
                CHECK(f.cutout == (at == c || (neighbourCutout && at == c + n)));
            }
        }
}

// Cutout quads merge like opaque ones (equal appearance id); a uniform
// cutout chunk meshes to its six sides, as its palette form does.
void testCutoutMerging() {
    const StateId a = fixture("grate_a"), b = fixture("grate_b");
    ChunkWorld row(cutoutRegistry());
    EditSet e;
    for (int x = 0; x < 8; ++x) e.edits.push_back({{2 + x, 5, 5}, a});
    row.apply(e);
    CHECK(facesOf(row).size() == 4 * 8 + 2);
    const WorldMesh m = meshWith(row, Mesher::Greedy);
    CHECK(m.quads.empty() && m.translucentQuads.empty() && m.cutoutQuads.size() == 6);
    // Two types side by side don't merge (and keep the faces between them).
    ChunkWorld mixed(cutoutRegistry());
    mixed.apply({{{{2, 5, 5}, a}, {{3, 5, 5}, b}}});
    CHECK(facesOf(mixed).size() == 12 && meshWith(mixed, Mesher::Greedy).cutoutQuads.size() == 12);

    ChunkWorld uniform(cutoutRegistry());
    fillChunk(ChunkWorldTestAccess::chunk(uniform), Cells(kBrickCells, a), true);
    CHECK(ChunkTestAccess::isUniform(ChunkWorldTestAccess::chunk(uniform)));
    const WorldMesh um = meshWith(uniform, Mesher::Greedy);
    const auto sizes = quadSizes(um);
    CHECK(um.cutoutQuads.size() == 6 && um.quads.empty() &&
          std::all_of(sizes.begin(), sizes.end(), [](const QuadSize& q) { return q.w == 64 && q.h == 64; }));
    CHECK(facesOf(uniform).size() == 6 * 64 * 64);
    ChunkWorld palette(cutoutRegistry());
    ChunkWorldTestAccess::chunk(palette) = ChunkWorldTestAccess::chunk(uniform);
    ChunkTestAccess::forceWidth(ChunkWorldTestAccess::chunk(palette), 8);
    for (Mesher mm : {Mesher::Greedy, Mesher::Unmerged, Mesher::PerCell}) CHECK(sameMesh(meshWith(uniform, mm), meshWith(palette, mm)));
}

// Glass against a cutout cell keeps its face and shows no surface layer
// (the cutout cell counts as opaque for frames), in both meshers.
void testGlassAgainstCutout() {
    ChunkWorld w(cutoutRegistry());
    w.apply({{{{4, 4, 4}, fixture("glass")}, {{5, 4, 4}, fixture("grate_a")}}});
    const auto faces = facesOf(w);
    bool found = false;
    for (const UnitFace& f : faces)
        if (f.translucent && f.x == 4 && f.dir == orient::PosX) {
            found = true;
            CHECK(f.frame == 0u);
        }
    CHECK(found);
}

}  // namespace

void runCutoutTests() {
    testCullingRule();
    testCutoutMerging();
    testGlassAgainstCutout();
}
