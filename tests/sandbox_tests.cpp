// sandbox_tests: pure-logic tests (no GPU), split by module into tests_*.cpp
// and registered with CTest.
//
//   sandbox_tests                 run all checks
//   sandbox_tests --time-mesh     time meshing a full 64³ chunk (use a Release build)

#include "test_support.h"

int main(int argc, char** argv) {
    if (argc > 1 && std::string(argv[1]) == "--time-mesh") {
        timeChunkMesh();
        return 0;
    }
    runCoordsTests();
    runChunkTests();
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
