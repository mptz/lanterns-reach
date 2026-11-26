// sandbox_tests — pure-logic tests (no GPU). Registered with CTest.
//
//   sandbox_tests                 run all checks

#include <algorithm>
#include <bitset>
#include <array>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <set>
#include <string>
#include <vector>

#include <glm/glm.hpp>

#include <GLFW/glfw3.h>

#include "block_mesh.h"
#include "blocks.h"
#include "camera.h"
#include "check.h"
#include "coords.h"
#include "grid_world.h"
#include "hands.h"
#include "intentions.h"
#include "orientation.h"
#include "scenario.h"
#include "placement.h"
#include "tools.h"
#include "translucency.h"

namespace {

using namespace orient;

// --- coords ------------------------------------------------------------------

// rebase() subtracts in f64 and only then downcasts, so a small offset far
// from the f64 origin survives exactly.
void testRebaseExactAtLargeOffset() {
    const WorldPos world{1e9 + 0.25, -1e9 - 0.5, 1e9 + 0.125};
    const WorldPos camera{1e9, -1e9, 1e9};
    const RenderPos r = rebase(world, camera);
    CHECK(r.v.x == 0.25f);
    CHECK(r.v.y == -0.5f);
    CHECK(r.v.z == 0.125f);
}

// Counterexample: the downcast-first form, (f32)world − (f32)origin. f32
// spacing at 1e9 is 64, so 1e9 + 0.25 rounds to exactly 1e9 and the offset
// is lost entirely. This is why the downcast lives only in rebase().
void testDowncastFirstLosesOffset() {
    const double world = 1e9 + 0.25;
    const double camera = 1e9;
    const float downcastFirst = static_cast<float>(world) - static_cast<float>(camera);
    CHECK(downcastFirst == 0.0f);
    CHECK(downcastFirst != 0.25f);
}

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

// --- block definitions --------------------------------------------------------------

Image solid(std::uint32_t n) {
    Image img{n, n, std::vector<std::uint8_t>(static_cast<std::size_t>(n) * n * 4, 128)};
    return img;
}

void setPixel(Image& img, std::uint32_t x, std::uint32_t y, std::uint8_t v) {
    for (int c = 0; c < 3; ++c) img.rgba[(y * img.width + x) * 4 + static_cast<std::uint32_t>(c)] = v;
}

std::uint8_t pixel(const Image& img, std::uint32_t x, std::uint32_t y) { return img.rgba[(y * img.width + x) * 4]; }

void testRotateCcw() {
    Image img = solid(4);
    setPixel(img, 0, 0, 255);  // top-left marker
    CHECK(pixel(rotateCcw(img, 1), 0, 3) == 255);  // CCW: top-left → bottom-left
    CHECK(pixel(rotateCcw(img, 2), 3, 3) == 255);
    CHECK(pixel(rotateCcw(img, 3), 3, 0) == 255);
    CHECK(rotateCcw(img, 4) == img);
    CHECK(rotateCcw(rotateCcw(img, 1), 3) == img);
}

void testParseDefinition() {
    std::string err;
    const auto log = parseDefinition(R"({"name":"log","symmetry":"column","textures":{"+z":"e.png","-y":"s.png"}})", err);
    CHECK(log && log->symmetry == Symmetry::Column && log->textures.size() == 2);
    CHECK(log && log->material.roughness == 0.8f && log->material.metallic == 0.0f &&
          log->material.reflectance == 0.5f && log->material.emissive == glm::vec3(0.0f));  // defaults
    // Wrong key set: +x is in the side orbit, whose representative is −y.
    CHECK(!parseDefinition(R"({"name":"log","symmetry":"column","textures":{"+z":"e.png","+x":"s.png"}})", err));
    CHECK(err.find("orbit representatives") != std::string::npos);
    CHECK(!parseDefinition(R"({"name":"log","symmetry":"column","textures":{"+z":"e.png"}})", err));
    CHECK(!parseDefinition(R"({"name":"x","symmetry":"spherical","textures":{"+z":"a.png"}})", err));
    CHECK(!parseDefinition(R"({"name":"x","symmetry":"uniform","textures":{"+z":"a.png"},"extra":1})", err));
    CHECK(!parseDefinition("not json", err));
    // No assertions any more (v0.1.8: the loader checks structure only).
    CHECK(!parseDefinition(R"({"name":"x","symmetry":"uniform","textures":{"+z":"a.png"},"asserts":{"+z":["tileable"]}})", err));
    CHECK(err.find("unknown field") != std::string::npos);
    // Variant sets: three rules.
    const auto rnd = parseDefinition(R"({"name":"d","symmetry":"uniform","textures":{"+z":{"random":"d.png","tiles":8}}})", err);
    CHECK(rnd && rnd->textures[0].set == VariantSet::Random && rnd->textures[0].tiles == 8 && rnd->textures[0].file == "d.png");
    const auto ns = parseDefinition(R"({"name":"b","symmetry":"uniform","textures":{"+z":{"matched":"b.png","edges":"ns"}}})", err);
    CHECK(ns && ns->textures[0].set == VariantSet::MatchedNS && ns->textures[0].tiles == 4);
    const auto nsew = parseDefinition(R"({"name":"g","symmetry":"uniform","textures":{"+z":{"matched":"g.png","edges":"nsew"}}})", err);
    CHECK(nsew && nsew->textures[0].set == VariantSet::MatchedNSEW && nsew->textures[0].tiles == 16);
    const auto conn = parseDefinition(R"({"name":"g","symmetry":"uniform","textures":{"+z":{"connected":"g.png"}}})", err);
    CHECK(conn && conn->textures[0].set == VariantSet::Connected && conn->textures[0].tiles == 16);
    for (const char* bad : {R"({"random":"d.png"})", R"({"random":"d.png","tiles":0})", R"({"random":"d.png","tiles":33})",
                            R"({"matched":"b.png"})", R"({"matched":"b.png","edges":"ew"})", R"({"wang":"s.png"})",
                            R"({"connected":"g.png","tiles":4})"}) {
        const std::string text = std::string(R"({"name":"x","symmetry":"uniform","textures":{"+z":)") + bad + "}}";
        CHECK(!parseDefinition(text, err));
        CHECK(err.find("bad texture entry") != std::string::npos);
    }
    // Material fields.
    const auto lamp = parseDefinition(R"({"name":"lamp","symmetry":"uniform","textures":{"+z":"a.png"},
                                          "roughness":0.25,"metallic":1,"reflectance":0.75,"emissive":[2,1,0.5]})", err);
    CHECK(lamp && lamp->material.roughness == 0.25f && lamp->material.metallic == 1.0f &&
          lamp->material.reflectance == 0.75f && lamp->material.emissive == glm::vec3(2.0f, 1.0f, 0.5f));
    CHECK(!parseDefinition(R"({"name":"x","symmetry":"uniform","textures":{"+z":"a.png"},"roughness":1.5})", err));
    CHECK(!parseDefinition(R"({"name":"x","symmetry":"uniform","textures":{"+z":"a.png"},"emissive":[1,-1,0]})", err));

    // Image checks, structural only: 16×16 singles; a set's atlas is 64 wide
    // and exactly tall enough for its tiles. Asymmetric art is legal.
    auto rect = [](std::uint32_t w, std::uint32_t h) {
        return Image{w, h, std::vector<std::uint8_t>(static_cast<std::size_t>(w) * h * 4, 128)};
    };
    Image end = solid(16), side = solid(16);
    setPixel(side, 0, 0, 255);
    const Image* sidePtr = &side;
    const Image* endPtr = &end;
    auto images = [&](const std::string& f) -> const Image* { return f == "e.png" ? endPtr : sidePtr; };
    CHECK(checkTextures(*log, images).empty());
    const Image big = solid(32);
    sidePtr = &big;
    CHECK(checkTextures(*log, images).find("block textures are 16×16") != std::string::npos);
    const Image a64x32 = rect(64, 32), a64x16 = rect(64, 16), a64x64 = rect(64, 64);
    sidePtr = &a64x32;
    CHECK(checkTextures(*rnd, images).empty());  // 8 tiles: two rows
    sidePtr = &a64x16;
    CHECK(checkTextures(*rnd, images).find("need 64×32") != std::string::npos);
    CHECK(checkTextures(*ns, images).empty());  // 4 tiles: one row
    sidePtr = &a64x64;
    CHECK(checkTextures(*ns, images).find("4 tiles of 16×16") != std::string::npos);
    CHECK(checkTextures(*nsew, images).empty() && checkTextures(*conn, images).empty());
}

// Variant sets: the atlas split (4 tiles per row, any count).
void testVariantSets() {
    Image atlas{64, 32, std::vector<std::uint8_t>(64 * 32 * 4, 128)};
    for (std::uint32_t i = 0; i < 8; ++i)  // mark each tile's top-left texel with its index
        setPixel(atlas, (i % 4) * 16, (i / 4) * 16, static_cast<std::uint8_t>(i));
    const std::vector<Image> tiles = splitSet(atlas, 8);
    CHECK(tiles.size() == 8 && tiles[0].width == 16 && tiles[0].height == 16);
    for (std::uint32_t i = 0; i < 8; ++i) CHECK(pixel(tiles[i], 0, 0) == i && pixel(tiles[i], 1, 0) == 128);
    CHECK(splitSet(atlas, 6).size() == 6);
    CHECK(setTileCount(VariantSet::Random, 5) == 5 && setTileCount(VariantSet::MatchedNS, 0) == 4 &&
          setTileCount(VariantSet::MatchedNSEW, 0) == 16 && setTileCount(VariantSet::Connected, 0) == 16 &&
          setTileCount(VariantSet::None, 0) == 1);
}

void testFaceTexturesForColumn() {
    const SymmetryInfo& col = symmetry(Symmetry::Column);
    for (Face side : {NegY, PosY, PosX, NegX}) {
        CHECK(col.repOf[side] == NegY);
        // Carried by turns about z or a half turn about x: bark stays along
        // the log's axis on every side.
        CHECK(col.repTurns[side] % 2 == 0);
    }
    CHECK(col.repOf[NegZ] == PosZ);
}

// --- PBR (the shader's own code: shaders/pbr.glsl is valid C++ with glm) ---

namespace shader {
using namespace glm;
#include "pbr.glsl"
}  // namespace shader

// Pinned variant tiles of the +z faces of cells (i, 2i, 15), i = 0…3.
constexpr std::array<std::uint32_t, 4> kPinnedRandom{0, 5, 6, 5}, kPinnedNS{1, 3, 0, 2}, kPinnedNSEW{1, 15, 2, 4};


namespace shader {
#include "variants.glsl"
}  // namespace shader

