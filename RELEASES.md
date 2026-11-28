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
Clang 19 Release, on the Dell Latitude 7480, `engine/reference-hardware`):

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

## v0.2.3 (closed 2026-11-27)

Greedy bitmask meshing per representation, `render.mesher`, and the
merged-quad harness retired.

### Choices
- **The mesher is chunk code, `BlockState`-specific, inside the palette
  representation** (owner, 2026-11-26): `bitmask_mesh.cpp` is instantiated
  per index width and reads the Brick's packed words directly (one virtual
  dispatch per mesh, none per cell). Making it entry-agnostic for coarse
  Bricks is dispositioned in v0.2.4. Measured before choosing, for the
  opaque mask alone: a virtual call per cell 0.43–0.47 ms, export to an
  array 0.21–0.26, a fused loop per width 0.09–0.18.
- **Dispatch** (`ChunkData::meshInto`, append-only): `per-cell` reads cells
  through the chunk's interface (the reference); otherwise uniform air
  emits nothing and touches no working state, uniform opaque emits six
  64×64 quads with no masks, uniform translucent runs the translucent pass
  with an all-ones group mask, and a palette runs the bitmask mesher.
- **Appearance ids** (`appearance.h`): every registry type × 24
  orientations × 6 faces, interned once per `ChunkWorld` over the payload
  (layer, material, packed texInfo). A stand-in until v0.2.4's state table.
- **Translucent faces** are emitted one quad per face in the per-cell rule's
  order: per row, every group's six face words are combined and the set
  bits visited in ascending x, each cell's faces in face order. Frame bits
  come from the masks, not from `frameBits`. Translucent output is
  byte-identical to the per-cell rule's.
- **What "byte-identical" covers:** origin, vertices, indices, translucent
  vertices and indices, and edge lines, in order. It holds across every
  representation and width for one mesher. The per-cell and bitmask meshers
  order opaque quads differently (cell order against direction, slice,
  row), so between meshers the opaque comparison is the unit-face multiset.
- **Uniform translucent** emits 24,576 unit quads (one per boundary face),
  as its palette form does.
- **Quad geometry:** a quad's four vertices follow the unit cube's corner
  order scaled along the face's bit and row axes, so a 1×1 quad is exactly
  the per-cell rule's face (the `unmerged` captures are pixel-identical).
- **Working state:** one `thread_local` struct (masks, ±x planes, per-entry
  tables, group masks), grown only for a larger palette or more groups;
  198 KB after the measurement cases. Re-entry on one thread is fatal.
  `MeshTimings` is an optional per-thread hook for the phase breakdown.
- **`render.mesher`** (`greedy` default, `unmerged`, `per-cell`) is a field
  of `ChunkWorld`, not of the `WorldQuery` seam. `render.mergedQuad` is
  rejected naming it; `mergeTopFaces` and its region constants are gone; the
  `quad_*` pairs compare `per-cell` against `greedy`.
- **`tools/headless_check.sh` takes a mesher** as a third argument and runs
  every scenario under it through a wrapper that includes the real one.

### Deviations from the spec, with the owner's decisions
- **Pinhole cracks at T-junctions** in `greedy` captures: isolated pixels,
  1–22 per capture on lavapipe, where the sky or a surface behind shows
  through. The `quad_*` pairs differ in 5 pixels (variant view) and 6 over
  1/255 (lit, up to 61/255). Not perceptible to the owner on the GPU
  machine. Owner's choice (2026-11-27): defer the fix (enlarging merged or
  all opaque quads) until more of the architecture is in place, and
  disposition it in v0.2.4. The `quad_*` comparisons therefore allow a
  recorded budget of 16 pixels over their tolerance (`imgdiff.py --allow`),
  so a real regression still fails.
- **`log_columns`: 216 pixels over 1/255** with `greedy`. The `unmerged`
  capture matches the reference exactly, and the differing pixels sit where
  the reference has high local contrast (median neighbour contrast 13
  against 5 for random geometry pixels). That is consistent with merged
  quads interpolating position across cell boundaries, so that `floor(pos)`
  picks the neighbouring cell's matched bark tile. Cause not fully
  confirmed; dispositioned in v0.2.4.

### Tests
- **Face sets** (`tests_meshing.cpp`): faces at every corner and face centre
  against outside air; runs at bit 0, bit 63 and the full width, rows 0 and
  63; a 64×64 plane to one quad; a checkerboard to one quad per face; two
  log orientations with different turns not merging; `random` and `matched`
  faces merging; glass against stone; frame bits from masks equal the
  per-cell rule's on the v0.1 scene and a three-glass blob; the transpose.
  Two cases are vacuous on shipped blocks and say so when run: no two
  distinct states share a face payload, and no two block types share a face
  texture (materials are per type).
