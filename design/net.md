# Networking

The client/server boundary, replication, and the scale premise. Netcode below
the consensus/arbitration sim model (`coords/f64-authoritative`), which this file
assumes rather than restates.

## net/scale-premise — Global scale is DISTRIBUTION infra; live concurrency is tens–hundreds

- **Two different "scales" that the design must not conflate:**
  - **Global scale = content DISTRIBUTION**, outside the game sim: the
    content-addressed authored-content / bundle registry and its CDN
    (`data/authored-content`, `data/content-bundles`). Hash-identified objects are
    published and fetched at internet scale, like a package registry or
    git/IPFS-style store. This is an *infra* problem (storage, caching, transfer),
    not a simulation problem.
  - **Live simulation scale = tens to low hundreds** of concurrent players in a
    single running world. NOT planet-scale concurrency.
- **Why this matters (keeps future work from over-engineering):** the whole sim
  architecture is tractable *because* concurrency is bounded to hundreds —
  consensus/arbitration (`coords/f64-authoritative`) is affordable at that size;
  interest management is "which of N≤~hundreds players sees which chunks," not a
  sharding/region-server problem; and the present-player invariant
  (`mobs/spawning`, `core/chunk-locality`) caps simulation to loaded,
  player-adjacent space, so sim cost scales with **players × view**, not world
  size: cost scales with the simulated set (`sim/residency-levels`), and
  closed-form processes cost nothing anywhere (`core/closed-form-first`). A planet-scale-concurrency reading would demand
  a fundamentally harder
  architecture the design does not need. Matches the `Minecraft-flavored
  single-player + small multiplayer` genre target in `progression.md`.

## net/client-server-seam — Client-server is ALWAYS the architecture (a seam, not a deployment); one authoritative sim + a provisional client prediction over resident state

