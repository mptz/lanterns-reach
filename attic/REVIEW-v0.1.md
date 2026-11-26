# Review of v0.1 (v0.1.9)

A review of the whole codebase at the end of v0.1 (`versions.md` v0.1.9):
quality and maintainability, debatable implementation choices from v0.1–v0.1.8,
performance, and named constants. Each finding has an id, the files, what is
wrong or debatable, a recommendation, and a disposition:

- **fix now:** small, behavior-preserving, done in this release (says where);
- **owner decides:** changes behavior, the design, or a decided choice;
  proposed, not made;
- **defer:** belongs with v0.2 or later (says where);
- **keep:** reviewed and fine, with the reason.

Performance baseline (owner's GPU, Intel HD 530, Release, 1080p, start view,
before any change): G-buffer 3.28 ms, lighting 3.41, absorbance 0.39,
translucent 0.38, present 1.43, UI 0.05; total 8.94 ms (`BUILD-NOTES.md`).

## Code quality and maintainability

| Id | Files | Finding | Recommendation | Disposition |
|---|---|---|---|---|
| Q1 | `src/main.cpp` | The file header still describes v0.1.6. | Describe the harness as it is now. | fix now |
| Q2 | `src/main.cpp` (~1,330 lines) | One file holds option parsing (~35 flags), GLFW callbacks and capture, the `Game` glue, eight dev-UI panels, the absorbance check and the loop. It is readable but every version adds to it. | Split into `options`, `dev_overlays`, `harness_checks` and the loop. Do it with v0.2.0's scenario files, which replace most of the flags; splitting the flag parser first would be wasted. | defer (v0.2.0, already recorded); owner agrees |
| Q3 | `src/bindings.cpp` | Comments drifted: the wheel is said to turn the ghost about "the ghost face looked at" (it's the clicked support face since v0.1.7), the unbound list omits R, "v0.1.5 subset". | Correct them. | fix now |
| Q4 | `src/main.cpp` (F8 panel) | "(creative: nothing is consumed)" is stale since `consume`; the placement-mode names are repeated as a local array instead of `placementModeName`. | Use the one name function; fix the comment. | fix now |
| Q5 | `src/main.cpp` | `outlineCell` and `boundsOverlay` both spell out a box's 12 edges. | One `boxEdges(lo, size, colour, lines)` helper. | fix now |
| Q6 | `src/main.cpp` | The main hand's planned edit is computed twice per frame (preview and HUD); `Game::pick` repeats `aim`'s raycast. | Compute it once and pass it to the HUD. | fix now |
| Q7 | `src/main.cpp`, `src/scene.cpp` | The absorbance-check cases hard-code scene coordinates that live in `scene.cpp`. | Move them with the scene into scenario files. | defer (v0.2.0); owner agrees |
| Q8 | `src/main.cpp` (`parseArgs`) | Silent fallbacks: an unknown `--click-at` button becomes middle; an out-of-range `--view` is ignored; numbers are parsed without checks. | Make the two silent cases errors now; the rest goes with scenario files. | fix now (the two cases); defer (v0.2.0) the rest; owner agrees |
| Q9 | `src/main.cpp` | A `--merged-quad` failure returns from inside the loop, skipping the window teardown and the validation summary. | Set the exit code and leave the loop. | fix now |
| Q10 | `src/renderer.cpp` | Descriptor-pool sizes are hand-counted (21 samplers, storage buffers, sets) beside the layouts they must match. Lavapipe doesn't enforce pool sizes (v0.1.8's ImGui abort), so a drift would pass headless. | Derive the pool sizes from the layouts' binding lists, one source. | fix now |
| Q11 | `src/main.cpp` | `itemName` and `toolLetter` switch over `Item::Kind` separately. | Fine at four tools; revisit when tools come from data. | keep |
| Q12 | `src/bindings.cpp` | `Move` uses args 0–3 as directions. | Fine and local; the table is the documentation. | keep |

## Tests and harness