- **Uniform fast paths, working state, a second `std::thread`, append-only
  output** (acceptance 6–8).
- **Mesh equivalence and rep-swap invariance**
  (`tests_mesh_equivalence.cpp`): eight fixed cases (sparse, medium, dense,
  terrain, boxes, uniform solid and glass, two states), each in the widths
  that hold it, built by edits (dead entries) and with renumbered entries,
  before and after random edits. Greedy and unmerged equal the per-cell
  rule's unit faces and translucent bytes, and every copy meshes to the
  bulk fill's bytes. Each case is its own `ctest` entry
  (`mesh_equivalence_N`, timeout 600 s); `sandbox_tests --mesh-case=N` runs
  one. `--long` adds 24 random cases at every width.
- **A slow test that was a bug:** the first version of case 7 asked for 300
  distinct states from a registry of about 100, and its loop drawing them
  never ended (the suite ran 30 minutes before it was stopped). Found by
  running each case alone in parallel under a timeout, then taking gdb
  stack samples (`tools/sample_stacks.sh`). Fixed by capping the draw at the
  registry's size; dense content is now a corner region.

### Measurements (this machine, Release, Clang 19; `sandbox_tests --time-meshers`)
| Case | greedy ms | unmerged ms | per-cell ms | greedy quads (+ translucent) | unit faces per quad |
|---|---|---|---|---|---|
| v0.1 scene | 0.63 | 1.12 | 2.55 | 163 (+132) | 17.0 |
| uniform solid | 0.002 | 3.9 | 17.9 | 6 | 4,096 |
| heightmap (stone, dirt, grass) | 1.63 | 3.27 | 11.2 | 6,880 | 2.97 |
| checkerboard | 152 | 147 | 154 | 786,432 | 1.00 |
| random, many states | 71 | 76 | 83 | 330,674 (+50,418) | 1.01 |

- **Phases** (greedy, ms): v0.1 scene masks 0.37, culling 0.22, transposes
  0.04, merging 0.03, emission 0.12; heightmap 0.52 / 0.10 / 0.05 / 0.37 /
  0.83. In the worst cases emission dominates (checkerboard 161 of about
  190 ms with timers on): about 200 ns per quad, growing the four-vertex,
  28-byte-per-vertex output and its outline lines (130 MB for the
  checkerboard). That is the cost v0.2.5's 8-byte packed quads address;
  masks and culling stay under 2 ms in every case.
- **Bytes per mesh:** 48 KiB greedy against 824 KiB unmerged for the v0.1
  scene; 1 KiB against 4 MiB for a uniform solid.
- **Working state:** 198 KB after all cases.
- **v0.1 scene, coplanar neighbouring opaque faces:** 8,792 with equal
  payload, 0 split by orientation, 0 by material alone, 230 with different
  textures. The scene has no rotated blocks side by side, so it doesn't test
  `world/block-orientation`'s expectation.
- Only one reference machine was measured.

### Reference captures
The owner accepted the `greedy` captures (`build-debug/cap-023`), cracks and
`log_columns` flips included, as the reference for later releases.

### Acceptance status (v0.2.3)
| # | Criterion | Status |
|---|---|---|
| 1 | Build + ctest, Debug and Release, Clang, `-Werror`, zero warnings | ✅ (16 tests Debug, 31 s with `-j8`; 15 Release, 6 s) |
| 2 | `per-cell` and `unmerged` identical to v0.2.2; `greedy` within 1/255 on diagonals and F3 outlines; absorbance with every mesher | ✅ for `per-cell`, `unmerged` and absorbance; `greedy` also shows the deferred cracks and the `log_columns` flips (above) |
| 3 | Face-set equivalence, fixed seeds in ctest and a longer run | ✅ (`--long` passes) |
| 4 | Rep-swap invariance | ✅ |
| 5 | Unit tests | ✅ (two vacuous on shipped blocks, noted) |
| 6 | Uniform fast paths | ✅ |
| 7 | Working state; a second thread | ✅ |
| 8 | Append-only | ✅ |
| 9 | `render.mergedQuad` rejected; `quad_*` comparisons | ✅ rejected; comparisons pass within the recorded crack budget |
| 10 | Plays unchanged; F3 shows merged quads | ✅ owner (stripes across the floor, large merged surfaces) |
| 11 | No cracks or sparkles at T-junctions | ✅ owner: not perceptible interactively |

## v0.2.4 (closed 2026-11-27)

### Dispositions (owner, 2026-11-27)
The open items listed at the head of v0.2.4 in `versions.md`, each decided.

