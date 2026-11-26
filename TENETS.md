# Tenets

Our core design tenets, consolidated in one place to keep key decision-making
criteria foregrounded in context, no matter what the detailed task.

## Meta-tenets

**Tenets capture beliefs and principles, not goals.** They are not time-bound
and should survive multiple rounds of goal-setting and achievement.

**Tenets distinguish rather than elevate.** They should capture what makes us
*different*, not what makes us *superior*.

**A good tenet plays at least one, and ideally all, of these roles:**
- a *tiebreaker*, which helps us decide among multiple viable options;
- a *reminder of essentials*, which risk being obscured by short-term
  objectives;
- an *idiom*, which provides handy nomenclature for a core concept.

## Tenet format

Every entry is identified by its **`group/slug`**, which never changes and is
used for all cross-references; entries are not numbered, so order and grouping
can change freely. The slug's group prefix records where the tenet originated,
not which section below holds it. Topical decisions stay in `design/*.md`;
`DESIGN.md` is the index.

**How tenets are written**: a statement, why, one or two telling
examples, and a design test. Detail stays only while it illuminates the
tenet. Mechanisms are named by slug, never described here, so a tenet
doesn't go stale when a mechanism changes; revision history lives in
git, and entries carry no status or date lines.

## Authority, representation and reproducibility

How authoritative state is kept apart from what derives from it, how it is
represented, and how it stays reproducible.

### core/authority-derived-split — Authoritative state is conserved; everything fast is a derived, rebuildable view of it

