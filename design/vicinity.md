# sim/vicinity — The Vicinity: a simulation transaction domain

A Vicinity is the scope of cross-chunk simulation: a disjoint set of
chunks around one or more viewpoints, run as a
single-threaded actor one level up from chunks. Everything a chunk can do
on its own stays in the chunk; everything that crosses a chunk seam with
hard (conserved) consequences happens inside exactly one Vicinity. It owns
the conserved objects that are not settled in a chunk, the hard messages
between its chunks, and its assemblies. It is a runtime structure only:
never persisted, rebuilt from chunks, viewpoints and the world's scarce
table.

`design/concurrency.md` keeps the assumptions, invariants and connective
tissue the Vicinity shares with the rest of the model; this file holds the
Vicinity's own role, invariants and open questions. Residency levels are
`sim/residency-levels`; the ledger is `data/entity-ledger`.

**Vicinity** is the C++ class name.

## Architectural role
- **The World's Surveyor decides membership; the Vicinity simulates.** The
  Surveyor (the World's planning component, `world/surveyor`) sees every
  viewpoint, so the World's turns
  decide which chunks are loaded, running or frozen, and when Vicinities
  form, merge, cleave and dissolve. A Vicinity never decides its own
  extent: it is active in simulation (its own turns, and keeping its
  running chunks' simulation going) and passive in membership.
- **A Vicinity is the Dispenser's pull source for its own turns and its
  members' turns:** a Vicinity is a universe for
  simulation, so it decides what in it runs next. When a chunk becomes
  runnable (mail bit or nudge, through its idle-to-scheduled CAS) it is
  marked in the Vicinity's runnable set, guarded by a short `std::mutex`,
  and the Vicinity advertises its best priority. Its `pullBatch` ranks its
  runnable chunks and its own turn live, at pull time (render priority,
  distance, its own simulation rate), never handing out its own turn twice
  at once. It owns those chunks' actors; a chunk outside every Vicinity
  has no actor, and its work is offered by the World (`world/chunk`).
- **Chunk turns reach their Vicinity through a per-turn handle,** which the
  Dispenser builds from the chunk actor's own Vicinity pointer at turn
  start.
- **The frame thread never talks to it directly about the world:** it
  routes the player's intentions to the player's body, which the Vicinity
  owns and simulates; edits are things the body does, sent by the Vicinity.
- **Publishing trajectories** (`engine/frame-thread`): each turn pushes its
  changed segments, spawns and despawns as one batch of deltas into a
  mutex-guarded vector the frame thread swaps out, so the frame thread sees
  a turn's changes together; attachments are relative to their parent.
  Physics proxies are a separate, per-chunk projection.
- **What a Vicinity buys** beyond avoiding conflicts: no global
  serialization point; a consistent save per Vicinity, which closes
  duplication by unsynchronized saves inside it; and a bounded failure and
  lifecycle domain for loading, unloading and regeneration.
- **Rendering is decoupled:** render working sets overlap, follow each
  viewer and never merge; they read chunks and the Vicinity's published
  state and own nothing.
- **Clients** may have their own Vicinity implementation playing the same
  in-game role without authority, merging or persistence; single-player
  runs the server implementation (`net/source-blind-client`).

## Membership
- **Running and frozen, at every scale.** "Running" and "frozen" mean the
  same thing for a chunk as for a Vicinity: is its simulation running, or
  frozen in place (readable, never written)? A Vicinity's members are its
  **running chunks**, which it simulates, and its **frozen chunks**, which
  its running chunks read but which never run (`sim/residency-levels`). A
  chunk can run only in a running Vicinity; freezing a Vicinity freezes
  all its chunks.
- **A running chunk never touches a Vicinity's border.** Every neighbour of
  a running chunk (any of the 26, or a portal link) is a member of the same
  Vicinity, running or frozen. So a running chunk reads nothing outside its
  own Vicinity, and with no two running Vicinities touching (below),
  simulation never reads across a Vicinity boundary at all. The running
  chunks need not be contiguous: a telescope's target or a distant
  Observer may run as a separate pocket in the same Vicinity, each
  surrounded by frozen chunks; shapes vary, and a 3×3×3 box is one shape,
  not the definition.
- **Membership is exclusive.** Every chunk actor is a member of exactly one
  Vicinity. The frozen chunks are derived from the running ones (their
  neighbours that don't run) and never include another Vicinity's member:
  growth that would bring two running Vicinities' members together is a
  merge instead, and a frozen Vicinity's members are waited for (below).
  Freezing a chunk and running it change its state, never its Vicinity.
- **The Vicinity owns its members' actors,** and each actor holds a
  non-null pointer to its Vicinity. An actor is constructed with its
  Vicinity (no default constructor), so it can't exist outside one, belong
  to two, or outlive its Vicinity: a Vicinity's shutdown dissolves its actors
  before it is destroyed, their data populating full-detail terrain nodes
  (`world/chunk`).
- **A Vicinity is running or frozen.** It is either **running** (its simulation
  runs: its chunks' turns and its own simulation) or **frozen** (its
  simulation is stopped and nothing in it mutates the world, the same
  sense as a frozen chunk: readable, never written). A frozen
  Vicinity is not idle: its own turns still run, to save and to shut down.
  It is frozen when cleaved off or unobserved (then it saves and shuts
  down), when paused, or briefly at the save deadline (then it saves and
  resumes). Freezing and thawing happen at a halt (`engine/dispenser`).
- **No two running Vicinities touch.** Two Vicinities touch when a member of
  one, running or frozen, is adjacent to a member of the other (any of the
  26 neighbours, or a portal link). Touching can only arise from a
  membership change, so a chunk joining or starting to run that would make
  two running Vicinities touch is carried out as a merge instead. So
  wherever Vicinities hold adjacent chunks, at most one of them is running
  (none, when the World is paused). Together with the border rule above,
  nothing that runs ever reads another running Vicinity's members; what
  lies beyond a Vicinity is a frozen Vicinity's members or chunk data the
  World holds, both immutable.
- **Connected through portals too.** Adjacency includes portal links, so
  walking through a portal never crosses a Vicinity boundary, and opening
  an Aether box brings its interior chunks into the carrier's Vicinity.
- **Complete by construction.** A chunk starts to run only once it and
  every one of its neighbours is real (generated or loaded) and a member of
  this Vicinity, so simulation never sees a hole. A neighbour in another
  running Vicinity means a merge first; one held by a frozen Vicinity
  means waiting until that Vicinity has shut down.
- **Reach is bounded by the frozen border.** Anything a running chunk or
  its Vicinity does reaches at most as far as the frozen chunks around it:
  one chunk of frozen border covers reach up to 64 cells (a body's
  targeting, a sweep, an explosion's `probeVolume`, a `planEdit`); anything
  reaching further stops at the border (freeze at the edge), or needs a
  thicker border. Every chunk and Vicinity operation declares its maximum
  reach.
- **The body waits, not the thread.** A mover whose next step needs a chunk
  that isn't running holds at the edge and continues when the chunk
  starts to run. Read-ahead (velocity prediction leading generation) makes this
  rare; a teleport waits for its destination, its portal powering up
  while the destination's cascade is generated (`world/surveyor`).
- **Asking never loads.** Only viewpoints decide what is loaded and
  simulated, so simulation effects can't drag the frontier outward.
- **Merge, never hand off.** Vicinities never hand chunks or objects to
  each other: a handoff between them would push the two-phase handshake
  of cross-chunk work up one level.
- **Touching triggers a merge.** Merging is a brief halt of both: one
  Vicinity absorbs the other (only the absorbed members' actors move, and
  their pointers are rewritten), and their objects, queues and indexes are
  united. Running Vicinities merge as soon as they would touch.
- **Merges and cleaves are the World's turn, under halt tokens.** The
  Surveyor computes them (`world/surveyor`); the World's halt continuation,
  a World turn on a worker, carries them out by calling the halted
  Vicinities' `absorb(other, token)` and `cleave(spec, token)`. A halted
  Vicinity runs nothing; the halt token stands in for its turn passkey, so
  only the parent holding it touches its state (`engine/dispenser`). A new
  Vicinity a cleave creates is built in that same World turn.
- **A cleave divides one Vicinity into several.** At a halt, a
  Vicinity is cleaved into **running parts,** each around its
  viewpoints and keeping the frozen chunks that surround its running ones,
  and **frozen parts** holding the chunks between and behind them, so that
  no two running parts touch. The frozen parts save and shut
  down (`sim/concurrency-model`'s persistence). Two players walking apart
  turn one Vicinity into three: one running per player and a frozen bridge
  between them, which saves and goes; a player walking on alone sheds a
  frozen part behind. **Hysteresis:** a running part is cleaved off only once
  the bridge would be at least k chunks wide (k > 0, to tune), so siblings
  don't touch and merge again at once.
- **Grow by joins, shrink by cleaves.** As viewpoints approach, a chunk
  **joins** as a frozen member (its actor is built in the Vicinity), and
  **starts to run** when the Surveyor's allocation of the simulation budget
  wants it running and all its neighbours are members: having its
  neighbours is necessary, not sufficient, since a chunk with every
  neighbour present may still be too distant to be worth running. Both
  happen while
  the chunk is idle (its idle-to-scheduled CAS), with no halt, and the
  Vicinity adopts the scarce instances anchored there in its own turn.
  Chunks behind a receding viewpoint are shed by a **cleave**, once that
  band is a few chunks thick (with hysteresis): a cleave into one
  running part and a frozen part, which saves and shuts down
  (`sim/concurrency-model`'s persistence). No chunk leaves a running
  Vicinity on its own. Only merge, cleave, freezing and thawing need a
  halt.
- **Running waits for frozen.** A running Vicinity never takes members of a
  frozen one: a chunk it wants that a frozen Vicinity holds waits until that
  Vicinity has saved and shut down, then joins from its full-detail
  terrain node, which is cheap; the body waits at the edge meanwhile, as when loading falls
  behind. A frozen Vicinity's save is short, so the wait is too.
- **Observers** (chunk loaders' in-world form, `sim/residency-levels`)
  are viewpoints that ask for simulation and draw no frames. They form or
  join Vicinities like players, and carry a diegetic cost
  (`core/diegetic-cost`). **A moving Observer,** such as one mounted in a
  rail car, is a moving viewpoint: its Vicinity grows ahead of it and
  shrinks behind it, velocity prediction leads loading, and if loading
  falls behind, the train holds at the edge like any body ("the body
  waits").
- **Dissolution** (last viewpoint gone, or shutdown) is a frozen save and
  shutdown, after which the chunk actors dissolve and the Vicinity's scarce
  instances rest in the world's scarce table.

## Conserved objects
- **One kind of conserved object,** in one of two states while the Vicinity
  owns it:
  - **in motion:** the Vicinity simulates it (an arrow in flight, a falling
    block, the player's body, a mob crossing a seam);
  - **ready for chunk C:** queued for C to take in its own turn (an arrow
    come to rest in C, an item given to C).
- **Settled things are chunk-owned.** Once a chunk takes an object it is
  part of that chunk's simulated state and the Vicinity no longer knows of
  it; it is persisted in the scarce table, never with the chunk's content
  (`sim/concurrency-model`). An
  entity that stays inside its chunk never involves the Vicinity at all.
- **Transitions:** a chunk that calculates an object will leave sends it
  out (it becomes in motion); coming to rest queues it for the chunk it
  rests in. A moving thing transfers at most twice, with no chunk-to-chunk
  ping-pong.
- **Owner is not thread:** the Vicinity's work is ordinary Dispenser work,
  and a chunk may compute an object's first step itself as an
  optimization.
- **Movers are simulated serially.** Cost scales with movers (bodies, mobs,
  projectiles, falling blocks), not blocks. This is a named exception under
  `core/parallel-by-default`; splitting movers into independent islands is
  the escape route if profiling demands it.
- **Assemblies** (piston lines, slime machines) commit in the Vicinity's
  own turn, since only it touches its state; no transactional memory is
  needed.
- **Receivers resolve, never reject.** A chunk or Vicinity that receives a
  conserved object takes custody and resolves it under its own physics:
  place it as predicted if still valid, otherwise apply physics, displace it
  to the nearest free space, or use a declared sink where the kind allows.
  Nothing is bounced and nothing is left inert.

## How chunks and the Vicinity see each other
- **The Vicinity reads chunks** through their thread-safe, validated read
  APIs (sweeps, raycasts, edit plans); a mover's step touches only adjacent
  chunks (`sim/speed-of-information`).
- **Chunks see movers only through physics proxies, opt-in.** A physics
  proxy is a soft,
  read-only projection of an object's bounds and collision-relevant state
  (position, velocity, owner id) into the chunks it overlaps, published by
  the owner with a TTL; the owner nudges a chunk when its physics proxy
  enters. The
  player's body publishes a physics proxy; arrows don't; mobs will.
- **Effects in each direction:**
  - chunk on a mover: soft, by the Vicinity reading the chunk's published
    state (water pushing a body);
  - mover on a chunk, soft: decided by the chunk from the physics proxy (a
    pressure
    plate pressed);
  - mover on a chunk, hard: a command (a body breaking a block; an arrow
    damaging a mob the chunk owns);
  - coming to rest: a conserved transfer into the chunk;
  - pickup: initiated by the chunk owning the item, which sees the body's
    physics proxy and sends the item to the body's inventory. Conservation holds
    however stale the physics proxy; at worst a pickup is a step late, and a
    full
    inventory resolves by putting the item back out as a mover.

## Hard messages
- **Hard messages live in the Vicinity.** It holds one queue per running chunk
  and one for itself, each guarded by a short `std::mutex` (chunk turns run
  in parallel with the Vicinity's own). A queue entry is a conserved
  object or a command (an edit with expected old state, an assembly
  commit). Nothing droppable enters a queue.
- **Delivery is exactly once, in order per sender.**
- **Chunks have no mailboxes.** Sending to C enqueues into C's queue and sets
  C's mail bit; C consumes one entry at a time in its turn, and consuming
  transfers ownership into C. C may send an object straight back out;
  otherwise it is settled in C.
- **Edits get a reply,** "applied" or "rejected" (a fact, never a retry
  request), to the sender, normally the body's Vicinity on behalf of the
  player.
- **The per-turn handle.** A chunk turn gets a handle to its Vicinity for
  the duration of the turn only; it is not kept past the turn (copy and
  move deleted). Only turns of running chunks get a handle with `send`;
  frozen chunks get none. Merge and cleave move an actor, and
  rewrite its Vicinity pointer, only while the Vicinities involved are
  halted, and QSBR keeps a Vicinity valid until every reader has passed a
  quiescent point.

## Halts
- **A halt is the Dispenser no longer taking from the Vicinity:** the
  World halts it in a turn with a continuation, holding a move-only
  halt token; the Dispenser then never calls its `pullBatch`, while mail and
  nudges still mark chunks runnable, offered again on resume. The
  Dispenser counts the Vicinity's batches in flight and runs the
  World's continuation once none remain (for a merge, once both
  Vicinities are quiet); the Vicinity itself counts and signals nothing
  (`engine/dispenser`). Merge, cleave, freezing,
  thawing, pause and shutdown all use it.
- **Halted and frozen are different.** A **halted** Vicinity runs nothing
  at all, and only its parent, holding the halt token, may touch its state
  (merge, cleave). A **frozen** Vicinity is resumed after the halt that
  freezes it: its `pullBatch` offers only its own turns (saving, shutting
  down), never a chunk turn, so that work is the Vicinity's own, as an
  actor.
- **Resuming rescans nudges.** A nudge marks its chunk runnable in the
  chunk's Vicinity; if that races with a merge or cleave, the mark can
  land in a Vicinity that has just been absorbed. So a Vicinity resuming
  after a merge or cleave rescans its members' nudge masks
  (`core/level-triggered`), and no nudge is lost.

## Persistence
- **Never persisted itself.** On load, the Vicinity a chunk joins adopts
  every scarce instance anchored there in the world's scarce table.
- **One save path** (`sim/concurrency-model`): write the dirty chunks'
  chunk blobs (plentiful content only), capture the scarce rows of its
  chunks and
  its own in memory, then commit one short SQLite transaction through the
  Registrar (manifest, scarce rows applied only if newer, world records).
  A frozen Vicinity's save always commits; a running one's is abandoned if
  its transformation counter changed during the capture, and a deadline
  falls back to a brief halt.
- **Each Vicinity saves on its own schedule:** when cleaved off or
  unobserved (then shuts down), on pause, optimistically at quiet moments,
  at the deadline and at shutdown.

## Build staging
The first build has **a single Vicinity** per world,
formed from the viewpoints, with no merge or cleave. Merge, cleave
and several Vicinities come once a single one works. This is a direction for
the roadmap, not a revised point-release spec: v0.2's releases simulate
only soft state, so a Vicinity there would at most define residency (which
chunks run and which are frozen); conserved objects arrive with hard
simulation (v0.4 in `versions.md`'s roadmap, whose wording predates this
model).

## Checks
These mechanisms check the invariants below; each invariant's
**Enforced** line names the ones it relies on. Checks exist to catch
mistakes, not adversaries.

- **API shape** (type, compile time; the actor convention in
  `sim/concurrency-model`). Move-only queue entries; a consume call
  returns an owned object; per-turn handle types with copy and move
  deleted, and a distinct handle type (with `send`) for running chunks'
  turns; the Vicinity's objects and indexes are
  `ActorOwned<…, VicinityTurn>`, reachable only from its own turn.
- **Running totals** (fatal check, in release; `data/cheap-invariants`).
  Each Vicinity keeps O(1) counts of the conserved objects it owns, by
  kind, updated on every transfer in and out; chunks keep the same for
  settled objects.
- **Turn asserts** (debug audit, plus cheap release fatal checks where
  noted). Using the Dispenser's worker marker: mutating Vicinity state
  asserts the current actor is this Vicinity; consuming a queue asserts
  the current actor is that queue's chunk; membership changes assert the
  chunks involved are idle or their Vicinity halted; a frozen save asserts
  its Vicinity is frozen.
- **Save audit** (fatal check, at each frozen save). The captured scarce
  rows are compared with the running totals, every UUID appears exactly
  once, and every captured row's location is inside this Vicinity.

## Invariants (draft)
- **`invariant/vicinity/one-vicinity-per-chunk-actor`** — Every chunk
  actor is owned by exactly one Vicinity, as a running or frozen member,
  and holds a non-null, dereferenceable pointer to it, which changes only
  while that Vicinity is halted.
  - **Enforced:** type: the API shape (the Vicinity owns its actors; the
    constructor takes a Vicinity reference, with no default constructor);
    debug audit: turn asserts (moving an actor asserts both Vicinities are
    halted).
- **`invariant/vicinity/complete-by-construction`** — Every running chunk,
  and every one of its neighbours, is real (generated or loaded) and
  resident.
  - **Enforced:** debug audit: turn asserts (starting to run asserts it).
- **`invariant/vicinity/no-turn-sees-change`** — No turn sees its Vicinity
  change: a chunk joins or starts to run only while it is idle, and merge
  and cleave happen only while the Vicinities involved are halted.
  - **Enforced:** debug audit: turn asserts.
- **`invariant/vicinity/freeze-at-edge`** — A Vicinity sends hard messages
  only to its running chunks, and nothing it owns moves into a frozen
  chunk; an object that would leave stops at the boundary.
  - **Enforced:** type: the API shape (only running chunks' turns get
    `send`); debug audit: turn asserts (`send` asserts the target is
    running, via the Vicinity's chunk index).
- **`invariant/vicinity/one-owner-per-conserved-object`** — At every
  moment each conserved object is in exactly one of a chunk's settled
  state, one Vicinity's objects (in motion or queued), or the world's
  scarce table (when no Vicinity holds its anchor).
  - **Enforced:** fatal check: running totals and the save audit.
- **`invariant/vicinity/single-threaded-state`** — Only the Vicinity's own
  turn mutates its objects and indexes; anyone else enqueues into its
  queue.
  - **Enforced:** type: the API shape (`ActorOwned`); debug audit: turn
    asserts.
- **`invariant/vicinity/queue-discipline`** — Each queue entry is consumed
  exactly once, by its receiver's turn, in per-sender order; consuming
  transfers ownership.
  - **Enforced:** type: the API shape (move-only entries); debug audit:
    turn asserts (the consumer assert).
- **`invariant/vicinity/no-dead-letters`** — Every consumed conserved
  object ends settled, in motion again, or in a declared sink.
  - **Enforced:** type: the API shape (the consumed object is an owned
    token that asserts if destroyed without one of those outcomes).
- **`invariant/vicinity/handles-within-turn`** — Handles don't outlive
  turns.
  - **Enforced:** type: the API shape.
- **`invariant/vicinity/saves-never-duplicate`** — A save commits only a
  capture during which its transformation counter didn't change (always
  true when frozen). Its rows then apply only if newer
  (`invariant/registrar/newer-rows-only`).
  - **Enforced:** debug audit: turn asserts (the save path asserts the
    counter); fatal check: the save audit.
- **`invariant/vicinity/no-wait-on-world`** — No thread waits on the
  world: a mover that needs a chunk that isn't running holds (the body
  waits), and no Vicinity turn blocks on another actor.
  - **Enforced:** type: the API shape (no blocking API reachable from turn
    code, as for the Cartographer).
- **`invariant/vicinity/running-or-frozen`** — Every Vicinity is either
  running or frozen, and a frozen one runs no simulation and mutates
  nothing in the world; its own turns only save it and shut it down.
  - **Enforced:** type: the API shape (a frozen Vicinity's `pullBatch`
    offers no chunk turns); debug audit: turn asserts.
- **`invariant/vicinity/running-vicinities-apart`** — No member of a
  running Vicinity is adjacent (26 neighbours or a portal link) to a
  member of another running Vicinity.
  - **Enforced:** debug audit: turn asserts (joins and chunks starting to
    run assert it, via the Surveyor's membership index).
  - **Why:** so whatever a running Vicinity reads outside its members is
    immutable while it reads.
- **`invariant/vicinity/running-waits-for-frozen`** — A running Vicinity
  never takes a member of a frozen one.
  - **Enforced:** debug audit: turn asserts.
- **`invariant/vicinity/running-chunk-off-border`** — Every neighbour of a
  running chunk (26 neighbours or a portal link) is a member of its own
  Vicinity, and a chunk runs only in a running Vicinity.
  - **Enforced:** debug audit: turn asserts (starting to run, and every
    cleave, assert it).
  - **Why:** so simulation never reads outside its own Vicinity, and every
    operation's declared reach is within the frozen border.

## Open
- **Distant pockets: same Vicinity or their own.** A telescope's target (or
  any distant demand) may run as a pocket in the demanding viewpoint's
  Vicinity or as a Vicinity of its own; both satisfy the invariants. Which
  to use may depend on range: a short-range target close enough to merge
  soon anyway suits a pocket, a far one its own Vicinity, which runs in
  parallel.
- **The turn budget.** A Vicinity with many movers is one serial actor, and
  a turn can't always finish. **Partly eased by `sim/motion-segments`:**
  passive movers (flung blocks, debris, arrows) may skip
  stepping and wake at a predicted impact if profiling calls for it, which
  would make their work O(events); bodies with intentions are still
  stepped. Options for what remains:
  - **lower rates for distant or slow movers** (`sim/multi-rate`): each
    mover steps at its own divisor of the base tick, so a turn's work is
    bounded by choosing rates, not by stopping halfway through a tick;
  - **partial completion:** the turn stops at a budget, leaves the rest
    queued and advertises urgency to the Dispenser; the risk is movers in
    one tick seeing each other at different steps;
  - **islands:** independent groups of movers (no possible contact within
    a step) as separate work items, which is the scaling escape route
    anyway.

  My lean is rates first, islands when needed, and partial completion
  only as the overload fallback (`sim/subsystem-rate-degradation`). It
  needs your view, and it's where `core/fair-share` meets the Vicinity.
- The cleave bridge k, and whether to merge earlier than touching for
  fast movers.
- The assembly transaction API.
- How a mob moves between chunk-owned (wandering inside one chunk) and
  Vicinity-owned.
- Which object kinds publish physics proxies beyond the body and mobs.
- Heavy scenes against a serial Vicinity (an explosion throwing hundreds of
  blocks), which the turn budget answer has to cover.
- The chunk loader's diegetic form.
- Whether an edit still queued at a save is persisted or dropped.
- How often halts can run before they show: membership changes are batched
  onto them, so a fast mover through new terrain sets the halt rate: a
  player in flight, or a train carrying an Observer, which may be faster.
  To measure.
- Later, as detailed design: how the World's turn requests and confirms a
  halt.
