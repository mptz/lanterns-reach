// sandbox_tests: pure-logic tests (no GPU), split by module into tests_*.cpp
// and registered with CTest.
//
//   sandbox_tests                 run all checks
//   sandbox_tests --long          the same with the longer equivalence run
//   sandbox_tests --mesh-case=N   one fixed case of the mesh equivalence test (SANDBOX_TEST_VERBOSE=1 for sizes)
//   sandbox_tests --skip-equivalence / --only-equivalence   ctest's entries (the mesh cases have their own)
//   sandbox_tests --time-chunk    time edits, growth, compaction, the digest and memory (Release)
//   sandbox_tests --time-states   interning, loading, the state table's size (Release)
//   sandbox_tests --time-meshers  greedy, unmerged and per-cell meshing: time, phases, sizes (Release)
//   sandbox_tests --time-mesh     time meshing a full 64³ chunk (use a Release build)

#include "test_support.h"

int main(int argc, char** argv) {
    const std::string arg = argc > 1 ? argv[1] : "";
    if (arg == "--time-mesh") {
        timeChunkMesh();
        return 0;
    }
    if (arg == "--time-states") {
        timeStates();
        return 0;
    }
    if (arg == "--time-meshers") {
        timeMeshers();
        return 0;
    }
    if (arg == "--time-chunk") {
        timeChunkRepresentations();
        return 0;
    }
    if (arg.rfind("--mesh-case=", 0) == 0) {  // one fixed mesh-equivalence case, to find a slow or failing one
        runMeshEquivalenceTests(false, std::stoi(arg.substr(12)));
        return check::failures() == 0 ? 0 : 1;
    }
    const bool onlyEquivalence = arg == "--only-equivalence", skipEquivalence = arg == "--skip-equivalence";
    if (!onlyEquivalence) {
        runCoordsTests();
        runIdentityTests();
        runStateTests();
        runChunkTests();
        runDigestTests();
        runRepresentationTests();
        runOrientationTests();
        runBlocksTests();
        runShadingTests();
        runWorldTests();
        runMeshingTests();
        runPlacementTests();
        runInputTests();
        runTranslucencyTests();
        runToolsTests();
        runScenarioTests();
    }
    if (!skipEquivalence) {
        runEquivalenceTests(arg == "--long");
        if (!onlyEquivalence) runMeshEquivalenceTests(arg == "--long");  // under ctest, one entry per case
    }
    const int n = check::failures();
    if (n == 0) std::printf("sandbox_tests: all checks passed\n");
    else std::fprintf(stderr, "sandbox_tests: %d check(s) failed\n", n);
    return n > 125 ? 125 : n;
}
