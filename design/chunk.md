# world/chunk (draft) — The chunk: its interface

A chunk is a 64³ region of one dimension (`world/cubic-chunks`). While it
is in a Vicinity it is an idle actor: it changes only in its own turns, and
everyone else reads it through semantic queries or signals it. Outside
every Vicinity it is not an object at all, only World state (Tenure,
below). This file specifies **only the chunk's interface**: what callers
can ask of it, under which contract, and what it publishes. How any of this is implemented (cell representations, version
counters, hidden simulation state and ticking structures, meshing kernels, where each piece
of state is stored) is deliberately left out, to be settled once the
interface is agreed.

## Contracts
Every operation belongs to exactly one contract (the actor convention,
`sim/concurrency-model`):
- **Any thread:** safe from any thread at any time, never waits behind work.
- **World only:** called by the World (through its Surveyor) in its own turn.
- **Own turn:** runs only inside the chunk's turn, reached through the
  turn's passkey; this is the only place a chunk's content changes.

## Tenure: where a chunk actor exists
- **Only inside a Vicinity,** running (simulated) or frozen (readable,
  never written, and likely to run soon), the same states a Vicinity has.
  Both carry all the machinery, so starting to run changes a state, not an
  object. The actor is owned by exactly one Vicinity and holds a non-null
  pointer to it, which changes only while that Vicinity is halted
  (`invariant/vicinity/one-vicinity-per-chunk-actor`). A running chunk
  never touches a Vicinity's border: all its neighbours are members of its
  Vicinity (`invariant/vicinity/running-chunk-off-border`).
- **Outside every Vicinity a chunk is World state, not an object:** a
  terrain node (`world/surveyor`). Loaded,
  visible and distant chunks are World-owned state, not chunk actors:
  terrain shape as TerrainNodes in the Surveyor's octree (completed
  generator actors holding const grids, `world/octave-pyramid`), plus
  per-region tables (bitmaps for stage and flags, say, with mesh handles),
  so thousands of visible chunks cost tree nodes and table entries, not
  chunk actors. Their generation, loading, refinement and meshing are goal
  actors in the World's `GoalSource` (`engine/goal-source`), not chunk
  turns, and any number run at once.
- **What the Cartographer creates and the World loads is chunk data, not a
  chunk:** a value at one level of detail (format to be settled): at
  coarse levels terrain shape, const node grids shared by their readers
  (`world/octave-pyramid`); at full detail cells, owned and move-only,
  which a chunk actor takes. Outside every Vicinity the World keeps the chunk data of the
  level it reached, and refinement starts from it when a viewpoint comes
  closer (`world/progressive-lod`). A chunk actor is built from chunk data
  at full detail. The World need not keep chunk data one per chunk: it may
  hold coarse levels as larger rectangular volumes spanning many chunks,
  which is also what a server replicates for distant terrain.
- **The key and the World's record are what is stable:** dimension, coord,
  `contentVersion` and stage persist across tenures. The World creates the
  actor when the chunk joins a Vicinity (from loaded or
  generated chunk data, refined to full detail first if needed), and
  dissolves it once the frozen Vicinity it was cleaved off into has saved and
  shut down.
- **Nothing outside its Vicinity holds a chunk:** the World's record points
  at the actor while one exists (an index, not ownership), valid to the
  reader's next quiescent point; grouped reads go through the Vicinity.
- **Signals to a chunk with no actor are dropped:** the full check when it
  next gets one catches whatever they meant (`core/level-triggered`).

## Rules that keep the two paths one
A chunk is either an actor in a Vicinity or chunk data held by the World.
A design choice and three invariants keep the two from drifting apart, so what is left open
about distant chunks is format, not concurrency:
- **`invariant/chunk/one-chunk-data-type`** — A chunk actor's content is
  exactly one chunk-data value (a Brick, scarce references and the
  last-materialized time), moved in on join and out on dissolve; the
  encoder and digest take that value, and the mesher takes its Brick, never
  an actor-specific form. Chunk data is a plain aggregate with no interface
  of its own, so it adds no layer between the actor and its Brick.
  - **Enforced:** type: the actor holds a `ChunkData` member, and the
    encoder, digest and mesher take only `const ChunkData&` or
    `const Brick&`; test: the randomized equivalence test (v0.2.2) over an
    actor's data and the World's copy.
- **The World's chunk data is never authoritative** (a design choice,
  not a checkable invariant): the truth is the manifest, its chunk blobs and the
  seed (`engine/world`). Any entry in the World's tables can be dropped
  and rebuilt, so a bug there costs pop-in, not corruption.
- **`invariant/chunk/form-changes-synchronised`** — Chunk data changes form
  only at synchronised moments: entry (a join, while the chunk is idle),
  exit (a cleaved Vicinity's save), and a goal actor's result installed in
  a turn of the actor it was sent to. No other path creates,
  replaces or discards it.
  - **Enforced:** debug audit: the Vicinity's turn asserts (a join asserts
    the chunk is idle, a cleave asserts the Vicinity is halted, an install
    asserts the recipient is the current actor).
