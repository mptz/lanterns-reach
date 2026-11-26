# Versions — v0.1 point releases (history)

The full specifications of v0.1 through v0.1.9, moved from `versions.md`
when v0.1 closed. **History, not authoritative:** current decisions live in
`DESIGN.md` and `design/`, and current behaviour in the code and
`BUILD-NOTES.md`. Release references inside point to the roadmap as it was.

## v0.1 — Single lit textured block, free camera (graphics + input validation)
- Status: DONE 2026-10-28. Headless criteria pass; display criteria pass on
  lavapipe/Xvfb and were confirmed by a human on the GPU + display (renders,
  mouse-look + WASD navigation work), with zero validation errors (sync
  validation included) on the real GPU (see `BUILD-NOTES.md`). (Specified 2026-10-26; revised 2026-10-28 to a minimal
  deferred pipeline per `render/deferred-pipeline`.)
- **Purpose:** validate the Vulkan 1.3 rendering path and input end-to-end. This
  is a **throwaway validation harness**, NOT the first architectural slice —
  there is deliberately **no chunk concept, no persistence, no sim**. The cube is
  hardcoded. v0.2+ will introduce the real chunk interface; v0.1 exists only to
  prove the graphics/input/coordinate plumbing works.

### What it proves
- A window + Vulkan 1.3 device + swapchain come up and present.
- A textured, lit mesh renders with correct depth and face winding.
- A **minimal deferred pipeline** works end-to-end: multiple color attachments
  under dynamic rendering, a fullscreen lighting pass reading them, and correct
  barriers/image-layout transitions between passes via a first, minimal
  **render-graph seam** (`engine/vulkan-abstraction`). These inter-pass sync
  mechanics are the error-prone part of Vulkan, which a one-pass forward
  triangle never exercises — so v0.1 deliberately includes them.
- Input drives a camera in real time.
- The **f64-authoritative / f32-camera-relative-rebase** coordinate discipline
  (`core/f32-origin-relative`, `coords/f64-authoritative`) works in practice —
  the one piece of real architecture v0.1 deliberately DOES exercise, because
  it's cheap to do now and expensive to retrofit.

### Scope (fully specified)
- **The block:** one hardcoded cube, **24 vertices** (per-face normals, hard
  edges — not 8 shared/smoothed), **the same texture on all six faces**, one
  texture sampler. No material/appearance model (deferred; not designed yet).
- **Lighting:** flat/Lambert — `color = texture × (ambient + directional·max(0, N·−L))`.
  - **Low ambient (~0.15; tuned to 0.10 on 2026-10-28, see `BUILD-NOTES.md`)**
    so faces turned away from the light stay dim-but-
    visible (keeps the silhouette readable from every angle — this is why ambient
    is present, not decoration).
  - **One bright directional light**, fixed direction, slightly off-axis (down-
    and-to-one-side) so no face starts exactly head-on or edge-on and the shading
    reads immediately. Per-face normals make each face a different brightness, so
    orbiting the cube shows real 3D shape — the point of the demo.
  - No shadows, no specular, no attenuation.
  - Consistent with `light/visibility-only` (lighting is presentation-only).
  - The formula is evaluated in the **lighting pass** from the G-buffer (below),
    not in the geometry pass.
- **Render pipeline (minimal subset of `render/deferred-pipeline`):** three
  passes, declared through the render-graph seam (passes state which resources
  they read/write; the seam emits the `synchronization2` barriers and layout
  transitions — do not hand-place barriers inside pass code):
  1. **G-buffer pass** — draws the cube into:
     | Target | Format | Contents |
     |---|---|---|
     | GB0 | `R8G8B8A8_SRGB` | texture color RGB (A unused, write 1) |
     | GB1 | `R16G16_UNORM` | normal, **octahedral-encoded** |
     | Depth | `D32_SFLOAT` | depth (depth test + background detection) |
     This is a strict subset of `render/gbuffer-layout` (GB2/GB3/velocity are
     omitted — no material system or motion vectors in v0.1).
  2. **Lighting pass** — fullscreen triangle; samples GB0, GB1, depth; writes
     `color = albedo × (ambient + directional·max(0, N·−L))` to an **HDR target
     `R16G16B16A16_SFLOAT`**. Pixels with depth = 1.0 (no geometry) write a
     fixed background color instead of being lit.
  3. **Present pass** — fullscreen triangle; reads HDR, applies a simple tonemap
     (Reinhard or clamp is fine), writes the swapchain image (prefer an sRGB
     swapchain format so the hardware does the encode).
  - **Debug views** (number keys in v0.1/v0.1.1; moved to F2 per
    `input/debug-keys` in the next release that touches input, since number
    keys belong to the hotbars): number keys select what the present pass shows —
    `1` lit (default), `2` GB0 albedo, `3` GB1 decoded normal (as `N·0.5+0.5`),
    `4` linearized depth. This makes G-buffer contents individually verifiable.
  - Recreate the G-buffer/HDR targets on swapchain resize.
- **Camera:** 6DOF **free-fly**, mouse-look (pitch/yaw) + WASD (+ vertical on
  E/Q — revised 2026-10-28; superseded 2026-10-29 by C/V per
  `input/vertical-movement`, applied in the next release that touches input). **Tab** toggles
  mouse capture (click recaptures) so the window can be resized/moved. The
  **camera moves; the cube is static.**
- **Coordinates (the real-architecture part):**
  - Cube authoritative position is a **`WorldPos` (f64)** at the origin `(0,0,0)`;
    never moves.
  - Camera authoritative position is **also f64** — WASD integrates into the f64
    world position (NOT an f32 accumulator; accumulating motion in f32 would
    validate the wrong thing).
  - **Per-frame rebase:** `RenderPos_f32 = (f32)( cubeWorldPos_f64 −
    cameraWorldPos_f64 )` — subtract in f64, THEN downcast the result (never
    `(f32)world − (f32)origin`). The camera is the render origin; the view matrix
    is built around a camera at the f32 origin. At v0.1 the cube is at origin and
    the camera is a few units away, so the rebase is trivially correct — but doing
    it the real way now makes it free at ±10⁹ later.
  - Stub the typed **`WorldPos` / `RenderPos`** split (`coords/typed-positions`)
    so the downcast lives in exactly one place.
  - **z-up, right-handed** (`coords/axis-convention`); set pipeline `frontFace`/
    cull to match the z-up-RH→Vulkan-clip basis change so the cube does not cull
    inside-out (`engine/vulkan-abstraction` winding footnote). 1 unit ≈ 1 foot.

### Stack / dependencies
- C++ (**C++20**), **raw Vulkan 1.3 with dynamic rendering** (no render-pass/
  framebuffer objects), Linux-only (`engine/cpp-vulkan-linux`, `engine/graphics-api`).
- Libraries (all per `deps.md`, all current-standard for a Vulkan 1.3 app):

  | Dep | Role | Acquire via |
  |---|---|---|
  | Vulkan SDK (LunarG) | loader, validation layers, Vulkan-Hpp headers, `glslc`/`glslangValidator` | system package / LunarG tarball; sets `VULKAN_SDK` |
  | GLFW | window + input + `glfwCreateWindowSurface` | CMake `FetchContent` or system pkg |
  | VMA (VulkanMemoryAllocator) | GPU allocation | CMake `FetchContent` (header/impl) |
  | vk-bootstrap | instance / physical-device / device / swapchain | CMake `FetchContent` |
  | Vulkan-Hpp | RAII C++ handles | ships with the Vulkan SDK |
  | GLM | f32 render-side vector/matrix math | CMake `FetchContent` or system pkg |

  Pin each `FetchContent` dependency to a specific tag/commit (don't float on a
  moving branch) and record the pins in `CMakeLists.txt`. Prefer the newest
  stable release of each at build time; if one fails to configure, record the
  working pin in `BUILD-NOTES.md`.
- **Shader toolchain:** GLSL → SPIR-V **offline at build time** via **`glslc`**
  (or `glslangValidator`) from the Vulkan SDK; `.spv` baked into the build (a
  CMake custom command compiling `*.vert`/`*.frag` → `*.spv`). No runtime
  shader-compile dependency for v0.1.
