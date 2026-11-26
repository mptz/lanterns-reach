# Worldgen features

Features that cross chunk boundaries (trees, large structures) and how
chunks generated independently agree on them. Features read the fields of
`design/worldgen.md` (`worldgen/field-driven-features`).

## worldgen/tree-generation (draft) — Worldgen trees are pure functions of a root block and its coordinates, stamped by every chunk they reach

- **A tree grows from a root block** placed flush with the ground, one
  unoriented block type per species, with a few states (8, say).
- **The root's states are worldgen's knobs, not its variety:** the fields
  that chose the species and its density at that place (climate, soil,
  exposure) are sampled once, when roots are placed, and their result is
  written into the root's state (a size or age class, vigour, a form such
  as upright or leaning). The tree function reads the state rather than
  resampling fields, so trees grow smaller where the climate suits the
  species less without the tree evaluating any noise.
- **Variety comes from the coordinates:** a tree is a pure function of
  (species, root state, the region's seed key `worldgen/seed-keys`, root
  coordinates), so every position grows a different tree from one state,
  with no metadata stored. Further inputs (a field sampled at the root) are
  added only if strictly needed. A tree reads no neighbouring tree or live
  state, so generation stays independent of order.
- **Stamping, not continuation, at worldgen:** each species has a
  deterministic placement pattern (a jittered grid keyed by coordinates,
  thinned by fields), so a chunk can enumerate every root in any box. A
  chunk's feature stage (`world/surveyor`) evaluates every tree whose root
  lies within its box plus a halo of the species' maximum reach, and writes
  only its own cells, as indices into the generator's fixed palette
  (`world/brick`), mostly as runs along x. That is a pure function of the
  chunk: no cross-chunk dependency, ordering or continuation record. A tree
  near a corner is evaluated by up to eight chunks, which is cheap for
  ordinary trees. Overlaps between trees merge by the same ranked rule as
  continuations (`worldgen/feature-continuations`: log over leaves, then
  the lower origin hash).
- **Huge trees, whose reach exceeds a practical halo,** use continuations
  (`worldgen/feature-continuations`), as does all growth after worldgen.
- **The coarse form comes from the same function:** the distant placeholder
  (a trunk column and a canopy superclass, `world/progressive-lod`) is the
  tree function's coarse output, so the real tree resolves within its
  placeholder by construction (the constraint `worldgen/cartographer`
  defers).
- **Trees grown after worldgen also hash their growth time:** a sapling
  grows at the official time T its closed form draws (growth is inherently
  random, `sim/multi-rate`; `sim/block-metadata`), and the tree it becomes
  also hashes T itself. It stays a closed form: whatever predicts when it
  grows predicts what grows, and nothing is stored before then. A tree felled
  and replanted at the same spot grows differently, because T differs.
  World time, not real time, so replays, paused worlds and catch-up stay
  deterministic. Worldgen trees use no growth time: they must stay a pure
  function of coordinates, since game clients regenerate unedited terrain
  from their keys.
- **Open:** each species' placement pattern and maximum reach; the root's
  states per species; the halo beyond which a tree counts as huge.

## worldgen/feature-continuations — Cross-chunk features (trees) generate by continuation into neighbours, merged order-independently

- **Scope:** continuations carry growth after worldgen (saplings, trees
  growing into chunks that already exist), huge trees whose reach exceeds a
  practical halo, and other localized simulation that crosses chunks.
  Ordinary worldgen trees don't use them: they are stamped by every chunk
  they reach (`worldgen/tree-generation`), the "overlap" alternative below.