| Item | Disposition |
|---|---|
| The owner's review of the Brick and chunk designs | **Done (design):** units rather than feet in engine docs; the chunk or terrain node is the abstraction boundary, the Brick only storage; superclasses are blocks and never obtainable; no chunk-data type, `Chunk` is the actor and owns its Brick. **Code in v0.2.4:** `ChunkData` becomes `Chunk`; raycasting and the emitter set move into it. |
| Mesher layering (entry-agnostic for coarse Bricks) | **Dropped:** palettes hold block states at every level, so the mesher's entry type is always `BlockState`. Meshing at a coarse cell size stays v0.2.9's. |
| T-junction cracks; `log_columns` tile flips | **Tile flips scheduled to v0.2.5**, whose packed quads rewrite that shader path. **Cracks deferred** until they show on GPU hardware (owner, 2026-11-27; first recorded as scheduled to v0.2.5). |
| Merge key and materials | **Deferred** until materials are their own definitions; open in `render/block-textures`, not yet scheduled. The two vacuous merge tests keep printing their notice. |
| Reverse lookup: SIMD scan, 16-bit table slots | **Dropped:** the scalar scan is accepted (16–43 ns per edit), and the table is at most about twice the entries. Revisit only if a profile shows edits. |
| Sub-byte widths (1, 2, 4 bits) | **Scheduled to v0.3**, when real chunks show where palettes sit. |
| Per-cell reads through a virtual call | **Dropped:** no hot path reads per cell any more (the mesher reads the Brick directly, emitters are maintained, the per-cell mesher is only the reference). |
| Discretionary compaction | **Decided:** compaction is never automatic, since neither the Brick nor its owner has the context to schedule it. It runs at a save or a bulk fill, or when a caller asks through an explicit operation that nothing uses yet (`world/brick`). |
| Measurements on a second reference machine | **Dropped:** releases are not validated on several machines. Measurements name the machine they ran on (`engine/reference-hardware`). Every v0.2.x number so far is from a Dell Latitude 7480: Intel Core i7-7600U (2 cores, 4 threads, 2.8–3.9 GHz, 4 MiB L3), 16 GB, Intel HD 620 (Kaby Lake GT2), Debian 13. |
| Profiling call graphs | **Done in v0.2.4:** frame pointers in every build, so `perf record --call-graph fp` works. |

### Choices (built 2026-11-27)
- **`Uid`** (`uid.h`) is the one 128-bit value type, with the aliases
  `Ulid` (minted) and `StateHash` (derived). The spec named the type `Ulid`;
  a state hash isn't a ULID (glossary), so the shared type has a neutral
  name, `Uid` (owner, 2026-11-27: uniqueness matters more than the width). Crockford text is strict: upper case out, either case in, `I`, `L`,
  `O`, `U`, wrong lengths and a first character above `7` rejected.
- **BLAKE3 1.8.8**, its portable C files only, built as a static library
  with every SIMD path compiled out (`deps/hashing`); checked against five
  of its published `derive_key` vectors. Context string
  `"lanterns-reach 2026-11-27 block state v1"`.
- **No value is a designated default** (owner, 2026-11-27, during the
  build): properties are optional (absent, or one of their values; a flag
  is optional with the one value `true`) or mandatory (always written).
  JSON: `"flag"`, a list (optional), or `{"values": [...], "mandatory":
  true}`, with `"initial"` naming each mandatory property's starting value.
  The spec's "first value is the default" and its reordering test are
  replaced: reordering a domain never changes a hash, adding an optional
  property never does, adding a mandatory one always does.
- **The state table** is owned by `BlockRegistry` (`registry.states()`,
  append-only, so a const registry can still intern). Registry type t is
  the table's definition t + 1; air is definition 0 and `StateId{0}`, with a
  built-in ULID (`01M8F3N35FMTBBXQ1NFX22HGFS`). A row holds the hash, the
  digest term, the definition, the dense index, the rotation, the
  visibility class and six appearance ids; property values are decoded
  from the dense index. Every shipped state (42) is interned at load.
- **The dense index** is orientation slot (canonical rotations ascending)
  then properties in name order, mixed radix; `withOrientation`, `with` and
  `without` are lookups in it, interning only a state never seen.
- **`H(s)`** is stored per row (`digestTerm`); the stand-in
  `digestStateHash` is gone. The Brick's `fullDigest` takes the term as a
  function from its owner, and its reverse lookup hashes `lookupKey`
  (the `StateId` itself).
- **Callers:** `BlockState` is gone. `registry.state(type, rotation)`
  gives a `StateId`; reads go through the table (`type`, `orientation`,
  `definition`, `appearance`, `visibility`). `afterCommit` and
  `targetingOverlay` gained a `StateTable&` parameter (they had no
  registry). Air checks compare with `kAirState`.
- **`Chunk`** holds `const StateTable&`, its id, its emitter set (moved from
  `ChunkWorld`) and raycasting; `meshInto` uses the chunk's own id.
