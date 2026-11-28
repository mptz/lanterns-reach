# Versions — point-release roadmap

Incremental point versions, each a runnable slice that validates a thin layer of
the architecture. A version lists what it proves, its fully-specified scope, and
any dependency/decision notes. Versions are build targets, not design decisions —
the design lives in `DESIGN.md` + `design/*`; this file says *in what order* we
realize it.

## How to read this file (read this first)

The repository is the whole record: `DESIGN.md`, `GLOSSARY.md`,
`design/*.md` and this file. Read `GLOSSARY.md` first for the vocabulary. A
version's entry states its scope and the build choices the design leaves
open, and names the `group/slug` entries it builds; **read those entries**,
which specify the design itself (from v0.2 on, entries reference the design
rather than restating it). Rules:

- **This file is authoritative for a version's SCOPE.** Where a version entry
  specifies something concrete (vertex count, shading formula, controls), build
  exactly that — do not expand scope from the broader design docs. The design
  docs describe the *eventual* game; a v0.x entry is a deliberately small slice
  of it and says what is explicitly OUT of scope.
- **`group/slug` references** (e.g. `coords/f64-authoritative`) resolve to a
  `## ` / `### ` heading in some `design/*.md` file — grep for the slug. Read the
  referenced entry when you need the rationale, but the version entry already
  states what to do.
- **Decisions already made are not up for relitigation** (language, API, axis
  convention, libraries) unless the owner reopens them. If a choice is
  genuinely unspecified and you must make one to proceed, pick the simplest
  reversible option, implement it, and record what you chose in
  `RELEASES.md` at the repo root (the per-release choices, results and
  measurements; `BUILD.md` is how to build). Don't block on it.
- **Platform:** development happens on machines with a GPU and a display
  (`engine/reference-hardware`); there is no separate headless authoring
  host. The build still needs no display to compile, link or run `ctest`,
  and the automated suite runs headlessly (lavapipe under Xvfb), so it is
  reproducible and runs anywhere.
- **Testing conventions:** a test executable (`sandbox_tests`, its sources
  split by module from v0.2.1) with a minimal in-house `CHECK(expr)` macro (no test framework dependency),
  registered with CTest; Python tool checks registered as CTest tests;
  `ctest --test-dir build` passes headlessly. Display-required checks run
  on lavapipe under Xvfb (`tools/headless_check.sh`) where noted; the owner
  confirms the rest by eye, interactively.
- **`attic/` is history, not authoritative:** don't read it unless a task
  points there.
- **Verification is part of the task.** Each version lists **acceptance
  criteria**; the build is not done until they pass (or, if they can't be checked
  headlessly, until the headless subset passes and the display-dependent ones are
  clearly listed for the owner to confirm interactively).
- **Captures are regression detectors, not specifications.** A
  **pixel-checked** criterion (glossary; from v0.2.5, replacing
  "pixel-identical") catches unintended change; it is never a reason to
  keep a slower or worse design. A capture is compared with its reference
  by visible difference (`tools/compare_captures.sh`): a differing pixel
  passes if its colour is within 1/255 of a pixel of the other image at
  most one pixel away, so a texel seam or an edge moved by a pixel passes
  while a wrong tile or tint, a missing face or a crack line doesn't; what
  remains fails only as a connected cluster of 4 or more pixels. The live
  text of the F1 and F7 panels is masked. Exact identity is still reported,
  and differences that pass are listed in `RELEASES.md`, not investigated.
  Criteria of closed releases keep their wording (they were checked
  exactly). Comparisons that hold a property rather than catch change
  (order invariance at 1/255, the variant runs exactly, for determinism)
  keep their own tolerances. When a capture fails the check,
  investigate the cause and record it in `RELEASES.md`; if it is the
  expected consequence of a sound change (different triangulation, a
  compiler's floating point), say so, and the owner rebaselines after
  inspecting it. Exactness that matters is held by its own test (face sets,
  digests, absorbance), not by pixels.

---

## v0.1 — the throwaway harness (closed 2026-11-04)
Each point release validated one thin layer of the eventual design, so that
v0.2 inherits working pieces. The full specifications are history in
`attic/versions-v0.1.md`; what later releases keep is in the design docs
(the world query seam in `core/opaque-chunk-queries`, the one downcast site
in `coords/typed-positions`, and the testing conventions above).

| Version | Delivered |
|---|---|
| v0.1 | one lit, textured block and a free camera: Vulkan 1.3, the f64/f32 rebase |
| v0.1.1 | four textured blocks: textures and several objects |
| v0.1.2 | hygiene, the dev UI (Dear ImGui), function-key overlays, light tests, harness bindings |
| v0.1.3 | block orientation (24 rotations, symmetry types), JSON block definitions, per-face textures, material ID |
| v0.1.4 | PBR shading, dynamic point lights, emissive blocks, AgX tonemap |
| v0.1.5 | the intention layer, two hotbars, an editable grid behind `WorldQuery`, raycast targeting, place and break |
| v0.1.6 | commutative translucency (MBOIT) and the order-invariance test |
| v0.1.7 | preview mode, edit sets, wrench, shovel, the orientation ghost, held containers |
| v0.1.8 | 16×16 block textures, anti-aliased nearest sampling, mipmaps, variant sets |
| v0.1.9 | code review and refinement (`attic/REVIEW-v0.1.md`), closing v0.1 |

---

## v0.2 — Octree terrain, the concurrency model and the first soft simulation (overview)
- Status: **restructured around octree terrain** (the earlier chunk-first
  roadmap is in git history). v0.2.0 is built and kept as written;
  v0.2.1 keeps its specification, renumbered stand-ins aside.
- **Goal:** a durable world representation first, then concurrency, then
  simulation. Terrain is a sparse octree of terrain nodes, generated
  strictly outside in and refined progressively (`world/surveyor`,
  `world/octave-pyramid`), with chunks' Bricks at its full-detail leaves; it is
  built single-threaded, then driven by goals (`engine/dispenser`'s
  `GoalSource`), then made concurrent. Only then do chunks become
  simulation objects: actors in a Vicinity (`world/chunk`, `sim/vicinity`),
  proven by a first slice of soft simulation. Hard simulation (conserved
  objects, scarce resources) is v0.4.
- **The order follows the data's dependencies:** a chunk's Brick is a leaf
  TerrainNode's, which needs its ancestors, so terrain nodes come before
  chunk actors, and a chunk mesh wanted as a goal pulls everything outside
  in.

### How v0.2 point releases are made
1. **Small:** one build session plus refinement each; v0.2 has 20 or more.
2. **One claim each:** a release opens with the single design claim it
   proves and the test that would fail if the claim were false.
3. **One new thing at a time:** a release adds a new structure or a new
   concurrency mechanism, never both. Structure is proven single-threaded
   first, then made concurrent.
4. **Specs reference the design, they don't restate it:** a release lists
   the design slugs it builds and states only the build choices the design
   leaves open (layouts, placeholder numbers, scenarios, harnesses), so a
   design change doesn't silently make the spec wrong.
5. **Stand-ins are explicit and expire:** an interim shortcut is named with
   the release that removes it.
6. **Every release ends healthy:** playable, the headless suite green, prior
   captures identical.
7. **Measurements are deliverables:** a release that can first measure an
   open number records it in `RELEASES.md`, naming the machine it was
   measured on (`engine/reference-hardware`).
8. **Detail just in time:** only the next one or two releases are fully
   specified; the rest are one line (claim and slugs) and are detailed when
   the release before them starts building. Releases past the next few
   follow designs still being refined (`engine/goal-source` and
   reclamation are drafts), and are re-checked against the design when
   detailed.

**Template for a detailed release:** Claim (and the falsifying test) ·
Builds on (prior release, design slugs) · Scope (build choices only) ·
Stand-ins (each with its expiry) · Out of scope · Acceptance (headless, then
display-required) · Measurements.

### Rules for all of v0.2
- **The v0.1 game layer is unchanged:** hands, hotbars, tools, edit sets,
  preview, the X ghost, containers and the inventory stub (capacity-1 slots
  and held containers, not conserved). The real inventory and scarce
  resources belong to v0.4; the player's body to its write-up in `sim/concurrency-model`.
- **Named constants** as in v0.1.9: every tuning value new in v0.2 is named,
  defined once and explained (a reason, or "placeholder" / "arbitrary").
- **The v0.1 scene survives as a scenario** (v0.2.0) and its headless
  captures stay identical, first in one chunk (v0.2.1), then as an edited
  chunk held at full detail inside octree terrain: the strongest test that
  the seam held.
- **No persistence in v0.2:** edited chunks stay in memory and are never
  evicted. (The persistence design is settled, `sim/concurrency-model`; it is
  built later.)
- **Testing: one world implementation.** Tests exercise the real chunk,
  octree and goal code, not a parallel world built for testing.
  - **Test and mocking APIs are added as needed** (inspection, forcing a
    representation, meshing one chunk under different neighbourhoods,
    running goals in a chosen or random order, dropping or duplicating
    nudges), kept out of production: compiled only into test builds or
    reachable only through a test-only access type.
  - **Tests may run under tighter constraints than production,** above
    all **lockstep mode** (one worker in strict alternation with the frame
    thread or a test driver, `engine/dispenser`), with separate tests
    (lockstep versus N-worker equivalence, TSan) proving that concurrency
    changes nothing.
  - **Once workers exist, play always has at least one free-running
    worker;** lockstep is selected only by tests or a debug flag.
  - A new test-only API or test-only representation is called out in
    `RELEASES.md`.

### Stand-ins in v0.2, and when they expire
- **Frame-thread `apply`** (edits written by the frame thread to the chunk
  holding them): until v0.2.23, when edits go through the Vicinity.
- **Palette entries as today's `BlockState`** (registry index plus
  orientation): until v0.2.4, when they become state hashes.
- **The toy pyramid generator** (a few summed sinusoid octaves per field,
  standing in for FastNoise2 behind the Cartographer seam, with stub
  keys): until v0.3, real worldgen.
- **Generation called directly by the planner** on the frame thread,
  within a per-frame budget: until v0.2.12, when the Surveyor states goals.
- **Goals run on an inline executor** (on the frame thread, within a
  per-frame budget): until v0.2.17, the Dispenser.
- **Every octree level is meshed and drawn,** so progressive refinement can
  be seen: until fallback meshing (`world/surveyor`'s open question on
  which levels are meshed) is designed.
