# Build notes

Choices the build agent made where `versions.md` left something open, plus
build/verification facts. Per `versions.md`, these are the simplest
reversible options. Revisit freely.

## v0.1 (closed 2026-11-04)
The build notes for v0.1 through v0.1.9 are history in
`attic/build-notes-v0.1.md`.

## v0.2.0 (built 2026-11-05)

### Build, test & run
As before; `ctest` runs 8 tests (Debug), 7 (Release). A run is now a
scenario (`scenarios/README.md`):
```
build/sandbox                                                   # scenarios/v0.1-scene.json, interactive
build/sandbox --scenario scenarios/headless/lit.json --capture lit.png
build-release/sandbox --scenario scenarios/perf-1080p.json --gpu-report   # the v0.1.9 baseline's setup
```
Remaining flags: `--scenario`, `--capture`, `--gpu-report`,
`--check-absorbance`, `--blocks`, `--check-blocks`. The 27 others are
rejected, naming the field that replaced them.

### Choices
- **Schema** (`scenarios/README.md`): `include`, `world`, `scene`, `camera`,
  `window`, `view`, `render`, `tools`, `events`, `frames`, `checks`, all
  optional. Unknown fields are errors at every level, with the field's path;
  numbers are typed by the JSON parser (Q8's unchecked parsing is gone).
  The F2 view and overlays are named (`"variant"`, `"editSet"`), not
  numbered.
- **`include` semantics:** the included file's fields are the defaults;
  `scene` appends (a shot can add blocks), `camera`, `window`, `view`,
  `render`, `tools` override field by field, `events`, `frames`, `checks`,
  `world` replace. Each headless shot includes `../v0.1-scene.json`
  (by reference, so the scene lives in one file).
- **The v0.1 scene is data:** `scenarios/v0.1-scene.json`, 58 operations
  (fills and placements; rotation indices computed from the old
  `rotationAbout` calls). Before deleting `fillTestWorld`, a one-off check
  compared the two worlds cell by cell: 0 differences.
- **Run length** is the engine's rule (`Scenario::runFrames`): `frames` if
  given; with `--capture`, 20 frames past the last press (60 past a resize),
  at least 60; otherwise interactive.
- **The absorbance cases** live in the v0.1 scene's `checks`; the two
  pairwise criteria use the first four cases (documented), and the 3-vs-1
  ratio takes σ from case 0's glass instead of a hard-coded type.
- **`main.cpp` split** (Q2): `main.cpp` (flags, block definitions, the
  scenario, validation; 175 lines), `app.cpp` (the loop), `app_input.cpp`
  (window management and GLFW callbacks), `dev_overlays.cpp` (HUD and
  F1–F10), `game.cpp` (the game glue and loadout), `harness_checks.cpp`
  (absorbance check, merged-quad region), `frame_stats.h` (F1 timing).
  `src/scene.cpp` keeps only `lightsFromEmitters`.
- **`renderer.cpp` split** at its natural seam: `renderer_setup.cpp`
  (shaders, layouts, pools, pipelines, materials, targets) and
  `renderer.cpp` (mesh upload, timestamps, per-frame recording), sharing
  `renderer_internal.h` (push blocks, GPU structs, tuning constants).
  Largest source file now 481 lines (`dev_overlays.cpp`).
- **`grid_confined_to_setup`** follows the split: `DenseGrid` may appear in
  `app.cpp` (construction) and `dev_overlays.*` (F10's bounds) only.
- **`check_validation_missing.py`** runs `scenarios/one-frame.json`, so it
  can't hang if the binary ever starts without the layer.
- No test-only API or representation was added.

### Results (lavapipe/Xvfb, Debug, sync validation on)
- `ctest` 8/8 (Debug) and 7/7 (Release); zero warnings in either.
- New unit tests: every scenario section, event expansion and ordering, the
  run-length rule, ten malformed scenarios rejected naming the field, key
  names, include overrides, the v0.1 scene's content, a missing block type,
  every shipped scenario loads.
- Headless suite from scenarios: 64 runs pass in 188 s, zero validation
  errors; `--check-absorbance` passes from the scenario's checks. Against
  the final v0.1.9 captures: 49 of 54 pixel-identical; the other 5 differ
  only inside the F1 window (live timings) and F7's "seconds ago" column.

### Acceptance status (v0.2.0)
| # | Criterion | Status |
|---|---|---|
| 1 | Debug and Release, `-Werror`, zero warnings, ctest | ✅ |
| 2 | Every v0.1.9 capture from a scenario, identical but F1/F7 text; zero validation errors; absorbance from `checks` | ✅ |
| 3 | Removed flags rejected naming the field; unknown field, key or button an error | ✅ |
| 4 | No source file over ~1,000 lines; `main()` only parses, loads and runs | ✅ (max 481) |
| 5 | `--scenario scenarios/v0.1-scene.json` opens the v0.1 world, play unchanged | ✅ owner ("no changes detected") |
- **Shift+click falls back to the primary action** (owner, 2026-11-05; a
  bug since v0.1.7, not from v0.2.0): with X held, Shift+wheel tips the ghost,
  but a click with Shift still held went to the block stack's (nonexistent)
  secondary action and did nothing. Now only the wrench has a secondary
  action (`hasSecondaryAction`); every other item treats Shift+click as its
  primary, in `planEdit` and `afterCommit`. A Shift+click still fires once
  (no hold-to-repeat), as secondary uses did. Recorded in
  `input/per-hand-use`. Tests: `testSecondaryFallsBack`; headless
  `xghost_shift_place` (X held, Shift+wheel tip, Shift+right-click places
  the tipped furnace).
