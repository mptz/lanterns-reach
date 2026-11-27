# Simulation execution model

Execution-level rules that sit beside the concurrency model: residency, how
subsystems degrade under load, the hard simulation and how it is modelled,
multiple rates, and the use cases execution designs are weighed against
(the alternatives weighed for the earlier plan are in
`attic/sim-execution-alternatives.md`). Individual subsystem rules (fluids, logic, falling blocks,
mobs) live in `design/sim.md`; **how chunks, entities and Vicinities run
on threads is `sim/concurrency-model` (`design/concurrency.md`).**

The plan this file was first built around (a two-phase step/reconcile
exchange over per-pair outboxes, scheduled by dependency counts, with
reserve-then-commit for multi-chunk actions) was replaced by
`sim/concurrency-model`; its slugs remain as short stubs so references
resolve. Simulation is now per chunk, in each
chunk's own turn, with cross-chunk work as Vicinity transactions and
conserved messages, and soft cross-chunk effects as published state read at
checks (`sim/concurrency-model`).

Kinds used here beyond the usual: **Evaluation** (use cases a design is
critically checked against).

Read with: `core/chunk-locality`, `core/opaque-chunk-queries`,
`core/parallel-by-default`, `core/fair-share`,
`core/closed-form-first`, `core/level-triggered`,
`core/soft-state`, `sim/queued-updates` (under revision),
`sim/speed-of-information`, `sim/cpu-parallelism`, `sim/block-metadata`,
`sim/process-kernels`, `sim/random-ticks`.

## sim/execution-plan (superseded) — Two-phase step/reconcile exchange over per-pair outboxes

The plan: every chunk stepped in parallel against neighbours'
published halos, writing cross-chunk effects into 26 per-neighbour outboxes
(A/B slots by step parity), then reconciled its incoming outboxes, scheduled
by per-pair dependency counts. Outboxes existed for single-writer memory and
determinism, and the phases for a global step barrier; the actor model has
neither need (single-consumer inboxes, no determinism in the soft layer, no
global phases).

## sim/execution-invariants (superseded) — Invariants of the two-phase plan

What survives, now stated elsewhere: **a chunk is changed only by its own
turns** (`sim/concurrency-model`); **bounded reach**, every effect within the
26 neighbours (`sim/speed-of-information`); **conservation and exactly one
location per entity** (`data/entity-ledger`, `sim/exact-core`). Neighbour
lag, per-step conservation in reconcile, A/B parity and "order independence
given the schedule" belonged to the removed plan.

## sim/cross-owner-commit (superseded) — Reserve-then-commit for multi-chunk actions

