#include "app.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <optional>
#include <set>
#include <stdexcept>
#include <string>
#include <vector>

#include <GLFW/glfw3.h>

#include "app_input.h"
#include "block_mesh.h"
#include "chunk_world.h"
#include "dev_overlays.h"
#include "frame_stats.h"
#include "game.h"
#include "harness_checks.h"
#include "png_write.h"
#include "renderer.h"
#include "scene.h"
#include "texture.h"
#include "tools.h"
#include "vk_context.h"

namespace {

constexpr double kMouseSensitivity = 0.0025;  // radians per pixel; chosen by eye (rebindable sensitivity later)

DebugView stepView(DebugView v, int step) {
    const int i = (static_cast<int>(v) - 1 + step + kDebugViewCount) % kDebugViewCount;
    return static_cast<DebugView>(i + 1);
}

}  // namespace

int run(const RunOptions& opt, const Scenario& sc, bool validation, const BlockRegistry& registry) {
    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
    glfwWindowHint(GLFW_RESIZABLE, GLFW_TRUE);
    GLFWwindow* window = glfwCreateWindow(sc.width, sc.height, "sandbox " SANDBOX_VERSION, nullptr, nullptr);
    if (!window) throw std::runtime_error("glfwCreateWindow failed");

    App state;
    IntentionLayer intentions(Hand::Right);
    state.intentions = &intentions;
    installInputCallbacks(window, &state);
    setCapture(window, true);

    FrameOptions frameOpts;
    frameOpts.view = static_cast<DebugView>(sc.bufferView);
    frameOpts.wireframe = sc.overlay(Overlay::Wireframe);
    frameOpts.lighting = sc.overlay(Overlay::Lighting);
    frameOpts.variants = sc.variants;
    frameOpts.oit.glassFrames = sc.glassFrames;
    frameOpts.oit.momentBias = sc.momentBias;
    frameOpts.oit.view = sc.oitView;
    FaceOrder order = sc.faceOrder;
    const std::optional<long> runFrames = sc.runFrames(!opt.capture.empty());
    bool reorder = false;
    bool captureRequested = false;
    int rc = 0;
    AbsorbanceCheck check(opt.checkAbsorbance ? sc.absorbance : std::vector<AbsorbanceCase>{});
    if (opt.checkAbsorbance) {
        frameOpts.oit.glassFrames = false;  // this checks the VOLUME integral; frames add surface absorbance
        if (sc.absorbance.empty()) {
            std::fprintf(stderr, "[sandbox] error: --check-absorbance: the scenario has no checks.absorbance cases\n");
            glfwDestroyWindow(window);
            return 1;
        }
    }
    bool stats = sc.overlay(Overlay::Stats), orientation = sc.overlay(Overlay::Orientation),
         targeting = sc.overlay(Overlay::Targeting), input = sc.overlay(Overlay::Input),
         bounds = sc.overlay(Overlay::Bounds), editSet = sc.overlay(Overlay::EditSet);
    {
        VkContext ctx(window, validation);
        // render.anisotropy: cap the block sampler's anisotropy (1 = off), for A/B timing.
        if (sc.anisotropy) ctx.maxSamplerAnisotropy = *sc.anisotropy <= 1.0f ? 0.0f : std::min(ctx.maxSamplerAnisotropy, *sc.anisotropy);
        std::fprintf(stderr, "[sandbox] block sampler anisotropy: %s\n",
                     ctx.maxSamplerAnisotropy >= 1.0f ? std::to_string(std::min(kMaxAnisotropy, ctx.maxSamplerAnisotropy)).c_str() : "off");
        // The app's GLFW callbacks are set above, so ImGui chains after them.
        Renderer renderer(ctx, registry);
        state.renderer = &renderer;
        DevUi::setMouseEnabled(!state.captured);

        ChunkWorld chunkWorld(registry);  // generator "empty": all air; the scene goes on top
        chunkWorld.setMesher(sc.mesher);
        {
            std::vector<std::string> warnings;
            chunkWorld.applyBulk(sceneEdits(sc, registry, &warnings));
            for (const std::string& w : warnings) std::fprintf(stderr, "[sandbox] warning: %s\n", w.c_str());
        }
        WorldQuery& world = chunkWorld;  // everything below goes through the seam
        Game game = makeGame(world, registry, sc);
        std::set<ChunkId> meshedChunks;  // the renderer keeps one mesh per chunk (one, until v0.2.24)
        std::optional<std::uint64_t> lightsGeneration;  // the world generation the lights were built from
        MeshStats meshStats;
        std::vector<PointLight> lights;
        FiredLog firedLog;

        Camera camera;
        camera.position = sc.camera;
        camera.lookAt(sc.look);

        double last = glfwGetTime();
        FrameClock clock{.windowStart = last};
        GpuTimeStats gpuStats{.windowStart = last};
        for (long frame = 0; !glfwWindowShouldClose(window); ++frame) {
            if (runFrames && frame >= *runFrames) {
                if (opt.capture.empty()) break;
                // --capture: grab the next recorded frame, then keep drawing
                // until it has completed and been read back.
                if (!captureRequested) {
                    renderer.requestCapture();
                    captureRequested = true;
                } else if (auto image = renderer.takeCapture()) {
                    if (writePngRgb(opt.capture, image->width, image->height, image->rgb)) {
                        std::fprintf(stderr, "[sandbox] captured %ux%u to %s (translucent order: %s)\n", image->width,
                                     image->height, opt.capture.c_str(), order.describe().c_str());
                    } else {
                        std::fprintf(stderr, "[sandbox] error: cannot write %s\n", opt.capture.c_str());
                        rc = 1;
                    }
                    break;
                }
            }
            if (opt.checkAbsorbance) {
                if (check.done()) break;
                check.aim(camera);
            }
            if (sc.resizeFrame && frame == *sc.resizeFrame) glfwSetWindowSize(window, sc.resizeWidth, sc.resizeHeight);

            glfwPollEvents();
            for (const ScenarioEvent& e : sc.events)
                if (e.frame == frame) intentions.push(e.event);
            int fbw = 0, fbh = 0;
            glfwGetFramebufferSize(window, &fbw, &fbh);
            while ((fbw == 0 || fbh == 0) && !glfwWindowShouldClose(window)) {  // minimized
                glfwWaitEvents();
                glfwGetFramebufferSize(window, &fbw, &fbh);
            }

            const double now = glfwGetTime();
            const double dt = now - last;
            last = now;
            clock.tick(now);
            gpuStats.add(renderer.gpuTimes(), now);

            // Raw input → intentions (ImGui's claims are from the last frame).
            const IntentionFrame in = intentions.process({DevUi::wantsMouse(), DevUi::wantsKeyboard()});
            std::array<bool, kHandCount> pressed{}, pressedSecondary{};
            bool pick = false;
            for (const Intention& i : in.fired) {
                firedLog.add(now, describe(i));
                Hotbar& bar = game.hands.bar(i.hand);
                switch (i.kind) {
                    case IntentionKind::Use:
                        (i.secondary ? pressedSecondary : pressed)[static_cast<std::size_t>(i.hand)] = true;
                        break;
                    case IntentionKind::TogglePreview: game.preview = !game.preview; break;
                    case IntentionKind::CycleMode:
                        if (const Item* held = game.mainItem(); held && held->kind == Item::Kind::Container) {
                            auto& m = game.settings.container.mode;
                            m = static_cast<PlacementMode>((static_cast<int>(m) + 1) % kPlacementModeCount);
                        }
                        break;
                    case IntentionKind::RotateGhost:
                        // X held: the wheel turns the main hand's placement ghost about
                        // the clicked (support) face's normal (up CW, down CCW);
                        // Shift+wheel tips it about screen-right made perpendicular to
                        // that normal (up = top away). Either locks the result until X is
                        // released.
                        if (const Item* held = game.mainItem(); held && held->kind == Item::Kind::Block)
                            if (const auto t = game.aim(camera)) {
                                if (i.secondary) tipGhost(held->block, *t, camera.right(), i.arg > 0, registry, game.latch);
                                else rotateGhost(held->block, *t, i.arg < 0, registry, game.latch);
                            }
                        break;
                    case IntentionKind::SelectSlot: bar.select(i.arg); break;
                    case IntentionKind::Scroll: bar.scroll(i.arg); break;
                    case IntentionKind::Bare: bar.setBare(); break;
                    case IntentionKind::SwapHotbars: game.hands.swapHotbars(); break;
                    case IntentionKind::PickBlock: pick = true; break;
                    case IntentionKind::ToggleOverlay:
                        switch (i.arg) {
                            case 1: stats = !stats; break;
                            case 2: frameOpts.view = stepView(frameOpts.view, i.secondary ? -1 : 1); break;
                            case 3: frameOpts.wireframe = !frameOpts.wireframe; break;
                            case 4: orientation = !orientation; break;
                            case 5: frameOpts.lighting = !frameOpts.lighting; break;
                            case 6: targeting = !targeting; break;
                            case 7: input = !input; break;
                            case 8: editSet = !editSet; break;
                            case 9: frameOpts.oit.view = (frameOpts.oit.view + 1) % 4; break;
                            case 10: bounds = !bounds; break;
                            default: break;
                        }
                        break;
                    default: break;
                }
            }

            // The orientation choice lives only while X is held (a quasimode):
            // releasing X returns placement to plain inference.
            if (!in.orientHeld) game.latch.clear();

            CameraInput move{.forward = in.forward, .right = in.right, .up = in.up};
            move.fast = in.fast;
            // Screen x right = turn right (yaw decreases); screen y down = look down.
            move.yawDelta = -state.mouseDX * kMouseSensitivity;
            move.pitchDelta = -state.mouseDY * kMouseSensitivity;
            state.mouseDX = state.mouseDY = 0.0;
            camera.update(move, dt);

            // Uses: fire on press, repeat while held.
            if (pick) game.pick(camera);
            for (Hand h : {Hand::Left, Hand::Right}) {
                const auto hi = static_cast<std::size_t>(h);
                for (int n = game.repeaters[hi].update(in.useHeld[hi], pressed[hi], now); n > 0; --n)
                    game.use(h, false, camera);
                if (pressedSecondary[hi]) game.use(h, true, camera);  // Shift+click (wrench: CCW); fires once
            }

            // Derived views follow the world: rebuild the mesh and lights after
            // edits (or a translucent face reorder from the F9 panel). The
            // translucent faces are ordered from the camera at build time.
            std::vector<ChunkId> stale = world.dirtyChunks();
            if (reorder) {
                for (const ChunkId& id : meshedChunks)
                    if (std::find(stale.begin(), stale.end(), id) == stale.end()) stale.push_back(id);
                reorder = false;
            }
            if (!stale.empty() || lightsGeneration != world.generation()) {
                const double t0 = glfwGetTime();
                for (const ChunkId& id : stale) {
                    WorldMesh mesh = world.buildMesh(id);
                    orderTranslucentFaces(mesh, order, camera.position);
                    renderer.setWorldMesh(mesh);  // one chunk: the renderer holds one mesh
                    meshedChunks.insert(id);
                    meshStats = {(glfwGetTime() - t0) * 1000.0, mesh.indices.size() / 6, meshStats.rebuilds + 1};
                }
                lights = lightsFromEmitters(world.emitters(), Renderer::kMaxPointLights);
                lightsGeneration = world.generation();
            }

            const std::optional<RayHit> target = world.raycast(camera.position, camera.forward(), kReach);
            std::vector<DebugLine> lines;

            // Preview (input/preview-mode): the main hand's planned edit — the
            // same edit set a click would apply. Fills/changes are ghosts,
            // removals red outlines. Holding X shows the placement ghost of a
            // held block even with preview off.
            const std::array<PlannedEdit, kHandCount> plans{game.plan(Hand::Left, false, camera),
                                                            game.plan(Hand::Right, false, camera)};
            const PlannedEdit& mainPlan = plans[static_cast<std::size_t>(game.hands.main())];
            std::vector<GhostCell> ghosts;
            const Item* held = game.mainItem();
            const bool xGhost = in.orientHeld && held && held->kind == Item::Kind::Block;
            if (game.preview || xGhost) {
                for (const Edit& e : mainPlan.edits.edits) {
                    if (!e.state.isAir()) ghosts.push_back({e.cell, e.state});
                    else if (!world.probe(e.cell).isAir()) outlineCell(e.cell, kRemovalColor, lines);
                }
                // Cells a consuming container can't afford: outlined, not placed.
                for (const CellPos& c : mainPlan.unaffordable) outlineCell(c, kUnaffordableColor, lines);
            }

            renderer.devUi().beginFrame();
            hud(game, target, plans, [&](std::uint32_t layer) { return renderer.blockIcon(layer); });
            crosshair();
            if (stats) statsOverlay(clock, gpuStats, ctx, camera, frameOpts, meshStats);
            if (orientation && target) orientationOverlay(*target, registry, renderer, camera, lines);
            if (frameOpts.lighting) lightingPanel(frameOpts.light, lights.size());
            if (targeting) targetingOverlay(camera, target, lines);
            if (input) inputOverlay(in, firedLog, now);
            if (bounds)
                for (const ChunkId& id : meshedChunks) boundsOverlay(id, lines);
            if (editSet) editSetOverlay(game, mainPlan);
            if (frameOpts.oit.view != 0) translucencyPanel(frameOpts.oit, order, reorder);

            if (opt.checkAbsorbance) check.frame(renderer, camera, registry);
            renderer.drawFrame(camera, frameOpts, lights, lines, ghosts);
        }
        if (opt.gpuReport) gpuStats.report();
        state.renderer = nullptr;
        state.intentions = nullptr;
        // renderer then ctx destruct here, before the window.
    }
    glfwDestroyWindow(window);

    const std::uint32_t validationErrors = validationStats().errors.load();
    if (validationErrors > 0) {
        std::fprintf(stderr, "[sandbox] %u validation error(s) reported\n", validationErrors);
        return 1;
    }
    if (opt.checkAbsorbance) return check.result(rc);
    return rc;
}
