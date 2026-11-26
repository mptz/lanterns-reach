# Persistence

How world state is stored, decays, and is reclaimed over a single save's
lifetime. Divergence from Minecraft's "generate once, persist forever" model.

## persist/diegetic-persistence — Persistence is tiered by player investment; unstabilized world decays toward its generation baseline
- **Open:** the decay curve, thresholds and reality-bomb caveats
  (`design/open-questions.md`).

- Problem with the Minecraft model: "generate once, persist forever, mutate only
  when loaded" means briefly-loaded flyover chunks are saved forever, bloating
  worlds to multi-GB on travel alone; and worldgen/biome changes never reach
  already-generated chunks.
- Core idea: a chunk only needs storage to the extent it **differs from what
  generation would reproduce**, and *how* it stores that is the chunk's own
  choice (see *Persistence strategy is delegated to the chunk*). Persistence is
  tiered by **player investment**, not by "was ever generated." Save size scales
  with investment, not distance traveled.

- The tiers:
  1. **Transient** — briefly loaded (flyover), unmodified or minimally modified:
     may never be written to persistent storage at all; regenerated from seed on
     demand. ("Meaningfully modified" threshold is open.)
  2. **Persisted-unstabilized** — meaningfully modified and saved, but not held
     by the player: **decays over world time (`WorldTime`,
     `coords/typed-time`) back toward its stored as-generated state**, and
     once fully reverted is deleted from storage (the chunk is reproducible from the generator again, so storing it is
     redundant; its next load regenerates fresh from the current generator).
  3. **Reality-stabilized** — the player spends resources to "stabilize reality"
     here (e.g. a home base): held against decay for as long as stabilization is
     maintained.

- **Stabilization is a maintained, consumable, progression-gated state — not a
  permanent flag.** Early game: stabilization burns consumables and lapses over
  time. (Lapse is closed-form in the consumable anchor and `WorldTime`, not
  stepped; adding fuel re-anchors it, `core/closed-form-first`.) Late-game
  progression unlocks larger stabilized radius, longer duration, and eventually indefinite persistence. Accessing bigger/longer/permanent
  stabilization is an element of game progression.

- **Stabilization is a spatial FIELD, not a per-chunk boolean.** Sources emit a
  stabilization strength that spreads/falls off over distance (radius / flood-fill
  / field with falloff — the field mechanic that lighting used to carry; see
  `light/visibility-only`). A chunk's decay resistance is a function of the field
  value there, so decay granularity is field-driven, not per-chunk flat.
  A change in the field value is an input change to decay: it triggers
  evaluate-then-re-anchor (`core/closed-form-first`), so decay is
  piecewise-exact across field changes rather than a function of elapsed time
  alone.
  - **The field is gameplay-AUTHORITATIVE** (it gates decay and feeds mob
    spawning, `mobs/spawning`). Therefore, unlike lighting, it MUST be
    deterministic authoritative state and must NOT be moved onto the
    fidelity-variable render/GPU layer, even if special equipment lets the player
    *see* it. (See the governing principle in `light/visibility-only`: same field
    shape as light, opposite side of `core/authority-derived-split`.)
  - **The field may propagate SLOWLY — by design.** Because it is a fictional
    effect, players have no instantaneous-propagation expectation (unlike light),
    so stabilization can "percolate outwards" over many ticks of world time.
    This slowness is a feature: the field is a closed-form relaxation (`core/closed-form-first`) toward the target
    its neighbours' published halo values imply, with Δ = elapsed `WorldTime`
    (the relaxation kernel, `sim/process-kernels`), so it is idempotent and
    independent of how often a chunk is loaded — never advanced a step per load
    or per chunk cycle. Stabilization crossing a chunk seam is therefore a slow,
    eventually-consistent propagation — the RELAXED case of
    `core/chunk-locality` / `sim/speed-of-information`, not a tight synchronous
    one.

- **Decay is lazy / on-load, NOT a background sweep.** On loading a chunk, compute
  elapsed `WorldTime` since it was last touched and evaluate decay then. This
  avoids wasted work on chunks no one revisits. It is the main worked example
  of `core/closed-form-first`: the authoritative state is (as-altered,
  as-generated baseline, anchor time) and the blended state is derived, so
  evaluating it at load is a pure read, not a write (`sim/residency-levels`:
  no catch-up on load).
  - NOTE: this deliberately differs from `data/dimension-gc`, which is
    eager/async. Chunk decay is lazy/on-load; dimension reclamation is
    async-sweep. Same conservation discipline, different trigger — do not assume
    shared scheduling machinery.