- **Test states past the registry** are states of synthetic definitions in
  the shared table (one mandatory property of 2¹⁰ values each), interned
  on first sight, so the representation tests' 2¹⁹ distinct states are real
  table rows; the lazy interning path carries them.
- **The minting tool** is `tools/mint_ulid.py`; every shipped definition
  and negative fixture got its ULID from it. `--add` inserts the field
  after the opening brace.
- **Frame pointers** (`-fno-omit-frame-pointer -mno-omit-leaf-frame-pointer`)
  in every target. `perf record --call-graph fp` needs
  `kernel.perf_event_paranoid` at 1 or below on this kernel (at 2 it refuses
  even user-only call chains); the owner set it to 1, and `BUILD.md` says
  how.

### Tests
- **Identifiers** (`tests_identity.cpp`): Crockford text at 0, the maximum,
  pinned values and 2,000 random ones, lower case, every rejection; BLAKE3's
  vectors at lengths 0, 1, 1023, 1024, 1025.
- **States** (`tests_states.cpp`): air; canonical texts pinned exactly
  (`<ULID>[color=red,orientation=+y+x,stage=b]`) and two hashes pinned;
  evolution (an added optional property keeps all 48 fixture hashes, an
  added mandatory one keeps none, reordered domains and another initial
  value keep all); orientation texts for all 11 symmetries (equal exactly
  for equal cosets, the identity writes nothing, each parses back); round
  trips of every shipped and fixture state through intern, hash, text,
  dense index and named values; no two hashes equal; 13 malformed texts
  rejected; lazy interning; fatal checks (duplicate ULID, over 2¹⁰ states,
  a value outside its domain, `without` on a mandatory property); pinned
  digests (v0.1 scene 1243621874014904966, uniform stone
  1879917277027827065); and nothing interns after load (scene, three
  meshers, emitters, a raycast, every tool's plan).
- **Loader** (`ctest`): missing, malformed, duplicate and air's ULID, a
  reserved property name, a one-value mandatory domain, a mandatory
  property without an initial value, 1,032 states; the minting tool.
- The v0.2.2 and v0.2.3 equivalence and meshing tests pass with `StateId`
  palette entries; `--long` passes.

### Measurements (Dell Latitude 7480, Release; `sandbox_tests --time-states`)
- **Interning a new state:** about 520 ns (BLAKE3 over a 26–60 byte text,
  the row, the maps); **an existing one:** about 12 ns.
- **Loading the 16 shipped definitions with textures and interning their
  42 states:** 0.9 ms.
- **The state table:** about 168 bytes per state (row, hash map, dense
  index), so 2¹⁹ states would take about 84 MB.
- **States per definition:** furnace 24, log 3, every other block 1.
- **Interning after load:** none (a test counts it).

### Acceptance status (v0.2.4)
| # | Criterion | Status |
|---|---|---|
| 1 | Build + ctest, Debug and Release, Clang, `-Werror`, zero warnings, frame pointers | ✅ (25 tests Debug, 32 s with `-j8`; 24 Release, 5 s) |
| 2 | Every v0.2.3 capture identical; `quad_*` within budget; absorbance | ✅ (65 runs; only the live F1/F7 text differs) |
| 3 | Identifier and BLAKE3 tests | ✅ |
| 4 | State-hash tests | ✅ (with the owner's optional/mandatory rule in place of first-value defaults) |
| 5 | Round trips | ✅ |
| 6 | Loader rejections | ✅ (8 fixtures) |
| 7 | Pinned digests; equivalence tests with `StateId` | ✅ |
| 8 | The minting tool | ✅ |
| 9 | `ChunkData` and `BlockState` gone; raycast and emitters are `Chunk`'s | ✅ |
| 10 | The v0.1 world plays unchanged | ✅ owner (interactive use) |

## v0.2.5 (in progress)
Built in steps, each committed with its own checks (owner, 2026-11-27):
**a** packed quads and vertex pulling with no visible change; **b** the
fragment's cell from its quad (`log_columns`); **c** inner-corner bits;
**d** constrained translucent merging; **e** the cutout class and grate.

### v0.2.5a: packed quads, pulled (built 2026-11-27)
#### Choices
- **`packed_quad.h`** defines the bits (`quad::Quad`, `pack`, `unpack`, the
  face axes and `kRightAlongBit`); `shaders/quad.glsl` mirrors it. Side bits
  are in the face frame, −R, +R, −U, +U (connectedTile's frame-bit order);
  corner bits (−R−U), (+R−U), (−R+U), (+R+U). Appearance ids past 2²³ − 1
  are fatal in the state table.
- **Buffer device addresses**, not descriptor sets: each draw pushes the
  address of its quads and of the appearance table, so a mesh replaced at
  any time needs no descriptor allocated or freed. `bufferDeviceAddress`
  (Vulkan 1.2 core) is now required; lavapipe has it and so does the HD 620.
- **Push constants stay within 128 bytes:** the translucency passes'
  frame-wide constants (light direction, sun, near/far, light count, the
  frames switch, the MBOIT bias and overestimation) moved to a per-frame
  uniform buffer beside the frame's lights (set 1, binding 1), and the
  absorbance pass now binds that set too. Its push block is the matrix, the
  mesh offset, the mesh origin's cell and the two addresses (112 bytes).
- **The appearance table** is `uvec4` per id (layer, material, texture
  info, unused): 16 bytes, uploaded whole when the state table has more
  payloads than were uploaded. The shipped blocks have 87 (largest id 86;
  1,392 bytes).
- **One static index buffer** (0 1 2 0 2 3 per quad), grown by doubling to
  the largest mesh drawn; retired buffers stay alive with the meshes that
  use them.
- **Translucent faces** carry their appearance id (the state's, so no CPU
  tile resolution) and, when framed (a connected set on an absorbing block,
  `framedFace`), their frame bits as side bits. The fragment shader picks
  the connected tile from the side bits at the fragment's cell (clamped to
  the quad) and random or matched tiles from the cell plus the mesh origin,
  so translucent faces now need the origin cell too. Opaque random and
  matched tiles still hash the unclamped `floor(pos − n/2)` cell, as in
  v0.2.4; step b changes that.
- **Ghosts** keep `MeshVertex` (moved to `block_mesh.h`) and get their own
  vertex shader, `ghost.vert`, and two pipelines sharing the translucency
  fragment shaders.
- **F3 outlines** come from a pulled line pipeline (`quad_lines.vert`, 8
  vertices per quad) over both quad arrays; `WorldMesh::edgeLines` is gone.
- **The bitmask mesher's API** lost its `ChunkId` (positions are
  chunk-local and translucent tiles are no longer resolved on the CPU).

#### Results
- `ctest` passes, Debug (25) and Release (24), zero warnings.
- **Captures** (`cap-025a`, greedy): all 65 runs pass, and every capture is
  pixel-identical to `cap-024` except the five with live F1/F7 text
  (hotbar, lighting, lit, overlays, resize), whose differences all lie in
  the text panels.
  With `unmerged` (`cap-025a-unmerged`) and `per-cell`
  (`cap-025a-percell`), 59 runs each (the `quad_*` pairs run only with the
  default mesher) pass and match `cap-023-unmerged` and `cap-023-percell`
  (which v0.2.4 left unchanged) with the same five exceptions.
  `--check-absorbance` passes with all three meshers, as do the
  order-invariance comparisons.
- **Tests:** the pack round trip at every field's extremes and each field's
  bit position; `R` along the bit axis as `variants.glsl` has it; the unit
  face decomposition now includes each unit face's frame bits, so the
  equivalence tests compare frames as well as payloads.

#### Measurements (Dell Latitude 7480, Release; `sandbox_tests --time-meshers`)
| Case | greedy ms (v0.2.3) | greedy KiB | same quads in v0.2.4's layout, KiB |
|---|---|---|---|
| v0.1 scene | 0.56 (0.63) | 2.3 | 76 |
| uniform solid | 0.002 (0.002) | 0.05 | 1.5 |
| heightmap | 0.66 (1.63) | 54 | 1,774 |
| checkerboard | 10.8 (152) | 6,144 | 202,752 |
| random, many states | 9.0 (71) | 2,977 | 98,250 |

The v0.2.4 layout cost 264 bytes per quad (4 vertices of 36 bytes, 6
indices, an 8-point outline); a packed quad is 8, so every mesh is 1/33 of
its v0.2.4 size. Emission no longer dominates: the checkerboard's greedy
mesh fell from 152 ms to 11.

- **On the HD 620** (owner, 2026-11-27): the v0.1 world draws and meshes,
  F3 included, through edits and block placement.
- **GPU time, `perf-1080p` on the HD 620** (`--gpu-report`, mean over 1,377
  frames; the `v0.2.4` tag built in a worktree against `b3f61e0`, two
  alternating runs each, on the owner's display with no input; a third
  pair, disturbed by mouse movement, was discarded):

  | Pass | v0.2.4 ms | v0.2.5a ms |
  |---|---|---|
  | gbuffer | 3.16, 3.12 | 3.16, 3.16 |
  | lighting | 4.70, 4.60 | 4.69, 4.60 |
  | absorbance | 0.45, 0.46 | 0.47, 0.48 |
  | translucent | 0.39, 0.38 | 0.46, 0.46 |
  | present | 2.55, 2.53 | 2.53, 2.50 |
  | total | 11.33, 11.16 | 11.37, 11.27 |

  Vertex pulling costs nothing measurable in the G-buffer pass (295 quads
  for the v0.1 scene). The translucent pass is 0.07–0.08 ms slower (about
  20%), and absorbance about 0.02 ms. Both now unpack the quad and choose
  the tile per fragment, and they read their frame constants from a
  uniform buffer rather than push constants. Total frame time is within
  0.1 ms.

### v0.2.5b: the fragment's cell from its quad (tried, reverted)
- **Tried:** the G-buffer fragment shader took the cell a random or matched
  tile hashes from the quad (its corner plus the floor of the position
  within it, clamped to the quad) rather than `floor(pos − n/2)`.
- **Result:** the meshers agreed less. Greedy against per-cell in
  `log_columns` went from 217 pixels over 1/255 to 270, and the `quad_*`
  variant-view pair from 5 to 465, failing its budget. With the clamp, every
  1×1 face keeps its own tile on each cell edge (the rasterizer decides which
  face owns a pixel there), while a merged quad's interior edges still go
  by `floor`, so the two meshers break ties differently. Nudging the sample
  point by a fraction of a pixel in each of four screen directions changed
  nothing measurable; rasterizer tie-breaking is up to the driver anyway.
- **What the `log_columns` pixels are:** none is on the logs. They lie on
  the grass floor (its far edges and short diagonal runs) and along the
  glass boxes. In 180 of the 217 the greedy colour is the per-cell image's
  colour at a neighbouring pixel: texel-seam rounding. A position
  interpolated across a quad up to 64 cells wide rounds differently from one
  across a single cell, and where a seam runs nearly through pixel centres
  a run of pixels lands one texel over. The rest are isolated, consistent
  with cell-edge ties and silhouette pixels (not confirmed one by one).
  v0.2.3's "tile flips" (merged quads picking a neighbour's bark tile) is
  withdrawn, and the count is 217 (v0.2.3 recorded 216).
