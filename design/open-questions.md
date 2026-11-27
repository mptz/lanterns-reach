# Open questions & resolved items

Slugs here are tracking IDs for unresolved threads; they graduate into a topic
file once decided.

## Open

- **persist/edit-atomicity** — DEEP DIVE REQUIRED before implementing storage.
  **Mostly dissolved 2026-11-05** by the committed direction in
  `sim/concurrency-model`. **Resolved:** per-Vicinity saves, immutable
  chunk blobs of plentiful content, and one short SQLite transaction per
  save committing the chunk manifest, the ULID-keyed scarce table
  (versioned rows, applied only if newer) and the world records (portal
  rows included), so SQLite is the only commit point
  (`sim/concurrency-model`).
  What remains is performance: which chunk-blob store, and the autosave
  interval. (The original
  problem, kept for history: the ledger in SQLite and chunk blobs in a bulk
  store were separate stores, so a place/break that wrote both could crash
  half-done, duplicating or losing the item.)
  - **Earlier lean, superseded 2026-11-05: an edit journal** (chunk blobs as
    checkpoints carrying the journal sequence they include; replay newer rows;
    group-commit per tick). Rejected in favour of snapshot epochs: no durable
    write per transfer (Michael).
  - **Alternative: one engine for both.** Worth measuring first: SQLite's own
    benchmarks report small blobs (roughly ≤100 KB) reading faster from the DB
    than from separate files, so storing chunk blobs in SQLite may be
    competitive at our chunk sizes — which would make atomicity free.
  - **Accounting stays sink/mint** (decided 2026-10-29): placing a block is a
    sink, breaking one a mint. A placed block (even diamond) is shape, so decay
    erasing it is an intended sink — stabilize what you value.
  - **Block metadata — where it lives (lean, to settle in the deep dive):**
    - *Small state* (orientation, enums): in the palette block
      state, inside the chunk (`world/block-orientation`). (Growth
      stage is the block state, and its timing a closed form of the state,
      the coordinates and world time, with no per-block metadata,
      `sim/block-metadata`.)
    - *Local, non-conserved data* (sign text — by content hash per
      `data/authored-content` — machine config): a per-chunk block-data section
      in the chunk blob, journaled with the edit; it goes with the block.
    - *Anything that references a dimension or ledger entities* (placed
      containers, soul-bearing blocks): relational, in the global store (the
      portal table, `data/dimension-gc`), because GC and conservation must
      query it.
    - On **break**, each block type says which metadata travels into the minted
      item (a chest keeps its dimension; a sign might keep its text). On **decay**,
      local metadata vanishes with the block; relational metadata is removed in
      the same transaction (a container's dimension then becomes unreachable and
      is collected, contents sunk).
    - Open: **worldgen-placed containers** (dungeon loot). Generation is pure, so
      a generated chest can't pre-own a dimension; likely its dimension and loot
      are minted lazily on first access (an extraction mint).
  (`data/entity-ledger`, `data/dimension-gc`, `deps/save-store`,
  `persist/diegetic-persistence`)

- **data/immutability-granularity** — confirm object/entity-level immutability
  with mutable/COW voxel storage; define exactly where the boundary sits.
  (`data/immutability`)
- **sim/concurrency-model** — (opened 2026-10-29; **v0.2 draft 2026-11-05 in
  `design/concurrency.md`, under review**) how SIMULATION runs on many
  threads, so v0.2's parallel generation isn't followed by a single-threaded
  sim. Chunk-local cellular work (fluids flowing up to a face before it must
  synchronize) is the natural parallel case; entity physics, players and other
  non-chunk-local elements are not, and distributed simulation is not a solved
  problem.
  - **Direction (2026-11-05): `sim/concurrency-model`** — chunks as idle
    actors; Vicinities as merge-and-split transaction domains; conserved
    movers owned by the Vicinity, conserved messages and assemblies for
    cross-chunk conserved work; published
    state for soft cross-chunk effects; snapshot epochs. The 2026-10-30
    step/reconcile plan (`sim/execution-plan`) was removed. Still open:
    validating the direction against the use cases
    (`sim/execution-use-cases`).
  - **Working framing, not yet a tenet — spill.** Coupling belongs to each
    EFFECT, not each subsystem: fluids and physics are mostly chunk-local with
    occasional spill (fluid crossing a face; most blocks drop as entities
    within their own chunk). An effect has a **home scope** (where it normally
    runs) and a **maximum spill** (how far it can reach); it runs at home and
    escalates only when it actually spills. For a radius-1 effect ~6/64 ≈ 9% of
    a 64³ chunk's cells lie in the boundary layer (less in practice).
  - **Scopes certain to exist:** **per chunk**, **neighbour** (radius-1 spill)
    and **global** (serial or transactional, kept small: dimension GC, the
    portal table, world and player records). (Revised 2026-11-05: the ledger
    is no longer global — chunks own entities and Vicinities are the
    cross-chunk scope, `sim/concurrency-model`; inventories travel with their
    body.) Anything between neighbour and global is `sim/regions`.
  - **Colouring (superseded 2026-10-30).** The colouring facts and the
    hierarchical-colouring candidate are recorded in
    `attic/sim-execution-alternatives.md`.
  - **Speed of information: at most one chunk per tick — DECIDED
    (2026-10-29), now `sim/speed-of-information`.** Bounded effects — fluid, falling at terminal velocity, most
    projectiles, field and effect propagation — reach at most 64 cells past
    their home per tick, so every effect targets one of the 26 neighbours;
    only true exceptions (bullet-speed objects, instant effects) go beyond. In world units the limit is 64 cells × tick
    rate: at 20 Hz, 1,280 units/s ≈ 390 m/s, just above the speed of sound
    (terminal velocity ≈ 9
    cells/tick, arrows 8–16, sound ≈ 56, a rifle bullet ≈ 150). It limits how
    far an effect reaches in a tick, not a requirement to sub-step it 64
    times: an effect moves as far as its physics says in one computation
    inside its owner's step. **Open: the tick rate** (`sim/tick-rate`).
  - **Speed limits make locality (design lever).** In cellular sims
    information moves ≤ 1 cell per step (the CFL condition), so radius-1 spill
    always suffices; the hierarchy is only needed for effects with NO speed
    limit (instant explosions, instant signals, whole contraptions or
    structures moving at once). Giving such effects a finite propagation speed
    — a shockwave expanding over ticks, signals with delay — is a game-design
    choice that buys locality, in the spirit of `core/diegetic-cost`.
  - Relates to `core/cross-chunk-mechanism`, `sim/parallelism-model`,
    `core/parallel-by-default`, `core/fair-share`.