Claim, vote and commit rounds through outboxes, with reservations, deadlines
and pinning. Replaced by movers owned by the Vicinity, conserved
messages between chunks and their Vicinity, and assemblies committing in
the Vicinity's own turn (`sim/vicinity`). The diegetic point survives: a
committable action has a visible wind-up (a piston's pressure phase) that
hides its latency.

## sim/residency-levels — Chunks are unloaded, held by the World, or frozen or running in a Vicinity

- Decision: four residency levels. Only the last two have a chunk actor
  (`world/chunk`).

  | Level | Simulated | Readable by neighbours | Initiates effects |
  |---|---|---|---|
  | **Unloaded** | no | no | no |
  | **Held** (a terrain node the World keeps at some level, up to full detail, `world/surveyor`) | no | no; rendered and replicated only | no |
  | **Frozen** (a Vicinity's member that doesn't run: the border around its running chunks, or every member of a frozen Vicinity) | no | yes, nothing writes it | no |
  | **Running** (a Vicinity's member that runs, only in a running Vicinity) | yes | yes | yes |

- **Freeze at the edge.** A Vicinity's transactions touch only its own
  chunks; anything that would leave it (a thrown item, a machine driving
  away) freezes at the boundary. Frozen chunks may evaluate closed forms on
  read (meshing, display) but don't materialize them.
- **No cascades by construction.** Only viewpoints decide what is simulated
  (`world/chunk-scheduling`, the Surveyor), never simulation effects, so a
  machine at the edge can't drag the loaded frontier outward. A deliberate
  chunk loader is a viewpoint with a diegetic cost (`sim/concurrency-model`).
- **Simulation is a budgeted service, not a fixed radius.** What gets
  simulated is the Surveyor's allocation of a simulation budget across
  demand, not a hard simulation distance; closed forms make the edge soft
  anyway, since anything not stepped is still exact when next looked at.
  - **Demand** comes from viewpoints, from every sense: a heightened sense
    or a telescope may ask for distant places to be simulated (animals
    moving far away, seen through the glass), not only loaded. It also
    comes from **observer artifacts**, which keep a place running without
    its player (a farm), crafted in limited numbers at a diegetic cost
    (`core/diegetic-cost`); they are the chunk loaders' in-world form.
    **An Observer can move:** mounted in a rail car, it lets a train travel
    long distances, loading and simulating chunks as it goes. A moving
    Observer is a moving viewpoint, so its Vicinity follows it like a
    player's (`sim/vicinity`).
  - **Supply is clamped by the operator:** a total simulation budget per
    server, and optionally per player or per active player. Under ordinary
    circumstances players won't use their whole share, so the clamps bite
    only under load; within them, allocation follows `core/fair-share`
    and degradation follows `sim/subsystem-rate-degradation`.
  - **Exploits are bounded by the budget, not by forbidding perception:**
    super-hearing or a telescope spends its owner's share; it can't buy
    more than the share allows.
  - **A distant simulated place is a pocket of running chunks** with its
    own frozen border (`sim/vicinity`): another Vicinity, or a separate
    pocket in the demanding viewpoint's own, for as long as the demand
    lasts.
  - **Working framing, not a tenet:** simulate enough places to keep the
    illusion that the whole world is ticking. Like any magic trick, the
    illusion needs creative leeway in where the effort goes.
- **No simulation backlog on load or on starting to run.** Catching up stepped
  simulation would cascade and contradicts retiring debt
  (`sim/subsystem-rate-degradation`) and decay-only absence
  (`persist/presence-not-absence`). The only time-jump is closed-form
  evaluation (`core/closed-form-first`), exact and chunk-local, applied
  identically at load, on starting to run, and when a visible frozen chunk is
  meshed.
- **Distinct from load skipping:** skipping under load is `core/fair-share`
  choosing what runs *within* a Vicinity; residency is which chunks are
  eligible at all.
- Open: whether frozen chunks are visible (if view distance exceeds
  simulation distance, as in Minecraft, closed-form evaluation on meshing
  keeps them current). Chunks leave a running Vicinity only by being
  cleaved off into a frozen Vicinity, which saves and shuts down (`sim/concurrency-model`).

## sim/subsystem-rate-degradation — Under load, subsystems run less often; the world clock holds
- **Open:** the catch-up ceiling, debt lifetime, and how slowdown is shown.

- Decision: this is a game, not a lockstep scientific simulation. **When
  demand exceeds the budget, individual subsystems run less often rather
  than the whole world slowing down.** Fluids flowing at half speed in a busy
  area is acceptable; the whole world lagging is not.
- **Simulation only, never world generation.** Generation stays a pure,
  repeatable function of (seed, address) (`core/worldgen-integer-domain`);
  load may *delay* it (`world/chunk-scheduling`), never alter its result.
- **Retire debt by default; catch up only below perceptibility.** A skipped
  update is normally lost subsystem time, not debt. A subsystem may repay a
  small debt by running modestly faster than real time only while the
  speed-up stays under the threshold of perceptibility; beyond that the debt
  is retired. Catch-up never spikes load when it is already high, and
  cellular subsystems never catch up by taking bigger timesteps (that breaks
  the reach bound and stability), only by extra ordinary updates. Processes
  with a closed form in elapsed time aren't stepped at all, so skipping and
  debt don't apply to them.
- **What degrades how:**
  - **Soft processes** (closed form or relaxation plus checks,
    `sim/random-ticks`) degrade by **lowering their check rate**: more
    latency, no change in dynamics. Their propagation is shed first where
    players can't readily see it (scent, disease, slow fields).
  - **Flowing water** is soft too and degrades the same
    way; it already moves irregularly, so it has a diegetic excuse.
  - **Sealed machines** promise deterministic behaviour, so they slow only
    **as a whole machine** (a straining machine, `core/fair-share`), never
    with parts of it skipping.
  - **Conserved transfers are never skipped.** They are hard simulation
    (`sim/exact-core`): a conserved message is processed promptly, and an
    assembly commits or doesn't. Load can delay the work that *starts* one
    (a piston strains), never leave one half-done.
- **Who runs less is `core/fair-share`'s decision.** Guaranteed minimum
  shares, burst credit and priority classes decide what runs; nearness to
  players ranks high. Slowdown should be visible, ideally diegetically
  (sluggish water, a straining machine), so players can tell slow from
  broken.
- **Cross-subsystem reads tolerate staleness.** A subsystem reads another's
  published state as it is; coupled effects (a current pushing a boat) are
  computed by the reading side.
- **Health metrics:** per subsystem, the share of demanded work that ran and
  the current check rate per soft-simulation type (F1 overlay).

## sim/exact-core — Hard simulation: a small, local, formally modelled core; everything else is soft simulation, justified under `core/closed-form-first`, `core/level-triggered` and `core/soft-state`
- **Open:** the first formal model, not yet written.

- Decision: the simulation has an explicit **hard simulation** — the mechanisms whose
  correctness depends on interleavings and must hold exactly — and it is kept
  small on purpose by pulling every approximate, good-enough mechanism out of
  it into the **soft simulation** (simplifying the hard part is one reason
  for doing so). Preference order is
  soft, then local hard, then global hard (`core/soft-over-hard`). The hard
  simulation is **formally modelled before it is built**.
- **In the hard simulation:**
  - **Conserved transfers in a Vicinity:** conserved messages with move
    semantics between chunks and their Vicinity (enqueue, then drop the
    sender's copy), held in the Vicinity's per-chunk queues and consumed
    by the receiver's turn (chunks have no mailboxes),
    receivers resolving and never rejecting, assemblies committing in the
    Vicinity's own turn; conservation and exactly one location per entity
    (`data/entity-ledger`).
  - **Vicinity merges and cleaves** at halts, with nothing in flight
    (`sim/vicinity`).
  - **Persistence:** one save path per Vicinity, frozen or optimistic;
    plentiful content in chunk blobs, scarce instances in a ULID-keyed
    scarce table with versioned rows; one short SQLite transaction per
    save; Vicinities rebuilt on load and adopting the scarce instances
    anchored in their chunks.
  - **Chunk lifecycle with custody:** one live instance per chunk; a chunk
    leaves a running Vicinity only by being cleaved off into a frozen
    Vicinity, which saves it; a hard message's receiver always exists.
  - **The mint is the soft→hard boundary:** harvest mints a soft-grown item
    into the ledger, a declared source.

  - **Valuable blocks tracked individually** (`sim/valuable-blocks`): minted in
    place when an ore site is resolved on reveal; sunk on decay and
    renewable (`geology/ore-sites`).
  - **Canonical content digests** (`data/content-digest`): what is digested is
    authoritative and canonical.
- **Outside it — the soft simulation:** closed-form processes,
  random-tick checks, relaxation fields, published state read at checks
  (markers, physics proxies) and content-free nudges, timestamped markers
  with TTLs (`core/closed-form-first`,
  `core/level-triggered`, `core/soft-state`). These are designed so that
  order, duplication and loss don't matter; their correctness claim is
  "converges to the right state and never violates the core's invariants",
  which is a short argument or a liveness property over a tiny state space.
  **Rule: a mechanism enters the hard simulation only if it cannot be made
  idempotent, level-triggered and soft;** anything else stays soft and
  justifies itself under `core/closed-form-first`, `core/level-triggered`
  and `core/soft-state`. Pulling mechanisms out pays twice: a smaller hard
  simulation, and a soft simulation that is nearly free to verify.
- **Modelling plan:**
  - **Tool:** TLA+ (PlusCal for the algorithm-like parts), checked with TLC;
    Quint (TLA-based, friendlier syntax) if the notation gets in the way.
    Reference for cost/benefit: Amazon's published experience using TLA+ on
    distributed protocols (Newcombe et al.).
  - **Scope:** 3 chunks in a line, then 2×2, in one or two Vicinities; one
    conserved item kind; an entity crossing, an assembly across a corner, and
    two rival assemblies; merge, cleave, save, crash and reload as
    nondeterministic environment actions.
  - **Safety:** totals conserved; each entity in exactly one place, including
    across crash and reload; a held cell changes only by its transaction's
    outcome; nothing addressed to an absent chunk is lost.
  - **Liveness:** every transaction resolves; some rival always progresses.

  - **Abstractions:** hashes and closed-form arithmetic are nondeterministic
    choices; the model checks the protocol, not the math.
- **Conformance (the model can't prove the C++ matches it):** property-based
  tests drive the engine through random interleavings against lockstep mode
  and content digests as the oracle; optionally, engine traces replayed
  against the spec (trace validation).

## sim/execution-use-cases — Use cases any execution design is critically evaluated against
- **Open:** re-walking the scenarios against Vicinities (U20 already revised, against `sim/block-metadata` too).

- **U1 Water across a seam:** a lake draining across a chunk face, an edge and
  a corner (soft: flowing water is derived from sources,
  `sim/gpu-speculative-compute`); an ocean healing a dug hole flat.
- **U2 Falling sand at terminal velocity** across vertical chunk boundaries;
  two blocks contending for one cell at a chunk corner.
- **U3 Structure collapse** spanning several chunks (a felled tree, a
  collapsing bridge). **Structural strength is not simulated in this phase**
 , so a collapse means chains of independent
  gravity-block falls (Minecraft-style). If structural support is ever
  added, a local support field (Valheim / 7 Days to Die style) would fit the
  speed of information, and a collapse moving as one rigid group would be
  an assembly (U9) — noted only, not planned.
- **U4 Explosion larger than a chunk:** a finite-speed shockwave under the
  speed of information.
- **U5 Entities at seams:** a player crossing the corner where 8 chunks meet;
  a mob crowd jostling across a seam (ownership with hysteresis, physics
  proxies).
- **U6 Item drops and transport chains** (hopper/conveyor-like) crossing
  seams continuously: throughput and latency of conserved transfers.
- **U7 Logic machines spanning chunks:** unsealed (world rules, erratic
  timing) vs. sealed (compiled, overclockable); signals crossing a seam.
- **U8 Entities over the speed of information:** launchers, fast vehicles,
  explosion-propelled contraptions.
- **U9 Physics island** spanning chunks: a stacked contraption or vehicle that
  must be solved as one body.
- **U10 Propagation at seams:** light or another field updating after a block
  is broken on a chunk boundary.
- **U11 Local overload:** one chunk with a giant machine or fluid body;
  fair-share degradation and its visibility.
- **U12 Idle world:** far from players, cost should be near zero.
- **U13 Frontier load/unload** with conserved things in flight toward a
  chunk leaving its Vicinity.
- **U14 Player edits** entering promptly and staying responsive.
- **U15 Multiplayer contention:** two players editing the same seam cell.
- **U16 Cross-subsystem coupling at a seam:** a boat carried by a current
  across a chunk boundary while fluids are degraded.
- **U17 Save during simulation:** a consistent snapshot with conserved things
  in flight (answered by per-Vicinity saves into a ULID-keyed scarce table,
  optimistic against a transformation counter, `sim/concurrency-model`).
- **U18 Piston push/pull across a chunk border:** a line (or, with sticky
  connectors, an assembly) of blocks whose cells and destination span two or
  more chunks; some blocks immovable; rival pistons. The push limit
  (Minecraft: 12) keeps every affected cell within the piston chunk's 26
  neighbours, so it must be < 64. Direction: an assembly
  committing in its Vicinity's own turn, with a visible pressure
  phase (`sim/concurrency-model`).
- **U19 Falling columns taller than a chunk:** a contiguous column of gravity
  blocks (possibly hundreds tall) falling and landing across several chunks.
  Notes kept: landing propagates upward at the fall speed, not the column
  height (≈ 18 cells per step at 10 Hz terminal velocity), inside the speed
  of information; arrivals at an occupied target stack upward from the pile
  top in their original column order (or scatter to an angle of repose: a
  gameplay choice); a column could be carried as one entity with a block
  count, much cheaper for very tall columns.
- **U20 Closed-form growth crossing a chunk border — WORKED EXAMPLE.** A
  reed whose top is in the face layer of the lower chunk L grows one block
  taller, into the face layer of the upper chunk U. It tests how
  closed-form growth produces a cross-owner effect under
  `core/closed-form-first`, `core/level-triggered` and `sim/block-metadata`
  (a cell is exactly its block state).
  - **The rules:**
    - **No inter-block references.** No block points at another block. Each
      block's state is its own.
    - **Growth inside a chunk** is driven by checks (`sim/random-ticks`) or
      iteration during worldgen.
    - **Growth across chunks is marker-based only, and markers derive from
      the face layer:** a marker is a projection of the publishing chunk's
      cells that touch the shared face, edge or corner (their block states),
      never of cells deeper inside.
    - **Consequence:** reach doesn't depend on the kernel. A reed 500 blocks
      tall crosses seams the same way a 3-block one does.
  - **States.** A reed that grows up to 9 tall has 9 states, one per height:
    every segment holds its height, since a cut can make any segment the top.
    The **tip** is not a state: it is whichever segment has air above it.
    A column's maximum height is a hash of its base cell, which a segment at
    height h finds h − 1 cells below its own coordinates, so reeds vary
    (3 to 9 tall, say) from coordinates alone. All 9 states present to
    players as one reed (`world/brick`).
  - **Growing inside a chunk.** The tip's growth is a memoryless hashed
    stream keyed by its state and coordinates (`sim/random-ticks`). Each
    event grows 0 or 1 blocks, keyed by the event's official time and the column's
    coordinates; skipping with probability p is the same as growing at rate
    λ(1 − p), so columns grow at varied speeds, and no single event places
    blocks in two chunks.
    Nothing records when the tip arrived: the chunk materializes all its
    cells together, so its one last-materialized time covers them.
  - **Why every segment holds its height** rather than only the tip holding
    a stage: a cut makes any segment the top, and that segment then knows
    how far it may still grow without looking down the column, which
    markers couldn't support across a seam. A tip-only stage (fewer states:
    with 1 or 2 blocks per stage, 5 stages plus one stem state reach 9 or
    10) suits plants that can never straddle a seam.
  - **Crossing the seam (L → U).** L's tip in its face layer is visible to U
    as a marker (reed, height h). U's check sampler includes its own
    face-layer cells adjacent to neighbour markers of an extending kind, so
    the air cell above gets checks though plain air carries none. **Growth
    across a seam starts when the receiving chunk observes the marker:** U
    grows height h + 1 at the first growing event of the tip's stream after
    the check at which U first sees it, and the new segment is U's tip. A state
    holds no time, so U can't know when L's tip arrived; starting at
    observation is what the stream's memorylessness makes unbiased, and
    costs only the wait for that check (about one check interval, with the
    face-layer boost of `sim/block-metadata`). No message is sent. If U is
    absent, the reed doesn't grow into it while it is away; it starts when
    U loads and observes the marker. That is the price of storing no time,
    accepted for soft growth.
  - **L learns from U's face layer the same way:** L's face segment is the
    tip only while U's adjacent face cell is air, which it reads from U's
    markers.
  - **Cuts.**
    - **Anywhere inside one chunk:** purely local. The segment below the cut
      is the new tip, and its stream starts from the cut, which is an edit
      in that chunk's own turn and so a materialization.
    - **At U's face cell:** the segment below is L's. L sees air above at
      its next check and becomes the tip from that check, by the same
      observation rule. There is no instant regrowth: L counts no events
      from before it observed the air, even if it last materialized long
      before the cut.
    - **At L's face cell:** U's segment reads L's marker (air) at its next
      check and loses its support, as sugarcane does.
  - **Obstruction:** a block placed in U's face cell above L's tip is simply
    its state, seen through U's marker; L's tip is not a tip while the cell
    is occupied, and becomes one again from the check at which it sees the
    cell clear.
  - **Loss is harmless:** markers can be wiped at any time and rebuilt from
    face-layer states, and no rule credits growth from before an
    observation, so there is no "cut, walk away, instant regrowth" exploit.
  - **Latency:** materialization waits for the next check of the face cell;
    where a visible delay would read as a bug, a check can be scheduled
    sooner (a hint, per `core/level-triggered`).
  - **Features too large for their own states use boundary states.** A
    sequoia's trunk crossing a chunk face is a **trunk-crossing state** in
    the face layer carrying a few small classes (species, growth remaining,
    a trunk-radius class, a lean). The receiving chunk builds its whole
    64-block section of the tree from that state and a hash of the crossing
    cell's coordinates (plus the crossing event's official time, for a tree
    growing after worldgen), so random decisions are made once per chunk
    crossed, not once per block. Players see an ordinary log
    (`world/brick`). This is how continuations carry huge features without
    records (`worldgen/feature-continuations`).
  - **Rejected:** a single anchor at the plant's base with segments in U
    tagged by its version and cross-chunk assertions (inter-block
    references and messages); per-cell anchor times and cut records (state
    beyond the block state, which `sim/block-metadata` rules out), which
    would have made re-anchoring exact to the cut time and let growth
    progress into an absent chunk.
## sim/multi-rate — Processes step at different rates: divisors of a base tick aligned with the frame rate

- **Tiers of physics stepping**: a very limited **fast** tier
  (about 60 Hz) mainly for input latency, a **medium** tier (about 20 Hz) for
  entity physics that should look physical, a **slow** tier (about 5 Hz) for
  slow physical processes, and a **background** tier (a fraction of a Hz)
  bridging real-time physics and random block ticking. Perhaps not strictly
  tiered: what matters is variable-speed stepping, so player input can run
  through a fast path to the world without computing all physics at excessive
  precision and cost.
- **A base tick aligned with the target frame rate, rates as its divisors**:
  e.g. base 60 Hz; 20 Hz steps every 3rd base tick, 5 Hz every 12th,
  slower ones at larger divisors. Phases line up: the schedule is which
  processes step on which base tick. The speed of information is per step
  (`sim/speed-of-information`), so a faster process's effects reach further
  per second.
- **Player bodies** (open; Michael wants to dig further before accepting a
  20 Hz authoritative body): in Minecraft, movement input applies once per
  20 Hz tick, so presses round to 50 ms; players feel it (tick-timed parkour,
  PvP hits and knockback on tick boundaries). Notes (proposed): client
  prediction at frame rate and a 20 Hz authority must give the same
  trajectory or every correction snaps, which movement physics rarely
  guarantees; players are few, so stepping player bodies at the base tick in
  the authority costs O(players). Current lean: player bodies at the base
  tick, other entities at 20 Hz.
- **Physical is not the same as authoritative:** decorative particles
  and effects (sparks drifting up, leaves falling from leaf blocks, drips) may
  collide with blocks, but if they don't affect gameplay they are **never
  reconciled across machines**: the emitter is shared state, its effects are
  each machine's own. They are closed forms evaluated
  mostly on the GPU; a particle either ignores blocks by its nature, or its
  intersections are computed by the client's chunks or Vicinity like any
  other physics, never on the frame thread (`engine/frame-thread`).
  Minecraft's particles are client-only and collide with block boxes (from
  memory). Client-only lighting effects are likewise allowed, since light has
  no gameplay role (`light/visibility-only`). A particle that does affect
  gameplay (an ember that can ignite) is an authoritative entity, though the
  effect can often be modelled without it (fire spread as a hazard on
  neighbouring cells, sparks as the visual).
- **Tumbling: translation and rotation as one closed form**.
  A flung entity or particle is two independent motions composed at its
  centre of mass: `p(t) = p₀ + v₀t + ½gt²` (drag, if any, is an exponential
  decay of velocity, still closed form) and `q(t) = exp(½ωt) ⊗ q₀` for a
  world-frame angular velocity `ω`; a body point sits at
  `p(t) + R(q(t))·x_body`. This is the physically correct decomposition for
  a free body, so blasts can fling things tumbling for the cost of one
  segment each:
  - **explosions** give each flung body `v₀` from the impulse and `ω` from
    the impulse's torque about its centre of mass divided by its inertia;
  - **particles** store `(t₀, p₀, v₀, q₀, ω)` and are evaluated in a shader
    at render time, with no per-frame CPU or simulation work
    (`render/gpu-driven`), so tumbling debris costs no more than dust;
  - **spirals for free:** a particle whose visible part is offset from its
    (invisible) centre of mass traces a circle around the centre's path, a
    helix when `ω` is along the motion (sparks spiralling up, leaves
    fluttering down, swirling motes). It's the same formula with a non-zero
    `x_body`, and several visible parts at different offsets give a
    spinning ring or cluster from one particle;
  - **screw interpolation is not used:** dual-quaternion screw motion
    (ScLERP) would make a thrown object's centre of mass spiral around the
    screw axis instead of following its parabola; offsets give the spiral
    where it is wanted;
  - **later, if wanted:** asymmetric bodies flip about their intermediate
    axis (the Dzhanibekov effect, a thrown racket); torque-free motion has
    a closed form in Jacobi elliptic functions. Constant `ω` is enough for
    blocks and particles.
- **Scheduled updates: the background tier as generic scheduled physics**: a
  time-ordered schedule of physics updates, of which a block
  check is one kind. Stateless random ticks can't guarantee a burning block is
  looked at soon (Minecraft's fire uses scheduled block ticks, a time-ordered
  queue, not random ticks). Notes (proposed): which cells to update is known
  from state the chunk already holds (cells in states with pending events,
  such as burning blocks, `sim/block-metadata`); when to update each
  is its type's rate, through the hashed process on absolute time
  (`sim/random-ticks`); together they form a per-chunk schedule ordered by
  next update time (a timing wheel), derived and rebuildable from metadata on
  load, so it can't drift. Plain stateless random ticks remain only for
  things with no metadata, if any.
- **Aligned logical time, spread wall-clock time**: every process of a tier
  steps at the same logical tick (a 20 Hz
  step at every 3rd base tick), so interactions within a tier have both sides
  present, but the compute for that step may take the whole period: its
  deadline is the tier's next step, not the next frame. Rendering interpolates
  between the last two completed steps.
- **Time budgeting** (proposed, open):
  - per base tick the workers offer W × 16.7 ms of compute; each tier's step
    cost is spread over its period, so the load per base tick is about
    C₆₀ + C₂₀/3 + C₅/12 + background, which must fit in that capacity with
    headroom;
  - **the Dispenser deals in coarse turns**:
    individual physics calculations would be swamped by dispatch overhead,
    so a turn is a whole chunk's step for one tier (every entity, collision
    and scheduled update homed in that chunk, as loops inside the turn), and
    cheap or idle chunk-steps are batched (`sim/concurrency-model`'s
    batches). The fast tier's O(players) work may be one turn, or not go
    through the Dispenser at all;
  - chunk-step turns carry deadlines (a 60 Hz turn within one base tick,
    20 Hz within 3, 5 Hz within 12), and the Dispenser favours the
    earliest deadline within each class's share (`core/fair-share`); this
    spreads the work without staggering logical time;
  - **a step's result is available only at the end of its window,** so a
    20 Hz entity is drawn interpolated between steps k−2 and k−1 while step k
    computes, up to about 100 ms behind (Minecraft is about one tick behind);
    the player's own body is predicted, so it isn't delayed;
  - **across tiers, interactions are asymmetric:** a faster process at time t
    sees a slower one's state at its last check, extrapolated by closed-form
    motion (exact in free flight; the only error is an event the slower one
    hasn't checked yet), and its effects on the slower one arrive as deltas
    in that one's next step; within a tier they are simultaneous;
  - under overload, slower tiers' steps are skipped first
    (`sim/subsystem-rate-degradation`); the fast tier and the world clock
    never skip.
- **Closed-form motion: rates change how often, not how fast**. Physics
  constants are per second, never per tick
  (Minecraft's per-tick gravity and drag tie its motion to 20 Hz). Each check
  **integrates exactly from the last check to now** in closed form (gravity
  plus linear drag, walking toward a target velocity, an impulse profile),
  **sweeps only that interval's path** against terrain (`sim/swept-collision`),
  resolves any contact at its exact time t* and continues from there. Only
  the current state is stored; **no trajectory is precomputed** (computing a
  jump's whole parabola is overkill). So a body moves at the same
  speed at 60, 20 or 6 Hz, with exact contact times; a contact found late is
  back-dated to t*, which the interpolation delay hides.
  - **Jumps are impulse profiles**: even the shortest press is a
    multi-frame acceleration over [t₀, t₀ + Δ_min]; a longer hold extends or
    reshapes it when the release arrives (piecewise in press duration).
  - **Where closed form stops:** resting contact and stacking, many-body
    contact (crowds, a cart pushing several entities), forces varying in
    space (currents, wind). Those step at their tier's rate. There is no
    inter-entity gravity, so no chaotic many-body sensitivity to rate.
  - **Rate is a property of the entity, not the chunk** (bodies live in the
    scope above chunks), set from proximity to players and interactions;
    crossing a rate border causes no speed discontinuity.
- **What stays rate-dependent, and the rules for it**:
  - detection and reaction latency (a trigger, a mob noticing a ledge), the
    sampling of inputs varying in space or time (crossing a current), stepped
    solvers (stacks, pens, fluids), and interaction with the player (a pushed
    entity, hit detection against what the player saw);
  - **escalation**: an entity interacting with a faster one runs at
    the faster rate while it lasts; a player pushing an entity runs it at the
    player's rate;
  - **a floor under everything predictable: 20 Hz** (20 rather than 10,
    since it divides the 60 Hz base tick
    and exceeds any 10 Hz machine rate). Distance may lower check rates down
    to the floor, never below it for predictable things; at rest, a body
    costs nothing until woken;
  - **slower than the floor only where the closed form is inherently
    random**: a randomly drawn activation time, as in fire spread, growth
    and
    decay, where checking less often changes nothing a player could rely on;
  - **fluids** may get bounded sub-second timing jitter for a naturalistic
    feel (tentative, Minecraft's fluids are very unrealistic); keep
    the jitter bounded (a few tenths of a second) so average flow stays
    dependable;
  - candidates (proposed, open): wake an entity at its predicted next event
    (time to reach a plate, a ledge, a current's edge) if earlier than its
    period, a small look-ahead rather than a trajectory; lag compensation for
    hits;
  - beyond a Vicinity's running chunks, things freeze rather than slow (the residency
    edge, `sim/residency-levels`), which composes with the floor;
  - general rule: **distance may change how often things are checked, never
    what they do.**
- **Contraptions run in the ordinary physics,** with no dedicated logic tick
  (`sim/logic-machines`, soft logic): sensors such as pressure plates
  are physics components that sample level-triggered at their check rate.
- Open: the tiers' exact rates; player bodies at the base tick; the
  per-chunk schedule's data structure; the render delay for 20 Hz entities
  (interpolation versus extrapolation); cross-tier interaction rules per
  effect; wake-on-predicted-event.

## sim/motion-segments (draft) — Bodies keep committed motion and limited control, so prediction stays close to physics; rubberbanding is the measure
Physics and prediction converge because the physics makes the near future
mostly decided already, not because it is charted out ahead. A client that
has the authority's last state can stay close to what the authority will
compute, because most of a body's motion over the next latency was
committed before then.

- **Every input goes through the physics:** an intention becomes a force
  or impulse the body's physics applies; nothing sets a body's velocity or
  position directly. The engine's job is then only to keep the physics in
  sync across machines.
- **Committed motion:** impulses act over time, not instantaneously. A
  jump's push imparts acceleration that continues for a while; a flung
  block keeps its velocity and spin. Whatever was committed before an
  input arrives plays out the same on every machine.
- **Limited control authority:** what an intention can change per unit of
  time is bounded, most of all away from the ground. In the air a player
  can steer, more than in real life as games traditionally allow, but far
  less than on the ground. An unforeseen intention moves a body by at most
  about `½·a_control·L²` over a latency `L`, where `a_control` is the
  acceleration intentions can apply in the body's situation (ground, air,
  water, climbing); that bound is the divergence the client can't avoid.
- **Bodies are still stepped:** the authority and the client's local
  physics (in multiplayer) run the
  same pure `stepBody` at the base tick (`sim/multi-rate`), so they agree
  whenever they have the same inputs. Nothing requires charting a jump's
  arc or its landing in advance; that would push too far towards closed
  form for bodies with intentions.
- **Segments describe motion for everyone else:** the renderer and the wire
  get short timestamped segments `(t₀, p₀, v₀, q₀, ω)` (the tumbling note in
  `sim/multi-rate`), evaluated between steps and slightly ahead.
- **Passive movers may go further, as an option:** a body with no
  intentions (a flung block, debris, an arrow) is a closed-form segment
  until it hits something, so if profiling calls for it, it can skip
  stepping and be woken at a predicted time of impact, with segments
  recording the versions of the chunks they cross and recomputing when
  one moves. That would make a Vicinity's passive-mover work O(events);
  not needed to start.

### Server wins, so rubberbanding is the measure
- **Reconciliation is as simple as it gets:** the authority wins. So the
  quality metric is **the total rubberbanding a client sees**: the
  integrated visible correction (displacement and rotation) per unit of
  time, measured on the client, logged and testable under simulated
  latency and jitter. A design test for any mechanic: does it raise the
  expected correction?
- **Avoidable versus unavoidable:** a correction is avoidable if client and
  authority disagreed with the same information (the same pure `stepBody`
  on both sides should drive this to zero); it is unavoidable if it carries
  information the client couldn't have had yet (another player's input,
  another player's edit) at the current latency.

### Prediction is part of the physics of impulses and events
We decide the world's physics, so instead of layering game-networking
prediction on top of it, the physics is designed so that the information
behind corrections arrives early or matters little. Working framing, not a
tenet; the mechanisms are proposals.
- **Inputs act through inertia:** a body responds to a new intention by
  accelerating, not by jumping to a new velocity, as real bodies do
  (`core/realistic-in-kind`); this is limited control authority, above,
  seen as a design lever. Your own body is predicted locally, so it costs
  nothing in feel; for anyone watching a remote player the correction stays
  small. The control acceleration is the knob between responsiveness and
  correction.
- **Impulses are telegraphed:** an action that applies a large impulse
  (a jump's crouch, a swing's wind-up, a piston's charge) is announced
  before it acts, and the announcement travels at once. When the lead time
  is at least the latency, every client sees the impulse at the same world
  time and nothing needs correcting. `tools/breaking-as-action` is the
  first instance.
- **Known events are published ahead** where they are known (a passive
  mover's landing, under the option above), so clients agree about
  predictable events
  before they happen.
- **Own inputs at their own time:** the authority applies a player's
  intention at the base tick the client tagged it with, within a bounded
  window (say 100–200 ms), by replaying that body's steps from its state
  at that tick (the authority keeps a short history per player body, as
  the client's local physics does). The client's local physics for its own
  body then matches the authority except where other
  players or the world intervene. The window bounds abuse (an intention
  older than the window applies late), and the result must still be valid
  in the authority's world.
- **Corrections are smoothed by size:** small ones blend out over a few
  frames; large ones (a real disagreement) snap. The metric counts what is
  visible.

### Worked example: landing is a deceleration, not a stop
- **The ground is rigid; the body is compliant.** On contact the centre of
  mass decelerates over a stopping distance `d` (the medium's give plus the
  body's: legs bending), at `a = v²/(2d)` for a time `T = 2d/v`. A normal
  jump landing (`v ≈ 15` cells/s, `d ≈ 1` cell) takes about 130 ms, several
  base ticks.
- **Fall damage follows peak deceleration** (`world/climbing-and-descent`:
  damage from speed lost over distance, above the body's absorption).
  Posture and surface set `d`, so a crouch, a roll or a hay bale soften a
  landing with no special rule.
- **The camera dips with the centre of mass** and rises as the legs
  extend, so the landing's feel comes from the physics.
- **Contact is less sensitive to step timing:** an instantaneous stop
  falls on one tick, so client and authority differ by however far the
  body moved inside it; a stop spread over several steps shrinks that.
  (The networking gain is modest here, since landing time is predictable
  from the arc on both sides.)
- **Not a stiff spring:** a stiff spring integrated at 60 Hz goes unstable
  or jitters. On contact the handler works out `d` from posture and
  surface and applies a prescribed deceleration (constant, or smoothly
  shaped) until the body stops or the legs bottom out; whatever speed
  remains then is a hard stop, the injury case. That is closed form inside
  the contact, stable at any step rate, and the same function on both
  sides.
- **It generalizes to any collision where something gives:** a body into a
  wall, a mob knocked into another, a block landing on a mob. Each gets a
  stopping distance from what is compliant, and a peak deceleration that
  drives damage.

### Worked example: stopping a sprint
- **The visible correction is the area under the velocity-difference
  curve.** An observer who hasn't yet heard that a player released W keeps
  predicting the sprint speed `v`; when the news arrives after a latency
  `L`, the gap to correct is ∫(v_predicted − v_actual) dt over `L`.
  - **Instant stop:** the gap is `v·L`; all of the latency becomes
    overshoot.
  - **Deceleration over `T`, with `T ≥ L`:** the gap is `½·(v/T)·L²`, a
    fraction `L/(2T)` of the instant case, falling with the square of
    latency (`T = 2L` gives a quarter).
  - **Deceleration with `T < L`:** the gap is `v·(L − T/2)`, still `v·T/2`
    less than instant, but linear in latency again.
  So `T ≥ L` is where the error turns quadratic, not a threshold; more
  deceleration time keeps paying off. The same holds for starting a sprint,
  for turning mid-run (velocity turns through acceleration even though look
  is instant) and for landing.
- **`L` is one-way legs, not pings:** watching another player, it is their
  one-way trip to the authority, plus up to a step there, plus the
  authority's one-way trip to the watcher, roughly half the sum of the two
  round-trip times plus a step (two 60 ms pings at 20 Hz: about 110 ms).
- **Your own body feels none of it:** it is predicted locally, so the
  deceleration is felt as physics, not lag.
- **Rendering remote bodies slightly in the past is a second, smaller
  lever:** a delay `D ≥ L` would remove corrections altogether, but makes
  every remote player look late, which costs aiming; so `D` is a small,
  tunable supplement (well under the 100 ms some engines use), and
  deceleration is the main lever.
- **Tuning ranges (open):** games typically stop a sprint in about
  50–150 ms and people in about 300–500 ms; something around 150–250 ms
  covers typical latencies while staying responsive and closer to real.

- **Open:** the control accelerations per situation (ground, air, water,
  climbing) and how they feel; how long a jump's push lasts; the input
  window's length; how a telegraph's lead time adapts to latency without
  changing gameplay timing for everyone; for the passive-mover option, time
  of impact along a parabola through the voxel grid and the broad phase for
  mover-on-mover contact.