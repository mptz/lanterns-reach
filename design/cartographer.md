# worldgen/cartographer — The Cartographer: chunk generation

The Cartographer provides a stateless, parallel, asynchronous,
player-independent world generation service. A requester names a box (a
chunk, or a larger volume of distant terrain) and a level of detail, and
is returned ownership of chunk data for it in *generated baseline* form:
the world as it would be if nobody had touched it. It encapsulates the
whole noise schedule (fields, octaves, levels, sample grids, lattice
offsets, the composite `surface`): no caller knows there are octaves. The Cartographer is called only from goal actors' and function actors'
turns, which run on the Dispenser's worker threads. It knows nothing about
who wants the chunk or why, and plays no role in post-worldgen chunk
evolution or persistence. Callers see it as pure functional; internally
it may cache work shared across chunks, such as structure plans;
earlier levels of detail reach it as an ancestor's node data in the
request, not through its cache. It runs on the server, and possibly on clients that hold
the seed keys necessary to generate an area of the world.

`design/concurrency.md` keeps the assumptions, invariants and connective
tissue the Cartographer shares with the rest of the model; this file
holds the Cartographer's own role, invariants and open questions. What
it generates lives in `design/worldgen.md` (fields), `design/regions.md`
(regional fields, provinces, poles), `design/worldgen-features.md`
(features and structures), `design/seed-keys.md` (keys) and
`design/dimensions.md` (the dimension warp).

**Cartographer** is proposed as the C++ world-generation class name.

## Architectural role
(Summarized from `sim/concurrency-model`.)
- **The Surveyor decides what to generate** (from published viewpoints) and
  registers it as goal actors with the World's `GoalSource`, which offers
  them to the Dispenser (`engine/goal-source`): generation and refinement
  have dependencies (a node's parent, structure plans), so they are goal
  actors, run on worker threads, any number at once.
- **Only goal actors and function actors call the Cartographer.** The
  actor holds the chunk data it computes: for a chunk outside every
  Vicinity the World owns that goal actor (`world/chunk`; a TerrainNode's
  sibling set is one, `world/surveyor`, `engine/goal-source`), or the chunk's actor
  receives it as a message and installs it in its own turn; the Surveyor records the stage reached (a
  fact, never a request). No actor's turn calls the Cartographer.
- **The output is chunk data, not a chunk:** an owned value at one level
  of detail: at a coarse level const node grids, from which the next
  level's refinement starts; at full detail move-only cells
  (`world/chunk`).
- **Seed keys are cached by the Cartographer** and come from the key
  provider (`worldgen/seed-keys`); see "Requests and keys" below.
- **Full-detail output carries its content digest**
  (`data/content-digest`), which sampled regeneration ("Checks") compares;
  coarser output carries none, and sampled regeneration compares that data
  directly.

## Requests and keys
- **A request is (dimension, box, level of detail, and an ancestor's node
  data, if any).** The ancestor's data is the Cartographer's own earlier
  output, const and opaque to callers, from which refinement starts: the
  parent for one level, a resident ancestor several levels up when one
  request composes several levels at once, none at a root. The box is a chunk,
  or a world-aligned power-of-two volume no larger than the key region of
  its coarsest octave (`worldgen/seed-keys`). The result is chunk data for
  the box at that level: cells at full detail; at a coarse level, its
  composed fields on the node's sample grid, for each field with an
  octave at that level, rather than cells. **Refinement is one request for
  a node's eight children,** given the node's data as the ancestor, and
  returns the sibling set (`world/octave-pyramid`) (`world/octave-pyramid`, `world/chunk`). Volumes of distant terrain are first-class requests, not
  an optimization.
- **`requiredKeys(request) → KeySet`** (pure, cheap): the Cartographer's
  own prediction of the key regions the request will touch, per release
  tier of the requested level: the box widened by the blend's transition
  width and the boundary warp's amplitude (`worldgen/seed-keys`). Only the Cartographer knows the octave schedule, so
  only it can answer. The World uses it to plan: which keys to release to
  a client, and which to prefetch so that a miss is rare.
- **The Cartographer keeps every key it is given,** until restart, in a
  cache shared across worker threads. Keys never change, so holding one
  changes no output, only whether a request can run; inserts happen once
  and need only the cache's short lock
  (`invariant/cartographer/never-waits`).
- **A missing key is a fact, not a wait:** generation that finds a
  declared key missing returns "needs keys X" without blocking; the World
  obtains them (derived on the server, requested from the server on a
  client) and reissues the request.
- **A needed key outside the declared set is a fatal error,** in every
  build: generation touched a region `requiredKeys` didn't predict (a warp
  tuned past its assumed amplitude, an off-by-one in a transition, an octave
  added to generation but not to the prediction), so key release is wrong.
  It shows only on a cache miss, so it may surface intermittently; debug
  builds also check every lookup against `requiredKeys(request)`, so tests
  catch it deterministically.

