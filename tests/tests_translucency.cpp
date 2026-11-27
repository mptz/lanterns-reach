// Tests: translucency. Registered through sandbox_tests.cpp.

#include "test_support.h"

namespace {

using namespace orient;

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
    ChunkWorld g(registry());
    g.apply({{{{1, 1, 1}, registry().state(*glass, orient::kIdentity)}, {{5, 1, 1}, registry().state(*glass, orient::kIdentity)}, {{3, 5, 2}, registry().state(*glass, orient::kIdentity)}}});
    const WorldMesh base = g.buildMesh(ChunkWorld::kChunk);
    CHECK(base.quads.empty() && base.translucentQuads.size() == 3 * 6);  // glass is translucent only
    auto faces = [](const WorldMesh& m) { return std::multiset<std::uint64_t>(m.translucentQuads.begin(), m.translucentQuads.end()); };
    const WorldPos eye{0.0, 0.0, 0.0};
    auto centerDist = [&](const WorldMesh& m, std::size_t f) {
        const quad::Quad q = quad::unpack(m.translucentQuads[f]);
        glm::dvec3 c = glm::dvec3(q.cell) + glm::dvec3(0.5) + 0.5 * glm::dvec3(orient::faceNormal(q.face));  // a unit face's centre
        return c.x * c.x + c.y * c.y + c.z * c.z;
    };
    for (const char* spec : {"forward", "reverse", "shuffle:7"}) {
        WorldMesh m = base;
        orderTranslucentFaces(m, *FaceOrder::parse(spec), eye);
        CHECK(faces(m) == faces(base));  // a permutation of whole faces
        if (std::string(spec) != "shuffle:7") {
            bool sorted = true;
            for (std::size_t f = 1; f < m.translucentQuads.size(); ++f) {
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
    CHECK(s1.translucentQuads == s2.translucentQuads && s1.translucentQuads != s3.translucentQuads);
    CHECK(!FaceOrder::parse("sideways") && !FaceOrder::parse("shuffle:") && !FaceOrder::parse("shuffle:x"));
}

// The face rule for translucent cells: same-group neighbours share no faces;
// different tints and opaque neighbours keep both sides' faces.
void testTranslucentFaceRule() {
    const auto red = registry().find("glass_red"), blue = registry().find("glass_blue"), glow = registry().find("glow");
    CHECK(red && blue && glow);
    if (!red || !blue || !glow) return;
    ChunkWorld g(registry());
    auto put = [&](CellPos c, BlockTypeId t) { g.apply({{{c, registry().state(t, orient::kIdentity)}}}); };
    put({1, 1, 1}, *red);
    put({2, 1, 1}, *red);  // a run of one tint: 10 faces, no internal pair
    CHECK(g.buildMesh(ChunkWorld::kChunk).translucentQuads.size() == 10);
    put({3, 1, 1}, *blue);  // another tint: the interface is two coplanar faces
    CHECK(g.buildMesh(ChunkWorld::kChunk).translucentQuads.size() == 10 + 6);
    put({1, 1, 0}, testStates().type(solidState()));  // glass on stone keeps its face; stone keeps its face to the glass
    const WorldMesh m = g.buildMesh(ChunkWorld::kChunk);
    CHECK(m.translucentQuads.size() == 16 && m.quads.size() == 6);
    // Emitters include translucent blocks with emission.
    put({6, 6, 6}, *glow);
    bool found = false;
    for (const Emitter& e : g.emitters()) found = found || (e.cell == CellPos{6, 6, 6} && e.emissive.x > 0.0f);
    CHECK(found);
}



// Connected-glass frames (v0.1.8: a connected variant set): the mesher's
// frame bits (a quad's side bits, per unit face) pick the set's tile in the
// fragment shader (block_mesh.h connectedTile is the same mapping), so a run of one tint is framed only along
// its outline; interface pairs are framed once; faces against opaque cells
// use tile 0 (fully transparent). Tiles are in the image's frame (kEdgeN …
// kEdgeW), carried from the face frame by the face's turn.
void testGlassFrames() {
    const auto red = registry().find("glass_red"), blue = registry().find("glass_blue");
    CHECK(red && blue);
    if (!red || !blue) return;
    const BlockDef& redDef = registry().def(*red);
    CHECK(redDef.faceSet[PosZ] == VariantSet::Connected);
    ChunkWorld g(registry());
    auto put = [&](CellPos c, BlockTypeId t) { g.apply({{{c, registry().state(t, orient::kIdentity)}}}); };
    // The tile index of the translucent face of `cell` toward n.
    auto tile = [&](const WorldMesh& m, CellPos cell, glm::vec3 n) -> std::optional<std::uint32_t> {
        const Face f = n.z > 0.5f ? PosZ : n.z < -0.5f ? NegZ : n.x > 0.5f ? PosX : n.x < -0.5f ? NegX : n.y > 0.5f ? PosY : NegY;
        for (const UnitFace& u : unitFaces(m))
            if (u.translucent && u.dir == static_cast<int>(f) && u.x == cell.x && u.y == cell.y && u.z == cell.z)
                return connectedTile(16u | u.frame, faceTexture(testStates(), g.probe(cell), f).turn);
        return std::nullopt;
    };
    // The +z face of a uniform block: R = +x, U = +y; its turn must be 0
    // for the expectations below to be in the image frame directly.
    CHECK(faceTexture(testStates(), registry().state(*red, orient::kIdentity), PosZ).turn == 0u);
    // A run of 3 along x: the top faces are framed on their outer edges only.
    put({1, 1, 1}, *red);
    put({2, 1, 1}, *red);
    put({3, 1, 1}, *red);
    WorldMesh m = g.buildMesh(ChunkWorld::kChunk);
    CHECK(tile(m, {1, 1, 1}, {0, 0, 1}) == (kEdgeW | kEdgeN | kEdgeS));
    CHECK(tile(m, {2, 1, 1}, {0, 0, 1}) == (kEdgeN | kEdgeS));  // the middle: no edges along the run
    CHECK(tile(m, {3, 1, 1}, {0, 0, 1}) == (kEdgeE | kEdgeN | kEdgeS));
    // A step up at x = 3 makes a crease: the edge between (2) and (3)'s tops is framed.
    put({3, 1, 2}, *red);
    m = g.buildMesh(ChunkWorld::kChunk);
    CHECK(tile(m, {2, 1, 1}, {0, 0, 1}) == (kEdgeE | kEdgeN | kEdgeS));
    // A different tint beside the run: the interface pair is framed once
    // (lower type id carries it).
    put({4, 1, 1}, *blue);
    m = g.buildMesh(ChunkWorld::kChunk);
    const auto onRed = tile(m, {3, 1, 1}, {1, 0, 0}), onBlue = tile(m, {4, 1, 1}, {-1, 0, 0});
    CHECK(onRed && onBlue && ((*onRed != 0) != (*onBlue != 0)));
    // Against opaque: the face exists (it closes the volume) but uses tile 0.
    put({1, 1, 0}, testStates().type(solidState()));
    m = g.buildMesh(ChunkWorld::kChunk);
    CHECK(tile(m, {1, 1, 1}, {0, 0, -1}) == 0u);
    // Turns: a face turned once CCW shows its image N edge at face position W.
    CHECK(connectedTile(16u | 1u, 1) == kEdgeN);  // framed −R (W) → image N after one CCW turn
    CHECK(connectedTile(16u | 8u, 0) == kEdgeN);  // framed +U = N, unturned
    CHECK(connectedTile(15u, 0) == 0u);          // no surface layer
}


}  // namespace

void runTranslucencyTests() {
    testBoundaryTerms();
    testMomentAntiderivatives();
    testMomentReconstruction();
    testFaceOrder();
    testTranslucentFaceRule();
    testGlassFrames();
}
