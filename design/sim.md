# Simulation

The tick, physics, fluids/CA, and how GPU compute relates to the authoritative
world simulation. See also `core/chunk-locality`. **The execution model every
subsystem here runs on is `sim/concurrency-model` (`design/concurrency.md`),
with residency, degradation, the hard simulation and evaluation use cases in
`design/sim-execution.md`.**

## sim/gpu-speculative-compute — Fluids are soft: source blocks plus derived, stored flowing water on the CPU; GPU speculation is parked

- **Sources and flowing water (the preferred approach).**
  - **Source blocks are ordinary shape:** worldgen places them (oceans,
    lakes, aquifers) and players move them only by edits. Filling a bucket
    is an extraction mint and emptying one is a sink, like any other block
    (`data/entity-ledger`).
  - **Flowing water is derived:** each cell's level is a function of nearby
    sources and terrain, so a check reads its neighbours and sets its own
    level. That is level-triggered soft simulation (`core/level-triggered`):
    across a seam, the receiving chunk's face cells read the neighbour's
    face-layer state, the neighbour nudges when its face changes, and no
    message is involved (`sim/concurrency-model`). Nothing is conserved, so
    fluids are outside the hard simulation (`sim/exact-core`). It also
    allows the bounded sub-second timing variation noted in `sim/multi-rate`.
  - **Flowing water is stored**, like other slow-moving, slow-spreading soft
    state such as fire: persisted with the chunk as ordinary
    state, convergent and self-restoring, never needing a re-flood on load.
  - **An infinite-source rule exists**, though not necessarily
    Minecraft's (two adjacent sources make a third). **The priority is that
    large bodies of water keep clean, non-pockmarked surfaces**, not
    tracking how much water an ocean holds. Candidates (proposed, open): a
    flowing cell becomes a source when enough horizontal neighbours are
    sources and it is supported below; or a per-body level (sea level for
    oceans, a worldgen parameter) under which any flowing cell connected to
    the body's sources becomes a source, so holes dug in an ocean heal flat.
  - **Gases** (`progression.md`, "gas as a fluid") follow the same model
    unless decided otherwise.

- **One solver, on the CPU, run per chunk.** Fluid runs in each chunk's
  own turn as level-triggered checks reading neighbours' published state
  (`sim/concurrency-model`), and its cadence degrades under load per
  `sim/subsystem-rate-degradation`. Consumers (pathfinding around water,
  pressure on mechanisms, buoyancy) read the last published state and
  tolerate slight staleness. Rejected: a GPU speculative solver beside a
  CPU authoritative one, joined by a `merge(authoritative, speculative)`
  layer (two solvers and a cross-device merge invariant), and a world-wide
  double-buffered field swapped as the commit.

- **No fluid conservation.** With sources as shape and flowing water
  derived, there is no fluid quantity to conserve, so fluid is not a
  formal-reasoning target.

- **Worldgen corollary** (`core/worldgen-integer-domain`): unchanged and
  independent of the fluid revision. A GPU-computed float must NEVER gate a
  worldgen decision (GPU floats are vendor/driver-dependent, not bit-reproducible);
  worldgen's discrete outcomes are decided in the integer domain on the CPU. Fluid,
  by contrast, has **no cross-machine reproducibility requirement** (runtime
  physics is outside the integer-domain tenet; `coords/f64-authoritative`
  declined lockstep; the soft layer is not deterministic,
  `sim/concurrency-model`) — which
  is why a *future* GPU fluid accelerator is clean while GPU worldgen stays
  permanently leashed. See `sim/cpu-parallelism` for the worldgen CPU/GPU split.

- **Parked future: optional GPU fluid accelerator (out of v1 scope).** If profiling
  ever justifies it, a GPU could run the fluid checks as an
  accelerator **behind the single solver's interface** — but it must remain
  non-authoritative and must NOT reintroduce a second authoritative solver or a
  cross-device merge. Also gated by the no-server-GPU premise (`sim/cpu-parallelism`:
  servers are assumed GPU-less, so the CPU path is the one that must be excellent).
  Any operator wanting GPU-accelerated heavy sim is the deferred `net/distributed-sim`
  topology, not a v1 path. Status: parked, not planned.

## sim/logic-machines — In-world logic machines; "mechanical souls" are player-built, sandbox-confined behavior

- Decision: The game has an **electronics/logic system** — powered circuits
  (e.g. copper-block wires, logic gates) plus **canned I/O modules** (sensors in,
  actuators out). A player assembles a logic machine from these; the machine maps
  **sensory inputs → logic → action/mobility outputs**. This is the *executable*
  layer of diegetic authoring (`core/diegetic-authoring`) — player-authored
  *behavior*, not just static content.
- **Mechanical soul = a logic machine bundled as a soul.** The machine lives in a
  **pocket dimension** (`world/dimensions`) — the soul's "brain" is a dimension
  you can enter and rewire. Bundling produces a **soul item that owns that
  dimension** (an ownership edge, `data/dimension-gc` — a weak reference would let
  the brain be collected); the soul is an `data/entity-ledger` entity (minted from the resources spent
  building + bundling; animating consumes/binds it — no faucet).
- **Soul is an INTERFACE (sense → act) with (at least) THREE implementations:**
  - *canned-behavior soul* — a distilled creature AI (a `data/grammar` binding);
  - *mechanical soul* — a player-built logic machine (this entry);
  - *player soul* — a human driving a body through a sensorium + intentions
    (`sim/player-embodiment`).
  A body binds any of them; it does not care which is behind the sense→act
  contract. Clean polymorphism — the player is not a privileged special case but
  the third implementation of the same interface — and the extension point for
  future soul kinds.
- **Turing-completeness is intended and fine.** A circuit with gates + feedback +
  memory can be Turing-complete (like Minecraft redstone). We do NOT restrict it
  to stay non-Turing. Safety comes from **capability-confinement**, not
  computational restriction: an in-world machine is sealed inside the game
  universe — no network, filesystem, host, or syscall access; its ONLY aperture
  is the canned sensor/actuator modules, which touch only the game world (an
  air-gapped VM). The confinement boundary must be real — the module set is the
  only I/O and must not leak host access (`builder-security`).
- **Fairness via a declining CPU budget (not a fixed per-tick cap).** By analogy
  with interactive-vs-batch OS scheduling: a bursty machine that thinks then
  rests runs at full speed; one that continuously pegs the CPU has its effective
  budget **decay**, so it degrades into a *sluggish creature* rather than hanging
  anyone or being killed. Smooth throttle on sustained load, consistent with
  `core/soft-thresholds`. A greedy mechanical soul is diegetically legible as a
  slow brain. (This is also the engine answer to the "CPU-hogging authored item"
  resource-abuse case in `data/content-moderation`.)
- **Evaluation model: interpret a topology-normalized form isomorphic to the
  built machine.** Don't simulate every wire voxel; collapse runs of copper-wire
  blocks into abstract edges and reduce the spatial circuit to its logical
  topology (graph of gates/modules + connections), then evaluate that. Keeps the
  diegetic "it really is the machine you built" property (isomorphic, not a lossy
  recompile) while skipping per-voxel redstone cost.