// Variant-set selection (render/block-textures): deterministic (pinned
// values, so a change to the hashes is a visible decision); `random` stays
// in range and depends on cell and face; `matched` labels agree across the
// declared edges and the tile uses only those; turns map image edges to
// face positions; the CPU path (ghosts, translucent faces) is this code.
void testVariantSelection() {
    using namespace shader;
    const glm::ivec3 up(0, 0, 1);
    // Pinned (also computed by an independent Python implementation).
    std::array<std::uint32_t, 4> rnd{}, ns{}, nsew{};
    for (int i = 0; i < 4; ++i) {
        const glm::ivec3 c(i, 2 * i, 15);
        rnd[static_cast<std::size_t>(i)] = randomTile(c, up, 8u);
        ns[static_cast<std::size_t>(i)] = matchedTile(c, up, 0u, false);
        nsew[static_cast<std::size_t>(i)] = matchedTile(c, up, 0u, true);
    }
    CHECK(rnd == kPinnedRandom && ns == kPinnedNS && nsew == kPinnedNSEW);
    // Random: in range, every tile used, and the face matters, not just the cell.
    std::array<int, 8> used{};
    int faceDiffers = 0;
    for (int x = -20; x < 20; ++x)
        for (int y = -20; y < 20; ++y) {
            const std::uint32_t t = randomTile(glm::ivec3(x, y, 3), up, 8u);
            CHECK(t < 8u);
            ++used[t];
            faceDiffers += t != randomTile(glm::ivec3(x, y, 3), glm::ivec3(0, 0, -1), 8u);
        }
    for (int n : used) CHECK(n > 0);
    CHECK(faceDiffers > 1000);
    CHECK(randomTile(glm::ivec3(4, 5, 6), up, 1u) == 0u);
    CHECK(variantTile(kSetRandom, 8u, glm::ivec3(4, 5, 6), up, 1u) == randomTile(glm::ivec3(4, 5, 6), up, 8u) &&
          variantTile(kSetMatchedNS, 1u, glm::ivec3(4, 5, 6), up, 1u) == matchedTile(glm::ivec3(4, 5, 6), up, 1u, false) &&
          variantTile(kSetConnected, 1u, glm::ivec3(4, 5, 6), up, 1u) == 0u);
    // Matched: neighbours agree on the shared declared edge, on every normal.
    for (const glm::ivec3 n : {glm::ivec3(0, 0, 1), glm::ivec3(0, 0, -1), glm::ivec3(1, 0, 0), glm::ivec3(-1, 0, 0),
                               glm::ivec3(0, 1, 0), glm::ivec3(0, -1, 0)}) {
        const glm::ivec3 u = variantFaceUp(n), r = variantFaceRight(n);
        for (int a = -30; a < 30; a += 3)
            for (int b = -30; b < 30; b += 7) {
                const glm::ivec3 c = n.z != 0 ? glm::ivec3(a, b, 5) : (n.x != 0 ? glm::ivec3(5, a, b) : glm::ivec3(a, 5, b));
                for (std::uint32_t turn = 0; turn < 4; ++turn) {
                    // Image edge e's neighbour across it is at face position (e − turn).
                    auto across = [&](std::uint32_t e) {
                        const std::uint32_t p = (e + 4u - turn) & 3u;
                        return p == 0u ? c + u : (p == 1u ? c + r : (p == 2u ? c - u : c - r));
                    };
                    // ns: this face's N label = the neighbour-across-N's S label.
                    const std::uint32_t t = matchedTile(c, n, turn, false);
                    CHECK(t < 4u);
                    CHECK((t & 1u) == ((matchedTile(across(0u), n, turn, false) >> 1u) & 1u));
                    // nsew: also E against the neighbour's W.
                    const std::uint32_t f = matchedTile(c, n, turn, true);
                    CHECK(((f & kEdgeN) != 0) == ((matchedTile(across(0u), n, turn, true) & kEdgeS) != 0));
                    CHECK(((f & kEdgeE) != 0) == ((matchedTile(across(1u), n, turn, true) & kEdgeW) != 0));
                    // ns uses exactly nsew's N and S labels.
                    CHECK(t == (((f & kEdgeN) ? 1u : 0u) | ((f & kEdgeS) ? 2u : 0u)));
                }
            }
    }
    // One CCW turn carries the image's N edge to face position W.
    CHECK(variantImageBits(8u, 1u) == kEdgeN && variantImageBits(1u, 1u) == kEdgeE && variantImageBits(1u, 0u) == kEdgeN);
    // The CPU path resolves to the same layer.
    const FaceTexture plain{10u, 2u, VariantSet::MatchedNSEW, 16u};
    CHECK(resolvePositional(plain, {7, -3, 15}, PosZ).layer == 10u + matchedTile(glm::ivec3(7, -3, 15), up, 2u, true));
    CHECK(resolvePositional(plain, {7, -3, 15}, PosZ).set == VariantSet::None);
    const FaceTexture conn{10u, 0u, VariantSet::Connected, 16u};
    CHECK(resolvePositional(conn, {7, -3, 15}, PosZ).layer == 10u);
}

void testFalloff() {
    using shader::lightFalloff;
    const float r = 5.0f;
    CHECK(lightFalloff(3.0f, 0.0f, r) == 3.0f);  // full intensity at the light (d² + 1 = 1)
    CHECK(lightFalloff(3.0f, r, r) == 0.0f);     // exactly zero at the radius…
    CHECK(lightFalloff(3.0f, r * 1.0001f, r) == 0.0f);
    CHECK(lightFalloff(3.0f, 2.0f * r, r) == 0.0f);  // …and beyond
    CHECK(lightFalloff(3.0f, 100.0f * r, r) == 0.0f);
    // Continuous at the radius: the window is (1 − x⁴)², so it approaches 0
    // quadratically — no visible edge.
    for (float eps : {1e-2f, 1e-3f, 1e-4f}) {
        const float just = lightFalloff(3.0f, r * (1.0f - eps), r);
        CHECK(just > 0.0f);
        CHECK(just < 3.0f * 16.0f * eps * eps / (r * r * (1.0f - eps) * (1.0f - eps) + 1.0f) * 1.01f);
    }
    // Monotonically non-increasing with distance.
    float prev = lightFalloff(3.0f, 0.0f, r);
    bool monotonic = true;
    for (int i = 1; i <= 1000; ++i) {
        const float f = lightFalloff(3.0f, r * 1.2f * static_cast<float>(i) / 1000.0f, r);
        monotonic = monotonic && f <= prev;
        prev = f;
    }
    CHECK(monotonic);
}

void testFresnelF0() {
    using shader::fresnelF0;
    const glm::vec3 albedo(0.9f, 0.6f, 0.2f);
    auto near = [](glm::vec3 a, float v) {
        return std::abs(a.x - v) < 1e-6f && std::abs(a.y - v) < 1e-6f && std::abs(a.z - v) < 1e-6f;
    };
    CHECK(near(fresnelF0(albedo, 0.0f, 0.0f), 0.0f));   // reflectance 0
    CHECK(near(fresnelF0(albedo, 0.0f, 0.5f), 0.04f));  // 0.16 · 0.25: the common dielectric 4 %
    CHECK(near(fresnelF0(albedo, 0.0f, 1.0f), 0.16f));
    const glm::vec3 metal = fresnelF0(albedo, 1.0f, 0.5f);  // metals reflect their albedo
    CHECK(std::abs(metal.x - 0.9f) < 1e-6f && std::abs(metal.y - 0.6f) < 1e-6f && std::abs(metal.z - 0.2f) < 1e-6f);
}

// Sanity of the full BRDF: no light from behind, energy roughly bounded, and
// a smooth surface's mirror highlight is far sharper than a rough one's.
void testShadePbr() {
    using shader::shadePbr;
    const glm::vec3 n(0, 0, 1), albedo(0.8f);
    const glm::vec3 v = glm::normalize(glm::vec3(0.3f, 0.0f, 1.0f));
    const glm::vec3 mirror(-v.x, -v.y, v.z);
    CHECK(shadePbr(n, v, glm::vec3(0, 0, -1), albedo, 0.5f, 0.0f, 0.5f) == glm::vec3(0.0f));
    const float smooth = shadePbr(n, v, mirror, albedo, 0.1f, 0.0f, 0.5f).x;
    const float rough = shadePbr(n, v, mirror, albedo, 1.0f, 0.0f, 0.5f).x;
    CHECK(smooth > 10.0f * rough);
    const glm::vec3 off = glm::normalize(glm::vec3(0.8f, 0.5f, 0.4f));
    CHECK(shadePbr(n, v, off, albedo, 0.1f, 0.0f, 0.5f).x < smooth / 10.0f);
    // A gold-like metal tints the highlight by its albedo.
    const glm::vec3 gold = shadePbr(n, v, mirror, glm::vec3(1.0f, 0.77f, 0.29f), 0.3f, 1.0f, 0.5f);
    CHECK(gold.x > gold.y && gold.y > gold.z);
}

// --- WorldQuery: the dense grid ---------------------------------------------------

const BlockRegistry& registry() {
    static const BlockRegistry reg = BlockRegistry::load(SANDBOX_ASSET_DIR "/blocks");
    return reg;
}

BlockState solidState() { return {*registry().find("stone"), orient::kIdentity}; }

DenseGrid gridWith(std::initializer_list<CellPos> solid, int size = 8) {
    DenseGrid g(registry(), size);
    EditSet e;
    for (const CellPos& c : solid) e.edits.push_back({c, solidState()});
    g.apply(e);
    return g;
}

bool near(double a, double b) { return std::abs(a - b) < 1e-9; }

void testRaycast() {
    {  // axis-aligned
        const DenseGrid g = gridWith({{5, 2, 2}});
        const auto hit = g.raycast({0.5, 2.5, 2.5}, {1, 0, 0}, 16.0);
        CHECK(hit && hit->cell == CellPos{5, 2, 2} && hit->normal == glm::ivec3(-1, 0, 0));
        CHECK(hit && near(hit->distance, 4.5) && near(hit->point.x, 5.0) && hit->state == solidState());
        CHECK(!g.raycast({0.5, 2.5, 2.5}, {1, 0, 0}, 4.4));  // beyond reach
    }
    {  // diagonal, non-degenerate: crosses x = 4 at y = 1.95 (cell y 1), then y = 2 at x = 4.1
        const DenseGrid g = gridWith({{4, 2, 0}});
        const auto hit = g.raycast({0.5, 0.2, 0.5}, {1, 0.5, 0}, 16.0);
        CHECK(hit && hit->cell == CellPos{4, 2, 0} && hit->normal == glm::ivec3(0, -1, 0));
        CHECK(hit && near(hit->point.x, 4.1) && near(hit->point.y, 2.0));
    }
    {  // diagonal through exact lattice corners: ties step x, then y, then z
        const DenseGrid g = gridWith({{3, 3, 3}});
        const auto hit = g.raycast({0.5, 0.5, 0.5}, {1, 1, 1}, 16.0);
        CHECK(hit && hit->cell == CellPos{3, 3, 3} && hit->normal == glm::ivec3(0, 0, -1));
        CHECK(hit && near(hit->distance, 2.5 * std::sqrt(3.0)));
    }
    {  // grazing an edge: the ray runs exactly along the edge y = 2, z = 2.
        // Cells are half-open, so it belongs to cells (x, 2, 2); a solid cell
        // touching the edge diagonally is not hit.
        const DenseGrid diag = gridWith({{5, 1, 1}});
        CHECK(!diag.raycast({0.5, 2.0, 2.0}, {1, 0, 0}, 16.0));
        const DenseGrid onEdge = gridWith({{5, 2, 2}});
        const auto hit = onEdge.raycast({0.5, 2.0, 2.0}, {1, 0, 0}, 16.0);
        CHECK(hit && hit->cell == CellPos{5, 2, 2} && hit->normal == glm::ivec3(-1, 0, 0));
    }
    {  // starting inside a solid cell: that cell, distance 0, no face normal
        const DenseGrid g = gridWith({{2, 2, 2}});
        const auto hit = g.raycast({2.5, 2.5, 2.5}, {0, 1, 0}, 16.0);
        CHECK(hit && hit->cell == CellPos{2, 2, 2} && hit->normal == glm::ivec3(0) && hit->distance == 0.0);
    }
    {  // leaving the grid without a hit; a ray that never enters it
        const DenseGrid g = gridWith({{2, 2, 2}});
        CHECK(!g.raycast({4.5, 4.5, 4.5}, {0, 0, 1}, 100.0));
        CHECK(!g.raycast({-2.0, 20.0, 1.0}, {1, 0, 0}, 100.0));
        CHECK(!g.raycast({4.5, 4.5, 4.5}, {0, 0, 0}, 100.0));  // no direction
    }
    {  // entering from outside, through either face, including from exactly on the far face
        const DenseGrid g = gridWith({{0, 4, 4}, {7, 5, 5}});
        const auto a = g.raycast({-3.0, 4.5, 4.5}, {1, 0, 0}, 16.0);
        CHECK(a && a->cell == CellPos{0, 4, 4} && a->normal == glm::ivec3(-1, 0, 0) && near(a->distance, 3.0));
        const auto b = g.raycast({11.0, 5.5, 5.5}, {-1, 0, 0}, 16.0);
        CHECK(b && b->cell == CellPos{7, 5, 5} && b->normal == glm::ivec3(1, 0, 0) && near(b->distance, 3.0));
        const auto c = g.raycast({8.0, 5.5, 5.5}, {-1, 0, 0}, 16.0);
        CHECK(c && c->cell == CellPos{7, 5, 5} && c->normal == glm::ivec3(1, 0, 0) && c->distance == 0.0);
        CHECK(!g.raycast({-3.0, 4.5, 4.5}, {1, 0, 0}, 2.9));  // the grid itself is out of reach
    }
}