- **Continuations are soft simulation.**
  Trees are small structures. Like any structure they reach beyond their
  origin chunk; unlike a stronghold, a tree's base is generated as part of
  its origin chunk and grows outwards, including into chunks that already
  exist and towards the player.
  - **Who does what:** the Cartographer (`worldgen/cartographer`) generates
    the tree inside its origin chunk and emits the face record as part of
    that chunk's baseline; the receiving chunk continues the tree in its
    own turn as soft simulation. The Cartographer never reads a neighbour.
  - **Bounded reach by a cap:** at the chunk edge the continuation encodes
    a cap on further growth extent, so a tree's reach from its root is
    bounded and declarable, not merely terminating.
  - **Visible growth is accepted:** pop-in and growth into existing chunks
    are accepted for living, soft features, which a static structure
    wouldn't be allowed (that is why strongholds use
    `worldgen/structure-plans`, generated out to their maximum radius).
    This widens "pop-in at the generation frontier", below.
  - **Predictability, not correctness**: once a tree's base is
    generated, the rest is running world simulation. The aim is rough,
    imperfect predictability; timing interactions with other soft
    simulation (a tree growing into a cell water has just flowed into) are
    accepted.
  - **Aim: order-independence in the untouched world** (probably not 100%
    achievable; the conditions are proposals). Continuations aim to be
    commutative, so that if nothing
    but generation and tree growth has touched the cells involved, the
    outcome is a pure function of seed and coordinate, independent of
    generation order. The conditions:
    1. a tree reads only baseline terrain, never other trees or live state
       (rule 1 below);
    2. every contribution is a ranked per-cell proposal and the cell keeps
       the highest rank (log over leaves, then lower origin hash); removals
       such as a root turning grass to dirt are ranked proposals too, and no
       step is conditional on a cell's current value;
    3. the edge cap derives from the record alone, never from the
       receiver's state;
    4. growth over time is closed-form in age (`core/closed-form-first`),
       so the outcome is a pure function of seed and time;
    5. "untouched" is provenance, not value: continuations skip cells
       changed by anything other than generation (the player, fire,
       fluids, other growth), even if changed back, which needs a sparse
       per-chunk changed-cells set (likely free from saving changes as
       deltas from baseline).
    Where it falls short, by acceptance: cells touched by other simulation,
    decay to baseline, partly-arrived trees at coarse LoD stages (open,
    below), and generator versions
    (`invariant/cartographer/same-machine-determinism`).

- Decision: a feature that crosses a chunk face during **creation** (the
  worked example: a tree generated from its base) does not re-derive from its
  root in every chunk it touches. When generation reaches a face, the origin
  chunk leaves a **tip state** in the tip cell (tree type and growth
  remaining, as small classes; there is no per-block record,
  `sim/block-metadata`), and the branch's randomness is rederived by
  hashing the tip's coordinates and state; the tip is published as a marker
  (a projection of that state). The receiving neighbour **continues** generation
  from it. The marker also carries a **tick hint**: a request for an
  immediate generation step in the receiver. Not a correctness matter — it
  only keeps the rest of the tree from appearing late.