- **Edited chunks are held at full detail and never coarsened** (no
  override grids in ancestors): until override grids are designed
  (`world/surveyor`'s open question).
- **A tool user standing in for the body** in the Vicinity (it turns
  preview-and-commit edit sets into edit messages; no physics): until
  the body is written up (`sim/concurrency-model`).
- **Edit latency of 2–4 frames accepted** from v0.2.19 (worker threads),
  measured rather than hidden; `tools/breaking-as-action` is the eventual
  answer.

### Roadmap
The chunk payload, single-threaded:
- **v0.2.0 Groundwork** (code done; below).
- **v0.2.1 One chunk:** chunk data as a full-detail Brick in the palette
  representation at one width; the v0.1 world placed in chunk (0,0,0). *Claim: v0.1 captures are
  identical with the world in a chunk.* (Detailed below.)
- **v0.2.2 Uniform, palette widths and the digest:** the representation
  pointer behind the chunk's interface, uniform, every storage width,
  promotion, growth, the reverse lookup and explicit compaction. *Claim: a change of representation or width is
  invisible to probes, digests and meshes.* `world/brick`,
  `world/chunk-representation`, `data/content-digest`. (Detailed below.)
- **v0.2.3 Greedy meshing per representation:** bitmask, `thread_local`
  working state, append-only output, whole-chunk remesh. *Claim: it covers
  the same faces as per-cell meshing.* `world/chunk-meshing`. (Detailed
  below.)
- **v0.2.4 Block definitions with ULIDs, and state hashes:** a ULID in
  every block definition, declared variant properties with closed domains
  (orientation formalised among them) and a per-definition state cap,
  state hashes (128-bit content hashes of the definition ULID and canonical
  property values), ids written as 26-character Crockford base32 with
  round-trip tests, the interned state table, and palette entries switching
  from `BlockState` to 32-bit state ids interned from state hashes, with the
  state table's six appearance ids per state (`render/block-textures`).
  Definitions stay JSON; the grammar-based loading arc comes later. *Claim: captures are identical, and every state round-trips
  through its definition ULID and named property values.* `world/brick`,
  `data/stable-identity`, `world/block-orientation`. (Detailed below.)
  - **Also in v0.2.4: disposition the open items** (owner, 2026-11-27). Each
    item gets a decision: done in v0.2.4, scheduled to a named release, or
    dropped with the reason, recorded in `RELEASES.md`. Not every item needs
    resolving here; none may be left without a disposition.
    - *The owner's review of the Brick and chunk designs as they stand*
      (`design/brick.md`, `design/chunk.md`, the v0.2.1–v0.2.3 code), and
      the questions the owner set aside while v0.2.3 was built. Brick
      review, design side done (2026-11-27): units rather than feet in
      engine docs (glossary `unit`); the chunk or terrain node, not the
      Brick, is the abstraction boundary (`invariant/brick/owner-only`);
      superclasses are blocks (`invariant/world/superclasses-unobtainable`);
      no chunk-data type: `Chunk` is the actor and owns its Brick, and a
      full-detail terrain node is the other owner
      (`invariant/chunk/content-is-cells-and-time`, with costly summaries
      handed off). **Code in v0.2.4:** `ChunkData` becomes `Chunk` (the
      single-threaded stand-in for the actor, held by `ChunkWorld`);
      raycasting within the chunk and the emitter set move from
      `ChunkWorld` into it; engine comments speak in units.
    - *Mesher layering:* the bitmask mesher is `BlockState`-specific inside
      the palette representation; whether to make it entry-agnostic
      (class and appearance tables in, abstract quads out) before coarse
      Bricks reuse it (v0.2.9). → **Dropped:** palettes hold block states at
      every level, so the entry type is always `BlockState`.
    - *Cracks at T-junctions in greedy meshes:* isolated pixels on lavapipe,
      not perceptible to the owner interactively; enlarge merged quads
      only, enlarge every opaque quad, or leave them; and the `log_columns`
      tile flips at cell edges in merged quads (cause unconfirmed).
      → **Tile flips scheduled to v0.2.5** (the packed quads rewrite that
      shader path); **cracks deferred** until they show on GPU hardware
      (owner, 2026-11-27).
    - *Merge key and materials:* material id is `type + 1`, so different
      block types never merge and the orientation/material split counts are
      near-trivial until materials are decoupled from types; and no two
      distinct shipped states share a face payload, so that merge is tested
      only by construction. → **Deferred** until materials are their own
      definitions (open in `render/block-textures`; not yet scheduled).
    - *Reverse lookup:* the scalar scan (threshold 8) against a SIMD scan;
      16-bit table slots for palettes under 2¹⁶ entries. → **Dropped:** the
      scalar scan is accepted; revisit only if a profile shows edits.
    - *Sub-byte widths (1, 2, 4 bits):* keep or drop once worldgen shows
      where chunks sit (the owner expects 8 bits). → **Scheduled to v0.3.**
    - *Per-cell reads through a virtual call* (the per-cell mesher,
      raycasts, `probe`): 13–42% on per-cell meshing since v0.2.1.
      → **Dropped:** no hot path reads per cell any more.
    - *Discretionary compaction* stays in `world/brick` for chunk actors
      (v0.2.23): confirm, or remove it from the design. → **Decided:**
      compaction is never automatic; an explicit operation stays, unused
      (`world/brick`).
    - *Measurements on the second reference machine* (v0.2.1–v0.2.3), which
      the owner did not gate those releases on. → **Dropped:** measurements
      name the machine they ran on (`engine/reference-hardware`).
    - *Profiling:* flat `perf` profiles work; call graphs need
      `kernel.perf_event_mlock_kb` raised or a frame-pointer build.
      → **Done in v0.2.4:** frame pointers in every build.
- **v0.2.5 Packed quads, translucent merging and a cutout block:** a packed
  quad format (8 bytes per quad, positions pulled in the vertex shader
  from the quad's corner and size) replacing the four-vertex quad, with
  provisional payload bits and spare room for later attributes (ambient
  occlusion, light); translucent faces merged like opaque ones, with
  connected frames and positional tiles chosen per fragment, removing the
  repurposed vertex fields; tinted glass as the stand-in for water until
  fluids; and a test cutout block (alpha-tested in the opaque pass) with
  its culling rule (it never hides a neighbour; an opaque neighbour hides
  it; whether faces between two of one kind are culled is settled here).
  Ahead of the mesh pool, so the pool, its staging blocks and the upload
  budget are sized for the format they keep. *Claim: every mesh covers
  the same faces as before at about a tenth of the bytes, and translucent
  absorbance stays exact.* `world/chunk-meshing`, `render/block-textures`.
  (Detailed below.) The appearance id's width and contents are open here: the owner is open
  to wider ids that embed payload bits directly (2026-11-27) if that
  helps the packed quad or the shader; appearance ids are derived and
  never persisted, so nothing constrains their layout but this format.
  Also (from v0.2.4's dispositions): the `log_columns` tile flips at cell
  edges in merged quads (`RELEASES.md` v0.2.3). The pinhole cracks at
  T-junctions stay deferred until they show on GPU hardware (owner,
  2026-11-27).
- **v0.2.6 The mesh pool:** device-local meshes, VMA virtual ranges, staging
  blocks, deferred release by timeline value, a record list rebuilt each
  frame. *Claim: meshes upload and retire without stalls, and the pool's
  health metric stays sound.* `sim/concurrency-model` ("Meshes and the GPU").
- **v0.2.7 Operations: logging, counters, profiling and the dev UI's
  layout:** structured logging (levels, per-subsystem channels, rate
  limiting, an in-memory ring and a file); a registry of named counters,
  gauges and log-linear histograms (`engine/telemetry`: fixed size,
  percentiles to a bounded relative error, per-frame and rolling windows),
  accumulated per thread and merged once a frame;
  fatal-check reports (check, actor, turn) into the log; headless runs dump
  the registry and the log; Tracy zones and frame marks on the CPU and the
  GPU; the F1 dashboard over the registry, and the function keys'
  fixed roles, overlay menu, launcher and favourites (`input/debug-keys`),
  with every scenario that pressed a function key converted to open views
  by name. *Claim:
  every named counter can be seen in the HUD and dumped headlessly, a fatal
  check leaves an attributable report, and with profiling compiled out the
  captures are unchanged.* `engine/telemetry`, `deps/profiling`,
  `input/debug-keys`, `render/dev-ui`.

Terrain outside in, single-threaded:
- **v0.2.8 The Cartographer seam and the toy pyramid:** generation behind
  its API in its own build target; requests of (dimension, box, level,
  ancestor node data); node-sized grids with their apron; refinement as
  one request for all eight children, sharing internal aprons; the
  recursive, cumulative composition per field; bounds and classification;
  `requiredKeys`, stub keys and the key cache; the include-visibility
  check; sampled regeneration. *Claim: a field composed level by level
  through the tree is bit-identical to the recursion evaluated from the
  root, and generation can't see the world.* `worldgen/cartographer`,
  `world/octave-pyramid`.
- **v0.2.9 TerrainNodes and the mosaic, for a fixed viewpoint:** sparse roots
  and strict halving to chunk leaves, solid and air nodes left
  unsubdivided, the desired mosaic by screen-space error with 2:1 balance,
  node meshes as cubes at the node's spacing (reusing the greedy mesher)
  with skirts, full-detail leaves owning chunks' Bricks, and chunk meshes, the v0.1
  scene's chunk as an edited leaf. *Claim: the mosaic covers the view exactly
  once, with no gaps at seams.* `world/surveyor`, `world/octave-pyramid`,
  `world/chunk`.
- **v0.2.10 Refinement as the viewpoint moves:** refine by replacing a
  parent with all eight children, coarsening the reverse, swaps applied as
  a group in one frame, hysteresis bands, every level meshed and drawn.
  *Claim: refinement and coarsening never show a gap or a half-applied
  swap.* `world/surveyor`, `engine/frame-thread`.
- **v0.2.11 The sky and joining:** the sky tier chosen from the viewpoint and
  one coarse sample (in Terra, black below the bias surface), haze scaled
  to the view radius, the joining cascade the player waits for. *Claim:
  joining always completes outside in, and the sky is right above and
  below the surface.* `world/surveyor`, `world/octave-pyramid`.

Goals, single-threaded:
- **v0.2.12 GoalSource on an inline executor:** goal actors owned and
  registered by their clients, dependency pointers, the four-state table
  with the withdrawn flag, `withdraw` with its cascade, cleanup after it, `done()` for outside
  waiters; the Surveyor allocates sibling sets and
  meshes, links them into the tree and registers them, replacing direct
  calls. *Claim: results don't depend on the order goal actors run in (a
  random-order run gives identical digests), and outside-in falls out of
  the dependencies.*
  `engine/goal-source`.
- **v0.2.13 Replanning:** registering new goal actors and withdrawing unwanted
  ones each planning turn, priorities by distance ring, bulk withdrawals
  under fast flight. *Claim: replanning never redoes completed work it keeps, and
  withdrawing every goal actor leaves nothing behind but what the tree
  keeps.* `engine/dispenser`, `world/surveyor`.
- **v0.2.14 Operations: terrain and goal inspectors, and capture
  curation:** a mosaic overlay (node outlines coloured by level, swaps
  highlighted); a GoalSource inspector (goal actors by state, the
  runnable set, abandonments by cascade); Cartographer request
  timings; tooling to curate scenarios and captures (naming, pruning,
  reviewed rebaselines). *Claim: the planner's and GoalSource's state can be
  inspected live, and a capture rebaseline is one reviewed step.*
  `engine/telemetry`, `engine/goal-source`.

GPU and distance:
- **v0.2.15 Far from origin, and render residency:** 10⁸ cells out; render
  priority classes and `visibleUntil`. *Claim: nothing degrades at
  distance.* `coords/f64-authoritative`.
- **v0.2.16 GPU-driven draws:** compute culling, indirect-count draws, the
  connectivity flood fill on the render table. *Claim: per-frame CPU cost
  is O(changes).* `render/gpu-driven`.

Actors and concurrency:
- **v0.2.17 The Dispenser with one worker in lockstep:** the baton between
  the frame thread and the worker, the actor convention types (passkeys,
  `ActorOwned`), the worker marker, function actors, and GoalSource as a
  source whose goal actors run as turns; System as the Dispenser's root
  actor and the World as its child, with halts and continuations for
  shutdown, pause and resume, reached from outside only through intentions
  to System. The two threads never overlap, so there is still no
  concurrency to get wrong. *Claim: the world is
  identical when built through turns.* `engine/dispenser`,
  `sim/concurrency-model`.
- **v0.2.18 Reclamation (global QSBR):** the grace-period counter,
  quiescent points at every turn's and frame's end, offline and online,
  `retire` cleanup actors, the ticker source, withdrawn goal actors
  freed after a grace period. *Claim: nothing is freed
  while borrowed (debug generation checks, stress tests).*
  `engine/dispenser`'s reclamation.
- **v0.2.19 Worker threads:** N workers, the eventcount, wake-one and chain
  wake, GoalSource's bookkeeping under its mutex, TSan, the header gates.
  *Claim: lockstep and N workers give identical digests.* `deps/tooling`.
  Builds on passing TLA+ models of the eventcount (no lost wakeup,
  wake-one, chain wake) and of halts (no dispatch under a halt, a
  continuation firing once and only after its children are quiet,
  bottom-up order), written as design work beforehand, since halts first
  run alongside other workers here.
- **v0.2.20 Concurrent meshing to the GPU:** the shared staging pool and the
  descriptor vector under real concurrency, lock hold-time statistics, sync
  validation. *Claim: the frame thread never waits behind work.*
- **v0.2.21 Priorities and ageing:** advertised priorities, render-class
  meshing urgency, rate caps. *Claim: fast flight keeps frame time steady,
  and terrain degrades to coarser levels rather than gaps.*
- **v0.2.22 Operations: concurrency telemetry:** per-source and per-class
  wait times, grace-period age and pending cleanups, lock hold times and
  contention (Tracy's lock instrumentation), worker utilisation. *Claim: a
  stall can be attributed from telemetry alone.* `engine/telemetry`,
  `engine/dispenser`.

Chunks as simulation objects:
- **v0.2.23 A single Vicinity, and edits:** chunk actors built from leaf
  terrain nodes' Bricks when they join and returned to terrain nodes when they leave
  (pinned until saved, here never), per-chunk queues, the nudge mask and
  mail bit, the tool-user stand-in, replies; the Vicinity joins the halt
  hierarchy under the World. *Claim: edits through actors
  play as v0.1 did, and the frame thread writes no chunk.* `sim/vicinity`,
  `world/chunk`.
- **v0.2.24 Many chunks in the Vicinity, seams and validated reads:** rays
  and edits across chunk seams, per-chunk isolated meshing, a seqlock per
  palette-form chunk with representation swap inside the write window, retry
  counters, stress-tested by reader threads. *Claim: a brute-force raycast
  agrees with the real one, a chunk's mesh doesn't depend on its
  neighbours, and readers never see a torn chunk.* `world/cubic-chunks`,
  `world/chunk-meshing`, `sim/concurrency-model`.

Soft simulation:
- **v0.2.25 World time and event streams:** world time, memoryless hashed
  event streams keyed by state, coordinates and time block, under
  time-block keys (`sim/random-ticks`), outcomes keyed by official event
  times, the chunk's last-materialized time,
  and hidden simulation state (never persisted). `coords/typed-time`,
  `sim/block-metadata`.
- **v0.2.26 The reed:** a closed form materialized by random-tick checks,
  capped at the chunk face. *Claim: checked once or often, it reaches the
  same height.* `core/closed-form-first`, `sim/random-ticks`.
- **v0.2.27 Simulation rates:** chunk turns at multi-rate divisors, a
  soft rate falloff with Euclidean nearest-point distance, the rate
  divided by 8 per doubling of distance beyond a core (`world/surveyor`), the simulation distance as a setting in blocks.
  *Claim: rates change how often, not what results.*
  `sim/multi-rate`.
- **v0.2.28 Running and frozen:** running and frozen chunks in the single
  Vicinity, full checks on load and on starting to run, no backlog.
  *Claim: a chunk that starts to run late catches up by the closed form
  alone.* `sim/residency-levels`, `sim/vicinity`.
- **v0.2.29 Cross-chunk growth:** growing-tip markers, nudges with direction
  bits, cut states read through markers, the border boost. *Claim: lost or
  duplicated nudges change nothing.* `sim/block-metadata`,
  `core/level-triggered`.
- **v0.2.30 Operations: simulation telemetry:** turn budgets per
  Vicinity, random-tick check rates, nudge and marker counters, digest
  drift. *Claim: the simulation's rates and budgets can be seen without a
  debugger.* `engine/telemetry`, `sim/vicinity`.

Close:
- **Final: review and refinement,** closing v0.2 as v0.1.9 closed v0.1.

### Out of scope (all of v0.2)
Real worldgen and the stage ladder's materials and features (v0.3); hard
simulation, conserved objects, scarce resources, harvesting and the real
inventory (v0.4); persistence; the player's body (`sim/concurrency-model`); fluids;
mobs; multiple dimensions; multiplayer; DDA ray-marching and smooth far
surfaces with microsurface shading (`world/octave-pyramid`'s ways of
drawing beyond cubes); override grids for edited chunks.

---

## v0.2.0 — Groundwork: scenario files, split main.cpp
- Status: **CLOSED 2026-11-13.** Code side done 2026-11-05; the
  concurrency model it waited on has been reviewed (`design/concurrency.md`
  and the entity docs), and is refined as later releases implement it.
  Criteria 1–5 pass, criterion 5 confirmed by the owner; also fixed
  Shift+click with items that have no secondary action
  (`input/per-hand-use`; `RELEASES.md`). SPECIFIED 2026-11-04, detailed 2026-11-05
  (after v0.1.9). Carry-over from v0.1 and preparation for the rest of v0.2
  (2026-11-04). v0.1.9 did neither item, so both are here.
- **Note (2026-11-10):** the concurrency model this release waits on is
  now `design/concurrency.md` plus the entity docs (`design/cartographer.md`,
  `design/vicinity.md`); the paragraph below predates it.
- **Purpose:** before chunks arrive, make the harness data-driven and break
  up the one large source file, so v0.2.1 can add worlds and tests without
  growing either.
- **Design work alongside the build:** the **v0.2 concurrency
  model** is written and reviewed with the owner in the design repo before
  v0.2.1's build starts. It is drafted as `sim/concurrency-model` in
  `design/concurrency.md` (2026-11-05); the requirements below were written
  before it and assumed snapshots and halos it has since dropped. It
  must say, for every piece of shared state, who writes it, who reads it, and
  how a read is made safe: chunk snapshots and copy-on-write, results
  installed by the main thread, and the simulation's halos (generation
  numbers, snapshot lifetime, when a step turn may read a neighbour's),
  per-pair outboxes and dependency counts. Meshing reads no other chunk, so it
  needs only the snapshot rules. It builds on `sim/cpu-parallelism`,
  `sim/execution-plan` and `sim/execution-invariants`. v0.2.0's build does
  not depend on it.

### Scope
- **Scenario files** (`attic/REVIEW-v0.1.md` Q7, Q8): a scenario is a JSON file
  under `scenarios/` that fully describes one run. `--scenario PATH` runs it;
  without `--capture` it opens interactively in that state, so the owner can
  start from any test setup. Field names below are indicative: the build
  agent picks the final schema, documents it in `scenarios/README.md`, and
  rejects unknown fields (as block definitions do). Numbers are typed by the
  JSON parser, which replaces Q8's unchecked `strtol`/`strtof` parsing.
  - **`world`:** for now `{ "grid": 32 }`, the v0.1 dense grid. (v0.2.1 adds
    generators and a chunk region here.)
  - **`scene`:** the placements, as data, applied in order as one edit set:
    `{ "fill": type, "from": [x,y,z], "to": [x,y,z] }` (inclusive box) and
    `{ "place": type, "at": [x,y,z], "rotation": r }` (`r` the rotation index,
    0–23), and whatever else the v0.1 scene needs, kept minimal. Missing
    block types are skipped with a warning, as today.
  - **`camera`:** `pos`, `look`.
  - **`window`:** initial `size`, and an optional `resize` `{ frame, size }`
    (was `--resize-at`).
  - **`view`:** the F2 buffer view, the overlays on at start (stats,
    wireframe, orientation, lighting, targeting, input, bounds, edit set),
    preview on or off.
  - **`render`:** anisotropy, variants on/off, glass frames on/off, OIT
    moment bias, translucent face order, OIT view, merged-quad test.
  - **`tools`:** container patch N, consume counts.
  - **`events`:** the timed input script: key presses, holds
    (`{ frame, hold, until }`), clicks (`left`/`right`), wheel steps
    (`up`/`down`, optional `shift`). An unknown key or button is an error.
  - **`frames`:** optional; by default 20 frames past the last event, or 60
    past a resize, or 60 if there are no events (v0.1.9's rule, now the
    engine's, not the script's).
  - **`checks`:** the `--check-absorbance` cases (ray, expected
    transmittance), which today hard-code scene coordinates in `main.cpp`;
    they move into the scenario that defines that scene (Q7).
- **Flags that remain:** `--scenario PATH`, `--capture PATH`,
  `--gpu-report`, `--check-blocks`, `--check-absorbance` (runs the
  scenario's `checks`), `--blocks DIR`. Every other flag (about 26 today) is
  removed and becomes a scenario field.
- **The v0.1 scene becomes data:** `scenarios/v0.1-scene.json` holds the
  scene `src/scene.cpp` builds today (the ground, logs, furnaces, lamps, the
  translucency scene, the mound, the walls). Each headless shot is a small
  scenario that names that scene (by including or referencing it: the agent's
  choice, documented) and adds camera, view and events. `src/scene.cpp`
  keeps only `lightsFromEmitters`.
- **`tools/headless_check.sh` becomes a list of scenarios** plus the
  comparisons it already does (determinism, order invariance, merged quad).
- **Split `src/main.cpp`** (1,413 lines; Q2) along the seams the review
  named: scenario loading (replacing option parsing), the dev overlays (F1–F10
  panels), the harness checks (absorbance, merged quad), input callbacks and
  capture, and the loop. `main()` only parses, loads and runs. No behavior
  change.
- **`src/renderer.cpp`** (1,004 lines) is at the threshold: split it if a
  seam is natural (for example pass setup versus per-frame recording), or
  justify keeping it whole in `RELEASES.md`.
  `tests/sandbox_tests.cpp` is split in v0.2.1 (T2), not here.
- **Deferred items from `attic/REVIEW-v0.1.md`, recorded so they aren't lost:**
  T2 split the tests by module (v0.2.1); D3 device-local meshes with a staging
  upload (v0.2.6); D10 pass timestamps only while F1 or `--gpu-report` is on,
  and P4 an in-run A/B timing mode (when performance work starts); P5 a
  per-cell tile lookup or a world-anchored noise texture (only if the variant
  hash measures); D1 tiled or clustered lighting (with more lights); D7 a
  relocatable asset path (with packaging).

### Acceptance criteria
Headless:
1. Build + `ctest` pass, Debug and Release, `-Werror`, zero warnings.
2. Every v0.1.9 headless capture is reproduced from a scenario file,
   pixel-identical except text in the F1 window and F7's "seconds ago" column
   (as in v0.1.9); the suite runs with zero validation errors;
   `--check-absorbance` passes from the scenario's `checks`.
3. Removed flags are rejected with a message naming the scenario field that
   replaced them; an unknown scenario field, key or button is an error.
4. No source file over ~1,000 lines except `tests/sandbox_tests.cpp` (v0.2.1)
   and anything justified in `RELEASES.md`; ~500 is a weak guideline
   (2026-11-04); `main()` only parses, loads and runs.

Display-required:
5. `--scenario scenarios/v0.1-scene.json` opens the v0.1 world interactively,
   and play is unchanged (owner spot-check).

---

## v0.2.1 — One chunk
- Status: **CLOSED 2026-11-26.** Criteria 1–5 pass; criterion 5 and the Clang
  captures confirmed by the owner. The meshing measurement was taken on one
  reference machine only (`RELEASES.md`); the owner did not gate the
  release on the second. Depends on v0.2.0.
- **Claim:** the world can live in a 64³ chunk without changing anything the
  game sees. **Falsifying test:** any v0.2.0 scenario capture differs.
- **Builds on:** v0.2.0; `world/cubic-chunks`, `world/brick` (the palette
  representation at one width only here), `world/chunk`,
  `core/opaque-chunk-queries`.

### Scope
- **Chunk data, a full-detail Brick** (`world/brick`): a 64³ cubic chunk addressed by
  `(dimension_id, chunk_coord)`, `dimension_id` always 0. `chunk_coord` is
  the 64-bit cell coordinate floor-divided by 64; local indices are 0–63 per
  axis. Floor division and the local index are implemented and unit-tested
  for negative coordinates now (cell −1 is chunk −1, local 63), even though
  this release uses only chunk (0,0,0).
- **One representation, the palette at one width:** 16-bit indices into a
  palette with a fixed capacity of 2¹⁶ entries (uniform, the other widths,
  growth and compaction are v0.2.2). Nothing outside the chunk code sees
  cells, indices or the palette. The Brick is generic over its palette
  entry type, instantiated here with today's `BlockState`.
- **Chunk data owns its Brick, through a pointer** (`world/brick`): edits
  go through the chunk's data, never straight to the Brick, and the Brick
  is never resized in place. This is the shape v0.2.2's copy-and-swap and
  representation pointer build on, so v0.2.2 adds behaviour without
  restructuring.
- **The reverse lookup is the chunk data's:** an edit finds its state's
  palette index by a linear scan of the palette, appending a new entry if
  the state is absent. The scan threshold and the table above it are
  v0.2.2's. Today's registry can't fill 2¹⁶ entries, so capacity is
  asserted, not handled.
- **The layout is `world/brick`'s,** chosen for bitmask meshing: cell index
  = x + 64·y + 64²·z (z up), a named constant behind the interface.
- **The chunk size is one named constant** (64), used everywhere it
  appears.
- **One chunk in the world:** the world is chunk (0,0,0), which holds cells
  0–63 and so contains the whole 32³ v0.1 world. Cells outside it read as
  air and reject edits (`editable` is false), as cells outside the grid did.
- **`DenseGrid` is removed,** replaced by the chunk (the "one world
  implementation" rule). The scenario's `world` field becomes
  `{ "generator": "empty" }`; `empty` fills the chunk with air, and the
  scenario's `scene` placements are applied as today. `world.grid` is
  rejected with a message naming its replacement.
- **The query seam is unchanged for callers** (`raycast`, `probe`,
  `editable`, `apply`, `emitters`, `generation`). Meshing becomes
  chunk-granular: `dirtyChunks()` plus `buildMesh(coord)`, using the existing
  per-cell rule over the chunk, with everything outside it treated as air.
  The renderer keeps one mesh per chunk (one, here), host-visible as today.
- **Split `tests/sandbox_tests.cpp` by module** (`attic/REVIEW-v0.1.md` T2), with
  the chunk tests in their own file.
- **Switch the compiler to Clang** (`deps/toolchain`), before the chunk
  work, in two steps so a capture change has one cause:
  1. still on GCC, set `-ffp-contract=off`; every capture must stay
     identical;
  2. switch to Clang (libstdc++ kept, `-Werror` as before, with
     `-Wthread-safety` on); compare every capture against step 1's within a
     tolerance (`tools/imgdiff.py --max`), record the differences and their
     cause in `RELEASES.md`, and, once the owner has inspected them, take
     the Clang captures as the reference.
  The chunk work then starts from the Clang reference.

### Stand-ins
- **Frame-thread `apply`** writes the chunk directly: until v0.2.23.
- **Palette entries are today's `BlockState`** (registry index plus
  orientation): until v0.2.4, state hashes.
- **A single chunk as the whole world:** until v0.2.9, when octree terrain
  surrounds it and it becomes an edited leaf.

### Out of scope
Uniform, the other palette widths and digests (v0.2.2); state hashes as
palette entries (v0.2.4); greedy meshing
(v0.2.3); device-local meshes (v0.2.6); the Cartographer seam and terrain
(v0.2.8 onwards); several chunks and seams (v0.2.24).

### Acceptance criteria
Headless:
1. Build + `ctest` pass, Debug and Release, `-Werror`, zero warnings.
2. Every v0.2.0 scenario capture is pixel-identical (same exceptions as
   v0.2.0) after the `-ffp-contract=off` step, and, after the Clang step,
   within the recorded tolerance and inspected; then identical to the
   Clang reference with the world in a chunk. `--check-absorbance` passes
   throughout.
3. Unit tests: floor division and local indices at negative, zero and
   boundary coordinates; probes and edits at every face and corner of the
   chunk; edits outside the chunk rejected; an edit to a state already in
   the palette reuses its entry, and a new state appends one.
4. `DenseGrid` no longer exists in the source; `world.grid` is rejected with
   the new field named.

Display-required:
5. The v0.1 world plays unchanged (owner spot-check).

### Measurements
- Time to mesh the full 64³ chunk with the per-cell rule (Release), on both
  reference machines (`engine/reference-hardware`): the first data point for
  per-chunk meshing cost, an open question in `sim/concurrency-model`.

## v0.2.2 — Uniform, palette widths and the digest
- Status: **CLOSED 2026-11-26.** Criteria 1–6 pass; criterion 6 confirmed
  by the owner. The palette's capacity is independent of its index width
  (`world/brick`). Measured on one reference machine only. Depends on v0.2.1.
- **Claim:** a chunk's representation and palette width are invisible:
  the same content, stored uniform or as a palette at any width, answers
  every probe and raycast, digests and meshes identically, through any
  sequence of edits. **Falsifying test:** the randomized equivalence test
  finds content or an edit sequence on which two representations differ.
- **Builds on:** v0.2.1; `world/brick` (representations, widths, growth and
  shrinking), `world/chunk-representation`, `data/content-digest`,
  `world/chunk-meshing` (rep-swap invariance), `core/opaque-chunk-queries`.
- **Prior work:** prototypes of the Brick, the digest and the reverse
  lookup, with tests and benchmarks, in `attic/prototypes/` (standalone,
  built from `attic/prototypes/CMakeLists.txt`). Its `NOTES.md` lists what is worth
  reusing (the randomised equivalence test, the digest, the reverse-lookup
  candidates, the benchmarks) and where `brick/brick.hpp` differs from the
  spec; the spec wins. `attic/prototypes/brick-research.md` has the research and
  its measurements.

### Scope
- **The representation pointer:** chunk data holds one pointer to its
  current representation object behind the chunk's interface, next to what
  doesn't change with representation (identity, the digest).
  Single-threaded here, so a change of representation builds the new object
  from the old, swaps the pointer and frees the old one directly; the
  seqlock window and retirement through a grace period arrive with validated
  reads (v0.2.24, `sim/concurrency-model`'s representation swapping).
- **Uniform:** one entry fills the Brick; probes answer it without
  per-cell storage; uniform air meshes to nothing; a uniform solid meshes
  through the per-cell rule like any other content (greedy fast paths are
  v0.2.3).
- **Palette at every storage width** (`world/brick`): 1, 2, 4, 8, 16 and
  32 bits, one instantiation per width, an access a shift and a mask.
- **Promotion and growth** (no per-entry counts):
  - a uniform chunk edited to a second state promotes to a palette at 1 bit;
  - a new state is appended within the palette's capacity, a fixed
    allocation independent of the index width (at least 16, a power of two,
    at most the width's range); when the palette is full, the chunk builds
    a new Brick with twice the capacity (the next width once the capacity
    reaches the width's range), copies into it and swaps its
    representation pointer, with no scan for dead entries; a Brick is
    never resized in place, and its palette is never a growing vector;
  - the reverse lookup, held by the chunk's data rather than the Brick
    (`world/brick`): a linear scan up to a threshold, then a table sized
    to the palette's capacity (at most about twice its entries), built
    lazily; an edit whose state has a dead entry reuses it;
  - edits only upgrade, and no edit compacts; a chunk becomes uniform or
    narrower only through a bulk fill or an explicit compaction (the test
    API here; at saves once persistence exists), which renumbers the live entries and picks
    uniform if one state is live, else the narrowest width under the
    low-water mark (`world/brick`).
- **The most compact representation after a bulk fill:** filling a chunk
  (the `empty` generator, a scenario's `scene` placements applied as one
  edit set) ends by choosing uniform if one state remains, else the palette
  at the smallest width.
- **The content digest** (`data/content-digest`), for block content:
  `digest = Σ W(c) · H(state_c) mod 2⁶¹ − 1`.
  - `W` is a function of the cell's local coordinates,
    `W(x, y, z) = 1 + (mix64(x | y << 6 | z << 12) mod (p − 1))` with
    `mix64` SplitMix64's finaliser, say: defined on coordinates rather than
    the storage layout (today the packing equals the layout index), and
    nonzero for every cell by construction (a test checks all 2¹⁸);
    `H(s)` is a defined 64-bit mix of the
    state's canonical encoding reduced mod p. Both are our own fixed
    functions, never `std::hash` (`deps/toolchain`), since digests are
    compared across processes later.
  - Lazy: computed on first request in one pass, then maintained per edit
    (`digest += W(c) · (H(b) − H(a))`); a uniform chunk's digest is
    `H(s) · K`, with `K = Σ W(c)` one constant for every chunk.
  - A change of representation or width carries the digest over unchanged.
  - Exposed as a chunk query, `digest()`.
- **Test-only APIs** (through the test access type, called out in
  `RELEASES.md`): force a representation and width, compact, inspect the
  representation, width, palette size and live entries.
- **The randomized equivalence test:** for many seeds, build contents from
  one state to as many distinct states as the registry allows (types times
  canonical orientations), store copies uniform where possible and as a
  palette at every width that holds them, apply the same random edit
  sequence to each, and after every edit compare probes at sampled cells and
  at every face and corner, a fixed set of raycasts, `digest()`, and the
  per-cell mesh (as a sorted list of quads). The edit sequences cross every
  width boundary (growth), fill a palette with dead entries so a new state
  grows it rather than reclaiming one, reuse dead entries by editing back
  to their states, and include explicit compactions down to narrower
  widths and to uniform.

### Stand-ins
- **Per-cell meshing:** until v0.2.3, greedy bitmask meshing.
- **`H(s)` over today's `BlockState` encoding** (registry index plus
  orientation, process-scoped): until v0.2.4, when it hashes the state hash,
  so digests become comparable across processes.
- **Direct representation swaps, no seqlock:** until v0.2.24, validated
  reads.

### Out of scope
Greedy and bitmask meshing (v0.2.3); state hashes (v0.2.4); seqlocks,
validated reads and retiring old representations through a grace period
(v0.2.24); digests of other subsystems;
compaction other than after bulk fills and through the test API (saves,
and so compaction at a save, come after v0.2; discretionary compaction
and proactive widening need the owner's later turns, so they arrive with
chunk actors, v0.2.23); here widening happens in the edit, the fallback
path; fixed generator palettes
(the v0.2.8 toy pyramid and v0.3 worldgen); persistence. Distinct-state counts beyond
what the registry can express are unreachable before v0.2.4, so widths
above what the registry needs are covered by forced widths, not by content.

### Acceptance criteria
Headless:
1. Build + `ctest` pass, Debug and Release, Clang, `-Werror`, zero
   warnings.
2. Every v0.2.1 capture is pixel-identical; `--check-absorbance` passes.
3. The randomized equivalence test passes for a fixed set of seeds in
   `ctest`, and a longer run (more seeds and longer edit sequences) passes
   once, recorded in `RELEASES.md`.
4. Digest unit tests: the incremental digest equals a full recomputation
   after random edits; a uniform chunk's digest equals `H(s) · K`;
   `W(c)` is nonzero for all 2¹⁸ cells;
   modular arithmetic is exact at the edges (values near p, products of
   values near p); a change of representation or width leaves `digest()`
   unchanged.
5. Representation unit tests: promotion at the first second state; growth
   at every width boundary, including a full palette with dead entries
   (it grows; nothing is reclaimed); an edit reusing a dead entry for its
   state; no edit compacting (asserted); compaction to the smallest width
   and to uniform; after the v0.1 scene's bulk fill, the chunk is a palette
   at the smallest width holding its states.

Display-required:
6. The v0.1 world plays unchanged (owner spot-check).

### Measurements
- Per-edit cost at each width (with the reverse lookup's scan and table on
  either side of its threshold), growth (repack) time at each boundary,
  and explicit compaction time (Release), on both reference machines.
- Full digest computation time for one chunk (computing W per cell; a
  precomputed 2 MiB W table is an alternative to measure against it, not
  assumed faster).
- Memory of a chunk at each width (indices, palette and reverse table).
- The v0.1 scene chunk's width, palette size and live entries.

## v0.2.3 — Greedy meshing per representation
- Status: **CLOSED 2026-11-27.** Criteria 1–11 pass; 10 and 11 confirmed by
  the owner. Cracks at T-junctions deferred by the owner and dispositioned
  in v0.2.4 (`RELEASES.md`). Measured on one reference machine only.
  Depends on v0.2.2.
- **Claim:** each representation meshes a chunk its own way, and the result
  covers exactly the faces the per-cell rule emits, each with the appearance
  the per-cell rule gives it, while merging coplanar faces of identical
  appearance into larger quads. **Falsifying test:** the randomized
  face-set equivalence test finds content, in any representation or width,
  whose greedy mesh, decomposed into unit faces, differs from the per-cell
  rule's faces in position, direction or vertex payload.
- **Builds on:** v0.2.2; `world/chunk-meshing` (the three layers, the face
  rule, isolated meshing, per-representation meshing, whole-chunk remesh,
  rep-swap invariance), `world/brick` (layout, widths, palette),
  `world/chunk-representation`, `core/opaque-chunk-queries` (meshing as a
  query; the bitmasks private to the representation),
  `world/block-orientation` (merging only where texture orientation
  matches), `render/block-textures` (variant tiles chosen per fragment),
  `render/transparency` (translucent cells emit their own boundary).

### Scope
- **`buildMesh(coord)` dispatches to the representation** behind the
  chunk's interface, as `world/chunk-meshing` requires; nothing outside the
  chunk code sees masks, indices or the palette:
  - *uniform air:* emits nothing and touches no working state;
  - *uniform opaque:* six quads of 64×64, one per side, each with the
    state's payload for that side; no masks are built;
  - *uniform translucent:* takes the palette path's translucent pass with
    its group mask set to all ones without reading cells (one quad per face,
    below), since a connected frame differs per cell and cannot be one quad
    in today's vertex format;
  - *palette, at every width:* the bitmask mesher below;
  - *the per-cell rule:* kept unchanged as the reference over any
    representation, reachable through the test access type and the
    `render.mesher` scenario field.
- **The merge key is the vertex payload, not the state.** Two unit faces
  may merge only if they share a direction and a slice and have equal
  payload: everything a `MeshVertex` carries except position (layer, turn,
  set kind, random tile count, material ID; the normal follows from the
  direction). The fragment shader reads nothing else besides position, so a
  merge is invisible by construction; any per-vertex attribute added later
  (ambient occlusion, baked light) joins the key. The key is the face's
  **appearance id** (`render/block-textures`): a process-wide 32-bit id per
  distinct payload, separate from block states, precomputed six per state
  (one per world face) when the state is interned. At the start of each
  mesh, one pass over the palette (never over cells) copies each entry's
  visibility class and six appearance ids into a small table indexed by
  palette index, with no hashing. Distinct entries whose faces look
  identical share an appearance id and merge.
- **Where the appearance rules make merging subtle** (each case is a unit
  test, below):
  - *Orientation:* a face's texture is the definition's layer for the
    local face `r⁻¹·w`, turned by the canonical transversal's turn plus the
    rotation's own (`world/block-orientation`). Two orientations of one
    block merge on a face only if both give the same layer and turn there;
    a log along x and a log along y differ in turn on their top faces, so
    they never merge, even though their states share a type. Canonical
    storage guarantees that orientations within one coset are one state, so
    no merge opportunity is lost to duplicate encodings.
  - *`random` and `matched` sets:* the tile is chosen per fragment from
    world position (`render/block-textures`), so opaque faces of these sets
    merge freely within equal payload. This rests on the shader hashing
    matched edge labels from world position alone; if matching ever
    consults the neighbour's type or orientation per face, those faces stop
    being mergeable and the key must grow.
  - *Material:* faces with identical textures but different material IDs
    never merge.
  - *Translucent faces* carry CPU-resolved tiles (`resolvePositional`) and
    connected-frame bits that differ per cell, so this release does not
    merge them (a stand-in, below).
- **The bitmask mesher** (palette representation; working set private to
  it, `core/opaque-chunk-queries`'s size rule):
  - *Masks from the layout:* with cell index = x + 64·y + 64²·z
    (`world/brick`), a row of 64 cells along x is one `uint64`. One pass
    over the cells, reading each index once and looking up its entry's
    class bits, builds the opaque mask `O[z][y]` and one mask per
    translucent group `G_g[z][y]` (group = block type, as in the per-cell
    rule), recording which groups actually occur. Palettes may hold dead
    entries (`world/brick`), so presence is taken from the cells, not the
    palette, and passes for groups that don't occur are skipped.
  - *Culling by word operations:* a face exists where a cell's class does
    not cover its neighbour's (`world/chunk-meshing`'s face rule).
    Along y and z the neighbour is the adjacent row word, so
    `O[z][y] & ~O[z][y+1]` gives +y faces and `O[z][y] & ~O[z+1][y]` +z
    faces, with no shift or transpose; along x the neighbour is the
    adjacent bit, `O & ~(O >> 1)` for +x and `O & ~(O << 1)` for −x
    ([cgerikj](https://github.com/cgerikj/binary-greedy-meshing) and
    [TanTan](https://github.com/TanTanDev/binary_greedy_mesher_demo) cull
    the same way). Opaque faces are hidden only by opaque neighbours;
    translucent faces of group g only by group g, so glass against stone
    keeps its face as the per-cell rule requires.
  - *Isolation falls out of the shifts:* a shift brings in a zero bit and
    a row past 63 is taken as zero, so cells outside the chunk read as air
    and every boundary face is emitted, with no padding. Published binary
    meshers mesh 62³ inside a padded 64³ to see one neighbour cell each way
    ([cgerikj](https://github.com/cgerikj/binary-greedy-meshing),
    [Inspirateur's port](https://github.com/Inspirateur/binary-greedy-meshing));
    isolated meshing lets the full 64³ use 64-bit words exactly. Seams that
    read neighbours would need 66 bits per row, which is the LoD design's
    concern, not this release's.
  - *Planes:* ±z faces form planes indexed by z with rows y and bits x,
    and ±y faces planes indexed by y with rows z and bits x, both read
    straight from the face masks. ±x faces need their planes turned so the
    slice is x: each z-layer of the ±x face masks (64 words over y, bits
    over x) is transposed as a 64×64 bit matrix, giving rows z and bits y
    per slice x. That is two directions × 64 transposes per mesh, by the
    recursive block-swap transpose (swap off-diagonal blocks of 32, then 16,
    down to 1; [Hacker's Delight §7-3](https://www.oreilly.com/library/view/hackers-delight/0201914654/0201914654_ch07lev1sec3.html),
    [Per Vognsen's notes](https://gist.github.com/pervognsen/983649888fa7c9075ae64e37c20f0ab3)),
    portable scalar code first.
  - *Greedy merge, one fixed rule:* for each direction in face order, each
    slice ascending, each row ascending: take the lowest set bit, extend
    the run along the bit axis while bits are set and the appearance id
    is equal (per-cell class lookups, as cgerikj compares voxel types while
    extending), then extend across following rows while the same bits are
    set and every cell of the run has that class, clearing the covered bits
    ([TanTan's `greedy_mesh_binary_plane`](https://github.com/TanTanDev/binary_greedy_mesher_demo/blob/main/src/greedy_mesher_optimized.rs)
    grows the same way over a per-key plane). The result is deterministic,
    not optimal; greedy meshing is a heuristic
    ([0fps](https://0fps.net/2012/06/30/meshing-in-a-minecraft-game/)).
    Quads are at most 64×64.
  - *Run masks of width 64:* a run covering the whole row must not be built
    as `(1 << 64) − 1`, which is undefined in C++; the mask helper handles
    it explicitly (TanTan's code uses a checked shift for the same reason).
  - *Translucent faces:* culled by the group masks as above, emitted one
    quad per face (no merge), in the per-cell rule's order (cell index,
    then face index), so translucent output is byte-identical to the
    per-cell rule and order-sensitive translucent captures cannot drift.
    Connected-frame bits are computed from masks, not by the per-cell
    rule's `frameBits`, so the two paths stay independent: a face carries
    the surface layer where its neighbour is air or a translucent group of
    higher type ID (mask `S_g`, never against opaque), and an in-plane edge
    is framed unless the neighbour across it is set in `S_g`. Positional
    tiles use the shared `faceTexture` and `resolvePositional`, which are
    appearance, not meshing.
- **`thread_local` working state:** one per-thread struct holds the masks,
  the face planes, the transpose buffer, the per-entry tables (visibility
  class and six appearance ids per palette entry) and merge scratch. It is
  allocated on a thread's first mesh, grown only when a palette has more
  entries or groups than before, never freed per call
  and never shared. No pointer into it outlives a call; `buildMesh` is not
  re-entrant on one thread (asserted). Masks are rebuilt every call, never
  cached between meshes (`world/chunk-meshing` allows either).
- **Append-only output:** the mesher appends quads to the output it is
  given and never reads back, rewrites, reorders or erases what is already
  there, so a later release can stream quads straight into a staging block
  (v0.2.6). The quad stays four `MeshVertex` and six indices, the F3 edge
  lines one outline per quad; packed quad formats are v0.2.5's.
- **`render.mesher` scenario field:** `greedy` (default), `unmerged` (the
  bitmask path with every face its own quad), or `per-cell` (the
  reference). `unmerged` separates culling bugs from merging bugs and gives
  a capture path that must match the reference pixel for pixel. Accepted
  in every build, not only tests, so meshers can be compared by eye.
- **The v0.1.8 merged-quad harness is removed:** `mergeTopFaces` edits a
  mesh in place, which append-only output no longer allows, and the stone
  floor is merged by the mesher now. The `quad_*` scenarios become
  comparisons of `per-cell` against `greedy` over the same view, with
  v0.1.8's tolerances (0 in the variant view, 1/255 lit);
  `render.mergedQuad` is rejected with a message naming `render.mesher`.
- **Test-only APIs** (through the test access type, called out in
  `RELEASES.md`): mesh one chunk with a chosen mesher; decompose a mesh
  into its sorted multiset of unit faces (cell, direction, payload); read
  a counter of working-state allocations on the current thread.
- **Named constants** for every new tuning value (none is expected beyond
  the chunk size and the mask word width).

### Stand-ins
- **Translucent faces are not merged** (one quad per face, tiles and frame
  bits resolved on the CPU): until connected frames and positional tiles
  are chosen per fragment for translucent faces as well, removing the
  repurposed vertex fields (`render/block-textures`): until v0.2.5.
- **Appearance ids are precomputed over today's registry** (every type
  times its canonical orientations, through the same accessor), since the
  state table doesn't exist yet: until v0.2.4, when the state table holds
  them (`world/brick`, `render/block-textures`). Per-mesh interning is
  never written.
- **Meshing runs on the frame thread, synchronously,** as in v0.2.1:
  until v0.2.12 (goals on the inline executor), then v0.2.17 and v0.2.20
  (workers).
- **The four-vertex `MeshVertex` quad:** until v0.2.5's packed quad
  format.

### Out of scope
Merging translucent faces (v0.2.5); ambient occlusion and any per-vertex light (they
would split merges); occlusion culling (`world/chunk-meshing` layer 3);
seams, padding and any neighbour read (`world/chunk-meshing`, the LoD
design; v0.2.24 tests that a mesh does not depend on neighbours); the cutout
class (v0.2.5 adds a test cutout block); packed quads and vertex
pulling (v0.2.5);
device-local meshes and the mesh pool (v0.2.6); sub-chunk blocks,
incremental face patching and the patch-then-re-merge hybrid; node meshes at
coarser spacing (v0.2.9 reuses this mesher); caching masks between meshes;
SIMD-specific transposes; meshing on workers and TSan (v0.2.19 onwards).

### Acceptance criteria
Headless:
1. Build + `ctest` pass, Debug and Release, Clang, `-Werror`, zero
   warnings.
2. With `render.mesher` `per-cell` and `unmerged`, every v0.2.2 capture is
   pixel-identical (same exceptions as v0.2.0). With `greedy`, every capture
   is identical except lit pixels within 1/255 along triangle diagonals, as
   v0.1.8 measured for its merged quad, and the F3 outlines in the scenarios
   with the wireframe on (`overlays`, `resize`), which now outline merged
   quads; the differences are recorded in `RELEASES.md` and, once
   inspected, the `greedy` captures become the reference.
   `--check-absorbance` passes with every mesher.
3. The randomized face-set equivalence test passes for a fixed set of seeds
   in `ctest`, and a longer run passes once, recorded in `RELEASES.md`:
   content from sparse to full, every registry type and canonical
   orientation, variant-set blocks and several translucent groups, stored in
   every representation and width that holds it; the `greedy` and
   `unmerged` meshes, decomposed into unit faces, equal the per-cell rule's
   multiset exactly (an overlapping or missing quad shows as a mismatch).
4. Rep-swap invariance: the same content meshes to byte-identical output,
   order included, uniform and as a palette at every width, before and
   after the v0.2.2 edit sequences, including palettes holding dead
   entries and the same content with its entries in a different order (a
   compacted, renumbered copy).
5. Unit tests, each for opaque and for translucent where it applies: faces
   on every side, edge and corner of the chunk emitted against outside
   air; runs at bit 0, at bit 63 and of the full width 64; rows 0 and 63;
   a 64×64 plane meshing to one quad; a checkerboard meshing to one quad per
   face; two orientations with different turns not merging, and two states
   with identical payload on a face merging; equal textures with different
   material IDs not merging; `random` and `matched` faces merging; glass
   against stone keeping its face; connected-frame bits equal to the
   per-cell rule's on every translucent face of the v0.1 scene.
6. Uniform fast paths: uniform air emits no quads and allocates no working
   state; uniform opaque emits exactly six 64×64 quads; uniform translucent
   matches its palette form byte for byte.
7. Working state: after a thread's first mesh, further meshes of chunks no
   larger in palette size (dead entries included) or translucent groups
   allocate nothing (test counter); a mesh made on a
   second `std::thread` is byte-identical to the same mesh made on the
   first.
8. Append-only: meshing into a non-empty output leaves the existing prefix
   byte-identical.
9. `render.mergedQuad` is rejected with `render.mesher` named; the recast
   `quad_*` comparisons pass at 0 (variant view) and 1/255 (lit).

Display-required:
10. The v0.1 world plays unchanged; F3 shows merged quads over the floors
    and walls (owner spot-check).
11. No cracks or sparkles at T-junctions between merged quads, close up, at
    grazing angles and at distance (owner). If cracks show, opaque quads are
    enlarged slightly in the vertex shader (cheap and reversible);
    translucent quads are never enlarged (`world/chunk-meshing`).

### Measurements
- Mesh time per chunk for `greedy`, `unmerged` and `per-cell` (Release),
  on both reference machines (`engine/reference-hardware`), for: the v0.1
  scene chunk, a uniform solid, a heightmap-like surface, a checkerboard,
  and random content with many states; broken down into mask building,
  culling, transposes, merging and emission. This is the first data point
  for `world/chunk-meshing`'s "well under a millisecond".
- Quads, vertices and bytes per mesh for the same cases, and the merge
  ratio (unit faces per quad).
- The `thread_local` working set's size, in bytes, at its high-water mark
  in the suite.
- In the v0.1 scene, how many merges were split by orientation (equal layer,
  different turn) or by material, as a check on `world/block-orientation`'s
  expectation that rotated faces reduce merging only slightly.

### Sources
- [cgerikj, binary-greedy-meshing](https://github.com/cgerikj/binary-greedy-meshing):
  occupancy mask as 64×64 `uint64` columns, face masks by shifts and
  adjacent columns, greedy merge comparing voxel types, 62³ inside a padded
  64³, 8-byte quads, T-junction mitigation; the algorithm itself in
  [`src/mesher.h`](https://raw.githubusercontent.com/cgerikj/binary-greedy-meshing/master/src/mesher.h).
- [TanTan, binary_greedy_mesher_demo](https://github.com/TanTanDev/binary_greedy_mesher_demo)
  and its [video](https://www.youtube.com/watch?v=qnGoGq7DWMc): per-axis
  column masks, culling by `col & !(col >> 1)`, per-key binary planes,
  row-then-width plane merging with a checked shift.
- [Inspirateur, binary-greedy-meshing](https://github.com/Inspirateur/binary-greedy-meshing):
  a Rust port adding transparent blocks through separate opaque and
  transparent masks, quads grouped per face direction.
- [Mikola Lysenko, "Meshing in a Minecraft game" (0fps)](https://0fps.net/2012/06/30/meshing-in-a-minecraft-game/):
  greedy meshing as a heuristic, merging only faces of the same type.
- [Henry S. Warren, Hacker's Delight, §7-3 "Transposing a Bit Matrix"](https://www.oreilly.com/library/view/hackers-delight/0201914654/0201914654_ch07lev1sec3.html)
  and [Per Vognsen, transpose notes](https://gist.github.com/pervognsen/983649888fa7c9075ae64e37c20f0ab3):
  the recursive block-swap transpose.
- [voxel.wiki, vertex pulling](https://voxel.wiki/wiki/vertex-pulling/):
  packed quads drawn without per-vertex buffers.

## v0.2.4 — Block definitions with ULIDs, and state hashes
- Status: **CLOSED 2026-11-27.** Criteria 1–10 pass; criterion 10 confirmed
  by the owner. Properties are optional or mandatory, with no designated
  defaults (owner, during the build). Depends on v0.2.3.
- **Claim:** a block state's identity is its definition's ULID and its
  named property values, and nothing the game shows changes when palettes
  hold interned state ids instead of process-local encodings. **Falsifying
  tests:** a v0.2.3 capture differs; a state fails to round-trip through
  its definition ULID and named property values, or through its state hash;
  two distinct states share a state hash; a chunk's digest differs between
  two runs.
- **Builds on:** v0.2.3; `world/brick` (palette entries, the state table,
  state ids, interning, the per-definition cap, retrieval ahead of time),
  `data/stable-identity` (ULIDs, the one identifier format, minting),
  `world/block-orientation` (orientation as a property),
  `render/block-textures` (appearance ids), `data/content-digest` (`H(s)`),
  `deps/hashing`, `world/chunk` (`Chunk` owns its Brick).
- **Also in this release:** the dispositions above, and the code they call
  for (`Chunk`, units in comments, frame pointers).

### Scope
- **The one identifier format** (`data/stable-identity`): a 128-bit `Ulid`
  value type and its 26-character Crockford base32 text, most significant
  bits first, upper case on output, case-insensitive on input; text with
  `I`, `L`, `O` or `U`, of the wrong length, or whose first character is
  above `7` is rejected. State hashes use the same type and text. The
  engine parses and prints; it mints nothing in this release.
- **A ULID in every block definition:** each JSON definition gains
  `"ulid"`. The loader rejects a definition with no ULID, a malformed one,
  or one another definition already has, naming the file. Air is a
  built-in definition with a fixed ULID in code (minted once with the
  tool), not a file.
- **The minting tool** (`tools/mint_ulid.py`, Python standard library
  only): prints new ULIDs (48-bit millisecond time from the real clock,
  80 random bits from the system, the random part incremented within a
  millisecond), and `--add FILE...` gives each definition that has no
  `"ulid"` one, inserting the field and leaving every other byte of the
  file unchanged; a file that has one is left alone. Authoring uses the
  real clock (`data/stable-identity`); simulation minting is not in this
  release.
- **Variant properties in definitions** (no value is a default, owner
  2026-11-27, `world/brick`): `"properties"` declares named properties,
  each optional (absent, or one of its values) or mandatory (always one of
  its values): `"flag"` (optional, the one value `true`), a list of value
  names (an optional enumeration), or `{"values": [...], "mandatory":
  true}`. `"initial"` names the starting value of each mandatory property
  (behaviour, not identity), and is required for each one. Names and
  values are `[a-z0-9_]+`; `orientation` is reserved, its domain coming from the
  definition's `symmetry` (the cosets of its symmetry group,
  `world/block-orientation`). No shipped definition declares a property
  yet; test fixtures do. Integer and reference properties are later.
- **The per-definition cap:** the product of a definition's domain sizes
  (an optional property counts absence as a value), orientations
  included, is at most 2¹⁰; the loader rejects a definition
  over it, naming the file and the product.
- **The canonical text and the state hash** (`world/brick`):
  `<ULID>[name=value,...]`, absent optional properties left out and
  mandatory ones always written, names sorted, the brackets left out when nothing is written. Orientation is
  written as `world/block-orientation` specifies (the images of +x and +z
  of the coset member that sorts first). The state hash is BLAKE3
  `derive_key` with the context string
  `"lanterns-reach 2026-11-27 block state v1"` over that text's UTF-8
  bytes, truncated to its first 16 bytes. The text also parses back to a
  definition and property values (unknown ULIDs and names, bad values, and
  a missing mandatory property are errors), so it is the reconstruction a
  saved state travels with.
- **BLAKE3** via `FetchContent`, pinned to a release commit, built portable
  (`deps/hashing`), checked against the library's published test vectors.
- **The state table:** process-wide, one row per interned state, indexed by
  `StateId` (32 bits, a distinct type; air is `StateId{0}`). A row holds the
  state hash, the definition, the property values (orientation as a
  rotation index), the visibility class, and six appearance ids, one per
  world face (`render/block-textures`). Appearance ids are interned over
  the face payload (layer, material, packed texture info) process-wide,
  replacing v0.2.3's table per world.
  - `intern(definition, property values) → StateId` canonicalises the
    values (orientation under the definition's symmetry), hashes, and
    returns the existing id for that hash or appends a row. Finding the
    hash with a different definition or values is fatal
    (`invariant/brick/unique-state-hash`); interning past 2³² is fatal.
  - `find(StateHash) → StateId?`; `parse(text) → StateId?` and
    `text(StateId)`; and per definition a **dense index** of its states by
    property values (mixed radix over its domains, at most 2¹⁰ entries), so
    `withOrientation(id, r)` and `with(id, name, value)` are array lookups,
    never an intern.
  - **Every state of every definition is interned when definitions load**
    (about 100 today), in a fixed order: definitions as loaded, then their
    dense index order. The intern-on-first-sight path exists and is tested,
    but nothing else uses it yet.
  - Single-threaded here: rows live in storage that never moves, so the
    concurrent version (reads by id without a lock, a short lock to intern)
    changes the locking, not the layout.
- **Palette entries become `StateId`s:** `Brick<StateId>`. Everything that
  held `BlockState{type, orientation}` holds a `StateId` and asks the state
  table for what it needs: chunks, meshers, tools, placement, preview and
  ghosts, hands and hotbars, the scenario loader (scene operations keep
  their block names and rotation indices as input, resolved when the
  scenario loads), harness checks, and tests. `BlockState` is removed.
  Code resolves the ids it needs before the loop that uses them
  (`world/brick`, retrieval ahead of time): a tool its states when it plans
  an action, the meshers one table row per palette entry; nothing per cell,
  per face or per frame interns or hashes.
- **The digest's `H(s)` becomes a function of the state hash:** the state
  hash's first 8 bytes, little-endian, reduced mod 2⁶¹ − 1. Digests then
  agree across processes and runs, so the v0.1 scene's digest and a uniform
  chunk's are pinned in tests. The reverse lookup hashes the `StateId`
  itself (in-process only).
- **`ChunkData` becomes `Chunk`** (`world/chunk`): the single-threaded
  stand-in for the chunk actor, held by `ChunkWorld`. Raycasting within the
  chunk and the emitter set move into `Chunk` from `ChunkWorld`, which keeps
  the world's duties: cells to chunks, edits outside the chunk rejected,
  the generation counter, dirtiness, the mesher setting.
- **Frame pointers in every build** (`-fno-omit-frame-pointer`), so
  `perf record --call-graph fp` gives call graphs with no system setting.
- **Engine comments speak in units** (the glossary's `unit`).

### Stand-ins
- **Every state interned at load:** until states also arrive at run time
  (chunk-blob loads with persistence, generator palettes in v0.2.8 and v0.3,
  player-created blocks later).
- **A single-threaded state table:** until states are interned off the
  frame thread (workers, v0.2.17 onwards).
- **Definitions in JSON:** until the grammar (`data/grammar`).
- **Materials per block type** (a face's material id is its type's): until
  materials are their own definitions (open in `render/block-textures`).

### Out of scope
Grammar loading; integer and reference properties and the world-level
records they name; superclass blocks and drop tables; player-created
definitions; persistence (saved chunk blobs carry states with their
reconstruction once saves exist); concurrent interning; minting outside the
tool; definitions whose appearance varies with a non-orientation property
(the fixtures' properties change identity, not looks).

### Acceptance criteria
Headless:
1. Build + `ctest` pass, Debug and Release, Clang, `-Werror`, zero
   warnings, with frame pointers.
2. Every v0.2.3 capture is pixel-identical (the `greedy` reference, the
   `quad_*` comparisons within their budget); `--check-absorbance` passes.
3. Identifier tests: `Ulid` text round-trips for 0, the maximum, and random
   values; lower case parses; `I`, `L`, `O`, `U`, wrong lengths and a first
   character above `7` are rejected; BLAKE3 matches its published vectors.
4. State-hash tests: pinned canonical texts and hashes for a fixture
   definition's states; adding an optional property to the fixture leaves
   every existing hash unchanged, and adding a mandatory one changes every
   hash; reordering a domain's values changes no hash; the initial values
   change no hash; every coset of every symmetry has a distinct
   orientation text that parses back to it; every interned state's hash is
   distinct.
5. Round trips, for every state of every shipped definition and fixture:
   `StateId` → (ULID, named property values) → `intern` → the same id;
   → state hash → `find` → the same id; → text → `parse` → the same id;
   and the dense index agrees with `intern`.
6. Loader: missing, malformed and duplicate ULIDs, an unknown or reserved
   property name, a bad domain, and a definition over 2¹⁰ states are each
   rejected naming the file (negative fixtures, as `tests/blocks_bad`).
7. Digests: the v0.1 scene's and a uniform chunk's digests equal pinned
   values; the v0.2.2 and v0.2.3 equivalence tests pass with `StateId`
   palette entries.
8. The minting tool (CTest): well-formed, distinct, monotonic ULIDs;
   `--add` gives a ULID only to definitions without one, is idempotent,
   and leaves the rest of each file byte-identical.
9. `ChunkData` and `BlockState` no longer exist in `src/`; raycasting and
   emitters are `Chunk`'s; the world tests pass unchanged in meaning.

Display-required:
10. The v0.1 world plays unchanged (owner spot-check).

### Measurements
- Intern cost per state (hash and table insert), and the time to load the
  definitions and intern every state.
- The state table's memory per state, and the number of states per
  shipped definition.
- Whether anything per cell or per frame interns (a counter, expected 0
  after load).

## v0.2.5 — Packed quads, translucent merging and a cutout block
- Status: IN PROGRESS (step a of a–e built 2026-11-27; RELEASES.md). Depends on v0.2.4.
- **Claim:** every mesh covers the same faces as before, each with the
  appearance the per-cell rule gives it, at about a tenth of the bytes;
  translucent faces merge like opaque ones and absorbance stays exact; a
  cutout block meshes and draws by its own culling rule. **Falsifying
  tests:** the face-set equivalence test finds a unit face, frame or tile
  that differs from the per-cell rule's; a mesh is not at most a tenth of
  its v0.2.4 bytes; `--check-absorbance` or an order-invariance comparison
  fails; a capture differs beyond what is listed below.
- **Builds on:** v0.2.4; `world/chunk-meshing` (the face rule, cutout,
  per-representation meshing), `render/block-textures` (appearance ids,
  variant sets chosen per fragment, connected frames),
  `render/transparency` (exact absorbance from closed boundaries; cutout
  in the G-buffer), `render/gpu-driven` and `sim/concurrency-model`'s
  "Meshes and the GPU" (vertex pulling, sized ahead of the mesh pool).
- **Also in this release** (v0.2.4's dispositions): the `log_columns`
  tile flips and the appearance id's width; inner corners of connected
  glass frames (owner, 2026-11-27). The pinhole cracks at T-junctions stay
  deferred (owner): nothing shows on GPU hardware.

### Scope
- **The packed quad: 8 bytes, pulled by the vertex shader** (provisional
  layout, a named struct with pack and unpack functions and one place that
  defines the bits; the shader mirrors it):

  | Bits | Field |
  |---|---|
  | 0–17 | x, y, z of the quad's minimum cell in the chunk, 6 bits each |
  | 18–23, 24–29 | width − 1 and height − 1 along the face's bit and row axes (1–64 each) |
  | 30–32 | face direction (`orient::Face`) |
  | 33–36 | translucent faces: which of the quad's four sides are framed (below); opaque and cutout: 0 |
  | 37–40 | translucent faces: which of the quad's four corners carry an inner-corner frame (below); otherwise 0 |
  | 41–63 | appearance id, 23 bits |

  The vertex shader takes the quad from `gl_VertexIndex / 4` and the corner
  from `gl_VertexIndex % 4`, in the unit cube's corner order scaled along
  the face's axes (so a 1×1 quad is exactly today's face); one static index
  buffer serves every mesh. Positions are integers relative to the chunk's
  origin, rebased on the CPU as today. A mesh is two arrays of `uint64_t`
  (opaque and cutout, translucent), read by the shaders as storage buffers
  (host-visible, as today; device-local is v0.2.6).
- **Appearance ids on the GPU:** the state table's appearance payloads
  (layer, material, packed texture info) are uploaded as one storage buffer
  indexed by appearance id, grown when the table grows; the shaders read the
  payload from it. **23 bits** of id (8 million payloads) are what is left
  beside the geometry and frame bits, and are enough for a long while (the
  shipped blocks have at most 252: 42 states × 6 faces); overflowing them is
  fatal, as state ids are. No bits are spare: ambient occlusion or
  per-vertex light needs about 8 bits per quad, so it brings a new layout or
  a second stream either way. The owner's offer of wider ids embedding
  payload bits is not taken: the payload (a 32-bit layer, a material and
  10 bits of texture info) doesn't fit beside the geometry in 8 bytes, and
  one lookup per vertex is cheap. Recorded so it can be revisited if the
  layout changes.
- **Every mesher emits packed quads:** the per-cell rule emits 1×1 quads in
  cell order, the bitmask mesher its merged (or, `unmerged`, 1×1) quads; the
  uniform paths likewise. `MeshVertex` and the four-vertex quad are gone
  from world meshes (the preview ghosts keep their own small vertex format:
  they are a few faces rebuilt per frame).
- **F3 outlines from the quads:** the wireframe pass pulls each quad's four
  edges from the same arrays (8 line vertices per quad), so meshes carry no
  outline list (`WorldMesh::edgeLines` goes). The `overlays` and `resize`
  captures keep drawing one outline per quad.
- **Translucent faces merge, with frames by quad side.** A connected frame
  marks each edge of a translucent face whose in-plane neighbour doesn't
  continue the surface (`render/block-textures`). Within a merged quad every
  interior edge continues, so only the quad's sides can be framed, but one
  side could mix framed and unframed edges, which no fixed-size quad can
  describe. So **translucent merging is constrained:** a quad grows only
  while each of its four sides stays uniformly framed or unframed (the
  in-plane neighbours along a side all continue the surface, or none
  does), and its four side bits then describe its frames exactly. The merge
  key is the appearance id plus whether the face shows the surface layer
  (against air, or a translucent group of higher type id; never against
  opaque, `render/block-textures`), which is the mesh's, not the state's.
  The fragment shader picks the connected tile from which of its cell's
  edges lie on a framed side (the edges at the quad's border, where the bit
  is set), so the per-cell rule's 1×1 quads carry exactly its frame bits,
  and the equivalence test compares them face by face. The constraint
  costs some quads on jagged outlines; the measurement says how many.
- **Inner corners of frames** (owner, 2026-11-27): at a concave corner of a
  glass region (the inside of an L) the corner cell's two edges both
  continue, so neither carries a band, but its diagonal neighbour doesn't,
  and the two bands meeting there stop a band's width short. A face's
  **corner bit** is set where both edges at a corner continue and the
  diagonal in-plane neighbour doesn't continue the surface; the fragment
  shader then draws that corner's square from the fully framed tile, over
  the face's edge tile, so the 16 edge tiles stay (no 47-tile set). A
  corner is one cell, so a quad carries its four corners' bits with no
  further constraint on merging; the per-cell rule computes the same bits,
  and the equivalence test compares them.
- **Positional tiles chosen per fragment for translucent faces too:**
  `random` and `matched` tiles come from world position in the shader, as
  for opaque faces; the CPU no longer resolves them for world meshes
  (`resolvePositional` stays for the ghosts).
- **The cell a fragment hashes comes from the quad, not the interpolated
  position:** the vertex shader passes the quad's minimum cell and extents
  flat, and the fragment shader takes the cell as that corner plus the
  floor of its position within the quad, clamped to the quad. This is the
  candidate fix for the `log_columns` flips (merged quads picking a
  neighbour's tile at cell edges); the captures say whether it suffices.
  **Outcome (v0.2.5b, owner, 2026-11-27):** it didn't, and was reverted for
  opaque faces: the `log_columns` pixels are texel-seam rounding inside
  merged quads, not tile flips, and the clamp made the meshers disagree at
  cell edges (`RELEASES.md`). Translucent faces keep the clamp, which their
  frames need.
- **Translucent faces merge, whatever their T-junctions do** (owner,
  2026-11-27): merging is the desired end state, so any specks of wrong
  absorbance at T-junctions are recorded, like the opaque pinholes, for
  later. No quad is enlarged: the opaque cracks stay deferred, and an
  enlarged translucent quad would count absorbance twice
  (`render/transparency`).
- **The cutout visibility class** (`world/chunk-meshing`): a definition
  marked `"cutout": true` is drawn in the G-buffer pass with an alpha test
  (texels below 0.5 discarded) and is opaque for every other purpose
  (lighting, shadows later). Its culling rule:
  - **a cutout cell never hides a neighbour:** an opaque or translucent
    face against it is emitted;
  - **an opaque neighbour hides a cutout face;**
  - **between two cutout cells of one block type the faces are culled**
    (decided here: one rule with translucent groups, fewer faces and less
    overdraw; foliage may revisit it once it exists), and between two
    different cutout types both faces are emitted.
  The bitmask mesher builds a cutout mask per cutout type present (like
  translucent groups), culls by word operations, and merges cutout quads
  like opaque ones (equal appearance id). **A test cutout block** ships: a
  grate (an iron lattice with transparent holes), its texture made by the
  texture generator from a recipe. It is not in the v0.1 scene, so the
  existing captures don't change; a new scenario shows it.
- **Tinted glass stands in for water** until fluids: no water block is added.
- **Test-only APIs:** pack and unpack a quad; decompose a mesh into unit
  faces with their payload and, for translucent faces, their frame bits.

### Stand-ins
- **Host-visible meshes:** until v0.2.6's mesh pool.
- **The quad layout is provisional:** the 23-bit appearance id and the
  frame bits are revisited when ambient occlusion or per-vertex light
  arrives.
- **Tinted glass as water:** until fluids.
- **Cutout culling between two of one type:** until foliage exists to judge
  it by.
- **Ghosts on their own vertex format:** until ghosts are drawn from packed
  quads, if ever (no release needs it).

### Out of scope
The mesh pool, device-local buffers and GPU culling (v0.2.6 onwards);
ambient occlusion and per-vertex light; the clear-glass fast path and water
(`render/transparency`); real foliage; materials decoupled from block types;
any change to the merge rule's shape beyond the translucent side constraint.

### Acceptance criteria
Headless:
1. Build + `ctest` pass, Debug and Release, Clang, `-Werror`, zero
   warnings.
2. With every mesher, every v0.2.4 capture is pixel-checked against its
   v0.2.4 reference for that mesher, except glass at inner corners, which
   now shows its corner frames; with `greedy`, translucent surfaces are
   also pixel-checked against the `per-cell` captures (merging is
   invisible). Exact differences are reported, and the visible ones (the
   corners) are inspected; then the new captures become the reference.
   `--check-absorbance` passes with every mesher, and the order-invariance
   comparisons pass.
3. The `quad_*` pairs are pixel-checked against each other (merging is
   invisible). `log_columns`, greedy against per-cell, is reported: the
   cell-from-quad change was tried and reverted (v0.2.5b in `RELEASES.md`),
   and its pixels are texel-seam rounding, not tile flips.
4. The face-set equivalence and rep-swap tests pass on packed quads, now
   including merged translucent faces: decomposed into unit faces, every
   mesher's output equals the per-cell rule's faces, payloads, frame bits
   and corner bits exactly, and every representation gives the same bytes.
5. Packed-format tests: pack and unpack round-trip at every field's
   extremes (cells 0 and 63, extents 1 and 64, every direction, side bits,
   appearance ids 0 and 2²⁴ − 1); the shader's unpacking agrees with the
   C++ (a compute or vertex readback of a pinned set of quads).
6. Translucent merging tests: a glass slab merges to one quad per side with
   its four sides framed; an L-shaped and a holed region merge into quads
   whose side bits reproduce the per-cell frame bits; the L's inner corner
   and the hole's four corners carry corner bits, and a convex corner
   doesn't; an interface between two tints is framed once; glass against
   stone is unframed and merges.
7. Cutout tests: the culling rule on every side and against each class;
   two of one cutout type cull, two types don't; cutout quads merge; a
   uniform cutout chunk meshes to its six sides; the new `cutout` scenario
   captures the grate in front of stone and glass, with zero validation
   errors.
8. Bytes: every mesh in the `--time-meshers` cases is at most a tenth of
   its v0.2.4 size (vertices, indices and outlines then; quads now).

Display-required:
9. The v0.1 world plays unchanged; glass shows merged quads in F3, with its
   frames unchanged except closed inner corners; the grate's holes show what
   is behind it (owner).
10. Translucent T-junctions: no specks of wrong tint visible close up, at
    grazing angles or at distance (owner; if any show, they are recorded
    and dispositioned, not fixed here).

### Measurements
- Bytes per mesh, v0.2.4 against v0.2.5, for the `--time-meshers` cases
  and the v0.1 scene.
- Mesh time per mesher (emission should fall with the bytes), and the
  translucent quad count: unmerged, merged without the side constraint
  (counted, not drawn) and with it.
- GPU frame time for `perf-1080p` on this machine's HD 620, v0.2.4 against
  v0.2.5 (`--gpu-report`): vertex pulling against vertex attributes.
- The appearance payload buffer's size, and the largest appearance id in use.

### Sources
- [voxel.wiki, vertex pulling](https://voxel.wiki/wiki/vertex-pulling/) and
  [cgerikj, binary-greedy-meshing](https://github.com/cgerikj/binary-greedy-meshing):
  8-byte quads pulled in the vertex shader, one index buffer for every mesh.

## Roadmap beyond v0.2 (direction, 2026-10-30)
- **Note (2026-11-10):** the v0.4 and v0.5 bullets predate
  `sim/concurrency-model` (outboxes and reserve-then-commit are superseded by
  Vicinities, conserved objects and per-Vicinity saves); they will be rewritten
  when v0.4 is planned.
- Status: **direction only**, not build specs. Ordered by
  `core/soft-over-hard` so that a rich world is generated on top of the most
  robust, correct-by-construction subsystems first.
- **v0.2** — octree terrain with progressive level of detail outside in,
  then goals and the concurrency model, then chunks as simulation objects
  and first soft simulation (roadmap above).
- **v0.3** — worldgen (also from v0.2.4's dispositions: whether the 1-,
  2- and 4-bit Brick widths earn their keep, from where real chunks'
  palettes sit; the owner expects 8 bits): density fields on stock
  FastNoise2 at full SIMD,
  keyed by blending fields across release-tier key regions (`deps/noise`,
  `worldgen/seed-keys`); first, a prototype of the blend's transition zones,
  with the fork as the fallback if they show; field-driven
  features (`worldgen/field-driven-features`), soft content gated by noise
  (rare cross-machine flips accepted, mitigated e.g. by per-world SIMD
  pinning), hard content placed in the integer domain
  (`core/worldgen-integer-domain`); replacing v0.2's toy pyramid behind the
  Cartographer seam, and completing level of detail
  (`world/progressive-lod`'s stage ladder for materials and features) on
  the octree v0.2.8–v0.2.11 established.
- **v0.4** — hard simulation, local first: the TLA+ model of the hard core is
  written and checked **before** the code (`sim/exact-core`); then two-phase
  outboxes, reserve-then-commit, the entity ledger with harvest minting, the
  real inventory, valuable blocks (`sim/valuable-blocks`).
- **v0.5 (tentative)** — global hard simulation: regions and global scope
  interleaving (`sim/regions`).