- **Compile vs. interpret is resolved by a diegetic action: "sealing"
  (`core/diegetic-cost`).** Sealing a mechanical soul's pocket dimension is an
  in-world act (not permanent) that declares the machine final-for-now and so
  PERMITS the engine to compile/preprocess the topology into a cheaper cached
  form — because sealing is exactly the stability guarantee compilation needs.
  - **Unsealed** = live, editable, interpreted at full fidelity (you can enter
    the brain and rewire it; the engine pays for that flexibility).
  - **Sealed** = compiled/cached/frozen (efficient but not editable).
  - **Unsealing** reverts to editable/interpreted and invalidates the cache.
  So it is not compile-OR-interpret; it is both, switched by player intent — and
  the in-world act hands the engine its optimization + invalidation permissions
  (the `core/diegetic-cost` corollary in action).
  - **Sealing also decides clock speed — diegetic timing closure.** An
    **unsealed** machine runs in the ordinary physics, at or above the 20 Hz
    floor for predictable things (`sim/multi-rate`), so per-hop delay is a
    physics check, with jitter from the soft layer's non-determinism
    (`sim/concurrency-model`). Its signal propagation is therefore erratic:
    it runs reliably only at a slow clock, one whose period exceeds the
    worst-case path delay plus jitter — the same rule real hardware obeys. A **sealed** machine is compiled: its timing is analysed
    once, its behaviour is deterministic, and it can be **overclocked**, up to
    its `core/fair-share` budget (bursting, then settling). So players who
    want predictable, fast circuits get them by sealing, and the unpredictable
    world rules never have to be special-cased for logic. Open: how to
    characterize unsealed jitter per hop, and how a sealed machine's
    achievable clock is surfaced to the player. Under load a sealed machine slows only as a whole
    (`sim/subsystem-rate-degradation`); machines spanning chunks are U7 in
    `sim/execution-use-cases`.
  - **Soft logic, projected into the world** (a look-ahead): much softer
    than Minecraft's redstone. No instant lines (a
    dropper line moving items in zero time), no depth-first recursion, no
    zero-delay propagation. **Unsealed contraptions run fully within the
    ordinary physics,** with no dedicated logic-tick system. The design work is
    to project these constraints into the world diegetically: **which
    components make this mode of operation tolerable**, components whose
    purpose is sampling, latching and debouncing. Contraptions are
    then engineered for timing tolerance, as real ones are; one that only
    works with exact timing is an in-world race condition. Candidate
    components (proposed, with real-world precedents):
    - **latch** (bistable, set/reset): holds a state so a brief pulse isn't
      lost (a relay with a holding contact, a mechanical toggle);
    - **debounce / filter:** ignores changes shorter than a time constant, so
      racing signals don't glitch (a dashpot, an RC delay);
    - **sampler / strobe:** reads an input only when a clock says so, giving
      synchronous behaviour from asynchronous parts;
    - **delay line:** a deliberate, roughly fixed delay for sequencing;
    - **clock / oscillator:** periodic, with some drift (a pendulum, an
      escapement, a water clock);
    - **handshake / ready signals:** "don't release the next item until the
      last arrived", asynchronous design that tolerates any delay;
    - **mechanical interlocks, ratchets and escapements:** enforce order
      physically, so it doesn't depend on timing.
    Sensors such as pressure plates are physics components that sample
    level-triggered at their check rate. Sealed machines remain the way to get
    precise, fast logic, at their diegetic cost.
- Open (see `open-questions`): the declining-budget/scheduler model specifics;
  the canned I/O module (capability) catalog; whether evaluation is live per-tick
  vs. further compiled.

## sim/seasons — Seasons as a season-invariant core + per-block seasonal specialization

- Goal: temperate biomes whose temperature and appearance (foliage, snow cover)
  vary over a virtual year — survival challenge, visual interest, sense of time.
- **Season = a global per-dimension clock** (position in a virtual year), a pure
  function of world-time (reproducible, shared in multiplayer, no per-player
  state). Having seasons / the seasonal model is a `worldgen/dimension-profiles`
  parameter (Terra yes; a cave or pocket dimension may not).
- **Temperature** = the static baseline `temperature` field (`worldgen/noise-fields`)
  + a seasonal/latitudinal offset computed **at read time** (a closed form of
  world time, `core/closed-form-first`). Not re-baked into the
  field, not stored. Survival effects read this authoritative temperature; it is a
  cheap derived scalar but IS gameplay-affecting, so it sits on the authoritative
  side even though seasonal *visuals* are presentation.
- **Worldgen core is season-invariant; seasonal appearance is block
  SPECIALIZATION** — the same mechanism as `world/progressive-lod`. Core gen emits
  season-invariant **superclasses** (generic leaf, generic ground cover);
  specialization resolves superclass → seasonal member (light/dark green, fall
  colors, bare, snow-covered) keyed on the season clock + temperature. Bounded by
  the refinement-hierarchy invariant (a superclass resolves only into its members).
  Season is thus "the season axis of block specialization," not a separate
  subsystem — specialization context includes BOTH LOD and season. Snow
  cover in particular is never materialized by random-tick checks
  (`sim/random-ticks` catalog) unless a phenomenon is chosen to be "real".
- **Decay-orthogonality is now STRUCTURAL, not a rule.** Generation emits the
  season-invariant superclass, so seasonal state is never part of what a chunk
  persists for `persist/diegetic-persistence` — there is nothing seasonal in the
  generation baseline, so seasonal appearance (derived at read time from the clock
  + hash) inherently cannot enter a chunk's persisted alterations, and seasonal
  cycling never looks like monotonic decay.
- **Soft transitions** (`core/soft-thresholds`): snow/foliage fade in gradually and
  patchily as temperature crosses a band — never a binary snap at a threshold, no
  flicker as the seasonal offset drifts temperature across it.
- **Two planes of a block:**
  - *Authoritative existence* (it is a leaf block, it is there, part of the world
    structure) — season-invariant.
  - *Perceptibility / tangibility* (visible? solid? targetable?) — seasonally
    specialized.
  Rule: **structural operations act on the AUTHORITATIVE plane; rendering and
  collision consult the SEASONAL plane.** Chopping a winter tree removes its
  authoritatively-present leaves regardless of visibility (the ghost-leaf fix).
- **"Intangible" seasonal block (e.g. winter leaf) = present-but-imperceptible,
  and specifically:** not visible, no collision, NOT targetable — perception,
  movement, and targeting treat the cell as empty — **but still authoritatively
  present and OVERWRITABLE: placing a block there overwrites and removes it**
  (mental model: Minecraft tall grass / a replaceable block). This dissolves the
  spring-reconciliation problem by construction: the overwrite is eager at
  placement time, so there is no lingering authoritative block to "return" in
  spring.
  - A static in-world block is NOT a ledger entity (it is world shape), so
    overwriting an intangible leaf has nothing to do with `data/entity-ledger` —
    no sink, no mint, no conservation event; it is just shape being overwritten,
    like replacing any block. (The ledger only ever engages when a *harvest*
    turns seasonal material into a free entity — gatherable snow → a snow item —
    which is the ordinary extraction mint, not an overwrite.)
- Open (see `open-questions`): per-phenomenon **reality level** — purely
  perceptible (cosmetic frost) vs. present-but-imperceptible-overwritable (winter
  leaves) vs. seasonally real/harvestable (gatherable snow). The interaction
  differs per phenomenon; conservation only enters for the harvestable case (at
  harvest, as an ordinary extraction mint).

## sim/player-embodiment — The player is a soul bound to a body; "player" is the third soul implementation

- Decision: A player is **a soul bound to a body**, exactly like a creature or a
  mechanical soul (`sim/logic-machines`). The body is the in-world physical
  object; the player soul is the third implementation of the sense → act
  interface. There is no separate "player entity" code path distinct from the
  soul/body model — the avatar is a body that happens to have a *human* soul
  behind its sense→act contract.