void testGridEditsMeshEmitters() {
    DenseGrid g(registry(), 8);
    const std::uint64_t gen0 = g.generation();
    EditSet e{{{{1, 1, 1}, solidState()}, {{9, 1, 1}, solidState()}}};  // the second is outside
    const ApplyResult r1 = g.apply(e);
    CHECK(r1.changed == 1 && r1.rejected.size() == 1 && r1.rejected[0] == CellPos{9, 1, 1});
    CHECK(g.generation() == gen0 + 1);
    CHECK(g.apply(e).changed == 0);  // no change: same state again
    CHECK(g.generation() == gen0 + 1);
    // An edit that is ONLY out of bounds is rejected and changes nothing at
    // all: not the grid, not the generation.
    const WorldMesh before = g.buildMesh();
    const ApplyResult r2 = g.apply({{{{-1, 0, 0}, solidState()}, {{0, 8, 0}, solidState()}}});
    CHECK(r2.changed == 0 && r2.rejected.size() == 2);
    CHECK(g.generation() == gen0 + 1 && g.buildMesh().indices == before.indices);
    CHECK(g.probe({1, 1, 1}) == solidState() && g.probe({9, 1, 1}).isAir() && g.probe({-1, 0, 0}).isAir());

    auto faces = [](const WorldMesh& m) { return m.indices.size() / 6; };
    CHECK(faces(g.buildMesh()) == 6);
    g.apply({{{{2, 1, 1}, solidState()}}});
    CHECK(faces(g.buildMesh()) == 10);  // the shared face is culled on both sides
    g.apply({{{{0, 0, 0}, solidState()}}});
    CHECK(faces(g.buildMesh()) == 16);  // at the grid edge: outside counts as air
    const WorldMesh m = g.buildMesh();
    CHECK(m.vertices.size() == faces(m) * 4 && m.edgeLines.size() == faces(m) * 8);
    g.apply({{{{1, 1, 1}, BlockState::air()}}});
    CHECK(faces(g.buildMesh()) == 12);

    CHECK(g.emitters().empty());
    const auto lamp = registry().find("lamp");
    CHECK(lamp.has_value());
    if (lamp) {
        g.apply({{{{5, 5, 5}, BlockState{*lamp, orient::kIdentity}}}});
        const auto em = g.emitters();
        CHECK(em.size() == 1 && em[0].cell == CellPos{5, 5, 5} && em[0].emissive.x > 0.0f);
    }
}

// A block mesh face shows the texture an independent derivation predicts:
// rendering a log rotated so its axis is along x puts its END texture on ±x.
void testMeshFaceTextures() {
    const auto log = registry().find("log");
    CHECK(log.has_value());
    if (!log) return;
    DenseGrid g(registry(), 4);
    const orient::Rot alongX = registry().state(*log, rotationAbout({0, 1, 0}, 90)).orientation;
    g.apply({{{{1, 1, 1}, BlockState{*log, alongX}}}});
    const WorldMesh m = g.buildMesh();
    const BlockDef& def = registry().def(*log);
    const std::uint32_t endLayer = def.faceLayer[PosZ], sideLayer = def.faceLayer[NegY];
    CHECK(endLayer != sideLayer);
    for (std::size_t q = 0; q + 3 < m.vertices.size(); q += 4) {
        const MeshVertex& v = m.vertices[q];
        const bool xFace = std::abs(v.normal.x) > 0.5f;
        CHECK(v.layer == (xFace ? endLayer : sideLayer));
    }
}

// --- placement rule ---------------------------------------------------------------

const glm::ivec3 kNormals[6] = {{1, 0, 0}, {-1, 0, 0}, {0, 1, 0}, {0, -1, 0}, {0, 0, 1}, {0, 0, -1}};

void testPlacementInputs() {
    using namespace placement;
    CHECK(faceRight({0, 0, 1}) == glm::ivec3(1, 0, 0) && faceUp({0, 0, 1}) == glm::ivec3(0, 1, 0));
    CHECK(faceRight({1, 0, 0}) == glm::ivec3(0, 1, 0) && faceUp({1, 0, 0}) == glm::ivec3(0, 0, 1));
    CHECK(snapPerpendicular({0.2, -3.0, 5.0}, {0, 0, 1}) == glm::ivec3(0, -1, 0));
    CHECK(snapPerpendicular({5.0, 0.1, 0.3}, {1, 0, 0}) == glm::ivec3(0, 0, 1));
    CHECK(snapPerpendicular({0.0, 0.0, 5.0}, {0, 0, 1}) == glm::ivec3(1, 0, 0));   // degenerate: first ⊥ axis
    CHECK(snapPerpendicular({2.0, 2.0, 0.0}, {0, 0, 1}) == glm::ivec3(1, 0, 0));   // exact tie: x first
    const glm::dvec2 uv = faceCoords({0.4, 0.4, 0.0}, {0, 0, 1});
    CHECK(near(uv.x, 0.9) && near(uv.y, 0.9));
}

// For every (n, f, uv) combination: the placed rotation's feature lands
// where the rule says, and the number of distinct canonical results is the
// type's orientation count.
void testPlacementRule() {
    using placement::placementRotation;
    using placement::placeOrientation;
    const Group& g = group();
    const glm::dvec2 uvs[] = {{0.1, 0.1}, {0.9, 0.1}, {0.1, 0.9}, {0.9, 0.9}, {0.5, 0.05}, {0.95, 0.5}, {0.5, 0.95}, {0.05, 0.5}};
    struct Case {
        Symmetry s;
        std::size_t classes;
    };
    for (const Case c : {Case{Symmetry::Uniform, 1}, {Symmetry::Column, 3}, {Symmetry::Pointed, 6}, {Symmetry::Brick, 6},
                         {Symmetry::TwistedColumn, 6}, {Symmetry::PointedBrick, 12}, {Symmetry::Oriented, 24},
                         {Symmetry::Corner, 8}, {Symmetry::Diagonal, 4}, {Symmetry::Tetrahedral, 2}, {Symmetry::Edge, 12}}) {
        std::set<Rot> seen;
        for (const glm::ivec3 n : kNormals)
            for (const glm::ivec3 f : kNormals) {
                if (f == n || f == -n) continue;
                for (const glm::dvec2 uv : uvs) {
                    const Rot r = placementRotation(c.s, n, f, uv);
                    const Mat3i& m = g.matrix[r];
                    const glm::ivec3 R = placement::faceRight(n), U = placement::faceUp(n);
                    switch (c.s) {
                        case Symmetry::Uniform: CHECK(r == kIdentity); break;
                        case Symmetry::Column:
                        case Symmetry::Pointed: CHECK(rotateVec(m, {0, 0, 1}) == n); break;
                        case Symmetry::Corner:
                        case Symmetry::Diagonal:
                        case Symmetry::Tetrahedral:
                            CHECK(rotateVec(m, {1, -1, 1}) == -n + R * (uv.x >= 0.5 ? 1 : -1) + U * (uv.y >= 0.5 ? 1 : -1));
                            break;
                        case Symmetry::Edge: {
                            const glm::ivec3 e = rotateVec(m, {0, -1, 1});
                            CHECK(e.x * n.x + e.y * n.y + e.z * n.z == -1);  // an edge of the clicked face
                            break;
                        }
                        default:  // up along n, front toward the player
                            CHECK(rotateVec(m, {0, 0, 1}) == n && rotateVec(m, {0, -1, 0}) == f);
                    }
                    seen.insert(placeOrientation(c.s, n, f, uv));
                }
            }
        CHECK(seen.size() == c.classes);
    }
    // Spot checks: logs lie along the clicked normal (z, y, x axes → 0, 8, 16).
    CHECK(placeOrientation(Symmetry::Column, {0, 0, 1}, {0, -1, 0}, {0.5, 0.5}) == 0);
    CHECK(placeOrientation(Symmetry::Column, {0, 1, 0}, {1, 0, 0}, {0.5, 0.5}) == 8);
    CHECK(placeOrientation(Symmetry::Column, {-1, 0, 0}, {0, 0, 1}, {0.5, 0.5}) == 16);
    // A furnace on the floor faces the player; on a wall it lies on its back
    // (top out of the wall), front toward the player (the rule as written).
    CHECK(placeOrientation(Symmetry::Oriented, {0, 0, 1}, {0, -1, 0}, {0.5, 0.5}) == 0);
    CHECK(placeOrientation(Symmetry::Oriented, {0, 0, 1}, {1, 0, 0}, {0.5, 0.5}) == rotationAbout({0, 0, 1}, 90));
    const Rot wall = placeOrientation(Symmetry::Oriented, {0, -1, 0}, {0, 0, 1}, {0.5, 0.5});
    CHECK(rotateVec(g.matrix[wall], {0, 0, 1}) == glm::ivec3(0, -1, 0));
    CHECK(rotateVec(g.matrix[wall], {0, -1, 0}) == glm::ivec3(0, 0, 1));
    // Edge: nearest edge of the clicked +z face to a hit near its −U (−y) side.
    const Rot edge = placementRotation(Symmetry::Edge, {0, 0, 1}, {0, -1, 0}, {0.5, 0.05});
    CHECK(rotateVec(g.matrix[edge], {0, -1, 1}) == glm::ivec3(0, -1, -1));
}

// --- hands, hotbars, use repeat --------------------------------------------------

Hotbar threeSlots() { return Hotbar({Item::blockStack(1), Item::blockStack(2), Item::blockStack(3)}); }

void testHotbar() {
    Hotbar h = threeSlots();
    CHECK(h.selected() == 0 && h.held() && h.held()->block == 1);
    h.scroll(-1);
    CHECK(h.selected() == 2);  // wraps backward
    h.scroll(+1);
    CHECK(h.selected() == 0);  // and forward
    h.select(1);
    h.setBare();
    CHECK(h.isBare() && !h.held() && h.lastSelected() == 1);
    h.scroll(+1);
    CHECK(h.selected() == 1);  // the wheel resumes at the last selected slot (bare is not a stop)
    h.setBare();
    h.select(2);
    CHECK(h.selected() == 2);  // selecting a slot leaves bare
    h.select(7);
    CHECK(h.selected() == 2);  // no slot 8: ignored
    h.pick(3);
    CHECK(h.selected() == 2);  // already on the hotbar: just select it
    h.pick(9);
    CHECK(h.selected() == 2 && h.slots()[2].block == 9);  // not on it: replaces the selected slot
    h.select(0);
    h.setBare();
    h.pick(8);
    CHECK(h.selected() == 0 && h.slots()[0].block == 8);  // bare: replaces the last selected slot
    CHECK(h.slots().size() == 3);

    Hands hands;  // right-handed
    CHECK(hands.main() == Hand::Right && hands.off() == Hand::Left);
    hands.bar(Hand::Right) = threeSlots();
    hands.bar(Hand::Right).select(2);
    hands.bar(Hand::Left) = Hotbar({Item::pick()});
    hands.swapHotbars();  // each hotbar keeps its selection
    CHECK(hands.bar(Hand::Left).selected() == 2 && hands.bar(Hand::Left).held()->block == 3);
    CHECK(hands.bar(Hand::Right).held()->kind == Item::Kind::Pick);
}