- Decision: **every** game — single-player included — runs through the
  client-server boundary. It is a **seam** (a stable message interface), not a
  **deployment** (it does not imply separate machines or processes):
  - **One authoritative simulation, and the client is a SEPARATE COMPONENT — not
    part of the server, even in single-player.** The authoritative sim is the
    **server component**; the **client component** is a distinct peer across the
    seam. In single-player the two are *co-located* (same machine, same process)
    and talk over an **in-memory transport**; opening to others promotes the server
    to a **listen server** also accepting sockets. They are co-located, never
    *fused*. **For reads of chunk state** (`net/source-blind-client`), the seam is the client-facing read API. In
    single-player it is backed by the authoritative chunks themselves (read
    through the same thread-safe, seqlock-validated API), in multiplayer by
    replicas written by replication, so there is no duplicated world in
    single-player. The client still never *writes* authoritative state and
    sees only what a multiplayer client could; intentions and edits still go
    as messages. This keeps SP and MP one code path by making the
    client unable to tell the difference rather than by copying the world. There is never a second
    authoritative world sim ("two world sims in tandem on one machine" is answered
    by: there is one, always).
  - **A stable message interface:** **intentions up, derived-view deltas down.**
    This is not new — it is two seams the design already has: the client sends
    *intentions* not raw input (`sim/player-embodiment`), and receives a *derived
    view* not authoritative state (`core/authority-derived-split`). The server is
    the authoritative sim both already describe.
  - **Transport swaps under the same interface:** an in-memory channel
    (single-player / the host's own client) or a socket (remote clients). SP and MP
    are ONE code path; multiplayer is never "bolted on later," and the local game
    is continuously exercising the netcode path, which is also its test.

- **The client is NOT a pure view — it runs a BOUNDED PROVISIONAL sim over
  RESIDENT state.** (Corrects an over-thin "client is only a derived view"
  framing.) Observed in Minecraft: during a network stall you can still move, and
  entity physics keep running, but you cannot load new chunks or open a
  container. That behavior is the defining line:
  - **Client simulates locally + provisionally, from state it ALREADY HOLDS:**
    own-body movement, collision, gravity, entity physics/interpolation for
    entities in already-loaded chunks, visual/physical continuity. This is
    *prediction* — run the sim rules forward on resident state so motion is smooth
    and input is responsive — so a network hiccup looks like "the world keeps
    physically behaving, nothing new or authoritative happens," not a freeze.
  - **Client DEFERS to the server for authoritative mutation or NEW data:** loading
    new chunks (server must send them), container contents (authoritative state the
    client was never handed — the authority/derived split *predicts* you cannot
    open one offline, it is not a special case), block place/break confirmation
    (client may show it *provisionally*; the server commits or rolls back —
    `coords/f64-authoritative` arbitration at the client boundary), and anything
    another player could contend.
  - **The defining TEST of which side an interaction lives on:** if it keeps
    working during a network interruption, it was client-predictable from resident
    state; if it blocks, it needed authority or new data. A clean, falsifiable rule
    for placing any future interaction.
  - **Closed-form state replicates as anchors** (`core/closed-form-first`):
    for growth, decay, burn-down and similar processes the server sends the
    anchor once and the client evaluates it locally for free, instead of
    replicating each change.

- **This is the predict/reconcile pattern again.** Mob planning on a stale
  snapshot (`sim/mob-navigation`), an arrow's outcome decided at the sender
  and re-swept by the receiver (`sim/concurrency-model`), and now client
  prediction over resident state are all *predict locally against a snapshot /
  authoritative source reconciles*. It is idiomatic to the engine, not a new
  paradigm — the client's provisional sim is bounded to resident-neighborhood
  physics, never a full world tick, and the server's deltas continuously correct
  it so it cannot diverge permanently.

- **The f64/f32 boundary is the sim/render axis — ORTHOGONAL to client/server.**
  Tempting to equate "server = f64, client = f32," but they are different axes and
  conflating them breaks `core/f32-origin-relative`:
  - **The server component is pure f64 and NON-RENDERING, in every configuration**
    (dedicated, listen, or single-player's co-located server). It never produces
    f32.
  - **The client component does the f32 work — always, wherever it runs**,
    including single-player (where the client is just co-located with the server).
    f32 is a property of *rendering*, which is the client's job, not of "being
    remote."
  - **A predicting client uses f32 in a local frame, not f64**
    (`sim/concurrency-model`, the jump path). It needn't hold f64 to match
    the server's numeric rules: near the player, f32 relative to a local origin
    has ample precision, client and server need not be bit-identical, and
    tolerance-based reconciliation with replay absorbs the drift. So there may
    be no f64 on the client at all.
  - **f32 is the per-camera RENDER REBASE** (`core/f32-origin-relative`: compute
    `world − origin` in f64, downcast the *result*), and the origin is **per client
    and per camera** — which is a second reason the server cannot do it: there is no
    single render origin, there is one per viewer.
  - **The wire carries authoritative/quantized values, NEVER f32.** Sending
    absolute f32 world positions would reintroduce the ~1-unit-ULP edge error
    `core/f32-origin-relative` exists to prevent; each client rebases to its own
    f32 locally. Positions travel as an integer chunk coordinate plus a
    quantized local offset; the exact quantization is open
    (`net/transport-and-replication`).
  - Net: **f64 = the server component (always); f32 = the client, for its
    per-camera render rebase and its local-frame prediction; the wire =
    integer chunk coordinates plus quantized local offsets.** This
    is just `core/authority-derived-split` with "derived side = *rendering*," not
    "derived side = *the client machine*."

- **Interest management is already half-designed.** "What the server tells each
  client" IS the present-player invariant + `core/chunk-locality`: the server
  streams the resident set (loaded, player-adjacent chunks/entities) and the client
  sims within it, stalling new acquisition when the stream stops. Naming it as
  netcode does not add a new mechanism; it reuses the locality the sim already
  enforces.

- Open (see `open-questions`, `net/transport-and-replication`): client-server vs.
  relay topology and host migration; what is replicated and at what rate (full
  state vs. deltas, per-entity cadence, delta compression); client-side
  prediction/interpolation detail for *remote* entities and the rollback/
  reconciliation window; join/sync of a live world; and the concrete
  resident-vs-authoritative **interaction catalog** (which interactions are
  client-predictable vs. server-deferred, applying the network-interruption test).

- **Predicted edits are sequenced and acknowledged**. Every
  edit the client predicts (place, break, use) carries a sequence number; the
  server acknowledges or rejects each one, and the client rolls back any
  prediction the server rejected or never acknowledged. A rejected edit can't
  leave a "ghost block" — the bug class Minecraft carried for years and fixed
  in 1.19 with exactly this mechanism. Details of the reconciliation follow
  established practice (client-side prediction with server reconciliation)
  and are part of the open replication design. Lean
  (`core/level-triggered`): phrase acknowledgement as a cumulative watermark
  ("all edits ≤ n are settled", idempotent level state) rather than
  per-edit acks, so lost or duplicated acks still converge.

- **Desync detection is sampled digest comparison**: the
  server sends content digests and step counters for a randomly chosen,
  relevance-weighted subset of the client's resident and predicted state; a
  mismatch triggers a linear-hash drill-down and a repair of only the differing
  subregion. Mechanism and rationale in `data/content-digest`.

## net/replication (proposal) — Chunks replicate as deltas between states at ticks, anchors not results, digests for drift

- **Prior art** (from memory, approximate):
  - **Minecraft Java:** a whole chunk compressed on first send; edits as
    single-block or multi-block (per 16³ section) change packets; no drift
    detection: it relies on TCP's ordered stream, and fixes ghost blocks by the
    server re-sending affected blocks after a failed action. Clients are
    never sent the seed in multiplayer (only a truncated hash, for cosmetic
    biome blending), yet seeds are routinely cracked from observed terrain.
  - **Minecraft Bedrock:** a blob cache: sub-chunks are content-addressed by
    hash, the client says which it has, the server sends the rest (for
    reload and re-entry bandwidth).
  - **Minetest (Luanti):** whole 16³ map blocks, compressed; single-node
    messages for small edits.
  - **Factorio:** lockstep (inputs only) with periodic state checksums for
    desync detection (lockstep is out of scope, `net/lockstep`).
  - **Quake 3:** each snapshot is delta-encoded against the last state the
    client acknowledged, so a lost packet costs only a larger next delta.
  - No shipping voxel game known to use Merkle trees for chunks.
- **Proposed:**
  - **Distant terrain is replicated per box, not per chunk**
    (`world/octave-pyramid`): the server sends keys, so the client generates
    the box's composed fields itself, or the composed fields themselves
    (edited areas, or clients without the keys). Deltas and digests below concern
    chunks held at full detail.
  - **Don't send what the client can generate:** unedited content may come
    from client generation, within the keys the server releases
    (`worldgen/seed-keys`); only the divergence from the baseline is sent
    (edits, soft-simulation anchors). Whether to do this at all is open.
  - **Send anchors, not results:** closed-form soft simulation is evaluated by
    the client from anchors on world time; stepped, conserved processes (future
    fluids) are the expensive case.
  - **Deltas between states at ticks:** chunk state at tick t
    (`sim/concurrency-model`) gives every delta an identity, "chunk C, from t₁
    to t₂", encoded against the client's last acknowledged state (Quake 3).
  - **Drift detection with digests** (`data/content-digest`, already planned):
    sampled, relevance-weighted; the digest is linear, so sub-block digests
    sum to the chunk's, which gives Merkle-like drill-down without a tree: on a
    mismatch, compare sub-blocks and resend only those that differ. Soft
    content can differ across machines (the SIMD exception), and corrections
    cover it.
  - **The bandwidth test:** a mechanism whose changes can't be expressed as a
    small delta between two states at ticks, or as an anchor, is too
    expensive for the wire.
- **Movers replicate as segments** (`sim/motion-segments`):
  short timestamped segments a client evaluates between updates. A passive
  mover's segment holds until it hits something, so its updates follow
  events rather than ticks; a body with intentions sends segments at its
  step rate, kept close by committed motion and limited control.
- **Carried by the network thread** (`engine/network-thread`):
  one asynchronous event loop serves every remote client, with plain Client
  records; it walks each client's interest and sends versions newer than
  the client acknowledged, from encode-once caches, plus short per-region
  buffers of transient events.
