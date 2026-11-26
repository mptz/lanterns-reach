// Tests: coords. Registered through sandbox_tests.cpp.

#include "test_support.h"

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


}  // namespace

void runCoordsTests() {
    testRebaseExactAtLargeOffset();
    testDowncastFirstLosesOffset();
}