- **sim/valuable-blocks** — (opened 2026-10-30, Michael; **direction 2026-10-31,
  `geology/ore-sites`:** (1) placement — soft generation places sites, and the
  authority resolves and mints a valuable ore in place on reveal; (2) decay —
  regrowth is a deliberate feature, ore is renewable on a 6+-month timescale;
  in-place ore is a ledger entity, sunk on decay. Remaining: what "revealed"
  means and the exact timescale) the hard-simulation /
  ledger treatment of **blocks valuable enough to track on an individual
  basis** (valuable ores and similar) — bounding the problem to that class
  keeps the scope contained (`core/soft-over-hard`). Two issues: (1)
  **placement** must be deterministic — integer-domain hashed sites, never a
  SIMD-noise gate whose rare cross-machine flip could add or remove one
  (`core/worldgen-integer-domain`'s exception covers soft content only); (2)
  **decay regeneration**: `persist/diegetic-persistence` lets "anything
  diggable regrow" when an unstabilized chunk reverts to baseline, which for
  valuable blocks is a farming loop (mine, walk away, let it revert, mine
  again). Candidates: revert valuable blocks to a depleted state, gate their
  regrowth, or accept it as costly enough to self-limit. Also: are they
  individually identified (ledger rows) while still in place, or only at the
  mint?
- **world/inhabitants** — (opened 2026-11-02; **post-v1.0**, Michael 2026-11-03:
  not to be decided for a long time) whether the world has
  "villagers" or other ordinary people. Low fantasy wants ordinary people
  (`core/low-fantasy`; folk magic "that villagers use too",
  `inventory/pouch-progression`), but sapient AI won't be realistic, so the
  choice may be forced between **lonely** (no people) and **goofy**
  (unconvincing people) (Michael). Related: `worldgen/territory`.
  **Resolved (direction):** animal peoples after Dwarf Fortress, in tribes
  and camps, as traders and a threat with faction reputation
  (`design/peoples.md`); stylized behaviour suits them where it wouldn't
  suit humans, which avoids the goofy horn.
- **magic/unification** — (opened 2026-11-02; deep dive later; refined
  incrementally, kept open as a reminder, Michael 2026-11-03) how
  folk magic, Aether, resonance, the `liminality` field, the stabilization
  field, Enigma's magic and reality bombs relate. E.g. is stabilization the
  opposite of liminality, and does a lantern work better or worse in a
  liminal place? (`worldgen/dimension-cosmology`,
  `persist/diegetic-persistence`)
- **first-session/open** — (opened 2026-11-01) sleep details (duration, a
  first-night bedroll), drink and water containers, the lantern housing, and
  how much food a first session includes (`design/first-session.md`).
  Resolved the same day: the tool is the knife; the axe is not needed.
- **input/no-player-coordinates** — (opened 2026-10-30) player-facing UI shows
  no world coordinates (dev overlays only), to keep soft-simulation seams out
  of the ordinary-player meta (`core/soft-over-hard`). Open: what replaces
  coordinates for navigation (landmarks, maps, compass) and whether any
  in-world, diegetic positioning item exists. Candidate (2026-11-01,
  `worldgen/dimension-cosmology`): maps the player draws, with blank
  *terra incognita* edges. Partly answered by `tools/compasses` (draft):
  instruments reading fields and places, with no coordinates.
- **sim/tick-rate** — (opened 2026-10-29) the authoritative simulation tick
  rate. It sets the speed of information (64 cells × tick rate per second,
  `sim/concurrency-model`), the per-tick budget for `core/fair-share`, and the
  resolution of the world clock (`coords/typed-time`). Minecraft's 20 Hz is
  the reference point; the client renders and predicts independently of it
  (`net/client-server-seam`). **Narrowed 2026-11-05 (Michael):** several
  rates, divisors of one base tick aligned with the frame rate
  (`sim/multi-rate`); the exact rates remain open.
- **sim/regions** — (opened 2026-10-29) the scope between neighbour and
  global: how a group of chunks that interact more than radius-1 is run as one
  unit. Many possible implementations: PaperMC Folia's regionized ticking
  (nearby loaded chunks grouped into regions, each ticked on its own thread,
  merging and splitting with activity); physics-engine islands (connected
  groups of interacting bodies solved independently; PhysX, Box2D, Jolt); the
  coarse levels of hierarchical colouring (now a fallback only,
  `attic/sim-execution-alternatives.md`); ad-hoc regions sized to one large effect.
  Open: whether regions are static levels or dynamic groupings, how they
  merge and split, and how `core/fair-share` schedules them; global/region
  interleaving is part of `sim/exact-core`. **Exploration 2026-11-05
  (Michael):** disjoint, merge-only (never hand off), split-with-hysteresis
  **Vicinities** sized like simulation distance, each an in-memory
  transaction domain for cross-chunk conserved work, freezing at their edge
  (`sim/vicinity`).
- **world/chunk-scheduling-tuning** — the priority weighting (distance vs. frustum
  vs. predicted motion), the soft eviction hysteresis band
  (`core/soft-thresholds`), and an efficient representation of the (box × level)
  generation frontier. (`world/chunk-scheduling`, `world/progressive-lod`)
- **worldgen/surface-free-placement** — design structure gen to anchor on
  local density-field interface queries (nearest solid/air, flat patch, cavern
  floor, cliff) rather than a surface height, so one primitive works in every
  dimension. (`worldgen/dimension-profiles`)
- **sim/3d-pathing** — the concrete 3D search shape used by the mob PLANNER
  (`sim/mob-navigation` decided the planning/actuation split and the
  heuristic-not-optimal stance; this is the search *algorithm* under the planner):
  fully 3D navigation (walkable = solid-with-air-above, discovered locally) that
  needs no surface-Z, handling caves/overhangs/floating terrain uniformly, with
  surface-Z only ever a seeding hint. Must stay a *heuristic/local* search over
  the O(n³) voxel volume (no global optimal solve), reading coarse distant
  representations at range. (`sim/mob-navigation`, `worldgen/dimension-profiles`,
  `core/chunk-locality`, `world/progressive-lod`)
- **sim/mob-navigation-tuning** — the heuristic family (greedy + limited
  lookahead vs. flow-field crowds vs. local steering), the per-soul
  capability-tier vocabulary (dumb animal → clever predator → robotic
  pathfinder), and the plan-handoff contract: how a stale plan is
  invalidated/re-requested when actuation discovers the world changed under it.
  (`sim/mob-navigation`)
- **sim/mob-perception-model** — the open sub-questions under `sim/mob-perception`
  (the authoritative-vs-presentation sensor fork is RESOLVED — perception is
  uniform rendering, no privileged introspection; see `sim/mob-perception` and
  `core/authority-derived-split`): the percept/belief **decay curve** (how
  last-known-position confidence ages) and the alert/investigate/search/idle state
  ladder it drives; and the concrete **concurrency-staleness budget** (how many
  ticks of planner snapshot-lag stays "sub-perceptible" given the other
  imperfections). (`sim/mob-perception`, `core/soft-thresholds`)
- **sim/mob-decision-tuning** — the open sub-questions under `sim/mob-decision`
  (utility/need-based layer): the **drive vocabulary** (the actual list of drives,
  which are universal vs. body-specific) — tuning-heavy, likely perpetual; and the
  **commitment-bias magnitude** relative to drive-curve steepness, which sets where
  "determination" ends and "stuck/robotic" begins (too low = dithering, too high =
  fails to break out for mid-urgency drives, though injury-avoidance's steep curve
  always clears it). (`sim/mob-decision`, `core/soft-thresholds`, `mobs/spawning`)
- **data/authoring-vocabulary** — the scope dial of `data/grammar`'s
  capability set for player authoring: too narrow = crafting with extra steps,
  too open = a scripting language with a fiction skin. Perpetual tuning.
  (`core/diegetic-authoring`)
- **data/soul-body-alignment** — how the soul↔body "sufficiently aligned"
  constraint is expressed and checked (bounded constraint-satisfaction in the
  grammar). Applies to both canned and mechanical souls. (`core/diegetic-authoring`,
  `sim/logic-machines`)
- **sim/logic-eval** — the declining-CPU-budget / scheduler model for mechanical
  souls; the canned I/O module (capability) catalog; live-per-tick vs. further
  compiled evaluation of the topology-normalized machine. (`sim/logic-machines`)
  Partly answered 2026-10-30: unsealed logic runs as relaxation + checks
  (`sim/random-ticks` catalog); sealed machines are stepped and, under load,
  slow as a whole (`sim/subsystem-rate-degradation`).
- **sim/seasonal-reality-level** — per-phenomenon choice: purely perceptible
  (cosmetic frost) vs. present-but-imperceptible-overwritable (winter leaves) vs.
  seasonally real/harvestable (gatherable snow). The interaction differs per
  phenomenon; conservation (`data/entity-ledger`) enters only for the harvestable
  case, and only at harvest (an ordinary extraction mint) — static seasonal
  blocks are world shape, not entities. (`sim/seasons`)
- **sim/embodiment-boundaries** — player-soul/body edges: what happens to a body
  when its player soul detaches (canned soul takes over? goes inert? ledger-
  tracked?); which intentions are universal vs. body-specific; how sensorium
  fidelity is negotiated per body; and whether the soul↔body alignment constraint
  (`data/soul-body-alignment`) gates which bodies a human may drive.
  (`sim/player-embodiment`, `sim/logic-machines`)
- **sim/designation-authority** — whether a dream designation (`sim/dreaming-designation`)
  is strictly a queued PLAN the waking body must physically execute (leaning yes:
  subject to reachability/resources/pathing, via the `sim/player-embodiment`
  intention-vs-actuation seam) or has any direct world effect; how designations
  queue, expire, and execute on waking; the no-acting-while-asleep multiplayer
  guarantee. Designations are coordination state, so `core/soft-state`
  applies: a TTL plus refresh rather than a queue. (`sim/dreaming-designation`,
  `core/diegetic-cost`)
- **sim/dream-leash** — the diegetic bound on dream range/clarity and spectator
  powers so dreaming is not a free scry/X-ray tool; leaning: bounded by the
  stabilization field with a soft falloff (dream the reality you anchored, not
  unexplored world). (`sim/dreaming-designation`, `persist/diegetic-persistence`,
  `core/soft-thresholds`)
- **progression/health-model** — is there a health/injury/recovery axis (damage,
  healing, disease/status), and how does it interact with the soft survival
  thresholds? Genuine gap: most survival-sims have this; the design has no entry.
  (`pressure/basic-survival`, `progression`)
- **progression/knowledge-model** — is progression gated by KNOWING (learn/
  discover recipes) or by HAVING (tools/materials)? Foundational genre fork not
  yet made (Minecraft ≈ having; many sims ≈ knowing); shapes the whole dependency
  graph. (`loop/dependency-graph`, `progression`)
- **progression/continuous-sim-resource** — DIRECTION SET (2026-10-22;
  rescoped 2026-10-30, see below): an
  unattended chunk produces (auto-farms) only if it is kept resident-and-
  TICKING, and that is bought with an **in-game resource** — no absent-world
  simulation and no catch-up replay. `core/diegetic-cost` projecting the real
  engine cost of a live chunk into the fiction, as `persist/diegetic-persistence`
  does for storage. Still open: the resource's form and cost curve; whether it is
  the SAME resource as reality stabilization or an adjacent tier (stabilization =
  hold SHAPE, this = keep TICKING — sibling axes); how a group pools it
  (cooperative scale); interaction with the present-player mob invariant (does
  paid-live space spawn mobs with nobody there? lean: no combat sim without a
  player, production only). (`progression`, `sim/logic-machines`,
  `core/diegetic-cost`, `persist/diegetic-persistence`)
  **Rescoped 2026-10-30 (Michael):** crop and tree growth is closed-form in
  ALL farms, everywhere, including unloaded chunks (Vintage-Story-like;
  `core/closed-form-first`, `sim/random-ticks`), and is NOT gated by this
  resource or by stabilization. Decay interacts emergently, with no
  cross-system rule: decay overwrites blocks toward baseline, and an
  overwritten crop is simply gone, its stage having been its block state
  (`sim/block-metadata`); players
  who care about a farm stabilize it. What residency/ticking buys is only
  STEPPED production: harvest actuators, item transfers, mob/entity farms,
  sealed machines (`sim/residency-levels`).
- **worldgen/distance-ratio-field** — make Terra⟷dimension travel-distance
  ratio a LOCAL noise field (a regional `distance-ratio`/`compression` scalar)
  rather than a fixed per-dimension constant (cf. Minecraft 8:1 Nether), so some
  regions/corridors compress travel more than others — a discoverable transport
  resource. Fits `worldgen/noise-fields` (coarse regional field) and
  `core/soft-thresholds` (no ratio cliffs). Hard part: a position-dependent ratio
  makes the cross-dimension coordinate MAP position-dependent — it must stay
  coherent and reversible (a round trip lands you where you started), unlike a
  clean linear fixed-ratio map. (`world/dimensions`, `worldgen/noise-fields`,
  `progression`) **Mechanism resolved 2026-11-08:** `worldgen/dimension-warp`
  (symmetric coupling layers with smoothed zigzags: local, fold-free,
  exactly invertible, modes around 12:1 and 1:12, 3D, through the Aether as hub).
- **net/adversarial-content** — (opened 2026-11-05, Michael; not urgent) players
  deliberately building content that exploits storage or processing limits to
  crash a server or kick other players, as on Minecraft's 2b2t ("chunk bans":
  a chunk whose stored data is too large to load or send, disconnecting
  anyone who comes near). Ours would include a chunk with more distinct states
  than a palette can address (`world/chunk-representation`; rejected edits
  for now), oversized per-chunk metadata or meshes, and expensive content for
  meshing or simulation. To revisit when adversarial players enter the scene,
  with the multiplayer design (`net/client-server-seam`) and alongside
  `data/content-moderation`. Notes (assistant): every limit an edit could hit
  should fail at the edit, cheaply, on the server, not later while loading,
  meshing or sending; and per-chunk costs should have explicit bounds the
  server enforces, so no chunk can be made unloadable.