void testUseRepeater() {
    UseRepeater r;
    CHECK(r.update(true, true, 0.0) == 1);    // fires on press
    CHECK(r.update(true, false, 0.2) == 0);   // still inside the 250 ms delay
    CHECK(r.update(true, false, 0.25) == 1);  // first repeat at 250 ms
    CHECK(r.update(true, false, 0.40) == 0);
    CHECK(r.update(true, false, 0.45) == 1);  // then every 200 ms
    CHECK(r.update(true, false, 0.86) == 2);  // a long frame catches up (repeats due at 0.65 and 0.85)
    CHECK(r.update(false, false, 0.9) == 0);  // released
    CHECK(r.update(true, false, 1.5) == 0);   // held without a press we saw: nothing
}

// --- intention layer ------------------------------------------------------------------

// v0.1.8: F swaps the hotbars; R is unbound.
void testSwapKey() {
    IntentionLayer layer(Hand::Right);
    layer.push({Device::Key, GLFW_KEY_R, true, false});
    CHECK(layer.process({}).fired.empty());
    layer.push({Device::Key, GLFW_KEY_F, true, false});
    const IntentionFrame f = layer.process({});
    CHECK(f.fired.size() == 1 && f.fired[0].kind == IntentionKind::SwapHotbars);
}

// v0.1.8: free flight is camera-relative; C/V move along the camera's up
// (right × forward), perpendicular to the look direction.
void testCameraRelativeAscend() {
    Camera cam;
    cam.yaw = 0.0;      // looking along +x …
    cam.pitch = -1.0;   // … and down
    const glm::dvec3 u = cam.up();
    CHECK(std::abs(glm::dot(u, cam.forward())) < 1e-12 && std::abs(glm::dot(u, cam.right())) < 1e-12);
    CHECK(std::abs(glm::length(u) - 1.0) < 1e-12);
    const WorldPos start = cam.position;
    cam.update({.up = 1.0}, 0.5);
    const glm::dvec3 d = cam.position - start;
    CHECK(d.x > 0.0 && d.z > 0.0 && std::abs(d.y) < 1e-12);  // forward-and-up in the world, not straight up
    CHECK(std::abs(glm::dot(glm::normalize(d), cam.forward())) < 1e-12);
    cam.pitch = 0.0;  // level: up is world up
    CHECK(glm::length(cam.up() - glm::dvec3(0, 0, 1)) < 1e-12);
}

void testIntentions() {
    IntentionLayer layer(Hand::Right);
    auto press = [&](Device d, int code, bool shift = false) { layer.push({d, code, true, shift}); };
    auto release = [&](Device d, int code) { layer.push({d, code, false, false}); };

    // Shift routing: 1–9 and the wheel are main-hand; Shift+ is the off hand.
    press(Device::Key, GLFW_KEY_3);
    press(Device::Key, GLFW_KEY_3, true);
    press(Device::Wheel, -1);
    press(Device::Wheel, +1, true);
    press(Device::Key, GLFW_KEY_H, true);
    press(Device::MouseButton, GLFW_MOUSE_BUTTON_RIGHT);
    press(Device::MouseButton, GLFW_MOUSE_BUTTON_LEFT, true);
    press(Device::Key, GLFW_KEY_F2, true);
    press(Device::Key, GLFW_KEY_E);  // unbound
    IntentionFrame f = layer.process({});
    CHECK(f.fired.size() == 8);
    if (f.fired.size() == 8) {
        CHECK(f.fired[0].kind == IntentionKind::SelectSlot && f.fired[0].hand == Hand::Right && f.fired[0].arg == 2);
        CHECK(f.fired[1].kind == IntentionKind::SelectSlot && f.fired[1].hand == Hand::Left && f.fired[1].arg == 2);
        CHECK(f.fired[2].kind == IntentionKind::Scroll && f.fired[2].hand == Hand::Right && f.fired[2].arg == +1);
        CHECK(f.fired[3].kind == IntentionKind::Scroll && f.fired[3].hand == Hand::Left && f.fired[3].arg == -1);
        CHECK(f.fired[4].kind == IntentionKind::Bare && f.fired[4].hand == Hand::Left);
        CHECK(f.fired[5].kind == IntentionKind::Use && f.fired[5].hand == Hand::Right && !f.fired[5].secondary);
        CHECK(f.fired[6].kind == IntentionKind::Use && f.fired[6].hand == Hand::Left && f.fired[6].secondary);
        CHECK(f.fired[7].kind == IntentionKind::ToggleOverlay && f.fired[7].arg == 2 && f.fired[7].secondary);
    }
    CHECK(f.useHeld[1] && !f.useHeld[0]);  // RMB primary held; LMB was a secondary
    CHECK(f.contexts.size() == 1 && f.contexts[0] == "gameplay");

    // Continuous intentions from held keys; Shift doesn't block them.
    press(Device::Key, GLFW_KEY_W, true);
    press(Device::Key, GLFW_KEY_A);
    press(Device::Key, GLFW_KEY_C);
    press(Device::Key, GLFW_KEY_LEFT_CONTROL);
    f = layer.process({});
    CHECK(f.fired.empty() && f.forward == 1.0 && f.right == -1.0 && f.up == 1.0 && f.fast);

    // The dev-UI context: while ImGui wants the keyboard, held keys stop
    // driving movement and new key presses are swallowed; the mouse still works.
    press(Device::Key, GLFW_KEY_F);
    press(Device::MouseButton, GLFW_MOUSE_BUTTON_MIDDLE);
    f = layer.process({.devUiWantsMouse = false, .devUiWantsKeyboard = true});
    CHECK(f.forward == 0.0 && !f.fast && f.contexts.size() == 2 && f.contexts[1] == "dev-ui (keyboard)");
    CHECK(f.fired.size() == 1 && f.fired[0].kind == IntentionKind::PickBlock);
    // Releases pass through even while swallowed, so nothing stays held.
    release(Device::Key, GLFW_KEY_W);
    release(Device::MouseButton, GLFW_MOUSE_BUTTON_RIGHT);
    f = layer.process({.devUiWantsMouse = true, .devUiWantsKeyboard = true});
    f = layer.process({});
    CHECK(f.forward == 0.0 && f.right == -1.0 && !f.useHeld[1]);
    // A mouse press while ImGui wants the mouse fires nothing and isn't held.
    press(Device::MouseButton, GLFW_MOUSE_BUTTON_LEFT);
    f = layer.process({.devUiWantsMouse = true});
    CHECK(f.fired.empty());
    f = layer.process({});
    CHECK(!f.useHeld[0]);
}

// --- translucency: boundary-integral absorbance (the shader's own code) ---

namespace oit {
using namespace glm;
#include "oit.glsl"
}  // namespace oit

// The face fragments one ray meets, as (σ, eye distance, front-facing).
struct Crossing {
    float sigma;
    float r;
    bool front;
};

float absorbanceAlong(std::initializer_list<Crossing> crossings, float rOpaque) {
    float sum = 0.0f;
    for (const Crossing& c : crossings) sum += oit::oitBoundaryTerm(c.sigma, c.r, rOpaque, c.front).b0;
    return sum;
}

bool approx(float a, float b, float tol = 1e-4f) { return std::abs(a - b) <= tol * std::max(1.0f, std::abs(b)); }

void testBoundaryTerms() {
    const float kNoWall = 1000.0f;
    // One block of σ = 0.5 from r = 4 to 5: σ × 1.
    CHECK(approx(absorbanceAlong({{0.5f, 4.0f, true}, {0.5f, 5.0f, false}}, kNoWall), 0.5f));
    // Nested: medium a (σ 0.3) from 2 to 8 with medium b (σ 0.9) from 4 to 5
    // inside it. The boundary of b is two coplanar pairs: leaving a (+σa)
    // + entering b (−σb) at 4, leaving b (+σb) + entering a (−σa) at 5.
    CHECK(approx(absorbanceAlong({{0.3f, 2.0f, true},
                                  {0.3f, 4.0f, false}, {0.9f, 4.0f, true},
                                  {0.9f, 5.0f, false}, {0.3f, 5.0f, true},
                                  {0.3f, 8.0f, false}},
                                 kNoWall),
                 0.3f * 2.0f + 0.9f * 1.0f + 0.3f * 3.0f));
    // Adjacent tints: a (σ 0.4) 3→4 then b (σ 1.2) 4→6: the interface pair
    // sums to (σa − σb)·r at r = 4.
    CHECK(approx(absorbanceAlong({{0.4f, 3.0f, true}, {0.4f, 4.0f, false}, {1.2f, 4.0f, true}, {1.2f, 6.0f, false}},
                                 kNoWall),
                 0.4f + 1.2f * 2.0f));
    // A run of 3 has no internal faces (same group): just 3σ.
    CHECK(approx(absorbanceAlong({{0.5f, 4.0f, true}, {0.5f, 7.0f, false}}, kNoWall), 1.5f));
    // Wall-clamped: the medium runs 3→9 but an opaque surface is at 5, so
    // the path ends there; a volume wholly behind the wall (6→8) cancels.
    CHECK(approx(absorbanceAlong({{0.7f, 3.0f, true}, {0.7f, 9.0f, false}}, 5.0f), 0.7f * 2.0f));
    CHECK(approx(absorbanceAlong({{0.7f, 6.0f, true}, {0.7f, 8.0f, false}}, 5.0f), 0.0f, 1e-6f));
    // Glass resting on stone: the exit face lies exactly at the wall.
    CHECK(approx(absorbanceAlong({{0.7f, 4.0f, true}, {0.7f, 5.0f, false}}, 5.0f), 0.7f));
    // Eye inside the medium: only the exit, measured from the near bound.
    CHECK(approx(absorbanceAlong({{0.5f, 3.0f, false}}, kNoWall), 0.5f * (3.0f - oit::kOitNear)));
    // The sum commutes (up to float rounding): reorder the nested case.
    CHECK(approx(absorbanceAlong({{0.3f, 8.0f, false}, {0.9f, 5.0f, false}, {0.3f, 2.0f, true},
                                  {0.3f, 5.0f, true}, {0.9f, 4.0f, true}, {0.3f, 4.0f, false}},
                                 kNoWall),
                 0.3f * 2.0f + 0.9f + 0.3f * 3.0f));
}

// F_k really is the antiderivative of w^k: compare with Simpson's rule.
void testMomentAntiderivatives() {
    for (const auto& [a, b] : {std::pair{0.2f, 0.9f}, {2.0f, 3.5f}, {7.0f, 40.0f}}) {
        const oit::OitMoments fa = oit::oitAntiderivative(a), fb = oit::oitAntiderivative(b);
        const double got[5] = {fb.b0 - fa.b0, fb.b.x - fa.b.x, fb.b.y - fa.b.y, fb.b.z - fa.b.z, fb.b.w - fa.b.w};
        constexpr int n = 2000;  // even
        double want[5] = {};
        for (int i = 0; i <= n; ++i) {
            const double r = static_cast<double>(a) + static_cast<double>(b - a) * i / n;
            const double w = static_cast<double>(oit::oitWarp(static_cast<float>(r)));
            const double weight = (i == 0 || i == n) ? 1.0 : (i % 2 ? 4.0 : 2.0);
            double p = 1.0;
            for (int k = 0; k < 5; ++k, p *= w) want[k] += weight * p;
        }
        for (int k = 0; k < 5; ++k) {
            want[k] *= static_cast<double>(b - a) / (3.0 * n);
            CHECK(std::abs(got[k] - want[k]) <= 2e-3 * std::max(1.0, std::abs(want[k])));
        }
    }
    CHECK(near(oit::oitWarp(oit::kOitNear), -1.0) && std::abs(oit::oitWarp(oit::kOitFar) - 1.0f) < 1e-5f);
}