- **Decay MECHANISM: a pseudorandom-threshold crossfade from the altered state
  toward the as-generated state, not a sampled-per-load roll.** Conceptually a
  chunk has an **as-generated** state (what generation produced) and an
  **as-altered** state (what the player made of it); decay crossfades the altered
  state toward as-generated element by element. Each element carries a **fixed
  pseudorandom threshold** `t_i ∈ [0,1)` derived deterministically from the chunk
  seed and the element's position — a stable per-element value, *not* a coin
  flipped at load time. An element has reverted iff `f(decay%) ≥ t_i`, where
  `decay%` is a function of elapsed `WorldTime` since last touch and `f` is a
  monotonic decay→progress curve (identity, or a nonlinear transform still landing
  in `[0,1]`). At t=0 nothing has reverted; as t→∞ every threshold is crossed and
  you get the pure as-generated state. Because the thresholds form a fixed
  pseudorandom field, reversion still *looks* organic (speckled reclamation in a
  stable spatial order) rather than a block-by-block timer. (These two are the
  decay endpoints the persistence contract must be able to produce — NOT a mandated
  storage layout; see *Persistence strategy is delegated to the chunk* below.)
  - This is `core/soft-thresholds` applied to decay — a graded crossfade over
    a pseudorandom threshold field instead of a hard global revert cutoff, which
    hides the seams of staleness the same way soft falloffs hide the async
    architecture's seams.
  - **Decay is load-frequency-INDEPENDENT by construction — this is a confluence
    property, and the reason for the fixed-threshold formulation over a
    sampled-per-load roll.** The reverted set at time `t` is exactly
    `{ i : f(decay%(t)) ≥ t_i }` — a pure function of elapsed world time and the
    fixed threshold field, with load *history* appearing nowhere. A chunk loaded
    once at t=10 and a chunk loaded at t=3, 7, 10 compute the identical reverted
    set, so repeated load/unload cycles cannot advance (or stall) decay. A
    naive per-load stochastic roll would NOT have this property: resampling each
    load makes the outcome depend on how often the chunk was visited, which is
    exploitable by load-cycling. The fixed field removes that degree of freedom.
  - **The blend is MONOTONIC.** Because `f` is monotonic in `decay%` and the
    thresholds are fixed, the reverted set only ever grows as world time advances
    (until the chunk is touched) — a block never un-reverts across loads, and the
    crossfade moves strictly toward the seed. This is the load-safety property:
    the CA double-buffering discipline in another guise (result depends only on an
    immutable input — the threshold field — never on evaluation order or count).
  - **Touch is the only non-time input, and it evaluates-then-re-anchors.** A
    player modification first evaluates the blend at touch time and makes that
    blended state the new as-altered state (blocks already reverted stay
    reverted), then resets `last_touched`, returning `decay%` to 0 against the
    *same* fixed thresholds (`core/closed-form-first`). Touch must be a
    discrete authoritative event, consistent with the shape-only firewall below —
    the threshold crossing decides only *which shape* has crossfaded, never a
    ledger transition.
  - **The threshold field must obey the integer-domain discipline
    (`core/worldgen-integer-domain`).** `t_i` is derived by the same reproducible
    integer-hash path as worldgen — never a GPU float or a platform-variant RNG —
    so two machines decay the *same* chunk to the *same* reverted set. Otherwise
    divergence re-enters through decay instead of generation.
  - Stays compatible with lazy/on-load: reversion is a *pure function* of
    (altered state, as-generated state, elapsed time, fixed threshold field)
    evaluated at load (a pure read; `core/closed-form-first`). Nothing runs in
    the background; the decay operator is a
    load-time function, however the chunk chose to persist the inputs it needs.
  - **When decay reaches 100%, the chunk's persisted form is deleted entirely.**
    Once the altered state has fully crossfaded to the as-generated state, the
    chunk is reproducible by generation again, so storing it is redundant — it is
    evicted. On the *next* load it is regenerated by the **current** generator.
    This is the single point where a generator change reaches a decayed chunk: not
    by re-rolling a live chunk, but because a deleted chunk is generated fresh next
    time it is needed.
  - **"As-generated" is not a single-step snapshot — generation is progressive.**
    A chunk passes through multiple LOD phases (`world/progressive-lod`), so
    as-generated is a moving target across refinement, and there is no global
    instant where it freezes and "altered" begins. Rather than answer "when does
    as-generated become as-altered?" globally, the engine **delegates it to the
    chunk** (next bullet): the chunk knows which LOD phases it realized and decides
    for itself how to represent its generation baseline vs. its alterations.
  - **Major version bump: a chunk's as-generated baseline is re-derived from the
    current generator.** The as-generated endpoint is a captured baseline, so it
    does not drift when the generator code changes. A deliberate **major-version
    bump** is the explicit event that makes existing world converge: a chunk
    re-derives its as-generated baseline from the current generator, so decay then
    blends its alterations toward the *new* generation. Major version is dev
    *intent*:
    - **Minor fix / tweak:** baselines are left as-is. The small mismatch between a
      chunk's captured baseline and what the current generator would produce is
      deliberately not worth an expensive re-derive pass; it only surfaces when a
      chunk fully decays and is deleted (its *next* load generates fresh). You skip
      the cost precisely because the difference is minor.
    - **Major bump:** you *do* want existing world to converge to the new
      generation, so baselines are re-derived — and from then on decay blends
      toward the new generation. Unstabilized world converges over world time;
      stabilized world is held frozen against the bump until stabilization lapses.
  - **The blend is a SHAPE-ONLY operator; ledger entities never ride the
    randomness.** A probabilistic crossfade of *shape* is free (shape is not
    ledgered — see the conservation rule below). Ledger entities
    (`data/entity-ledger`) transition only through the explicit sink/mint rules: a
    decayed chest sinks its contents to the equity account — it is never a coin
    flip that sometimes keeps and sometimes vanishes an entity. Shape blends
    stochastically; entities are accounted deterministically.