- **`invariant/chunk/no-content-change-outside-vicinity`** — On the
  authority, nothing outside a Vicinity changes content: only a chunk
  actor's turn writes content, and outside every Vicinity there is none.
  Refinement adds detail derived from the seed; closed forms keep a fixed
  anchor, evaluated on read or on entry; decay of absent chunks
  (`persist/presence-not-absence`) is applied on entry; none of these is a
  write. A client's distant copies change only when its World installs
  chunk data replicated from the server.
  - **Enforced:** type: content is `ActorOwned<…, ChunkTurn>`, so only a
    chunk actor's turn can write it, and World-held chunk data is
    `const`.

## Full-detail terrain nodes
A terrain node (`world/surveyor`) at full detail is one chunk's data, and it
is what a chunk actor is built from when the chunk joins a Vicinity.
- **A dissolving actor populates one.** When the actor dissolves, its
  full-detail chunk data moves into a terrain node without a copy, with the
  digests it was maintaining, keyed by the chunk-blob version its Vicinity's
  save
  committed.
- **Pending, then clean.** Until that save commits, the node is pinned:
  the save's chunk blobs already hold its content, but the manifest still names
  the previous version, so dropping the node and joining again would load
  stale content. Once it commits, the node is identical to the durable
  chunk blob, so the Surveyor may drop it or downsample it to a coarser level at
  any time, evicting by distance first. An unedited chunk's terrain node is clean
  from the start, since the seed regenerates it.
- **Joining again uses it as is** when its version matches the manifest:
  no load and no digest recomputation. A mismatch means the node is stale,
  and it is discarded.

## Identity (any thread)
- `dimension()`, `coord()`: which chunk this is. Fixed for its whole life.

## Reads: semantic queries (any thread)
All reads are semantic (`core/opaque-chunk-queries`): a caller never sees
cells or how they are stored. Each answer is consistent with one state of
the chunk; a read that spans several chunks can be grouped, through
their Vicinity, so the whole answer is consistent across them.
- `raycast(ray, maxDistance) → Hit?`: the first solid cell along a ray within
  this chunk, with face, distance and that cell's block state (what you are
  aimed at comes with the hit that found it).
- `sweep(box, motion) → Contact?`: where a moving box first touches the
  chunk's solid geometry (`sim/swept-collision`).
- `probeVolume(shape, question) → Answer`: a bounded region of some shape
  (box, sphere, cylinder) with a question the chunk answers itself: any or
  all cells matching a predicate, a count or material histogram, or the
  matching cells with their states. The iteration, if any, stays behind the
  representation (`core/opaque-chunk-queries`). Uses: an explosion's
  breakable cells in a sphere, a clear-space check for placement; a shape
  spanning chunks is asked of each through a grouped read.
- `planEdit(action, region) → EditSet`: the part of a tool action that
  falls in this chunk: the cell changes it would make, each with the state
  it expects the cell to hold now. Pure, so the same call gives the preview
  and the commit (the v0.1.7 pattern); an action spanning chunks asks each
  for its part through a grouped read. There is no single-cell read on the
  interface (`core/opaque-chunk-queries`); one exists only for tests.
- `faceConnectivity() → Connectivity`: which faces connect through open
  space, for visibility and refinement (`world/progressive-lod`).
- `emitters() → span<Emitter>`: light-emitting cells, for rendering.
- `markers(face) → span<Marker>`: the soft markers it publishes on a face
  (growing tips, cut markers), read by neighbours at their checks
  (`sim/block-metadata`).
- `digest(subsystem) → Digest` and `contentVersion() → Version`: a
  representation-independent digest of one subsystem's full-detail content
  (`data/content-digest`), computed lazily on first request and maintained
  per edit after that, and a version that changes whenever content does.
  Digests belong to chunk actors, which take part in simulation; coarse
  chunk data outside a Vicinity carries none, or at most a digest of that
  level's own data, never one over every cell it stands for.
- `stage() → Stage`: the generation or refinement stage it has reached.
- `encoded(version) → Payload?`: its encoded content or delta at a version,
  for replication and saving, encoded once and shared by every reader
  (`engine/network-thread`).

## Signals (any thread)
Signals carry no content and change no content; losing or repeating one
costs at most latency.
- `nudge(bits)`: "look again", with neighbour-direction bits, a general bit
  and the mail bit (set by its Vicinity when a hard message is queued for
  it). A nudge to a frozen chunk is remembered but runs nothing until
  it starts to run; a nudge to a chunk with no actor is dropped (Tenure).
- `publishGhost(owner, bounds, state, ttl)`: a mover's soft, read-only
  projection into this chunk, refreshed by its owner and expiring by
  itself; the chunk reacts to physics proxies only at its own checks.
