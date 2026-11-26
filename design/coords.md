# Coordinates & precision

How absolute position is represented (authoritative) and how it is projected
into the f32 render/compute pipeline. See also `core/f32-origin-relative`.

## coords/f64-authoritative — Pure f64 authoritative coords; reproducible worldgen, non-deterministic sim

- Decision: 64-bit float is the primary native coordinate system for world
  position and physics. Rendering pipeline is 32-bit, origin-relative (see
  `core/f32-origin-relative`, `coords/typed-positions`), with integer IDs where
  appropriate. We use 64-bit floating point for precision rather than perfect
  cross-platform compatibility, accepting **the same tradeoff as Minecraft**:
  seeded worldgen is reproducible on the same machine, binary and SIMD
  level, but **different hardware architectures aren't guaranteed to produce
  identical worlds** (`worldgen/cartographer`); physics-based simulation is
  NOT required to be bit-exact across machines. Multiplayer resolves
  simulation divergence with a
  consensus/arbitration layer (server-authoritative, quorum, or similar)
  rather than lockstep bit-exactness.
- Rationale:
  - Precision is settled: f64 at our ±10⁹ world edge (one billion units each
    direction, the chosen round bound) has ~36 nm ULP (~1.2×10⁻⁷ units) —
    still ~28,000× finer than a millimetre, so vastly sufficient for physics.
    f64 does not begin to strain until ~10¹²–10¹³ units (edge ULP approaching a
    millimetre), three-plus orders of magnitude past our bound. Minecraft Java
    proves f64 is clean at ±3×10⁷, and precision degrades only with the *linear*
    edge magnitude, not world volume, so a 100× larger edge than Java's costs
    only ~7 extra ULP doublings. (Bedrock's far-distance jitter is an f32
    artifact, not f64.)
  - Reproducible worldgen does NOT require float determinism: Minecraft achieves
    it with f64 by driving all generation from an integer-seeded PRNG, so float
    results never gate a discrete outcome. We adopt the same approach — see
    `core/worldgen-integer-domain`.
  - Simulation lockstep is the only thing f64 makes genuinely hard given
    platform divergence in floating-point implementations, and we have
    explicitly opted out of needing it (consensus/arbitration instead).
  - **Why int+f32 (integer cell + f32 fraction) is rejected**, even though it is
    a popular voxel-game choice:
    1. It is a *software* extended-precision number with no native hardware
       support: every op crossing a cell boundary needs a normalize (carry
       fraction overflow into the integer cell). f64 is the *hardware*
       extended-precision equivalent and already covers the whole ±10⁹ world
       with ~23 fractional bits to spare — so int+f32 buys precision we already
       have natively.
    2. Its one real payoff — exact integer-grid physics for cross-machine
       lockstep — is something we have deliberately declined (consensus/
       arbitration, not lockstep). The scenario where the compound type earns
       its keep is off the table.
    3. **It optimizes the wrong side of the authority/derived split.** A pure-f64
       camera-relative rebase is a *stateless, one-site, output-only* projection
       at the end of the pipeline (sim → render): it owns no state, feeds nothing
       back, and a bug there is one cosmetic frame. int+f32 instead pushes the
       compound representation *into the authoritative simulation state*, so the
       normalize-on-cell-crossing burden lands on every position consumer
       (integration, collision, broadphase, constraints, spatial queries,
       fluid/CA, pathfinding), each of which can now corrupt *simulation* state,
       not a frame. It taxes the layer we most want to keep clean to subsidize a
       layer that is already cheap and disposable — the opposite of
       `core/authority-derived-split`.
    4. The relative-frame machinery is NOT a cost differentiator: a camera/
       chunk-relative frame is owed under pure f64 anyway
       (`core/f32-origin-relative`, `coords/typed-positions`), so it is a wash.
       The decision turns on (1)–(3), not on relative-coord bookkeeping.