- **Persistence strategy is delegated to the chunk; persistence MECHANISM is a
  central resource.** The chunk owns the *policy* — what to persist and in what
  form — subject only to a load/unload/decay **contract**; the engine owns the
  *implementation* — a centrally-available key-value / blob store the chunk
  serializes into. (Policy delegated, mechanism centralized — the same split as an
  allocator vs. what a struct chooses to put in its allocation.) This is
  `world/chunk-representation` / `core/opaque-chunk-queries` extended from
  in-memory representation to at-rest persistence: just as a chunk privately picks
  its live rep (uniform / palette), it privately picks its persisted form.
  - **The contract — what every persisted form must support, regardless of
    internal layout.** At **unload**, a chunk is handed the moment it has the
    fullest picture of what it became, and it chooses a minimal persistent form.
    Every persisted form carries the chunk's block states and its
    last-materialized time
    (`sim/block-metadata`); hidden simulation state, cross-chunk markers and
    other soft state (`core/soft-state`) are never persisted, and are
    rebuilt or forgotten on load.
    At **load**, given elapsed time and the current generator, that form must be
    able to: (1) reconstruct the chunk's **as-altered** state, (2) produce the
    **decay-blended** state toward its as-generated baseline for the elapsed time,
    and (3) report when it has **fully reverted** (so the chunk can be evicted).
    Any internal form is legal iff it can answer those three — the same "answer the
    query cheaply in your natural complexity" rule `core/opaque-chunk-queries` imposes for voxel queries,
    now imposed for persistence.
  - **"Two concrete states" is one legal strategy, not the model.** A heavily
    rewritten chunk may persist a full concrete snapshot of both endpoints; a
    lightly touched chunk may persist a **diff against its generation parameters**
    (re-admitting divergence-from-seed storage as a *per-chunk choice*, not a
    global mandate); a chunk near full decay may persist almost nothing. The chunk
    picks the cheapest form that honors the contract — the entropy-proportional
    instinct `world/chunk-representation` already runs on, now at the storage
    boundary.
  - **The chunk owns its in-memory rep and its persisted form INDEPENDENTLY.**
    They are related but distinct axes (one is the live rep for sim/meshing, the
    other the at-rest form for storage+decay): a chunk may use a wide-palette in-memory
    Brick yet persist a compact diff. Both decisions are the chunk's, under one
    "chunks decide their own representations" principle; neither is forced to match
    the other.
  - **Storage cost is bounded by player investment, not distance traveled.**
    Transient chunks are never written at all; decayed chunks are deleted; only a
    meaningfully-altered chunk pays, and it pays the minimal form it chose. This is
    the real win over Minecraft's generate-once-persist-forever bloat — far larger
    than any constant factor in a single chunk's chosen form.