- **Build system:** CMake (≥3.21). Out-of-source build.
- **GLM configuration note:** define `GLM_FORCE_DEPTH_ZERO_TO_ONE` (Vulkan clip
  space is depth 0→1, not GL's −1→1) and `GLM_FORCE_RADIANS`. Getting this wrong
  gives a cube that renders but with broken depth/projection.

### Suggested file layout (create under repo root; this is code, separate from `design/`)
```
/CMakeLists.txt            top-level build; FetchContent pins; shader-compile step
/src/
  main.cpp                 entry: init → loop → teardown
  vk_context.{h,cpp}       instance/device/swapchain via vk-bootstrap; VMA init
  renderer.{h,cpp}         frame loop; builds the 3-pass graph each frame
  render_graph.{h,cpp}     minimal seam: passes declare reads/writes; emits barriers/transitions
  gbuffer.{h,cpp}          G-buffer + HDR target creation (VMA), resize handling
  cube.{h,cpp}             hardcoded 24-vertex cube (pos, normal, uv)
  camera.{h,cpp}           f64 world pos + orientation; WASD/mouse integrate; view matrix
  coords.h                 WorldPos (f64) / RenderPos (f32) types + the rebase fn
  texture.{h,cpp}          load a texture, upload, sampler
/shaders/
  gbuffer.vert             MVP with camera-relative model; pass normal + uv
  gbuffer.frag             sample texture → GB0; octahedral-encode normal → GB1
  fullscreen.vert          fullscreen triangle (no vertex buffer)
  lighting.frag            read GB0/GB1/depth; ambient + directional Lambert → HDR
  present.frag             tonemap HDR → swapchain; debug-view switch
/assets/
  block.png                any small texture (checker/brick ok; generate if absent)
```
(Exact names are a suggestion; keep the `coords.h` rebase-in-one-place split.)

### Build & run
- **Compile (headless-safe — no display needed):**
  `cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug && cmake --build build`
  Must compile, link, and compile shaders to SPIR-V with **no errors**. Enable
  Vulkan **validation layers** in Debug.
- **Run (needs GPU + display, i.e. the non-headless machine):**
  `./build/<binary>` → a window showing the lit cube; WASD + mouse fly the
  camera. Clean exit on window close / Esc, no validation-layer errors on the
  console.

### Acceptance criteria
Headless (checkable on the build machine):
1. Clean configure + build + shader compile, zero errors/warnings-as-errors.
2. `coords.h` rebase is `(f32)(worldF64 − originF64)` — subtract in f64 THEN
   downcast; grep shows no `(f32)world − (f32)origin` pattern. Camera and cube
   positions are f64.
3. Vulkan 1.3 **dynamic rendering** used (no `VkRenderPass`/`VkFramebuffer`
   objects created).
4. Three passes (G-buffer, lighting, present) are declared through the
   render-graph seam; pass code contains no direct `vkCmdPipelineBarrier*` calls
   (barriers come from the seam).

Display-required (list for human confirm on the GPU machine if not runnable here):
5. A textured cube is visible, correct depth (no z-fighting / inverted depth).
6. Flying around it, the six faces show **different, correct brightness** (a lit
   face is bright, a face turned away is dim-but-visible, never pure black) —
   proves per-face normals + ambient + directional all work.
7. No face culls inside-out from any angle (correct `frontFace`/winding).
8. Mouse-look + WASD move the camera smoothly in real time; cube stays put.
9. Debug views `2`/`3`/`4` show plausible albedo, normals (each face a distinct
   flat color that stays fixed to the face as the camera moves) and depth.
10. **Zero validation-layer errors**, including synchronization validation
    (enable `VK_VALIDATION_FEATURE_ENABLE_SYNCHRONIZATION_VALIDATION_EXT` in
    Debug) — the check that the seam's barriers are actually right.
11. Resizing the window works (targets recreated, no errors, no stretching).

### Out of scope for v0.1 (do NOT build)
No chunks, no world/terrain, no worldgen or noise, no persistence/save, no
simulation, no multiple blocks, no material system, no second light, no shadows,
no GUI, no anti-aliasing (no TAA/SMAA, no velocity target), no transparency/OIT,
no SSAO/SSR, no material table or GB2/GB3, no clustered light list. Those arrive
in later versions; adding them now is a scope error.

### Notes for the record
- **The rebase is industry-standard, not novel:** "camera-relative rendering" /
  "floating origin" — Unreal's Large World Coordinates, Star Citizen, KSP,
  No Man's Sky, Cesium all do f64-authoritative + f32-camera-relative render.
  Performance is net-positive: one f64 subtract per object per frame (CPU-side,
  full-speed on x86-64), folded into the model→view matrix; the GPU sees only
  small f32 values and never does f64 (which would be 1/16–1/64 rate). It is
  *how* f32 rendering is made viable at scale, not a tax on it.
- At v0.1 the cube is at the f64 origin and the camera a few units away, so the
  rebase result is tiny and precision is trivially fine — the point of doing it
  the real way now is that the SAME code is correct at the ±10⁹ world bound later
  (`coords/f64-authoritative`), with zero rework.

---

## v0.1.1 — Four textured blocks, 2×2 (texture/multi-object validation)
- Status: DONE 2026-10-28. Headless criteria pass; confirmed by a human on
  the GPU + display (four blocks render, navigation works).
- **Purpose:** a small delta on v0.1. It proves per-object textures and the
  per-object f64→f32 rebase with more than one object. It is still a
  throwaway harness: **no chunk concept**, blocks are hardcoded.

### Scope (delta from v0.1; everything else unchanged)
- **Four blocks**, each a unit cube with **one uniform texture on all six
  faces** (no per-face textures, e.g. no grass-top/dirt-side):
  | Block | `WorldPos` (cube center) | Texture |
  |---|---|---|
  | stone brick (the v0.1 block) | (0, 0, 0) | `assets/block.png` |
  | dirt | (2, 0, 0) | `assets/dirt.png` |
  | natural stone | (0, 2, 0) | `assets/stone.png` |
  | grass | (2, 2, 0) | `assets/grass.png` |
  This is 2×2 on the horizontal (x,y) plane with **one-block gaps** between
  neighbors. Positions are f64 and each is rebased against the camera per
  frame through `coords.h` `rebase()`.
- One descriptor set (texture + sampler) per block, and one draw per block in
  the G-buffer pass. Lighting, passes, debug views and controls are as in v0.1.
- The camera starts at (−3.5, −2.5, 3.2) looking at the group center (1, 1, 0).
- Textures are 32×32, generated by `tools/gen_block_texture.py` (deterministic).

### Acceptance criteria
v0.1's criteria still hold. In addition:
1. Four blocks are visible in the stated arrangement: from the start view,
   dirt is to the right of the brick, stone to the left, grass behind.
2. Each block shows its own texture on all faces, and depth occlusion between
   blocks is correct from any angle.

### Out of scope
Everything out of scope for v0.1 still is. In particular: no chunks, no block
types/registry, no per-face textures, no texture arrays/atlas, no instancing.

---

## The v0.1.x line — shared rules for v0.1.2 through v0.1.8
- Status: SPECIFIED 2026-10-29; v0.1.2–v0.1.4 built 2026-10-29, v0.1.5 and v0.1.6 2026-10-30.
- **Still the throwaway harness.** No terrain/worldgen/noise, no persistence,
  no simulation, no ledger, no inventory, no body (free camera only). Each
  point release validates one thin layer of the eventual design so that v0.2
  (the real chunk interface) inherits working pieces. Build them **in order**;
  each assumes the previous ones.

  | Version | Adds | Validates |
  |---|---|---|
  | v0.1.2 | hygiene, dev UI (ImGui), F-key overlays, light tests, new harness bindings | tooling the rest depends on |
  | v0.1.3 | block orientation (24-rotation group, 11 symmetry types), JSON block definitions, per-face textures, material ID | `world/block-orientation` |
  | v0.1.4 | PBR shading, 16 dynamic point lights, emissive blocks, AgX tonemap | `render/deferred-pipeline`, `render/gbuffer-layout` |
  | v0.1.5 | intention layer, two hotbars, editable in-memory grid behind `WorldQuery`, raycast targeting, place/break | `input/*`, placement rule |
  | v0.1.6 | MBOIT commutative translucency + order-invariance test | `render/transparency` |
  | v0.1.7 | preview mode, edit sets, wrench, shovel, orientation ghost, held-container fill with container placement modes | `input/preview-mode`, wrench, `inventory/hand-pointers` |
  | v0.1.8 | 16×16 block textures, anti-aliased nearest sampling, mipmaps, Wang variants | `render/block-textures` |

  (v0.1.6 and v0.1.7 are ordered so preview ghosts use the real translucency
  path — no throwaway blend path is built.)
- **Harness architecture that later versions keep:**
  - **`WorldQuery` seam (from v0.1.5).** All world access by targeting, tools
    and preview goes through a minimal interface shaped by
    `core/opaque-chunk-queries` — **no bulk cell access**:
    - `raycast(origin, dir, max_dist) → optional<Hit{cell, face normal, hit
      point, BlockState}>` — the world answers the ray itself (a uniform region
      can be skipped in one step), so targeting never loops over cells;
    - `probe(cell) → BlockState` — a **single-cell semantic probe for bounded,
      edit-local reads only**: pick-block, placement checks, a tool reading its
      own small footprint (the shovel's 5×5). Never called in a loop over a
      region by renderers, lighting or sim;
    - `apply(const EditSet&)`;
    - bulk consumers ask the world for results, not cells: `buildMesh()` (the
      world meshes itself) and `emitters()` (lamp positions for point lights).
    Behind it in v0.1.x is a **dense in-memory grid** (throwaway); v0.2 swaps
    real chunks in underneath without touching callers. (Revised 2026-10-29
    before v0.1.5 was built: the earlier `block_at(cell)` was the `getVoxel`
    `core/opaque-chunk-queries` forbids, which v0.2's chunks could not have honoured.)
  - **`BlockState` = block type id + canonical orientation index**
    (`world/block-orientation`). That is the whole block state in v0.1.x.
  - The f64 `WorldPos` / f32 `RenderPos` rebase in `coords.h` stays the only
    downcast site, for every object including the grid mesh.
- **Testing is deliberately light.** One test executable (`sandbox_tests`)
  with a minimal in-house `CHECK(expr)` macro (no test framework dependency),
  registered with CTest; plus Python tool checks registered as CTest tests.
  `ctest --test-dir build` must pass headlessly. Test pure logic only (math,
  group tables, raycast, placement rule, edit sets) — no GPU in tests.
- **Display-required checks may run on lavapipe under Xvfb**
  (`tools/headless_check.sh`) where noted; the human on the GPU machine
  confirms the rest, as in v0.1.
- **Out of scope for the whole v0.1.x line** (do NOT build): chunks, meshing
  beyond the naive culled mesh of v0.1.5, worldgen/noise, persistence, entity
  ledger, inventory/encumbrance/pouches, item entities or dropping, a player
  body (gravity, collision, crouch, jump, swim), TAA/SMAA/velocity, clustered
  light lists, SSAO/SSR, shadows, refraction, player-facing text/UI (MSDF etc.),
  rebinding UI, multiplayer.

---

## v0.1.2 — Hygiene, dev UI, debug overlays, light tests
- Status: BUILT 2026-10-29. Headless criteria pass (ctest; lavapipe/Xvfb
  runs with F1 + F3 open, zero validation errors); key handling and
  real-GPU checks await human confirmation (see `BUILD-NOTES.md`).
- **Purpose:** close v0.1's follow-ups and put in the tooling every later
  version uses: Dear ImGui, the F-key overlay map, a test target, and the
  final harness bindings.

### Scope (delta from v0.1.1)
- **Validation must not fail silently.** At startup print
  `[sandbox] validation: on` or `off`. In Debug builds, if the validation layer
  was requested but is not available, **exit non-zero with a clear message**
  (use vk-bootstrap's `enable_validation_layers` / check the returned instance,
  not `request_validation_layers`).
- **Dev UI** (`render/dev-ui`, `deps/dev-ui`): Dear ImGui via `FetchContent`
  (pinned to a commit SHA) with `imgui_impl_glfw` + `imgui_impl_vulkan`, drawn as **one
  render-graph pass after the present/tonemap pass**, onto the swapchain image,
  at native resolution. While ImGui wants the mouse/keyboard, game input
  ignores it.
- **Debug overlays on function keys** (`input/debug-keys`); each toggles:
  - **F1 — stats:** FPS and frame time; **per-pass GPU time** from timestamp
    queries (G-buffer, lighting, present, UI); camera f64 world position; GPU
    name; validation on/off.
  - **F2 — buffer view, cycles** lit → albedo → normal → depth → lit;
    **Shift+F2** cycles backward. **Replaces v0.1's `1`–`4` keys** (number keys
    now belong to hotbars and do nothing until v0.1.5).
  - **F3 — wireframe overlay:** block edges drawn as lines over the final
    image, depth-tested against the G-buffer depth with a small bias (needs
    `fillModeNonSolid` or line-list geometry).
  - F4–F10 are reserved for later versions (see `input/debug-keys`); unused
    keys do nothing.
- **Harness bindings** (from `input/bindings`, harness subset):
  - **C / V** = up / down (world z) — replaces E/Q, which now do nothing.
  - **Ctrl** = 4× fast-fly — replaces Shift (Shift is now only a modifier).
  - **Esc** releases mouse capture (left-click recaptures); **Esc while
    already released quits**; closing the window quits. The **recapture click
    is swallowed**: it never reaches game input (from v0.1.5, the intention
    layer), so recapturing can't also break a block. **Tab does nothing**
    (it becomes the inventory key later).
  - Mouse-look and WASD unchanged.
- **Cosmetic fixes:** one version label everywhere (CMake project, Vulkan app
  name, window title, all `v0.1.2`); fix the stale "single block texture"
  comment in `texture.h`; debug views use the **exact sRGB transfer function**
  instead of pow 2.2; `tools/gen_block_texture.py` creates its output directory.
- **Tests:** add `tests/` with `sandbox_tests` (minimal `CHECK` macro, CTest).
  First tests: `rebase()` is exact for a large offset — e.g. world
  `1e9 + 0.25`, camera `1e9` → exactly `0.25f` — and a comment-documented
  counterexample showing the downcast-first form would not be.

### Acceptance criteria
Headless:
1. Clean configure + build; `ctest --test-dir build` passes.
2. Running a Debug build with the layer disabled (`VK_LOADER_LAYERS_DISABLE=~all~`)
   prints `validation: off` and an error, and exits non-zero; with the layer
   present it prints `validation: on` and runs.
3. Version label is `v0.1.2` in all three places.

Display-required:
4. F1 shows plausible stats including per-pass GPU times; F2/Shift+F2 cycle all
   four views in both directions; F3 shows edges over the blocks.
5. C/V move vertically, Ctrl speeds up, Esc behaves as specified (the
   recapture click has no other effect), E/Q/Tab/1–4 do nothing.
6. ImGui drawn crisp at native resolution; zero validation errors (sync
   validation on) with overlays open.

### Out of scope
No new rendering features, no block changes, no input layer yet (bindings are
still read directly; the intention layer arrives in v0.1.5).

---

## v0.1.3 — Block orientation and JSON block definitions
- Status: BUILT 2026-10-29. Headless criteria pass (ctest incl. group
  tests, validator and engine-loader rejection of the negative fixtures);
  display criteria pass on lavapipe/Xvfb and await human confirmation on
  the GPU (see `BUILD-NOTES.md`).
- **Revised after build (2026-10-29):** the mandatory stabilizer-invariance
  check (validator check 3 below, and acceptance criterion 2's
  "not 180°-symmetric" fixture) is superseded by optional artist-declared
  texture assertions and a canonical transversal. Implemented in v0.1.4's
  carry-over item; the text below is left as built.
- **Purpose:** implement `world/block-orientation` for real — the rotation
  group, the 11 self-symmetry types, canonicalization, per-face textures that
  must agree with the declared symmetry — and the first data-driven block
  definitions. This is mostly pure math, so it is mostly checked headlessly.

### Scope
- **The group.** The 24 proper rotations of the cube as **integer 3×3
  matrices**, indexed 0–23 (index 0 = identity), with precomputed tables:
  composition (24×24), inverse, and face permutation (each rotation maps each of
  the 6 faces `+x −x +y −y +z −z` to a face, plus the in-plane quarter-turn
  count of that face's texture frame — see below).
- **The 11 types and their standard positions** (block local frame, +z =
  attach direction, −y = front). Each subgroup H is given by generators;
  `R_a(θ)` = rotation by θ about axis a:

  | Keyword | Generators of H (standard position) | \|H\| | Orientations |
  |---|---|---|---|
  | `uniform` | all 24 | 24 | 1 |
  | `column` | R_z(90°), R_x(180°) | 8 | 3 |
  | `pointed` | R_z(90°) | 4 | 6 |
  | `brick` | R_x(180°), R_y(180°) | 4 | 6 |
  | `twisted-column` | R_z(180°), R_(1,1,0)(180°) | 4 | 6 |
  | `pointed-brick` | R_z(180°) | 2 | 12 |
  | `edge` | R_(0,−1,1)(180°) (the +z/−y edge) | 2 | 12 |
  | `corner` | R_(1,−1,1)(120°) (the +x,−y,+z vertex) | 3 | 8 |
  | `diagonal` | R_(1,−1,1)(120°), R_(1,1,0)(180°) | 6 | 4 |
  | `tetrahedral` | R_z(180°), R_(1,−1,1)(120°) | 12 | 2 |
  | `oriented` | identity only | 1 | 24 |

  These standard positions are a v0.1.3 choice and will be promoted into the
  grammar (`data/grammar`) when it exists. The `edge`/`corner`/`diagonal`
  features sit on the attach/front side by convention. (Check: R_(1,1,0)(180°)
  is perpendicular to (1,−1,1), as `diagonal` requires.)
- **Canonicalization:** a block placed with rotation g has orientation coset
  gH; store the **lowest rotation index in gH**. Precompute a 24-entry table per
  type. `BlockState` = (type id, canonical index).
- **Face texture frames** (needed to rotate textures and to check symmetry):
  for face normal n, texture "up" U = **+z for the four side faces** and **+y
  for ±z**; texture "right" = U × n (so +z viewed from above has +x to the right,
  +x viewed from outside has +y to the right). A rotation maps a face's frame to
  another face's frame up to a quarter-turn count k ∈ {0,1,2,3}; that k is the
  in-plane texture rotation stored in the face-permutation table.
- **JSON block definitions:** `assets/blocks/<name>.json`, temporary until the
  grammar (`data/grammar`) replaces JSON with grammar text files:
  ```json
  { "name": "log", "symmetry": "column",
    "textures": { "+z": "log_end.png", "-y": "log_side.png" } }
  ```
  - `textures` has **exactly one entry per orbit of H on the faces**, keyed by
    the orbit's **representative face = the first face of the orbit in the
    order `+z, -z, -y, +y, +x, -x`** (attach, bottom, front, back, right,
    left). Other faces in the orbit get the representative's texture, rotated
    by the frame mapping.
  - Read with **nlohmann/json** (`FetchContent`, pinned). The engine refuses to
    load a definition whose keys don't match the orbit representatives
    (clear error, non-zero exit in Debug).
- **Validator tool:** `tools/validate_blocks.py` (Python 3 standard library
  only; decode the PNG subset the generator writes: 8-bit RGB/RGBA,
  non-interlaced). For every definition it checks:
  1. the symmetry keyword is one of the 11;
  2. the texture keys are exactly the orbit representatives;
  3. **in-plane invariance:** each representative texture equals itself rotated
     by every in-plane rotation its face stabilizer in H induces (pixel-exact);
  4. optional `--lint`: warn if a texture's actual symmetry exceeds what H
     requires (declared symmetry smaller than the art's).
  Registered as a CTest test over `assets/blocks/`.
- **Rendering changes:**
  - Block textures move to one **texture array** (`sampler2DArray`); a small
    per-type **material table** (SSBO) maps each face to an array layer and
    in-plane rotation. Indexed by material id.
  - Add **GB3 = `R16_UINT` material id** to the G-buffer (`render/gbuffer-layout`).
  - Each placed object carries its rotation index (push constant); the vertex
    shader applies the rotation matrix and the face/UV mapping.
  - F2 cycle gains a **material-id** view (hash id → color).
  - **F4 — orientation overlay:** draws each block's local axes (+x red, +y
    green, +z blue) and prints its symmetry keyword + canonical index.
- **Test content:** keep the four v0.1.1 blocks (now as `uniform` definitions).
  Add, generated deterministically by the texture tool, which must produce
  textures with the required symmetry:
  - **log** (`column`): 4-fold-symmetric end rings, 180°-symmetric vertical bark
    sides; shown in its **3 distinct orientations** (axis along x, y, z), plus a
    **fourth log** authored as the z-axis log turned R_z(90°) about its own
    axis.
  - **furnace** (`oriented`): six distinct face textures (front clearly marked,
    e.g. an opening, plus an arrow on top pointing to the front); shown in
    **6 orientations** including facing up and facing down.
  Positions (block centers, z = 0): logs at y = −3, x = 0, 2, 4, 6; furnaces at
  y = −6, x = 0, 2, …, 10.
- **Tests (`sandbox_tests`):** the 24 matrices are distinct, closed under
  composition, and the tables agree with matrix multiplication; each type's H
  generated from its generators has the stated order; canonicalization is
  idempotent and constant on cosets, giving exactly 24/|H| classes; face-orbit
  counts per type match the validator's; **wrench reachability**: BFS over the
  six face quarter-turns reaches all 24 rotations in ≤ 3 steps (distance
  histogram 1 / 6 / 11 / 6 for 0 / 1 / 2 / 3 steps).

### Acceptance criteria
Headless:
1. Build + `ctest` pass, including group tests and `validate_blocks.py` on all
   shipped definitions.
2. A deliberately broken definition (wrong key set, or a side texture that is
   not 180°-symmetric for `column`) is rejected by both the validator and the
   engine loader (keep these as negative test fixtures under `tests/`).

Display-required:
3. The three logs show three different axes. The fourth log looks identical
   to the z-axis log and F4 shows the **same canonical index** for both
   (rotation about a block's own symmetry is not a distinct state).
4. Furnaces facing up and down render with the front texture on the correct
   face; no texture appears mirrored anywhere.
5. F2 material-id view and F4 overlay correct; zero validation errors.

### Out of scope
No placement or wrench interaction (v0.1.5/v0.1.7), no non-solid (model)
blocks, no grammar parser, no bindless descriptor indexing beyond the texture
array.

---

## v0.1.4 — PBR shading, dynamic point lights, emissive blocks
- Status: DONE 2026-10-29. Headless criteria pass (ctest incl. the
  texture-assertion fixtures); display criteria 2–4 confirmed by a human on
  the GPU + display; criterion 5's HD 530 per-pass timing recorded in
  `BUILD-NOTES.md` (GPU total ≈ 8–9 ms at 1080p, vsync-capped at 60 fps).
- **Purpose:** test the main reason for deferred shading — many small dynamic
  lights (`render/deferred-pipeline`) — and the material layout of
  `render/gbuffer-layout`.

### Scope
- **Materials:** block definitions gain optional `"roughness"` (default 0.8),
  `"metallic"` (default 0), `"reflectance"` (default 0.5) and `"emissive"`
  (linear RGB, default 0). Written per pixel to **GB2 = `R8G8B8A8_UNORM`**
  (roughness, metallic, reflectance, shading-model bits = 0).
- **Emission** is written directly into the **HDR target during the G-buffer
  pass** (cleared to 0 first); the lighting pass adds its result on top
  (additive blend), per `render/gbuffer-layout`.
- **BRDF** (glTF metallic-roughness reference): Lambert diffuse; GGX normal
  distribution; height-correlated Smith-GGX visibility; Schlick Fresnel with
  dielectric `F0 = 0.16 · reflectance²` and metallic `F0 = albedo`;
  `α = roughness²`.
- **Lights:**
  - Keep the directional light and ambient (now through the BRDF).
  - Add **16 point lights** in an SSBO; the lighting pass loops over **all of
    them per pixel** (brute force — clustering is out of scope).
  - Falloff: inverse-square with a smooth window to zero at radius r —
    `I · saturate(1 − (d/r)⁴)² / (d² + 1)`.
  - **Two emissive "lamp" blocks** (new `uniform` definition with `emissive`
    set), each with a co-located point light. At least **4 lights move**
    (e.g. orbit the scene) to show that dynamic lights cost nothing extra.
  - A static **16×16 floor** of stone blocks at z = −1 as one merged mesh, so
    light falloff is visible (hardcoded; replaced by the grid in v0.1.5).
- **Tonemap:** **AgX** replaces Reinhard in the present pass, with a fixed
  exposure that is adjustable in an ImGui panel (also: light intensities,
  pause light motion).
- **Overlays:** F2 cycle gains **roughness/metallic**; **F5 — lighting:** draws
  point-light positions and radius spheres, and a **per-pixel light-count
  heatmap** (lights whose radius covers the pixel).
- **Tests:** falloff is exactly 0 at d ≥ r and continuous; `F0` formula values
  for reflectance 0 / 0.5 / 1.
- **Carried over from v0.1.3 — texture assertions replace the mandatory
  stabilizer check** (`world/block-orientation`, revised 2026-10-29 after
  v0.1.3 was built):
  - **The transversal is the spec, not an implementation detail.** Each
    non-representative face takes its representative's texture turned by the
    **lowest-index h ∈ H** carrying the representative onto it (what v0.1.3
    already does). Asymmetric textures are legal and render in that fixed
    orientation. Rewrite the `sandbox_tests` "g vs g·h render identically"
    check so it relies on canonical storage (g and g·h canonicalize to the
    same index), not on texture symmetry.
  - **Drop the always-on invariance check** (validator check 3, and the
    engine loader's equivalent). The `--lint` "art more symmetric than H"
    warning stays.
  - **Optional per-texture assertions**, keyed by orbit representative like
    `textures`:
    ```json
    { "name": "log", "symmetry": "column",
      "textures": { "+z": "log_end.png", "-y": "log_side.png" },
      "asserts":  { "+z": ["invariant"], "-y": ["invariant", "tileable"] } }
    ```
    Vocabulary: `invariant` (equal to itself under every in-plane turn the
    face's stabilizer in H induces), `rot2` (180°), `rot4` (90°, implies
    `rot2`), `tileable`. A false assertion is a **hard error** in both the
    validator and the engine loader, with the same reason text style as
    today. Unknown assertion names, and keys that aren't orbit
    representatives, are errors.
  - **`tileable` metric** (rotations are pixel-exact; tiling can't be): per
    axis, the mean absolute RGB difference across the wrap seam (last
    column vs first, last row vs first) must be ≤ **1.5 × the mean
    difference between interior neighbours along that axis + 1** (8-bit
    units; the +1 keeps flat textures from failing on rounding). Same
    integer arithmetic in Python and C++ so both sides agree exactly.
  - **Fixtures:** `tests/blocks_bad/asym_side` now *asserts* `invariant` on
    its asymmetric side and must still be rejected; add a positive fixture
    loading the same asymmetric side with no assertion, which must pass both
    validator and loader; add a negative fixture asserting `tileable` on a
    texture with a deliberate seam. Shipped definitions assert `invariant`
    wherever the generator guarantees symmetry (all of them today), and
    `tileable` wherever the generator guarantees wrapping (record which in
    `BUILD-NOTES.md`; at least one).

### Acceptance criteria
Headless:
1. Build + `ctest` pass, including the texture-assertion fixtures (asserted
   asymmetry rejected, unasserted asymmetry accepted, false `tileable`
   rejected) in both validator and loader.

Display-required:
2. Moving lights illuminate floor and blocks smoothly with **no visible edge
   at the light radius**; lamps glow and light their surroundings.
3. A low-roughness and a high-roughness block (add one of each as test
   definitions) show clearly different highlights; one metallic block tints its
   highlight by albedo.
4. F5 heatmap never exceeds 16; zero validation errors.
5. Record F1 per-pass GPU times at 1080p on the reference GPU (Intel HD
   Graphics 530) in `BUILD-NOTES.md` (a measurement, not a pass/fail target).

### Out of scope
Clustered/tiled light lists, shadows, SSAO, IBL/environment lighting, velocity
target, TAA.

---

## v0.1.5 — Intention layer, hotbars, editable grid, targeting, place/break
- Status: DONE 2026-10-30. Step 0 (third-party review) done; one flag
  resolved before feature work. Headless criteria pass; display criteria
  confirmed by a human on the GPU + display (no validation errors, stable
  60 fps, interactions and placements as specified). Criterion 3's wall-furnace
  clause is superseded by the placement rule as written (owner's decision,
  2026-10-30; see `BUILD-NOTES.md`).
- **Purpose:** the first gameplay input. Validates the intention layer, the
  per-hand model (`input/per-hand-use`, `input/hotbars`), the placement
  inference rule (`world/block-orientation`) and the `WorldQuery` seam.

### Scope
- **First step, before any feature work — third-party verification and
  security review** (`deps/acquisition`; added 2026-10-29 after the
  dependencies were pinned to commit SHAs):
  - **Verify the pins.** For each `FetchContent_Declare` in `CMakeLists.txt`:
    the repository URL is the project's canonical upstream (not a fork or
    lookalike); the SHA is the commit its tag comment names (`git ls-remote
    <url> refs/tags/<tag> 'refs/tags/<tag>^{}'`); and the fetched source in
    `build/_deps/<name>-src` is at that SHA (`git rev-parse HEAD`) with no
    local modifications (`git status --porcelain` empty).
  - **Review what runs at configure/build time first** — the highest-risk
    surface, since it executes before anything is compiled: every
    `CMakeLists.txt` / `*.cmake` the fetch pulls in, looking for
    `execute_process`, `file(DOWNLOAD …)`, `ExternalProject`, nested
    `FetchContent`, custom commands/targets, and anything writing outside the
    build tree.
  - **Review the code we actually compile and link,** not each repo's whole
    tree: list the translation units and headers our targets use (e.g. only
    stb_image from stb; only the core files + `imgui_impl_glfw` /
    `imgui_impl_vulkan` from ImGui). Flag networking (sockets, HTTP),
    process spawning (`system`, `popen`, `exec*`, `fork`), dynamic loading
    beyond the expected Vulkan loader / windowing libraries (`dlopen`), file
    access outside what the game requests, environment-variable-driven
    behaviour, obfuscated or encoded blobs, and inline assembly outside
    known SIMD paths. Note memory-safety hot spots in input parsers we feed
    data to (stb_image, nlohmann/json), since those read files.
  - **Record findings in `BUILD-NOTES.md`:** per dependency, what was
    reviewed (files, line counts), how (read vs. pattern search), anything
    flagged and its resolution. **State the limits honestly:** a review of
    this size reduces risk and catches gross problems; it cannot prove the
    absence of a subtle, deliberately hidden backdoor.
  - **If anything is flagged:** stop and report it before continuing with
    v0.1.5; do not patch third-party code in place.
- **Editable grid behind `WorldQuery`** (see shared rules): dense **32×32×32**
  `BlockState` array; cell (0,0,0) at `WorldPos` (0,0,0), z up. Initial
  contents: z = 0–14 stone, z = 15 grass, everything above empty; the v0.1.3
  logs/furnaces and v0.1.4 lamps placed on the surface. Replaces all hardcoded
  blocks and the v0.1.4 floor. Camera starts at (−4, −4, 24) looking at
  (16, 16, 16).
- **Naive culled mesh (throwaway):** the grid meshes **itself**
  (`WorldQuery::buildMesh()`, no caller reads cells to build it): one mesh
  emitting only faces whose neighbor is empty or outside the grid; **rebuilt
  fully after each edit**. No greedy merging. Vertices relative to the grid
  origin, rebased through `coords.h`. Point lights for lamps come from
  `WorldQuery::emitters()` (first 16).
- **Intention layer** (`sim/player-embodiment`, `input/*`): raw input maps to
  intentions through a **stack of mapping contexts**: a base gameplay context,
  and a dev-UI context pushed while ImGui wants input. Intentions:
  `move`, `ascend`, `descend`, `fast`, `use(hand, primary|secondary)`,
  `select_slot(hand, i)`, `scroll(hand, ±1)`, `bare(hand)`, `swap_hotbars`,
  `pick_block`, `toggle_overlay(k)`. Bindings live in **one table in one source
  file** mirroring `input/bindings`; no rebinding UI.
- **Hands and hotbars** (`input/per-hand-use`, `input/hotbars`):
  - LMB = left hand, RMB = right hand; **handedness = right** (main = right).
  - Two hotbars, **≤ 9 slots, no empty slots**; each hotbar's selection is a
    slot or **bare**. Contents are **hardcoded creative stacks** (infinite):
    - right (main): stone, dirt, grass, brick, log, furnace, lamp;
    - left (off): pickaxe.
  - Wheel / 1–9 select on the main hotbar; **Shift + wheel / Shift + 1–9** on the
    off hotbar; only Shift changes this. The wheel **wraps** and skips bare.
  - **H** / **Shift+H** set main / off hand bare; selecting any slot leaves bare
    (the wheel resumes from the **last selected slot**).
  - **R** swaps the two hotbars, each with its current selection.
  - **MMB pick-block:** if the targeted block's type is on the main hotbar,
    select that slot; otherwise replace the main hand's selected slot with it
    (creative).
  - 0, E, G, T, Tab do nothing yet.
- **Items (harness behavior):** a **block stack** places; the **pickaxe**
  breaks; a **bare hand** also breaks (creative: breaking is instant for both).
  A use fires on press, and **repeats every 200 ms while held** after a 250 ms
  delay. Secondary actions do nothing yet.
- **Targeting:** DDA voxel raycast **inside** `WorldQuery::raycast` (the grid
  walks its own cells), **reach 12 blocks** (revised 2026-10-30 from 16); returns hit cell, face normal n,
  hit point on the face and the hit `BlockState`. Break removes the hit cell;
  place puts a block in the empty cell adjacent across n, if inside the grid.
- **Placement orientation:** the rule in `world/block-orientation` — inputs n,
  f (direction toward the player snapped perpendicular to n) and hit point —
  implemented for **all 11 types**, then canonicalized. Hit-point selection is
  plain nearest vertex/edge (no dead zones; region tuning is open).
- **Crosshair** (added 2026-10-30): always on, at the screen center, marking
  the targeting ray.
- **HUD (ImGui text, no hand graphics):** each hand's hotbar with the selected
  slot highlighted, or `bare`; target readout (cell, face, block type,
  canonical orientation).
- **Overlays:** **F6 — targeting** (ray, hit face outline, normal, hit-point
  regions); **F7 — input** (context stack and the intentions fired this
  frame); **F10 — grid bounds**.
- **Tests:** raycast (axis-aligned, diagonal, grazing an edge, starting inside a
  solid cell, leaving the grid); placement rule expected orientation for each
  type for a set of (n, f, hit point) cases; hotbar logic (wrap, bare, resume,
  swap carries selection, Shift routing); edit apply.

### Acceptance criteria
Headless:
0. Third-party verification done and recorded in `BUILD-NOTES.md`: every pin
   checked against its canonical repo and tag, fetched trees clean at their
   SHAs, build-time scripts and compiled sources reviewed, findings and the
   review's limits stated. Nothing flagged, or every flag reported and
   resolved before feature work began.
1. Build + `ctest` pass.

Display-required:
2. Breaking and placing work at up to 12 blocks with no frame hitch noticeable
   at this grid size; the mesh never shows internal faces.
3. A log placed against a wall lies horizontally along the wall's normal; a
   furnace placed on a floor faces the player; one placed on a wall lies on
   its back (top out of the wall, front toward the player), as the rule in
   `world/block-orientation` gives. (Reworded 2026-10-30: the original "faces
   out of the wall" contradicted the rule; the owner kept the rule.)
4. Hotbar behavior matches the spec (wheel wrap, Shift routing, H/Shift+H, R,
   MMB pick); HUD text always matches what LMB/RMB actually do.
5. Zero validation errors.

### Out of scope
Preview, ghosts, wrench, shovel, X/Q (v0.1.7); translucent blocks (v0.1.6);
greedy meshing and chunks (v0.2); a body, gravity or collision.

---

## v0.1.6 — Commutative translucency (MBOIT) and the order-invariance test
- Status: DONE 2026-10-30 (specified 2026-10-29, revised 2026-10-30).
  The boundary-integral moment definition was verified against the paper
  before building (it holds). Headless criteria 1–6 pass on lavapipe;
  display criteria 7–9 confirmed by the owner on the GPU (see
  `BUILD-NOTES.md`).
- **Purpose:** prove `render/transparency`'s central claim — translucent
  geometry composites correctly **without sorting**, so draw order does not
  matter. Comes before preview mode so ghosts (v0.1.7) use this path.

### Scope
- **Translucent block definitions:** `"translucency": { "absorption": [r,g,b],
  "emission": [r,g,b] }` (linear; absorption is σ per block of path length).
  New blocks: **tinted glass** in three colors (absorption only) and a
  **glow block** (emission only, no absorption). **Group = block type:** each
  tint is its own group. **The glow block is also a light source** (revised
  2026-10-30): `emitters()` includes translucent blocks with non-zero
  `translucency.emission`, so it lights its surroundings like a lamp (same
  point-light parameters, colored by its emission, within the 16-light cap),
  as well as reading bright through the translucency passes. Known artifact,
  accepted: point lights ignore occlusion (no shadows yet), so light from a
  glow block behind tinted glass reaches surfaces in front of the glass
  untinted, the same class of artifact as lamps lighting through stone. A
  non-illuminating glowing block, if ever wanted, is a definition flag away;
  not in this version.
- **Meshing:** translucent blocks go into a **separate mesh**, built **per
  cell** like the opaque mesh: each translucent cell emits its own faces,
  wound outward, wherever its neighbor is not the same group (air, opaque,
  another tint, or outside the grid). So a run of one tint has no internal
  faces, an interface between two tints is **two coplanar faces** (one per
  side), and a translucent cell against an opaque one still emits its face.
  Opaque faces next to a translucent block are emitted as before.
- **Absorbance is a boundary integral** (`render/transparency`, "Volumetric
  absorbance from boundaries"): a translucent face fragment at view depth z
  adds **−σ·z when front-facing** (entering its cell's medium) and **+σ·z when
  back-facing** (leaving it), using `gl_FrontFacing`. Summed over a pixel this
  is exactly Σ σ × path length, at any angle and any thickness. z is measured
  from the eye and **clamped to the opaque depth** (z = min(z_frag,
  z_opaque)), so geometry behind a wall cancels and a camera inside the medium
  needs no special case.
- **Moment-based OIT** (Münstermann et al. 2018), after lighting, before
  present:
  1. **Absorbance pass:** `cullMode = NONE`, **no depth test** (the clamp does
     occlusion). Accumulates total absorbance and **4 moments** of warped depth
     (32-bit float targets), additive blend. Each fragment adds ±σ·Fₖ(z) to
     moment k, where Fₖ is the closed-form antiderivative of w(z)ᵏ for the
     logarithmic warp w, so the totals are the moments of the continuous
     absorbance density ∫σ·w(z)ᵏ dz (non-negative, as MBOIT's reconstruction
     requires). Measure depth relative to the translucent near bound to limit
     fp32 cancellation. **Verify this against the paper's moment definition
     before building;** if it doesn't hold, record why in `BUILD-NOTES.md` and
     stop for a decision rather than substituting a per-face rule.
  2. **Shading pass:** `cullMode = BACK`, depth-tested read-only against opaque
     depth, so of a coplanar interface pair only the camera-facing face is
     shaded. Each fragment is lit with the same BRDF and light loop as opaque
     (forward), reconstructs transmittance in front of it from the moments,
     and **adds** `emission + lit color` × that transmittance (additive
     blend). Emission is per front face (surface emission) in this version.
  3. **Composite:** `C = C_opaque · exp(−total absorbance) + accumulated`.
  - Depth warp: logarithmic between the translucent near/far range (fixed
    harness constants); keep the paper's bias parameter tunable in ImGui.
  - **No refraction** (accepted, `render/transparency`).
- **Order control for testing:** a CLI/ImGui setting that reorders the
  **faces in the translucent mesh's index buffer** before upload (the GPU
  blends in index-buffer order within a draw, so this permutes fragment
  arrival order at each pixel; one mesh means one draw, so permuting draws
  would test nothing): `forward` = faces sorted nearest-first from the
  capture camera, `reverse` = farthest-first (the order naive "over"
  blending needs, so the one most likely to hide a bug), `shuffle:<seed>` =
  seeded Fisher–Yates. Both translucent passes draw from the same buffer.
- **Capture:** `--capture <file.png> --frames N` writes the final image after N
  frames and exits; `tools/imgdiff.py` (stdlib) compares two PNGs and reports
  the max per-channel difference.
- **Overlays:** **F9 — translucency:** visualize total absorbance and the
  moment buffers.
- **Carry-overs from the v0.1.5 review (added 2026-10-30):**
  - **Start camera within reach:** start at (10, 10, 19) looking at
    (16, 16, 15), about 9 blocks from the surface, so blocks are targetable
    without flying (reach is 12). The capture camera is separate.
  - **`WorldQuery::apply(EditSet)` rejects out-of-bounds edits itself** and
    reports what it rejected; the placement code no longer calls `DenseGrid`
    for an "inside the grid" check. `DenseGrid` is then used directly only
    for setup and F10 bounds.
  - **No coordinates in the always-on HUD** (`input/no-player-coordinates`):
    the HUD keeps each hand's hotbar, what each button does, and the targeted
    block's name. Cell, face, canonical index and distance move to the F6
    targeting overlay (dev only).
- **Targeting:** translucent blocks are solid for the raycast: they can be
  targeted, broken, and placed against.
- **Test scene:** add to the grid overlapping stacks of differently tinted
  glass (2–4 layers), a run of 3 blocks of one tint beside a single block of
  it, glass resting on stone beside the same glass over air, a glow block
  behind glass and one in front, viewed from a fixed capture camera.

### Acceptance criteria
Headless (lavapipe under Xvfb via `tools/headless_check.sh` counts):
1. Build + `ctest` pass.
2. **Order invariance:** captures with `forward`, `reverse` and three shuffle
   seeds differ by **at most 1/255 per channel** (float-add reordering only).
3. Zero validation errors on lavapipe.
4. **Absorbance matches Beer–Lambert** (read back from the absorbance target,
   F9's data, or a debug readout), within a stated tolerance:
   - a run of 3 blocks of one tint vs 1 block: ratio of transmittances
     exp(−2σ) per channel;
   - glass resting on stone vs the same glass over air: equal absorbance
     through the block;
   - one view at a known oblique angle through a single block: σ × the
     analytic chord length.
5. A unit test of the boundary terms: front/back pairs for nested,
   adjacent-tint and wall-clamped cases sum to σ × path length.
6. Carry-overs: a unit test that `apply` rejects an out-of-bounds edit and
   leaves the grid and its generation unchanged; outside setup and F10, no
   code includes `grid_world.h` or names `DenseGrid`.

Display-required:
7. Stacked tinted glass darkens and tints plausibly (more layers = darker,
   colors multiply); the glow block reads bright through glass and brighter in
   front of it.
8. Zero validation errors on the reference GPU; record the translucency pass
   times from F1.
9. The start view has blocks within reach; the always-on HUD shows no
   coordinates (cell, face, distance appear only with F6).
   (Display checks in this version are subjective plausibility checks by
   the owner, which is sufficient for their purpose.)

### Out of scope
Reactive mask (no TAA yet), refraction, a water pass, the sorted fallback,
cutout (alpha-tested) blocks.

---

## v0.1.7 — Preview mode, edit sets, wrench, shovel, orientation ghost
- Status: DONE 2026-11-03 (specified 2026-10-29, revised 2026-11-02;
  built 2026-11-02; revised after the build 2026-11-03: the X quasimode).
  Criteria 1–6 pass, display criteria confirmed by the owner on the GPU. Ghosts are textured (owner's
  choice). Container placement revised 2026-11-03 (one block per click,
  temporal randomness; the N×N patch moves to v0.1.8); see `BUILD-NOTES.md`.
- **Purpose:** validate `input/preview-mode`'s rule that **preview and commit
  are the same pure edit-set function**, the wrench, and the orientation ghost.
  The container placement modes are its hardest case: placement that looks
  random must still preview exactly.

### Scope
- **Edit sets:** every tool exposes
  `EditSet compute(const ToolState&, const Target&, const WorldQuery&)`. The
  **preview draws** that edit set; a **click applies the same edit set**
  through `WorldQuery::apply`. No tool has a separate preview code path.
- **Preview mode:** **Q toggles** it; it shows the **main hand only**
  (`input/preview-mode`). Placing/breaking works the same with preview off.
  - Additions (cells to fill) render as **ghosts through the v0.1.6
    translucency path** (light tint + faint emission), **without glass
    frames**: a ghost is a tint, not a pane.
  - Removals render as a **red outline overlay** on the affected cells.
  - The HUD shows one text line for the main hand's pending action, e.g.
    `place log`, `break stone`, `shovel: fill 12 / cut 7`.
- **Tools** (added to the hardcoded off hotbar: pickaxe, **wrench**,
  **shovel**; the generic "leveler" was dropped 2026-10-31 in favour of the
  shovel, `tools/catalog`):
  - **Wrench:** primary = rotate the targeted block **90° clockwise about the
    axis through the clicked face** (as seen looking at it); **Shift+click** =
    counterclockwise (the first secondary action, `input/per-hand-use`). Result
    canonicalized; clicks inside the block's own symmetry are visible no-ops.
    Preview shows the rotated block as a ghost.
  - **Shovel** (digs and places soft materials): level height h = the z of
    the targeted cell. Over the **5×5 columns centered on the target**: **soft**
    cells (dirt, grass, sand, gravel) above h become empty (**cut**); rock
    above h is left alone (the shovel doesn't dig rock) and counted as
    blocked; empty cells at or below h, from h down to the first solid cell,
    are **filled** with the shovel's fill block. Fill block is set in an ImGui
    panel (default dirt; soft materials only); infinite in creative, standing
    in for drawing fill from a held container (`inventory/hand-pointers`).
    Edit set reports fill / cut / blocked counts.
  - **Held container** (`inventory/hand-pointers`): one hardcoded container
    item on the main hotbar holding three block types with counts (set in an
    ImGui panel; infinite in creative). Primary places **one block** in the
    cell against the clicked face (revised 2026-11-03, Michael: multi-cell
    placement is a separate capability, v0.1.8's patch, decoupled from the
    modes). Its block type comes from the container's **placement mode**
    (**M cycles it**, for now; the HUD shows the current mode):
    1. **in order:** successive placements take the container's slots in
       order;
    2. **random slot:** each placement picks a slot uniformly;
    3. **count-weighted:** each placement picks a slot weighted by its count
       (a palette);
    4. **match target:** the targeted block's type if the container holds
       it; otherwise the edit set is empty and nothing is placed.
    **The randomness is temporal, not spatial** (revised 2026-11-03,
    Michael): the container carries a **draw count**, which is the in-order
    cursor and, hashed with the cell and the container id, the RNG state (a
    counter-based RNG). It is tool state, so `compute` stays pure and the
    preview shows the next pick exactly; it advances **only when a placement
    changed the world** (an empty or rejected edit set uses no roll). The
    client predicts from the same replicated inputs; if client and server
    disagree, the server wins. Orientation follows the placement rule (or
    the X choice). Counts are not consumed in creative.
- **Orientation modifier (X):** while **X is held**, the placement ghost of the
  main hand's block is shown even with preview off, and the **wheel turns the
  ghost** (up CW, down CCW) about the normal of the clicked face (the placed
  block face it sits against) — the wrench vocabulary on a ghost
  (`world/block-orientation`); **Shift+wheel tips it** about screen-right made
  perpendicular to that normal and snapped (up = top away), so every
  orientation is reachable from any viewpoint. Nothing depends on the ghost's
  own faces (revised 2026-11-03). The mouse buttons still place.
  - **The choice lasts only while X is held** (revised 2026-11-03, after the
    build, replacing a persistent per-type latch cleared in ImGui): once the
    wheel is used, the orientation it lands on is locked, even the default,
    and every placement of that type uses it, in world frame, until X is
    released; then plain inference resumes. Nothing to clear.
- **Overlay:** **F8 — edit set:** lists the raw edit set (cells, before → after
  state) for the current target.
- **Tests:** for each tool and a set of targets, `apply(compute(...))` equals the
  committed world, and `compute` is pure (calling it twice on the same inputs
  gives identical edit sets and does not modify the world); shovel counts for
  stepped terrain (including rock it must leave); wrench CW then CCW is identity; four CW clicks are identity;
  for each container mode, `compute` is identical across repeated calls and
  camera moves for a fixed state; in one cell, in-order cycles the slots
  click by click, random-slot draws are uniform and count-weighted draws track
  the counts over successive draws; the draw count advances only on a commit
  that changed the world; match target ignores it and places nothing when the
  type is missing.

### Acceptance criteria
Headless:
1. Build + `ctest` pass.

Display-required:
2. With preview on, what the ghost/outline shows is exactly what happens on
   click, for every tool (including the shovel on uneven ground with rock in it).
3. Wrench rotations behave as specified, including Shift+click reverse and
   no-op clicks on a log's own axis; any orientation reachable in ≤ 3 clicks.
4. X-held ghost rotation (wheel) and placing with the chosen orientation while
   X is held work for logs and furnaces; releasing X returns to inference.
5. For every container placement mode, the preview shows exactly what the
   click places, including the random modes, after moving the camera and
   re-targeting; after each click the preview shows the next pick.
6. Ghosts composite correctly over and behind tinted glass (they share the
   v0.1.6 path); zero validation errors.

### Out of scope
Multi-step tool parameters (pick region, then apply), costs/inventory (real
container contents and consumption), setting down containers (G), rough tools
(sledge, axe: no exact preview by design), two-hand previews, undo.

---

## v0.1.8 — 16×16 block textures, sharp sampling, mipmaps, variant sets
- Status: DONE 2026-11-04. Criteria 1–11 pass; display criteria confirmed
  by the owner on the GPU (Intel HD 530: zero validation errors, G-buffer
  ~1 ms, 60 fps). History: SPECIFIED 2026-11-02, revised the same day (drop texture symmetry
  assertions, edge-matching `tileable`, index-bit edge labels, one validator,
  connected-glass frames as an edge-bit texture set); revised 2026-11-03
  (container patch, pick rename) and 2026-11-04 (darker tinted glass,
  camera-relative C/V in free flight, swap hands on F, hotbars in the
  lower corners); built 2026-11-04, then **revised after the owner's GPU
  check (2026-11-04)**: the edge-matched 16-tile wang sets for every natural
  block showed a block-period pattern and were more mechanism than needed
  (`render/block-textures`, "three principles"). Variant sets now have three
  selection rules (`random`, `matched` on declared edges, `connected`), and
  the engine checks structure only, not visual seam quality.
- **Purpose:** move the harness to the decided texture style
  (`render/block-textures`) before v0.2 adds chunks and streaming; and add
  the container's N×N patch placement (added 2026-11-03), decoupled from its
  placement modes.

### Scope
- **Real 16×16 sources:** every block face texture is 16×16. The engine's
  loader and `--check-blocks` reject any other size (`validate_blocks.py` is
  removed in this release; see "One block validator" below).
  Regenerate the shipped textures at 16×16 with `tools/gen_block_texture.py`
  (the furnace's chiral letters and the brick bond are redrawn for 16 px).
  They no longer need to be rotationally symmetric. PBR maps are 16×16 too.
- **Assertions removed** (`world/block-orientation`): **remove the
  `invariant` assertion** from the loader, `validate_blocks.py`, all block
  definitions, fixtures and tests. Rotational invariance comes from canonical
  orientations, not from texture properties. **`rot2` / `rot4` and the
  `--lint` "art more symmetric than H" warning are removed too** (same
  reasoning), along with the `asym_unasserted` fixtures, which only tested that
  unasserted asymmetry is accepted. **`tileable` is removed as well**
  (revised 2026-11-04, Michael): the engine doesn't validate visual seam
  characteristics; continuity at edges is the generator's job, by
  construction. With it go the seam metric and its tests; the definition
  format has no `asserts` any more.
- **Sampling:** **anti-aliased nearest** in the G-buffer shader (each texel a
  flat color; edges between texels blended over about one screen pixel using
  `fwidth`), plus a **full mip chain** (generated at load; 16 → 1) and
  **anisotropic filtering** at grazing angles.
- **One block validator** (2026-11-02, Michael): the engine's `--check-blocks`
  (no window) becomes the **only** definition validator. Delete
  `tools/validate_blocks.py` and its paired CTests (`validator_rejects_*`,
  `validator_accepts_*`, `validate_blocks`), so new checks are written once.
  **The validator checks structure only** (revised 2026-11-04): fields and
  ranges, symmetry and orbit keys, 16×16 textures, and that a set's atlas
  holds the right number of tiles for its rule and declared edges. Drop `orbit_tables_agree` (the C++ group tests
  cover the orbit tables). Python stays for texture generation
  (`gen_block_texture.py` and its byte-identical test).
- **Translucent blocks keep their texture** (decided 2026-11-02): the format
  still requires one texture per orbit for every block, so the loader has no
  translucent special case. From this version the texture is sampled
  (connected-glass frames, below).
- **Variant sets: one mechanism, three selection rules** (Michael: clean up
  and orthogonalize; revised 2026-11-04, `render/block-textures`). A texture
  may be a **set of variants**: 16×16 tiles in one atlas, 4 tiles per row, tile
  i at column i % 4, row i / 4, stored as consecutive layers of the one array.
  Selection is per fragment from world position, so it keeps working once
  v0.2 merges faces into larger quads. The definition declares the rule:
  - **`random`** (`{ "random": file, "tiles": n }`, any n ≥ 1): one tile per
    face by a hash of its cell and face. It breaks up repetition; no edge
    constraints.
  - **`matched`** (`{ "matched": file, "edges": "ns" | "nsew" }`): edge
    labels on the **declared edges only**, two labels each, every label a
    hash of the shared edge computed in world position, so both neighbours
    agree without communicating. The tile index is the labels as bits (`ns`:
    N = 1, S = 2, 4 tiles; `nsew`: N = 1, E = 2, S = 4, W = 8, 16 tiles), so
    no label table is needed. Labels are computed in world space, then the
    orientation's face turn applies. The four-edge case is Wang tiling; the
    hashed labels follow Wei (2004). For coherent small features that cross
    edges.
  - **`connected`** (glass, 16 tiles): bit set = that edge is **framed**, i.e.
    the neighbouring face in the plane is not the same type continuing the flat
    surface (v0.1.6's frame rule, `DenseGrid::frameBits`). This depends on
    neighbours, so the **mesher** computes it and writes the face's **real
    layer index** (base layer + bits) into the vertex. Variant 0 (no framed
    edges) is fully transparent; the second face of an interface pair, and
    faces against opaque cells, use variant 0, which replaces the separate
    "surface layer on" bit.
- **Connected-glass frames move from procedural to texture** (scheduled from
  `render/block-textures`): `gen_block_texture.py` generates the 16 frame
  variants of a neutral pale-grey band one texel wide (the v0.1.6 band was
  1/16 block, i.e. exactly one texel at 16 px), with **alpha = coverage**
  (0.85 on the band, 0 elsewhere). The absorbance pass samples alpha and adds
  the surface absorbance −ln(1 − α) at the face's depth, as v0.1.6 did; the
  shading pass samples color and alpha as the lit surface layer. The tint stays
  in the volume. **Translucent vertices then use `uv` and `layer` exactly as
  opaque vertices do:** remove the repurposed face-frame coordinates and bit
  fields, `glassFrameCoverage`, and the procedural band code in
  `shaders/oit.glsl`. `--no-glass-frames` stays (forces variant 0).
- **Variant content** (revised 2026-11-04), generated by
  `gen_block_texture.py`:
  - **dirt, grass, stone:** `random`, 8 tiles of independent per-texel
    noise (no feature larger than a texel, so any tile meets any other and
    nothing repeats per block).
  - **log sides (bark):** `matched` `ns`, 4 tiles: grain along the log's
    axis, continuous across the end-to-end edges (log on log), free across
    the side-by-side edges, which are real breaks. Log ends stay a single
    texture.
  - **gravel** (new block, soft): `matched` `nsew`, 16 tiles: pebbles of
    about 3–5 texels crossing edges in both directions, plain noise near the
    corners (a corner is shared by four tiles that each know only two of its
    edges). It exercises the four-edge mechanism.
  - **stone brick** (new block): `random`, 4 tiles: one dressed stone per
    face with a mortar border; the tiles differ only in subtle wear and tint,
    since the joint already shows the block. It exercises random selection
    on a patterned block.
  - Gravel and stone brick replace grass and brick on the main hotbar
    (slots 3 and 4); grass and brick stay in the world and in the
    container.
- **Overlay:** an F2 view or F-key toggle showing the variant index per face.
- **Test scene:** a large flat wall and floor of each natural block, viewed
  head-on and at grazing angles; a stone-brick wall; a gravel floor; three
  vertical log columns side by side (end-to-end edges matched, side-by-side
  edges breaks).
- **Darker tinted glass** (2026-11-04, Michael): one or two layers must read
  as clearly tinted; today it takes several. Retune the three glass blocks'
  `absorption` (σ per block, linear) from a target **transmittance per
  block**: about **0.85 in the colour the glass passes** and **0.10–0.15 in
  the colours it absorbs** (`render/transparency`, tint strength). Starting
  values, to tune by eye on the GPU:
  - blue `[2.2, 1.2, 0.15]`, green `[2.0, 0.15, 1.8]`, red `[0.15, 1.9, 2.2]`
    (one layer passes about 0.86 and stops 0.11–0.30; two layers stop
    about 0.01–0.09).
  Tests or captures that assumed the old σ values are updated; the
  absorbance math is unchanged, so `--check-absorbance` still passes.
- **C/V move along the camera's up in free flight** (2026-11-04, Michael;
  `input/vertical-movement`): ascend/descend in flight now move along the
  camera's up vector, perpendicular to the look direction (`right × forward`),
  like strafing, instead of world +z. WASD already moves along the full look
  direction, so flight becomes fully camera-relative. Only free flight changes;
  other media (ropes, swimming) interpret the intention themselves later.
- **Swap hands moves from R to F** (2026-11-04, Michael; `input/per-hand-use`):
  F swaps the two hands' hotbars, as Minecraft's swap-hands key does. F was
  unbound; R becomes unbound (reserved). HUD and F7 strings follow.
- **Hotbars in the lower corners** (2026-11-04, Michael; `input/hotbars`):
  the left hand's hotbar is drawn at the lower left and the right hand's at
  the lower right, each with its selection (or `bare`) and its planned
  action, so the screen matches hands and mouse buttons. A swap moves the
  contents between corners. Status (preview, container mode, "orientation
  chosen") moves to the bottom centre. Still ImGui, no hand graphics.
  - **Slot icons (placeholder, Michael, 2026-11-04):** a block slot shows its
    16×16 texture, one face (the front for blocks that have one), drawn with
    `ImGui::Image` at a whole-number scale (e.g. 3×) times the DPI scale, with
    a nearest sampler on a per-layer 2D view of the texture array (mip 0 only).
    Tinted glass is multiplied by its tint. Items with no texture show a single
    letter: **P** pick, **W** wrench, **S** shovel, and **C** for the container
    (assistant's choice).
  - **Label over the active slot:** the selected slot's name (or `bare`) is
    drawn centred above it, nudged sideways only as far as needed to stay on
    screen.
  - Confirm the texture path against the pinned ImGui (1.92 reworked the
    texture API; `ImGui_ImplVulkan_AddTexture` is the expected route).
- **Pick rename** (2026-11-02, Michael; `tools/catalog`): the pickaxe is now
  the **pick**, the one-block, one-handed breaking tool. Rename it in code,
  tests, HUD and F8 (`Item::Kind::Pickaxe` → `Pick`, `Item::pickaxe()` →
  `pick()`, the "pickaxe" label). No behavior change.
- **Container patch placement** (added 2026-11-03, Michael; deferred from
  v0.1.7, `inventory/hand-pointers`). A **patch size N** (1 = one block, the
  v0.1.7 behaviour; set in ImGui for now) is a capability of its own,
  **orthogonal to the placement modes**: every mode works at every N.
  - **Footprint:** the N×N patch in the layer against the clicked face,
    centred on the target, skipping occupied cells and cells outside the
    world. (Open: whether a patch can be stood up off the clicked face, e.g.
    a wall from a floor click, and how N is chosen in play.)
  - **Picks:** cells are visited in a fixed scan order (rows along the face's
    U, then R). In-order continues the container's cursor cell by cell;
    random picks hash (cell, container id, draw count), so cells within one
    patch differ. One commit advances the draw count by the number of cells
    placed.
  - **Consumption** (the rule for survival, specified now so N = 1 is its
    special case): every placed block **decrements its slot's count**, and
    count-weighted picks always weight by the **current** counts, so the
    weights change with every placement (Michael, 2026-11-03), within a patch
    as well as across clicks. That is drawing from a bag without
    replacement: emptying a 60/30/10 container places exactly 60/30/10. A
    type that reaches 0 simply drops out (in-order and random-slot skip
    it); when everything is out, the rest of the patch stays empty.
    Placement is **partial, not all-or-nothing**, and the preview marks the cells the container can't afford (`input/preview-mode`). The
    preview stays exact: counts and draw count are tool state and the scan
    order is fixed. In creative nothing is consumed, so this is exercised
    with a test-only "consume" flag.

### Acceptance criteria
Headless:
1. Build + `ctest` pass; the loader rejects a non-16×16 texture and a set
   whose atlas has the wrong tile count for its rule and edges (new
   fixtures); the mip chain is complete for every layer; no source, test or
   HUD string still says `pickaxe` (`BUILD-NOTES.md` history excepted).
2. Variant choice is deterministic: the same cell gets the same variant across
   rebuilds and runs (unit tests with pinned values and a capture
   comparison); `random` picks a tile in range from cell and face; for
   `matched`, two faces sharing a declared edge compute the same label for it
   and the tile index uses only the declared edges; the CPU path (ghosts)
   picks what the shader picks.
3. `--check-blocks` checks structure only. No definition, fixture, test or
   tool still mentions `invariant`, `rot2`, `rot4`, `tileable` or the
   symmetry lint, there is no seam metric, and `validate_blocks.py` is gone.
4. A merged-quad test (a hand-built quad spanning several cells) shows
   per-cell variants, proving selection is per fragment.
5. Container patch: for every mode and N, `compute` is pure and preview
   equals commit; N = 1 matches v0.1.7 exactly; with consumption on, a patch
   larger than the container's contents places exactly the affordable cells,
   weights each count-weighted pick by the counts remaining at that cell, and
   marks the rest.
6. Connected-glass frames: unit test that the mesher's edge bits pick the
   right layer for runs, creases, interface pairs and faces against opaque
   cells; translucent and opaque vertices share one layout with no repurposed
   fields; order invariance stays ≤ 1/255 with frames on; `--check-absorbance`
   still passes; a capture of the v0.1.6 glass scene matches the procedural
   frames closely.

Display-required:
7. Up close, texels are crisp with no crawling edges as the camera moves; at a
   distance, no shimmer or moiré.
8. Large dirt, grass and stone walls and floors show no visible repeat; bark
   is seamless end to end up a log column, with visible breaks between
   side-by-side logs; gravel shows no seams; stone brick shows subtle
   variation between blocks.
9. Zero validation errors; record G-buffer pass time from F1 before and after.
10. One layer of each tinted glass reads as clearly coloured and two layers as
    strongly so, against both the sky and a lit wall.
11. In free flight, C/V move along the camera's up (looking down, C moves
    forward-and-up relative to the view, not straight up in the world); F
    swaps hotbars and R does nothing; each hand's hotbar is in its own lower
    corner, and swapping moves the contents across; block slots show crisp
    16×16 icons, tools their letter, and the active slot's label stays on
    screen at both edges.

### Out of scope
Macro tiles, vein blocks and other oriented-pattern blocks, world-space tint,
ashlar half-stones (`render/block-textures`).

---

## v0.1.9 — Code review and refinement (closes v0.1)
- Status: DONE 2026-11-04. Criteria 0–7 pass; the owner read
  `REVIEW-v0.1.md` and accepted every disposition (D2 adopted), confirmed
  no visible change on the GPU (after a fix for diagonal lines on glass and
  ghosts, review L4), and the before/after GPU times are recorded in
  `BUILD-NOTES.md`. v0.1 is closed. History: SPECIFIED 2026-11-04 (Michael).
  Builds on v0.1.8.
- **Purpose:** close out v0.1 with **no new features**: review the whole
  codebase for quality and maintainability, revisit debatable implementation
  choices made across v0.1–v0.1.8, and take obvious or overlooked performance
  wins, so v0.2 (chunks, streaming, threads) starts from a clean base.

### Scope
- **First, before the review: a faster headless suite** (added 2026-11-04,
  Michael). Today each screenshot shot in `tools/headless_check.sh` runs the
  engine for a fixed 600–900 frames and grabs the X screen (`xwd`) after a
  fixed sleep; about 90% of the ~29,000 frames (20–25 minutes under lavapipe
  with validation) only keep the window alive until the grab. Switch every
  shot to the engine's own `--capture` at a small frame count (about 60, or
  just past a shot's last scripted event; the resize test about 120), so it
  writes the same final image, UI included, and exits. Validation still
  covers every frame that runs. Then check that each shot's image still
  shows what it should, especially the scripted-input ones, and record the
  before/after suite time in `BUILD-NOTES.md`. Parallel runs and other
  speed-ups wait until this result is known.
- **Deliverable: a review report,** `REVIEW-v0.1.md` at the repo root. Each
  finding has an id, the files involved, what's wrong or debatable, a
  recommendation, and a disposition:
  - **fix now:** small, behavior-preserving, done in this release;
  - **owner decides:** changes behavior, the design, or a decided choice
    (these are proposed, not made: decisions already made are not the build
    agent's to relitigate, see "For the build agent");
  - **defer:** belongs with v0.2 or later (say where);
  - **keep:** reviewed and fine, with the reason.
- **Code quality and maintainability:**
  - Structure and size: `src/main.cpp` (~1,200 lines) and
    `src/renderer.cpp` (~950) in particular; module boundaries, ownership and
    lifetimes, dead code, duplicated logic, naming, comments that have gone
    stale.
  - Tests: coverage gaps, slow or brittle tests, anything that tests the
    implementation rather than behavior.
  - The harness: the growth of CLI flags (`--key-at`, `--hold-key`,
    `--wheel-at`, …) and `tools/headless_check.sh`.
  - Build: warnings policy, dependency pins (`BUILD-NOTES.md`), compile times.
- **Debatable implementation choices to revisit** (starting list, from
  `BUILD-NOTES.md`; add any others found):
  - per-pixel brute-force loop over all point lights in the lighting pass;
  - five G-buffer color attachments (needs `maxColorAttachments ≥ 5`);
  - host-visible SSBOs for the material and rotation tables, and per-frame
    host buffers for debug lines and ghosts (ghost capacity 1,024 cells);
  - manual depth test in the debug-line pass;
  - ImGui's own font-upload submit (the one exception to "barriers only from
    the seam");
  - F1's unsmoothed per-pass GPU times (a known follow-up);
  - the compiled-in absolute asset path and embedded SPIR-V;
  - anything in the tools / intention / binding layers that grew by
    accretion over v0.1.5–v0.1.8;
  - **headless tests that lavapipe passes but a real GPU fails** (v0.1.8's
    ImGui descriptor-pool abort on Intel: lavapipe doesn't enforce pool
    sizes). Look for other limits lavapipe doesn't enforce (descriptor pools,
    push-constant size, attachment counts, image formats) and check them
    explicitly where cheap;
  - the 2–4 ms G-buffer time seen in v0.1.8's first build, not broken down
    before the revision brought it to ~1 ms (`--anisotropy`, `--no-variants`
    are the A/B switches).
- **Performance:** measure before changing anything. Record F1 per-pass times
  on the GPU machine at 1080p (Release) before and after, in `BUILD-NOTES.md`.
  Look for obvious wins: redundant per-frame work (rebuilds, uploads,
  allocations, descriptor updates), CPU stalls (waits, fence placement),
  unnecessary barriers, and the lighting pass, which dominated in v0.1.4
  (about 3–5 ms of 8–9). A win counts only if it is measured.
- **Named constants** (Michael, 2026-11-04): constants significant to the
  simulation **and to rendering** are **named, defined once (not inlined),
  and explained**, and each holds its value for a reason the comment states.
  This is not every literal in the code: it means tuning values, ones that
  shape how the world behaves or looks. Examples from v0.1–v0.1.8:
  - simulation: reach distance, the shovel's 5×5 footprint and ±8 scan, the
    soft-material list, flight and sprint speeds, the container defaults,
    block-texture size, variant-set tile counts;
  - rendering: exposure and the AgX range, sun and ambient levels, light
    radii and intensities, glass σ, ghost inflation and emission, the
    debug-line bias, near and far planes, the heatmap ramp.
  - **The comment says why the value is what it is:** a physical reference,
    a design decision (with its `group/slug`), a measurement, "placeholder,
    tune in play", or **"chosen by eye" / "arbitrary"** when that's the
    truth. An honest "arbitrary" is still useful later: it says the value can
    move freely.
  - **Exceptions** are allowed where naming would make code messier than it
    clarifies (a one-off local, an index arithmetic constant). The review
    lists the notable ones it left inline.
  - Values are unchanged by this item; a value that looks wrong is an "owner
    decides" finding.
- **Fixes are behavior-preserving:** headless captures stay identical within
  the existing tolerances, order invariance stays ≤ 1/255, and
  `--check-absorbance` and `--check-blocks` still pass. Anything that would
  change what the player sees or does is an "owner decides" finding instead.
- **Already deferred to v0.2** (recorded 2026-11-02, Michael): splitting
  `src/main.cpp`, and replacing the harness flags with scenario files
  (v0.2.0). The
  review should assess both and may recommend pulling either into v0.1.9;
  doing so is the owner's call.

### Acceptance criteria
Headless:
0. The headless suite uses `--capture` throughout (no `xwd`, no fixed
   sleeps), passes as before, and its run time before and after is recorded.
1. Build + `ctest` pass, `-Werror`, zero warnings, Debug and Release.
2. `REVIEW-v0.1.md` exists and every finding has a disposition; every "fix
   now" finding is done and says where.
3. Headless suite: zero validation errors; captures unchanged within
   tolerance; order invariance ≤ 1/255; absorbance and block checks pass.
4. Every simulation- or rendering-significant constant is named, defined
   once and has a comment giving its reason ("arbitrary" included); the
   review lists the exceptions left inline.

Display-required:
5. Owner reads the review and rules on the "owner decides" findings.
6. Before/after F1 per-pass GPU times recorded on the GPU machine; no pass
   is slower.
7. No visible behavior change (owner spot-check of the v0.1.7 and v0.1.8
   scenes).

### Out of scope
New features; v0.2 work (chunks, streaming, threads, soft simulation);
changes to decided design. Larger refactors are proposed in the review, not
done, unless the owner pulls them in.
