# Data & persistence

Identity, immutability, the content-definition grammar, persistent storage, the
entity ledger, and dimension garbage collection.

## data/immutability — ULIDs + immutable data objects (at object granularity)
- **Open:** the exact granularity boundary (`design/open-questions.md`).

- Decision: Heavy use of ULIDs for identity and immutable data objects for
  entities/items/definitions. Mutations produce new versions; identity is
  stable.
- Rationale: Strong fit for determinism, networking, undo/history, and the
  entity ledger's conservation accounting (`data/entity-ledger`).
- Consequences: Apply immutability at object/entity granularity, NOT at voxel
  granularity — per-voxel immutability under stepped sim and edits would be
  prohibitive.
  Voxels use cheap mutable/copy-on-write storage (see
  `world/chunk-representation`). Granularity boundary is an open question.

## data/grammar — Data-driven world via a textual grammar (not JSON), LR-family parser

- Decision: World/content defined in a custom textual grammar parsed by a
  bottom-up (LR/LALR-family) parser — explicitly not JSON. **Scope:
  the grammar is a declarative capability-access language, NOT a programming
  language.** No looping, no general control flow, no Turing-completeness — it
  names and configures preconfigured engine behaviors only.
- Rationale: A real DSL is terser and can encode structure/relationships and
  semantics JSON cannot. **The grammar is a BINDING AGENT, not a computation
  medium — that is its role, and non-Turing-completeness is a statement of that
  role, not primarily a safety mechanism.** Computation is simply not what the
  grammar is for; it binds parameters to engine capabilities. (Safety — no loops
  to sandbox — is a consequence of the role, not its motivation.) This also keeps
  the "data vs. scripting" boundary from eroding: adding control flow would be
  changing what the grammar IS, a deliberate visible act.
- Consequences: We own a grammar + parser toolchain (generator or hand-written
  LALR). Behavior the grammar cannot express must be provided as a named engine
  capability the grammar references. Genuine *computation* authored by players is
  a DIFFERENT kind of thing — an in-world machine (`sim/logic-machines`), not an
  extension of this grammar. See the two-layer authoring taxonomy in
  `core/diegetic-authoring`.
- **Simulation behaviour is declared here too:** block
  types bind the generic process kernels of `sim/process-kernels` (counter,
  hazard, relaxation, decaying trace, extension, timed container) with
  parameters, and their check rates (`sim/random-ticks`); there is no bespoke
  per-block simulation code.

## data/entity-ledger — The ledger is conservation accounting over chunk- and Vicinity-owned entities, not a central table

- **What "the ledger" means:** the **conservation
  accounting** for entities — items and anything that can move between
  containers or into an inventory, plus mobs, pets, vehicles and other
  identified entities. Creation and destruction go through auditable
  **mint/sink** operations (double-entry style), so moves transfer rather than
  create or destroy, and every entity has **exactly one location**. Elsewhere
  in the docs, "ledger entity", "minted into the ledger" and "sunk through the
  ledger" keep this meaning.
- **Where it lives at runtime:** not in one table. **Chunks own the entities
  in them**, in their own entity tables, simulated in their own turns without
  locks. **Movers** (entities that will leave their chunk) are owned by the
  **Vicinity** until they come to rest in a chunk; transfers between a
  chunk and its Vicinity are **conserved messages**, and assemblies
  commit in the Vicinity's own turn. These are the only places
  cross-chunk conserved work happens (`sim/vicinity`). A central table
  would be a serialization point that every chunk's physics contended on.
