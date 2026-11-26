// Tests: shading. Registered through sandbox_tests.cpp.

#include "test_support.h"

namespace {

using namespace orient;

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


}  // namespace

void runShadingTests() {
    testVariantSelection();
    testFalloff();
    testFresnelF0();
    testShadePbr();
}
