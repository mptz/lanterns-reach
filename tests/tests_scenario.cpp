// Tests: scenario. Registered through sandbox_tests.cpp.

#include "test_support.h"

namespace {

using namespace orient;

// --- v0.2.0: scenario files ----------------------------------------------------------

void testScenarioParse() {
    std::string err;
    // Defaults: the v0.1 start view, 1280×720, lit, no events, unlimited
    // interactive; capturing runs 60 frames.
    const auto empty = parseScenario("{}", ".", err);
    CHECK(empty && empty->generator == "empty" && empty->width == 1280 && empty->height == 720 && empty->bufferView == 1);
    CHECK(empty && !empty->runFrames(false) && empty->runFrames(true) == 60L);
    // Every section.
    const auto s = parseScenario(R"({
        "world": {"generator": "empty"},
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
    CHECK(s->generator == "empty" && s->scene.size() == 2 && s->scene[1].rotation == 8 && s->scene[1].from == CellPos{3, 4, 5});
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
    // v0.1's dense grid is gone: world.grid is rejected, naming its replacement.
    const auto grid = parseScenario(R"({"world": {"grid": 16}})", ".", err);
    CHECK(!grid && err.find("world.grid") != std::string::npos && err.find("world.generator") != std::string::npos);
    CHECK(!parseScenario(R"({"world": {"generator": "flat"}})", ".", err) && err.find("world.generator") != std::string::npos);
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
    ChunkWorld g(registry());
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


}  // namespace

void runScenarioTests() {
    testScenarioParse();
    testScenarioIncludeAndScene();
}