- `markVisibleUntil(frame)` (the frame thread) and render priority (the
  World): hints for when to mesh; never content.

## Lifecycle and membership (World only)
- **Created and dissolved by the World:** the World creates a chunk's actor
  when the chunk joins a Vicinity, and dissolves it once the
  frozen Vicinity it was cleaved off into has saved it; only the World
  decides either (Tenure).
- `join(vicinity)`: membership, set when the actor is built; a chunk
  leaves a running Vicinity only by being cleaved off into a frozen one,
  which saves it before it is dissolved.
- `run()` and `freeze()`: its state within its Vicinity, changed only while
  the chunk is idle (or its Vicinity is halted). It runs when the
  Surveyor's simulation budget wants it running, and only once every
  neighbour is a member of its Vicinity (`sim/vicinity`): necessary, not
  sufficient. Starting to run triggers a full check (below).

## Turns (own turn)
The Dispenser runs a chunk's turn through its Vicinity, the source for its
members' turns, with an **intent**; the turn does exactly that, and
reports facts, never requests.
- **Install chunk data:** installs chunk data a goal actor generated or
  refined (`worldgen/cartographer`), received as a message; reports the stage reached. No chunk
  turn calls the Cartographer.
- **Load:** installs loaded chunk data (plentiful content) and the scarce
  instances the scarce table anchors here; reports the stage (full).
- **Consume mail:** takes the hard messages its Vicinity holds for it, one
  at a time, in per-sender order; consuming takes ownership. Each is a
  conserved object (settled here, or sent back out through the Vicinity
  handle) or a command, such as an **edit**: a set of cell changes, each
  with the state the sender expects the cell to hold, applied where it
  still holds and replied to as applied or rejected per change. An edit
  may carry an **effective time** (a break's impact): its effect becomes
  visible from that time.
- **Check:** its soft simulation at its own rate: closed forms brought up to
  date, markers and physics proxies reacted to, nudges' bits narrowing what is
  rechecked.
- **Full check:** on load and on starting to run, it rechecks everything and nudges
  each loaded neighbour back across the shared face, so nothing missed
  while it was away stays missed (`core/level-triggered`).
- **Mesh:** when its content changed and it is render-resident, it produces a
  mesh for the frame thread (`sim/concurrency-model`, "Meshes and the GPU"), possibly ahead of an edit's
  effective time.
- **Hand over for a save:** in a running Vicinity's save, it hands over a
  frozen copy of its plentiful content and the list of scarce instances it
  holds, and keeps working on the live one; a frozen Vicinity's save reads
  it directly.
- **Through the per-turn Vicinity handle** (running chunks only), a turn may
  send hard messages: an object leaving the chunk, a command to a
  neighbour, an item given to a body. Ring chunks' turns get no handle.

## What it publishes
- Its read answers (above), consistent at any moment.
- Its markers and its digest, versions and stage.
- Mesh descriptors, to the frame thread.
- Encoded payloads, to savers and replication.
- Facts for its source: stages reached, completions.
- Replies to edits, through its Vicinity.

## What the interface guarantees
- **Only its own turn changes its content,** and at most one of its turns
  runs at a time.
- **Every read is consistent:** no caller ever sees a half-applied change.
- **No read waits behind its turn** beyond a short, bounded retry.
- **Signals never change content and never block.**
- **The key and the World's record are stable** for the chunk's whole
  life, and the actor for its whole tenure in a Vicinity; how its content
  is stored may change without any caller noticing.
- **Outside every Vicinity its content doesn't change** on the authority
  (`invariant/chunk/no-content-change-outside-vicinity`).

## Deliberately not here
Implementation, to settle after the interface: how cells are stored and
when that changes; how consistent reads are achieved; where identity,
scheduling state and hints live (the World's record or the actor); how
hidden simulation state, ticking and settled entities are organized; the meshing and
encoding kernels.

## Open
- The shape of the Vicinity's grouped multi-chunk read (a read
  transaction, or a query over a set of chunks).
- Whether `encoded(version)` is the chunk's or a separate cache's.
- The World's state for chunks with no actor beyond TerrainNodes: bitmaps
  per region for stage and flags, mesh handles, and how coarse chunk data
  is grouped into
  volumes (world-aligned power-of-two boxes no larger than the key spacing
  of the coarsest release tier they hold, `worldgen/seed-keys`; and whether
  the server and
  client group alike).
- The format of chunk data at each level of detail (for terrain shape, a
  coarse level's content is its composed fields, `world/octave-pyramid`), and
  the eviction of full-detail terrain nodes beyond distance-first.
- Entry latency: a chunk joining a Vicinity must be decoded or generated
  before it is useful; whether the World builds actors ahead of a fast
  mover, and the hysteresis that stops a chunk near a Vicinity's edge from
  churning.
