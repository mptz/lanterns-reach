#pragma once
// Harness checks that need the renderer: --check-absorbance (the OIT
// absorbance read back at the centre pixel, compared with Beer–Lambert: σ ×
// the analytic chord of that pixel's ray through each translucent cube) and
// the --merged-quad region. The cases come from the scenario's `checks`.

#include <cstddef>
#include <vector>

#include <glm/vec3.hpp>

#include "blocks.h"
#include "camera.h"
#include "renderer.h"
#include "scenario.h"

// The merged-quad test region: inside the v0.1 scene's stone floor
// (scenarios/v0.1-scene.json), 4×4 cells.
inline constexpr CellPos kMergedQuadCell{2, 2, 15};
inline constexpr int kMergedQuadSize = 4;

class AbsorbanceCheck {
public:
    explicit AbsorbanceCheck(std::vector<AbsorbanceCase> cases) : cases_(std::move(cases)) {}

    bool done() const { return index_ == cases_.size(); }
    // Puts the camera at the current case's view.
    void aim(Camera& camera) const;
    // Once per frame after the overlays: let the view settle, request the
    // centre texel, and check it when it arrives.
    void frame(Renderer& renderer, const Camera& camera, const BlockRegistry& registry);
    // The run's exit code, given the loop's: non-zero if any case failed or
    // the run ended before every case was checked.
    int result(int rc) const;

private:
    std::vector<AbsorbanceCase> cases_;
    std::size_t index_ = 0;
    int waited_ = 0;
    bool requested_ = false;
    int failures_ = 0;
    std::vector<glm::dvec3> measured_;  // per case, rgb
};
