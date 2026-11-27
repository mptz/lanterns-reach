# world/surveyor (draft) — The Surveyor: the residency plan for the sky, terrain nodes and the running world

As the Cartographer hides the noise schedule that defines the world as
generated (`worldgen/cartographer`, `world/octave-pyramid`), the Surveyor
hides the logic that decides **what of the world exists where, at what
fidelity, given who occupies and perceives it**: the sky, the inert
terrain nodes around the running world, and the Vicinities and chunk
actors that run it, each with dimension-specific policy. It is the World's
component for this (a class with turn-only state inside the World actor,
not an actor of its own, `engine/world`). It plays no part in the
simulation and never looks inside chunks: it manages the chessboard, and
chunks own their content, Vicinities their simulation.

`design/concurrency.md` keeps the assumptions, invariants and connective
tissue the Surveyor shares with the rest of the model (chunk records, the
map and nudges; persistence); this file holds the Surveyor's own role,
contracts, invariants and open questions.

**Surveyor** is the C++ class name.

## Requirements
- **The Surveyor's work is parallelisable across threads.** Its work has
  dependencies (an octree level depends on the level above it, a render
  swap on its children's meshes), and that work,
  octree maintenance included, runs on many workers at once rather than
  serialised through one actor's turns. The single component in the
  World's turn described below is the starting point this requirement
  revises.
- **The Dispenser runs that work natively** (`engine/dispenser`'s
  `GoalSource`: the Surveyor registers goal actors with their dependencies,
  and GoalSource sequences their turns).

## Architectural role
- **One per world, inside the World actor:** the World is the actor
  (System → World → Vicinity and Registrar, `sim/concurrency-model`), and
  the Surveyor is the part of it that plans residency and membership.
  Within a world there is exactly one; one per dimension is unlikely, since
  Vicinities can span dimensions through portals. The client's world has
  its own, playing the same role without authority, merging or
  persistence.
- **It lives as long as its World,** created when the world loads, before
  any Vicinity exists.
- **Its planning is one component in the World's turn, for now;** the
  work it plans runs in parallel as goal actors it registers
  (`Requirements`, `engine/goal-source`). If a
  planning turn grows too long, parts split into World-owned sub-actors,
  distant terrain planning and the LoD flood fill being the natural
  first.
- **What it plans, the World offers:** the World is the Dispenser source
  for work outside every Vicinity (generation and refinement, loading
  through the Registrar, downsampling and meshing of terrain nodes,
  `world/chunk`), as goal actors in its `GoalSource` (`engine/goal-source`),
  which runs each one once its dependencies have completed. Chunks in
  a Vicinity have their turns offered by it (`sim/vicinity`).
- **Inputs:** viewpoints (from the frame thread for local players, from
  sessions for remote ones, from Observers), outcomes of the work it
  offered, the world's manifest (what has been saved,
  `sim/concurrency-model`'s persistence), and halting and resume requests
  from the World.
- **Outputs:** the residency plan's delta (below) as work items,
  membership changes and render items (chunk meshes, terrain nodes, the
  sky) with their retire descriptors (`sim/concurrency-model`, "Meshes and the GPU"); chunk records and the
  map; the simulation budget's allocation; the perception index; and the
  units replicated for distant terrain (`net/replication`).

## The residency plan
- **Desired, current, and the delta between them.** Each planning turn
  computes the **desired residency** from the viewpoints: which terrain
  boxes at which levels, which chunks running or frozen in which
  Vicinities, and which sky. It compares that with the **current
  residency** it has recorded, and the difference becomes the turn's
  output: work items, membership changes, render items to add or retire.
  This is level-triggered (`core/level-triggered`): a missed or stale
  update only means the next turn's comparison catches it, and work in
  flight is recorded so the same change isn't issued twice.
- **Three tiers:**
  1. **The sky:** whatever lies beyond the furthest terrain node (sun,
     stars, weather, far silhouettes). The dimension's survey profile
     chooses it from the viewpoint and coarse fields alone, needing nothing
     loaded: in Terra, a viewpoint deep enough below the 2D bias surface
     almost certainly has terrain above it, loaded or not, so the sky
     switches from the night sky to black.
  2. **The terrain:** inert volumes of the world as it is, outside every
     Vicinity, at varied levels of detail up to full detail
     (`world/chunk`), held as terrain nodes. An unedited node is the octave pyramid's partial sum
     (`world/octave-pyramid`), generated by the Cartographer. An edited
     chunk's node comes from its content: at full detail from its chunk
     blob, and
     at coarser levels by downsampling that content (a real mip filter, in
     a function actor) when it leaves full detail. A full-detail node is what a
     chunk actor is built from when that part of the world starts to be
     simulated, and a dissolving actor's data populates one in turn.
  3. **The running world:** Vicinities and their chunk actors, running or
     frozen, with scarce instances in the system, at full detail
     (`sim/vicinity`).
  So a chunk's life is a terrain node that gets an actor when it joins a
  Vicinity and becomes a terrain node again when it leaves.
- **`Terrain` is a sparse octree of terrain nodes,** owned by the World.
  Roots are world-aligned 2¹⁷ units
  boxes (the view radius); each node splits into eight half-size children,
  strictly, down to chunk-sized leaves at full detail
  (`world/octave-pyramid`: one bin of scale per level, the roots folding
  in the coarsest octaves). A node stores its **composed field**: its
  parent's composed field interpolated onto its own grid, plus the octaves
  of its own bin, so meshing and refinement start from the parent rather
  than walking from the root, and refining adds children without touching
  the parent (`world/octave-pyramid`). **A node is never evicted while it
  has resident descendants:** coarsening evicts children before parents,
  so the resident tree stays connected to its roots and needs plain
  pointers only, and refining again always starts from a resident parent.
  The cost is bounded: with eight children per node, the ancestors of any
  set of resident nodes add at most about a seventh more nodes
  (1/8 + 1/64 + …). **A node is a
  `TerrainNode`, held in a `SiblingSet`, a goal actor owned by the World**
  (`engine/goal-source`), long-lived and held in a slab: the Surveyor
  allocates each sibling set in the World's turn, links it into the tree
  at once, and registers it with the World's `GoalSource` with the sibling
  set holding the parent as its dependency; its turn computes its eight
  nodes' grids from the parent's through a dependency pointer, and tree
  readers use a node only once its sibling set has completed. **Shape
  refinement makes children eight at a time,** by one sibling set per parent
  (`world/octave-pyramid`), since siblings share internal aprons and
  evaluate 2D fields once per footprint; the mosaic needs all eight whenever
  a parent is refined for drawing, and where only a chain is needed (a
  server's Vicinities and their halo) the unused siblings cost about
  1.3–1.7× at the leaf level, counted by telemetry (siblings computed but
  never used). **The stages after shape are per node:** materials and
  features are each a goal actor of one node, depending on its previous
  stage (and on structure plans where they need them), never coupled to
  siblings. The feature stage stamps every tree rooted within the node's
  box plus a halo (`worldgen/tree-generation`), so it needs no neighbour;
  only huge trees continue across chunks, as soft simulation
  (`worldgen/feature-continuations`). A sibling set deep in the tree runs only
  after its ancestors, outside in by construction; siblings never wait on
  each other. The Surveyor registers a mesh goal actor for each node of the
  desired mosaic, depending on the node's sibling set, and the chunks its
  Vicinities need; replanning is registering what is new and withdrawing what
  is no longer wanted, the tree's own index deduplicating. Evicting a node
  is unlinking its sibling set from the tree, then `withdraw`, which
  abandons every dependent not yet started, then a cleanup that frees it
  after a grace period (`engine/dispenser`'s reclamation); a planning
  turn withdraws everything it evicts in one call and frees it with one
  cleanup. Nodes classified as certainly solid or
  certainly air have no children and no data, so most of the world's
  vertical extent is never subdivided; the same octree serves Terra
  (mostly flat around the height field) and cave worlds (genuinely 3D).
- **Residency centres on the mosaic.** The **mosaic** is the set of tree
  nodes currently drawn: a boundary across the octree such that every
  point in view is covered by exactly one node, fine near the viewer and
  coarse far away, each node drawn at its own level. Desired residency is a
  mosaic plus the ancestors of its nodes (a node is never evicted while it
  has resident descendants), and the delta is how the mosaic changes
  between planning turns; as a viewpoint moves, the mosaic refines ahead of
  it and coarsens behind it. **Refine by
  replacing, never by carving out:** all eight children (trivial ones
  included) are generated while the parent is still drawn, then the parent's
  render item is swapped for the children's in one change, applied by the
  frame thread in one frame once all their uploads are complete
  (`invariant/frame-thread/only-completed-uploads-drawn`), and coarsening is
  the reverse (the "chunked LOD" approach). Neighbouring nodes in the mosaic
  differ by at most one level (2:1 balance), keeping the seams skirts cover
  small.
- **Edited chunks in the tree:** ancestors covering an edited chunk keep a
  small override grid, downsampled from its content, that replaces the
  synthesised composed field inside its footprint.
- **Strictly outside in: ancestors before descendants.** No node is
  generated before its parent exists, so composing a node always finds its
  parent's composed field already generated, and every node's ancestors are
  present (`invariant/surveyor/ancestors-first`). **Joining** is a fixed
  cascade, with no priority question: first the sky, from the viewpoint and
  a single coarse sample of the fields (in Terra, the `surface` value at the
  player's column, for the night-sky or black-sky choice); then the roots
  the view sphere touches (up to 3×3 horizontally; vertically, roots above
  or below the surface band are classified all air or all stone without
  generation, so only one layer, occasionally two, is real); then far, mid
  and near nodes inwards, coarse detail reused down the tree; then the
  full-detail chunks the viewpoint's Vicinity needs, after which the player
  joins. The player waits for the whole cascade, about 1–3 seconds on a
  mid-tier machine (a loading screen); it is the only full wait.
- **During play,** refinement still goes ancestors first, ranked by the
  screen-space error it removes; movement never outruns the roots, which
  cover the whole view radius.
- **Portals and teleports are a join at the destination, with a diegetic
  cost** (`core/diegetic-cost`): the portal powers up while the far side's
  cascade is generated and opens once it is done, so the wait is part of
  the world, not a loading screen.
- **Fast movement degrades the picture, and stalls only the body.**
  Beyond a Vicinity, terrain that generation can't keep up with is drawn
  at the best level already generated and sharpens level by level
  (`world/octave-pyramid`). Only a Vicinity's chunks need full detail, so a
  mover stalls only when its Vicinity can't grow fast enough: it holds at
  the edge (`sim/vicinity`: the body waits), as
  Bedrock Realms stalls players who move faster than terrain, rather than
  letting physics run with no world around it (traditional Minecraft
  Java). Velocity-led read-ahead keeps the mosaic refined ahead of a fast
  mover so the stall is rare; the planner bounds how many swaps it issues
  per turn.
- **Survey profiles, per dimension:** which tiers exist and at what
  distances and fidelities is dimension-specific, behind one interface, as
  the Cartographer's dimension profiles are: Terra has a sky and a far
  horizon; an underdark has no sky and a short view, so its terrain nodes
  end close in; Enigma's endless cliff wants a long view along one axis.
  Viewpoints span dimensions through portals, so one viewpoint's plan can
  involve several profiles.
- **Bands, not cut-offs:** every threshold (load, level, render, simulate,
  unload) has a hysteresis band, so a viewpoint near a boundary doesn't
  thrash.

## Contracts (the actor convention)
- **The World's turn** does the compute through it, on turn-only state
  (`ActorOwned<…, WorldTurn>`): viewpoints into desired residency, the
  delta against current residency, outcomes read from its goal actors'
  states into recorded levels, unloading, the budget's allocation, Vicinity
  membership, the goal actors to register and withdraw and their priorities,
  and publishing the
  perception index.
- **The World's `GoalSource`** (`engine/goal-source`) runs the work: its
  `pullBatch` claims the best runnable goal actor, so no planning runs
  under a lock or on the caller's time; `returnRemainder` returns unfinished work
  to it.
- **Any-thread services:** `publishViewpoints` stores its input under a
  short lock and nudges the World; work it asked for reports back through
  goal actors' states and conditions, not calls (`engine/dispenser`); `find` and
  `nudge` go
  through the sharded map ("Chunk records, the map and nudges" in
  `design/concurrency.md`).
  ```cpp
  class Surveyor {                       // a component of World
  public:
      void         publishViewpoints(std::span<const Viewpoint>);  // latest wins
      void         nudge(DimensionId, ChunkCoord, NudgeBits);      // dropped if absent
      ChunkRecord* find(DimensionId, ChunkCoord);                  // to the caller's quiescent point
      void         plan(WorldTurn&, GoalSource&);                  // registers and withdraws goal actors, sets priorities
  private:
      ActorOwned<PlanningState, WorldTurn>   planning_;   // desired and current residency
      ActorOwned<MembershipState, WorldTurn> membership_;
      ShardedRecordMap      map_;
      Guarded<ViewpointSet> viewpoints_;
  };
  ```
- **Its policies are separable modules inside it,** behind the one
  facade: the survey profiles, the desired-level function, priorities,
  eviction bands and the membership rules change without changing its API.

## Chunk records
- **Record versus content:** the record (identity, version and stage, and
  an index to the chunk's actor while one exists; the actor holds its
  Vicinity pointer, scheduling state, the nudge mask, counters and the
  representation pointer) is the Surveyor's and outlives the chunk's
  transient content (`sim/concurrency-model`). Only the Surveyor writes
  the map; a record pointer stays valid until the reader's quiescent
  point.
- **Which chunks have records:** chunks held at full detail, as an actor in
  a Vicinity or as a full-detail terrain node. Coarser terrain nodes (a
  chunk or a larger volume at a coarse level) are entries in the World's
  tables, with no per-chunk record (`world/chunk`, `world/octave-pyramid`).
- **A record's life:** absent → requested (the record exists from the moment
  work is handed out, so an in-flight chunk can always be found) → loaded
  or generated to level k → … → removed.
- **"Already generated" comes from completion:** a completion records the
  level a chunk actually reached, a fact; an edited chunk is at full
  detail and is never asked for again.
- **Removal:** only when the chunk has no actor, it is saved or unedited,
  and it lies outside every viewpoint's ranges by a hysteresis band
  (`core/soft-thresholds`). A removed chunk is absent again and may be
  generated or loaded the next time it is wanted.

## Viewpoints and perception
- **A viewpoint is a body's sensorium projected for scheduling and routing**
  (`engine/audio`): position, facing, velocity (for read-ahead), and a range
  per sense (view, hearing), which sensory effects change. A player's body,
  an Observer, a telescope's distant target and a dreaming player's
  spectator view are all viewpoints.
  ```cpp
  struct Viewpoint {
      WorldPos  pos;
      glm::vec3 facing;
      glm::vec3 velocity;
      SenseRanges ranges;      // view, hearing, …
      SimulationDemand demand; // what it asks to have simulated (below)
  };
  ```
- **Published, latest wins:** `publishViewpoints` replaces the set; publishing
  often is harmless and a missed update only delays.
- **The perception index:** each turn publishes a read-only index from
  coarse regions to the viewpoints that perceive them, by sense; server-side
  event routing (sound, subtitles, objectives) reads it, and replication
  interest can later (`engine/audio`).
- **Asking never loads:** nothing but viewpoints decides residency, so
  lookups and simulation effects can't drag the frontier outward
  (`sim/residency-levels`). What is loaded is the union of every
  viewpoint's sense ranges, plus read-ahead along its velocity; hearing may
  rarely reach beyond view.

## Rendering
- **Render residency is part of the plan.** The Surveyor sets each render
  item's priority class (occupied, soon visible, near; the frame thread
  raises visible through `visibleUntil`), and when an item leaves the plan
  it pushes a retire descriptor to the frame thread ("Meshes and the GPU" in
  `design/concurrency.md`). Render items are chunk meshes near the player,
  terrain nodes further out, each drawn at its level
  (`world/octave-pyramid`), and the sky.

## Generation and loading work
- **The desired level** of each wanted chunk or box comes from its distance
  and visibility to viewpoints (`world/chunk-scheduling`'s view-aware
  priority, `world/progressive-lod`'s stages); the delta against the
  recorded level becomes goals, each with a priority (distance rings
  around each viewpoint, updated when a viewpoint enters a new chunk).
- **Generate, load or downsample:** a chunk with a manifest entry is loaded
  from its chunk blob; one without is generated by the Cartographer, in function
  actors offered by the World (`worldgen/cartographer`,
  `engine/dispenser`). An unedited distant box is generated at its level;
  an edited chunk's coarse levels are downsampled from its content when it
  leaves full detail (`world/octave-pyramid`).
- **Pull keeps reprioritization live:** `pullBatch` resolves the best goal at
  the moment a worker asks; a claimed step is in flight and isn't handed
  out twice; if viewpoints move on meanwhile, its result still lands
  (generation is monotonic) and is kept or withdrawn.
- **Meshing is a goal of its own,** whose rule needs its TerrainNode, so it
  runs as soon as the node is done, while the data is warm. **The first
  implementation meshes and draws every level,** so progressive refinement
  can be seen as it happens; meshing only what the mosaic will draw comes
  later (`Open`).

## The simulation budget
- **Demand** comes from viewpoints (every sense may ask for distant places
  to be simulated, a telescope's target, say) and from Observers, the
  in-world chunk loaders (`sim/residency-levels`), moving ones included.
- **Supply** is the operator's clamps: a total per server, optionally per
  player or per active player. Ordinarily nobody uses their whole share, so
  clamps bite only under load.
- **Allocation:** each turn the Surveyor decides which demanded places are
  simulated, and how, expressed as Vicinities' running chunks and the
  priorities and rates those Vicinities advertise; the Dispenser only
  honours priorities (`engine/dispenser`). Fairness across players, and
  across worlds on one server (a head-to-head race, `loop/speedrunning`),
  is part of this allocation.
- **Distance is Euclidean and isotropic, to the nearest point of a box:**
  a chunk's or terrain node's distance from a viewpoint is the distance
  from the viewpoint to the nearest point of its box (zero inside it),
  equal in every direction, since the world has no assumed surface. Every
  distance rule uses it: rendering's level selection
  (`world/octave-pyramid`), simulation membership and rates, and
  priorities. Vertical anisotropy (a flattened range, since players mostly
  look and move horizontally) is a later adjustment if measurement asks.
- **The simulation distance is a setting in blocks** (cells), not chunks:
  a chunk is wanted running when its box comes within that distance, so
  the setting's granularity is independent of the chunk size, and
  hysteresis bands keep a chunk at the edge from flickering in and out.
- **Simulation effort tails off with distance, not at a cliff:** closed
  forms make a process's state correct whenever it is next checked, so
  far running chunks are checked less often rather than stopped, then
  frozen beyond the simulation distance. The floors of `sim/multi-rate`
  are per process, not per chunk:
  - **a chunk has no rate of its own:** it is an idle actor, given a turn
    only when nudged (`world/chunk`) or when a background check is due, so
    a running chunk with nothing in motion costs only those checks;
  - **background checks taper freely with distance:** growth, spread,
    decay and other inherently random closed forms may run well below
    20 Hz, and further out less often still. **The default curve divides
    the rate by 8 each time the distance doubles** beyond a core of radius
    r₀ checked at rate f₀ (rate ∝ 1/r³): each shell one doubling thick
    (r₀ to 2r₀, 2r₀ to 4r₀, …) holds 8 times the volume of the one inside
    it at an eighth of the rate, so it costs about the same as the core
    (7/8 of it). Total work then grows with the logarithm of the
    simulation distance, not its cube (a 128-block core and a 1,024-block
    distance cost about 3.6 times the core, against 512 times at a
    constant rate), the same economics as the terrain octree. This
    assumes activity spread evenly; a busy distant place costs what its
    processes cost at that distance's rate. Rates are rounded to divisors
    of the base tick (`sim/multi-rate`);
  - **predictable processes in motion keep 20 Hz:** a body or projectile
    moving through a chunk (an arrow fired into it) steps at least at the
    floor wherever it is simulated, and what it touches escalates to its
    rate while the interaction lasts (`sim/multi-rate`); distance may lower
    such a process's rate only down to the floor, and at rest it costs
    nothing until woken.
  The core radius r₀, the core rate f₀ and the hysteresis bands are
  placeholders until v0.2.27 measures them.

## Vicinity membership
- **The Surveyor decides, and does what needs a halt from the World's
  turn.** Merges and cleaves are computed by the Surveyor and carried
  out in the World's halt continuation, a World turn on a worker thread,
  which calls into the halted Vicinities' state directly. The **halt
  token** is what allows it: while the World holds a quiet Vicinity's
  token, no turn of that Vicinity runs, so the token stands in for the
  Vicinity's turn passkey (`engine/dispenser`). The Vicinity provides
  `absorb(other, token)` and `cleave(spec, token)`, callable only that
  way, so its state stays `ActorOwned` and only the parent touches a
  halted child. What a **frozen** Vicinity does (saving, shutting down) is
  its own work, in its own turns.
- **Formation:** a simulated place with no Vicinity gets one; the first
  build has a single Vicinity (`sim/vicinity`'s build staging).
- **Joins without halts, shrinking by cleaves:** a chunk joins a Vicinity
  as a frozen member, built from its full-detail terrain node, and starts to
  run when the budget wants it running and all its neighbours are members;
  both happen while the chunk is idle (its idle-to-scheduled CAS), and the
  Vicinity adopts the scarce instances anchored there. Chunks behind a
  receding viewpoint are cleaved off into a frozen Vicinity, which saves
  and shuts down.
- **Merge and cleave:** when a join or a chunk starting to run would make
  two running Vicinities touch, the World halts both with one call and the
  Surveyor merges them in the continuation; when a Vicinity's viewpoints
  would leave a bridge at least k chunks wide between them, it cleaves
  the Vicinity the same way, into running parts and frozen parts that save
  and shut down (`sim/vicinity`). A chunk a frozen Vicinity holds joins a
  running one only after the frozen one has shut down.
- **Dissolution:** when a Vicinity's last demand goes, the World freezes
  it; it saves (committed by the Registrar) and shuts down, and its chunk
  actors dissolve, their data populating full-detail terrain nodes.
- **Moving Observers** are moving viewpoints, so their Vicinities grow
  ahead of them and shrink behind them like a player's.

## Halts and shutdown
- **In the World's halting turn** (pause or shutdown), the World freezes
  its Vicinities, which save in their own turns and send their captures to
  the Registrar (and, for shutdown, deregisters them), then halts the
  Registrar (`engine/world`, `engine/dispenser`).
- **Resume** releases halts top-down; the Surveyor's next turn recomputes
  desired residency from the current viewpoints.

## Checks
These mechanisms check the invariants below; each invariant's
**Enforced** line names the ones it relies on.

- **Turn-only state** (type, compile time): planning and membership state
  are `ActorOwned<…, WorldTurn>`, unreachable from `pullBatch` and the
  any-thread services; the map's insert and erase are reachable only from
  the World's turn; a halted Vicinity's `absorb` and `cleave` take its
  halt token.
- **API shape** (type, compile time): record pointers are borrowed to the
  caller's quiescent point and never stored by callers; halting and
  resuming Vicinities use the Dispenser's tokens.
- **Asserts and counters** (fatal check, and telemetry): map insert asserts
  the key is absent; removal asserts the removal conditions; a recorded
  level never decreases while a record lives; every issued action is in
  the turn's delta; counts of records, terrain nodes, in-flight items and
  Vicinities per state.
- **Measurements** (telemetry): planning turn time, delta size, ready
  steps, budget spent against clamps, and how often membership changes as
  players move.

## Invariants (draft)
- **`invariant/surveyor/one-record-per-chunk`** — There is at most one
  record per (dimension, chunk coordinate), and only the World's turn
  inserts or removes one.
  - **Enforced:** type: turn-only state; fatal check: asserts and
    counters.
- **`invariant/surveyor/records-outlive-content`** — Content is installed
  and retired only while its record exists.
  - **Enforced:** fatal check: asserts and counters.
- **`invariant/surveyor/removal-only-when-safe`** — A record is removed
  only when its chunk has no actor, is saved or unedited, and is outside
  every viewpoint's ranges by the hysteresis band.
  - **Enforced:** fatal check: asserts and counters.
- **`invariant/surveyor/asking-never-loads`** — Records and terrain nodes
  are created only by the World's turn, from viewpoints' demand.
  - **Enforced:** type: turn-only state.
- **`invariant/surveyor/pull-does-no-planning`** — `pullBatch` only pops
  the published plan.
  - **Enforced:** type: turn-only state.
- **`invariant/surveyor/no-item-twice`** — An item handed out is in flight
  until its completion or give-back, and isn't handed out again.
  - **Enforced:** fatal check: asserts and counters.
- **`invariant/surveyor/levels-from-completions`** — A record's level
  changes only by a completion, and never decreases while the record
  lives.
  - **Enforced:** fatal check: asserts and counters.
- **`invariant/surveyor/one-membership-writer`** — Only the World's turn
  changes membership: joins while the chunk is idle, merges and cleaves
  only under the halt tokens of the Vicinities involved
  (`invariant/vicinity/one-vicinity-per-chunk-actor` and
  `invariant/vicinity/no-turn-sees-change`).
  - **Enforced:** type: turn-only state; fatal check: asserts and
    counters.
- **`invariant/surveyor/within-clamps`** — The cost of what the Surveyor
  allocates to simulation stays within the operator's clamps (soft,
  measured rather than enforced exactly).
  - **Enforced:** telemetry or watchdog: measurements.
- **`invariant/surveyor/actions-from-delta`** — Every work item,
  membership change and render change the Surveyor issues is part of the
  difference between desired and current residency in that turn.
  - **Enforced:** fatal check: asserts and counters.
- **`invariant/surveyor/ancestors-first`** — No terrain-node node is
  generated or installed unless its parent exists, and the mosaic covers the
  view with no overlaps, each node's neighbours in the mosaic within one level
  of it (2:1).
  - **Enforced:** fatal check: asserts and counters.

## Open
- **Its invariants above** are a first draft for discussion.
- **One priority across all work during play:** refining ahead,
  coarsening behind, full-detail chunks at a Vicinity's edge and membership
  changes, ranked by screen-space error removed per unit of work plus hard
  needs (a Vicinity's chunks, a body waiting at the edge); joining is a
  fixed cascade and needs none of this.
- **Which levels are meshed, after the first implementation:** meshing
  only what the mosaic will draw saves work and the per-frame upload budget
  when generation is ahead; when it is behind, the best level ready needs
  a mesh in time to be drawn. The likely shape is a **delayed fallback
  mesh step** for each intermediate level, runnable only after a delay
  (perhaps a frame count since the request), which abandons itself if
  finer meshes covering its box already exist; this needs a Dispenser
  feature for delayed steps (`engine/dispenser`). A prediction from
  measured throughput is the alternative. Multi-level swaps (a node replaced by a complete
  covering of its descendants several levels down, under 2:1 balance)
  would let the mosaic skip unmeshed levels.
- **Managing the octree:** enforcing 2:1 balance (one refinement can force
  neighbours to refine), the latency of replace-on-refine needing all
  eight children (prefetching one level ahead), hysteresis bands per
  level, and the memory budget and eviction order for terrain nodes,
  full detail included.
- **Fast-flight streaming:** velocity-led read-ahead for the tree as well
  as for chunks, the swap rate, and the cleave halt rate a fast mover
  causes.
- **Multiplayer:** the server's tree as the union of every viewpoint's
  needs and each client's own Surveyor and mosaic; what the server sends per
  client (keys, or composed fields for edited areas and clients without keys);
  tracking which tier keys each client holds; whether clients generate at
  all (`net/replication`).
- **Edited chunks in the tree:** when override grids are built and kept
  current as a chunk is edited again, and their cost over large edited
  areas (a big build, terraforming).
- **Survey profiles:** what each dimension's profile holds (tiers,
  distances, the sky's rules), and how a viewpoint's plan combines
  profiles across a portal.
- **How membership is computed:** touching as the merge trigger, the
  cleave bridge k, how often the planning turn runs as viewpoints move,
  and the halt rate a fast mover (a flying player, an Observer's train)
  causes.
- **How the budget becomes priorities and rates,** and how fairness across
  players and across worlds is expressed.
- **The perception index's granularity,** and whether hearing is
  omnidirectional with occlusion or also attenuated by facing.
- **What a level is worth at a distance** (many coarse boxes against a few
  fine chunks), and one level per item or straight to the desired level.
- **Timed nudges** (`wakeAt`) for growth and decay that should recheck
  without a neighbour's prompt.