- **The player soul's two halves (its concrete sense→act binding):**
  - *Sensorium (the "sense" side)* — the body's situation is delivered to the
    human through presentation channels: video (render), sound, text/readouts,
    and whatever else the body affords. This is explicitly a **presentation**
    concern (`light/visibility-only`, `core/authority-derived-split`): the human
    perceives a *derived view* of the body's authoritative situation, not the
    authoritative state directly.
  - *Intentions (the "act" side)* — the human's control surfaces (keyboard, mouse,
    pad, etc.) are mapped to **control-surface-derived intentions** ("move
    forward," "grasp," "go to nearest X") — the same *intention* vocabulary a
    canned or mechanical soul emits. The body resolves intentions into actuation;
    it does not take raw keystrokes. Intention, not input, is the interface.
- **The payoff — body possession/transfer falls out for free.** Because the soul
  is detachable and the body only knows the sense→act contract, a player soul can
  bind a *different* body: possess a creature, pilot a construct, drive a
  mechanical body, swap avatars. Mind-control, remote drones, "ridden" mobs, and
  spectating are all **one mechanic** (rebind the player soul to another body),
  not bespoke features. A body whose soul is swapped from canned-AI to player (or
  back) just changes what sits behind its sense→act contract.
- **Consequences / fits:**
  - Sensorium = presentation means a possessed body's senses are bounded by that
    body's affordances (a mole-body "sees" differently) — a diegetic, not
    engine-special, limit, and it lives on the derived/render side so fidelity can
    vary without touching authoritative state.
  - Intention-mapping is the clean seam for input remapping, accessibility, and
    non-human bodies: you remap control surface → intention, and the body is
    unchanged.
  - Multiplayer is "many player souls, each bound to a body" — no new concept;
    the body is authoritative world state, the sensorium is each client's derived
    view, consistent with the consensus/arbitration sim model
    (`coords/f64-authoritative`).
- Open (`open-questions`, `sim/embodiment-boundaries`): what happens to a body
  when its player soul detaches (does a canned soul take over, does it go inert,
  is it ledger-tracked?); which intentions are universal vs. body-specific; how
  sensorium fidelity is negotiated per body; the soul↔body alignment constraint
  (`data/soul-body-alignment`) applied to player souls (can any human drive any
  body?).

## sim/dreaming-designation — Sleep is a need; dreaming is a spectator soul-rebind that captures intent (no night-skip)

- Decision: Sleep is a real gameplay need (a metabolic pressure, sibling to
  `pressure/basic-survival`), NOT a time-skip. We explicitly reject Minecraft's
  "sleep fast-forwards to morning" — it breaks the shared world in multiplayer.
  Instead, sleeping detaches the player soul into a **dream**: a bounded
  **spectator** mode over the nearby world — a free-moving, non-acting observer
  perspective you cannot get in normal embodied play — during which the player
  can **designate / capture intent** (Dwarf-Fortress-style "mark it now, the
  waking body does it later").
- **This is a soul rebind, not a new subsystem** (`sim/player-embodiment`): the
  dream is the player soul detaching from the sleeping body and binding a
  *spectator observer* instead of another body. Its sensorium is an observer view
  (free-fly, no collision, possibly see-through-terrain); its intention set is
  **restricted to designation** — it emits plans, not actuations. The body sleeps
  in the world as authoritative state the whole time; the shared clock never
  pauses (consistent with `persist/presence-not-absence`'s "no unilateral world
  pause" and the consensus-pause stance). It is the appealing concrete answer to
  one branch of `sim/embodiment-boundaries`: a detached player soul can enter a
  bounded spectator state rather than only going inert.
- **Designation is deferred-intent capture** (`core/diegetic-cost`): dreaming is
  the diegetic act that PRODUCES a plan; the designation is the engine's
  permission to queue work. You spend sleep-time (a need you had to satisfy
  anyway) and the captured designations are the payoff — the reward for sleeping
  well, which is what keeps dreaming from being a free power.
- **Why it fits rather than bolts on:** sleep-as-need gives dreaming a cost
  (`pressure/*`); soul-rebind gives it a mechanism (`sim/player-embodiment`);
  diegetic-cost gives designations a clean "plan now, body executes on waking"
  model; and because it is a local rebind-to-observer and not a time advance, it
  sidesteps the Minecraft multiplayer problem by construction.
- **Dreaming is where elaborate build planning lives**:
  Litematica-style modes fit the dream's local free movement and
  designation-only intentions, so the waking game needs no separate build
  mode. Sketch (proposed, open):
  - **Schematic projection:** place a design as a ghost overlay anywhere in
    dream range, rotated and mirrored; designs are player-authored,
    content-addressed objects (`data/authored-content`).
  - **Region selection and copy:** select a region of the anchored world and
    capture it as a design.
  - **Layer-by-layer view and see-through terrain** for planning, within the
    dream leash (`sim/dream-leash`).
  - **Material list:** what the design needs, against what the player holds,
    with palettes (`tools/catalog`) for mixed fills.
  - **Output is designations, not edits:** the ghost persists as soft-state
    designation (`sim/designation-authority`) that the waking player, or
    later workers, build against. Awake, the ghost is a guide visible in
    preview mode.
- **Open — the two knife-edges (`open-questions`):**
  - `sim/designation-authority` — a dream-designation should be a **plan only**
    (a queued intent the waking body must still physically carry out, subject to
    reachability / resources / pathing — the `sim/player-embodiment`
    intention-vs-actuation seam), never a direct world effect. Leaning plan-only;
    confirm, and define how designations queue, expire, and are executed on
    waking (and the no-acting-while-asleep multiplayer guarantee that follows).
    Candidate: designations are coordination state, so soft
    state with a process-derived TTL (`core/soft-state`), refreshed by
    the planner and executed by level checks (`core/level-triggered`)
    rather than a queue.
  - `sim/dream-leash` — "very nearby" and the spectator's powers need a diegetic
    bound or dreaming becomes a free scry/X-ray tool that trivializes
    exploration. Leaning: dream range/clarity is bounded by the **stabilization
    field** (`persist/diegetic-persistence`) with a soft falloff
    (`core/soft-thresholds`) — you can dream-explore the reality you have
    anchored, not scry unexplored/unstabilized world — so the unique perspective
    is tied to the base you built, not handed out as omniscience.

## sim/cpu-parallelism — Parallelism is a pool of workers running actor turns, not thread-per-subsystem

- Decision: concurrency is a **pool of worker threads that pull work**, not a
  fixed split of threads by subsystem (no "worldgen thread", no "physics
  thread"). Every work item a worker runs is an actor's turn, pulled from a
  source (`engine/dispenser`). `core/chunk-locality` makes chunks
  independently schedulable, so chunks' turns (in a Vicinity) and goal
  actors' turns (generation, refinement and meshing of terrain nodes,
  `engine/goal-source`) are the parallel work by construction. Idle workers
  take whatever is runnable.
- **This is the execution model; `world/chunk-scheduling` is the policy on
  top** (what runs next when oversubscribed). This says how work runs in
  parallel, that says in what order; they stay separate.
- **No work-stealing:** per-worker deques would fight the single, live
  priority order across sources; the Dispenser ranks across sources when
  work is taken, and its contention is negligible at millisecond-scale
  turns (`engine/dispenser`).
- **Memory layout is the real lever, not the thread count.** Parallel
  simulation is memory-bound, not compute-bound: the win is data-oriented
  design (structure-of-arrays, contiguous cache-friendly data, minimal
  pointer chasing), so workers are not stalled on latency. Consistent with
  `coords/axis-convention` (horizontal axes as inner strides for the hot
  sweep) and `world/brick` (one tight, pointer-free cell container).
- **Lock-free in the hot path:** each chunk is changed only in its own
  turns, with conserved cross-chunk work as conserved messages and Vicinity
  transactions, and other threads read it by validated zero-copy reads
  (`sim/concurrency-model`).
- **Verification:** lockstep mode runs one worker alternating with the
  frame thread as the reference; a run with many workers must match it
  exactly for generation, edits and conserved state and converge for soft
  simulation (`sim/concurrency-model`), plus ThreadSanitizer builds.
- **Worldgen split (the CPU-versus-GPU question for generation):** the
  **authoritative discrete decisions stay on the CPU** (integer-domain,
  reproducible, `core/worldgen-integer-domain`), because GPU floats are not
  bit-reproducible. The **continuous candidate math (field sampling) may run
  on the GPU** as an accelerator (`sim/gpu-speculative-compute` permits GPU
  noise as a candidate generator), but a GPU float must never gate a
  discrete generation outcome. **Note (`sim/gpu-speculative-compute`
  scoping):** the GPU-candidate-noise path is *shelved for now* under the
  no-server-GPU premise (worldgen runs fully on the CPU today); even if
  revived it stays reproducibility-leashed (candidate only, re-derived and
  validated in the integer domain), unlike fluid-GPU, which is a clean
  accelerator.

## sim/mob-navigation — Mob movement splits approximate off-thread planning from authoritative in-lockstep actuation; pathing is deliberately heuristic, not optimal

- Decision: mob movement is governed by two explicit halves, and the split is the
  same propose-against-snapshot / reconcile-against-authority spine the engine
  uses elsewhere (client prediction, `net/client-server-seam`; mobs crossing
  seams are U5 in `sim/execution-use-cases`):
  - **Path PLANNING (intention)** — "get to roughly *there*." Computed **off the
    hot path, on a worker thread, against a slightly STALE/imperfect snapshot** of
    the world. Its navigable-space input is the pather's **local direct-read of
    geometry up close, heuristic at distance** (`sim/mob-perception`) — NOT a
    global navigation graph and NOT the mob's perceptual memory — which is why the
    plan is local and heuristic by construction. Latency-tolerant and approximate:
    a plan may be several ticks old and that is acceptable, even desirable (see
    below). This is the mob form of the control-surface-derived **intention** a
    player soul emits (`sim/player-embodiment`) — a canned/mechanical soul emits a
    *navigation intention*, not actuations.
  - **Path IMPLEMENTATION (actuation)** — the body executes the plan in **small
    discrete steps that MUST be in lockstep with authoritative world physics**.
    Each tick the body takes the next step toward the planned waypoint *only if
    current occupancy/collision permits it*. If the world changed under the stale
    plan (a block placed, a bridge decayed, another body now blocking), the body
    stalls / stumbles / re-requests a plan rather than clipping through. **The
    plan proposes; physics disposes.**

- **Pathing is deliberately HEURISTIC, not globally optimal — a believability
  constraint, not a performance one.** Global A*-style optimal pathing is
  *rejected* for ordinary mobs because it is uncanny: a mob that always takes the
  perfect route reads as having a map of a world it should not know. Most mobs
  (robots/constructs aside) are **not puzzle-solvers** — they navigate like
  animals: greedily toward the goal, around what they can locally see, sometimes
  into a dead-end they must back out of. This is also a **fairness/readability**
  win: an occasionally-dumb route is legible and beatable; a perfect pursuer is
  neither.
  - **Path quality DEGRADES with range, and the degradation is both forced and
    wanted.** Long range = coarse, greedy, "head toward the player's last-known
    (stale) position"; close range = sharper, reactive, actually avoids obstacles.
    This is *forced* by `core/chunk-locality` + `world/progressive-lod` (you cannot
    run a fine global solve over distant coarse/unloaded chunks anyway — cheap
    heuristic is the only honest option at range) and simultaneously *wanted* for
    believability. The two agree, so "mobs aren't long-range puzzle-solvers" is a
    structural property, not a tuning choice.
  - **Capability is a per-SOUL property, not a global setting.** "How good a
    navigator is this thing" is a trait of the soul behind the body
    (`sim/player-embodiment`, `data/soul-body-alignment`): a dumb animal, a clever
    predator, and a robotic pathfinder are different souls, so the *robots-aside*
    exception is expressed as a high-capability soul, not a special code path.

- **Staleness is a FEATURE on the planning half.** A plan computed against a
  slightly-old snapshot is another honest source of the imperfection above — the
  mob commits to a mildly-wrong intention and discovers the error *locally* during
  actuation (`core/soft-thresholds`: a small world-vs-plan mismatch is an
  imperceptible behavioral error, not a glitch). This is the stale-position mob
  window (`mobs/spawning`) applied to movement, not just spawn probability.

- **Resolves the stale-position OSCILLATION risk by placement.** Local avoidance
  (two mobs not clumping/jittering) belongs to **implementation** — reactive,
  current-tick, against live occupancy — **never to planning**. If avoidance read
  each mob's *latched* position it could oscillate; keeping it on the authoritative
  actuation half means mobs avoid each other against *present* state while their
  coarse intentions stay cheap and stale. Planning is coarse intention; avoidance
  is live.

- **Fits the present-player invariant.** Because mobs exist only in loaded,
  player-adjacent space (`mobs/spawning`, `core/chunk-locality`), planning is
  always *local* to resident space — there is no global cross-world path to solve,
  which is exactly why the heuristic/stale planner is sufficient and the expensive
  optimal solver is unnecessary as well as unwanted.

- Open (see `open-questions`): the heuristic family itself (greedy + limited
  lookahead? flow-field for crowds? local steering?) and how it reads a coarse
  distant representation (`sim/3d-pathing`); the capability-tier vocabulary per
  soul; the concrete 3D search shape over the voxel volume (the O(n³) volumetric
  problem `sim/3d-pathing` raises); and the plan-handoff contract (how a stale plan
  is invalidated/re-requested when actuation finds the world changed).

## sim/mob-perception — Mobs act on timestamped, uncertain PERCEPTS, never ground truth; sensors are per-body soft-threshold filters; stimuli ride the event bus and are culled by the present-player invariant

- Decision: a mob's decision layer reads **percepts** — timestamped, positioned,
  *uncertain* observations — and the beliefs it accumulates from them, **never the
  authoritative world state directly**. This single indirection (decide on belief,
  not truth) is what buys believability, stealth, fairness and readability; wiring
  decisions to ground truth is exactly the omniscient-AI failure mode that makes
  the uncanny pursuer (`sim/mob-navigation`'s heuristic-not-optimal rationale, the
  perceptual twin).
  - **Percepts are the universal currency.** A vision percept and a sound percept
    are the same *kind* of thing (what, roughly where, how sure, when observed), so
    the decision/planning layer consumes one percept stream regardless of channel.

- **The planner's staleness is a CONCURRENCY mechanism, not a perception feature —
  and must stay below the perceptibility floor.** The stale snapshot the planner
  reads (`sim/mob-navigation`, `sim/cpu-parallelism` double-buffering) exists to
  make planning **lock-free** — read a snapshot without locking the authoritative
  world — NOT to manufacture believable error. Its magnitude is budgeted to be
  **sub-perceptible given the imperfections already present** (heuristic pathing,
  soft spawn/position windows), so concurrency-lag never *compounds* with belief
  uncertainty; it hides beneath it. The believable imperfection comes from the
  heuristic planner and from percept uncertainty below, not from planning lag.
  (Corrects an earlier conflation: lag = the price of lock-free concurrency, kept
  under the noise floor; it is not itself the source of believability.)

- **Vision = aggro range + field-of-view cone + a basic visibility (line-of-sight)
  check. No foveal/peripheral graded vision** — range + cone + LoS is close
  enough, and the `core/soft-thresholds` edge on the range/cone boundary
  already removes the binary-aggro pop (no snap from oblivious to omniscient at an
  exact radius).
  - **Sensor parameters are VIRTUALIZED as body/soul capabilities.** FOV angle,
    range, and whether the visibility check runs at all are **per-body
    properties** (`sim/player-embodiment`, `data/soul-body-alignment`): perception
    is part of what a body affords its soul, the sense-side analog of the player
    sensorium. So "poor-sighted mob," "blind hearing-only cave mob," and
    "flashbanged/blinded mob" (temporarily zero the vision range) are the **same
    sensor with different parameters**, never special-case code. One parameterized
    mechanism, not a family of vision systems.

- **Sound is timestamped level state, culled when there are no listeners
  (draft; no detailed sound design is accepted yet).** A sound is a
  decaying trace (`sim/process-kernels`) at a position with an intensity,
  published in the marker map (`sim/block-metadata`) and read by listeners'
  checks (`core/level-triggered`, `core/soft-state`), so it propagates
  within the speed of information and needs no separate perception bus. A
  listener hears it when intensity − distance-falloff clears that body's
  hearing threshold (`core/soft-thresholds` soft edge), and hearing yields
  a *position estimate*, not an identity.
  - **The listener-cull is a free consequence of the present-player invariant, not
    a special optimization.** Sound matters only to *mob* listeners (the player
    hears through their own presentation channel, `light/visibility-only`, not this
    system), and mobs exist only in loaded, player-adjacent space
    (`mobs/spawning`, `core/chunk-locality`). So where there are no mob listeners
    there is nothing to post to and the event is elided by construction — the same
    "nothing simulates where nothing listens" posture as lazy decay. Emission can
    be lazy: a sound-making action skips posting entirely if no listener sensor is
    within max plausible range.

- **Pathing geometry access is a DELIBERATE perception seam: the pather reads
  LOCAL geometry directly (not from vision, not from memory), and goes heuristic
  at distance — a conscious performance-for-purity trade.** We explicitly reject
  routing pathing through a faithful perceptual/memory model: a mob memory rich
  enough to *path from remembered geometry* is too much state/cost per mob, and is
  not built. So the pathing algorithm's access to geometry is **distinct from what
  the mob sees / has seen**, and that mismatch is tolerated:
  - **Up close — direct local read.** The pather queries the actual occupancy of
    the cells around the body. Cheap (local + current, a handful of cells), no
    memory required: the mob navigates its immediate surroundings competently
    because the data is right there, not because it remembers.
    (`core/authority-derived-split`'s render-only rule takes a **scoped exception** here — see the
    re-scoped fairness bound below.)
  - **At a distance — heuristic only.** The pather gets *no* accurate remote
    geometry: it is "head roughly toward the goal," which is exactly where the
    believable imperfection lives (`sim/mob-navigation`'s heuristic-not-optimal
    stance).
  - **The governing axis is `local → remote`, a `core/soft-thresholds` falloff**
    (direct-local-read fading to heuristic-remote over distance, not a hard
    radius). This is the one tuning knob for the seam.
  - **Fairness re-scoped from "senses only" to "local-direct / remote-heuristic."**
    The structural guarantee is no longer "a mob acts only on rendered senses"
    (the pather breaches that by reading local authoritative geometry); it is that
    the breach is **local only**. A local direct-read is imperceptible anyway — you
    cannot tell from outside whether a mob *sensed* or *read* the floor it stands
    on. The uncanny case (pathing perfectly around distant unseen obstacles) is the
    *remote* case, which stays heuristic — so a mob still cannot route around a
    corner it has no local access to and must go look. Same believable "discover
    the dead-end locally" outcome as before, now sourced from "no remote geometry"
    instead of "no remote memory," and far cheaper.
  - **Vision/hearing still drive TARGETING and decisions** ("there is a player,
    go toward them"); the pather independently handles *how to move through local
    space*. The two are separate on purpose — perception feeds the decision layer,
    local-geometry access feeds the pather, and they are not reconciled into one
    render.

- **No persistent world-belief — the model is short-horizon.** Because mobs
  threaten only the *present* player and are not simulated in absence
  (`mobs/spawning` invariant), a mob needs no cross-absence or cross-session
  memory: percepts + a decaying last-known estimate + a confidence scalar, scoped
  to the current encounter. The invariant hands this simplification over for free —
  the belief *indirection* is required for believability, but long-term memory is
  not.

- The through-line: **percepts are the currency, sensors are per-body
  soft-threshold filters, stimuli ride the existing event bus and are
  listener-culled, pathing reads LOCAL geometry directly (a scoped seam, heuristic
  at distance) while perception feeds the decision layer, and that decision layer
  reads beliefs, never truth.** Smaller
  than full Alien-Isolation sense modeling precisely because the present-player
  invariant removes persistent world-belief.

- **Perception is RENDERING, uniform across souls — the authoritative-vs-
  presentation "fork" is resolved as there being no fork
  (`core/authority-derived-split`).** A mob sensor does NOT get a privileged
  introspection path into authoritative state that the player lacks; both player
  and mob are *consumers of a render*. The player receives a high-fidelity,
  human-consumable render (pixels/audio); the mob receives a low-fidelity,
  machine-consumable render (percepts). Same transform — project authoritative
  state through the consumer's affordances into a view it can act on — different
  output target. This keeps the sense→act contract uniform across all three soul
  types (`sim/player-embodiment`) and makes a mob's percepts literally its
  sensorium. The earlier asymmetric option (mobs read authoritative, players read
  presentation) is rejected: it would be a second, privileged read path into truth,
  exactly what the split exists to prevent.
  - **Perception is bounded by declared body senses.** A soul's only input is its
    body's rendered senses, so a "cheat" sense is an **affordance, not an AI
    privilege**: wall-sight / X-ray is a legitimate body capability — a sensor with
    the occlusion term dropped, the same per-body parameterization as `range=0` for
    blinding. What is unrepresentable is *un-declared* omniscience (perception with
    no sensor behind it), not exotic senses. A wall-sighted mob stays legible,
    bounded, and counterable, and "finds-you-anywhere" becomes a content decision
    rather than a code special case. (Full statement in
    `core/authority-derived-split`.)

- Open (see `open-questions`, `sim/mob-perception-model`): the concrete
  percept/belief **decay curve** (how last-known-position confidence ages) and the
  alert/investigate/search/idle state ladder it drives; the exact
  concurrency-staleness budget (how many ticks is "sub-perceptible"). Default
  for the decay curve: a closed form in T − t_seen (exponential, the
  decaying-trace shape of scent; `core/closed-form-first`,
  `sim/process-kernels`).

## sim/mob-decision — The canned-soul decision layer is utility/need-based: soft-weighted drives score candidate intentions; interrupts emerge from the numbers, not hard-coded rules

- Decision: a mob soul's **"act" side** (`sim/player-embodiment`'s intention
  output) is a **utility / need-based evaluator**, not a behavior tree or FSM. The
  soul carries a set of **drives** (hunger, thirst, fatigue, fear/injury-avoidance,
  curiosity, …) that integrate over time; each drive maps through a **response
  curve** to a motivation; candidate intentions are scored from the current drives
  + percepts/beliefs (`sim/mob-perception`); the highest-scoring intention is
  emitted and handed to the planner/pather (`sim/mob-navigation`). This is the
  concrete form of the canned-soul decision layer, chosen because behavior should
  **emerge from internal state over time**, not be scripted — the right fit for
  ambient creatures (the technique behind *The Sims*' needs system). It consumes
  exactly what the belief model already produces and needs no new sensing.

- **Soft-weighted drives, NOT a strict Maslow ladder.** The hierarchy-of-needs
  intuition is right about the *felt* ordering (survival usually trumps comfort
  trumps curiosity) but wrong if taken as a strict priority ladder: a hard ladder
  gives robotic behavior (a hungry animal that won't flee a predator until fed) and
  threshold-flipping dither. Instead the ordering **emerges from the shape of the
  response curves** (`core/soft-thresholds`): survival drives have steep,
  high-urgency curves, curiosity a shallow low one, so the typical Maslow ordering
  falls out *without* being enforced, and a low-but-spiking drive can still win.
  The strict-ladder reading is explicitly rejected.

- **Hysteresis is a per-evaluation COMMITMENT BIAS, not a commit-for-N-seconds
  timer.** The current goal is scored as *its utility + a commitment bonus*, so
  small fluctuations do not cause mid-action abandonment (reads as
  "determination"). A competing drive takes over iff its raw urgency **exceeds that
  sum**. No global "locked in for N ticks" timer — which would itself need a
  hard-coded override.

- **Interrupts EMERGE FROM THE NUMBERS — this is the central feature, not a special
  case.** Being attacked does not trigger a hard-coded "drop everything" branch.
  Instead **injury-avoidance is a drive with an extreme, steep-shouldered curve:
  flat near zero across its whole normal range, near-vertical when a damage/threat
  percept arrives** — "usually inactive, overwhelming when active." When it fires it
  scores well above *any* current goal + commitment bias, so it clears hysteresis
  and wins instantly. Hysteresis (resist mild competitors) and interrupt (yield to
  a spiking strong one) are therefore **the same comparison at two urgency
  levels**, no mode switch:
  - The interrupt trigger is a **percept** (damage taken, attacker in view/
    earshot — `sim/mob-perception`), so the break-out is driven by the perception
    model, never by privileged "am I being attacked" access. Attacked *is* a
    percept → drive spikes → clears bias → flee intention → pather. The whole chain
    is pieces already built, in series.

- **Temperament is emergent from two numbers, per-soul — the same virtualization
  as sensors/drives.** "How interruptible / committed is this creature" is a point
  in **(commitment-bias × drive-curve-steepness)** space, not a behavioral mode: a
  skittish prey animal = low bias + hair-trigger fear curve (bolts at anything); a
  focused predator or berserk mob = high bias + possibly *suppressed*
  injury-avoidance (fights through pain). Stubborn / jumpy / fearless /
  single-minded are tunings, not scripts.
  - **Drive set scales with mob TIER** (`mobs/spawning`'s immune-response): low-tier
    mobs have one or two crude drives with shallow curves; high-tier mobs have
    richer drive sets and sharper curves (more "intelligent"-looking). A mechanical
    soul may carry an **authored goal** in place of biological drives — just a
    different drive set, consistent with "robots aside" from `sim/mob-navigation`.

- **Short-horizon, consistent with the no-persistent-memory stance
  (`sim/mob-perception`).** Drives are current-state scalars that integrate over
  the present encounter, not a remembered history; percepts/beliefs supply the
  external triggers. No long-term memory is introduced. "Integrate over time"
  means closed form, not stepping: each drive is a closed-form anchor (`core/closed-form-first`)
  (`core/closed-form-first`) evaluated when the decision layer reads it, and
  re-anchored on a percept or when the need is satisfied (`sim/random-ticks`
  catalog).

- Open (see `open-questions`, `sim/mob-decision-tuning`): the **drive vocabulary**
  (the actual list, which drives are universal vs. body-specific) — tuning-heavy,
  likely perpetual; and the **commitment-bias magnitude** relative to drive-curve
  steepness (the knob that sets where "determination" ends and "stuck/robotic"
  begins).

## sim/queued-updates — Updates are queued and prioritized, never recursive; overload slows subsystems, not the world

- Decision: **every kind of block-state update** (fluids, logic machines,
  falling blocks, neighbour reactions; growth is closed-form, never
  stepped or queued, `core/closed-form-first`) follows a snapshot
  discipline: read the last committed state, write the next, swap. There are **no in-place neighbour updates and no recursive or
  DFS propagation** anywhere in the sim. An update that causes further updates
  enqueues them on an **explicit, prioritized work queue** with bounded work
  per tick.
- Why (the Minecraft lesson): redstone's location- and direction-dependence
  and quasi-connectivity come from order-dependent in-place updates. "Update
  suppression" (a recursive neighbour-update chain overflows the stack, the
  tick aborts halfway, and the world is left inconsistent) comes from
  recursion. Both are impossible by construction here: results can't depend
  on visit order, and there is no stack to overflow.
- **Write conflicts: integer merges and hash arbitration.** Reads come from
  the last committed state, so read order is
  irrelevant; order can only matter where two effects WRITE the same target:
  - **Quantities** (fluid amount, heat, momentum) merge by **integer
    addition** — commutative and associative, so neither phase, thread nor
    queue order can change the result (and integers are what conservation
    wants anyway). Floats are not associative and are not used for merged
    authoritative quantities.
  - **Discrete conflicts** (two falling blocks into one cell, two mobs into
    one space) are won by the highest `hash(world seed, tick, cell,
    candidate)`.
  - Result: **unpredictable to the player** (no direction, position or order
    bias to notice or exploit — the wires don't show) and **deterministic for
    us** (same seed and inputs → same world on any thread count, so the
    lockstep-mode test holds). Scheduling structure — colour order,
    executor-grid position, thread count — has no visible effect, so none of
    it needs randomizing. (Colouring survives only as a possible fallback,
    `attic/sim-execution-alternatives.md`; the point stands for turn order
    and thread count.) Same move as
    `worldgen/metered-information`: variety
    from a hash, free and reproducible. (Considered and rejected: randomizing
    colour order and grid offset per tick — it still leaks executor-grid
    seams and makes results depend on scheduling.)
  - **Exception: logic machines,** whose builders want predictability — see
    the sealing rule in `sim/logic-machines`.
- **Overload policy: subsystems slow down, the world doesn't lag.** When work
  exceeds the budget, each subsystem's queue drains at a bounded rate and
  falls behind (fluids flow slower, growth lags) while the world clock and
  player responsiveness hold. Subsystems degrade independently. Same shape as
  `world/chunk-scheduling`'s prioritized frontier. The mechanism is stepped
  subsystems skipping steps and soft-simulation checks running at a lower λ
  (`sim/subsystem-rate-degradation`); growth is closed form and never lags.
- Consequences:
  - **Queue health is a first-class metric:** per-queue size and age of the
    oldest item (F1 overlay, telemetry). Sustained growth is a signal to
    investigate, not just load.
  - **Coalescing** is the main structural bound: a cell queued twice for the
    same kind of update is processed once, so a queue is bounded by the cells
    it covers, not by the number of triggers. (Conditional on whatever still
    uses queues after re-derivation; for markers the bound comes from state —
    last-writer-wins, `sim/block-metadata`.)
- **Under revision: `core/level-triggered`** makes
  level-triggered, idempotent checks the default and explicit queues the
  fallback. The snapshot discipline, no-recursion rule and conflict rules
  above stand; which update kinds still need queues (vs. closed-form state
  plus checks, `sim/random-ticks`) is to be re-derived.
- Open: per-subsystem priority and budget split, now governed by the
  fair-share tenet (`core/fair-share`: guaranteed minimum shares, burst
  credit settling to equilibrium, per-entity rather than per-subsystem); exact
  coalescing semantics.

## sim/speed-of-information — Effects propagate at most one chunk per tick; the sim's speed of light

- Decision: a bounded effect reaches **at most 64 cells (one chunk) past its
  home per tick**. This is the simulation's speed of information.
- **Why it's worth committing to:** every effect a step produces lands in the
  home chunk or one of its **26 neighbours**, so cross-chunk interaction is
  always local: a chunk calls only its neighbours' APIs, an arrow's sweep
  touches only adjacent chunks, and nothing needs the Vicinity for reads
  (`sim/concurrency-model`). Every bounded effect fits; only declared
  exceptions go beyond.
- **In world units:** 64 cells × tick rate per second. At 20 Hz: 1,280 units/s ≈
  390 m/s (at `world/voxel-scale`'s foot). At 10 Hz: 640 units/s ≈ 195 m/s. At the 60 Hz base tick of
  `sim/multi-rate`, 3,840 units/s ≈ 1,170 m/s. Either way it covers terminal
  velocity (~55 m/s), fluid flow, thrown objects and arrows (50–100 m/s) and
  field/effect propagation. At low tick rates **sound is slowed** below its
  real 343 m/s (to ~57% at 10 Hz); accepted, because players won't notice a
  variance in the speed of sound.
- **The limit is a ceiling; the effective speed is variable**. How fast
  information actually travels depends on how each
  subsystem is simulated: its rate (`sim/multi-rate`; a subsystem stepping
  at 20 Hz moves information at most 1,280 units/s), skipped steps
  (`sim/subsystem-rate-degradation`), and whether a chunk is simulated at
  all. Unsimulated chunks already implied this; the rule turns a hard drop
  to zero into a variable rate below the ceiling.
- **Cellular and field effects are clamped; entities may exceed the limit by
  spilling to global.** Fluid, fields and other
  propagation travel at most at the limit. An **entity** may move faster than
  the speed of information, sparingly: it simply runs in the global scope (or
  a region, `sim/regions`) for as long as it does. Correct, just not
  parallel — so the risk is scale, not correctness.
  - **Hitscan and bullet-speed projectiles are swept rays**
    (`sim/swept-collision`), resolved without propagating cell by cell; they
    aren't the concern.
  - **The concern is gameplay that drives many entities over the limit:**
    physics tricks that propel the player or entities very fast (launchers,
    explosion-propelled contraptions — the TNT-cannon class that other voxel
    games already have to police), and any item whose normal use crosses it.
    Every such feature is a design-time question: does it stay rare?
  - **Watched, cheaply:** the number of entities in the global spill scope and
    the share of the tick budget they take are health metrics
    (`data/cheap-invariants` style counters, F1 overlay), so growth is noticed
    rather than discovered as lag.
  - Genuinely instant effects should preferably be redesigned with a finite
    propagation speed rather than escalated.
- **A limit on reach, not a sub-step count:** an effect moves as far as its
  physics says in one computation inside its chunk's step; it is not stepped 64
  times.

## sim/swept-collision — Entity-vs-terrain collision is swept; no speed passes through a solid cell

- Decision: a moving entity's box is advanced **through the voxel grid along
  its motion** (an Amanatides–Woo-style traversal generalized to the box's
  leading faces), not tested only at its end position. No speed can tunnel
  through a solid cell. Projectiles are the ray case of the same traversal.
- **Cost: nothing extra at normal speeds.** If per-tick displacement on every
  axis is under 1 cell (the smallest feature), testing the cells the box
  overlaps at the end is already exact, so the sweep only runs when a
  displacement reaches 1 cell. Its cost is then proportional to cells crossed
  × the box's cross-section in cells (a player-sized box touches roughly
  2×2×6 cells): inherent to the distance moved, and the grid needs no
  broadphase.
- Entity-vs-entity contact stays discrete and soft (mobs jostling) unless a
  fast entity class needs more; projectiles sweep against hitboxes.
- Why: tunnelling through thin walls at high speed is a classic, recurring
  voxel-game bug class; this removes it rather than tuning speed caps.
- **A sweep that crosses a chunk seam:** the sweeping
  chunk calls the neighbour's thread-safe sweep API, which computes the hit
  against its own geometry, so no chunk reads another's interior
  (`core/opaque-chunk-queries`; the arrow worked example in
  `sim/concurrency-model`). The speed of information bounds a sweep to the 26
  neighbours.

## sim/block-metadata (draft) — A cell's authoritative state is its block state; there is no per-block metadata, only hidden simulation state and world-level records

- **Open:** which processes keep hidden
  state, and what each loses on unload; how world-level records are owned,
  collected and replicated; the marker map's refresh cadence.

- Decision: **a cell is exactly its block state.** The Brick holds every
  persisted, digested and replicated fact about a cell, as a 32-bit state id
  into the process-wide state table (`world/brick`). There is no freeform
  per-cell side table. This keeps chunk content tight and pointer-free, and
  keeps concurrent reading of a chunk to the Brick's one validated-read
  path (`world/chunk-representation`), with no second structure to keep
  consistent with it.
- **Variety and timing come from closed forms, not stored data:** a
  process's official behaviour at a cell is a function of (the simulation
  key, state id, cell coordinates, world time):
  - **events are memoryless hashed streams** (`sim/random-ticks`): a state's
    events (a sapling growing, a crop advancing a stage, a burning block
    burning out) are a hashed Poisson stream on absolute time, keyed by
    (state id, coordinates) and the time block (`sim/random-ticks`). Because
    the stream is memoryless,
    a cell needs no record of when it entered its state: it advances at the
    first of its events after it is next materialized, and a chunk that was
    absent catches up from its **last-materialized time**, one value per
    chunk persisted with it, never per cell;
  - **stages are states:** "how far along" is the block state itself (a
    sapling at stage 2, a cut reed stub regrowing, a fire burning), with a
    small closed domain per definition;
  - **an event's outcome is keyed by its official time:** what an event
    produces (the tree a sapling becomes, a drop, a variant) hashes the
    event's time as the closed form gives it, never the time a check
    noticed it, so outcomes don't depend on tick rates or residency. A
    sapling replanted at the same spot grows on a different schedule into
    a different tree, because its events fall at different times; the
    streams themselves never repeat, being fresh randomness in every time
    block. So no per-chunk seed or reseeding is needed;
  - **prediction is prevented by keys, not by reseeding:** the simulation
    key is a server-only branch of the world's root key; clients receive
    derived keys for the time blocks they may simulate, not the key
    itself (`sim/random-ticks`). Reloading a backup repeats outcomes,
    which is accepted: a local world is the player's own choice, and a
    server's players can't reload.
- **Hidden simulation state is allowed, never authoritative:** a chunk may
  keep any per-cell data that makes simulation efficient (a schedule of
  next event times, a timing wheel of burning cells, scent pass times, an
  index of cells with pending events), private to the chunk's own turns
  (`ActorOwned`), never read by another thread, never persisted, digested
  or replicated. It is either derivable again from block states and the
  closed forms, or soft data whose loss on unload is acceptable (scent
  fades, a trace is forgotten). Losing it can only delay or blur an effect,
  never change what the closed forms say.
- **Rare content too rich for a state is a world-level record a state refers
  to:** a written book is a new state of the book block whose property is a
  reference to a centrally allocated record, persisted at the world level
  and named by its ULID; sign text likewise. Reference properties are the
  one exception to small closed domains (`world/brick`'s per-definition
  state cap), allowed only for content that is rare per chunk, so palettes
  stay small. Scarce instances keep their own table (`data/entity-ledger`).
- **No inter-block references; cross-chunk growth is marker-based only**
 . No block's state points at another block. Growth
  inside a chunk is driven by checks and iteration during worldgen; growth
  across chunks happens only through face-layer markers, read by the
  receiving chunk's own checks. A plant's growing tip is a state of its own
  (a tip, with its height class).
- **Cross-chunk markers are only a propagation mechanism**: a
  chunk publishes, in a **sparse marker map** keyed by cell, the projections
  of its face cells that neighbours need, to resynchronize chunks that are
  not always loaded or ticking. The map is readable by all 26 neighbours (no
  separate face/edge/corner grids; a corner entry is just another key).
  **Markers derive from the face layer only**: each is a function
  of the publishing chunk's cells touching the shared face, edge or corner
  (their block states, and hidden state where a soft process publishes it,
  such as scent), never of deeper cells, so reach doesn't depend on the
  kernel (U20). O(surface); empty in the common case.
- **Markers are ephemeral and soft** (`core/soft-state`): they can be wiped at
  any time (unload, memory pressure, a bug) and are rebuilt from block
  states on the next refresh. Persisting them is never required. Because
  nothing published carries information that isn't in authoritative state
  or acceptable to lose, loss can't change an outcome or open an exploit.
- **Markers vs. a queue/transaction layer:** markers do
  the propagation job a queue would, but they carry **state, not events**:
  newer state overwrites older (last-writer-wins), so the map can't build a
  backlog (its size is bounded by the boundary band × the number of marker
  kinds, not by how much has happened), and losing it is recoverable,
  because the chunk repopulates it from its own block states. This is the
  precise sense in which it replaces a queue for everything outside the
  hard simulation (`sim/exact-core`).
- **Eager rebuild on load; persistence not needed.** A chunk rebuilds its
  marker map when it loads (cost O(face cells with marker kinds), usually
  zero), so the map exists before any neighbour reads it. With no
  persistence plus eager rebuild, reloads add no perceptible seam.
- **Seams converge to imperceptible; border checks equalize latency.**
  Inside a chunk an effect waits for one check (mean 1/λ); across a seam it
  waits for two in series: the publisher's refresh, then the consumer's
  check (mean 1/λ_pub + 1/λ_con). So the random-tick sampling
  (`sim/random-ticks`) **boosts checks ~2× in the face layer**, bringing the
  seam's mean latency back to 1/λ. Safe because checks are idempotent and
  only materialize state; cost ≈ 6/64 of a chunk's cells, ~9% more;
  deterministic because the weighting is part of the hashed check process.
  **Boost to equalize, not to maximize:** more than ~2× makes the seam
  visible the other way. The residual difference (variance of two
  exponentials in series vs. one) is below perception.
- **Consequences** (of replacing per-cell anchors): use case U20 of
  `sim/execution-use-cases` (growth across a chunk border) reads cuts and
  obstructions as states through markers, and growth across a seam starts
  when the receiving chunk observes the marker; scent trails become hidden state; fire's
  schedule becomes hidden state derived from burning states; continuation
  records for huge trees become tip states (`worldgen/feature-continuations`).

## sim/process-kernels — Soft-state, nondeterministic mechanisms are generic engine kernels, configured per block in the grammar

- Decision: the soft-simulation mechanisms (`sim/exact-core`) — closed-form
  processes, random-tick checks, relaxation fields, published state read at
  checks, timestamped markers with TTLs — are implemented **once, as generic engine
  kernels**, and each block type **declares** which kernels it uses and with
  what parameters in the content grammar (`data/grammar`: a binding agent
  that names and configures engine capabilities, not a programming
  language). No bespoke code per block: wheat, sugarcane and fire are
  declarations, not classes.
- **Why:** correctness is verified per kernel, not per block, so it scales
  with the number of kernels rather than the amount of content; a content
  author can't break an invariant, because the grammar can only bind
  parameters to kernels that already uphold them; and content stays
  diegetically authorable (`core/diegetic-authoring`).
- **Candidate kernels** (from the `sim/random-ticks` catalog):
  - **counter process:** stages advanced by a hashed event count on
    absolute time (crops, saplings, weathering), with declared inputs and
    thresholds (light, moisture, season) and a cap;
  - **hazard process:** an event whose rate depends on neighbours' published
    state (fire ignition, spread of grass or moss);
  - **relaxation field:** a value relaxing toward a neighbour-derived target
    (magic fields, disease, moisture, unsealed logic signal levels);
  - **decaying trace:** a sum of exponentially decaying contributions from
    event times held as hidden simulation state (scent trails), soft and
    lost on unload (`sim/block-metadata`);
  - **extension:** a multi-cell growth across cells and chunks from its
    growing tip, the segment with air above it; across a seam the receiving
    chunk extends it from the neighbour's face-layer markers, starting when
    it observes them (U20);
  - **timed container process:** single-owner arithmetic on counts
    (furnaces, spoilage).
- **Derived, not authored, where the invariant needs it:** a marker's TTL is
  computed by the kernel from the declared parameters (e.g. the full
  regrowth time from the cut for an extension), so expiry is exact by
  construction (`core/soft-state`) and no author can set an unsafe TTL. The
  same goes for anything else an invariant depends on.
- **Illustration** (syntax not decided):
  wheat = counter process (8 stages, native rate r, needs light ≥ 9 and
  moisture); sugarcane = counter process + extension (upward, max height 3);
  fire = hazard process (ignites from burning neighbours at rate k) + decay
  to air.
- Open: the exact kernel set and their parameters; the grammar syntax for
  declaring them; check rates per type (`sim/random-ticks`).

## sim/random-ticks — Random ticks are checks, not drivers: a hashed per-cell Poisson process on absolute time that materializes closed-form state
- **Open:** epoch size, rates per block type and the per-chunk random-tick
  budget.

- **Reference (Minecraft, from memory):** a global game-time counter per world
  advances every server tick; chunks store `LastUpdate` and `InhabitedTime`
  (the latter drives local difficulty) but nothing catches up. Every tick, each
  ticking 16³ section picks `randomTickSpeed` (default 3) random blocks, and
  blocks with random-tick behaviour react (crops advance, leaves decay, ice
  melts, fire spreads). Lazy and unloaded chunks get none, so farms only grow
  while a player is near. Elegant, but it pays every tick for blocks that
  mostly do nothing.
- **Our times:** a global world time T always advances while the server runs
  (`coords/typed-time`); stepped subsystems keep per-(chunk, subsystem) step
  counters (`sim/subsystem-rate-degradation`); closed-form processes keep a
  per-process anchor t₀ (`core/closed-form-first`). No per-chunk "last
  updated" time is needed: a closed-form value is a function of T and its
  anchor, whether or not the chunk ticked in between.
- Decision: **a random tick is an event of a per-cell Poisson process** with
  rate λ (Minecraft's default is ≈ 3/4096 per cell per tick). An effect that
  fires with probability p per tick is the thinned process with rate λp.
- **Realized on absolute time, never per evaluation** (`core/closed-form-first`'s correctness
  condition):
  - **Fixed epochs** (e.g. 1,024 ticks) with a hashed event count per
    (cell, epoch) and hashed offsets within the epoch, so "events in
    [t₀, now]" is well defined however the interval is split.
  - **Hierarchical split:** sample a large epoch's count, then divide it
    between its halves with a hashed binomial draw, recursively. Any
    interval's count costs O(log Δ), and children always sum to their parent,
    so long absences are cheap. Counting over [t₀, t₁] walks it like a
    segment tree: blocks wholly inside contribute their count, and only the
    two boundary blocks per level split further, about 2 log₂(Δ/E) steps
    (some 30 for a year at E = 1,024 ticks). The first event after t₀ is a
    descent to the first non-empty epoch after it, also O(log Δ); a
    multi-stage process repeats it once per stage until it saturates.
  - **Every block has its own key:** a block's hash key is derived from its
    parent block's key, and the top block's from the region's simulation
    key, a server-only branch of the world's root key (`worldgen/seed-keys`).
    A key derives nothing above or beside it. Clients receive the keys of
    the current and next blocks at one level (a few in-game hours, a
    placeholder) for the regions they may simulate, so they compute events
    within that horizon and can't predict beyond it; the server holds the
    top keys, and its catch-up stays O(log Δ), one key derivation per step.
    Catching up chunks loaded on the server is the server's alone: clients
    receive the resulting states.
  - **Portable samplers:** the Poisson and binomial draws are integer or
    fixed-point, so every machine draws the same counts
    (`core/worldgen-integer-domain`).
  - **Catch-up is exact by default:** a chunk loaded late reaches the same
    states as one simulated continuously. A process kind may declare a
    cheaper approximate catch-up (for example drawing its end state
    directly once an absence is long enough that the start no longer
    matters) after review, provided it yields an outcome the process could
    have produced, stays deterministic, and applies only to intervals no
    client held keys for, since a client computing a released window
    itself would disagree. The server is authoritative; how much pop-in a
    shortcut may cause is a gameplay judgement, not a fairness one.
  - **Saturation early-out:** a process at its maximum (a fully grown crop)
    stops counting.
- **The process's own dynamics are closed-form** (`core/closed-form-first`):
  a crop's stage after Δ is `min(max, stage + N)` with N the event count of
  its native growth process over the interval — the same distribution as
  Minecraft's random ticks, at zero per-tick cost, in every residency level.
- **Reactions that depend on neighbours are closed-form per cell too.** A
  cell's ignition time, for example, is a hashed exponential hazard
  starting when a neighbour began burning, read from that neighbour's anchor
  in level state (halo across a seam). Leaf decay, spread of grass or moss,
  and similar reactions take the same form.
- **Random ticks are CHECKS, not drivers of change**
  (`core/level-triggered`). A check reads the current closed-form state of
  a cell (and its neighbours' published state) and materializes it in the
  world: places the grown extension, shows the fire, removes the decayed leaf.
  It is level-triggered and idempotent: a missed check is caught by the next
  one; a repeated check changes nothing.
  - **Rates combine like resistances in series.** The native process makes a
    cell eligible at t*; the visible effect lags by the wait for the next
    check, exponential with mean 1/λ. A cascade of N hops takes about
    N × (τ_native + 1/λ). Raising λ asymptotically approaches the native rate
    and can never exceed it; lowering λ throttles a cascading reaction.
  - **λ is a latency and throttle knob, not a gameplay rate.** Native rates
    set how fast things *happen*; λ sets how promptly the world *shows* it.
    Under load `core/fair-share` lowers λ: more latency, no change in any
    process's dynamics, no backlog (a skipped check is simply lost — retiring
    debt with no bookkeeping, `sim/subsystem-rate-degradation`).
  - **Cost is bounded by volume, independent of pending workload.** A chunk
    pays λ × (its cells of check-bearing types), at most λ_max × its volume —
    Minecraft's constant per-section cost at the same rate, reached only if
    every cell is reactive; a chunk whose palette has no check-bearing type
    pays nothing. A burning forest costs no more than any other full chunk.
    What needs a budget is only follow-on work a materialization triggers (a
    lighting or fluid update), bounded by check count × work per check.
  - **Sampling without scanning:** per tick, each check-bearing type draws its
    check count from the hashed process (Poisson with mean type-cell-count ×
    λ × dt; the palette knows the counts) and picks that many of its cells —
    uniform pick with rejection when the type is common, a small per-type cell
    index when it is sparse.
  - **Check rates are per block type, in events per second of world time**
    (independent of `sim/tick-rate`); the per-tick probability is λ / tick
    rate. Starting points to measure, not decisions: Minecraft-equivalent
    baseline ≈ 1/68 s⁻¹ (3/4096 per block per tick at 20 Hz); since checks
    only add latency, a type whose visible lag matters (spreading reactions,
    whose hops are a third of a Minecraft block across — `world/voxel-scale`)
    gets a higher λ so its cascades stay near their native speed. A global
    multiplier (Minecraft's `randomTickSpeed`) is a debug/creative knob only.
- **Input changes** re-anchor at the moment of change; inputs only change
  through stepping, an edit, or **materialization by a check** (a placed
  extension, a removed leaf, fire shown), all of which trigger the re-anchor
  of affected processes; frozen and unloaded chunks can't change inputs at
  all (`sim/residency-levels`). Checks run in the owning chunk's own turn; a
  frozen chunk evaluates closed forms on read (display) but does not
  materialize them.
- **Cross-owner growth (U20):** growth is closed-form in its growing tip;
  the extension into another chunk's cell is performed by that chunk's own
  check, reading the neighbour's tip from its face-layer markers and
  starting from that observation. No messages, no inter-block references;
  features too large for their own states cross as boundary states. See
  U20 in `sim/execution-use-cases`.
- **Catalog: what uses the queue-free model (closed form or relaxation +
  checks).** The obstacle is not conservation in
  general but **conserved transfers between owners**; a process conserved
  within one owner fits. Fit test: a cell's state is computable from (world
  time, its own anchor, its neighbours' published level state), and the
  process is non-conserved or conserved within a single owner.
  - **Growth and decay:** crops, vines, kelp, bamboo, cactus (growing tips,
    as in U20); grass, moss and mycelium spread; leaf decay. **Trees:**
    worldgen trees are stamped whole by every chunk they reach
    (`worldgen/tree-generation`); huge trees and trees growing after
    worldgen cross chunks as boundary states (U20).
  - **Weathering and phase changes:** rust/patina, ice melt and freeze, snow
    accumulation and melt, mud drying, concrete curing — closed forms of
    season, temperature and light inputs. Only phenomena chosen to be "real"
    are materialized by checks; **cosmetic snow cover stays the read-time
    seasonal specialization** of `sim/seasons` and is never materialized.
    Which is which is the per-phenomenon reality-level question
    (`sim/seasonal-reality-level`).
  - **Slow fields (relaxation):** magic fields, disease/germ spread,
    pollution, slow heat diffusion, soil moisture / farmland hydration as a
    function of distance to water.
  - **Scent trails (closed form over hidden state):** a passing mob records
    its pass time on the cell as hidden simulation state, never persisted,
    so a trail is forgotten when its chunk unloads (`sim/block-metadata`); a
    cell's scent is `Σ e^(−k(T − t_pass))`, evaluated when read — nothing is
    written as scent fades, and each entry expires once its contribution
    falls below the detection threshold (expiry derived by the kernel from
    k). Neighbouring chunks see it through the marker map.
  - **Single-owner timed machines:** a furnace's output after Δ is
    `min(inputs, fuel-limited count, Δ/τ)` — conserved, but within one
    container, and closed-form arithmetic on counts conserves by
    construction; correct whenever opened (Minecraft only runs furnaces in
    loaded chunks).
  - **Spoilage and wear over time:** food in a chest spoils as a function of
    time and container type, evaluated when opened.
  - **Mob needs:** hunger, fatigue and other decaying drives are linear or
    exponential functions of time, evaluated when the decision layer reads
    them (`sim/mob-decision`).
  - **Spawning and despawning as checks:** "is the local population below its
    target density?" — level-triggered and convergent, consistent with the
    present-player invariant (`mobs/spawning`).
  - **Unsealed logic machines (relaxation):** signal levels as power =
    max over neighbours − 1 (redstone-like) are level-triggered by nature;
    with checks as the trigger, propagation delays become irregular on their
    own — exactly the diegetic rule in `sim/logic-machines` that unsealed
    machines are erratic and reliable only at a slow clock. Sealed machines
    stay compiled and deterministic, outside this model.
  - **Doesn't fit (messages or stepping):** piston pushes, items or entities
    crossing a seam, player edits, entity motion and physics, sealed logic.
