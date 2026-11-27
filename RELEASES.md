# Release notes

Per point release: the choices made where `versions.md` left something open
(the simplest reversible option each time, so revisit freely), the results of
verification, and measurements. How to build and run is `BUILD.md`; what a
release is meant to do is `versions.md`. Notes for finished major versions
move to `attic/` once they stop being useful.

## Tagging a release
A point release is tagged `vX.Y.Z` (annotated, `git tag -a`) on the commit
that closes it. That commit sets `project(sandbox VERSION X.Y.Z)` in
`CMakeLists.txt`, so the window title, the F1 overlay and the Vulkan app
version match the tag, and marks the release closed in `versions.md` and
here. Documentation commits after it aren't tagged. The tag message is the
release's title and claim from `versions.md`. Tags are pushed only when the
owner says so.

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

## v0.2.2 (closed 2026-11-26)

Uniform, palette widths 1–32 bits, growth, compaction and the content digest,
behind the chunk's interface. The prototypes (`attic/prototypes/brick/`) were the
starting point; the spec won where they differ (their `NOTES.md` lists where).

### Choices
- **Files:** `brick.h` (rewritten: abstract `Brick<E>` with `UniformBrick`
  and `PaletteBrickAt<E, B>`, one instantiation per width; a palette Brick
  is built, never resized, and `widened()` returns the next-width Brick),
  `reverse_lookup.h` (the owner's lookup), `digest.h`, `fatal.h`
  (always-on fatal checks with a throwing test handler), `chunk_mesh.h/.cpp`
  (the per-cell mesher, moved out of `ChunkWorld` and run over any
  `ChunkData`), `chunk.h/.cpp` (`ChunkData`: the representation pointer, the
  reverse lookup, the lazy digest).
- **The representation pointer is the Brick pointer** `ChunkData` already
  held. A change builds the new Brick from the old, swaps the pointer and
  frees the old one directly. Reads go through one virtual call per cell
  (the mesher and raycasts), which cost the per-cell mesh 13–42% (below);
  v0.2.3's bitmask meshing doesn't read cell by cell.
- **Palette capacity is independent of the index width** (owner,
  2026-11-26; first built as capacity = the width's range, which sized the
  reverse table at 512 KiB for any 16-bit chunk). A palette is one array
  allocated at its capacity and never a vector: the capacity is the next
  power of two at or above its entries, at least 16 (a placeholder), at
  most what the width can name (2ʷ). A full palette is replaced by a new
  Brick with twice the capacity at the same width, or, once the capacity
  equals the width's range, at the next width; the old Brick is retired
  (here freed at the swap) and is never written after it. So the 1-, 2- and
  4-bit widths hold 2, 4 and 16 entries and never double, and an 8-bit
  palette swaps at 32, 64 and 128 entries before widening at 256.
- **Growth is the fallback only:** it happens when an edit needs a new
  entry and the palette is full. There is no high-water mark and no
  discretionary compaction (chunk actors, v0.2.23).
- **Compaction** (bulk fills and the test API) picks uniform if one state
  is live, else the narrowest width with live entries at most 7/8 of
  capacity, so two live states take 2 bits, not 1 (width 1 comes from
  promotion, or is forced). The v0.1 scene's 20 states take 8 bits.
- **A bulk fill** is `ChunkWorld::applyBulk`: the edits through the normal
  path, then compaction, ending where a fill from scratch would. The
  `empty` generator is a uniform air chunk. `app.cpp` applies the scenario's
  scene with it.
- **The reverse lookup** is a scalar scan up to 8 entries, then a table
  sized to the palette's capacity (at most about twice its entries),
  built lazily and dropped on any swap or compaction (rebuilt from the
  palette, O(entries)). The prototypes' SIMD scan (threshold 32) needs
  4-byte entries with no padding, which `BlockState` doesn't promise; not
  done. The owner agreed to the scalar scan and to revisit it.
- **The digest:** `H(s)` hashes `type << 8 | orientation` (the stand-in
  until v0.2.4), through a domain-separated SplitMix64 finaliser reduced
  mod p; `W` and the arithmetic are as `data/content-digest` defines them.
  Lazy per chunk, maintained per edit, kept across any change of
  representation. `K` is computed once per process on first use (0.7 ms).
- **Test-only APIs:** `ChunkTestAccess` (`chunk.h`: representation, width,
  palette size and capacity, live entries, force a width, compact, drop the
  digest, whether the table is built, growth count, bytes) and
  `ChunkWorldTestAccess` (the world's `ChunkData`). `ChunkWorld::paletteSize`
  is gone. `gReverseScanMax` can be changed by measurements only.

### Tests
- **Digest** (`tests_digest.cpp`): modular arithmetic at the edges and
  against 128-bit division; all 2¹⁸ weights nonzero and on coordinates; `K`;
  `mix64(0)` pinned; uniform digest `H(s)·K` in every width; incremental
  equals recomputation after 20,000 random edits; any width or compaction
  leaves it unchanged.
- **Representations** (`tests_representation.cpp`): promotion; growth at
  every width boundary 1→2→4→8→16→32; capacity doubling within a width,
  32 → 64 → 128 → 256, then widening; the palette array never moves and the
  retired Brick stays intact; a full palette with a dead entry grows; a
  dead entry is reused; no edit compacts; the low-water width rule and the
  build capacity; bulk
  fill (the v0.1 scene, uniform, two states); uniform air and solid meshes;
  the reverse lookup either side of its threshold; a 32-bit palette past
  2¹⁸ entries by sixteen doublings from 16; the fatal checks.
- **Randomized equivalence** (`tests_equivalence.cpp`): per seed, copies by
  bulk fill, by single edits from uniform air, with a lazy digest, and
  forced to every width that holds the content, against a plain-array
  oracle: probes after every edit, and at checkpoints the corners, a
  random 4096 cells (every cell at the last one), 24 raycasts (an
  independent DDA), the digest, and the per-cell mesh as a sorted quad list
  (bulk fill against the widest copy, every fourth checkpoint). Phases:
  random edits, growth across width boundaries, a full palette with a dead
  entry, explicit compactions, a bulk fill of new content, collapse to one
  state and compaction to uniform, promotion again. Six fixed seeds run in
  `ctest` (about 3 s Release, 25 s Debug); `sandbox_tests --long` adds 24
  seeds of 20,000 edits (16 s Release).
- **Mutation checks** (by hand, each caught): skipping the incremental
  digest, dropping the last cell when widening, off-by-one renumbering in
  compaction, the lookup missing an entry, a wrong low-water rule.

### Measurements (this machine, Release, Clang 19)
`build-release/sandbox_tests --time-chunk`; runs vary by about ±30%.
Only one reference machine was measured (the owner did not gate the release
on the second).

| Per-edit cost, ns (random cell, random existing state) | lookup | ns |
|---|---|---|
| 1 bit, 2 entries | scan | 16 |
| 2 bits, 4 entries | scan | 19 |
| 4 bits, 16 entries | table | 19 |
| 8 bits, 32 / 33 / 200 entries | table | 19 / 17 / 20 |
| 16 bits, 32 / 200 / 5000 / 65000 entries | table | 20 / 19 / 23 / 42 |
| 32 bits, 32 / 200 / 5000 / 70000 entries | table | 22 / 20 / 20 / 43 |

- **Scan against table** (width 8, table sized to capacity): the scan is 19
  ns at 2 entries, 22 at 8, 26 at 16, 54 at 32 and 79 at 64; the table is
  18–20 ns throughout. The scalar scan's crossover is 2–8 entries, so the
  threshold is 8 (it was 32 with the prototypes' SIMD scan).
- **Growth to the next width** (a repacking edit): about 0.5–0.65 ms up to
  16 bits, 1.1 ms for 16→32 (65,536 entries).
- **Capacity doubling within a width** (a copy of the indices and palette):
  0.013 ms at 8 bits (32→64), 0.055 ms (128→256), 0.04 ms at 16 bits
  (1,024→2,048), 0.32 ms (32,768→65,536), 1.9 ms at 32 bits
  (131,072→262,144).
- **Explicit compaction:** 0.6–1.0 ms.
- **Full digest:** 1.2 ms for a palette chunk. The weights alone cost 0.69 ms
  computed and 0.30 ms from a precomputed 2 MiB table: the table would save
  about 0.4 ms of the 1.2 per full pass, which happens once per chunk
  lifetime; not worth 2 MiB, not adopted. A uniform digest is O(1) after `K`
  (computed once per process, 0.7 ms).
- **Memory** (indices + palette array + reverse table, KiB): 32 (1 bit),
  64 (2), 128 (4), 256 (8 bits, 32 entries: 256.4; 256 entries: 259),
  518 (16 bits, 300 entries: 512 + 2 + 4), 1,280 (16 bits, full: 512 + 256 +
  512), 2,560 (32 bits, 70,000 entries: 1,024 + 512 + 1,024). Uniform: 4
  bytes. (The first build sized the table to the width's range: 1,026 KiB
  for the 300-entry 16-bit chunk, 5,632 for the 70,000-entry 32-bit one.)
- **The v0.1 scene chunk:** width 8, palette 20, live 20 (capacity 32).
- **Per-cell meshing** is slower through the virtual `at`: 23.8 ms solid,
  151 ms checkerboard, 2.0 ms for the v0.1 scene (v0.2.1: 18, 134, 1.4).

### Acceptance status (v0.2.2)
| # | Criterion | Status |
|---|---|---|
| 1 | Build + ctest, Debug and Release, Clang, `-Werror`, zero warnings | ✅ (Debug 8 tests, 26 s; Release 7, 4 s) |
| 2 | Every v0.2.1 capture identical; absorbance | ✅ (65 runs pass; the same five differ only in live F1/F7 text) |
| 3 | Equivalence test in `ctest` for fixed seeds; a longer run once | ✅ (`--long`: 24 more seeds, passes) |
| 4 | Digest unit tests | ✅ |
| 5 | Representation unit tests | ✅ |
| 6 | The v0.1 world plays unchanged | ✅ owner on `a83b9c6` (2026-11-26); the palette-capacity change after it re-checked by the 65 headless runs |

## Cleanup between v0.2.2 and v0.2.3 (2026-11-26)
No behaviour change: the 65 headless captures match v0.2.2's, and the tests pass.
- **`ChunkWorld::emitters()` is O(emitters):** the world keeps the set of
  emitting cells, updated in `apply` from the old and new state of each
  changed cell (z, y, x order, as the scan gave). It had scanned all 262,144
  cells on every edit, a v0.1 dense-grid method carried over. A test compares
  it with a brute-force scan across random edits, bulk applies and copies.
- **`chunk_coord.h`** holds `ChunkCoord`, `ChunkId`, `chunkOf` and `localOf`;
  `world_query.h` includes it instead of `chunk.h`, so tools, placement and
  the renderer no longer pull in the Brick, the digest and the lookup.
- **`hash.h`** has the one `mix64`; `tools.cpp`'s copy is gone (same function,
  so the container captures match). `kDigestK` is a literal that the digest
  test checks against the sum of the weights.
- **The scan threshold** is a member of the lookup, set through
  `ChunkTestAccess`, not a global.
- **Measurements** moved to `tests/measurements.cpp`.
- **`ctest`** has `sandbox_equivalence` apart from `sandbox_tests`, so they run
  together under `-j` (Debug: 24 s instead of 28). `no_dense_grid` is retired.
- **`prototypes/` moved to `attic/prototypes/`.**