- **data/content-moderation** — publish-time validation, resource bounds on a
  single authored object, hash-revocation and moderation/attribution tooling;
  the curation/reputation model behind bundle ratings. Integrity is solved;
  moderation + resource-abuse are not. (`data/authored-content`, `data/content-bundles`)
- **data/bundle-composition** — bundle dependency / compatibility / conflict
  declarations to guide (not forbid) cross-bundle mixing; lean is
  composition-with-declared-compatibility, a power-user manifest act.
  (`data/content-bundles`)
- **data/dimension-collector** — leans pure reference counting: current
  inventory rules (no nesting, no containers/portals in traversable pocket
  dimensions) bound ownership depth to one level. Mark-sweep only if a later
  rule re-introduces nesting. **Moot for pockets (2026-11-02):** pockets are
  Aether regions that decay once their container is gone
  (`dimensions/aether-pockets`), so they need no collector; this question would
  remain only for other dimensions. (`data/dimension-gc`, `inventory/slots-and-pack`)
- **world/multiblock-placed-items** — items that occupy more than one block when
  placed (big chests, large machines, furniture): how they're addressed,
  oriented (`world/block-orientation`), broken and carried. Multi-block
  containers are deferred here. (`inventory/slots-and-pack`)
- **sim/soul-brain-containers** — a mechanical soul's brain is a traversable
  pocket dimension, and traversable pocket dimensions currently allow no
  containers. Logic machines may want memory/storage blocks inside: exception,
  or a non-container mechanism? (`sim/logic-machines`, `inventory/slots-and-pack`)