| Id | Files | Finding | Recommendation | Disposition |
|---|---|---|---|---|
| T1 | `tools/headless_check.sh` | Shots ran 600–900 frames and grabbed the screen after fixed sleeps (~36 min). | `--capture` at about 60 frames; 177 s (step 0). | fixed (step 0) |
| T2 | `tests/sandbox_tests.cpp` (~1,570 lines) | One file, in-house `CHECK`; well organised by section. | Split by module when v0.2's chunk tests arrive. | defer (v0.2); owner agrees |
| T3 | tests | The renderer is covered only by captures compared between runs (determinism, order invariance, merged quad) and the absorbance readback, not by golden images. | That is deliberate: goldens would break on every texture or lighting tweak, and the owner's eye is the visual check. | keep |
| T4 | headless suite | Passing on lavapipe isn't passing on a GPU (v0.1.8 ImGui pool). | See L1–L3. | — |

## Lavapipe-unenforced limits

| Id | Files | Finding | Recommendation | Disposition |
|---|---|---|---|---|
| L1 | `src/renderer.cpp` | Push-constant structs: `LightingPush` and `TranslucentPush` assert ≤ 128 bytes; `GBufferPush`, `PresentPush` and `LinesPush` don't. | Assert all of them. | fix now |
| L2 | `src/texture.cpp` | The texture array's layer count isn't checked against `maxImageArrayLayers` (guaranteed ≥ 256; 76 used). | Check it at creation, with a clear error. | fix now |
| L4 | `shaders/translucent.frag` | Found by the owner on the GPU: a corner-to-corner diagonal on glass frames and ghost textures, coming and going with the view. The shader discarded fragments behind opaque geometry *before* sampling the face texture, whose anti-aliased nearest filtering takes derivatives (`fwidth`, `textureGrad`); derivatives after a discard are undefined. Intel produced bad ones in quads straddling a face's two triangles; lavapipe didn't. | Sample before the discard (derivatives in uniform control flow). Checked the other shaders: `gbuffer.frag` has no discard; `lines.frag` discards but takes no derivatives; `absorbance.frag` samples unconditionally. | fixed (owner confirmed the lines are gone) |
| L3 | `src/renderer.cpp` | Already checked: `maxColorAttachments` ≥ 5, every target format's features, anisotropy support, timestamp support. Descriptor pools: Q10. | — | keep |

## Debatable implementation choices