- Consequences:
  - World position lives in f64 (authoritative). f32 is a derived,
    origin-relative view only (`core/f32-origin-relative`), enforced by typed
    positions (`coords/typed-positions`).
  - World is bounded at ±10⁹ units (one billion each direction); no precision
    concern inside that bound.
  - **World size drives no global costs.** The ±10⁹ bound is a coordinate
    *range*, not a resident data set: it sets how large a number a position may
    hold, nothing more. All real costs are **local**, scaling with what is
    actually loaded/active/visible, never with the world's extent:
    - *Worldgen* is generated on demand per visited chunk (`world/chunk-scheduling`);
      an unvisited region of the billion-unit cube costs nothing.
    - *Rendering/draw* scales with the camera-keyed resident frontier and draw
      distance (`core/f32-origin-relative` rebase is per-camera), not with how
      far away the world edge is.
    - *Storage* holds only modified/persisted chunks (`persist/diegetic-persistence`),
      not the unmodified procedural bulk.
    So enlarging the bound from 10⁷ to 10⁹ changes no cost model — it only
    widens the reachable space. The one thing that *must* scale with the bound
    is the integer chunk/voxel **addressing type** (below): it must index the
    full range without overflow. That is a type-width property, not a runtime
    cost.
  - Clean integer chunk/voxel addressing is still needed
    (`world/cubic-chunks`, `world/dimensions`); we derive it from f64 position
    via floor-to-cell with correct negative-coordinate handling (not C
    truncation-toward-zero), localized at the addressing seam. **The integer
    unit offset is `int32`.** At the ±10⁹ bound a unit/voxel coordinate (±1×10⁹)
    fits a signed 32-bit integer (±2.14×10⁹) with ~2× headroom; chunk coordinates
    (±10⁹ ÷ 64) sit far inside int32. The reasoning runs *both* ways and is the
    point: the ±10⁹ bound is deliberately arbitrary and large, so we're unlikely
    to need to change it — and the fact that it nests cleanly inside a 32-bit index
    is itself a reason to pick this particular round scale. The bound and the index
    width co-justify each other; a "nicer" round number that overflowed int32 would
    be a worse choice. (If the unit index were ever bit-packed with other data, or
    the bound grew past ~2×10⁹, int64 would be forced — but neither is planned.)

## coords/typed-positions — Typed positions enforce the f32/f64 boundary via the type system

- Decision: Model position with distinct C++ types rather than raw float
  vectors: `WorldPos` = authoritative absolute position (f64, per
  `coords/f64-authoritative`); `RenderPos` / `LocalPos` = f32 relative to a
  specific origin it carries/knows. An absolute f32 world coordinate is not a
  constructible value.
- Rationale: `core/f32-origin-relative` as a comment is silently violable; as a
  type it is unforgeable. Fits the project's lean toward making illegal states
  unrepresentable (UUIDs, immutable objects).
- Consequences: Constructing a `RenderPos` requires going through a rebase that
  takes an origin (the rebase rule in `core/f32-origin-relative`), so precision
  loss has nowhere to hide. Minor ergonomic cost: conversions are explicit at
  seams (sim→render, chunk-mesh build, GPU upload). **The rebase in
  `coords.h` is the only downcast site** in the code, for every object,
  meshes included.

## coords/axis-convention — x,y horizontal; z vertical (z-up); right-handed, +x+y = northeast

- Decision: **x and y are the horizontal (ground-plane) axes; z is vertical
  (up).** +x = east, +y = north (so +x,+y = northeast), +z = up. This is the
  mathematics / physics / CAD / Dwarf Fortress convention, NOT the game-engine
  convention (Unity/Unreal/Minecraft use y-up with the ground plane in x,z). The
  system is **right-handed** (x→y→z by the right-hand rule).
- Rationale:
  - **Simulation-first fit.** This project is closer to a simulation (DF, CAD,
    scientific/terrain work) than to a y-up rendering engine. z-up with a
    mathematical horizontal plane matches how terrain, worldgen noise fields,
    per-column operations, and physics are naturally expressed — "height" is a z
    value, a 2D horizontal query is an (x,y) query, and +x,+y = NE reads the same
    as a map.
  - A consistent, explicitly-stated handedness is what avoids cross-product /
    normal / winding-order bugs; the axis *labels* are secondary to pinning the
    handedness.