// The reconstruction on a single slab of medium: in front of the slab the
// normalized absorbance is ~0, behind it ~1, and it rises monotonically
// through it (MBOIT is approximate, so the tolerances are loose).
void testMomentReconstruction() {
    const float sigma = 0.8f, r0 = 4.0f, r1 = 6.0f;
    const oit::OitMoments f0 = oit::oitAntiderivative(r0), f1 = oit::oitAntiderivative(r1);
    const float b0 = sigma * (f1.b0 - f0.b0);
    const glm::vec4 b = (f1.b - f0.b) * sigma / b0;
    CHECK(approx(b0, sigma * 2.0f));
    auto A = [&](float r) { return oit::oitNormalizedAbsorbance(b, oit::oitWarp(r), 5e-4f, 0.25f); };
    CHECK(A(2.0f) < 0.15f);
    CHECK(A(12.0f) > 0.85f);
    float prev = -1.0f;
    bool monotonic = true;
    for (float r = 2.0f; r <= 12.0f; r += 0.25f) {
        monotonic = monotonic && A(r) >= prev - 1e-4f;
        prev = A(r);
    }
    CHECK(monotonic);
}

// Face ordering for the order-invariance test: a permutation of whole faces,
// sorted as asked, seeded shuffles reproducible.
void testFaceOrder() {
    const auto glass = registry().find("glass_red");
    CHECK(glass.has_value());
    if (!glass) return;
    DenseGrid g(registry(), 8);
    g.apply({{{{1, 1, 1}, {*glass, kIdentity}}, {{5, 1, 1}, {*glass, kIdentity}}, {{3, 5, 2}, {*glass, kIdentity}}}});
    const WorldMesh base = g.buildMesh();
    CHECK(base.indices.empty() && base.translucentIndices.size() == 3 * 6 * 6);  // glass is translucent only
    auto faces = [](const WorldMesh& m) {
        std::multiset<std::array<std::uint32_t, 6>> out;
        for (std::size_t f = 0; f < m.translucentIndices.size() / 6; ++f) {
            std::array<std::uint32_t, 6> q;
            for (std::size_t k = 0; k < 6; ++k) q[k] = m.translucentIndices[6 * f + k];
            out.insert(q);
        }
        return out;
    };
    const WorldPos eye{0.0, 0.0, 0.0};
    auto centerDist = [&](const WorldMesh& m, std::size_t f) {
        glm::dvec3 c(0.0);
        for (std::size_t k = 0; k < 6; ++k) c += glm::dvec3(m.translucentVertices[m.translucentIndices[6 * f + k]].pos);
        c /= 6.0;
        return c.x * c.x + c.y * c.y + c.z * c.z;
    };
    for (const char* spec : {"forward", "reverse", "shuffle:7"}) {
        WorldMesh m = base;
        orderTranslucentFaces(m, *FaceOrder::parse(spec), eye);
        CHECK(faces(m) == faces(base));  // a permutation of whole faces
        if (std::string(spec) != "shuffle:7") {
            bool sorted = true;
            for (std::size_t f = 1; f < m.translucentIndices.size() / 6; ++f) {
                const double a = centerDist(m, f - 1), b = centerDist(m, f);
                sorted = sorted && (std::string(spec) == "forward" ? a <= b : a >= b);
            }
            CHECK(sorted);
        }
    }
    WorldMesh s1 = base, s2 = base, s3 = base;
    orderTranslucentFaces(s1, *FaceOrder::parse("shuffle:7"), eye);
    orderTranslucentFaces(s2, *FaceOrder::parse("shuffle:7"), eye);
    orderTranslucentFaces(s3, *FaceOrder::parse("shuffle:8"), eye);
    CHECK(s1.translucentIndices == s2.translucentIndices && s1.translucentIndices != s3.translucentIndices);
    CHECK(!FaceOrder::parse("sideways") && !FaceOrder::parse("shuffle:") && !FaceOrder::parse("shuffle:x"));
}

// The face rule for translucent cells: same-group neighbours share no faces;
// different tints and opaque neighbours keep both sides' faces.
void testTranslucentFaceRule() {
    const auto red = registry().find("glass_red"), blue = registry().find("glass_blue"), glow = registry().find("glow");
    CHECK(red && blue && glow);
    if (!red || !blue || !glow) return;
    DenseGrid g(registry(), 8);
    auto put = [&](CellPos c, BlockTypeId t) { g.apply({{{c, {t, kIdentity}}}}); };
    put({1, 1, 1}, *red);
    put({2, 1, 1}, *red);  // a run of one tint: 10 faces, no internal pair
    CHECK(g.buildMesh().translucentIndices.size() / 6 == 10);
    put({3, 1, 1}, *blue);  // another tint: the interface is two coplanar faces
    CHECK(g.buildMesh().translucentIndices.size() / 6 == 10 + 6);
    put({1, 1, 0}, solidState().type);  // glass on stone keeps its face; stone keeps its face to the glass
    const WorldMesh m = g.buildMesh();
    CHECK(m.translucentIndices.size() / 6 == 16 && m.indices.size() / 6 == 6);
    // Emitters include translucent blocks with emission.
    put({6, 6, 6}, *glow);
    bool found = false;
    for (const Emitter& e : g.emitters()) found = found || (e.cell == CellPos{6, 6, 6} && e.emissive.x > 0.0f);
    CHECK(found);
}

// The mip chain (render/block-textures): complete (16 → 1) for every
// shipped layer, each level a linear-light box filter, the 1×1 level the
// face's mean colour; alpha averages linearly.
void testMipChain() {
    for (const Image& layer : registry().layers()) {
        CHECK(layer.width == kTextureSize && layer.height == kTextureSize);
        const std::vector<Image> chain = buildMipChain(layer);
        CHECK(chain.size() == 5);
        for (std::size_t m = 0; m < chain.size(); ++m)
            CHECK(chain[m].width == (kTextureSize >> m) && chain[m].height == (kTextureSize >> m) &&
                  chain[m].rgba.size() == static_cast<std::size_t>(chain[m].width) * chain[m].height * 4);
    }
    // Half black, half white: the mean in linear light is 0.5, which is
    // sRGB 188 (not 128, the naive average of the encoded values).
    Image half = solid(16);
    for (std::uint32_t y = 0; y < 16; ++y)
        for (std::uint32_t x = 0; x < 16; ++x) {
            setPixel(half, x, y, x < 8 ? 0 : 255);
            half.rgba[(y * 16 + x) * 4 + 3] = x < 8 ? 0 : 255;
        }
    const Image last = buildMipChain(half).back();
    CHECK(last.width == 1 && (last.rgba[0] == 187 || last.rgba[0] == 188) && (last.rgba[3] == 127 || last.rgba[3] == 128));
}

// Connected-glass frames (v0.1.8: a connected variant set): the mesher's
// frame bits pick the set's tile, so a run of one tint is framed only along
// its outline; interface pairs are framed once; faces against opaque cells
// use tile 0 (fully transparent). Tiles are in the image's frame (kEdgeN …
// kEdgeW), carried from the face frame by the face's turn.
void testGlassFrames() {
    const auto red = registry().find("glass_red"), blue = registry().find("glass_blue");
    CHECK(red && blue);
    if (!red || !blue) return;
    const BlockDef& redDef = registry().def(*red);
    CHECK(redDef.faceSet[PosZ] == VariantSet::Connected);
    DenseGrid g(registry(), 8);
    auto put = [&](CellPos c, BlockTypeId t) { g.apply({{{c, {t, kIdentity}}}}); };
    // The tile index of the translucent face of `cell` toward n.
    auto tile = [&](const WorldMesh& m, CellPos cell, glm::vec3 n) -> std::optional<std::uint32_t> {
        const glm::vec3 lo(static_cast<float>(cell.x), static_cast<float>(cell.y), static_cast<float>(cell.z));
        for (std::size_t q = 0; q < m.translucentVertices.size(); q += 4) {
            glm::vec3 c(0.0f);
            for (std::size_t k = 0; k < 4; ++k) c += m.translucentVertices[q + k].pos * 0.25f;
            const MeshVertex& v = m.translucentVertices[q];
            if (v.normal == n && glm::all(glm::lessThan(glm::abs(c - (lo + glm::vec3(0.5f) + n * 0.5f)), glm::vec3(1e-4f)))) {
                const BlockState st = g.probe(cell);
                const Face f = n.z > 0.5f ? PosZ : n.z < -0.5f ? NegZ : n.x > 0.5f ? PosX : n.x < -0.5f ? NegX : n.y > 0.5f ? PosY : NegY;
                return v.layer - faceTexture(registry(), st, f).layer;
            }
        }
        return std::nullopt;
    };
    // The +z face of a uniform block: R = +x, U = +y; its turn must be 0
    // for the expectations below to be in the image frame directly.
    CHECK(faceTexture(registry(), {*red, kIdentity}, PosZ).turn == 0u);
    // A run of 3 along x: the top faces are framed on their outer edges only.
    put({1, 1, 1}, *red);
    put({2, 1, 1}, *red);
    put({3, 1, 1}, *red);
    WorldMesh m = g.buildMesh();
    CHECK(tile(m, {1, 1, 1}, {0, 0, 1}) == (kEdgeW | kEdgeN | kEdgeS));
    CHECK(tile(m, {2, 1, 1}, {0, 0, 1}) == (kEdgeN | kEdgeS));  // the middle: no edges along the run
    CHECK(tile(m, {3, 1, 1}, {0, 0, 1}) == (kEdgeE | kEdgeN | kEdgeS));
    // A step up at x = 3 makes a crease: the edge between (2) and (3)'s tops is framed.
    put({3, 1, 2}, *red);
    m = g.buildMesh();
    CHECK(tile(m, {2, 1, 1}, {0, 0, 1}) == (kEdgeE | kEdgeN | kEdgeS));
    // A different tint beside the run: the interface pair is framed once
    // (lower type id carries it).
    put({4, 1, 1}, *blue);
    m = g.buildMesh();
    const auto onRed = tile(m, {3, 1, 1}, {1, 0, 0}), onBlue = tile(m, {4, 1, 1}, {-1, 0, 0});
    CHECK(onRed && onBlue && ((*onRed != 0) != (*onBlue != 0)));
    // Against opaque: the face exists (it closes the volume) but uses tile 0.
    put({1, 1, 0}, solidState().type);
    m = g.buildMesh();
    CHECK(tile(m, {1, 1, 1}, {0, 0, -1}) == 0u);
    // Turns: a face turned once CCW shows its image N edge at face position W.
    CHECK(connectedTile(16u | 1u, 1) == kEdgeN);  // framed −R (W) → image N after one CCW turn
    CHECK(connectedTile(16u | 8u, 0) == kEdgeN);  // framed +U = N, unturned
    CHECK(connectedTile(15u, 0) == 0u);          // no surface layer
}

// --- v0.1.7: tools as pure edit-set functions -------------------------------------

BlockTypeId typeOf(const char* name) { return *registry().find(name); }