**Authoritative state is conserved and immutable; everything fast is a derived,
rebuildable view of it.** The authoritative layer is identity-stable and
auditable (f64 coords, UUIDs, immutable objects, the entity ledger's
conservation accounting). The performance layer is derived and disposable
(f32 render data, the choice of a chunk's representation, caches); the
cells themselves are authoritative. Many
decisions are an instance of this split in some subsystem.

- **Perception is rendering, and no soul reads authoritative state directly.**
  Every consumer of the world, a player's eye or a mob's sensor, receives a
  derived view rendered for it. A player's render is high-fidelity pixels and
  audio; a mob's is low-fidelity percepts (`sim/mob-perception`). They differ
  in fidelity and modality, not in kind, so every soul type senses a render
  and none reads truth (`sim/player-embodiment`).
  - **One scoped exception, for performance:** a mob's pathing reads *local*
    authoritative geometry directly, because a memory rich enough to path
    from costs too much per mob. Locally a sensed floor and a read one are
    indistinguishable; remote pathing stays heuristic, so there is still no
    omniscience.
  - **Consequence: "cheats" are declared senses, not code privileges.** A
    soul perceives only through its body's senses, so X-ray sight is
    legitimate as a declared sensor (sight without occlusion), while
    undeclared omniscience is unrepresentable. A wall-sighted mob is
    **legible** (its body has that sense), **bounded** (it can't exceed it)
    and **counterable** (each sense has its own counter). "This mob finds you
    anywhere" becomes a content decision, and a player possessing that body
    inherits the sense, because it lives in the body, not in who drives it.

### net/source-blind-client — The client can't tell simulation from replication, and doesn't need to

**Client code reads and acts on the world the same way whether the state it
sees is being simulated in the same process or replicated from a remote
server.** What differs between single-player and multiplayer is confined to
*who writes* the state the client reads, never to how the client reads it,
predicts against it or renders it. That keeps single-player and multiplayer
one code path without duplicating the world.

- **Why:** a client that branches on where its data came from grows two
  sets of bugs, and the single-player game stops testing the multiplayer
  one. A client that can't tell needs only one.
- **Examples:**
  - In single-player the client reads the authoritative chunks directly
    through the same thread-safe read API a multiplayer client uses on its
    replicas; the chunks are written by the server's simulation in one case
    and by replication in the other (`sim/concurrency-model`).
  - The client sends an edit to "its chunk" in both modes: in single-player
    that chunk is the authority and simply applies it; in multiplayer it is
    a replica that applies it tentatively, as a reversible delta, until the
    server confirms or rejects it.
  - Positions reach every client reader in the same client form (an integer
    chunk coordinate and an f32 local offset, `core/f32-origin-relative`),
    as timestamped samples, so neither precision nor freshness reveals the
    source.
- **What it requires:** a client-facing read API that exposes only what a
  multiplayer client could know (no server-only data in single-player
  either), and a client that treats all state as possibly stale.
- **Design test:** "would this client code behave differently if the state
  came from the network?" If yes, move the difference below the read API.

### core/f32-origin-relative — f32 is origin-relative, never absolute world coordinates

f32 only ever holds a value relative to a nearby origin (camera, chunk corner,
or dimension-local frame) — never an absolute world position. Absolute position
lives solely in the authoritative type (f64, see `coords/f64-authoritative`).
- Why: absolute f32 at the 10⁹ world edge has ~64-unit (foot) ULP — far past
  Bedrock's jitter and into whole-building granularity. Camera/chunk-relative
  f32 carries only small magnitudes (render distance), where f32 ULP is
  sub-millimetre. Same type, clean precision, purely because the magnitude
  stays small — and this is *why* the f64/f32 split holds at any world size.
- **Rebase rule:** convert to f32 by computing `authoritative_world −
  authoritative_origin` IN the authoritative type (f64), then downcast the
  *result* to f32. Never `(f32)world − (f32)origin`: downcasting an absolute
  operand loses precision before the subtraction.
- Enforcement: prefer the type system over convention (see
  `coords/typed-positions`) so an absolute f32 world coordinate is
  unrepresentable rather than merely discouraged.

### core/worldgen-integer-domain — Worldgen RNG lives in a reproducible integer domain; the float boundary is one-way

All seeding, RNG extraction, and bifurcation/splitting of RNG state for world
generation happens entirely in a reproducible **integer** domain (integer-seeded
PRNG, integer hashing of seed + coordinates, integer state splits). Once a value
crosses the boundary into floating point, it has left the reproducible domain:
**a float-derived result must never be consumed back into any worldgen
decision.** Floats may only shape values *within* an already-decided discrete
cell; they must not gate which block, which branch, which structure, or any
other discrete generation outcome.
- Why: this is how same-seed → same-world holds across compilers, CPU
  architectures, and optimization levels without relying on float
  bit-exactness.
- Scope: worldgen only. Runtime simulation is outside this tenet
  (`coords/f64-authoritative`).
- Practical rule: the gen pipeline is integer-in → (discrete decision) → float
  only at the leaves. A `float`/`double` feeding a comparison or hash that
  decides generation is a violation and a repro bug, except for the one
  recorded exception below.
- **The recorded exception: accelerated noise may gate soft content**
  (`deps/noise`). FastNoise2 at full SIMD isn't bit-identical across
  SIMD levels, and its values do gate discrete outcomes such as solid versus
  air. We accept that because fast noise is a wanted baseline, and bound it:
  - only for **soft content** (`core/soft-over-hard`: terrain shape,
    vegetation, ambience); **hard content** (valuable ores, loot-bearing
    structures) is placed purely in the integer domain;
  - a rare cell generating differently on another machine is tolerated under
    the server's authority, not treated as a repro bug;
  - the stance is to minimize and mitigate the soft gates, not eliminate
    them. Masking low bits doesn't help (it only moves the boundary); the
    mitigations are in `deps/noise`.
- Existence proof: Minecraft shows sufficient worldgen reproducibility is
  achievable in a shipped game. How generator changes reach existing worlds
  is `worldgen/generator-versioning`.

### worldgen/metered-information — Worldgen meters authoritative information; appearance variety is free via hashing

Worldgen must **meter the Shannon information it puts into the authoritative
world.** Authoritative/gameplay content is structured, compressible, and
meaningful — never white noise. A **white-noise chunk is forbidden under any
circumstances**: it maximizes stored entropy, defeats `world/chunk-representation`
(forces a wide palette), defeats `persist/`'s reproducible-⇒-need-not-be-stored model
(nothing diverges to compress, and nothing decays back to anything), and gives
the player no learnable structure. The generative-side partner to
`core/opaque-chunk-queries`'s "high-entropy chunks should be rare": that tenet bounds
entropy at the STORAGE boundary, this bounds it at the GENERATION source.
- **Two channels, split on `core/authority-derived-split`:**
  - *Authoritative / gameplay channel* — **bounded, metered** information.
    Structure a player can read, learn, and exploit (biomes, ore distribution,
    terrain form). Low-entropy by design; this is the channel that must never
    degrade to noise.
  - *Perceptual / appearance channel* — **may be arbitrarily varied, because it is
    a pure function of a hash** (`hash(seed, coord, …)`), adds NO stored state and
    NO gameplay information, and is perfectly reproducible. Vary appearance all
    you like here.
- **The governing rule — runtime-specialize appearance with a raw hash, maintain
  gameplay invariance.** Visual variety (per-voxel color/texture jitter, foliage
  tint, surface detail) is resolved at runtime from a hash. Two blocks that look
  different via the hash are the SAME gameplay object; appearance divergence
  must never imply gameplay divergence (same lesson as `light/visibility-only`).
- **Why it matters (not just taste):** information in the authoritative channel
  is information the engine must STORE, the player must LEARN, and every system
  must handle. Information in the appearance channel is recomputed and free. So
  push variety into the hashed channel, and spend authoritative entropy only
  where it buys learnable gameplay meaning.
- **Litmus test:** if a proposed source of variation changes what the player can
  DO (collision, yield, behavior, navigability), it is authoritative — meter it,
  structure it, make it compressible. If it only changes how something LOOKS,
  route it through the hash and store nothing.

### core/opaque-chunk-queries — Chunks answer semantic queries; the voxel representation is never introspected

A chunk's internal voxel representation is **private**. No consumer outside the
chunk may read its voxel storage; consumers only ask the chunk **semantic
questions** and receive **answers**. There is NO public `getVoxel(x,y,z)`
accessor — its absence is the tenet. This is the enforcement spine that makes
`world/chunk-representation`'s swappable reps real: the moment a raw voxel
accessor exists, every caller grows a dense-array assumption and no
representation can ever change without breaking them.
- **Questions, not storage.** Examples: face-to-face connectivity
  (`faceConnectivity()`, the 6×6 reachability graph), `isUniform()` /
  `dominantMaterial()`, interface finders, and a hit that reports the cell
  it hit. The test for a legal
  method: does it answer a question the caller HAS, or expose how the chunk
  is STORED?
- **Every query must be cheap for every representation**, ideally in each
  rep's natural complexity (a uniform chunk answers connectivity in O(1)). A
  query only a dense array can answer efficiently is a voxel walk wearing a
  method signature: redesign it. Answers come from a maintained query
  summary beside the rep, not an on-demand scan (`world/chunk-representation`).
- **Size rule: a public answer is O(1) or O(surface), never O(volume).** An
  answer that grows with the chunk's volume is its contents in another form,
  and its data belongs inside the representation that needs it. The palette
  chunk's 3D meshing bitmasks (32 KB) fail the test, which is why they are
  private (`world/chunk-meshing`). This is also the cross-chunk seam
  (`core/chunk-locality`): whatever a chunk learns about a neighbour comes
  through the neighbour's queries, never its interior.
- **Results, not cells, for every consumer:** meshing (`buildMesh()`), ray
  targeting (`raycast(ray)`) and light-emitter discovery are queries the chunk
  answers itself. There is no single-cell probe: what you are aimed at comes
  with the hit that found it, and edit planning is itself a semantic query
  (`planEdit(action, region)`: a tool action's cell changes, each with the
  state it expects the cell to hold), the same pure function for preview and
  commit. A raw cell read exists only for tests, behind a test-only access
  type.
- **Shape queries keep the iteration behind the virtualization:** a bounded
  region of some shape (box, sphere, cylinder) with a question, answered by
  the chunk (`probeVolume(shape, question)`): any or all cells matching a
  predicate, a count or material histogram, or the matching cells with
  their states. Each representation answers in its own way (a uniform
  chunk at once, a palette chunk by ruling out materials from its palette
  first), and the caller never iterates cells itself. An explosion's
  breakable cells in a sphere, or a clear-space check for placement, are
  examples.
- **Today's seam** (`WorldQuery`, from v0.1.5, kept for callers through
  v0.2): `raycast(origin, dir, max_dist)` returns the hit (cell, face
  normal, hit point, state), the world answering the ray itself;
  `probe(cell)` is a single-cell read for bounded, edit-local uses only
  (pick-block, placement checks, a tool's own small footprint), never
  looped over a region, standing in for `planEdit` until edits become
  semantic queries; `editable(cell)`; `apply(edit set)`; and results for
  bulk consumers (`buildMesh()`, `emitters()`, a `generation` counter).
- **Keep the query set small and compositional:** a few reusable primitives,
  not a bespoke `getFluidData()` per consumer. A hundred bespoke voxel walks
  recreate the leak behind a facade.
- **Enforced by the type system, not convention** (same discipline as
  `coords/typed-positions`): the voxel storage type is not part of a chunk's
  public type. "Don't introspect voxels" is **can't**, not **shouldn't**.
- **The hard-line stance:** better to push computation INTO the chunk (in the
  limit, a small evaluator behind the query interface) than to permit a
  raw-array accessor. Opening the array saves a little effort now and
  forecloses every future representation.
- **Corollary:** high-entropy chunks that force a wide palette should be rare
  (`worldgen/metered-information`). If they become common, that is a signal to
  investigate, not to add a voxel accessor.

### deps/selection-principles — Selection principles for taking on a dependency

Rules for taking on a dependency, in priority order:
1. **A dependency must not cross an authority seam it doesn't belong in.** A lib
   that only ever touches the *derived* side (render, audio, input) is low-risk;
   a lib that touches *authoritative* state (the save store, anything feeding
   worldgen) is held to the reproducibility/determinism bars below. This is
   `core/authority-derived-split` applied to the dependency graph.
2. **Reproducibility is a hard gate for anything feeding worldgen.**
   `core/worldgen-integer-domain` forbids a non-reproducible float from gating a
   discrete generation outcome. A library whose output varies across
   CPU/compiler (notably SIMD float) may be used for *candidate/visual* values
   but **never** on the integer-domain decision path (`deps/noise` records the
   one exception).
3. **Prefer a thin, replaceable seam over a framework.** Depend on libraries that
   do *one thing* (allocation, windowing, serialization) behind the engine's own
   abstractions; avoid frameworks that want to own the main loop or the data
   model. Matches the render-backend seam discipline in `engine/vulkan-abstraction`.
4. **Permissive license, maintained, buildable.** MIT/BSD/Zlib/Apache-2.0
   preferred; single-header or CMake-native strongly preferred; avoid abandoned
   or copyleft-for-linking libraries for anything statically linked.
5. **Boring and conventional beats clever.** For each slot, the library "nobody
   gets fired for choosing" is usually right — the AI-authorship economics
   (`engine/graphics-api`) favor well-documented, widely-used libraries the model
   has seen thousands of correct examples of.

## Simulation and concurrency

How the simulation is partitioned, scheduled and kept cheap and convergent.

### core/chunk-locality — Chunk simulation is independent by default; cross-chunk coupling is the controlled exception

A chunk must be simulable as independently as possible: its work operates on
its own interior without requiring neighbors to be loaded or simulated. All
cross-chunk interactions (fluids crossing a face, gravity blocks falling across
a boundary, multi-chunk structures, light across seams) are **explicit,
controlled exceptions** routed through a defined exchange mechanism — never
direct reach-ins into a neighbor's state.
- Why (the DF vs. Minecraft lesson): DF's fortress-mode fluid/river dynamics
  depend on a bounded, fully-resident simulation region — the solver assumes
  global visibility of its domain, which neither scales nor streams. Minecraft
  survives chunk-at-a-time simulation precisely because fluid behavior is
  defined to degrade gracefully at the loaded-world boundary rather than require
  a coherent global domain. We choose the Minecraft locality model.
- This is a precondition, not a preference: it is what makes streaming and
  chunk-level parallelism possible at all, and the controlled cross-chunk
  interactions are the concurrency synchronization points. The mechanism is
  `sim/concurrency-model`. Effects travel at most one chunk per tick
  (`sim/speed-of-information`).
- **Cases split by what correctness they need:**
  - **Relaxed** (eventually consistent is fine): lighting across seams
    (presentation-only), stabilization-field percolation (authoritative but
    deliberately slow) and flowing water (derived from sources).
  - **Tight** (must be gameplay-correct): falling blocks, entities and
    multi-chunk structures that cross a seam. These are conserved transfers
    in the hard simulation (`sim/exact-core`).

### core/soft-over-hard — Prefer soft simulation to hard, and local hard simulation to global
- **Open:** the boundary for valuable blocks (`sim/valuable-blocks`) and
  seam-hiding UX details.

**Simulation comes in two kinds, and we prefer them in this order: soft, then
local hard, then global hard.** Soft simulation is approximate, convergent and
nondeterministic where that doesn't matter (`core/closed-form-first`,
`core/level-triggered`, `core/soft-state`). Hard simulation is exact,
conserved and dupe-proof, formally modelled (`sim/exact-core`). Local hard is
always preferred to global hard, for performance as much as for correctness.
Being deliberate about this means deciding, for each kind of state, **which
visible artifacts of divergent or nondeterministic gameplay matter enough to
care about**.

- **What goes where, by value:** crops and tree growth are **soft** — a soft
  divergence may occasionally yield a little extra wheat or wood, which is
  acceptable. **Valuable things are hard:** valuable ores and other blocks
  valuable enough to track individually are deterministic, conserved and
  dupe-protected (`sim/valuable-blocks`, open).
- **The entity ledger is part of the hard simulation, and the mint is the
  boundary.** Soft simulation decides how much of a non-valuable thing exists
  in the world; harvest **mints** the item into the ledger
  (`data/entity-ledger`), after which it is conserved, in exactly one place,
  dupe-proof. So soft simulation deliberately gives **no duping prevention**
  for what it grows: soft kernels never carry conservation, and each
  soft→hard mint is a declared source. **Valuable ores mint earlier, at
  resolution** (`geology/ore-sites`), so pistons and other block manipulation
  conserve them.
- **Aligned with the engagement ethics:** randomness lives where value is low
  (wheat behaves unpredictably but isn't valuable); valuable rewards are
  deterministic and conserved (diamonds) — `ethics/no-variable-ratio-core`
  made structural. Watch `core/no-meta`: a reliable trick that farms extra soft
  output would become the meta; it stays acceptable while the output is low in
  value.
- **Hide the seams reasonably well.** Highly technical players may find and
  exploit the soft simulation's characteristics, which is its own legitimate
  enjoyment for that profile; the goal is to keep it out of the ordinary-player
  meta. One means: **not showing coordinates to players** (dev overlays only).
- Worldgen's discrete gates follow the same hierarchy
  (`core/worldgen-integer-domain`'s exception).
- **Design test:** for any new kind of state, ask "what does a divergence or
  duplicate of this cost a player?" If little, it is soft. If it is valuable
  enough to track individually, it is hard — and then, "can it be local?"

### core/parallel-by-default — Subsystems are parallel-by-default; serial sections are justified, named exceptions

**Design every subsystem to run across many cores from the start; a serial
section or a shared-mutable hot path is an exception that must be justified, not
the silent default.** Most sim games (Minecraft's largely single-threaded tick,
DF, pre-parallel Factorio) are too single-threaded, and the reason is almost
never the thread pool — it is that their **world state was not architected for
parallelism**, so retrofitting means fighting shared mutable state everywhere.
We architect for parallelism up front so the cores are usable by construction.

- **This tenet names what the architecture is already for:**
  `core/chunk-locality` (chunks are parallel work units at all),
  `sim/concurrency-model` (how they run on a shared worker pool), and pure
  transforms with no shared mutable state to lock
  (`core/authority-derived-split`).
- **The hard rule.** Every NEW subsystem must answer "how does this parallelize?"
  before it is accepted: is it chunk-local? a pure transform? coupled only at
  boundaries, in an order-independent way? A subsystem that needs a serial
  section or touches shared mutable state on the hot path must **justify that
  as an explicit, bounded exception**. How to relax isolation when it must be
  relaxed is `core/isolation-first`. This makes single-threadedness the thing
  that has to pay for itself, inverting how the genre's engines usually end up.
- **Named parallelization targets** (illustrative): fluid and other cellular
  simulation, per chunk; mob perception, decision and planning, per mob;
  chunk generation, per chunk (the embarrassingly parallel case);
  breadth-first propagation (light, logic signals), across the frontier; slow
  fields, as closed-form relaxation (`core/closed-form-first`).
- **Legitimate exceptions, named rather than pretended away:** the
  cross-chunk exchange is the inherent synchronization point, but it need
  not be a serial section; **global-consensus** operations (the multiplayer
  consensus pause, portal/dimension links, authoritative commit ordering)
  genuinely can't be fully parallel. They earn their serialism; they are not
  the ambient default that quietly serializes the whole tick.

### core/isolation-first — Parallelism is easy to lose and hard to regain: explore isolated and pure designs thoroughly before relaxing them

**On a road of pure isolation or pure functional calculation, explore that
road thoroughly, pushing past the obvious approaches, before relaxing it;
when relaxing, do it carefully and contain the damage.** How far to push is a
judgement call, not a rule. In systems engineering, parallelism opportunities
are easy to lose and hard to gain back: one convenient dependency between
units of work (a chunk reading its neighbour, a turn touching shared state)
quietly becomes something everything relies on, and removing it later means
re-architecting.

- **Default to the isolated or pure form**, even when it costs something
  visible (extra geometry, pop-in, a known cosmetic limitation), and look for
  a fix that stays isolated or pure before reaching for shared state or
  cross-unit reads.
- **When relaxing, contain it:**
  - name the exception and bound its scope (which data, which phase);
  - route it through the **one carefully engineered soft mechanism** the
    simulation already uses, never a second ad hoc one;
  - ask whether the relaxed part must be deterministic and whether it must be
    instantaneous; usually neither, which keeps it soft and off the critical
    path;
  - keep the isolated version as the correct fallback, so the relaxed part is
    an optimization that can be lost, delayed or removed.
- **Beware relaxations that arrive through the back door:** an optimization,
  a convenience API or a test harness that introduces a dependency nobody
  decided on.
- **Make coupling hard to write by accident**: reading another unit's state
  needs an explicit, named input,
  so every dependency is visible and reviewed when it is added, not
  discovered later. The real enemy is implicit coupling.
- Example: v0.2 meshes every chunk in isolation, accepting seams, instead of
  reading neighbours (`world/chunk-meshing`); seams are revisited with LoD,
  through the soft mechanisms.

### core/fair-share — No consumer of simulation work may starve another; work is scheduled like an OS scheduler with QoS, not first-come
- **Open:** weights, burst parameters, entity granularity and how slowdown
  is shown.

**Every consumer of simulation work gets a guaranteed share and makes progress
within bounded time; none can starve another.** When demand exceeds the
budget, work is allocated the way an OS scheduler with quality-of-service
classes allocates CPU, not in the order requests happen to arrive.

- **Scheduling entities, not just subsystems.** The units are subsystems
  (fluids, logic, growth, falling blocks, worldgen, …) *and* heavy consumers
  inside them (one large logic machine, one large body of fluid, one player's
  builds in multiplayer). One giant machine must not starve every other
  machine, any more than fluids may starve logic.
- **Guaranteed minimum + work-conserving.** Each entity has a weight and a
  guaranteed minimum share; capacity an entity doesn't use is redistributed,
  so nothing is wasted when load is light.
- **Bursts, then equilibrium.** An entity may run above its share for a while
  by spending accumulated credit (token-bucket style), then settles to its
  equilibrium share. An expensive machine can therefore start fast and run
  sustainably slower, instead of being capped from the first tick or allowed
  to dominate forever.
- **Priority classes.** The world clock and player responsiveness (input,
  own-body actuation, the client seam) are the top class and are never traded
  away; background classes such as distant worldgen and decay sit below
  ordinary sim.
- **Degradation is per-entity and observable.** An over-budget entity slows
  down on its own; the rest of the world doesn't lag
  (`sim/subsystem-rate-degradation`). Its slowdown should be visible, ideally
  diegetically per `core/diegetic-cost` (a straining machine), so players can
  tell "my machine is slow" from "my machine is wrong".
- `core/parallel-by-default` says how work runs; this says who gets it.

### core/closed-form-first — Use a closed-form solution wherever one exists; step only what genuinely can't be evaluated directly

**Wherever a process has a closed form — its state at any time is a direct
function of its state at an anchor time and the elapsed time — evaluate that
function when the value is needed instead of stepping the process every
tick.** Stepping is for dynamics that genuinely can't be evaluated directly
(fluids, falling blocks, logic, anything that interacts with its neighbours
every step).

- **The form:** each such process keeps an anchor `(state(t₀), t₀)`; any
  consumer that needs the present value computes
  `state(now) = f(state(t₀), now − t₀)`. Becoming simulated, being loaded,
  being meshed for display, a player inspecting it — all are the same case;
  there are no special transition rules.
- **Reads are pure; the authoritative anchor moves only at semantic events.**
  Evaluating on read may cache the result, but the cache is derived state;
  the anchor changes only when an input changes or the process is acted on
  (harvested, cut). So two copies of a world that were read at different
  moments hold identical authoritative state, and their content digests agree.
  (Prior art for the failure this prevents: a Factorio desync where merely
  *reading* lazily created production statistics changed game state and the
  CRC check failed.)
- **Randomness comes from a hash tied to ABSOLUTE time, so evaluation is
  interval-independent.** Drawing randomness per evaluation would make
  [t₀, now] evaluated once and [t₀, t₁] + [t₁, now] evaluated twice two
  different realizations, so the outcome would depend on when someone looked.
  Events instead come from a hashed point process on the world timeline
  (`sim/random-ticks`), so evaluating once over an hour gives exactly the
  same result as evaluating every minute.
- **Relaxation toward a local equilibrium, for coupled fields.** A field
  whose cells depend on their neighbours has no per-cell closed form, but on
  each check a cell can move toward the target its neighbours imply, by an
  amount set by the elapsed time, e.g. `v ← target + (v − target)·e^(−kΔ)`. A
  second check at the same moment changes nothing, and it converges to the
  same steady state. Fine wherever players don't rely on the timing; where
  they do, step it.
- **A closed form is valid only while its inputs are constant** (light, water,
  season, the block below). **A change to an input triggers
  evaluate-then-re-anchor:** bring the process up to now under the old inputs,
  then re-anchor under the new. The result is piecewise-exact.
- **Why it's a tenet:** it is exact, not approximate (no catch-up, no debt);
  it costs nothing for state nobody observes (a field of crops is free until
  something looks at it); it can't cascade, because evaluation is chunk-local;
  and it makes absence (`persist/presence-not-absence`) a direct evaluation
  rather than a simulated backlog.
- **Design test:** for any new time-dependent process, ask "does its state
  have a closed form in elapsed time, given locally constant inputs?" If yes,
  it must not be stepped. If its dynamics can be *reshaped* to have one at no
  cost to gameplay (e.g. growth as a hashed event count rather than per-tick
  rolls), prefer the reshaped version.

### core/level-triggered — Prefer level-triggered, idempotent checks over edge-triggered, queued notifications; designs converge from current state

**Designs should converge from the current state in constant space and time:
a check reads what the state IS and acts on it, and checking again is
harmless. Edge-triggered notifications — "something changed", which must be
delivered exactly once and in order — have no place in soft simulation.**
Latency and irregularity are acceptable prices where the process tolerates
them ("multiple layers of unpredictability" are fine for cross-chunk crop
growth if they buy a constant-space, constant-time, convergent solution).

- **Why:** an edge-triggered design is only correct if no notification is
  lost, duplicated or reordered, so it needs queues, delivery guarantees and
  bookkeeping that grows with pending work. A level-triggered design is
  self-healing: a missed check is caught by the next one, a duplicate check
  changes nothing, and there is no queue to grow — only state and a check.
- **Soft simulation has no messages**: a chunk
  publishes state about itself and others read it at their own checks.
  **Nudges** may make a check happen sooner and may say where to look, but
  carry no content, and correctness never depends on them. Exactly-once
  messages belong to the hard simulation (`sim/exact-core`), which is
  designed for its own kind of correctness, not as an exception to this
  tenet.
- **A reusable pattern, timestamped level state** (worked example: U20 in
  `sim/execution-use-cases`): publish not just a constraint but *since when*
  (`(cut, T_cut)`, `(occupied, since T)`), so a reader can re-anchor exactly
  and retroactively whenever it next looks; comparisons of timestamps
  (last-writer-wins) replace acknowledgement messages.
- **Consequences:** random ticks are checks, not drivers of change
  (`sim/random-ticks`); cross-chunk inputs are read as level state at check
  time, not pushed as change notifications; explicit update queues
  (`sim/queued-updates`) are the fallback, not the default.
- **Design test:** for any reaction to change, ask "if this notification were
  lost or delivered twice, would the world still converge to the right
  state?" If not, make it a level check; if it is conserved, it belongs to
  the hard simulation.
- **Kin:** `core/closed-form-first` makes state a function of time; this tenet
  says how the world observes it. Together they replace "update when something
  happens" with "the state is known; check it".

### core/soft-state — Coordination state is soft: it expires unless refreshed, and its TTL is set from the process it coordinates

**State that exists to coordinate — markers, physics proxies, hints,
published pass
times, caches — is soft: each piece carries a TTL and disappears unless
whoever needs it keeps refreshing it.** Authoritative content is never soft.
Without TTLs, bookkeeping accumulates forever in places nobody revisits; with
them, the world sheds it on its own.

- **TTL from the process, so expiry is exact.** Set the TTL to at least the
  time after which the closed form no longer depends on the state — a cut
  marker lasts longer than the plant's full regrowth time. Forgetting it then
  gives exactly the answer it would have given. A shorter TTL that only
  approximates is a deliberate, recorded choice.
- **Expiry is lazy and free.** An expired item reads as absent (expiry is one
  more function of time); it is physically deleted when its chunk is next
  loaded or saved. No sweeper walks the world.
- **Nothing can be stranded.** A participant that vanished — a chunk never
  reloaded, a player who never returned — can't leave coordination state
  behind forever.
- **The boundary is `core/authority-derived-split`.**
  - **Soft:** cross-chunk markers, physics proxies, acceptance hints, caches.
  - **Soft too:** hidden simulation state (scent on a cell, a chunk's own
    schedule of next events), never persisted (`sim/block-metadata`).
  - **Not soft, but stored as nothing more than a state:** closed-form
    processes over block states (a cut reed regrowing, a sapling growing),
    whose timing is a function of the state, the coordinates and the chunk
    epoch, so they end on schedule, never by being dropped.
  - **Never soft:** blocks, ledger state and conserved quantities,
    inventories, player-authored content, and anchors themselves, which *are*
    the state.
- **Prior art:** "soft state" in Internet protocol design (RSVP, IGMP; Clark).
- **Design test:** for every piece of coordination state, ask what refreshes
  it and what its TTL is. If the answer is "nothing", it is either
  authoritative or a leak.

## World, content and fiction

What the world should feel like and how content earns its place.

### core/soft-thresholds — Prefer probabilistic / sigmoid soft thresholds over hard cutoffs

Any decision that reads an **approximate, stale, or parallel-system-produced**
input SHOULD use a smooth probabilistic falloff (sigmoid, S-curve, product of
sigmoids for a "window/bump") rather than a hard cutoff. Hard cutoffs are a smell
to be justified, reserved for inputs that are genuinely **exact and
authoritative**.
- Two independent justifications:
  1. **Believability.** Hard cutoffs are legible and gameable ("mobs spawn past
     exactly 24 blocks"); smooth falloffs read as natural variation and hide the
     simulation's seams.
  2. **They hide the wires between async, unsynchronized systems — and this is
     structural, not cosmetic.** Much of this design trades exactness for
     parallelism and staleness: neighbours' state may lag, subsystems may skip
     steps under load, worldgen fields are interpolated off a coarse sample grid,
     spawn distance reads a stale player position. A **hard cutoff turns a
     small input error into a discrete wrong outcome** (popping, flicker,
     boundary artifacts); a **sigmoid turns a small input error into a small
     output error** (prob 0.42 vs 0.44 — imperceptible).
- **Governing rule:** a decision's sensitivity to input error should be
  proportional to how much that input is actually trustworthy.
- **Clean partition with `core/worldgen-integer-domain`:** worldgen decisions are
  discrete and exact BY DESIGN — hard thresholds are correct there. Runtime
  decisions reading fuzzy inputs are soft. The input's trustworthiness tells you
  which regime you are in.

### core/earned-vastness — Vastness and proliferation must be justified by perceptible complexity, not raw count

**Players see past raw size and count to the amount of hand-built or
thoughtfully-generated content behind it.** They are remarkably good at
intuiting the *Kolmogorov complexity* of what they're looking at — the length of
the shortest description that generates it — and a space (or a catalog of items,
biomes, mobs, dimensions) whose apparent richness compresses to a short
description reads as padding, however physically large or numerous it is. So raw
vastness and proliferation are not free: **each unit of size or count must earn
its existence by adding complexity the player can actually perceive.**

- **The quantity is player-PERCEIVED complexity, not generator complexity.** A
  trillion procedurally-different chunks that all reduce to "the same ruleset
  tiled" is low perceived complexity even though the byte-stream is huge; a small,
  dense, hand-tuned region is high. Metering the generator's information is the
  worldgen-side mechanism (`worldgen/metered-information`), but the TEST is what
  the player can distinguish and learn.
- **Applies to proliferation, not just physical size:** a new dimension, a new
  biome, a new tier of items/mobs/materials, a new region — each must break *new*
  perceptible ground. "We added 40 biomes" is worthless if a player can't tell 35
  of them apart; one more dimension is dead weight if it reads as a reskin (the
  test that rejected the elemental and mirror dimensions, `dimensions/pilot-set`).
- **The design test for any "add more" proposal:** *does this raise the
  shortest-description length a player would need to account for what they
  experience?* If the addition compresses away — same rules, bigger number — it
  fails, and the effort belongs in deepening existing content instead. Vastness
  is a stage for content, never a substitute for it.

### core/low-fantasy — Low fantasy enlarges the world; high fantasy shrinks it

**Prefer a low-fantasy framing: ordinary people, mundane interactions, and
deadly, arbitrary threats.** A high-fantasy setting, where the player is a
destined hero and the world is at stake, shrinks the world's perceived vastness
and mystery by bending everything around the player's narrative gravitational
field. Low fantasy grows the world and preserves its mystery: things exist for
their own reasons, not because the player is coming.

- **Why:** when the world revolves around the player, every place, creature
  and secret reads as staged for them, so the world compresses to "the
  player's story" (`core/earned-vastness`: players discount what compresses).
  A world indifferent to the player is larger than one that needs them.
- **How it guides a sandbox:**
  - The player is an ordinary person in the fiction, not a chosen one; nothing
    in the world is waiting for them, and no world-ending stakes hang on them.
  - Threats can be deadly and arbitrary rather than scaled to the player's
    level or story beats; danger is a property of the world, not a narrative
    device.
  - **Animals are deadly.** A bull or a bear can easily kill an
    armed, armoured human; real threat doesn't require positing otherworldly
    monsters.
  - **The stabilization immune response is consistent with this tenet**: it
    scales to the player's *investment*, not to the player as
    the ultimate threat or hero of destiny, so the world may regard a player
    as a minor inconvenience.
  - Mundane interactions — fire, food, shelter, walls, a lantern — carry real
    weight.
  - Mystery is left unexplained where explaining it would make it about the
    player (lore in Enigma's archive is found, not delivered).
- **Design test:** "does this make the world revolve around the player?" If a
  feature exists to make the player special or central, prefer a version where
  the world simply is the way it is and the player is one inhabitant of it.

### core/diegetic-authoring — World-building and content authoring are in-world, not a separate code/data boundary

Content-authoring acts should have an **in-world, diegetic expression** rather
than living on a privileged code/data/editor tier. Players author by binding
preconfigured engine capabilities through in-world tools and the content grammar
(`data/grammar`) — never by writing code. The MUD lineage: build from inside the
world, not by editing the server.
- Examples (illustrative, not committed features): alchemy/necromancy distills a
  creature's AI into a bottled "soul", later bound to a sufficiently-aligned body
  (corpse, construct, machine); a player sculpts an N³ colored surface and
  registers it as a new in-game block.
- This tenet is the content-creation analog of `core/authority-derived-split`:
  refuse to split authoring across a false code/world boundary, as that one
  refuses to split state across a false authoritative/derived boundary.
- **Two-layer authoring taxonomy** (different power, different safety mechanism):
  - **Declarative authored content** — static definitions (blocks, canned souls)
    in the `data/grammar` binding language, which is non-Turing by ROLE (a
    binding agent, not a computation medium). Safe because it does not compute.
  - **Executable authored machines** — logic contraptions / mechanical souls
    (`sim/logic-machines`). These CAN be Turing-complete, and that is intended.
    Their safety is **capability confinement**, not computational restriction:
    sealed inside the game universe, with no network, filesystem or host
    access, and only canned sensor/actuator modules for I/O. Fairness comes
    from a scheduler-style **declining CPU budget**, not a loop ban.
- Hard consequence (multiplayer): authored content crosses a **trust boundary**
  when shared (`data/authored-content`, `data/content-bundles`); integrity is
  not safety.
- **Governing stance (MIT open-source, mechanism-not-enforcement):** the project
  does NOT hard-restrict user behavior. The engine provides mechanisms
  (authoring, bundling, trust labels, adoption choices); it does not police taste
  or prevent self-inflicted harm. Safety mechanisms exist to let communities
  protect THEMSELVES (ratings, revocation, adoption), not to let the engine
  impose a global policy.

### core/diegetic-cost — Significant / expensive engine operations are projected into the world as diegetic actions

A semantically-significant or resource-intensive engine operation should be
surfaced as a **diegetic in-world action**, not hidden behind a menu or done
invisibly. The player's fiction and the engine's cost model stay aligned, and —
crucially — **player intent hands the engine its optimization / invalidation
permissions** for free.
- Distinct from `core/diegetic-authoring`: that tenet is about *who creates
  content and where*; this is about *engine operations and their costs being
  visible as fiction*.
- Why it is more than flavor: expensive operations usually need a precondition
  the engine would otherwise have to *infer* — compilation needs a stability
  guarantee, caching needs an invalidation signal. When the precondition is a
  diegetic act, the player **hands you the invariant by acting**: the act that
  "finalizes" a thing is the permission to optimize it; the act that "changes"
  it is the cache invalidation.
- Already pervasive: reality stabilization (`persist/diegetic-persistence`),
  soul distillation gating the ledger mint (`data/entity-ledger`), publishing
  a bundle (`data/content-bundles`), reality bombs, and sealing a mechanical
  soul as an in-world commitment to compile it (`sim/logic-machines`).
- It is the inverse of hiding the machinery: EXPOSE cost as fiction, so cost is
  intuitive, tuning *is* gameplay, and there is no separate settings layer for it.

### core/realistic-in-kind — Realistic in kind, forgiving in magnitude

**Pressures and processes are realistic in kind and forgiving in
magnitude**. A need or hazard
has a real-world cause and a real-world remedy, so it is learnable and
diegetic, but its size, rate and penalty are scaled down until it is pressure
rather than tedium. This is common to many games with a survival element.
- **Instances already decided:** heat raises thirst up to 2–3× instead of
  causing heatstroke (`pressure/basic-survival`); sleep is 3 game hours per 2
  nights, paced to real-world breaks (`first-session/sleep-and-day`); water is
  needed once per 2 days, and a fresh source needs no container; days without
  food are survivable, with an invisible caloric balance; carry weights are
  gamified (`inventory/encumbrance`).
- **Test:** is the cause and the remedy something a player could reason
  about from the real world? If not, it's not realistic in kind. Does
  following it feel like a chore? If so, it's not forgiving enough in
  magnitude.

## Gameplay and engagement ethics

What kind of engagement this game is allowed to produce, and the specific
mechanics it must NOT use to produce it. This section is a **standing constraint on
tuning**, not a feature list: most of these are decided not by whether a system
exists but by the *numbers and framing* chosen when it is implemented, so the
warning has to sit next to the design rather than be discovered at balance time.

### ethics/wanting-coupled-to-liking — The pull to keep playing must stay proportional to the satisfaction delivered

**The pull to keep playing must stay
proportional to the satisfaction delivered**. The mechanism of
exploitative design is Berridge's *wanting* vs. *liking* dissociation (real
neuroscience): exploitative systems maximize *wanting* (compulsion to
continue) while delivering little *liking* (actual satisfaction). Every
engagement mechanic is tested against this coupling. The grounding for the
endorsed-engagement half is Self-Determination Theory (competence / autonomy /
relatedness; Rigby & Ryan, *Glued to Games*).

The operational test for the whole category: **"do players endorse having
played"**, not **"do they keep playing."** Retention is not the metric;
endorsed time is.

Why this game starts with the advantage: a **self-directed sandbox is
structurally the least exploitative genre**, because the player owns their own
reward schedule. Every rejected mechanic below is a way of seizing that
schedule back from the player — so the rule is simply *don't take it back.*

### core/no-meta — Avoid a meta: find clearly optimal choices and balance them away

**When one gameplay choice is clearly optimal, treat it as a defect: find it and
balance it.** Some players can't resist following the optimal path even when
they enjoy it less, so a dominant strategy quietly overrides what players would
otherwise choose; and every alternative it dominates becomes wasted content.

- **Why:** a meta converts a space of interesting choices into one correct
  answer. It harms the players who optimize (they play the dull path) and the
  content (everything off the path stops being used). It is also an ethics
  issue: an optimum players feel compelled to follow while liking it less is
  wanting decoupled from liking (`ethics/wanting-coupled-to-liking`).
- **What it is not:** it doesn't forbid optimization *within* a system the
  player builds (a well-designed farm or machine is the point of a sandbox).
  The target is a dominant option among alternatives the game offers — one
  tool, material, build, route or strategy that beats the others everywhere.
- **How to balance:** prefer situational trade-offs over flat superiority (each
  option best somewhere); diminishing returns and soft thresholds
  (`core/soft-thresholds`) rather than linear stacking; costs that scale with
  power (`core/diegetic-cost`); and, where an option is dominated everywhere,
  cut it or give it a niche rather than leave it as dead content
  (`core/earned-vastness`).
- **Design test:** for each choice the game offers, "is there an option a
  well-informed player would always pick?" Check it at design time, and in
  playtests by watching the distribution of choices: a choice nearly everyone
  makes the same way is a meta candidate.

### Rejected mechanics (the known-exploitative toolkit)

These are design-against targets. If a proposed system reduces to one of these,
that is a bug in the design, not a tuning knob.

### ethics/no-variable-ratio-core — No variable-ratio reward schedules on core progression

**No variable-ratio reward schedules on core
progression**. Unpredictable, slot-machine-math payoffs on the actions
the player *must* take to advance are the single most potent compulsion lever.
Keep randomized payoffs away from the core loop; reward for advancement is
legible and earned, not a pull of the lever.

### ethics/no-appointment-mechanics — No appointment / time-gate mechanics

**No appointment / time-gate mechanics**
Nothing may demand the player return at a specific wall-clock time or
lose value. This is the **key risk for THIS design**: the investment-tiered
decay curve (`persist/diegetic-persistence`) becomes an appointment mechanic
the moment it **punishes absence** instead of **rewarding presence** — the same
mechanic, opposite ethics, decided entirely by framing and numbers
(`persist/presence-not-absence`, next).

### persist/presence-not-absence — Decay rewards presence; it must never punish absence. A single-player world is pausable; multiplayer supports a consensus pause
- **Open:** exact decay-curve numbers (`persist/decay-curve`).

- **The ethical constraint on the signature mechanic.** Decay
  (`persist/diegetic-persistence`) is the design's most load-bearing mechanic
  and its largest exploitation risk. "Unstabilized world reverts over world
  time" is ethical or exploitative *entirely by framing and numbers*:
  - **Appointment framing (FORBIDDEN):** "your base decays while you are
    away, so log in or lose it." This punishes absence and manufactures
    obligation: wanting decoupled from liking.
  - **Reward-presence framing (REQUIRED):** stabilization lets you *hold what
    you invest in*, the world is *forgivingly regenerative* (reversion is
    toward seed, mints nothing, and anything diggable regrows), and absence
    is *cheap*. Decay is a reason to build and return *because you want to*.
- **The rule `persist/decay-curve` is tuned against:** if a decay number makes
  players return out of dread rather than desire, it is wrong *regardless of
  retention*. Concretely: decay is slow and forgiving by default; losing an
  unstabilized area costs *shape*, never invested *entities* (ledger contents
  sink to the equity account, never vanish); and the world offers clean
  stopping points.
- **Pause is the structural guarantee, not just a framing.** Decay reads
  `WorldTime` (`coords/typed-time`), never `WallTime`.
  - **Single-player: the world is pausable.** Pausing freezes `WorldTime` —
    decay, seasons, everything — so stepping away costs nothing.
  - **Multiplayer: a consensus pause option**, when all active participants
    agree; mechanism and quorum are open (`persist/pause-semantics`).
  - Pause is cheap because absence is evaluated in closed form on load, never
    caught up (`core/closed-form-first`): `WorldTime` simply stops.
- **Real time is risky only where absence causes LOSS.** It does two separable
  jobs with opposite ethics:
  - **Shared rhythm (benign):** night is really night, seasons turn, a hearth
    is still warm when a friend logs in later. The world has its own life, and
    an absent player loses nothing; it is also why a shared world can't pause
    unilaterally.
  - **Loss deadline (harmful):** the same clock turned into jeopardy that
    accrues during absence ("crops die in 8h"). The firewall is the
    conservation rule: rhythm may change shape, but it may not eat what the
    player earned.
- **In multiplayer the floor is the stabilization ECONOMY, not a pause.** An
  absent player's unstabilized build decays in real time, and only the economy
  resolves that:
  - Building without stabilization is an opt-in risk, never a forced one.
  - So **basic stabilization is a day-1 affordance**, reachable with early
    resources; progression unlocks bigger, longer and permanent tiers, but
    the entry tier can't sit behind a grind. Otherwise the appointment
    mechanic is rebuilt at the shared-world layer however kind the framing.
  - The checkable rule: **the cost of holding what you care about against the
    clock must be payable by presence the player WANTED to spend, not
    presence extracted by fear.** It constrains `pressure/hearth-mechanics`
    and `mobs/suppression-systems` tuning as well as `persist/decay-curve`.
- **Recorded exception by choice: torches and the early
  hearth tiers burn down on world time, even while a multiplayer player is
  offline,** to keep the urgency of a Minecraft-like first day; many players
  (Michael included) enjoy that time pressure. It stays inside this tenet
  only while the release valve, the lantern (a short-range, non-consumable
  stabilizer, `pressure/hearth`), is a reasonable first-session stopping
  point. The check, in playtests: a first session reaches the lantern
  (including making its safe range achievable) and stops with nothing urgent
  burning. If typical players need a second session just to secure their
  first base, the chain is too long and this becomes an appointment
  mechanic.
- **Scope:** the logic generalizes to mob pressure, hunger and any future time
  gate; decay is simply where the design is most at risk.

### ethics/no-fomo — No FOMO / limited-time content

**No FOMO / limited-time content**. No artificial
scarcity that pressures play *now* rather than when the player wants to.
Content is available when the player reaches it, not when a clock allows it.

### ethics/no-manufactured-friction — No artificial friction sold away

**No artificial friction sold away**
Do not insert tedium whose only purpose is to be removed by payment or
grind — i.e. manufacturing the problem you then sell the solution to. Friction
must earn its place as genuine gameplay, never as a toll.

### ethics/clean-stop — Let the player stop cleanly

**Let the player stop cleanly**. The player must
be able to end a session without penalty or loss. Emergent-goal games get this
for free (the player sets the goal, so they can declare it done); the
constraint is to **not design it away** — no unfinished-business hooks engineered
to make stopping feel like a loss.

### Relationship of the engagement ethics to the rest of the design

This is the ethical reading of decisions already made elsewhere, pulled into one
place so the constraints are visible before the numbers exist:

- `persist/presence-not-absence` is `ethics/no-appointment-mechanics` applied to
  the decay curve — the one place the trap is nearest — including the recorded
  torch and hearth burn-down exception and its first-session condition.
- `loop/deferred` (the deliberately-deferred gameplay loop) must be designed
  under `ethics/wanting-coupled-to-liking`; the loop knobs are exactly where a
  variable-ratio or appointment mechanic could creep in.
- The sandbox's player-owned reward schedule (`sim/player-embodiment`,
  emergent goals) is the structural reason for the advantage named in
  `ethics/wanting-coupled-to-liking` — and the
  thing the rejected mechanics would erode.