- **No waiting for neighbours.** A chunk is shown and playable as soon as its
  own generation is done; later continuations spread into it. This is sound
  because:
  1. **A feature reads only base terrain, never other features.** Each tree is
     generated as if alone against the deterministic noise-stage terrain;
     overlaps are resolved afterwards by the merge. (If a branch stopped at
     another tree's log, shape would depend on arrival order.)
  2. **The merge is commutative, associative and idempotent:** log beats
     leaves; between same-kind cells the lower origin hash wins (max-like).
     The generated world is therefore identical in any generation order, and
     re-applying a continuation changes nothing.
  3. **Generation never overwrites non-generated content.** A cell a player
     built or the sim changed before the continuation arrived wins; the branch
     is cut there. That makes the outcome depend on timing relative to the
     player, which is acceptable divergence for soft content
     (`core/soft-over-hard`); the edit-free world stays exactly repeatable.
- **Consequences:**
  - **Locality holds:** every hop goes only to one of the 26 neighbours, so
    `core/chunk-locality` and the speed-of-information bound are unchanged.
    Trees may span more than two chunks (tall trees): that is just more hops.
    **Termination** is by construction: growth remaining decreases each hop.
  - A continuation into a chunk whose base terrain doesn't exist yet stays
    **pending** (authoritative, persisted if generation is interrupted) until
    that terrain exists. A tree that wraps back into its origin chunk works
    the same way, a few steps later.
  - **The RNG is integer and portable** (`core/worldgen-integer-domain`), so a
    record's continuation is the same on every machine.
  - **Decay to baseline loses neighbour-rooted parts — accepted**.
    A chunk regenerated alone (`persist/diegetic-persistence`) lacks the parts
    of trees rooted in non-decayed neighbours; it reads as decay (a cut
    tree). The reverse case is exact: a regenerated origin re-emits the same
    continuation, and the idempotent merge leaves the neighbour unchanged.
  - **Pop-in is accepted at the generation frontier:** a tall tree may complete
    upward over a few generation steps (about one tick each, with the hint),
    visible only at a distance. Not worth generating ahead by the tallest
    tree's extent; pop-in will in any case be amortized across the many phases
    of progressive LOD generation (`world/progressive-lod`).
- Alternatives considered:
  - **Overlap** (each chunk regenerates every feature whose origin is within
    reach and keeps only its own cells; used for huge hard structures as
    seed-derived plans, `worldgen/structure-plans`): order-independent with no
    communication, but repeats each tree's generator in every chunk it
    touches, and bounds reach by how far chunks look for origins. Adopted
    for ordinary worldgen trees (`worldgen/tree-generation`), where the
    repetition is cheap and the reach is bounded per species.
  - **Finalization** (a chunk isn't shown until all neighbours in reach have
    delivered): no late arrivals, but stalls display on neighbours.
    Rejected: the order-independent merge makes waiting unnecessary.
- Open: how continuations interact with progressive LOD (a coarse level's
  representation of a partly-arrived tree); merge rules for feature kinds
  other than log/leaves; the record's exact contents per feature kind.

## worldgen/structure-plans — Rare, huge structures (strongholds) generate from a seed-derived plan; each chunk materializes its own clipped pieces

- Decision: a **huge structure** whose soft spread would be far too extensive
  for continuations (`worldgen/feature-continuations`) — the example is a
  stronghold — uses the Minecraft-style split between **plan** and **blocks**:
  - **Placement and plan are pure functions of the seed,** integer-domain
    throughout (`core/worldgen-integer-domain`). The plan is the full layout:
    the list of pieces (rooms, corridors) with their bounding boxes.
  - **The plan lives in a derived, rebuildable cache keyed by origin,** not in
    the origin chunk. Reading it never touches another chunk's interior
    (`core/opaque-chunk-queries`); if the cache is lost, the plan is
    recomputed from the seed and comes out identical.
  - **Each chunk materializes only the pieces intersecting itself,** found by
    querying the plans whose bounds reach it. No waiting, no continuations, no
    communication between chunks.
- **Why it differs from trees:** these structures typically carry valuable
  loot, so they are **hard content** (`core/soft-over-hard`) and must be
  exactly repeatable. Plans give that by construction; continuations are
  reserved for soft, organic features where divergence is acceptable.
- **How Minecraft does it** (from memory of Java Edition, not checked against
  code): a structure's layout is computed once when its origin chunk reaches
  the structure-starts stage and saved with that chunk; chunks in range store
  only references to origins; each chunk places its own clipped pieces during
  the features stage. Stronghold positions come from a separate per-world
  step (deterministic rings), cached. So the layout is not regenerated per
  chunk; only the cheap intersection test repeats.
- **Relation to the noise-first stance** (`dimensions/enigma`
  and `worldgen/dungeon-connectivity`):
  that stance rejects *structure-spreading* (growing corridors across chunks,
  which needs chunk B to agree with A without seeing A) as a **core**
  generation step. Seed-derived plans don't have that problem: every chunk
  evaluates the same plan independently, so chunk-independence holds. Scope
  stays narrow: plans are for **rare landmark structures**; the bulk of
  generation, including Enigma's dungeon delve, stays noise-first with
  local corrections.
- **A plan is usually a sparse overlay, not the whole structure**. With
  noise-first structures, noise generates the bulk (rooms,
  halls, cells); the plan adds only what noise can't guarantee: **connecting
  corridors** (a guaranteed route to the structure's key room) and
  **corrections** to the noise layout, plus hard placements such as loot.
  Plans stay small, and the plan/noise split mirrors "rules correct the
  noise" at a larger scale than the per-face door hash.
  - **Constraint:** a plan is hard content, so it must not depend on
    float-noise output. Either it is defined independently of the noise (its
    corridors carve through whatever the noise made), or the noise it reads
    is integer-domain (e.g. hash-based cellular cells, which the quantized
    rectilinear noise can be). **Aim for the second**:
    plans that read integer-domain cells fit the rooms naturally instead of
    carving blindly through them.
- Open: the search radius for plans reaching a chunk (bounds the largest
  structure); a spatial index for the cache; whether placement may read
  noise fields (allowed only through the integer-domain gate, as hard
  content); how a plan interacts with terrain (carve vs adapt) and with
  progressive LOD; decay to baseline restores plan-generated blocks except
  valuable ones (`sim/valuable-blocks`).