| Id | Files | Finding | Recommendation | Disposition |
|---|---|---|---|---|
| D1 | `shaders/lighting.frag` | Brute-force loop over all point lights per pixel. With 4 lights and a 16-light cap the lighting pass (3.4 ms at 1080p) is bandwidth-bound — it reads four targets and blends onto HDR — not loop-bound. | Keep for v0.1; tiled or clustered lighting when light counts grow. | defer (v0.2+, with more lights) |
| D2 | `src/gbuffer.*`, `shaders/gbuffer.frag`, `lighting.frag` | Five colour attachments: the G-buffer pass writes emission into HDR (8 B/px for every pixel, mostly zeros) and lighting blends onto it, reading it back. Computing emission in the lighting pass from GB3's material id × albedo would drop an attachment and about 16 B/px of traffic (≈ 33 MB/frame at 1080p) with the same result. This changes a decided layout (`render/gbuffer-layout`: "emission written here in the G-buffer pass"). | Try it and measure on the GPU; adopt only if it wins. | owner decides: **adopted** (2026-11-04): cost-neutral on the HD 530 (G-buffer about −9% and lighting about +2% relative to `present`, total unchanged within run-to-run variation), kept for the attachment headroom (TAA's velocity target); `render/gbuffer-layout` updated with when it reopens (threshold, mask, RGB). Done in `shaders/lighting.frag`, `gbuffer.frag`, `src/renderer.cpp`; captures differ by ≤ 2/255 on emissive pixels only (8-bit albedo) |
| D3 | `src/renderer.cpp`, `src/gpu_buffer.*` | Host-visible memory for the material table, the world mesh and the per-frame line, light and ghost buffers. Fine on the iGPU (unified memory); on a discrete GPU the mesh would be read over PCIe every frame. | Device-local meshes with a staging upload when v0.2 streams chunk meshes. | defer (v0.2) |
| D4 | `shaders/lines.frag` | Manual depth test (sampled depth, discard) instead of a read-only depth attachment. | Lines are few and drawn onto the swapchain; no depth attachment there keeps the pass simple. | keep |
| D5 | `src/dev_ui.cpp` | ImGui uploads its font with its own submit, the one exception to "barriers only from the seam". | Once at startup, inside the backend. | keep |
| D6 | `src/main.cpp` (F1) | F1's per-pass GPU times were unsmoothed and unreadable. | Half-second means, and `--gpu-report` for run means (added before the baseline). | fixed |
| D7 | `CMakeLists.txt` | The asset directory is compiled in as an absolute path; SPIR-V is embedded. | The absolute path makes the binary unrelocatable — fix when there is a package or install step. Embedded SPIR-V: keep (no runtime shader files to find or mismatch). | defer (packaging); keep (SPIR-V) |
| D8 | tools, intentions, bindings | Accretion over v0.1.5–v0.1.8: stale comments (Q3, Q4), a duplicate plan (Q6). The structure itself (one `planEdit` per tool, one binding table, mapping contexts) has held up. | The fixes above. | fix now (Q3, Q4, Q6) |
| D9 | v0.1.8 | The 2–4 ms G-buffer time of v0.1.8's first build was never broken down; the revision measured ~1 ms at 720p. At 1080p it's 3.28 ms. | Measure anisotropy's share with `--anisotropy 1 / 4 / 8` (P2). | see P2 |
| D10 | `src/render_graph.cpp` | Every pass is bracketed by `ALL_COMMANDS` timestamps, always on, which serializes the passes (idle gaps aren't counted in the pass times). | Time passes only while F1 is shown or `--gpu-report` is set. Not measurable while vsync caps the frame; low priority. | defer (v0.2, when the frame budget matters) |

## Performance

| Id | Finding | Recommendation | Disposition |
|---|---|---|---|
| P1 | CPU per frame: small vectors rebuilt (lines, lights, ghosts), one `planEdit` per hand plus preview (Q6), three raycasts. All microseconds at this scale. | Q6 only. | fix now (Q6) |
| P2 | G-buffer 3.28 ms at 1080p on the HD 530: fragment work (16× anisotropic `textureGrad`, the per-pixel variant hash) and target bandwidth (D2). | Measured: run-to-run variation is ±8% (every pass moved together between runs), larger than the effects, so passes were compared relative to `present`, which none of the changes touch. 16× vs 4× anisotropy: ~2% of the G-buffer pass, within noise; the hash wasn't separated. | keep: 16× anisotropy (owner), per-pixel selection (it keeps v0.2's greedy merging possible) |
| P4 | Raw pass times can't be compared across runs at this precision (P2). | An in-run A/B mode for `--gpu-report`: alternate two configurations every N frames in one run, so clocks and temperature affect both alike, and report both means. | defer (v0.2, when performance work starts) |
| P5 | The variant hash is recomputed for every pixel of a face though its result is constant per cell. | If it ever measures: the mesher writes each chunk's per-cell tile choices into a small lookup texture (one byte per face cell, same `variants.glsl` code), and the shader fetches one texel instead of hashing. Greedy merging is unaffected (the lookup is per cell, not per vertex). Simpler still for pure-noise blocks (owner, 2026-11-04): one large world-anchored texture (e.g. 128×128 texels = 8×8 blocks at 16 texels per block) sampled by world position — no per-pixel choice, merge-safe, nothing shifts on remesh; it repeats every 8 blocks (a 4-block period risks a visible lattice), and it would amend `render/block-textures`' "macro tiles: reserve only" for featureless noise. Not a per-quad variant, which would re-roll a patch whenever an edit remerges it. | defer (v0.2+, only if measurable) |
| P3 | Absorbance + translucent 0.77 ms: three full-screen RGBA32F targets cleared every frame even with no glass in view. | fp32 is needed (order invariance). Skipping the passes when nothing translucent is drawn would need present to skip its reads; small win only in glass-free views. | keep |

