// The v0.1 harness: an editable 64³ chunk behind WorldQuery, rendered with
// deferred PBR (point lights from emitters), commutative translucency
// (moment-based OIT) and 16×16 block textures with variant sets; the
// intention layer, two hotbars, tools as pure edit-set functions with a
// preview, the X orientation quasimode; a free camera and the dev UI.
//
// A run is a scenario file (scenarios/README.md): the world and scene,
// camera, window, view, render and tool settings, a timed input script, the
// run length and checks. main() only parses the command line, loads the
// block definitions and the scenario, and runs it (app.cpp).
//
//   --scenario PATH      the scenario (default: scenarios/v0.1-scene.json),
//                        interactive unless it sets `frames` or --capture
//   --capture FILE.png   write the final image and exit (the run length is
//                        the scenario's `frames`, or 20 frames past its last
//                        event, at least 60)
//   --gpu-report         at exit, print each pass's mean GPU time over the
//                        run (after a 120-frame warm-up)
//   --check-absorbance   run the scenario's checks.absorbance cases; exits
//                        non-zero on any failure (glass frames are off for it)
//   --check-quads        run the shaders' packed-quad unpacking over pinned
//                        quads and compare with the C++ (quad_check.h);
//                        exits non-zero on any difference, drawing no frame
//   --blocks DIR         block definitions directory (default: assets/blocks)
//   --check-blocks       load the definitions, report, and exit (no window;
//                        non-zero if any definition is rejected)
// Every other setting is a scenario field; the old flags are rejected with
// the field that replaced them. Exit status is non-zero if the validation
// layers reported any error, or (Debug builds) if the validation layer is
// unavailable or a block definition is rejected.
//
// SANDBOX_GLFW_PLATFORM=x11|wayland forces GLFW's window-system backend
// (default: GLFW's own choice, which prefers Wayland when available).

#include <cstdio>
#include <cstdlib>
#include <exception>
#include <string>
#include <string_view>

#include <GLFW/glfw3.h>

#include "app.h"
#include "blocks.h"
#include "scenario.h"
#include "vk_context.h"

namespace {

struct CommandLine {
    RunOptions run;
    std::string scenario = SANDBOX_SCENARIO_DIR "/v0.1-scene.json";
    std::string blocksDir = SANDBOX_ASSET_DIR "/blocks";
    bool checkBlocks = false;
};

// Flags v0.2.0 replaced with scenario fields (scenarios/README.md).
constexpr std::pair<std::string_view, std::string_view> kRemovedFlags[] = {
    {"--frames", "frames"},
    {"--view", "view.buffer"},
    {"--stats", "view.overlays: \"stats\""},
    {"--wireframe", "view.overlays: \"wireframe\""},
    {"--orientation", "view.overlays: \"orientation\""},
    {"--lighting", "view.overlays: \"lighting\""},
    {"--targeting", "view.overlays: \"targeting\""},
    {"--input", "view.overlays: \"input\""},
    {"--bounds", "view.overlays: \"bounds\""},
    {"--edit-set", "view.overlays: \"editSet\""},
    {"--preview", "view.preview"},
    {"--oit-view", "render.oitView"},
    {"--translucent-order", "render.faceOrder"},
    {"--moment-bias", "render.momentBias"},
    {"--no-glass-frames", "render.glassFrames"},
    {"--anisotropy", "render.anisotropy"},
    {"--no-variants", "render.variants"},
    {"--merged-quad", "render.mesher"},
    {"--patch", "tools.patch"},
    {"--consume", "tools.consume"},
    {"--cam", "camera.pos"},
    {"--look", "camera.look"},
    {"--click-at", "events: {\"frame\", \"click\"}"},
    {"--key-at", "events: {\"frame\", \"key\"}"},
    {"--hold-key", "events: {\"frame\", \"hold\", \"until\"}"},
    {"--wheel-at", "events: {\"frame\", \"wheel\"}"},
    {"--resize-at", "window.resize"},
};

[[noreturn]] void usage(const std::string& message) {
    std::fprintf(stderr, "%s\n", message.c_str());
    std::exit(2);
}

CommandLine parseCommandLine(int argc, char** argv) {
    CommandLine c;
    for (int i = 1; i < argc; ++i) {
        const std::string_view a = argv[i];
        auto value = [&]() -> std::string {
            if (i + 1 >= argc) usage("missing value for " + std::string(a));
            return argv[++i];
        };
        if (a == "--scenario") c.scenario = value();
        else if (a == "--capture") c.run.capture = value();
        else if (a == "--gpu-report") c.run.gpuReport = true;
        else if (a == "--check-absorbance") c.run.checkAbsorbance = true;
        else if (a == "--check-quads") c.run.checkQuads = true;
        else if (a == "--blocks") c.blocksDir = value();
        else if (a == "--check-blocks") c.checkBlocks = true;
        else {
            for (const auto& [flag, field] : kRemovedFlags)
                if (a == flag)
                    usage(std::string(a) + " was removed in v0.2.0: set the scenario field " + std::string(field) +
                          " (scenarios/README.md)");
            usage("unknown argument: " + std::string(a));
        }
    }
    return c;
}

}  // namespace