std::optional<ToolTarget> aim(const WorldQuery& w, WorldPos eye, WorldPos at) {
    const auto hit = w.raycast(eye, at - eye, 64.0);
    if (!hit) return std::nullopt;
    return ToolTarget{*hit, eye};
}

// planEdit is pure: identical results on repeated calls, world untouched;
// and committing applies exactly the planned states.
void checkPureAndCommits(DenseGrid& w, const Item* item, bool secondary, const std::optional<ToolTarget>& t,
                         const ToolContext& ctx) {
    const std::uint64_t gen = w.generation();
    const WorldMesh before = w.buildMesh();
    const PlannedEdit a = planEdit(item, secondary, t, ctx, w);
    const PlannedEdit b = planEdit(item, secondary, t, ctx, w);
    CHECK(a.edits == b.edits && a.action == b.action);
    CHECK(w.generation() == gen && w.buildMesh().indices == before.indices);  // compute doesn't modify the world
    DenseGrid committed = w;  // the click path: apply(planEdit(...))
    const ApplyResult r = committed.apply(a.edits);
    CHECK(r.rejected.empty());  // planned edits are always editable
    for (const Edit& e : a.edits.edits) CHECK(committed.probe(e.cell) == e.state);
}

void testToolsPureAndCommit() {
    DenseGrid w(registry(), 12);
    EditSet ground;
    for (int y = 0; y < 12; ++y)
        for (int x = 0; x < 12; ++x)
            for (int z = 0; z < 3; ++z) ground.edits.push_back({{x, y, z}, {typeOf(z == 2 ? "grass" : "dirt"), kIdentity}});
    ground.edits.push_back({{6, 6, 3}, {typeOf("furnace"), kIdentity}});
    w.apply(ground);
    ToolSettings settings;
    settings.shovelFill = typeOf("dirt");
    settings.container.slots = {{{typeOf("brick"), 6}, {typeOf("stone"), 3}, {typeOf("dirt"), 1}}};
    OrientationLatch latch;
    const ToolContext ctx{registry(), settings, latch};
    const Item items[] = {Item::blockStack(typeOf("log")), Item::blockStack(typeOf("furnace")), Item::pick(),
                          Item::wrench(), Item::shovel(), Item::container()};
    const WorldPos eyes[] = {{3.3, 2.7, 7.1}, {9.8, 4.2, 6.4}, {6.5, 1.5, 4.5}};
    const WorldPos ats[] = {{6.5, 6.5, 3.5}, {4.2, 5.1, 3.0}, {6.5, 6.5, 2.9}};
    for (const Item& item : items)
        for (const WorldPos& eye : eyes)
            for (const WorldPos& at : ats)
                for (bool secondary : {false, true}) checkPureAndCommits(w, &item, secondary, aim(w, eye, at), ctx);
    checkPureAndCommits(w, nullptr, false, aim(w, eyes[0], ats[0]), ctx);  // bare hand
    CHECK(planEdit(&items[0], false, std::nullopt, ctx, w).edits.edits.empty());  // nothing in reach
}

// The shovel on stepped soft ground with rock in it.
void testShovel() {
    DenseGrid w(registry(), 12);
    const BlockTypeId dirt = typeOf("dirt"), stone = typeOf("stone");
    EditSet e;
    for (int y = 0; y < 12; ++y)
        for (int x = 0; x < 12; ++x)
            for (int z = 0; z <= 3; ++z) e.edits.push_back({{x, y, z}, {dirt, kIdentity}});
    // A step: columns x ≥ 7 rise to z = 5 (two soft cells above h = 3) …
    for (int y = 0; y < 12; ++y)
        for (int x = 7; x < 12; ++x)
            for (int z = 4; z <= 5; ++z) e.edits.push_back({{x, y, z}, {dirt, kIdentity}});
    // … with a rock on that step inside the footprint, and a 2-deep hole.
    e.edits.push_back({{7, 5, 6}, {stone, kIdentity}});
    e.edits.push_back({{4, 4, 3}, BlockState::air()});
    e.edits.push_back({{4, 4, 2}, BlockState::air()});
    w.apply(e);
    ToolSettings settings;
    settings.shovelFill = dirt;
    OrientationLatch latch;
    const ToolContext ctx{registry(), settings, latch};
    const Item shovel = Item::shovel();
    // Aim at the top of (5, 5, 3): h = 3, columns x 3..7, y 3..7.
    const auto t = aim(w, {5.5, 5.5, 9.0}, {5.5, 5.5, 3.5});
    CHECK(t && t->hit.cell == CellPos{5, 5, 3});
    const PlannedEdit p = planEdit(&shovel, false, t, ctx, w);
    CHECK(p.cut == 5 * 2);   // column x = 7 (5 rows) has two soft cells above h
    CHECK(p.blocked == 1);   // the rock at (7, 5, 6) is left alone
    CHECK(p.fill == 2);      // the hole at (4, 4): z = 3 and 2, down to the first solid
    bool rockKept = true;
    for (const Edit& x : p.edits.edits) rockKept = rockKept && !(x.cell == CellPos{7, 5, 6});
    CHECK(rockKept);
    settings.shovelFill = stone;  // rock isn't a shovel fill
    CHECK(planEdit(&shovel, false, t, ctx, w).fill == 0);
}

void testWrench() {
    const Group& g = group();
    for (int r = 0; r < kRotCount; ++r)
        for (const glm::ivec3 n : kNormals) {
            const Rot cw = turnAbout(static_cast<Rot>(r), n, false);
            CHECK(turnAbout(cw, n, true) == r);  // CW then CCW is identity
            Rot four = static_cast<Rot>(r);
            for (int k = 0; k < 4; ++k) four = turnAbout(four, n, false);
            CHECK(four == r);  // four CW clicks
        }
    // Clockwise as seen looking at the face: on the +z face, +x goes to −y.
    CHECK(rotateVec(g.matrix[turnAbout(kIdentity, {0, 0, 1}, false)], {1, 0, 0}) == glm::ivec3(0, -1, 0));
    // A click about a log's own axis is a no-op once canonicalized.
    DenseGrid w(registry(), 6);
    const BlockTypeId log = typeOf("log");
    w.apply({{{{2, 2, 2}, {log, kIdentity}}}});  // axis along z
    ToolSettings settings;
    OrientationLatch latch;
    const ToolContext ctx{registry(), settings, latch};
    const Item wrench = Item::wrench();
    const PlannedEdit top = planEdit(&wrench, false, aim(w, {2.5, 2.5, 5.5}, {2.5, 2.5, 2.5}), ctx, w);
    CHECK(top.edits.edits.size() == 1 && top.edits.edits[0].state == BlockState{log, kIdentity});
    const PlannedEdit side = planEdit(&wrench, false, aim(w, {5.5, 2.5, 2.5}, {2.5, 2.5, 2.5}), ctx, w);
    CHECK(side.edits.edits.size() == 1 && side.edits.edits[0].state.orientation != kIdentity);
}

void testContainerModes() {
    DenseGrid w(registry(), 40);
    EditSet floor;
    for (int y = 0; y < 40; ++y)
        for (int x = 0; x < 40; ++x) floor.edits.push_back({{x, y, 0}, {typeOf("stone"), kIdentity}});
    w.apply(floor);
    const BlockTypeId brick = typeOf("brick"), stone = typeOf("stone"), dirt = typeOf("dirt");
    ToolSettings settings;
    settings.container.slots = {{{brick, 60}, {stone, 30}, {dirt, 10}}};
    OrientationLatch latch;
    const ToolContext ctx{registry(), settings, latch};
    const Item box = Item::container();
    const WorldPos center{20.5, 20.5, 1.0};
    const CellPos above{20, 20, 1};
    // One block per click, against the clicked face; pure for a fixed state.
    for (int m = 0; m < kPlacementModeCount; ++m) {
        settings.container.mode = static_cast<PlacementMode>(m);
        const PlannedEdit a = planEdit(&box, false, aim(w, {20.4, 18.0, 9.0}, center), ctx, w);
        const PlannedEdit b = planEdit(&box, false, aim(w, {20.4, 18.0, 9.0}, center), ctx, w);
        const PlannedEdit c = planEdit(&box, false, aim(w, {25.0, 26.0, 6.0}, center), ctx, w);  // the camera moved
        CHECK(a.edits == b.edits && a.edits == c.edits);
        CHECK(a.edits.edits.size() == 1 && a.edits.edits[0].cell == above);
    }
    // The randomness is temporal: the pick in ONE cell, over successive
    // draws. `draws` advances only when a placement changed the world.
    auto pickAt = [&](std::uint64_t draws) {
        settings.container.draws = draws;
        const PlannedEdit p = planEdit(&box, false, aim(w, {20.5, 20.5, 9.0}, center), ctx, w);
        return p.edits.edits.empty() ? BlockState::kAirType : p.edits.edits[0].state.type;
    };
    // In order: one cell cycles through the slots, click by click.
    settings.container.mode = PlacementMode::InOrder;
    for (std::uint64_t d = 0; d < 7; ++d) CHECK(pickAt(d) == settings.container.slots[d % 3].type);
    // Random slot: every slot comes up, roughly uniformly; count-weighted
    // tracks the counts.
    for (PlacementMode m : {PlacementMode::RandomSlot, PlacementMode::CountWeighted}) {
        settings.container.mode = m;
        std::map<BlockTypeId, int> freq;
        constexpr int kDraws = 3000;
        for (int d = 0; d < kDraws; ++d) ++freq[pickAt(static_cast<std::uint64_t>(d))];
        const double n = kDraws;
        if (m == PlacementMode::RandomSlot)
            CHECK(std::abs(freq[brick] / n - 1.0 / 3) < 0.04 && std::abs(freq[stone] / n - 1.0 / 3) < 0.04 &&
                  std::abs(freq[dirt] / n - 1.0 / 3) < 0.04);
        else
            CHECK(std::abs(freq[brick] / n - 0.6) < 0.04 && std::abs(freq[stone] / n - 0.3) < 0.04 &&
                  std::abs(freq[dirt] / n - 0.1) < 0.03);
    }
    // Commit advances the state only when the world changed; the preview
    // after a commit is the next draw.
    settings.container.mode = PlacementMode::InOrder;
    settings.container.draws = 0;
    {
        DenseGrid g(registry(), 8);
        g.apply({{{{4, 4, 0}, {stone, kIdentity}}}});
        const auto t = aim(g, {4.5, 4.5, 6.0}, {4.5, 4.5, 1.0});
        const PlannedEdit first = planEdit(&box, false, t, ctx, g);
        CHECK(first.edits.edits.size() == 1 && first.edits.edits[0].state.type == brick);
        afterCommit(&box, false, first, g.apply(first.edits).changed, settings);
        CHECK(settings.container.draws == 1);
        const auto t2 = aim(g, {4.5, 4.5, 6.0}, {4.5, 4.5, 2.0});  // now on top of the brick
        const PlannedEdit second = planEdit(&box, false, t2, ctx, g);
        CHECK(second.edits.edits[0].state.type == stone);
        afterCommit(&box, false, second, 0, settings);  // nothing changed: no roll used
        CHECK(settings.container.draws == 1);
    }
    // Match target ignores the state: the targeted block (stone) is in the
    // container → stone, every draw; take it out → nothing at all.
    settings.container.mode = PlacementMode::MatchTarget;
    for (std::uint64_t d = 0; d < 5; ++d) CHECK(pickAt(d) == stone);
    settings.container.slots[1] = {typeOf("log"), 30};
    CHECK(pickAt(0) == BlockState::kAirType);
    CHECK(placementHash({1, 2, 3}, 1, 7) == placementHash({1, 2, 3}, 1, 7));
    CHECK(placementHash({1, 2, 3}, 1, 7) != placementHash({1, 2, 3}, 1, 8));
    CHECK(placementHash({1, 2, 3}, 1, 7) != placementHash({1, 2, 4}, 1, 7));
}