- **Where it lives on disk:**
  - **every ledger entity persists in the world's scarce table,** wherever
    it is simulated: one row per instance, keyed by a ULID minted with it,
    versioned, written only if newer, by the Vicinity that owns it when it
    saves; chunk content refers to entities by ULID and never holds them,
    so a chunk's saved content is plentiful state only
    (`sim/concurrency-model`). A crash can roll an entity back to where it
    was last saved, never duplicate or lose it;
  - **sinking is a move to equity, not a deletion:** a consumed entity's
    row stays, with its location set to equity and the sink named, so
    mint/sink is double entry in the table itself; offline, the books are
    balanced (live plus equity equals minted, per kind) and old equity rows
    are folded into totals (`sim/concurrency-model`);
  - **identity survives state changes; transformations carry their inputs'
    moves to equity:** an entity keeps its ULID while it stays the same
    instance, and when entities become others (crafting, splitting or
    merging a stack, ore into nuggets) the outputs get new ULIDs and the
    inputs' moves to equity commit with them;
  - **genuinely global records live in a small global store**: the
    portal / interdimensional reference table (`data/dimension-gc`),
    dimension records, per-region state (a defeated boss), world metadata and
    player accounts. Written rarely and never on the hot path; SQLite is the
    natural fit (`deps/save-store`).
- **Part of the hard simulation (`core/soft-over-hard`):** the
  ledger is hard — exact, conserved, dupe-proof, formally modelled
  (`sim/exact-core`). Soft simulation (crop and tree growth) never touches it;
  **harvest mints** the item into the ledger, which is the soft→hard
  boundary. Soft simulation therefore gives no hard duping prevention for what
  it grows, deliberately. Valuable ores are the exception: they mint in place
  when their site is resolved on reveal (`geology/ore-sites`) and are tracked
  as in-world entities from then on.
- **Shape operations never touch entities.** The entity section is separate
  from shape, so decay (`persist/diegetic-persistence`), LoD refinement
  (`world/progressive-lod`) and regeneration rewrite shape only; entity
  cleanup for a reverting region is an explicit sink, not a side effect of a
  shape rewrite. (Entities and terrain are now stored apart altogether:
  entities in the scarce table, terrain in chunk blobs.)
- **Ledger boundary (what is and isn't an entity):** the ledger tracks only
  **free entities that can move between containers** — NOT in-situ world
  material. A block in the ground is world *shape*, not a ledger entity,
  regardless of LOD level or whether it is "harvestable." It enters the ledger
  only at **extraction** (mined → free-floating entity); that transition is the
  **mint**. This keeps the high-volume path (worldgen, LOD, decay — all on world
  shape) decoupled from the ledger.
- **Exactly one location per identified entity:** an entity is in exactly one chunk's entity table, in
  one Vicinity's objects, in one conserved message, or in one inventory
  slot. The transfer protocol guarantees it (the sender enqueues the message
  before dropping its copy; the receiver takes it before adding), so an
  entity saved in two chunks —
  the classic chunk-unload dupe — can't arise inside a Vicinity, and a
  save-time check per Vicinity catches it if it ever does
  (`data/cheap-invariants`).
- **Two kinds of location** (`inventory/slots-and-pack`): a
  ledger entry is either **loose in a dimension** (in a chunk) or **in an
  inventory slot** (`(holder, slot)`, where the holder is a body or a storage
  dimension). A storage dimension (pouch/chest interior) is a slot grid, not
  voxel space, so it needs no terrain chunks. Moves between the two kinds are
  ordinary accounted transfers. A body's inventory travels with the body.
- **Open: a logged-out player's body and inventory** (Michael, 2026-11-05:
  leave open; the right answer may become clear later): the body stays in its
  chunk, asleep (Rust), or moves to the global player store (Minecraft's
  player data). The first is simpler under chunk ownership; the second keeps
  an absent player's things out of the world (`persist/presence-not-absence`).
- **Drop timestamps:** a loose item dropped by a player carries its drop time in
  world time; despawn (days to weeks for death drops, `inventory/slots-and-pack`) is closed-form in
  `WorldTime` against that timestamp (`core/closed-form-first`), independent of
  whether its chunk is loaded (an expired item reads as gone); the sink
  itself is materialized by the owning chunk's check.