- **Disposition (owner, 2026-11-27):** reverted; opaque faces hash
  `floor(pos − n/2)` as in v0.2.4 (translucent faces keep the clamp, which
  frames need). The `log_columns` pixels are sub-pixel merge artifacts like
  the T-junction pinholes: not visible on the HD 620, not pursued.

### Capture comparison by visible difference (owner, 2026-11-27)
Pixel identity stays what a regression shows first, but the gate for
captures is now **visible difference**, so sub-pixel artifacts stop costing
investigation (step b was one).
- **`imgdiff.py --tolerant R`** excuses a pixel over `--max` (default 1)
  when its colour is within `--max` of a pixel of the other image at most R
  pixels away, checked both ways. A seam or edge moved by a pixel shows a
  colour already there nearby; a wrong tile or tint, a missing face, or
  sky through a crack doesn't. What's left is grouped into 8-connected
  clusters, and **`--cluster N`** fails on any of N pixels or more.
  **`--mask`** ignores a rectangle.
- **The gate is `--tolerant 1 --cluster 4`.** The `quad_*` pairs use it in
  place of `--max 0/1 --allow 16`. On the v0.2.5a captures they leave 3 and
  5 single pixels (the pinholes). The order-invariance comparisons (1/255)
  and the variant runs (exact, for determinism) are unchanged, as is the
  OIT moment bias (owner).