// v0.1.8: the container patch (N×N), orthogonal to the placement modes,
// and consumption (the survival rule).
void testContainerPatch() {
    DenseGrid w(registry(), 24);
    EditSet floor;
    for (int y = 0; y < 24; ++y)
        for (int x = 0; x < 24; ++x) floor.edits.push_back({{x, y, 0}, {typeOf("stone"), kIdentity}});
    w.apply(floor);
    const BlockTypeId brick = typeOf("brick"), stone = typeOf("stone"), dirt = typeOf("dirt");
    ToolSettings settings;
    settings.container.slots = {{{brick, 60}, {stone, 30}, {dirt, 10}}};
    OrientationLatch latch;
    const ToolContext ctx{registry(), settings, latch};
    const Item box = Item::container();
    const WorldPos center{12.5, 12.5, 1.0};
    const auto target = aim(w, {12.4, 10.0, 9.0}, center);
    // Every mode at every N: pure, the patch is N×N against the face, and
    // preview equals commit.
    for (int m = 0; m < kPlacementModeCount; ++m)
        for (int n : {1, 2, 3, 5}) {
            settings.container.mode = static_cast<PlacementMode>(m);
            settings.container.patch = n;
            settings.container.draws = 17;
            const PlannedEdit a = planEdit(&box, false, target, ctx, w);
            CHECK(a.edits == planEdit(&box, false, target, ctx, w).edits);
            CHECK(a.edits.edits.size() == static_cast<std::size_t>(n * n) && a.unaffordable.empty());
            for (const Edit& e : a.edits.edits) CHECK(e.cell.z == 1);
            checkPureAndCommits(w, &box, false, target, ctx);
        }
    // N = 1 is v0.1.7 exactly: one cell; in order = slot draws mod 3;
    // random slot / count-weighted = the v0.1.7 hash of (cell, id, draws).
    settings.container.patch = 1;
    const CellPos above{12, 12, 1};
    for (std::uint64_t d = 0; d < 40; ++d) {
        settings.container.draws = d;
        const std::uint64_t h = placementHash(above, settings.container.id, d);
        settings.container.mode = PlacementMode::InOrder;
        CHECK(planEdit(&box, false, target, ctx, w).edits.edits.at(0).state.type == settings.container.slots[d % 3].type);
        settings.container.mode = PlacementMode::RandomSlot;
        CHECK(planEdit(&box, false, target, ctx, w).edits.edits.at(0).state.type == settings.container.slots[h % 3].type);
        settings.container.mode = PlacementMode::CountWeighted;
        const std::uint64_t u = h % 100;
        const BlockTypeId expect = u < 60 ? brick : (u < 90 ? stone : dirt);
        CHECK(planEdit(&box, false, target, ctx, w).edits.edits.at(0).state.type == expect);
    }
    // A patch's cells differ (the cell is in the hash), and in order
    // continues the cursor cell by cell; a commit advances draws by the
    // number placed.
    settings.container.patch = 3;
    settings.container.draws = 5;
    settings.container.mode = PlacementMode::InOrder;
    const PlannedEdit ordered = planEdit(&box, false, target, ctx, w);
    for (std::size_t i = 0; i < ordered.edits.edits.size(); ++i)
        CHECK(ordered.edits.edits[i].state.type == settings.container.slots[(5 + i) % 3].type);
    {
        DenseGrid g(registry(), 24);
        g.apply(floor);
        afterCommit(&box, false, ordered, g.apply(ordered.edits).changed, settings);
        CHECK(settings.container.draws == 5 + 9);
    }
    // Consumption: a 5×5 patch from a container of 6 brick / 3 stone / 1
    // dirt places exactly the 10 it can afford — exactly 6/3/1, a bag drawn
    // without replacement — weights each pick by the counts left at that
    // cell, and marks the other 15.
    settings.container.slots = {{{brick, 6}, {stone, 3}, {dirt, 1}}};
    settings.container.consume = true;
    settings.container.patch = 5;
    settings.container.mode = PlacementMode::CountWeighted;
    settings.container.draws = 3;
    const PlannedEdit bag = planEdit(&box, false, target, ctx, w);
    CHECK(bag.edits.edits.size() == 10 && bag.unaffordable.size() == 15 && bag.fill == 10);
    std::map<BlockTypeId, int> placed;
    std::array<std::uint32_t, 3> left{6, 3, 1};
    for (const Edit& e : bag.edits.edits) {
        ++placed[e.state.type];
        // Replay the pick from the counts left at this cell.
        std::uint64_t total = left[0] + left[1] + left[2];
        std::uint64_t u = placementHash(e.cell, settings.container.id, 3) % total;
        std::size_t k = 0;
        while (u >= left[k]) u -= left[k++];
        CHECK(e.state.type == settings.container.slots[k].type);
        --left[k];
    }
    CHECK(placed[brick] == 6 && placed[stone] == 3 && placed[dirt] == 1);
    // In order and random slot skip an emptied type.
    settings.container.mode = PlacementMode::InOrder;
    const PlannedEdit cycle = planEdit(&box, false, target, ctx, w);
    std::map<BlockTypeId, int> cycled;
    for (const Edit& e : cycle.edits.edits) ++cycled[e.state.type];
    CHECK(cycle.edits.edits.size() == 10 && cycled[brick] == 6 && cycled[stone] == 3 && cycled[dirt] == 1);
    // The commit uses the counts up; then the container is empty.
    {
        DenseGrid g(registry(), 24);
        g.apply(floor);
        afterCommit(&box, false, bag, g.apply(bag.edits).changed, settings);
        CHECK(settings.container.slots[0].count == 0 && settings.container.slots[1].count == 0 &&
              settings.container.slots[2].count == 0);
        CHECK(settings.container.draws == 3 + 10);
        const PlannedEdit none = planEdit(&box, false, target, ctx, g);
        CHECK(none.edits.edits.empty() && none.action.find("empty") != std::string::npos);
    }
    // Match target with consumption: only the matching type, until it runs out.
    settings.container.slots = {{{brick, 6}, {stone, 4}, {dirt, 1}}};
    settings.container.mode = PlacementMode::MatchTarget;
    settings.container.patch = 3;
    const PlannedEdit match = planEdit(&box, false, target, ctx, w);
    CHECK(match.edits.edits.size() == 4 && match.unaffordable.size() == 5);
    for (const Edit& e : match.edits.edits) CHECK(e.state.type == stone);
}

// Shift+click (input/per-hand-use): only the wrench has a secondary action;
// every other item treats Shift+click as a click (owner, 2026-11-05: X+Shift
// placing did nothing).
void testSecondaryFallsBack() {
    DenseGrid w(registry(), 8);
    w.apply({{{{4, 4, 0}, {typeOf("furnace"), kIdentity}}}});  // oriented: every turn shows
    ToolSettings settings;
    settings.shovelFill = typeOf("dirt");
    settings.container.slots = {{{typeOf("brick"), 6}, {typeOf("stone"), 3}, {typeOf("dirt"), 1}}};
    OrientationLatch latch;
    const ToolContext ctx{registry(), settings, latch};
    const auto t = aim(w, {4.5, 2.5, 3.0}, {4.5, 4.5, 0.5});
    for (const Item& item : {Item::blockStack(typeOf("furnace")), Item::pick(), Item::shovel(), Item::container()}) {
        CHECK(!hasSecondaryAction(&item));
        const PlannedEdit a = planEdit(&item, false, t, ctx, w), b = planEdit(&item, true, t, ctx, w);
        CHECK(a.edits == b.edits && a.action == b.action);
    }
    CHECK(!hasSecondaryAction(nullptr) && planEdit(nullptr, true, t, ctx, w).edits == planEdit(nullptr, false, t, ctx, w).edits);
    const Item wrench = Item::wrench();
    CHECK(hasSecondaryAction(&wrench) && !(planEdit(&wrench, true, t, ctx, w).edits == planEdit(&wrench, false, t, ctx, w).edits));
    // A container's Shift+click commits like a click: draws advance.
    const Item box = Item::container();
    const PlannedEdit p = planEdit(&box, true, t, ctx, w);
    afterCommit(&box, true, p, w.apply(p.edits).changed, settings);
    CHECK(settings.container.draws == 1);
}

// --- v0.2.0: scenario files ----------------------------------------------------------

void testScenarioParse() {
    std::string err;
    // Defaults: the v0.1 start view, 1280×720, lit, no events, unlimited
    // interactive; capturing runs 60 frames.
    const auto empty = parseScenario("{}", ".", err);
    CHECK(empty && empty->grid == 32 && empty->width == 1280 && empty->height == 720 && empty->bufferView == 1);
    CHECK(empty && !empty->runFrames(false) && empty->runFrames(true) == 60L);
    // Every section.
    const auto s = parseScenario(R"({
        "world": {"grid": 16},
        "scene": [{"fill": "stone", "from": [0,0,0], "to": [1,1,0]}, {"place": "log", "at": [3,4,5], "rotation": 8}],
        "camera": {"pos": [1, 2, 3.5], "look": [4, 5, 6]},
        "window": {"size": [800, 600], "resize": {"frame": 40, "size": [640, 480]}},
        "view": {"buffer": "variant", "overlays": ["stats", "editSet"], "preview": true},
        "render": {"anisotropy": 4, "variants": false, "glassFrames": false, "momentBias": 1e-3,
                   "faceOrder": "shuffle:7", "oitView": 2, "mergedQuad": true},
        "tools": {"patch": 3, "consume": true},
        "events": [{"frame": 5, "key": "x"}, {"frame": 2, "hold": "x", "until": 9, "shift": true},
                   {"frame": 7, "click": "right"}, {"frame": 8, "wheel": "down"}],
        "checks": {"absorbance": [{"name": "c", "eye": [0,0,0], "look": [1,0,0], "cubes": [{"at": [1,0,0], "type": "glass_blue"}]}]}
    })", ".", err);
    CHECK(s.has_value());
    if (!s) return;
    CHECK(s->grid == 16 && s->scene.size() == 2 && s->scene[1].rotation == 8 && s->scene[1].from == CellPos{3, 4, 5});
    CHECK(s->camera.z == 3.5 && s->look.y == 5.0 && s->width == 800 && s->resizeFrame == 40L && s->resizeWidth == 640);
    CHECK(s->bufferView == 7 && s->overlay(Overlay::Stats) && s->overlay(Overlay::EditSet) && !s->overlay(Overlay::Bounds));
    CHECK(s->preview && s->anisotropy == 4.0f && !s->variants && !s->glassFrames && s->oitView == 2 && s->mergedQuad);
    CHECK(s->faceOrder.kind == FaceOrder::Kind::Shuffle && s->faceOrder.seed == 7 && s->patch == 3 && s->consume);
    CHECK(s->absorbance.size() == 1 && s->absorbance[0].cubes.size() == 1);
    // Events expand to raw press/release pairs, sorted by frame (stable):
    // hold 2→9, key 5/6, click 7/8, wheel 8 (no release).
    CHECK(s->events.size() == 7);
    CHECK(s->events[0].frame == 2 && s->events[0].event.press && s->events[0].event.shift);
    CHECK(s->events[1].frame == 5 && s->events[1].event.code == GLFW_KEY_X && s->events[2].frame == 6 && !s->events[2].event.press);
    CHECK(s->events.back().frame == 9 && !s->events.back().event.press);
    // Length: 20 past the last press (wheel at 8 → 28), but 60 past the resize at 40 → 100.
    CHECK(s->runFrames(true) == 100L && !s->runFrames(false));
    // Unknown fields, keys and buttons are errors naming the field.
    struct Bad { const char* text; const char* where; };
    for (const Bad& b : {Bad{R"({"camera": {"pos": [0,0,0], "zoom": 2}})", "camera.zoom"},
                         Bad{R"({"bogus": 1})", "bogus"},
                         Bad{R"({"events": [{"frame": 1, "key": "Q1"}]})", "events[0].key"},
                         Bad{R"({"events": [{"frame": 1, "click": "side"}]})", "events[0].click"},
                         Bad{R"({"view": {"buffer": "shiny"}})", "view.buffer"},
                         Bad{R"({"view": {"overlays": ["stats", "f12"]}})", "view.overlays[1]"},
                         Bad{R"({"render": {"faceOrder": "sideways"}})", "render.faceOrder"},
                         Bad{R"({"scene": [{"fill": "stone", "from": [0,0,0]}]})", "scene[0]"},
                         Bad{R"({"scene": [{"place": "log", "at": [0,0,0], "rotation": 24}]})", "scene[0].rotation"},
                         Bad{R"({"tools": {"patch": 10}})", "tools.patch"}}) {
        CHECK(!parseScenario(b.text, ".", err));
        CHECK(err.find(b.where) != std::string::npos);
    }
    CHECK(keyFromName("a") == GLFW_KEY_A && keyFromName("9") == GLFW_KEY_9 && keyFromName("F12") == GLFW_KEY_F12);
    CHECK(!keyFromName("F13") && !keyFromName("esc") && !keyFromName(""));
}

