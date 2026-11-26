#pragma once
// The interactive loop: one window, one world, one scenario. Input goes
// through the intention layer (bindings.cpp: the one binding table,
// input/bindings); everything the game does goes through WorldQuery.

#include <string>

#include "blocks.h"
#include "scenario.h"

// What the command line adds to a scenario (main.cpp).
struct RunOptions {
    std::string capture;          // --capture: write the final image and exit
    bool gpuReport = false;       // --gpu-report: per-pass mean GPU times at exit
    bool checkAbsorbance = false; // --check-absorbance: run the scenario's checks
};

// Runs the scenario; returns the exit code (non-zero on validation errors,
// a failed check, or a capture that couldn't be written).
int run(const RunOptions& options, const Scenario& scenario, bool validation, const BlockRegistry& registry);