- **`tools/compare_captures.sh REF NEW`** compares a run with a reference
  capture by capture (`identical`, `invisible`, `VISIBLE`, with exact
  counts), masking the F1 panel (lit, lighting, overlays, resize) and the
  F7 panel (hotbar). `cap-024` against `cap-025a`: all identical once
  masked. `cap-025a` against step b's captures: 5 visible (`log_columns`,
  the four `quad_*`, with clusters of up to 18 pixels in 1-pixel rows), so
  the gate would have caught step b.
- **Self-test** (`ctest imgdiff_visible`): on synthetic images a seam moved
  by a pixel and isolated pinholes pass; a wrong 8×8 block, a 40-pixel
  crack line and a tint 8/255 off fail; a mask hides the block.
- **ꟻLIP** (a perceptual metric) is not adopted yet (owner: premature);
  open to it later.
- **"Pixel-checked" replaces "pixel-identical"** in the shared rules and
  v0.2.5's criteria 2 and 3 (`versions.md`, glossary): identical captures
  are still reported, and any `invisible` difference is listed, not
  investigated. Closed releases keep their wording.
- **Image-reproduction criteria by cost and benefit** (owner, 2026-11-27;
  `versions.md`, shared rules): only the criteria asking for near
  pixel-for-pixel reproduction of a scene (pixel-checked captures, and
  comparisons between captures of one view). A failure there is resolved by
  a fix or by an accepted disposition, and doesn't by itself call for a fix;
  imperceptible or barely perceptible differences are weighed against what
  fixing them costs. Every other criterion is a gate as stated.

