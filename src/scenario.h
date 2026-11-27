#pragma once
// Scenario files (v0.2.0; REVIEW-v0.1.md Q7, Q8): one JSON file fully
// describes a run — the world and its scene, the camera, the window, what is
// shown, render and tool settings, a timed input script, how long to run, and
// the absorbance checks. `--scenario PATH` runs one. The schema is documented
// in scenarios/README.md; unknown fields are errors, as in block definitions.
// CPU only (key codes come from GLFW's header, as in bindings.cpp).

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "blocks.h"
#include "coords.h"
#include "mesher.h"
#include "intentions.h"
#include "translucency.h"
#include "world_query.h"

// One scene operation, applied in order: fill an inclusive box, or place one
// block (type "air" clears).
struct SceneOp {
    enum class Kind : std::uint8_t { Fill, Place } kind = Kind::Place;
    std::string type;
    CellPos from{}, to{};                    // Fill: inclusive box; Place: from = the cell
    orient::Rot rotation = orient::kIdentity;  // Place: rotation index 0–23 (canonicalized on apply)
};

// One timed raw input event, as the intention layer receives it.
struct ScenarioEvent {
    long frame = 0;
    RawEvent event;
};

// One --check-absorbance case: a view, and the translucent cells the centre
// pixel's ray crosses (with their types), checked against Beer–Lambert.
struct AbsorbanceCase {
    std::string name;
    WorldPos eye{}, look{};
    std::vector<std::pair<CellPos, std::string>> cubes;
};

// Indices for Scenario::overlays.
enum class Overlay : std::uint8_t { Stats, Wireframe, Orientation, Lighting, Targeting, Input, Bounds, EditSet };
inline constexpr int kOverlayCount = 8;

struct Scenario {
    // world
    std::string generator = "empty";  // fills the chunk with air; the scene is applied on top
    // scene: applied in order as one edit set
    std::vector<SceneOp> scene;
    // camera (the v0.1 start view: within reach of the surface, about 9 blocks)
    WorldPos camera{10.0, 10.0, 19.0};
    WorldPos look{16.0, 16.0, 15.0};
    // window
    int width = 1280, height = 720;
    std::optional<long> resizeFrame;
    int resizeWidth = 0, resizeHeight = 0;
    // view: the F2 buffer view (1 lit … 7 texture variant), overlays, preview
    int bufferView = 1;
    bool overlays[kOverlayCount] = {};
    bool preview = false;
    // render
    std::optional<float> anisotropy;  // cap on the block sampler's (1 = off)
    bool variants = true;
    bool glassFrames = true;
    float momentBias = 5e-4f;  // as OitSettings
    FaceOrder faceOrder;
    int oitView = 0;
    Mesher mesher = Mesher::Greedy;
    // tools
    int patch = 1;
    bool consume = false;
    // events and length
    std::vector<ScenarioEvent> events;  // sorted by frame (stable)
    std::optional<long> frames;
    // checks
    std::vector<AbsorbanceCase> absorbance;

    bool overlay(Overlay o) const { return overlays[static_cast<int>(o)]; }

    // How many frames to run: `frames` if given; otherwise, when capturing,
    // 20 past the last event (60 past a resize, so the new size settles), at
    // least 60; otherwise unlimited (interactive).
    std::optional<long> runFrames(bool capturing) const;
};

// Parses a scenario. `include` (a path relative to the including file) loads
// another scenario first; this file's fields then override it — a whole
// top-level field at a time, except `scene`, which appends, and `view`,
// `render`, `tools`, `window` and `camera`, which override field by field.
// Returns nullopt with a message naming the offending field on error.
std::optional<Scenario> loadScenario(const std::string& path, std::string& error);
std::optional<Scenario> parseScenario(std::string_view text, const std::string& directory, std::string& error);

// The scene as one edit set (missing block types are skipped with a warning
// in `warnings`).
EditSet sceneEdits(const Scenario& s, const BlockRegistry& registry, std::vector<std::string>* warnings = nullptr);

// Named key → GLFW key code: a–z, 0–9, F1–F12 (nullopt otherwise).
std::optional<int> keyFromName(std::string_view name);