- **Performance/practical impact (the question answered):**
  1. **No arithmetic cost.** Axis assignment is a labeling convention; the FPU
     does not care which vector component is "up." A dot/cross product, an
     integration step, a distance — all cost the same regardless of which axis is
     vertical. There is no per-operation penalty for z-up.
  2. **The real impact is chunk memory layout / cache locality, and it is a
     *benefit* here.** For 64³ cubic chunks (`world/cubic-chunks`), the hot access
     patterns — per-column worldgen, meshing, horizontal pathfinding — traverse
     the horizontal plane. With z
     vertical we lay voxels out so the two horizontal axes are the inner
     (contiguous) strides and z is the outer stride:
     `index = z·(64·64) + y·64 + x`. A horizontal slab is then contiguous, so the
     common horizontal sweep is cache-friendly. This is the palette-index
     order *inside* a rep, not a public addressing contract — voxel layout is
     private to each representation (`core/opaque-chunk-queries`). (This layout follows the access
     pattern, not the axis *label* — but z-up makes "the vertical axis is the
     outer stride" the natural, legible default.)
  3. **One render-seam transform, not a running cost.** Vulkan clip space is not
     z-up right-handed, so the view/projection matrices carry a fixed basis change
     (world z-up RH → clip space). This is a constant baked into the camera
     matrices once per draw setup — not a per-vertex or per-op conversion. It is
     isolated at the sim→render seam, consistent with `core/f32-origin-relative` /
     `coords/typed-positions` already localizing the authoritative→render
     projection there.
  4. **The genuine cost is ecosystem friction, not compute.** Most engines,
     middleware, art assets, and graphics tutorials assume y-up; importing content
     or following a reference means a mental/transform remap. That is a
     human/tooling tax paid at import/integration seams, not a runtime cost — and
     it is outweighed by the simulation-domain fit above. 3D art import, if/when
     relevant, applies a fixed y-up→z-up basis swap at the asset-ingest seam.
- Consequences:
  - Right-handed z-up is a global invariant: cross products, surface normals,
    rotation sense, and triangle winding are all defined against it. State it once
    and never mix conventions internally.
  - "Height" / elevation / altitude is the z coordinate throughout (worldgen, the
    optional per-column surface-Z cache, LOD, pathfinding, UI). The cache is a "surface-Z"
    cache under this convention; the stored task-fact key
    `project.sandbox_game.surface_y_cache` keeps its old literal name only as an
    external identifier — the axis it caches is z.
  - The render seam owns exactly one world→clip basis change; the asset-ingest
    seam owns one y-up→z-up swap. No other site converts handedness.
  - **Winding-order corollary:** the basis change is a reflection (net odd axis
    flips, det < 0), which reverses triangle winding. The render pipeline's
    `frontFace` / cull state must be set to match, or geometry culls inside-out.
    A one-line pipeline-state convention, not per-vertex work — detailed in
    `engine/vulkan-abstraction`.

## coords/typed-time — World time, tick counts and wall time are distinct types

- Decision: time gets the `coords/typed-positions` treatment. **`WorldTime`**
  is the world clock and pauses with the world (decay, despawn, growth, drop
  timestamps); it is the absolute time that closed-form anchors
  (`core/closed-form-first`) and hashed point processes
  (`sim/random-ticks`) are realized on. **`TickCount`** counts world ticks T.
  Each (chunk, subsystem) also has its own step count `s ≤ T`
  (`sim/subsystem-rate-degradation`), a distinct typed count that must not
  be mixed with `TickCount`. **`WallTime`** is real elapsed
  time, used only for presentation, network timeouts and telemetry. There are
  no implicit conversions, and the sim has **no `WallTime` → `WorldTime`
  conversion at all**: pausing makes that relationship not a function.
- **`WorldTime` is real time minus pauses, not fictional game days**
 . Durations such as chunk decay (real months), ore
  renewal (6+ real months) and despawn (real days to weeks for death drops)
  are stated in `WorldTime`. The **fictional calendar** (a game day is 48 real
  minutes, `first-session/sleep-and-day`, so 1 real minute = 30 game minutes)
  is a derived view of `WorldTime` used for day and night, seasons, moon
  phases and the survival needs that are stated in game hours (sleep, food,
  water). **It is its own type, `CalendarTime`**: the in-game calendar time.
  Mixing "a week" of each is exactly the
  plausible bug these types exist to catch (a game week is under 6 real
  hours), so `CalendarTime` is derived from `WorldTime` by one explicit
  conversion (×30), never implicitly, and never converted back to a
  `WorldTime` duration without that same explicit step.
- Why: rules like `persist/presence-not-absence` and the world-time
  despawn (`inventory/slots-and-pack`) break silently if someone reaches for
  the wall clock. As types they can't.
- Extension: the same treatment applies later to other dimensioned quantities
  where mixing is a plausible bug (carry weight, rates).