## Checks
These mechanisms check the invariants below; each invariant's
**Enforced** line names the ones it relies on.

- **Build boundary** (type, compile time). The Cartographer's build target
  sees only an allowed set of headers: seed keys, the generator
  specification, chunk representations and the noise library. Everything
  else (players, cameras and local positions, the Surveyor, chunks,
  Vicinities, the Dispenser, the ledger) is out of reach (`deps/tooling`).
  Its API types do the rest: the result is a move-only owned value, and
  there is no cancellation parameter.
- **Keys as capabilities** (type, plus fatal checks and a debug audit).
  Server-only and client-releasable keys are distinct types, so a client's
  Cartographer can never hold a secret branch's key. A needed key outside
  `requiredKeys(request)` is fatal on a cache miss in every build, and
  every lookup is checked against it in debug builds.
- **Entry guard** (fatal check). A scope guard at the API entry asserts
  that the Dispenser's worker marker
  (`invariant/dispenser/worker-marker`) says the thread is in a goal
  actor's or a function actor's turn.
- **Sampled regeneration** (telemetry or watchdog, in release). A small
  sampled fraction of outputs is regenerated under varied context (cold
  cache, another thread, a different order relative to other requests)
  and compared, by content digest at full detail and by the data itself
  when coarser.

## Invariants
- **`invariant/cartographer/no-players-or-vicinities`** — The Cartographer
  has no knowledge of players or Vicinities, and no camera or local (f32)
  positions; f32 inside noise sampling is fine.
  - **Enforced:** type: the build boundary, via typed positions
    (`coords/typed-positions`).
- **`invariant/cartographer/pure-function`** — The Cartographer is
  conceptually a pure function of seed keys and generation specification
  (dimension, coordinate, stage / LoD, generator version) to generated
  chunk data; side effects (logging, internal caching) don't appear at the
  API.
  - **Enforced:** telemetry or watchdog: sampled regeneration (varying the
    context is what tests purity rather than repeatability).
  - **Why:** caching exists because noise and structure generation may
    produce information useful to chunks not yet requested (structure
    plans), and it is invisible to callers; refinement reuses earlier work
    through the ancestor's node data in the request, itself a function of
    keys and specification, so purity holds.
- **`invariant/cartographer/worker-turns-only`** — The Cartographer is
  called only from goal actors' and function actors' turns, on the
  Dispenser's worker threads: no other execution context, and never inside
  a stateful actor's turn.
  - **Enforced:** fatal check: the entry guard.
  - **Why:** it is a library-style API, not a Dispenser source or
    consumer: the Surveyor decides what to generate and registers it as
    goal actors, each of which calls the Cartographer and holds the chunk
    data it computes.