## Named constants (criterion 4)

Every simulation- or rendering-significant value is named, defined once, and
explained. Values are unchanged.

| Where | Constants | Change |
|---|---|---|
| `src/camera.h`, `camera.cpp` | near/far planes, vertical FOV, fly speed, fast factor, pitch limit | reasons added ("placeholder, tune in play", "chosen by eye", the degeneracy the pitch limit avoids) |
| `src/main.cpp` | mouse sensitivity, reach, shovel fill default, container defaults (60/30/10), stats window, preview outline inflation | container and shovel defaults and the outline inflation were inline: now named; reasons added |
| `src/hands.h` | use repeat delay and period | reason added |
| `src/scene.h`, `scene.cpp` | grid size, lamp-light intensity and radius | reasons added |
| `src/tools.cpp` | shovel radius and reach, soft materials | the soft-material list was an inline `||` chain: now `kSoftMaterials`; reasons cite `tools/catalog` |
| `src/renderer.h`, `renderer.cpp` | sun and ambient defaults, light capacity, frames in flight, timing/line/ghost capacities, background, depth-view range, line bias, wire colour, sphere segments, ghost σ/emission/inflation, π | reasons added; the two inline π literals now use `kPi` |
| `src/blocks.h` | texture size, atlas columns, random-tile cap | the cap's reason (5 bits in `texInfo`) added |
| `shaders/heat.glsl` (new) | the F5 heatmap ramp | was written twice (present shader, F5 legend): now one GLSL/C++ file |
| `shaders/translucent.frag` | frame roughness | reason added |

Already named and explained before this pass: the light direction, OIT
moment bias and overestimation, OIT depth range, anisotropy cap, icon scale,
GPU timing window and warm-up, the dev-UI texture cap, variant-set kinds.

**Exceptions left inline** (naming would add noise, not clarity):
- **Published or standard formulas:** the AgX inset/outset matrices and EV
  range (`present.frag`), the sRGB transfer functions (`present.frag`,
  `blocks.cpp` mips), the GGX/Smith/Fresnel terms and F0 = 0.16·r²
  (`pbr.glsl`), splitmix64 and lowbias32 hash constants (`tools.cpp`,
  `variants.glsl`).
- **Dev-UI layout:** paddings, margins, crosshair arms, panel widths, slider
  ranges in `main.cpp`: presentation of developer tools, not of the world.
- **Geometry and index arithmetic:** 0.5 cell centres, ±1 axis steps, face
  frame tables, quad index patterns.
- **Data files:** glass σ, material roughness and emission live in the block
  JSON, which can't carry comments; their reasons are in `versions.md`
  (v0.1.6, v0.1.8) and `BUILD-NOTES.md`.
- **Texture recipes:** the colours and noise mixes in
  `tools/gen_block_texture.py` are content, tuned by eye on the GPU.

## Status
- Fix-now findings done: Q1, Q3, Q4, Q5, Q6, Q8 (the two silent cases), Q9,
  Q10 (`renderer.cpp`), L1 (`renderer.cpp`), L2 (`texture.cpp`), D6, T1, and
  the constants pass above.
- Checks after the fixes: Debug and Release build with `-Werror` and zero
  warnings; `ctest` 8/8 (Debug) and 7/7 (Release; the missing-validation test
  is Debug-only); headless suite 51/51 in 169 s, zero validation errors;
  every capture equal to the step-0 reference within 1/255 except text in
  the F1 window (the version string and live timings) and F7's
  "seconds ago" column.
- D2 adopted by the owner (emission computed in the lighting pass).
- P2 measured and settled (16× kept); after-times recorded (criterion 6:
  no pass slower beyond the ±8% run-to-run variation).
- Owner (2026-11-04): read the whole review, no changes to any disposition
  (D10 discussed and agreed: defer); spot-check passed after L4's fix.