int main(int argc, char** argv) {
    const CommandLine cl = parseCommandLine(argc, argv);
    std::fprintf(stderr, "[sandbox] sandbox %s\n", SANDBOX_VERSION);

    // Block definitions: CPU only, so this also runs without a display.
    const BlockRegistry registry = BlockRegistry::load(cl.blocksDir);
    for (const std::string& e : registry.errors())
        std::fprintf(stderr, "[sandbox] block definition rejected: %s\n", e.c_str());
    std::fprintf(stderr, "[sandbox] %zu block type(s), %zu texture layer(s) from %s\n", registry.defs().size(),
                 registry.layers().size(), cl.blocksDir.c_str());
    if (cl.checkBlocks) return registry.errors().empty() ? 0 : 1;
#ifdef SANDBOX_VALIDATION
    if (!registry.errors().empty()) {
        std::fprintf(stderr, "[sandbox] error: Debug builds refuse to run with rejected block definitions\n");
        return 1;
    }
#endif

    std::string error;
    const auto scenario = loadScenario(cl.scenario, error);
    if (!scenario) {
        std::fprintf(stderr, "[sandbox] error: scenario %s\n", error.c_str());
        return 2;
    }

    // Validation must not fail silently. Checked before GLFW so it needs no
    // display: a Debug build without the layer exits non-zero right here.
#ifdef SANDBOX_VALIDATION
    const bool validation = validationLayerAvailable();
    std::fprintf(stderr, "[sandbox] validation: %s\n", validation ? "on" : "off");
    if (!validation) {
        std::fprintf(stderr,
                     "[sandbox] error: Debug build requires VK_LAYER_KHRONOS_validation, which is not available "
                     "(install vulkan-validationlayers, or check VK_LOADER_LAYERS_DISABLE / VK_LAYER_PATH)\n");
        return 1;
    }
#else
    const bool validation = false;
    std::fprintf(stderr, "[sandbox] validation: off\n");
#endif

    if (const char* p = std::getenv("SANDBOX_GLFW_PLATFORM")) {
        const std::string platform = p;
        if (platform == "x11") glfwInitHint(GLFW_PLATFORM, GLFW_PLATFORM_X11);
        else if (platform == "wayland") glfwInitHint(GLFW_PLATFORM, GLFW_PLATFORM_WAYLAND);
    }
    if (!glfwInit()) {
        std::fprintf(stderr, "glfwInit failed (no display?)\n");
        return 1;
    }
    int rc = 1;
    try {
        rc = run(cl.run, *scenario, validation, registry);
    } catch (const std::exception& e) {
        std::fprintf(stderr, "fatal: %s\n", e.what());
    }
    glfwTerminate();
    return rc;
}
