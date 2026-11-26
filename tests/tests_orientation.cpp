// Tests: orientation. Registered through sandbox_tests.cpp.

#include "test_support.h"

namespace {

using namespace orient;

// --- the rotation group ------------------------------------------------------------

int det(const Mat3i& m) {
    return m[0][0] * (m[1][1] * m[2][2] - m[1][2] * m[2][1]) - m[0][1] * (m[1][0] * m[2][2] - m[1][2] * m[2][0]) +
           m[0][2] * (m[1][0] * m[2][1] - m[1][1] * m[2][0]);
}

const Mat3i kI = {{{1, 0, 0}, {0, 1, 0}, {0, 0, 1}}};

void testGroupMatrices() {
    const Group& g = group();
    CHECK(g.matrix[kIdentity] == kI);
    std::set<Mat3i> distinct(g.matrix.begin(), g.matrix.end());
    CHECK(distinct.size() == 24);
    for (const Mat3i& m : g.matrix) {
        CHECK(det(m) == 1);  // proper: no mirrors
        for (int r = 0; r < 3; ++r) {  // signed permutation: one ±1 per row
            int nonzero = 0;
            for (int c = 0; c < 3; ++c) nonzero += m[static_cast<std::size_t>(r)][static_cast<std::size_t>(c)] != 0;
            CHECK(nonzero == 1);
        }
    }
    // Indexing: 4·facing + spin, facing = image of +z in kRepOrder order.
    for (int r = 0; r < kRotCount; ++r)
        CHECK(rotateVec(g.matrix[static_cast<std::size_t>(r)], {0, 0, 1}) ==
              faceNormal(kRepOrder[static_cast<std::size_t>(r / 4)]));
}

void testGroupTables() {
    const Group& g = group();
    for (int a = 0; a < kRotCount; ++a) {
        const auto ua = static_cast<std::size_t>(a);
        for (int b = 0; b < kRotCount; ++b) {
            const auto ub = static_cast<std::size_t>(b);
            // Composition table agrees with matrix multiplication (and closure).
            CHECK(g.matrix[g.compose[ua][ub]] == multiply(g.matrix[ua], g.matrix[ub]));
        }
        CHECK(multiply(g.matrix[ua], g.matrix[g.inverse[ua]]) == kI);
        CHECK(g.compose[g.inverse[ua]][ua] == kIdentity);
        for (int n = 0; n < kFaceCount; ++n) {
            const auto un = static_cast<std::size_t>(n);
            CHECK(faceNormal(g.faceMap[ua][un]) == rotateVec(g.matrix[ua], faceNormal(static_cast<Face>(n))));
            CHECK(g.faceTurns[ua][un] < 4);
        }
    }
    // Texture-frame turns compose like the rotations do:
    // turns(a·b, n) = turns(b, n) + turns(a, b·n)  (mod 4).
    for (int a = 0; a < kRotCount; ++a)
        for (int b = 0; b < kRotCount; ++b)
            for (int n = 0; n < kFaceCount; ++n) {
                const auto ua = static_cast<std::size_t>(a), ub = static_cast<std::size_t>(b);
                const auto un = static_cast<std::size_t>(n);
                const Face bn = g.faceMap[ub][un];
                CHECK(g.faceTurns[g.compose[ua][ub]][un] == (g.faceTurns[ub][un] + g.faceTurns[ua][bn]) % 4);
            }
    // Spot checks of the frame convention: R_z(90°) keeps side-face textures
    // upright; turning +z's texture by R_z(90°) turns it one quarter CCW.
    const Rot z90 = rotationAbout({0, 0, 1}, 90);
    CHECK(g.faceMap[z90][NegY] == PosX);
    CHECK(g.faceTurns[z90][NegY] == 0);
    CHECK(g.faceTurns[z90][PosZ] == 1);
    CHECK(rotateVec(g.matrix[z90], {1, 0, 0}) == glm::ivec3(0, 1, 0));
}

void testRotationAbout() {
    const Group& g = group();
    const Rot v = rotationAbout({1, -1, 1}, 120);
    CHECK(rotateVec(g.matrix[v], {1, -1, 1}) == glm::ivec3(1, -1, 1));  // fixes its axis
    CHECK(v != kIdentity && g.compose[v][v] != kIdentity && g.compose[g.compose[v][v]][v] == kIdentity);
    const Rot e = rotationAbout({0, -1, 1}, 180);
    CHECK(g.faceMap[e][PosZ] == NegY);  // the +z/−y edge axis swaps those faces
    CHECK(g.compose[e][e] == kIdentity);
}

// --- symmetry types -----------------------------------------------------------------

struct Expected {
    Symmetry s;
    const char* keyword;
    std::size_t order;
    std::size_t orbits;
};
constexpr Expected kExpected[] = {
    {Symmetry::Uniform, "uniform", 24, 1},        {Symmetry::Column, "column", 8, 2},
    {Symmetry::Pointed, "pointed", 4, 3},         {Symmetry::Brick, "brick", 4, 3},
    {Symmetry::TwistedColumn, "twisted-column", 4, 2}, {Symmetry::PointedBrick, "pointed-brick", 2, 4},
    {Symmetry::Edge, "edge", 2, 3},               {Symmetry::Corner, "corner", 3, 2},
    {Symmetry::Diagonal, "diagonal", 6, 1},       {Symmetry::Tetrahedral, "tetrahedral", 12, 1},
    {Symmetry::Oriented, "oriented", 1, 6},
};

void testSubgroups() {
    const Group& g = group();
    for (const Expected& e : kExpected) {
        const SymmetryInfo& info = symmetry(e.s);
        CHECK(std::string(info.keyword) == e.keyword);
        CHECK(symmetryFromKeyword(e.keyword) == e.s);
        CHECK(info.subgroup.size() == e.order);
        CHECK(info.subgroup.front() == kIdentity);
        for (Rot a : info.subgroup)  // closed under composition
            for (Rot b : info.subgroup)
                CHECK(std::binary_search(info.subgroup.begin(), info.subgroup.end(), g.compose[a][b]));
        CHECK(info.orbits.size() == e.orbits);
        std::size_t covered = 0;
        for (const Orbit& o : info.orbits) {
            covered += o.faces.size();
            CHECK(o.faces.front() == o.rep);
            // Orbit-stabilizer: |orbit|·|stabilizer| = |H|.
            std::size_t stab = 0;
            for (Rot h : info.subgroup) stab += g.faceMap[h][o.rep] == o.rep;
            CHECK(o.faces.size() * stab == e.order);
        }
        CHECK(covered == 6);
    }
    CHECK(!symmetryFromKeyword("spherical"));
}

void testCanonicalization() {
    const Group& g = group();
    for (const Expected& e : kExpected) {
        const SymmetryInfo& info = symmetry(e.s);
        std::set<Rot> classes;
        for (int r = 0; r < kRotCount; ++r) {
            const auto ur = static_cast<std::size_t>(r);
            const Rot c = info.canonical[ur];
            classes.insert(c);
            CHECK(info.canonical[c] == c);  // idempotent
            CHECK(c <= r);                  // the lowest index in its coset
            bool inCoset = false;
            for (Rot h : info.subgroup) {
                CHECK(info.canonical[g.compose[ur][h]] == c);  // constant on the coset gH
                inCoset = inCoset || g.compose[ur][h] == c;
            }
            CHECK(inCoset);
        }
        CHECK(classes.size() == 24 / e.order);
    }
    // The v0.1.3 test scene's logs: z-axis 0, and z-axis turned about its
    // own axis is the same state.
    CHECK(canonicalize(Symmetry::Column, rotationAbout({0, 0, 1}, 90)) == 0);
    CHECK(canonicalize(Symmetry::Column, rotationAbout({0, 1, 0}, 90)) !=
          canonicalize(Symmetry::Column, rotationAbout({1, 0, 0}, 90)));
}

// Canonical storage makes g and g·h (h ∈ H) look identical with ANY
// texture, symmetric or not (world/block-orientation, revised): both
// canonicalize to the same index, and rendering uses only the stored index.
// The per-face mapping is the canonical transversal: the lowest-index h ∈ H
// carrying the representative onto the face.
void testCanonicalLooksIdentical() {
    const Group& g = group();
    for (const Expected& e : kExpected) {
        const SymmetryInfo& info = symmetry(e.s);
        // What a world face shows when the block is STORED with rotation r:
        // (representative texture, total CCW turns).
        auto look = [&](Rot r, Face world) {
            const Face local = g.faceMap[g.inverse[r]][world];
            const int turns = (info.repTurns[local] + g.faceTurns[r][local]) % 4;
            return std::pair<Face, int>{info.repOf[local], turns};
        };
        for (int r = 0; r < kRotCount; ++r)
            for (Rot h : info.subgroup) {
                const Rot a = info.canonical[static_cast<std::size_t>(r)];
                const Rot b = info.canonical[g.compose[static_cast<std::size_t>(r)][h]];
                CHECK(a == b);
                for (int w = 0; w < kFaceCount; ++w) CHECK(look(a, static_cast<Face>(w)) == look(b, static_cast<Face>(w)));
            }
        for (int f = 0; f < kFaceCount; ++f) {
            const Face face = static_cast<Face>(f);
            const Rot first = *std::find_if(info.subgroup.begin(), info.subgroup.end(),
                                            [&](Rot h) { return g.faceMap[h][info.repOf[face]] == face; });
            CHECK(info.repTurns[face] == g.faceTurns[first][info.repOf[face]]);
        }
    }
}

// The wrench: quarter turns about the six face axes reach all 24 rotations in
// at most 3 clicks, with distance histogram 1 / 6 / 11 / 6.
void testWrenchReachability() {
    const Group& g = group();
    std::vector<Rot> turns;
    for (const glm::ivec3 axis : {glm::ivec3{1, 0, 0}, glm::ivec3{0, 1, 0}, glm::ivec3{0, 0, 1}})
        for (int deg : {90, -90}) turns.push_back(rotationAbout(axis, deg));
    CHECK(turns.size() == 6);
    for (int start = 0; start < kRotCount; ++start) {
        std::array<int, kRotCount> dist;
        dist.fill(-1);
        dist[static_cast<std::size_t>(start)] = 0;
        std::vector<Rot> frontier = {static_cast<Rot>(start)};
        for (std::size_t i = 0; i < frontier.size(); ++i)
            for (Rot t : turns) {
                const Rot next = g.compose[t][frontier[i]];  // world-frame turn of the placed block
                if (dist[next] < 0) {
                    dist[next] = dist[frontier[i]] + 1;
                    frontier.push_back(next);
                }
            }
        std::array<int, 4> histogram{};
        bool ok = true;
        for (int d : dist) {
            ok = ok && d >= 0 && d <= 3;
            if (d >= 0 && d <= 3) ++histogram[static_cast<std::size_t>(d)];
        }
        CHECK(ok);
        CHECK((histogram == std::array<int, 4>{1, 6, 11, 6}));
    }
}


}  // namespace

void runOrientationTests() {
    testGroupMatrices();
    testGroupTables();
    testRotationAbout();
    testSubgroups();
    testCanonicalization();
    testCanonicalLooksIdentical();
    testWrenchReachability();
}
