// sandbox_tests: pure-logic tests (no GPU), split by module into tests_*.cpp
// and registered with CTest.
//
//   sandbox_tests                 run all checks
//   sandbox_tests --long          the same with the longer equivalence run
//   sandbox_tests --time-chunk    time edits, growth, compaction, the digest and memory (Release)
//   sandbox_tests --time-mesh     time meshing a full 64³ chunk (use a Release build)

#include "test_support.h"

int main(int argc, char** argv) {
    const std::string arg = argc > 1 ? argv[1] : "";
    if (arg == "--time-mesh") {
        timeChunkMesh();
        return 0;
    }
    if (arg == "--time-chunk") {
        timeChunkRepresentations();
        return 0;
    }
    runCoordsTests();
    runChunkTests();
    runDigestTests();
    runRepresentationTests();
    runEquivalenceTests(arg == "--long");
    runOrientationTests();
    runBlocksTests();
    runShadingTests();
    runWorldTests();
    runPlacementTests();
    runInputTests();
    runTranslucencyTests();
    runToolsTests();
    runScenarioTests();
    const int n = check::failures();
    if (n == 0) std::printf("sandbox_tests: all checks passed\n");
    else std::fprintf(stderr, "sandbox_tests: %d check(s) failed\n", n);
    return n > 125 ? 125 : n;
}