### v0.2.5c: inner corners of glass frames (built 2026-11-27)
#### Choices
- **Corner bits** are computed beside the frame bits, by the per-cell rule
  (`frameBitsAt`) and the bitmask mesher (`frameBitsFromMasks`) alike: bit
  k of (−R−U), (+R−U), (−R+U), (+R+U) is set when both of the face's edges at
  that corner continue the surface and the diagonal in-plane neighbour
  doesn't. Only framed faces (connected, absorbing, showing the surface
  layer) have them.
- **The square** is drawn in the fragment shader (`translucent_surface.glsl`)
  in the corner's quadrant of the quad's corner cell: its coverage is the
  lesser of the two one-edge tiles' coverage at that texel (exactly the
  overlap of the two bands, whatever their width), its colour the fully
  framed tile's, and it replaces the face's own tile where it covers more.
  The 16 edge tiles stay. The extra samples use derivatives taken before
  the branch (`blockFaceGrad`), since derivatives are undefined in
  non-uniform control flow.
- **A new scenario, `glass_corners`**, shows the case on a stone floor: an
  L, a 3×3 ring and a 5×5 slab with a hole and a notch. No existing capture
  has an inner corner: every tint region in the v0.1 scene is a straight
  run.

#### Results
- `ctest` passes, Debug (26), zero warnings. A new test (`testInnerCorners`):
  an L carries one corner bit, at its inside corner; a ring carries one at
  each corner cell, pointing at the hole, and none at its edge cells; glass
  under stone has none; all three meshers give the same unit faces,
  corners included.