- **world/streaming-vs-bounded** — `coords/f64-authoritative` already bounds
  position at ±10⁹; open question is whether generation streams outward to that
  bound or the playable world is bounded tighter. Note `persist/diegetic-persistence`
  makes storage cost scale with investment rather than extent, which relaxes the
  pressure behind this question.
- **world/aether-storage** — (opened 2026-11-02, Michael) whether
  extradimensional storage is inert in the Aether, since the Aether *is* the
  extra dimension; matches no-nesting. Michael is weighing the gameplay
  implications; notes in `dimensions/aether-pockets`. Depends on how long
  players stay in the Aether (`world/dimensional-travel`).
- **world/dimensional-travel** — (resolved incrementally; kept open as a
  reminder, Michael 2026-11-03) the inter-dimensional travel / topology model the
  Aether nexus requires (`dimensions/aether`). This is
  the connective tissue between dimensions that nothing currently owns. Open: the
  **portal models** — likely MULTIPLE, not one (randomized, affine-transform,
  movable), possibly as independent progression elements; the **space-dilation**
  model (how Ocean distance maps to dimensional reach / how the many-dimensions
  graph compresses into a traversable space); dimension **addressing** (how a
  portal names/indexes its destination; procedural vs. curated hub). **Engine
  sub-question: can portals be seen THROUGH** (render the destination dimension
  through the portal surface — a real renderer/culling cost and a view into a
  separate chunk space) or are they opaque transitions? Touches `world/dimensions`
  (registry/topology) and `net/scale-premise` (global scale = content distribution).
  (`dimensions/aether`, `world/dimensions`)