- **Placed containers are not ledger entities while placed.** Placing any block
  turns an entity into world shape (a sink) and breaking it is a mint; a placed
  pouch/chest is the same, plus a row in the global **portal / interdimensional
  reference table** (`data/dimension-gc`), which is what is tracked for it
  while placed.
- **Place/break atomicity:** a place or break changes the cell's reference
  in chunk content and the entity's row in the scarce table, committed in
  the same Vicinity save (the manifest switch and the rows in one
  transaction); a placed container's portal row joins that transaction as a
  world record (`persist/edit-atomicity`). If they ever disagree on load,
  the table wins and an irreconcilable entity goes to limbo.
- Note: this boundary happens to coincide with the generation/alteration boundary
  (generation-reproducible shape is outside the ledger; free entities are inside).
  Useful as a sanity check — if gen/LOD/decay is touching the ledger, something
  is miscategorized — but it is an observation, not a load-bearing abstraction;
  don't force two things to share it when they genuinely want to differ.
- **Open: where equity comes from, and when the world may enlarge it.**
  Equity is both sides of the ledger's double entry: a mint draws an
  instance out of it into the world, and a sink returns one. Still to
  decide: the initial balance a world starts with (for instance a
  seed-derived endowment of finite deposits, such as the ore sites
  resolved on reveal, `geology/ore-sites`), which mint rules may enlarge
  it and how fast (harvest of soft growth, the soft→hard boundary above, is
  renewable but rate-limited by the soft simulation), whether any
  creative or operator mint exists, and whether each mint must name the
  rule that allowed it, so the books show not just how much was minted but
  under what authority.

## data/dimension-gc — Pocket-dimension reclamation: async, conservation-safe; ownership graph is a DAG
- **Open:** the collector choice (leaning reference counting;
  `design/open-questions.md`).

- **Pockets are reclaimed by decay, not collection**
  (`dimensions/aether-pockets`). A container interior is a region of the
  Aether that its container stabilizes. When the container is gone (despawn,
  decay, destruction), the pocket is just unstabilized Aether and decays by
  the ordinary mechanics (`persist/diegetic-persistence`). Notes (proposed):
  - **No separate collector for pockets.** Decay is already a closed form on
    `WorldTime` (`core/closed-form-first`), so there is no sweep to run.
  - **Conservation still holds:** decayed contents go through the ledger's
    decay sink (`data/entity-ledger`), as decayed chunk contents do. Nothing
    vanishes silently.
  - **The ownership graph stays,** with a new job: it records which container
    stabilizes which pocket, and it enforces no-nesting. Reference counting
    over it is no longer needed to free memory.
  - **Shared (Ender-Chest-style) pockets** are permanently stabilized Aether
    regions, the analogue of the old GC roots.
- What follows was decided for collected pocket dimensions and still applies
  to any dimension that is not part of the Aether. It may become moot if no
  such dimension remains (`data/dimension-collector`).

- Decided:
  - **Reclamation is asynchronous.** Dimensions (including pocket dimensions)
    are reclaimed by a deferred background sweep, NOT by synchronous teardown on
    the tick that drops the last reference. Tearing down a dimension full of
    chunks/entities inline would be a tick-latency spike.
  - **Reclaiming a dimension is a conservation event** (per
    `data/entity-ledger`), never a silent delete. Contents must not
    vanish: on collection they are either spilled to the parent dimension (the
    container breaks, contents fall out) or routed through an explicit sink.
    Silent destruction of contained items/entities is a conservation violation
    (a vanish bug).
- Decided principle (keeps the cheap collector viable):
  - **Separate ownership edges from reference edges.**
    - *Ownership edges* keep the target alive and **must form a DAG/forest.**
      Enforced by an insertion-time ancestor walk (O(depth)) that rejects any
      move placing a container into a dimension that transitively contains it —
      self-containment is unrepresentable (same spirit as `data/immutability`,
      `coords/typed-positions`).
    - *Reference/traversal edges* (portal-style links) are weak: they do NOT
      keep the target alive and MAY cycle freely (portal A↔B is fine).
  - Rationale: physical nesting is naturally acyclic, so the ownership graph can
    be a DAG by construction; only ownership edges need the invariant, which is
    what lets reference counting remain sufficient.