- **Dependence on `core/worldgen-integer-domain`.** This model requires a
  chunk to be *exactly* regenerable from seed — to serve transient chunks that are
  never saved, to derive a chunk's as-generated baseline (the decay target,
  however the chunk chooses to store it), to regenerate a deleted (fully-decayed)
  chunk on its next load, and to re-derive baselines on a major-version bump. The
  decay *target* is the chunk's as-generated baseline (which a chunk may store
  concretely, or re-derive from generation parameters — its choice), and every
  such baseline originates from a generation that is reproducible under `core/worldgen-integer-domain`. This is a *different*
  dependence on `core/worldgen-integer-domain` than Minecraft's: Minecraft's reproducibility is mainly
  cross-*world* (same seed → same generation for two players); ours is intra-world
  and temporal (the generator is the source of as-generated baselines and the
  regeneration source for evicted chunks *within a single save*). Not a new
  consumer of `core/worldgen-integer-domain` — a different axis of the same guarantee.

- **Conservation rule for decay/reversion.** Decay reverts the world's *shape*
  toward seed. In-situ world material is NOT a ledger object (see the ledger
  boundary in `data/entity-ledger`), so reverting shape is free and
  conserves nothing by itself. The rules that matter:
  - **Resources already extracted from a chunk are unaffected by its reversion.**
    Extraction already minted a free entity that LEFT the chunk; reverting the
    chunk's shape cannot touch an entity that is no longer in-situ.
  - **Regrowth is free shape, not a mint.** Anything diggable is renewable via
    reversion: regrown ore/terrain is just shape reverting toward seed — no
    ledger event occurs until/unless the player actually re-harvests it (the mint
    is at extraction, per `data/entity-ledger`). Renewable, and throttled
    by the world-time decay timeline (you may wait months to re-harvest).
    **Valuable ores (`geology/ore-sites`):** minted in place at
    resolution, so one still in place when its chunk reverts is sunk through
    the ledger and its site re-resolves later. Renewability is deliberate, on a
    6+-month timescale: it discourages the hoarding compulsion that
    non-renewable resources create.
  - **Ledger-tracked contents are sunk, never vanished.** When a chest (or other
    container of conserved items) decays away, its items — which ARE ledger
    entities — are returned to the "equity account" (sunk via
    `data/entity-ledger`). No silent destruction. For a placed pouch/chest the path
    is two steps: the decayed block's portal row goes, so nothing stabilizes its
    Aether pocket any more, and the pocket then decays in turn, sinking its
    contents (`dimensions/aether-pockets`, `data/dimension-gc`).

- **Reality bombs (open).** An *active* reversion that promptly decays an
  area (rather than waiting out the decay timeline), likely with additional
  caveats. Subject to the same conservation rule above. Details TBD.
  **Direction:** reality bombs are anti-lantern effects of **Enigma's
  magic**: powerful artifacts, found rather than crafted, unreplicable and
  dangerous; optional late- or endgame items on no progression critical
  path (`worldgen/dimension-cosmology`).

- Payoffs recorded for the record:
  - Kills multi-GB flyover bloat: transient chunks cost nothing; unstabilized
    chunks are bounded-lifetime; only stabilized divergence persists.
  - Worldgen/biome updates naturally reach unstabilized areas: a major-version
    bump re-captures their as-generated state (decay then converges the modified
    state toward the new generation), and any fully-decayed chunk regenerates from
    the current generator on next load — while stabilized areas are held frozen
    against the bump until stabilization lapses. This is the inverse of
    Minecraft's "updates can't reach old worlds." Be deliberate that a major bump
    therefore alters unstabilized terrain over time (intended, even thematic);
    minor fixes deliberately do *not* re-capture, so they only reach chunks that
    happen to fully decay and get regenerated.

*(Tenet `persist/presence-not-absence` lives in `TENETS.md`.)*