- **Uncommitted dimensions** (Discordia, Machina, Somnium, base
  extrapolation) and their open questions moved to the bottom of
  `design/dimensions.md` (2026-11-03): `dimensions/discordia` (formerly
  `world/discordia`), `dimensions/machina` (formerly `world/machine-dimension`),
  `dimensions/somnium` and `dimensions/base-extrapolation` (formerly
  `world/authored-dimensions`).
- **worldgen/dungeon-connectivity** — Enigma's dungeons are generated from
  NOISE (quantized-domain cellular → rectilinear cells), NOT structure-spreading,
  to keep small areas independently generable (`core/chunk-locality`,
  `core/parallel-by-default`). Pure noise gives independence but not guaranteed
  traversable connectivity, so rules are needed — **but the principle is rules
  that LOCALLY CORRECT the noise (bounded to a cell + immediate neighbors / shared
  faces), never large multi-chunk structures core to generation.** Open: the
  **local per-face door rule** — doorway between two adjacent cells as a
  deterministic hash of their shared face (both chunks agree without
  communicating, like `core/cross-chunk-mechanism`), plus local repair rules
  (guarantee a cell has ≥1 exit, heal isolated pockets) tuned so dungeons are
  reliably traversable without any global layout pass. Also: the **depth axis is
  orthogonal to the cliff surface** (into the face along the inward normal, with
  a ~20° descent bias — mostly horizontal), so difficulty/loot and
  per-section style gradients run along *that* axis, not world-z; how they couple
  to cell generation; how guaranteed-reachable loot/objectives are placed locally.
  (`dimensions/enigma`, `core/cross-chunk-mechanism`)