- Decided:
  - **Who holds ownership edges:** an **item entity** in the ledger (a carried
    pouch or chest, a mechanical-soul item, a portal item), or a **placed-portal
    row** in the portal table (a placed pouch/chest/portal). A mechanical soul's
    brain is owned by an item, never held by a weak reference (`sim/logic-machines`).
  - **Roots:** some dimensions are **persistent and never collected** — the main
    game worlds, and Ender-Chest-style shared inventories. They are GC roots;
    their access points (chests) are reference edges.
  - **Placing / picking up transfers ownership atomically.** Break-and-pick-up
    deletes the portal row and mints the owning item in **one transaction**
    (placing is the reverse), so the collector never sees an owned dimension as
    unowned in between. Decay of a placed container goes the same way: shape
    decays → portal row removed → dimension unreachable → collected.
  - **Contents of a collected container:** a dimension whose owner was destroyed
    (despawn, decay) has its contents sunk to the equity account at collection.
    A container broken by a player is picked up *with* its contents
    (`inventory/slots-and-pack`), so nothing spills.
  - **Depth is now bounded.** No nesting of extradimensional items, and no
    containers or portals inside traversable pocket dimensions
    (`inventory/slots-and-pack`), make the ownership graph one level deep under
    the roots and bodies. Pure reference counting is therefore valid under the
    current rules.
- Left open (the collector choice):
  - Pure reference counting — valid *iff* the ownership-DAG invariant holds and
    is enforced (no ownership cycles ever) — vs. tracing mark-sweep, needed only
    if some future mechanic genuinely requires an ownership cycle.
  - Lean: reference counting, since current rules bound depth to
    one level (above). Mark-sweep only if a later rule re-introduces nesting.
    Async reclamation and the conservation rule hold under either collector.

## data/authored-content — Player-authored content is immutable and content-addressed (hash = identity)
- **Open:** moderation and resource-bound tooling (`design/open-questions.md`).

- Decision: Content a player authors diegetically (`core/diegetic-authoring`) —
  a soul/AI binding, an N³ block, etc. — is an **immutable** object
  (`data/immutability`) that is **content-addressed by hash**. The hash IS the
  content identity; a ULID identifies a particular instance/publication. Authored
  objects are expressed in `data/grammar` (bindings to engine capabilities).
- Mechanism (content-addressed store, git/IPFS-like):
  author → canonicalize → hash → dedupe-or-publish → **reference by hash
  everywhere**. Entities "carry" authored content by hash; the heavy definition
  is fetched/cached once.
- Properties this buys (why ULIDs + immutability were chosen early):
  - **Global dedup** — identical definitions converge to one hashed object; sync
    and storage scale with *distinct content*, not authoring volume (same
    entropy-proportional instinct as persistence and chunk reps).
  - **Integrity / tamper-evidence** — a peer cannot ship a definition that claims
    to be one thing and behaves as another; the hash verifies it, immutability
    keeps it from changing under you.
  - **Cheap sync & revocation** — references are hashes; a hash denylist revokes
    content trivially (used by `data/content-bundles`).
- Minted as `data/entity-ledger` entities via **in-world cost** (you spent a
  creature/reagents/resources) — authoring is NOT free creation; no mint faucet
  outside the ledger.
- **integrity ≠ safety (the critical distinction).** Content-addressing +
  immutability + the non-Turing grammar solve **integrity, dedup, and
  code-execution safety**. They do NOT solve **content moderation** (offensive
  textures/semantics) or **resource abuse** (degenerate-but-valid definitions
  expensive to render/sim). Those require publish-time validation + resource
  bounds, and the trust boundary is the bundle (`data/content-bundles`). A hash
  authenticates a hostile object as faithfully as a benign one.