void testScenarioIncludeAndScene() {
    std::string err;
    // Include: the included file's fields are the defaults; scene appends,
    // sections override field by field, events replace.
    const auto s = loadScenario(SANDBOX_SCENARIO_DIR "/headless/xghost.json", err);
    CHECK(s.has_value());
    if (!s) return;
    const auto base = loadScenario(SANDBOX_SCENARIO_DIR "/v0.1-scene.json", err);
    CHECK(base && s->scene.size() == base->scene.size() && s->absorbance.size() == base->absorbance.size());
    CHECK(s->camera.x == 12.5 && s->overlay(Overlay::Orientation) && s->events.size() == 5);
    // The v0.1 scene: the ground, the mound, the walls and the placements,
    // applied as one edit set (later operations overwrite earlier ones).
    DenseGrid g(registry(), base->grid);
    std::vector<std::string> warnings;
    g.apply(sceneEdits(*base, registry(), &warnings));
    CHECK(warnings.empty());
    CHECK(g.probe({0, 0, 0}).type == typeOf("stone") && g.probe({25, 25, 15}).type == typeOf("grass"));
    CHECK(g.probe({24, 6, 15}).isAir() && g.probe({27, 5, 18}).type == typeOf("stone"));  // the mound's dip and rock
    CHECK(g.probe({16, 31, 16}).type == typeOf("dirt") && g.probe({0, 15, 18}).type == typeOf("stone_brick"));
    CHECK(g.probe({12, 14, 16}) == registry().state(typeOf("log"), rotationAbout({0, 1, 0}, 90)));
    CHECK(base->absorbance.size() == 5);
    // A missing block type is skipped with one warning.
    const auto missing = parseScenario(R"({"scene": [{"place": "unobtainium", "at": [0,0,0]},
                                                     {"place": "unobtainium", "at": [1,0,0]}]})", ".", err);
    warnings.clear();
    CHECK(missing && sceneEdits(*missing, registry(), &warnings).edits.empty() && warnings.size() == 1);
    // Every shipped scenario loads.
    for (const auto& e : std::filesystem::directory_iterator(SANDBOX_SCENARIO_DIR "/headless")) {
        const bool ok = loadScenario(e.path().string(), err).has_value();
        CHECK(ok);
        if (!ok) std::printf("  %s\n", err.c_str());
    }
}

// X ghost: once the wheel turns the ghost, the orientation it lands on is
// locked (in world frame) for placements of that type, even when it equals
// the inferred one, until X is released.
void testOrientationLatch() {
    DenseGrid w(registry(), 8);
    w.apply({{{{4, 4, 0}, {typeOf("stone"), kIdentity}}}});
    const BlockTypeId furnace = typeOf("furnace");
    ToolSettings settings;
    OrientationLatch latch;
    const ToolContext ctx{registry(), settings, latch};
    const WorldPos eye{4.5, 1.0, 3.0};
    const auto t = aim(w, eye, {4.5, 4.5, 1.0});
    CHECK(t.has_value());
    if (!t) return;
    const Rot inferred = inferredOrientation(furnace, *t, registry());
    CHECK(rotateGhost(furnace, *t, false, registry(), latch));
    CHECK(latch.count(furnace) == 1 && latch.at(furnace) != inferred);
    const Item stack = Item::blockStack(furnace);
    CHECK(planEdit(&stack, false, t, ctx, w).edits.edits[0].state.orientation == latch.at(furnace));
    CHECK(rotateGhost(furnace, *t, true, registry(), latch));  // back to the inferred orientation …
    CHECK(latch.count(furnace) == 1 && latch.at(furnace) == inferred);  // … which is now LOCKED too
    // A different target would infer differently, but the lock holds.
    const auto t2 = aim(w, {7.5, 4.5, 3.0}, {4.5, 4.5, 1.0});
    CHECK(t2 && inferredOrientation(furnace, *t2, registry()) != inferred);
    if (t2) CHECK(planEdit(&stack, false, t2, ctx, w).edits.edits[0].state.orientation == inferred);
    latch.clear();  // X released
    if (t2) CHECK(planEdit(&stack, false, t2, ctx, w).edits.edits[0].state.orientation != inferred);
}

// From ONE fixed viewpoint, the wheel (turn about the clicked face's normal)
// and Shift+wheel (tip about screen-right made perpendicular to it) reach
// every orientation: all 3 of a log, all 24 of a furnace — on a floor, and
// on a wall seen obliquely, where raw screen-right would snap onto n.
void testGhostReachability() {
    DenseGrid w(registry(), 8);
    w.apply({{{{4, 4, 0}, {typeOf("stone"), kIdentity}}, {{4, 4, 1}, {typeOf("stone"), kIdentity}}}});
    struct View {
        WorldPos eye, at;
        glm::dvec3 right;
    };
    const View views[] = {
        {{4.5, 1.0, 3.0}, {4.5, 4.5, 2.0}, {1.0, 0.0, 0.0}},                     // the top face (n = +z)
        {{6.5, 1.5, 1.5}, {5.0, 4.6, 1.5}, glm::normalize(glm::dvec3(1.0, 0.9, 0.0))},  // the +x wall, obliquely
    };
    for (const View& v : views) {
        const auto t = aim(w, v.eye, v.at);
        CHECK(t.has_value() && t->hit.normal != glm::ivec3(0));
        if (!t) continue;
        const glm::ivec3 tip = tipAxis(t->hit.normal, v.right);
        CHECK(tip.x * t->hit.normal.x + tip.y * t->hit.normal.y + tip.z * t->hit.normal.z == 0);  // ⊥ n, always
        for (const auto& [name, expected] : {std::pair{"log", 3u}, {"furnace", 24u}}) {
            const BlockTypeId type = typeOf(name);
            std::set<Rot> seen;
            std::vector<OrientationLatch> frontier(1);
            for (std::size_t i = 0; i < frontier.size() && i < 200; ++i)
                for (int op = 0; op < 4; ++op) {
                    OrientationLatch next = frontier[i];
                    if (op < 2) rotateGhost(type, *t, op == 1, registry(), next);
                    else tipGhost(type, *t, v.right, op == 2, registry(), next);
                    if (seen.insert(next.at(type)).second) frontier.push_back(next);
                }
            CHECK(seen.size() == expected);
        }
    }
    CHECK(nearestAxis({0.3, -0.9, 0.1}) == glm::ivec3(0, -1, 0));
    CHECK(tipAxis({1, 0, 0}, {1.0, 0.0, 0.0}) != glm::ivec3(1, 0, 0));  // degenerate: still ⊥ n
}

void testOrientationContext() {
    IntentionLayer layer(Hand::Right);
    // X held: the wheel turns the ghost; the mouse buttons still use the hands.
    layer.push({Device::Key, GLFW_KEY_X, true, false});
    layer.push({Device::Wheel, +1, true, false});
    layer.push({Device::Wheel, -1, true, false});
    layer.push({Device::MouseButton, GLFW_MOUSE_BUTTON_RIGHT, true, false});
    IntentionFrame f = layer.process({});
    CHECK(f.orientHeld && f.fired.size() == 3);
    if (f.fired.size() == 3) {
        CHECK(f.fired[0].kind == IntentionKind::RotateGhost && f.fired[0].arg == +1);
        CHECK(f.fired[1].kind == IntentionKind::RotateGhost && f.fired[1].arg == -1);
        CHECK(f.fired[2].kind == IntentionKind::Use && f.fired[2].hand == Hand::Right);
    }
    CHECK(f.useHeld[1]);  // placing with X held
    CHECK(f.contexts.size() == 2 && f.contexts[1] == "orientation (X)");
    // Shift+wheel with X held tips the ghost (the second axis).
    layer.push({Device::Wheel, +1, true, true});
    const IntentionFrame tip = layer.process({});
    CHECK(tip.fired.size() == 1 && tip.fired[0].kind == IntentionKind::RotateGhost && tip.fired[0].secondary);
    // X released: the wheel is the hotbar again.
    layer.push({Device::MouseButton, GLFW_MOUSE_BUTTON_RIGHT, false, false});
    layer.push({Device::Key, GLFW_KEY_X, false, false});
    layer.push({Device::Wheel, -1, true, false});
    layer.push({Device::Key, GLFW_KEY_Q, true, false});
    layer.push({Device::Key, GLFW_KEY_M, true, false});
    f = layer.process({});
    CHECK(!f.orientHeld && f.fired.size() == 3);
    if (f.fired.size() == 3) {
        CHECK(f.fired[0].kind == IntentionKind::Scroll && f.fired[0].hand == Hand::Right);
        CHECK(f.fired[1].kind == IntentionKind::TogglePreview && f.fired[2].kind == IntentionKind::CycleMode);
    }
}

}  // namespace

int main() {
    testRebaseExactAtLargeOffset();
    testDowncastFirstLosesOffset();
    testGroupMatrices();
    testGroupTables();
    testRotationAbout();
    testSubgroups();
    testCanonicalization();
    testCanonicalLooksIdentical();
    testWrenchReachability();
    testRotateCcw();
    testParseDefinition();
    testVariantSets();
    testMipChain();
    testFaceTexturesForColumn();
    testVariantSelection();
    testFalloff();
    testFresnelF0();
    testShadePbr();
    testRaycast();
    testGridEditsMeshEmitters();
    testMeshFaceTextures();
    testPlacementInputs();
    testPlacementRule();
    testHotbar();
    testUseRepeater();
    testIntentions();
    testSwapKey();
    testCameraRelativeAscend();
    testBoundaryTerms();
    testMomentAntiderivatives();
    testMomentReconstruction();
    testFaceOrder();
    testTranslucentFaceRule();
    testGlassFrames();
    testToolsPureAndCommit();
    testShovel();
    testWrench();
    testContainerModes();
    testContainerPatch();
    testSecondaryFallsBack();
    testScenarioParse();
    testScenarioIncludeAndScene();
    testOrientationLatch();
    testOrientationContext();
    testGhostReachability();
    const int n = check::failures();
    if (n == 0) std::printf("sandbox_tests: all checks passed\n");
    else std::fprintf(stderr, "sandbox_tests: %d check(s) failed\n", n);
    return n > 125 ? 125 : n;
}