- **persist/decay-curve** — the decay rate (on `WorldTime`,
  `coords/typed-time`) and granularity for
  unstabilized chunks (per-chunk? per-edit? is reversion gradual or stepwise?),
  and what counts as "meaningfully modified" (the save / don't-save threshold).
  **Hard constraint:** must be tuned to reward presence, never punish absence
  (`persist/presence-not-absence`). Fixed-threshold decay is a closed form in
  elapsed time (`core/closed-form-first`).
  (`persist/diegetic-persistence`)
- **persist/pause-semantics** — the multiplayer *consensus* pause: quorum to
  freeze the shared wall-clock timeline, and what happens when players join or
  leave a paused world (do they inherit the frozen clock? does a join unfreeze?).
  Single-player pause is decided (freeze the clock); this is the shared-world case.
  (`persist/presence-not-absence`)
- **persist/decay-targets** — how decay treats entities vs. items vs. terrain vs.
  fluids distinctly; what is shape (reverts freely) vs. ledger-tracked (sunk).
  (`persist/diegetic-persistence`, `data/entity-ledger`)
- **persist/decay-threshold-field** — how the fixed per-element decay thresholds
  `t_i` are generated, which is purely a *look* decision now that
  load-frequency-independence is settled by the fixed-threshold formulation
  (`persist/diegetic-persistence`). A white/uncorrelated hash gives salt-and-pepper
  reversion in a stable pseudorandom spatial order; a spatially *correlated* noise
  field gives clustered/patchy reversion (erosion-like fronts rather than
  speckle). It is a property of the threshold generator, not of the decay curve
  `f`, so it is tunable independently and could even be per-material. Open: which
  (or whether it is itself a biome/material parameter). (`core/soft-thresholds`,
  `core/worldgen-integer-domain`, `core/closed-form-first`)
- **persist/reality-bombs** — the active prompt-reversion mechanic: cost and
  caveats. (`persist/diegetic-persistence`) Direction 2026-11-01:
  an Enigma effect, found not crafted (`worldgen/dimension-cosmology`);
  remaining: its forms, dangers, and the ore re-roll consequence.
- **mobs/spawning-model** — spawn rates/conditions, which mobs, how stabilization
  field strength maps to spawn pressure. Spawning and despawning run as
  level-triggered checks (`sim/random-ticks` catalog). (`mobs/spawning`)
- **mobs/suppression-systems** — the fortification system and the repellent-resource
  system that, with the stabilization field, keep mobs at bay. (`mobs/spawning`)
- **pressure/survival-tuning** — the actual rates/curves for heat/cold/food/drink,
  how impairment/damage scales, and the dependency chains each need bottoms out in
  (clothing/shelter/food/water production) — the deferred resource-graph content.
  (`pressure/basic-survival`)
- **pressure/hearth-mechanics** — whether the hearth sources the stabilization
  field or just sits at its center; fuel types + their dependency chain; how upkeep
  scales with anchored area. **Hard constraint:** the hearth / first stabilization
  tier must be a day-1 affordance (early, basic resources), so unstabilized
  building is an opt-in unsafe choice and never a forced one
  (`persist/presence-not-absence`). **First-session chain decided
  2026-10-30 (Michael)** — campfire → hearthstone → lantern, with
  world-time burn-down while offline as a deliberate choice; see
  `pressure/hearth`. Still open: fuel types and how upkeep scales with
  anchored area. (`pressure/hearth`)
- **pressure/defense-enclosure** — what counts as a "sealed" space; whether
  enclosure is checked by the `core/opaque-chunk-queries` connectivity digest; how
  mobs path against barriers. (`pressure/defensible-space`, `sim/3d-pathing`)
- **pressure/light-economy** — consumable light burn rates + fuel, and the later
  permanent-light progression unlock. Burn-down is closed-form on world time
  (`sim/process-kernels` timed container), including while a multiplayer
  player is offline (deliberate, `pressure/light`). (`pressure/light`)
- **loop/dependency-graph** — THE deferred loop content: the resource/crafting/
  transformation dependency web the pressures bottom out in. Depth and pressure
  shape are what make the emergent loop rich vs. shallow. (`loop/deferred`, all
  `pressure/*`)
- **sim/tick-model** — how much per-tick simulation the `world/voxel-scale`
  density allows (gated partly by `core/chunk-locality`'s independent-chunk
  parallelism). Minecraft sets a shipped FLOOR for a largely single-threaded,
  GC'd-language, f64 sim; our heavier axis vs. it is 27× voxel density
  (revised 2026-10-30: "GPU-latched fluids" dropped — the GPU dual solver was
  cut) — so the floor is proven, the ceiling is what needs a
  reality-check. Overlaps `sim/tick-rate` and
  `sim/subsystem-rate-degradation`; candidate to merge into them.
- **perf/virtual-chunk-hot-path** — stress-test the `world/chunk-representation`
  bet against the worst case: a genuinely heterogeneous, heavily-edited 64³ chunk
  under active sim. This bet carries the whole storage-and-scale story and is
  currently unprofiled.
- **worldgen/territory** — civilization influence / territory as a discrete,
  process-driven subsystem (seeded capitals, spreading influence, neighbor-
  dependent borders), likely sharing spread/falloff machinery with the
  stabilization field — NOT a noise field. (`worldgen/noise-fields`,
  `persist/diegetic-persistence`) Now the territory of the animal peoples:
  `peoples/territory` (`design/peoples.md`).
- **(future, out of scope) net/lockstep** — if peer-to-peer lockstep networking
  is ever pursued, revisit fixed-point/integer physics (separate from
  `coords/f64-authoritative`). We currently assume consensus/arbitration for
  multiplayer sim, not lockstep.
- **net/transport-and-replication** — the wire-level netcode under
  `net/client-server-seam`: client-server vs. relay topology and host migration;
  what is replicated and at what rate (full state vs. deltas, per-entity cadence,
  delta compression); client-side prediction/interpolation for *remote* entities
  and the rollback/reconciliation window; join/sync of a live world; and the
  concrete **resident-vs-authoritative interaction catalog** (which interactions
  are client-predictable vs. server-deferred, decided by the network-interruption
  test). (`net/client-server-seam`, `core/authority-derived-split`,
  `coords/f64-authoritative`, `core/chunk-locality`) **Chunks narrowed
  2026-11-05:** `net/replication` (proposal) and `worldgen/seed-keys`.
- **(future, deferred) net/distributed-sim** — horizontally distributing the
  SERVER component across operator-administered worker nodes (NOT player clients,
  so no fairness/trust issue; NOT player-GPU outsourcing). Region-sharded on chunk
  boundaries, reusing `core/chunk-locality` + `core/cross-chunk-mechanism` as the
  inter-worker protocol (now naturally Vicinities: a worker node could own
  whole Vicinities, `sim/concurrency-model`); GPU-sim workers are
  one motivating case (an operator
  opting into GPU infra, since servers are assumed GPU-less by default —
  `sim/gpu-speculative-compute`). Orthogonal to the player-facing
  `net/client-server-seam`. Engineering caveats: inter-region boundary-exchange
  latency, region load-balancing as the player-resident set shifts, and worker
  failover (eased by chunks being reloadable). Premature at tens–hundreds
  concurrency; recorded as architecturally sound, not planned.
  (`net/client-server-seam`, `sim/gpu-speculative-compute`, `core/chunk-locality`,
  `core/cross-chunk-mechanism`)

## Resolved

- **core/cross-chunk-mechanism** — RESOLVED (2026-10-30) — design the controlled exchange from
  `core/chunk-locality`: read-only halo/ghost borders + an explicit queued
  cross-chunk event model and its schedule — covering fluids, falling blocks,
  multi-chunk structures, and light/propagation across seams. These are also the
  concurrency sync points. **Decided part (2026-10-29), superseded
  2026-11-04:** the meshing halo (published per-face slices with
  generations, push-notified remesh) was dropped; meshing is isolated and
  reads no other chunk in the first pass, and may later read neighbours,
  for instance through a Vicinity (`world/chunk-meshing`,
  `sim/concurrency-model`). **Added 2026-10-29: tight cases are transactions.**
  Relaxed flows (water spreading in from above) can stay eventually
  consistent, but anything conserved or identified crossing a border (a
  valuable falling block, an entity, an item) must move atomically: removed
  from one chunk and added to the other, or neither. **Resolved 2026-11-05
  by `sim/concurrency-model`:** movers owned by the Vicinity, conserved
  messages between chunks and their Vicinity, assemblies committing in
  the Vicinity's own turn, published state for soft
  effects, and durability by per-Vicinity saves into a ULID-keyed scarce
  table committed through SQLite (`persist/edit-atomicity`). (The 2026-10-30 resolution,
  send-and-bounce through per-pair outboxes plus reserve-then-commit, was
  removed.)
- **sim/parallelism-model** — RESOLVED (2026-10-30) — (rescoped 2026-10-30) the library for the
  GENERAL worker pool that runs worldgen and meshing and dispatches ready chunks
  (custom pool vs. a TBB/Taskflow-style DAG executor; work-stealing is
  rejected; **library question closed 2026-11-06: in-house**, the Dispenser,
  `deps/parallelism`).
  The simulation needs no DAG of work over cross-chunk events: chunks run as
  actors on the shared pool (`sim/concurrency-model`), and lockstep is not
  wanted (`coords/f64-authoritative`). (Revised 2026-11-05: the 2026-10-30
  resolution by dependency counts was removed.) (`sim/cpu-parallelism`)
- **sim/fluid-cadence** — RESOLVED (2026-10-30) — (supersedes the obsolete `sim/gpu-reconciliation-cadence`,
  which assumed the now-cut GPU/CPU dual solver) for the single async CPU fluid
  solver: how often the double-buffer swaps relative to the main tick, the max
  staleness consumers must tolerate, and how fluid's cadence degrades under tick-
  budget pressure (fewer active regions / coarser steps — ties `sim/tick-model`).
  **Degradation answered 2026-10-30 by `sim/subsystem-rate-degradation`: skip
  steps, don't take coarser ones.** The 2026-10-30 resolution of the swap
  cadence (an A/B slot flip in reconcile) was removed with
  `sim/execution-plan`; the fluid solver's cadence under the actor model is
  to be revisited (`design/sim-execution.md`).
  (`sim/gpu-speculative-compute`, `sim/cpu-parallelism`)
- **world/dimension-gravity** — RESOLVED (2026-10-28) — there is no per-dimension
  gravity axis. Gravity is always lattice −z and blocks are always axis-aligned to
  it, in every dimension. Enigma's tilt is purely a **worldgen phenomenon**: its
  bias profile slopes the density field along a horizontal axis, so the emergent
  surface sits ~70° off horizontal while gravity, the voxel grid, physics and
  fluids are unchanged. No new engine axis; "up" is always a lattice direction,
  which also keeps up-dependent block behavior simple under
  `world/block-orientation`. (`dimensions/enigma`)

- **worldgen/generator-versioning** — RESOLVED — there is only ONE current
  worldgen (no old generators retained). Conceptually a chunk has an
  **as-generated** baseline and an **as-altered** state; decay blends altered →
  as-generated. *How* a chunk stores what it needs to honor that is delegated to
  the chunk (`persist/diegetic-persistence`): it may keep both concrete, a diff
  against generation parameters, or near-nothing — the engine only provides the
  chunk-blob store and requires the load/unload/decay contract. The as-generated
  baseline is a snapshot, so it does not drift when the generator code changes — it
  is re-derived only on a deliberate **major-version bump** (the explicit event
  that makes existing world converge: re-derive baseline, decay then blends toward
  the new generation). When decay reaches 100% the chunk's persisted form is
  **deleted entirely**; its next load generates fresh from the current generator —
  the single path by which a generator change reaches a chunk without a bump. The
  **major version is an advisory dev flag** (did I intend this change to converge
  existing world?): minor fixes aren't worth a re-derive pass and simply don't get
  one (they reach only chunks that happen to fully decay and regenerate); a major
  bump re-derives and converges via decay. No migration, no reconstruction, no
  retained old generators, no striping. Blend is shape-only; entities defer to the
  ledger (`persist/diegetic-persistence`, `core/worldgen-integer-domain`).
- **worldgen/cave-field** — RESOLVED — commit 2 fine-3D noise fields
  (`cave_primary`, `cave_secondary`), noise-only caves, morphologies derived by
  thresholding; cost controlled by LOD deferral + regional masking
  (`worldgen/noise-fields`).
- **coords/f64-authoritative** — pure f64, hybrid integer-cell rejected
  (precision sufficient; sim determinism explicitly not required).
- **multiplayer sim determinism** — Minecraft-style tradeoff; reproducible
  worldgen (`core/worldgen-integer-domain`), non-deterministic physics with a
  consensus/arbitration layer (`coords/f64-authoritative`). Refined
  2026-11-05: the soft layer is not deterministic (`sim/concurrency-model`);
  the hard simulation is conserved by construction; desync is detected by
  per-chunk content digests (`data/content-digest`).
- **simulation depth vs. scale** — reframed as a *locality* decision, not a
  depth slider: independent-chunk simulation by default, controlled cross-chunk
  coupling (`core/chunk-locality`). Minecraft's chunk-independent model over
  DF's globally-resident region.
- **data/grammar scope** — declarative capability-access language, not a
  programming language (no control flow / not Turing-complete).
