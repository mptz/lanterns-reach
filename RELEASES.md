# Release notes

Per point release: the choices made where `versions.md` left something open
(the simplest reversible option each time, so revisit freely), the results of
verification, and measurements. How to build and run is `BUILD.md`; what a
release is meant to do is `versions.md`. Notes for finished major versions
move to `attic/` once they stop being useful.

## v0.1 (closed 2026-11-04)
The build notes for v0.1 through v0.1.9 are history in
`attic/build-notes-v0.1.md`.

## v0.2.0 (built 2026-11-05)

### Flags
A run is a scenario (`scenarios/README.md`). Remaining flags: `--scenario`,
`--capture`, `--gpu-report`, `--check-absorbance`, `--blocks`,
`--check-blocks`. The 27 others are rejected, naming the field that replaced
them. `ctest` ran 8 tests (Debug), 7 (Release).

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

## v0.2.1 (closed 2026-11-26)

### Toolchain switch (`deps/toolchain`)
Done in two steps so a capture change has one cause. Captures are from
`tools/headless_check.sh` (lavapipe/Xvfb, Debug), 65 runs each, all passing.
- **Baseline** (GCC 14.2, before any change) vs **step 1** (GCC,
  `-ffp-contract=off`): 60 of 65 captures pixel-identical. The other five
  (`hotbar`, `lighting`, `lit`, `overlays`, `resize`) differ only in the F1
  timing window (x 19–311, y 37–242) and, for `hotbar`, the F7 "seconds ago"
  column: live text, as in v0.2.0's exceptions.
- **Step 1 vs step 2** (Clang 19.1.7, libstdc++, `-Werror`, `-Wthread-safety`):
  the same 60 pixel-identical and the same five differ, max 207–218/255 only
  inside those live-text regions. No other pixel changed.
- **Clang build fixes:** Clang's `-Wconversion` includes `-Wsign-conversion`
  (GCC's doesn't). It flagged eight `int` array indices in
  `src/orientation.cpp`, now `std::size_t`. Nothing else.
- **Compiler choice (owner, 2026-11):** Clang is the only supported
  compiler; GCC support is removed. `CMakeLists.txt` defaults to
  `clang`/`clang++` and fails configuration for any other compiler. The compilers are the OS
  packages (Debian `clang`, 19.1.7), not pinned. `-Wthread-safety` is on for
  Clang; nothing is annotated yet.
- **Reference:** the owner inspected the Clang captures (`build-debug/cap`) (and played
  interactively) and accepted it. Those Clang captures are the reference for
  the chunk work. Existing GCC build trees need a fresh directory.

### One chunk
- **New files:** `brick.h` (the `Brick<Entry>` template: 16-bit indices into
  a palette, capacity 2¹⁶ asserted, layout `x + 64·y + 64²·z` behind
  `brickCellIndex`; `kChunkSize` = 64 lives here), `chunk.h/.cpp`
  (`floorDiv`/`floorMod`, `ChunkCoord`, `ChunkId` = dimension + coord,
  `chunkOf`/`localOf`, and `ChunkData`, which owns its Brick through a
  `unique_ptr` and does the linear-scan reverse lookup), `chunk_world.h/.cpp`
  (`ChunkWorld`, the `WorldQuery` implementation, from `grid_world.*` by
  `git mv`). `DenseGrid` is gone; `tests/check_no_dense_grid.py` (CTest
  `no_dense_grid`) replaces `grid_confined_to_setup`.
- **Seam change:** `buildMesh()` became `dirtyChunks()` plus
  `buildMesh(const ChunkId&)`. Building a chunk's mesh clears its
  dirtiness, so `buildMesh` is non-const. `app.cpp` remembers which chunks
  it has meshed so an F9 face reorder re-meshes them all. Mesh positions
  are relative to the chunk's minimum corner (the mesh origin); positional
  hashes use world cells.
- **Renderer:** unchanged; it holds one mesh, which is the one chunk's. It
  becomes a mesh per chunk when a second chunk exists (v0.2.24).
- **F10 bounds** now draws the chunk's 64³ box. No capture shows it
  differently (the box edges beyond 32 fall outside the `overlays` view).
- **Scenario:** `world.generator` (only `empty`); `world.grid` is rejected
  naming `world.generator`. `scenarios/v0.1-scene.json` and the README
  follow.
- **Tests:** `tests/sandbox_tests.cpp` is now `main()` only; the tests are
  split into `tests_{coords,chunk,orientation,blocks,shading,world,
  placement,input,translucency,tools,scenario}.cpp` with shared helpers in
  `test_support.*`. Every old test function is kept (diffed by name);
  `testGridEditsMeshEmitters` became `testChunkEditsMeshEmitters`, with
  boundaries at 0 and 63/64. New: `tests_chunk.cpp` (floor division and
  local indices at negative, zero and boundary coordinates and at ±10⁹;
  palette reuse and append; copies are independent; probes and edits at all
  8 corners and 6 face centres; every just-outside neighbour rejected).

### Measurement: meshing a full 64³ chunk
`build-release/sandbox_tests --time-mesh` (median of 7, per-cell rule,
Clang 19 Release, this headless authoring machine; the reference machines
(`engine/reference-hardware`) are still to run it):

| World | Time | Faces |
|---|---|---|
| solid stone (shell faces only) | 18.1 ms | 24,576 |
| 3-D checkerboard (the most faces: 6 per solid cell) | 134.1 ms | 786,432 |
| v0.1 scene | 1.4 ms | 4,890 |

### Results (Clang, lavapipe/Xvfb)
- `ctest`: 8/8 Debug, 7/7 Release; zero warnings, `-Werror`.
- Headless suite on the chunk world: 65 runs pass, zero validation errors.
  Against the Clang reference, 60 captures are pixel-identical; the same
  five (`hotbar`, `lighting`, `lit`, `overlays`, `resize`) differ only in
  the live F1 timing window and F7 column. `--check-absorbance` passes.

### Acceptance status (v0.2.1)
| # | Criterion | Status |
|---|---|---|
| 1 | Build + ctest, Debug and Release, `-Werror`, zero warnings | ✅ |
| 2 | Captures identical (fp-contract step; Clang within tolerance, inspected; chunk identical to Clang reference); absorbance | ✅ |
| 3 | Unit tests: floor division/local indices, face and corner probes and edits, outside rejected, palette reuse and append | ✅ |
| 4 | `DenseGrid` gone; `world.grid` rejected naming `world.generator` | ✅ |
| 5 | The v0.1 world plays unchanged | ✅ owner (interactive use, 2026-11-26) |