- **`invariant/cartographer/same-machine-determinism`** — Generation is
  deterministic on the same machine, binary and SIMD level, and that is
  sufficient: newer binaries may change worldgen, and a generated world
  moved between architectures (Intel to ARM) needn't be identical.
  - **Enforced:** telemetry or watchdog: sampled regeneration.
  - **Why:** where a client generated terrain, the server overriding it is
    fine, like any other non-authoritative client data; so the fastest SIMD
    path is used, at least for now (revising the "pin the SIMD level per
    world" idea in `deps/noise` and `core/worldgen-integer-domain`).
- **`invariant/cartographer/never-reads-world`** — The Cartographer never
  reads the world: no Surveyor, chunk, Vicinity or edited state; it
  generates the baseline only, and saved edits are applied by the chunk on
  load.
  - **Enforced:** type: the build boundary.
- **`invariant/cartographer/never-waits`** — The Cartographer never waits
  on another part of our design: not on another turn's result, the
  Surveyor, a chunk or a Vicinity; information about a neighbouring region
  is computed (pure, cacheable), never awaited. OS calls, logging and the
  shared cache's mutex are fine, and OS APIs are not wrapped or banned.
  - **Enforced:** type: the build boundary (nothing it could wait on is
    visible, and no future-of-a-result API exists).
  - **Why:** so generation is deadlock-free by construction. Guideline for
    the cache: compute outside the lock, insert under it; duplicate
    computation is benign.
- **`invariant/cartographer/transfers-ownership`** — The Cartographer
  transfers ownership of what it generates: the result is an owned value,
  held by the calling goal actor or function actor, and the Cartographer keeps no handle
  into it.
  - **Enforced:** type: the build boundary and the API types.
- **`invariant/cartographer/invisible-cache`** — The cache is invisible: a
  hit equals recomputation, eviction at any moment changes nothing, and
  concurrent computation of one entry is benign. The key cache is not this
  cache: it holds inputs, not results, and only decides whether a request
  can run.
  - **Enforced:** telemetry or watchdog: sampled regeneration (the
    cold-cache variant).
- **`invariant/cartographer/declared-reach`** — Output for a request
  depends only on the keys of the regions `requiredKeys(request)` names,
  and callers know it.
  - **Enforced:** type, fatal check and debug audit: keys as capabilities.
  - **Why:** the declared reach bounds how far seed keys are released to a
    client (`worldgen/seed-keys`), lets the World prefetch and group nearby
    requests, and keeps generation honestly local.
- **`invariant/cartographer/no-conservation-effects`** — The Cartographer
  never creates ledger-tracked (conserved) entities: everything it emits is
  baseline, outside the ledger (`data/entity-ledger`), and placeholders are
  real blocks in the generated chunk (generic ore-bearing rock, unfilled
  containers).
  - **Enforced:** type: the build boundary.
  - **Why:** specialization happens outside worldgen: the authority
    resolves and mints ore on reveal with server-only keys
    (`geology/ore-sites`), and container contents are assigned on first
    access. This also raises x-ray resistance.
- **`invariant/cartographer/secrets-on-server`** — A client's Cartographer
  only ever holds sub-keys for released regions.
  - **Enforced:** type, fatal check and debug audit: keys as capabilities.
- **`invariant/cartographer/exact-representation`** — Each chunk is
  emitted in a representation that holds it exactly, never altered to
  fit; choosing the representation is co-designed with generation and
  fully encapsulated in the Cartographer.
  - **Enforced:** test: each representation has round-trip tests; there is
    no runtime check.
- **`invariant/cartographer/no-cancellation`** — The Cartographer has no
  external cancellation; a result no longer wanted is the Surveyor's
  concern (keep, cache or discard).
  - **Enforced:** type: the build boundary (the API has no cancellation
    parameter).
  - **Why:** each LoD stage is its own request, giving natural yield
    points.

## Features that cross chunks
Ordinary trees are stamped, not continued (`worldgen/tree-generation`):
a box's request enumerates every tree root within the box plus a halo of
each species' maximum reach, evaluates each tree as a pure function of its
root block's state and coordinates, and writes only the box's own cells,
so the Cartographer still never reads a neighbour. Huge trees, and all
growth after worldgen, continue into neighbouring chunks as soft
simulation (`worldgen/feature-continuations`): the Cartographer generates
the part of such a tree inside its origin chunk and emits the continuation
record at the face as part of that chunk's baseline, so its output stays
pure; the receiving chunk applies the record in its own turn, outside the
Cartographer. Reach is bounded because the record encodes a cap on further
growth extent. Unlike a
stronghold (`worldgen/structure-plans`: a pure plan, generated out to its
maximum radius so it exists before any chunk it touches), a tree may
visibly grow into existing chunks, including towards the player; that is
accepted for living features and not for static ones. Once the base is
generated, the rest is running world simulation: the aim is rough
predictability, not correctness, though continuations aim to be
commutative so the untouched world stays order-independent.

## Boxes, refinement and bounds
- **One request per box:** chunk boxes near the player, volumes of
  distant terrain further out, the coarse octaves coming from the
  ancestor's node data in the request (below), so neighbouring requests
  share them through the tree; the Dispenser bundles cheap requests into
  batches.
- **Refinement is hierarchical:** terrain nodes form a sparse octree
  (`world/surveyor`), and a node's request generates only the octaves of
  that node's own bin; the coarser octaves come from its parent's composed
  field, generated once. The request carries the ancestor's node data and
  the Cartographer composes the node's field from it
  (`world/octave-pyramid`: the recursion), then its bounds and its
  classification, so coarse detail is never regenerated for each
  descendant and no cache of composed fields is needed. Purity holds
  (`invariant/cartographer/pure-function`): the ancestor's data is itself a
  function of keys and specification, and sampled regeneration regenerates
  from the root with a cold cache.
  Node data is const and never modified. The ancestor is always the
  baseline, never edited content (`invariant/cartographer/never-reads-world`),
  so an edited chunk's
  override grids sit beside a node, never in it.
- **The real saving is bounds classification:** the coarser levels plus a
  bound on the remaining octaves' amplitude (`world/octave-pyramid`) show, before any fine
  evaluation, which parts of a box are certainly solid or certainly air,
  so the finest octaves run only near the threshold.
- **Edited chunks are loaded, not generated:** their detail comes from
  their chunk blob, and their coarse view is projected up from their content.

## Deferred and open
- **Deferred:** constraints on what LoD refinement may change in general
  (too ambiguous yet). For trees, the placeholder and the real tree come
  from one function (`worldgen/tree-generation`), so the real tree resolves
  within its placeholder by construction.
- **Open:** the cache's structure and eviction policy (structure plans and
  other shared work; bounded by memory), how `requiredKeys` is computed
  from the generator spec, and how a generator version is named.