## data/content-bundles — Authored content aggregates into curated, versioned, content-addressed bundles
- **Open:** composition, compatibility and the curation model
  (`design/open-questions.md`).

- Decision: Published content aggregates into **bundles** — immutable, versioned,
  content-addressed **manifests** (a hashed list of member content hashes +
  metadata: name, rating, curator identity, version, compatibility declarations).
  A bundle hash commits to all member hashes (Merkle-ish); dedup and integrity
  from `data/authored-content` carry over (two bundles sharing a block share the
  hashed block).
- Bundles serve **two jobs at two granularities:**
  1. **Aesthetic/design coherence** — the mishmash problem. A world is assembled
     from curated bundles ("Vanilla", "Child Friendly", "Dark Fantasy") whose
     contents were vetted to cohere thematically and mechanically. The bundle is
     the unit of design intent (content-pack / modpack model).
  2. **Trust / moderation / rating** — the unit of trust is the BUNDLE, not the
     individual object. A server/player **adopts** bundles, transitively trusting
     their vetted contents. This collapses "moderate the global atom soup" into
     "establish trust in bundles" — a tractable social/reputation problem.
     Curation, rating ("Child Friendly"), provenance, and revocation attach here.
- Enforcement model: the engine can't semantically verify "child friendly" — that
  is a **curatorial claim** backed by provenance/reputation. A server enforces
  "admit only bundles rated/sourced acceptably." Revocation works at bundle AND
  object hash granularity (both are hashes on a denylist).
- **Governing stance — mechanism, not enforcement (MIT open-source):** the engine
  provides authoring, bundling, trust labels, and adoption choice; it does NOT
  hard-restrict user behavior or police taste. Safety mechanisms exist so
  communities can protect THEMSELVES, not so the engine imposes global policy.
- **Composition is allowed but is a power-user act.** Mixing content across
  bundles (Dark-Fantasy terrain + Child-Friendly creatures) is possible but
  requires hand-authoring a new manifest — and the consequences are the author's
  (poor dynamics, CPU-hogging items are self-inflicted). Bundles may declare
  dependencies/compatibility/conflicts to guide (not forbid) composition; this is
  the open design question, not a hard restriction.

## data/stable-identity — ULIDs on every definition object; versioned records; migrations are total, tested functions

- Decision (extends `data/immutability`):
  - **ULIDs identify every definition object**: block definitions, textures,
    attributes, materials, and so on, not just entities. The grammar
    (`data/grammar`) supports human-readable **nicknames** that resolve to
    ULIDs at parse time. Persisted data and network messages reference ULIDs
    (and the state hashes derived from them, `world/brick`) only: never
    nicknames, and never a numeric index outside the scope that defines it (a
    chunk palette index is chunk-local).
  - **One identifier format.** Every durable id is 128 bits and prints as 26
    characters of Crockford base32 (most significant bits first, so the first
    character is 0–7; case-insensitive on input, upper case on output, no
    `I`, `L`, `O` or `U`). **ULIDs** are minted once, for a definition at
    authoring time or a scarce instance when it is minted: a 48-bit
    millisecond timestamp, then 80 random bits, ordered within a millisecond
    by incrementing the random part. **State hashes** are not ULIDs: they are
    128-bit content hashes of a definition ULID and canonical property
    values (`world/brick`), derived by anyone who has the inputs and never
    minted, so they carry no timestamp. They share the encoding so that one
    parser, printer and database key type serves both.
  - **Minting takes its clock and randomness from the caller.** The
    simulation never reads the wall clock or an unseeded generator to mint a
    ledger entity's ULID: the Vicinity passes in world time and a stream
    derived from its seed, so a replay mints the same ids
    (`data/entity-ledger`, `sim/exact-core`). Authoring tools use the real
    clock and system randomness.
  - **Every persisted record carries a schema version**: chunk blobs, ledger
    rows, portal rows, player records, save metadata.
  - **Migration N → N+1 is a total, pure, tested function** over one record
    kind; loading applies the chain. **Derived data is never persisted** (it
    is re-derived), so it never needs migrating.