- `ctest` Release (25) passes too.
- **Captures:** `cap-025c` (greedy, 65 runs, before `glass_corners` was
  added), `cap-025c-unmerged` and `cap-025c-percell` (60 runs each, with
  it) all pass, and every capture they share with `cap-025a` is identical. `glass_corners` against the same scene built
  without step c: 8 clusters of 12–14 pixels differ (up to 74/255), one per
  expected corner (1 in the L, 4 in the ring, 3 around the slab's hole; the
  hole's fourth corner cell is in the notch), each a square of the frame's
  grey filling the gap where two bands met. Its `per-cell` and `unmerged`
  captures are pixel-checked against it (no cluster).

- **Owner, 2026-11-27:** `glass_corners` checked on the HD 620; the corners
  look right.

### v0.2.5d: translucent faces merge (built 2026-11-27)
#### Choices
- **`mergeTranslucent`** (bitmask mesher, `greedy` only) runs the opaque
  merge's shape per translucent group, direction and slice, on the group's
  face planes (±x transposed as for opaque faces). The key is the
  appearance id plus the surface-layer flag, which only framed faces carry.
  With the side constraint, a run grows along the bits only while every
  cell's −U and +U frame edges match the first cell's, and a row is added
  only if its cells' +U edges agree and its end cells' R-side edges match
  the quad's. The quad's side bits come from its first cell (low bit side
  and −U), its last column (high bit side) and its top row (+U); its corner
  bits from its four corner cells. Interior edges always continue (one
  group, all showing the surface or none), so they need no check.
- **`unmerged`** keeps one quad per face in the per-cell rule's order, so its
  translucent quads are still byte-identical to the reference's; greedy's
  translucent quads are compared as unit faces (frames and corners
  included), and their order is now by group, direction and slice.
- **A measurement hook**, `setTranslucentSideConstraint(false)`, merges by
  key alone so `--time-meshers` can count what the constraint costs.
- **A new measurement case, "glass lake":** the heightmap with tinted glass
  (water's stand-in) filling it to z = 26, a large flat surface with a
  jagged shore.

#### Results
- `ctest` passes, Debug (26) and Release (25), zero warnings. Tests: a slab
  merges to six quads, each with all four sides framed; an L and a holed
  square merge to fewer than half as many quads as faces and match the
  per-cell frames and corners; an interface between two tints is framed
  once (the lower type id's face); glass on stone gives one 6×6 bottom quad,
  unframed. The random equivalence cases now cover merged translucent faces.
- **Captures** (`cap-025d`, greedy, 66 runs with `glass_corners`): all pass;
  `--check-absorbance` and the order-invariance comparisons pass. Against
  `cap-025c` every capture pixel-checks except the two with F3 outlines
  (overlays, resize), whose outlines now show the merged translucent quads,
  as expected. The rest are `invisible`: single pixels, at most 7 per
  capture and up to 94/255, at T-junctions of the merged glass (the specks
  the spec anticipated; recorded, not pursued). Against `cap-025c-percell`
  the translucent surfaces pixel-check too (at most 13 single pixels per
  capture); the only other visible differences are the F3 outlines and one
  13-pixel row in `floor_far` that greedy and per-cell already differed by
  in step c (opaque).
- `compare_captures.sh` masks a larger F1 panel (460×300): its width follows
  its text, which differs between meshers ("world mesh: N faces").

#### Measurements (Dell Latitude 7480, Release; `sandbox_tests --time-meshers`)
| Case | translucent quads: unmerged | merged by key alone | with the side constraint | greedy ms (v0.2.5a) |
|---|---|---|---|---|
| v0.1 scene | 132 | 74 | 74 (+0%) | 0.77 (0.56) |
| glass lake | 9,296 | 4,087 | 4,297 (+5.1%) | 1.42 (new) |
| random, many states | 50,418 | 49,861 | 49,862 (+0%) | 12.3 (9.0) |

The constraint costs 5% more quads on the lake's jagged shore and nothing
elsewhere. Merging translucent faces costs mesh time: about 0.2 ms more for
the v0.1 scene (culling and merging the group planes), 3 ms more for the
random case, where almost nothing merges.

- **Owner, 2026-11-27 (display checks 9, part, and 10):** merged glass
  looks right on the HD 620; no off-tint specks close up, at grazing angles
  or at distance.

### v0.2.5e: the cutout class and a grate (built 2026-11-27)
#### Choices
- **`"cutout": true`** in a definition (excludes `translucency`; a new
  negative fixture, `cutout_translucent`) gives its states the visibility
  class `Cutout`. `StateTable::occludes` (opaque only) is what hides a
  neighbour's face now, so neither meshing rule has to list the classes
  that don't.
- **A third mesh array, `WorldMesh::cutoutQuads`** (the spec has opaque and
  cutout in one array). Cutout faces are drawn by a second G-buffer draw
  whose fragment shader alpha-tests (`gbuffer_cutout.frag`, the opaque
  shader with `GBUFFER_CUTOUT` defined; both include `gbuffer_body.glsl`),
  so the opaque draw has no `discard` and keeps early depth testing.
  Back faces are culled, as for opaque faces.
- **The bitmask mesher** gives each cutout type present a group slot, like
  a translucent group (marked cutout), and meshes it with the opaque
  machinery: faces C & ~(O | C) shifted, merged by appearance id
  (`solidFaces`, now shared with the opaque faces). Cutout slots are skipped
  by the translucent passes, and count as opaque for glass frames (no
  surface layer against a cutout cell), as in the per-cell rule. A uniform
  cutout chunk takes the opaque uniform path into the cutout array.
- **The grate** (`assets/blocks/grate.json`, ULID minted): an iron lattice
  from the texture generator's new `grate` recipe (bars two texels wide at
  the edges and through the middle, holes alpha 0), appended last so every
  other texture's seed is unchanged; metallic, roughness 0.45. It sorts
  between grass and gravel, so the block types after it (gravel to
  stone_brick) moved up by one: their material ids too, which is why the
  `material` capture changed (below).
- **Cutout tests use a fixture registry** (`tests/blocks_cutout/`: one
  opaque block, one glass, two cutout types), since only one cutout type
  ships. `unitFaces` takes the registry's state table.

#### Results
- `ctest` passes, Debug (27) and Release (26), zero warnings. Tests: a
  cutout cell against each class on all six sides (air, opaque, glass, its
  own type, another cutout type): its face is hidden by an opaque neighbour
  and its own type only, the neighbour's face toward it is kept except
  between two of one type, and cutout faces land in the cutout array; a row
  of eight merges to six quads; two types side by side keep the faces
  between them and don't merge; a uniform cutout chunk is six 64×64 quads,
  as its palette form is with every mesher; glass against a grate keeps an
  unframed face. Every case agrees across the three meshers.
- **Captures** (`cap-025e`, greedy, 67 runs with `cutout`): all pass with
  zero validation errors. Against `cap-025d` every capture pixel-checks
  except `material`, the material-id view, whose colours changed for the
  blocks that moved up (a sound change: ids are indices, not identity).
  `cutout`'s `per-cell` and `unmerged` captures pixel-check against its
  greedy one (5 single pixels).
- **Appearance table:** 91 payloads (largest id 90), 1,456 bytes.
- **Mesh time:** the v0.1 scene's greedy mesh measured 0.86–0.94 ms in
  three runs (0.77 in step d); not investigated.

- **Owner, 2026-11-27 (display check 9, part):** the grate's holes show
  what is behind it. Two-sided cutout and cutout with tint are deferred to
  foliage (open in `render/transparency`).

### Pending for v0.2.5
- **Shader unpacking against the C++** (acceptance 5's readback): covered
  so far only by the pixel-identical captures.