- Relation to `data/authored-content`: a ULID names the thing; a content hash
  names one exact immutable content. They are complementary, not competing.
- Why (the Minecraft lesson): numeric block IDs and ad-hoc state encodings
  forced "The Flattening" (1.13) and a large data-fixer layer that every save
  format change has to extend.
- Open: test strategy (golden corpora of old saves, round-trip checks); whether
  migration runs eagerly at load or lazily per record.

## data/cheap-invariants — Always-on invariant checks are O(1) comparisons over incrementally maintained state

- Decision: invariants are checked **in every build, release included**, when
  maintaining them adds a small constant to each operation and checking them
  is trivial. A violation halts (or flags, outside the sim's critical path)
  with the event that caused it — silent corruption becomes an immediate,
  attributable failure. Examples:
  - **Conservation:** running totals are maintained per mint/sink/transfer, so
    "ledger total + equity = constant" is one comparison per tick.
  - **Single location per entity:** each entity id is in exactly one chunk
    table, Vicinity, conserved message or inventory slot; the transfer
    protocol guarantees it, and
    a check per Vicinity at save time (or continuously in debug builds)
    catches a violation (`data/entity-ledger`).
  - **Canonical state:** a stored orientation index is in its type's canonical
    table.
- **Rejected: audits that recompute what a subsystem already maintains.** A
  dimension-reachability sweep would be a second garbage collector, not a
  check; it is not run routinely (at most an offline debug tool).

## data/content-digest — Every chunk in a Vicinity has a lazily computed, incrementally maintained, representation-independent linear content digest; mismatches are found by sampled comparison
- Canonical digests are part of the formally modelled hard simulation
  (`sim/exact-core`).

- **Scope: chunk actors, at full detail.** Digests belong to chunks in a
  Vicinity, which take part in simulation (`world/chunk`). Coarse chunk
  data outside every Vicinity carries none, or at most a digest of that
  level's own data, never one over every cell a coarse chunk stands for.
- **Lazy, then incremental.** A digest is computed on first request (an
  O(volume) pass, or folded into a full-detail generation pass), then
  maintained per edit as below; one never requested costs nothing.
- Decision: each chunk maintains, **per subsystem state**, a content digest
  defined over its canonical content only:

  ```
  digest = Σ over cells c of  W(c) · H(state_c)   (mod p, p = 2^61 − 1)
  ```

  - `W(c)`: a fixed pseudo-random weight per cell, a function of the cell's
    local coordinates (z up):
    `W(x, y, z) = 1 + (mix64(x | y << 6 | z << 12) mod (p − 1))`, with
    `mix64` a defined 64-bit mix (SplitMix64's finaliser, say). It is
    defined on coordinates, not on the Brick's storage layout, so the layout
    may change without invalidating stored digests; today the packing
    inside W equals the layout index (`world/brick`), so W costs nothing
    extra in the edit path. **W is nonzero for every cell by construction**
    (a zero weight would make a cell invisible to the digest); a test
    checks all 2¹⁸ cells.
  - `H(s)`: a hash of the canonical state (canonical `BlockState`s,
    `data/cheap-invariants`), so equal content always means equal digest.
  - Arithmetic modulo the Mersenne prime 2⁶¹ − 1, not 2⁶⁴: modulo 2⁶⁴ an even
    weight discards low bits of what it multiplies. Two different contents
    collide with probability ≈ 2⁻⁶¹. This is for **accidental** divergence;
    adversarial clients (cheating) are out of scope here.
  - Not XOR-based: identical pairs cancel under XOR.
  - The chunk address is mixed in only when digests from different chunks
    are combined, so `W` is shared by every chunk.
- **Why it's cheap in every representation (`core/opaque-chunk-queries`):**

  | Representation | Cost |
  |---|---|
  | Uniform (state s) | `H(s) · K` with `K = Σ W(c)` over a chunk, one constant for every chunk: O(1), nothing stored |
  | Palette | an edit a → b at c applies `digest += W(c) · (H(b) − H(a))`: O(1) per edit |
  | Generation | one multiply-add per cell folded into the O(volume) generation pass |
  | Representation change (promote/demote) | the digest carries over unchanged, because content didn't change |

  So once computed, `digest(subsystem)` is a public chunk query, O(1) to
  answer and O(1) to maintain (`core/opaque-chunk-queries`), and the stored
  cost is 8 bytes per chunk per digested subsystem.
- **Linearity pays twice:**
  - **Free hierarchy.** A region's digest is the sum of its parts', so locating
    a mismatch needs no stored tree: both sides compute the 8 octants'
    digests on demand, recurse into those that differ, and exchange cells only
    at the bottom. That is O(volume) of the region involved, but only on a
    mismatch.
  - **Delta digests.** "The digest of the changes since step N" is a sum of
    edit terms, so a check can target recent changes, where divergence is
    most likely.
- **The digest covers authoritative state only, in canonical form.** For closed-form processes that means the
  authoritative anchor, which changes only at semantic events (input changes,
  being acted on); values cached by on-read evaluation are derived and are
  NOT digested (`core/closed-form-first`: reads are pure). Otherwise two
  copies with the same content, read at different moments, would digest
  differently and report false desyncs — the Factorio failure class where a
  read mutated state and broke the CRC check. Rule: anything digested must
  be canonical (a function of content and semantic history, never of when or
  how often it was read). Authoritative state is the block states and the
  chunk's last-materialized time (there is no per-block metadata, `sim/block-metadata`);
  hidden simulation state and soft cross-chunk markers (`core/soft-state`)
  are never digested.
- **Comparison carries the step counter** (`sim/subsystem-rate-degradation`):
  a mismatch at different step counts means "skipped or behind", not
  "corrupted".
- **Uses (general bug-catching hygiene, not only netcode):**
  - **Rep-swap invariance** (`world/chunk-meshing`, `world/chunk-representation`):
    the same content in any representation must give the identical digest — a
    single 8-byte comparison, checkable in release (`data/cheap-invariants`).
  - **Concurrency checks:** lockstep runs must reproduce each other's
    digests; N-worker runs must match lockstep exactly for generation,
    edits and conserved state, and converge once the soft layer settles,
    since the soft layer need not be deterministic (`sim/concurrency-model`).
    Digests are compared first, with drill-down only on mismatch.
  - **Persistence round-trips:** save → load must reproduce every digest;
    a stored digest detects on-disk corruption (`persist/edit-atomicity`).
  - **Worldgen reproducibility:** regenerating a chunk from (seed, address)
    must reproduce its stored generation digest (`core/worldgen-integer-domain`);
    also catches the cross-SIMD divergence `deps/noise` warns about.
  - **Client-server desync** (`net/client-server-seam`): see below.
- **Sampled desync detection.** The server periodically sends digests (and
  step counters) for a **randomly chosen** subset of the client's
  simulated chunks (those in its Vicinities) and predicted state,
  weighted toward what the client actually
  predicts and toward chunks near the player. The client compares; on a
  mismatch it requests the drill-down and repairs the differing subregion.
  If a fraction f of state has diverged, k random checks miss it with
  probability (1 − f)^k, so persistent desyncs are caught almost surely and
  transient ones heal on their own. Known prior art: checksum exchange in
  lockstep games (RTS engines, Factorio), priority-accumulator refresh in
  state-sync netcode, Merkle-tree anti-entropy in distributed databases.
- Open: the exact `W`/`H` functions (fast, well-mixed, stable across
  platforms and versions — the digest is a persisted value); which subsystem
  states get digests (block states certainly; fluid levels, entity sets);
  the sampling rate and weighting; whether entities use the same linear form
  over (entity id, state).
