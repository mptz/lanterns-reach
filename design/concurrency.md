# sim/concurrency-model (draft) — The concurrency model: actors, validated reads, hard messages

How threads share the world: who changes what, how others read it, and
what makes each read safe. It applies `core/isolation-first`,
`core/parallel-by-default`, `core/chunk-locality` and
`core/opaque-chunk-queries`, and builds on `sim/cpu-parallelism`.

**Key classes get their own files;** this file holds the approach, the
assumptions and invariants they share, and the connective tissue between
them. The entity docs: the Dispenser (`design/dispenser.md`), GoalSource
(`design/goal-source.md`), the Vicinity (`design/vicinity.md`), the
chunk's interface (`design/chunk.md`), the Brick (`design/brick.md`), the
Surveyor (`design/surveyor.md`), the World actor with its Registrar
(`design/world-actor.md`), the Cartographer (`design/cartographer.md`),
the frame thread (`design/frame-thread.md`), audio (`design/audio.md`)
and the network thread (`design/network-thread.md`).

## The approach
- **Parallelism comes from isolation, not locking.** State is divided
  among **actors**: owners of state that run one **turn** at a time,
  single-threaded, on a shared pool of worker threads. No state belongs to
  a thread. The chunk is the basic actor, and a chunk changes only in its
  own turns, so the hot paths (simulating, meshing and editing a chunk)
  take no locks.
- **All work is pulled, by priority.** Workers take turns from pull
  sources through the Dispenser; there is no thread per subsystem and no
  central ready queue. Work with dependencies is goal actors sequenced by
  GoalSource (`engine/dispenser`, `engine/goal-source`).
- **Actors read each other directly and write each other only by
  message.** Reads are synchronous semantic queries on another actor's
  live storage, zero-copy and validated by a version counter. Writes are
  hard messages, used only for hard simulation; soft effects across
  chunks are published state that others read at their own checks,
  prompted by content-free nudges.
- **Hard and soft simulation get different guarantees.** Soft simulation
  needn't be deterministic: it is level-triggered and self-repairing, and
  its results may depend on thread timing. Hard simulation (conserved
  objects, the ledger) is correct by construction whatever the timing:
  everything that crosses a chunk seam with hard consequences happens
  inside one **Vicinity**, a disjoint, single-threaded transaction domain
  one level above chunks, which is also the unit of saving.
- **Memory safety comes from a few ownership rules, checked in release.**
  Every shared object has one owner, is borrowed for at most one turn,
  and is freed only after a grace period (quiescent-state-based
  reclamation). Most shared objects are immutable once published
  (terrain nodes, completed goal actors). The exception is live chunk
  storage, which its owner writes in place while other threads read it:
  it is pointer-free and never resized in place, so a racing read may be
  stale or catch a write midway, but never reaches freed memory, and
  readers validate it or tolerate it as the owner's read API declares.
  Violations are fatal errors, caught at about a load and compare per
  dereference.
- **A few threads stay outside the actor model** (the frame thread, the
  audio callback, the network thread), and no thread ever waits behind
  another's work: locks are short, never nested, and guard only moves of
  indices and pointers.
- **Verification:** lockstep mode (one worker alternating with the frame
  thread) is the reference; runs with many workers must match it exactly
  for generation, edits and conserved state and converge for soft
  simulation; ThreadSanitizer builds; TLA+ models of the reclamation and
  GoalSource protocols (`model/`).

The rest of this file drills down in that order: actors and turns; how
actors interact; shared memory, ownership and locking; the threads
outside the actor model; determinism and verification; hard simulation
and persistence; then the components built on the model.

## Actors and turns

### Chunks as idle actors
- **A chunk belongs to no thread.** Normally it is idle. When there is work
  for it (generation, a refinement stage, an edit, a simulation step), the
  scheduler hands it to a worker, which does the work and detaches. Everything a chunk does is strictly per chunk.
- **A chunk changes only in its own turn.** Work reaches it as generation,
  refinement or loading (from the Surveyor), as hard messages its Vicinity
  holds for it (edit sets, conserved transfers), and as nudges prompting
  soft checks. Chunks have no mailboxes: a chunk's mail is a queue in its
  Vicinity, signalled by the mail bit on its nudge mask ("Conserved
  objects, hard messages and persistence", below). Nothing outside a chunk
  ever writes it, the frame thread included. **Messages are hard-simulation territory**: soft cross-chunk effects are published state read at checks
  (below), never messages.
- **Reading a chunk goes through its semantic queries** (raycast through the
  chunk, sweep a box, plan a tool's edit, mesh, face connectivity, digest;
  `core/opaque-chunk-queries`), which read the chunk's **live storage,
  zero-copy, with optimistic validation** (below, "Reading chunks"). There is
  no published copy of a chunk and no history of past states. Readers never
  see cells or the representation.
- **At most one turn runs on a chunk at a time.** When a nudge (the mail bit
  included) reaches an idle chunk, an atomic compare-and-swap flips it from
  idle to scheduled and enqueues it; the turn clears the mask, consumes its
  mail from the Vicinity, works, and flips back to idle (or re-enqueues if
  new bits arrived). This flag is the
  scheduler's private mechanism, not a lock anyone else takes.
- **Precedent:** virtual actors (Microsoft Orleans): activated on demand,
  deactivated when idle, one message at a time.

### Who runs on worker threads: actors
- **Every work item is a turn of exactly one actor, or of a claimed set of
  chunk actors, or a Dispenser-internal item** (exit items). Actor kinds:
  - **Chunk:** generation, refinement, loading from storage, consuming
    its queued messages, its own simulation checks, meshing, handing over
    its content and scarce list for a save.
  - **Vicinity:** conserved objects in motion, assemblies, hard-message
    queues, its saves.
  - **System** (one per process): the Dispenser's root actor;
    it loads, pauses, resumes and shuts down Worlds, and is the only actor
    the outside can reach (the actor hierarchy, below).
  - **World** (`engine/world`): one world's
    coordinator: global records, and through its Surveyor component the
    world's residency, generation work, simulation budget and Vicinity
    membership; the Dispenser source for work outside every Vicinity;
    parent of its Vicinities and Registrar. A paused World has nothing to do, so
    pausing comes from outside it, from System.
  - **Registrar** (one per World): the world's persistence
    actor, owning the SQLite store and the chunk-blob store; its turns are I/O
    items (commits, loads, offline collection), so commits never stall the
    World's planning.
  - **Dimension** (possible): per-dimension state such as pocket
    decay.
  - **Client-side actors**: the client's **Vicinity**
    implementation (in multiplayer, local physics for the player's body,
    merged continuously with the authority's, `engine/frame-thread`) and
    the **Listener** (acoustic analysis and voices, `engine/audio`).
  - **Surveyor**: no longer an actor of its own but a
    component of the World (`world/surveyor`, `engine/world`); work best
    done asynchronously (the LoD visibility flood fill) runs as World-owned
    sub-actors that only read chunks through validated reads.
  - **Function actors** (`engine/dispenser`): one turn each, inputs and
    results as members, results reached afterwards through any-thread
    methods; one-turn work that neither has dependencies nor is one (the
    mip filter). Work with dependencies (generation, refinement, meshing
    outside every Vicinity, a save's chunk-blob writes and commit,
    chunk-blob reads)
    uses goal actors (`engine/goal-source`), which their clients
    own and register; a TerrainNode's sibling set is one, owned by the
    World.
  The current-actor marker is set for every work item, so every check that
  relies on it holds universally.
- **Not actors run by workers:** the frame thread (a thread with its own
  channels, running its own loop) and the Dispenser itself.
- **The actor hierarchy and watches**. Actors that manage
  others form a tree, from a single root:
  - **System** (one per process; the Dispenser's distinguished root actor)
    → **World** (one per world, the client's world included) →
    **Vicinity** (many) and **Registrar** (one, the world's persistence
    actor). The Surveyor is a component of the World, not an actor
    (`engine/world`). Chunk actors belong to their Vicinity (outside every
    Vicinity a chunk is World state) and are not halted individually; the
    client's other actors (the Listener) sit under the client's World.
  - **Desired state flows down, observed state flows up.** A parent asks a
    child for something by setting the child's desired state ("paused",
    "shut down") through an any-thread method that stores it and nudges the
    child; the child's turn converges on it. Halting is the exception: one
    call that carries both intent and continuation (below). A child never calls or
    messages its parent about it: the parent learns from its children's
    published state, or from a halt's continuation.
  - **Halting is one call with a continuation:** a parent halts children
    in its turn (`halt(turn, children, then)`, returning a move-only token)
    and ends its turn; the Dispenser runs `then` as a later turn of the
    parent once they are quiet (halted, nothing in flight in their
    subtrees). There is no separate request and waiter, so a parent waits
    only on what it halted. The Dispenser owns this: it knows the tree,
    owns each node's halt flag and a subtree count of batches in flight,
    gives a node with children a halting turn in which it halts them
    itself, in the order it needs, so a node is halted only after its
    children are quiet, and needs no nudge from any child. Quiet is stable by construction:
    nothing is dispatched under a halt, so the count only falls, and only
    the token's holder can resume (`engine/dispenser`).
  - **A parent halts only its children,** never its own layer and never
    further down: System its Worlds, a World its Vicinities and its
    Registrar. So waits form no cycles, halts travel down one layer at a
    time and completion travels up one layer at a time. Checked at compile
    time: `halt` takes the parent's turn passkey and children whose type
    names that parent type as its parent (`ParentOf<Vicinity>` is
    `World`).
  - **The bridge to the outside is the root:** from outside the Dispenser
    (the frame thread, a test driver) the only way in is an **intention to
    System** (shut down, load a world, pause or resume a world), and the
    only way to learn the result is System's published state, polled;
    waiting for shutdown is polling System for "shut down"
    (`engine/dispenser`).
- **The actor convention: one class, separate contracts, zero runtime
  cost.** An actor's API falls into up to three contracts, each enforced by
  one cheap mechanism (checks catch mistakes, not adversaries):
  1. **Any thread:** ordinary public methods, each safe internally (a
     sharded mutex, an atomic, a short lock), such as the Surveyor's
     `nudge`, `find` and `publishViewpoints` (store the latest set, nudge
     the actor), a chunk's read APIs and nudge mask, a Vicinity's queue
     `send`.
  2. **Dispenser source:** the class inherits privately from
     `DispenserSource` and registers itself, so only the Dispenser's base
     pointer can call `pullBatch` and `returnRemainder` (the virtual call exists
     anyway).
  3. **Own turn:** `runTurn(XTurn&)`, where `XTurn` is an empty passkey only
     the Dispenser can construct; every turn has one, a function actor's
     included. State only the turn may touch is wrapped
     as `ActorOwned<T, XTurn>`, whose `get` requires the passkey; it
     inlines to a plain member access.
  ```cpp
  template <class T, class Key>
  class ActorOwned {
  public:
      T&       get(Key&)       { return value_; }
      const T& get(Key&) const { return value_; }
  private:
      T value_;
  };

  class World : private DispenserSource {
  public:
      Surveyor&    surveyor();                                  // its any-thread services:
                                                                // nudge, find, publishViewpoints
      void         runTurn(WorldTurn&);                         // own turn only
  private:
      PullResult pullBatch(/*…*/) override;                          // Dispenser only; its own turns
      void       returnRemainder(/*…*/) override;
      GoalSource work_;        // pulls the steps the Surveyor's goals need
      Surveyor   surveyor_;   // turn-only planning state as ActorOwned<…, WorldTurn>,
                              // plus the sharded map
      ActorOwned<WorldRecords, WorldTurn> records_;
  };
  ```
  - **Every piece of state is declared on one side:** turn-only
    (`ActorOwned`) or shared (`Guarded`, atomics, the sharded map). That is
    the point, more than the compile error. For example, `pullBatch` runs on any
    worker, possibly while the World's own turn runs, so it may not read
    the Surveyor's working state; the goal source's `pullBatch` reads only the
    goals and priorities the World's turn set and the any-thread store, so
    scheduling compute stays out from under the lock.
  - **The same pattern for every actor:** a Vicinity's queues are any-thread
    and its objects and indexes `ActorOwned<…, VicinityTurn>`; a chunk's
    nudge mask and read APIs are any-thread and its writable state is
    turn-only. The per-turn Vicinity handle a chunk turn receives is the
    same idea.
  - **Later, if wanted:** Clang's thread-safety analysis (`-Wthread-safety`,
    with roles such as "World turn" as capabilities) checks the same
    split in a Clang build; the annotations are no-ops on GCC and free at
    run time. Not needed to start.
- **Compute and I/O work items, by duty cycle:** I/O items add a thread for
  their duration, so CPU threads stay at their target (`engine/dispenser`).
- **On "actor"** (the actor model has felt non-rigorous, with
  Hewitt's legacy). What we use is narrow: single-writer owners of state,
  communicating by queued messages, run one turn at a time on a shared
  pool. That is closer to E's vats and turns (Mark Miller) and Orleans'
  virtual actors than to Hewitt's full model, and its rigour comes from
  our own invariants and the planned formal model (`sim/exact-core`), not
  from the label.

### The Dispenser: the worker pool
**Specified in `design/dispenser.md`** (`engine/dispenser`):
its role, acquisition, waking, work items, holds, lockstep mode, worker
count, 18 invariants and open questions. What the rest of this model relies
on:
- **One per process,** started and destroyed explicitly, reachable only
  through two gated headers; turn code can't see either, so it has no
  Dispenser handle and makes work only by sending mail or nudges.
- **All work comes from pull sources** through a non-blocking `pullBatch`; it
  has no ready queue and stores no work. Each Vicinity is the source for
  its own and its members' turns, the World for generation,
  loading and meshing of chunks outside every Vicinity, and the World and
  client actors for themselves.
- **Workers block only at their acquisition point,** never on another
  actor; a reply is mail that makes the actor runnable again. Returning to
  `nextBatch()` is a worker's quiescent point for reclamation.
- **It sets the worker marker** around every work item, which the
  Cartographer's and turn-only APIs' checks rely on.
- **Halts are the Dispenser's:** a parent halts children with one call that
  carries a continuation and returns a move-only token; the Dispenser never
  dispatches under a halt, applies it bottom-up, counts each subtree's
  batches in flight, and runs the continuation once the children are
  quiet. Halts come at three levels
  (Vicinity, World, System), and **System is the Dispenser's root actor**:
  from outside, only intentions to System (shut down, load, pause,
  resume) and reading its published state are possible.
- **Lockstep mode** (one worker alternating with the frame thread) and **at
  least one free-running worker in play** are its configurations.

### Work allocation and the worker loop
**Specified in `design/dispenser.md`** (`engine/dispenser`): vocabulary
(work item, batch, turn), the worker count, the Dispenser dispensing all
work, workers reporting completion, granularity, partial completion by
giving back, and work entering only through sources. What stays here:
- **Meshing follows generation while the data is warm:** outside every
  Vicinity it is a mesh step waiting on its TerrainNode's `done()`
  (`world/surveyor`); which levels are meshed is open there.

## How actors interact

### Reads and writes, by who needs them
- **Reads are synchronous semantic queries** on the chunks a caller's
  Vicinity holds, cheap and parallel, with no messages and no ownership.
  A query that spans several chunks (a world raycast, a body's sweep) can be
  validated across all of them, so it sees one consistent view.
- **Writes are hard messages,** queued in the chunk's Vicinity and
  consumed in the chunk's next turn. An idle chunk is scheduled at once; a
  busy one picks them up in its next turn.
- **Meshes** are produced in the chunk's own turn (below, "Meshing").
- **The player's body and other movers are Vicinity-owned** ("Vicinities", below). Moving a body is reads
  (collision sweeps through thread-safe chunk APIs, `sim/swept-collision`;
  ground; intersections), so it generates no message traffic. Edits
  (breaking, placing) are hard messages the body's Vicinity sends when the
  body acts on the player's intentions; soft effects in either direction are
  published state read by the affected side (a pressure plate sees the
  body's physics proxy; the Vicinity reads the water's state and applies the
  push).
- **Edits carry the expected old state of each cell**: a
  per-cell compare-and-set. The chunk applies each edit only where the cell
  still holds what the tool saw, rejects the rest, and replies with the
  result; no history is needed to validate. Edits that must be all-or-nothing
  across chunks are Vicinity transactions (below).
- **Saving the soft layer needs no consistent snapshot**:
  each chunk is saved as its own consistent state; neighbours saved a moment
  apart are no worse than neighbours that ticked at different rates or sat
  lazy, and cross-chunk soft effects converge after load (level-triggered
  re-assertion, anchor versions). A consistent snapshot matters only for conserved
  things (items and entities crossing seams, valuable blocks), which is the
  ledger's job (`persist/edit-atomicity`). (Scarce, conserved state is
  saved per Vicinity in the world's scarce table, "Conserved objects, hard
  messages and persistence".) Loading is a pure
  goal actor that reads chunk data from its chunk blob instead of the seed;
  a chunk
  actor is built from it only when the chunk enters a Vicinity
  (`world/chunk`).

### Messages, nudges and published state
- **Chunk APIs may be called across threads when specified thread-safe.**
  **Read APIs are synchronous**, using validated atomic reads (a mover's
  sweep through a chunk's geometry is the chunk's own computation);
  **mutating APIs are asynchronous**, enqueueing an intent, since a chunk's
  state changes only while it runs. A mutating call *is* a message with a
  typed shape.
- **Messages are hard simulation; the soft simulation has none.** Soft
  simulation is declarative and level-triggered: each owner publishes
  level state about itself (face-layer markers, physics proxies, its own
  cells) and
  others read it at their own checks. **Nudges** are content-free wakeups;
  **a nudge may carry a location hint** ("check your +x face"), part of
  the pointer to the thing to notice, not the thing itself. **Messages**
  are exactly-once commands: conserved transfers (move semantics, like a
  `unique_ptr` in the message), edits with expected old state, assembly
  commits. They can't be dropped and are captured by saves; nudges are
  not.
- **Rendering** draws chunks' entities and the Vicinity's objects, both
  through thread-safe reads; a mover never stalls on a chunk being
  scheduled, because the Vicinity simulates it continuously.
- **Prompt action:** a message for an idle chunk (its mail bit) schedules
  it at once, at high priority (its idle-to-scheduled CAS); target well
  under a frame.

### Chunk records, the map and nudges
- **Record versus content** (what must outlive a transient chunk belongs
  with the Surveyor): the
  **chunk record** is the stable part. It holds identity, version and
  stage, and points at the chunk's actor while one exists; the actor holds
  its Vicinity pointer (never null,
  `invariant/vicinity/one-vicinity-per-chunk-actor`),
  scheduling state (idle/scheduled), the nudge mask, counters and the
  representation pointer (`world/chunk`). It is owned by the
  Surveyor and outlives the chunk's transient **content** (its
  representation: blocks, settled entities, mesh state). Content comes and
  goes by representation swap ("Reading chunks", below); the record is
  created when the chunk is first requested at full detail and removed
  only on unload.
- **Which chunks have records:** those held at full detail, as an actor in
  a Vicinity or as a full-detail terrain node in the World.
  Distant terrain has no per-chunk records: it is held per box (a chunk or
  a larger volume) in the World's tables (`world/chunk`,
  `world/octave-pyramid`).
- **The map**: a sharded hash map keyed by
  (dimension, chunk coordinate) to a record pointer, covering chunks with
  records only, with a short
  `std::mutex` per shard. It holds an index lookup only, never work
  (the frame-thread waiting rule: nanosecond critical sections are
  fine, waiting behind work is not). **Only the Surveyor writes** (insert on
  request, erase on unload). A pointer read from the map stays valid until
  the caller's next quiescent point (quiescent-state-based reclamation:
  workers between turns, the frame thread between frames), so records are
  not reference-counted. Hot paths don't touch the map: a Vicinity keeps
  its own chunk index for its members and neighbours.
- **Hard messages are lossless and live in the Vicinity,** not in the
  record ("Conserved objects, hard messages and persistence", below).
- **Nudges** (a content-free "look again"): a `std::atomic<uint32_t>` hint mask on the chunk's actor.
  Bits 0–25 name the 26 neighbour directions the nudge came from; bit 26 is
  "general" (no location); bit 27 is **mail** (the Vicinity has queued a
  hard message for this chunk). A nudge does `fetch_or(bits)`; if the previous
  mask was zero and the chunk is idle, the nudger CASes the scheduling state
  from idle to scheduled and the chunk is marked runnable in its source (its
  Vicinity; only chunks in a Vicinity have actors; a Vicinity resuming
  after a merge or cleave rescans its members' masks, so a mark that
  landed in an absorbed Vicinity isn't lost). A turn starts
  with `exchange(0)` and uses the bits to narrow what it rechecks. The
  mail bit ranks above the others in scheduling. A nudge to a **frozen**
  chunk sets its bits but doesn't schedule a turn (frozen chunks run no
  checks); starting to run sets the full mask anyway. A nudge to a chunk with no
  actor is **dropped**: nothing is lost, because:
- **Load and starting to run do a full check:** when a chunk is loaded or
  starts to run, its mask is set to all bits, and it nudges each loaded
  neighbour with the matching direction bit, so state that changed while it
  was absent is picked up from both sides (`core/level-triggered`).
- **The API path**:
  ```cpp
  // One Surveyor per world, so (dimension, coordinate) is enough.
  class Surveyor {
  public:
      void nudge(DimensionId, ChunkCoord, NudgeBits);  // dropped if absent
  };
  ```
  The Surveyor looks up the record pointer from the integer indices; if
  absent, the nudge is dropped; otherwise it calls `chunk.nudge(bits)` (the
  atomic `fetch_or` above). Callers with a Vicinity index skip the map and
  call the record directly. There is no `surveyor.send`: hard messages go
  only through a Vicinity (below).

## Shared memory, ownership and locking

### What is genuinely shared
- **The Surveyor:** which chunk records exist, keyed by
  coordinate (for chunks held at full detail; distant terrain is held per
  box in the World's tables, `world/chunk`), together with generation work
  and unloading. Lookups use a
  sharded map only the Surveyor writes.
- **The sources' runnable sets** (each Vicinity's ready chunks, GoalSource's
  runnable goal actors; the Dispenser has no ready queue); generation and
  refinement work is the Surveyor's.
- **Chunk storage itself, through a version counter per palette-form chunk** (below): one writer (the
  chunk actor's own turn), many optimistic readers. Chunk data the World
  holds outside every Vicinity is immutable and needs neither.
- **Hard messages:** the Vicinity's per-chunk queues (edits, conserved
  transfers), each multi-producer and consumed only by its chunk's turn
  (chunks have no mailboxes). A sender with several
  messages batches them into one enqueue.
- **The GPU:** uploads and submission stay on the frame thread (multi-threaded
  command recording is a later topic, `sim/cpu-parallelism`).

### Reading chunks: versioned storage, validated zero-copy reads
The goal: a zero-copy cross-thread mechanism that is thread-safe
*enough*, ideally paying no more than something like an STM log; dirty reads
of raw palette arrays would be too weak a guarantee, so the next best is an
ultralightweight shared-memory concurrency model behind the virtualized API.
- **Start with one seqlock counter per chunk**, and **instrument retries**; split into per-subchunk counters only
  if retries show it's needed.
- **Scope of the terms**: this is the **reference implementation
  for palette-form chunk data** (the palette representation, `world/brick`), not for
  air or uniform chunks. "Chunk" is an engine-wide term; **"subchunk" is
  scoped to particular representations,** each of which may define it its
  own way (named "subchunk" to avoid the in-game brick blocks and the
  `brick` symmetry type).
- **A seqlock, the lightest form of STM.** The palette-form chunk's storage has
  one version counter (or, later, one per subchunk, say 16³). The chunk's own
  turn is still the only writer.
  - **Writing:** make the subchunk's counter odd, write the cells, make it even.
    An edit set or a step's changes to a subchunk are applied within one window,
    so a reader never observes half an edit.
  - **Reading:** read the counter, read the cells straight from the live
    arrays, read the counter again; if it was odd or changed, retry.
    Readers are zero-copy and never write shared memory, so many readers
    don't contend for cache lines.
  - **Retries are rare and bounded:** write windows are short and per subchunk;
    reads are short semantic queries (a raycast through a chunk, an edit plan, a
    collision sweep).
- **Consistent reads across subchunks or chunks** (a TL2-style read-only
  transaction): read the counters of every subchunk touched, do the query,
  re-check them all; retry if any changed. A raycast or sweep across a seam
  sees one consistent view.
- **The virtualization keeps it cheap**: only representations that
  need it use it, and they hide it behind higher-level semantic queries
  (cast one or more rays, sweep a box). **A uniform chunk is one word,** read
  with a single atomic load: no subchunks, no counters; an air chunk costs
  nothing. Each representation picks its own subchunk size, or one counter per
  chunk.
- **Structural changes** (promoting uniform to palette, growing the palette's
  bit width, reallocating index arrays) are the one way a reader could touch
  freed memory. They are rare, so a swapped pointer plus deferred
  reclamation suffices, with no per-read cost:
  - **Representation swapping, in situ** (not unload and reload through
    the Surveyor). A chunk actor holds **one pointer to its current
    representation object** (uniform or palette) next to what doesn't
    change with representation (identity, nudge mask, entity table, block
    metadata, version counters). A change of representation, or growth that
    reallocates storage, has the chunk's own turn **build a new object from
    the old, store the new pointer atomically inside its seqlock write
    window, and hand the old object to a retire list**. The chunk never
    holds two representations; ordinary edits within a representation still
    modify it in place with no copy. Readers go through the current
    pointer's virtual API; a reader that loaded the old pointer fails
    validation and retries with the new one.
  - **The chunk actor is stable for its tenure in a Vicinity:** its
    identity, nudge mask and scheduling state, entity table, metadata,
    counters and Vicinity membership never move while it is in a Vicinity,
    running or frozen, so nothing is redirected and no nudge is lost. Outside every
    Vicinity there is no chunk object at all, only World state
    (`world/chunk`), so the actor machinery never has to move: it exists
    only where it is used, and a chunk entering a Vicinity gets a new
    actor. **The indirection is kept cheap:**
    hot consumers make few, large calls (results, not cells,
    `core/opaque-chunk-queries`); calls dispatch on a small type tag stored
    next to the representation pointer in the record's cache line (a
    `switch` to statically dispatched, inlinable functions, rather than a
    virtual call); a uniform chunk's single block state is stored inline
    in the record, with no indirection at all.
  - **Representations don't need to understand each other:** each
    implements **export** (stream its contents in a neutral form, such as
    runs of block state and count in a fixed cell order) and **build from a
    stream**, so a conversion is `old.export → New::build`, with no N×N
    converters.
  - **Quiescent-state-based reclamation (QSBR).** The virtual API protects
    meaning, not lifetime: a retired object can't be freed while a reader
    may still be inside it. Every thread has natural quiescent points where
    it provably holds no chunk pointers: **a worker between turns** (a turn
    neither migrates nor suspends) and **the frame thread between frames**.
    Each thread copies a global grace-period counter at its quiescent
    points (the worker loop at every turn boundary; the frame thread once
    per frame), and a sleeping worker is offline, so idle workers never hold
    up reclamation; a retired object is freed by a cleanup actor once every
    online thread has passed a quiescent point since its retirement
    (`engine/dispenser`'s reclamation). No per-read cost; memory is freed
    about a turn, or on a client a frame, late, so retired memory stays
    bounded.
  - **Unloading uses the same mechanism:** a chunk record, and anything
    else readers reach by raw pointer, retires the same way once the
    Surveyor drops it.
  - **Invariant:** no thread holds a chunk or representation pointer across
    its quiescent point. Workers can't, by the turn model; the frame thread
    holds chunk ids, not chunk pointers, between frames (already true of the
    render table).
- **C++ memory model:** reading data that is being written is a data race even
  if the counter check discards the result, so protected reads are relaxed
  atomic loads (plain loads on x86, no cost). This shapes the storage types.
- **No history:** there are no past states and no tick-addressed reads. A thought experiment
  (consistent current reads only) showed nothing essential needs history:
  rendering, prediction, soft simulation, edits (compare-and-set) and chunks
  at different rates all work; atomic multi-chunk decisions are
  Vicinity transactions; soft saves need no consistent snapshot; entity interpolation keeps its
  two states in the entity scope. Short rewinds exist only on the client
  (undoing predicted edits), as reversible deltas captured while applying
  (cell, old, new); closed-form state needs none.

### Ownership and borrowing: the pointer rules (draft)
**Checks exist to catch mistakes, not adversaries:** we are not our own
adversary. A check earns its place by catching a plausible slip cheaply (a
header a target shouldn't see, a deleted copy, an assert at an entry
point, a sampled comparison), not by making misuse impossible. Prefer
conventions backed by one simple check over elaborate type machinery;
leave a loophole open when closing it costs more than the mistake it would
catch.

The rules every actor follows for objects shared between actors, chosen to
be obviously safe, with every violation a fatal error in release builds too
(`data/cheap-invariants`). Language support is used where it is free, never
at a real cost in performance. For each piece of code, a shared object has
one of two roles: **owner** or **borrower**.
- **`invariant/concurrency/one-owner`** — Every object has exactly one
  owner at a time (an actor's state: the World owns its TerrainNodes),
  holding it by `unique_ptr` or in a container of them; ownership moves
  only by `std::move`.
  - **Enforced:** type: `unique_ptr`, and shared types are not copyable.
  - **Why:** rules out double frees.
- **`invariant/concurrency/immutable-once-published`** — An object other
  actors can reach is `const` from the moment it is published into a
  shared index (the octree's index) or completed as a goal actor
  (`engine/goal-source`); publishing is a release store, finding an
  acquire load.
  - **Enforced:** type: shared types are exposed only as `const T` once
    published.
  - **Why:** rules out data races on shared objects.
- **`invariant/concurrency/live-storage-single-writer`** — The one kind of
  shared object written after other threads can reach it is live chunk
  storage (a chunk actor's live Brick): only its owner's turn writes it,
  in place, and other threads reach it only through the owner's read API,
  by relaxed atomic loads, either validated against the version counter
  or with the dirty-read tolerance that API declares. The storage is
  pointer-free and never reallocated in place (a change of size or form
  builds a new object, swapped in and retired), so a racing read can see
  stale or partly written cells but never freed memory.
  - **Enforced:** type: the storage is reachable from other actors only
    through the chunk's read API (`core/opaque-chunk-queries`), and its
    cells are atomic types; review: each read query's validation or
    declared tolerance (`world/brick`); test: ThreadSanitizer builds.
  - **Cost:** relaxed loads (plain loads on x86), plus the version check
    where a query validates.
  - **Why:** lets cross-thread reads of hot chunk data be zero-copy
    without making the owner copy on write.
- **`invariant/concurrency/free-after-grace-period`** — A published object
  is freed only by a cleanup actor its owner registers with `retire` after
  unlinking it from every shared index; the Dispenser runs the cleanup only
  after a grace period (`invariant/dispenser/grace-period`).
  - **Enforced:** type: shared types' destructors are private, friended to
    the cleanup actor; fatal check: `Borrow<T>`'s generation check
    catches unlinking or grace-period bugs; model: TLA+ `QSBR`
    `NoUseAfterFree`.
  - **Cost:** a load and compare per dereference.
- **`invariant/concurrency/borrow-one-turn`** — A borrower gets a pointer
  only from a shared index, inside a turn, and it doesn't outlive that
  turn: not in actor state, a message, a goal or a static. Anything that
  names a shared object beyond a turn holds its key and looks it up again,
  except the object's owner (`invariant/concurrency/owner-pointers-end-at-retire`)
  and a goal actor's dependency pointers
  (`invariant/concurrency/dependency-pointers`).
  - **Enforced:** fatal check: `Borrow<T>` records the work item's serial
    when made (in the turn, or in `pullBatch` for what a source resolves
    for it), and each dereference checks it against the thread's current
    serial; type: goals, messages and actor state hold keys by type, and
    `Borrow<T>` can't be stored in them (no raw-pointer conversion, no
    default constructor); model: TLA+ `QSBR` `NoUseAfterFree`.
  - **Cost:** a thread-local load and compare per dereference, hoistable.
- **`invariant/concurrency/owner-pointers-end-at-retire`** — An owner may
  keep pointers to what it owns across its own turns, but none survives the
  turn in which it unlinks the object and passes it to `retire`.
  - **Enforced:** fatal check: `Borrow<T>`'s generation check catches a
    dereference after the free.
  - **Cost:** a load and compare per dereference.
- **`invariant/concurrency/dependency-pointers`** — A goal actor may hold
  `DepPtr<T>` members to its dependencies across any number of grace
  periods, set at registration and dereferenceable only inside its own
  turn, which runs only once every dependency has completed; a dependency
  is freed only by a cleanup registered after `GoalSource::withdraw`,
  which first abandons every dependent that hasn't started
  (`engine/goal-source`).
  - **Enforced:** type: `DepPtr<T>` dereferences only with the holder's
    `GoalTurn&` passkey, which only `pullBatch` constructs, and a goal
    actor's destructor is friended only to the cleanup actor; fatal check:
    `DepPtr<T>` records its target's generation and each dereference
    compares, and adding a goal actor to a cleanup asserts it is already
    withdrawn; model: TLA+ `GoalSource` C1 and C3.
  - **Cost:** a load and compare per dereference.

**Why they are safe:** a borrow lives within one turn
(`invariant/concurrency/borrow-one-turn`), so it ends before its thread's
next quiescent point; a free waits until every online thread has passed a
quiescent point since the unlink
(`invariant/concurrency/free-after-grace-period`,
`invariant/dispenser/grace-period`); so no borrow outlives
its object. A dependency pointer is usable only from a pull, and every
pull is either before its dependency's withdrawal, so reclamation covers
the turn, or after it, when the dependent has been abandoned and is never
pulled (`invariant/concurrency/dependency-pointers`).
`invariant/concurrency/one-owner` rules out double frees, and
`invariant/concurrency/immutable-once-published` and
`invariant/concurrency/live-storage-single-writer` together rule out data
races on shared objects.

**Enforcement, all fatal in release** (a summary of the entries above):
| Invariant | Check | Release cost |
|---|---|---|
| `invariant/concurrency/one-owner` | `unique_ptr`; shared types not copyable | none (compile time) |
| `invariant/concurrency/immutable-once-published` | shared types exposed only as `const T` once published | none (compile time) |
| `invariant/concurrency/live-storage-single-writer` | live chunk storage reachable only through the chunk's read API; cells are atomic types | relaxed loads, plus a version check where a query validates |
| `invariant/concurrency/free-after-grace-period` | shared types' destructors private, friended to the cleanup actor | none (compile time) |
| `invariant/concurrency/borrow-one-turn` | `Borrow<T>` records the work item's serial when made (in the turn, or in `pullBatch` for what a source resolves for it); each dereference checks it against the thread's current serial; goals, messages and actor state hold keys by type, and `Borrow<T>` can't be stored in them (no raw-pointer conversion, no default constructor) | a thread-local load and compare per dereference, hoistable |
| `invariant/concurrency/free-after-grace-period`, `invariant/concurrency/owner-pointers-end-at-retire` (unlinking or grace-period bugs) | `Borrow<T>` also records the object's generation; shared objects live in **type-stable memory** (slabs, never unmapped), freeing bumps the generation, and each dereference compares | a load and compare |
| `invariant/concurrency/dependency-pointers` | `DepPtr<T>` dereferences only with the holder's `GoalTurn&` passkey, which only `pullBatch` constructs; it records its target's generation and each dereference compares; a goal actor's destructor is friended only to the cleanup actor, and adding one to a cleanup asserts it is already withdrawn | a load and compare per dereference |
A failed check logs the check, the actor and the turn, then terminates;
only checks that need scans (auditing an index for a dangling entry) are
debug-only.
- **Type-stable memory** is the one cost the rules impose: freed shared
  objects stay readable so the generation check is valid, which rules out
  returning their memory to the general heap. Slabs suit them anyway
  (TerrainNodes' grids are all one size).
- **Free language support,** since Clang is the compiler
  (`deps/toolchain`): `[[clang::lifetimebound]]` on accessors such as
  `find` (a warning when a borrow obviously escapes its scope) and
  thread-safety analysis (`-Wthread-safety`, for `ActorOwned` and mutexes),
  both on with `-Werror`. The C++ Core Guidelines lifetime profile isn't
  usable in practice yet.

### Locking and waiting
These rules apply to every thread, and most sharply to the frame thread.
Workers block only at their acquisition point, never on another actor
(`engine/dispenser`).

**The frame thread never waits while other threads do work.** It
may block, even context-switch, on a lock only when that is extremely rare
(nanosecond critical sections against millisecond turns) and only behind
another thread leaving that tiny critical section, never behind
application-level work. Standard C++ synchronization primitives are
preferred to custom lock-free data structures; `try_lock` is used
where it costs nothing.

**No nested locks** (get in, get out): no thread
takes a lock while holding another, and `advertise` and `try_lock` are never
called under another lock. Locks are short mutual exclusion, never long
periods of exclusion.

**Which primitive where**: `std::atomic` (with
`wait`/`notify_one`) for counters, epochs and signals, such as the
Dispenser's eventcount and advertised priorities, and the seqlock version
counters; `std::mutex` for short mutual exclusion around data-structure
manipulation, such as the staging block pool and the descriptor vector.
**Safeguards on every lock the frame thread touches**:
critical sections only move indices and pointers, with capacity reserved so
nothing reallocates while a lock is held; debug builds record lock hold
times (maximum and percentiles per lock), so a growing critical section
shows up before it shows in frame times. Context: the frame thread already
synchronizes invisibly in `malloc`, logging and the Vulkan driver, and the
OS preempts it far more than these locks could; the one outlier is
preemption of a lock holder (up to a scheduler time slice), which
`try_lock` removes where used. Priority inversion only matters if the main
thread is ever given a higher OS priority than the workers.

## Threads outside the actor model

### The frame thread and the audio callback
**Specified in `design/frame-thread.md`** (`engine/frame-thread`): its
role, its loop, its part in lockstep mode, invariants and open questions.
In short: **the frame thread** runs outside the actor model, on the
process's main thread (GLFW requires window and event handling there),
once per frame: input in, a rendered frame and audio out. A headless
server has none. The locking rules ("Locking and waiting", above) apply
to it most sharply. **The audio callback thread** (`engine/audio`) is another
thread outside the actor model, driven by the OS's audio device, with
stricter rules still: no locks, no allocation, no waiting; it is fed by
atomic snapshots and one single-producer, single-consumer ring buffer, the
named exception to preferring standard primitives over lock-free
structures.

**It gathers input and renders the world.** It runs the frame loop, turns
input into intentions and routes them to the player's body, applies mouse
look directly, evaluates the trajectories it is handed (`engine/frame-thread`:
input goes to the body through a per-player slot it pulls from), draws the
body's published target and tool preview, publishes viewpoints to the
Surveyor, uploads finished meshes and records frames. It writes no chunk
and **sends no hard messages**: an edit is something the body
does, such as a pick's final swing breaking a block, so the edit command is
sent by the body's owner, its Vicinity, in its own turn, the same path a
mob's action takes. Intentions cross a thread in single-player and the
network in multiplayer (`net/client-server-seam`, `net/source-blind-client`).
Discrete actions (a click to place) travel in a short, bounded,
sequence-numbered event ring in the player's input slot; they aren't conserved, so a
lost one is a nuisance, not a duplication route.

## Determinism and verification
- **The soft layer is not deterministic and needn't be.** Its results are
  subject to OS thread scheduling. The goal is **very high performance with
  acceptable, self-repairing, non-game-breaking wonkiness**, achieved through
  very high rigor in modelling and encapsulating the concurrency constructs,
  not through determinism.
- **The hard layer** (the ledger, conserved transfers, valuable blocks) still
  needs correctness by construction: conservation and "in exactly one place"
  hold whatever the thread timing. Whether it needs determinism is
  its own question (`sim/exact-core`).
- **Lockstep mode, the reference for tests and reproducing bugs**: one worker in strict alternation with the frame thread (in
  headless tests, a test-driver thread), so the two never run at once:
  - **a baton, not just a mutex:** the frame thread runs its part of the
    frame, hands over and waits; the worker runs its share and hands back
    (two binary semaphores, or a condition variable with a turn flag). A
    mutex alone gives exclusion but lets the OS pick the order;
  - **the worker's share is counted, not timed:** a fixed number of turns
    per handoff, or until the ready set is empty, chosen by the
    Dispenser's seeded draw. Intentions are tagged with base ticks and
    viewpoints are published at the handoff, so inputs land at
    reproducible points;
  - **every check stays unconditional:** the frame thread never runs a
    turn and turn code never runs on it, in every mode; the threads are
    real, so ThreadSanitizer still sees two threads and the handoff's
    happens-before edges;
  - **the frame thread's waiting rule is relaxed by declaration** in this
    mode: it waits behind the worker on purpose, and its blocked-time
    statistic is not counted. For tests and debugging, never play.
  - **Lockstep versus N workers:** generation, edits and later conserved
    state must match exactly; soft simulation must converge. N-worker tests
    check invariants, convergence and the absence of persistent glitches
    rather than digest equality.
  - Not to be confused with lockstep networking (`net/lockstep`, out of
    scope): this is two threads in one process.
- **Generation stays pure,** so lockstep versus N-worker equivalence holds
  exactly for generation alone (no simulation running).
- **ThreadSanitizer** stress scenarios (streaming, edits while streaming,
  simulation with edits), and debug checks: no write to a chunk outside its
  own turn; at most one turn per chunk; every read validated.

## Hard simulation and persistence

### Vicinities
**Specified in `design/vicinity.md`** (`sim/vicinity`): its role,
membership, conserved objects, hard messages, halts, invariants and open
questions. What the rest of this model relies on:
- **The dividing line is per chunk versus across chunks, not blocks versus
  entities.** Blocks and entities that stay in a chunk are simulated in
  its own turn, single-threaded and with no locks; anything that crosses a
  seam with hard consequences happens inside exactly one Vicinity, a
  single-threaded actor one level up. A central ledger or entity table
  that every chunk's physics contended on would be a serialization point,
  just as running every block operation on one thread would be.
- **Vicinities are disjoint and sized like simulation distance.** They
  merge when they would touch and cleave at halts,
  and never hand chunks or objects to each other: a handoff between
  Vicinities would push the two-phase handshake up one level.
- **Settled things are chunk-owned, movers Vicinity-owned.** A mover
  transfers at most twice (out of a chunk, then into the one it settles
  in); a Vicinity's movers are simulated serially, a named exception under
  `core/parallel-by-default`.
- **Freeze at the edge:** nothing a Vicinity owns leaves it, its running
  chunks are complete by construction, and a body that needs a chunk that
  isn't running waits at the edge while the thread carries on.
- **What this buys:** no global serialization point; a consistent save
  per Vicinity, which closes duplication by unsynchronized saves inside
  it; and a bounded failure and lifecycle domain for loading, unloading
  and regeneration.
- **Render and simulation are decoupled:** render working sets overlap,
  follow each viewer and never merge; in single-player both derive from
  the same viewpoints through the Surveyor without being the same object.

### Conserved objects, hard messages and persistence
- **The duplication routes this design must close.** Inside a chunk,
  single-threaded work can't duplicate; the routes are elsewhere, and each
  has a mirror-image loss. Each is listed with what closes it:
  1. **crossing a chunk boundary:** conserved objects outside a chunk are
     the Vicinity's, and move only as hard messages, delivered exactly
     once;
  2. **acting on a read as if it were custody:** a conserved effect moves
     only by a hard message from its owner, never by a write based on a
     read (pickup is initiated by the item's owner);
  3. **saving at different moments:** one save path per Vicinity, rows
     written only if newer, and a running Vicinity's capture checked
     against its transformation counter;
  4. **torn writes across stores:** scarce instances live only in the
     scarce table, chunk blobs hold only plentiful content, and one SQLite
     transaction switches the manifest and applies the rows together;
  5. **two live instances of one chunk:** one record per chunk
     (`invariant/surveyor/one-record-per-chunk`) and one Vicinity per chunk
     actor (`invariant/vicinity/one-vicinity-per-chunk-actor`);
  6. **messages or transfers reaching an absent, unloading or replaced
     chunk:** can't arise, since a Vicinity's running chunks are complete
     by construction and a chunk leaves only by being cleaved off with its
     queue;
  7. **shape-rewriting processes** (decay, reality bombs, pocket decay)
     **touching owned entities:** open;
  8. **a derived copy promoted to authority** (client prediction, caches,
     level of detail): the World's chunk data is never authoritative
     (`world/chunk`), and a client's edits
     are tentative until the authority replies;
  9. **rewinds, replays and duplicate delivery:** exactly-once delivery,
     and idempotence by ULID with versioned rows;
  10. **the soft-to-hard mint:** allowed by design, for low-value soft
      output only;
  11. **holders and their contents:** a row's location may be a holder and
      slot, so a box's interior moves with the box;
  12. **partial acceptance of quantities:** the remainder must return
      exactly; receivers resolve, never reject, and running totals check
      it (`sim/vicinity`).
- **The axes of the hard/soft boundary:** value and expectation
  (`core/soft-over-hard`, plus mechanisms players understand); scope
  (within a chunk, or across chunks); persistence (memory only, or across
  saves and crashes); lifecycle (steady state, or load, unload,
  regeneration, decay); authority (server state, or derived copies).
- **One kind of conserved object.** Any conserved thing outside a chunk's
  settled state is an object the Vicinity owns, in one of two states:
  - **in motion:** the Vicinity simulates it (an arrow in flight, a falling
    block, a mob crossing a seam);
  - **ready for chunk C:** queued for C to take in its own turn (an arrow
    that has come to rest in C, an item given to C).
  Coming to rest moves an object from the first state to the second; a
  chunk sending something out puts it into the first. Once a chunk takes an
  object it is **settled**, part of that chunk's simulated state, and
  persisted in the scarce table (below), never with the chunk's content.
  Blocks pushed across a seam stay blocks: converting them into ledger
  entities would buy only custody, and their animation needs only a
  closed form.
- **Commands queue alongside objects:** edits with expected old state and
  assembly commits are hard messages with no object. Edits get a reply,
  "applied" or "rejected" (a fact, never a retry request).
- **Hard messages live in the Vicinity, never in chunks.** The Vicinity
  holds a queue per receiving chunk (and one for itself), guarded by a
  short `std::mutex`, since chunk turns run in parallel with the
  Vicinity's own. Delivery is exactly once, in order per sender; nothing
  droppable ever enters a queue (anything a receiver may ignore is a nudge
  or published state).
- **Chunks have no mailboxes.** Sending to C enqueues into C's queue and
  sets C's **mail bit** on its nudge mask. In its turn, C consumes its
  queue one message at a time; consuming transfers ownership into the
  chunk. C may put an object straight back out through its Vicinity
  handle; otherwise the object is settled in C and invisible elsewhere.
- **Hard sends go only through the Vicinity.** Freeze at the edge means
  sender and receiver are always running chunks of the same Vicinity, so there is
  no `surveyor.send`. The Dispenser reads the chunk actor's own Vicinity
  pointer at turn start and passes a **per-turn handle** into the turn;
  the handle is not kept past the turn (a convention, with copy and move
  deleted; merge and cleave rewrite the pointer only while halted, and
  QSBR keeps the Vicinity valid through the turn). Only turns of running
  chunks get a handle with `send`; frozen chunks get none.
- **No absent receivers.** A Vicinity's running chunks are complete by
  construction and change only at a quiesce, so a hard message's receiver
  always exists. A chunk leaves a Vicinity only by being cleaved off with
  its queue,
  into the new Vicinity (below), never into a closed mailbox.
- **Vicinities are never persisted.** They are runtime structures,
  rebuilt from chunks, viewpoints and the world's scarce table.
- **Scarce and plentiful.** **Scarce** things are the conserved,
  ledger-tracked instances (items, mobs, pets, vehicles, valuable blocks
  once minted, `data/entity-ledger`); everything else is **plentiful**
  (terrain shape, ordinary blocks, soft simulation state). Chunks simulate
  both, but a chunk's saved content holds only plentiful state: every
  scarce instance is persisted in the world's **scarce table**, by
  whichever Vicinity owns it when that Vicinity saves.
- **The scarce table:** one row per instance, keyed by a **ULID** minted
  with the instance (random, 128-bit, so any Vicinity mints without
  coordination). A row holds the instance's kind, state, location (an
  anchor chunk and cell, a holder and slot, in motion or queued in a
  Vicinity, limbo, or equity) and a **version**, and the table is indexed
  by anchor chunk. **Rows are never deleted at runtime:** a consumed
  instance moves to **equity**, the ledger's sink side, as a location
  naming its sink (crafted into, burned, despawned), so consuming is a
  move like any other (`data/entity-ledger`).
  The key is the instance itself, so a row can be overwritten but never
  doubled: a crash can roll an instance back to where it was last saved
  (its place, or who held it), but can't duplicate it or lose it.
- **What makes that hold:**
  - **`invariant/concurrency/vicinity-writes-owned-rows`** — A Vicinity
    writes only the rows it owns when it captures them, never a row for
    something it no longer holds.
    - **Enforced:** fatal check: the Vicinity's save audit also checks that
      every captured row's location is inside this Vicinity
      (`sim/vicinity`).
  - **`invariant/concurrency/versions-rise`** — Every scarce row is
    versioned, and whoever holds an instance raises its version whenever it
    changes hands or state, moving to equity included; a chunk's content
    version rises with every change to its content. The Registrar applies
    a row or manifest entry only if it is newer
    (`invariant/registrar/newer-rows-only`).
    - **Enforced:** type: every move and transformation goes through one
      ledger mutation API, which raises the version; fatal check, in every
      build, inside the Registrar's commit transaction: each row and
      manifest entry carries a digest, and an incoming one whose version
      equals the stored one but whose digest differs means its holder
      changed it without raising the version.
    - **Cost:** one digest comparison per equal-version row, in the
      commit.
    - **Why:** a delayed save from an earlier owner can then neither
      overwrite a newer row nor bring a consumed instance back out of
      equity, so commits from different Vicinities need no order.
  - **`invariant/concurrency/scarce-by-ulid`** — Chunk content refers to
    scarce instances by ULID and never holds them; the table is
    authoritative. A valuable block in a wall is a cell that refers to its
    row.
    - **Enforced:** type: a scarce instance is a move-only object held only
      by a Vicinity or the table, and a cell refers to it through a
      reference property carrying its ULID (`sim/block-metadata`).
  - **`invariant/concurrency/identity-survives-state-changes`** — An
    instance keeps its ULID while it stays the same instance (a valuable
    block mined and picked up changes only its row's state and location).
    When instances become others (an ore block into nuggets, crafting,
    splitting or merging a stack), the outputs get new ULIDs and the
    inputs' moves to equity travel with them, committed in the same
    transaction as their rows.
    - **Enforced:** type: one transform API takes the inputs and outputs
      together and emits the inputs' moves to equity in the same capture;
      test: the offline balance of the books at startup.
- **Loading reconciles towards the table:** a cell that refers to a ULID
  whose row says it is elsewhere shows nothing there; a row anchored at a
  cell restores its instance there. Where the two can't be reconciled (the
  cell now holds something else), the instance goes into **limbo**, a
  location in the table, rather than being duplicated.
- **One save path per Vicinity:**
  1. **write the dirty chunks' blobs** (plentiful content only), as pure
     I/O goal actors; chunk blobs are write-once and unreferenced until the
     commit, so
     this needs no ordering;
  2. **capture the scarce rows** in memory: every member's (through
     validated reads, or each chunk's turn handing its list over) and the
     Vicinity's own (in motion or queued), with any travelling moves to
     equity;
  3. **commit one SQLite transaction,** through the Registrar: switch the
     manifest to the new chunk blobs and apply the captured rows (each only if
     newer), with the world records. Nothing is gathered
     while the transaction is open, so it is short.
  A crash before the commit leaves the old manifest and rows current and
  the new chunk blobs unreferenced; after it, the new ones are current. No
  two-phase commit.
- **Idle or running:**
  - **A frozen Vicinity** (cleaved off, unobserved, paused or shutting
    down: its simulation stopped, its own turns still running) changes
    nothing in the world while it saves, so its save always commits. It then shuts down (a cleaved or unobserved Vicinity dissolves,
    and its chunk actors dissolve, their data populating full-detail terrain
    boxes, `world/chunk`) or
    resumes (pause).
  - **A running Vicinity saves optimistically:** it reads its
    **transformation counter** when the capture starts and again when it
    ends, and abandons the save if the counter changed. The counter changes
    only on a mint, a consume or a transformation, never on a move: an
    instance that moves during the capture is captured twice (one row, by
    its key) or missed (its row stays as last saved), which is at worst a
    rollback, whereas a transformation straddling the capture could record
    both its inputs and its outputs, the one way to duplicate value.
    Changes after the capture don't matter, since the capture is what is
    committed. Ordinary activity, which is mostly movement, never abandons
    a save.
  - **A Vicinity whose optimistic saves keep abandoning** past a deadline
    halts briefly, saves while frozen and resumes; the deadline bounds what a crash
    can lose (minutes, to tune).
- **Vicinities shrink by cleaving.** A Vicinity sheds the chunks behind a moving
  viewpoint by splitting them off, once that band is a few chunks thick
  (with hysteresis), into a new Vicinity with no viewpoint, which is frozen
  from the start: it saves and shuts down. No chunk leaves a running
  Vicinity on its own, and a Vicinity that loses its last viewpoint is saved and
  dissolved the same way, so the split path is exercised constantly.
- **When saves happen:** when a Vicinity is cleaved off or loses its last
  viewpoint; on pause (the World halts and every Vicinity saves while frozen;
  single-player only); optimistically, at quiet moments; at the deadline;
  and at shutdown.
- **The SQLite world store** holds:
  - the **chunk manifest:** chunk key → current chunk-blob version, its content
    version and its content digest (`data/content-digest`, checked when the
    chunk becomes an actor); no row means the chunk is at its generated
    baseline, so unedited chunks are never stored;
  - the **scarce table** (above);
  - the **world records:** portals, dimensions, world and player records
    (so a placed container's portal row commits with the Vicinity that
    placed it);
  - optionally, a **release list** of superseded chunk-blob versions.
  Chunk blobs are **immutable, write-once**, keyed by (chunk, version), in
  a chunk-blob store that needs no transactions of its own (LMDB, region
  files or
  plain files are equally correct). Only the commit has a single writer,
  the World's Registrar (`engine/world`).
- **Balancing the books, offline:** at startup (or as a tool), before the
  world loads, check that live rows plus equity equal what was minted, per
  kind, then fold old equity rows into per-kind and per-sink totals. Only
  then may an equity row go, since no delayed save can still be pending
  (`data/entity-ledger`).
- **Garbage collection, offline first:** at the same time, delete chunk
  blobs the
  manifest doesn't reference: a set difference over the chunk-blob store's
  keys. The release list,
  deleting superseded versions right after each commit, is the later
  refinement. (This is storage housekeeping, not one of the always-on
  runtime audits `data/cheap-invariants` rejects.)
- **Loading:** a chunk loads from its manifest chunk blob, or generates if it
  has none; when a chunk joins a Vicinity, the Vicinity adopts every
  scarce instance anchored there. Instances anchored to chunks in no
  Vicinity stay in the table, so after a reload a receiver may be adopted by a
  different Vicinity than its sender, or wait while its chunk is frozen.
- **Open:** whether scarce stacks are one row per stack (splits and merges
  then being transformations) or one row per item; what becomes of
  instances in limbo; whether an edit still queued at a save is persisted
  or dropped (dropping reads to the player like an ordinary rejected
  prediction); the deadline; how often cleaving halts can run before they
  show.

## Components on the model

### The Surveyor
**Specified in `design/surveyor.md`** (`world/surveyor`): its
role, contracts, records, viewpoints and perception, residency, generation
and loading work, the simulation budget, Vicinity membership, halts, draft
invariants and open questions. What the rest of this model relies on:
- **One per world,** inside the World; it alone decides residency (loaded, simulated, rendered, unloaded) from
  viewpoints, and asking never loads.
- **It holds the chunk records and is the only writer of the map** ("Chunk
  records, the map and nudges", above);
  records outlive content.
- **Its turn plans;** `publishViewpoints`, `find` and `nudge` are
  any-thread services, and the work it plans runs as goal actors it
  registers with the World's `GoalSource`, whose outcomes it reads in its
  turns or waits on through conditions (`world/surveyor`, `engine/dispenser`).
- **It is a component of the World, not an actor**: the World
  is the Dispenser source for work outside every Vicinity (generation,
  loading and meshing of chunks with no actor), and Vicinities offer their
  members.
- **It decides Vicinity membership:** joins while idle, Vicinities
  shrinking by cleaves, merge and cleave through halts with continuations.

### The Cartographer: chunk generation
**Moved to `design/cartographer.md`** (`worldgen/cartographer`):
its role, 13 invariants, batching and open questions. What the rest of this
model relies on:
- **A library called only from goal actors' and function actors' turns**
  (`invariant/cartographer/worker-turns-only`): not a Dispenser
  source or consumer. The Surveyor decides what to generate and registers
  goal actors with the World's `GoalSource` (`engine/goal-source`), and the
  completed goal actor holds the chunk data: the World keeps it as a
  terrain node, or sends it to a joining chunk's actor to install. Checked by the Dispenser's worker
  marker (`invariant/dispenser/worker-marker`).
- **Pure, and blind to the world** (`invariant/cartographer/pure-function`,
  `invariant/cartographer/never-reads-world`): no Surveyor, chunk, Vicinity or
  edited state; its build target can't see those headers. Saved edits are
  the chunk's to apply on load.
- **Never waits on our design** (`invariant/cartographer/never-waits`):
  neighbouring information is computed, never awaited, so generation can't
  deadlock; its cache's mutex is the one allowed lock and holds no work
  (compute outside, insert under).
- **Hands over ownership** (`invariant/cartographer/transfers-ownership`):
  the result belongs to the calling actor; the Cartographer keeps
  no handle into it.
- **Bounded, declared reach** (`invariant/cartographer/declared-reach`):
  `requiredKeys(request)` predicts the
  key regions a request touches; the World plans key release and
  prefetching from it, and a needed key outside it is fatal.
- **No minting and no cancellation**
  (`invariant/cartographer/no-conservation-effects`,
  `invariant/cartographer/no-cancellation`): placeholders are real blocks;
  an unwanted result is the Surveyor's concern.
- **Volume goal actors** (an option): one goal actor may generate a whole volume
  of distant chunks; it owns only its output, so nothing is claimed.

### Client and server share one chunk model
The tenet is `net/source-blind-client`: the client can't tell simulation
from replication, and doesn't need to.
- **One chunk type, two kinds of writer.** In single-player the client reads
  the **authoritative chunks** through the same thread-safe,
  seqlock-validated read API; in multiplayer it reads **replica chunks**,
  the same type, written by the client's replication turns from server
  deltas. Readers (meshing, raycasts, collision sweeps, visibility, the
  body's prediction) never know which. **No duplicated world in
  single-player.** Considered and set aside: Minecraft's integrated-server
  duplication (the client keeping its own copy fed over the in-process
  connection), at the cost of chunk storage for the overlap and
  replication work, but with no shared reads.
- **Meshing stays in the chunk's own turn** (keeping mutation and
  re-meshing coupled keeps incremental re-meshing simple and latency low):
  the turn that changes a chunk re-meshes it, with plain loads, knowing
  exactly which cells changed, **if the chunk is render-resident in this
  process**; a headless server never meshes. In single-player the
  authoritative chunk's turn meshes; on a multiplayer client, the replica's
  turn does. Considered and set aside: meshing as a separate client-side
  reader with validated reads (avoids server actors doing client work,
  but retries on busy chunks and loses the coupling).
- **Edits go to "the client's chunk" in both modes.** The client sends its
  edit as a message to the chunk it reads: in single-player that is the
  authority, which applies it within a frame; in multiplayer it is the
  replica, which applies it **tentatively as a reversible delta** (the
  client-side short rewinds of "Reading chunks", above) and meshes at once, then
  confirms or rolls back when the server's result arrives. No separate
  prediction overlay. Considered and set aside: a client-side overlay of
  predicted cell overrides consulted by every client reader.
- **The client-facing read API** is the seam for reads:
  - it exposes only what a multiplayer client could know (no unresolved
    ore sites or server-only metadata, in single-player either, so the two
    modes render the same); enforced at compile time by a read-only
    interface the client links against;
  - positions come in the client form, an integer chunk coordinate plus an
    f32 local offset (as on the wire, `net/replication`), converted from
    f64 at the server side, so the client never sees f64
    (`core/f32-origin-relative`, `net/client-server-seam`);
  - state is read as **timestamped samples**, so freshness (current in
    single-player, a round trip old in multiplayer) doesn't reveal the
    source either.
- **Reclamation still covers every reader:** client and server run in one
  process in single-player, so the quiescent-state scheme applies across
  both.
- **Open:** render residency (which chunks are render-resident, decided
  where, signalled how); what triggers meshing is settled (render priority
  classes, "Meshes and the GPU", below).

### Meshing and the simulation border
- **Meshing runs in the chunk's own turn** (chunks in a Vicinity.
  Full-detail chunk data the World holds is meshed by mesh goal actors, and
  distant terrain is drawn per `world/octave-pyramid`): at most one turn per chunk
  already means meshing and simulation never run on one chunk at once; the
  data is warm in cache. Meshing reads the chunk's own storage, which no
  other writer touches: **it runs without locks, validation or retries,
  using plain loads**, since no other turn can run on the
  chunk, new mail waits in the Vicinity's queue, other threads'
  validated reads write nothing, and structural changes happen only in this
  same turn. The cost is that the chunk's next turn (an edit queued for it)
  waits for the mesh. The chunk is the basic unit of concurrency throughout. Mesh only when visible content changed, possibly
  rate-limited, so a chunk changing every 20 Hz step isn't re-meshed every
  step. A finished mesh is handed to the frame thread for upload.
- **No hard simulation-distance boundary:** a **soft border**. Stepping probability falls off smoothly with
  distance from viewpoints (`core/soft-thresholds`), using the existing skip
  mechanism (`sim/subsystem-rate-degradation`): a distant chunk skips more
  steps. Closed forms (fire spread as a hazard, growth, decay) are exact
  however rarely checked, so only stepped processes such as fluids slow down
  toward the edge, gradually rather than cutting off. Rates come from the
  scheduler, never from simulation effects, so the chunk-loader exploit stays
  closed (`sim/residency-levels`). A Vicinity's frozen chunks never
  simulate, but they are far away.

### Meshes and the GPU (direction)
Rendering-side context: `render/gpu-driven` (resident
pooled meshes, GPU culling, indirect draws) and `engine/reference-hardware`.

- **Meshing runs in the chunk's own turn,** with plain loads (unchanged),
  **only for render-resident chunks; a headless server never meshes**
  ("Client and server share one chunk model"; in single-player
  the authoritative chunk meshes, on a multiplayer client the replica).
- **Who decides re-meshing:** **the chunk knows *whether*** it needs re-meshing
  (its mesh version is behind its content version); **how urgently** comes
  from outside it; **the Dispenser only orders** work by advertised
  priority, without knowing why (it holds no world state).
  - **A per-chunk render priority,** a small atomic scheduling hint on the
    chunk's record (not chunk content; soft, level-triggered, staleness
    harmless). Classes, highest first: **occupied** (a
    player is in the chunk), **visible** (drawn last frame), **soon
    visible** (likely to come into view, from viewpoint motion), **near**
    (in a player's vicinity), and **not render-resident** (zero). The
    **Surveyor** sets occupied, soon visible and near when viewpoints move;
    the **frame thread** raises chunks it drew to visible (its connectivity
    flood fill). A headless server never sets it.
  - **The combination, in the chunk,** at the end of a turn that changed it:
    occupied or visible → **mesh now, in the same turn** (keeping
    mutation and re-meshing coupled keeps incremental re-meshing simple and
    latency low); soon visible or near → mark the mesh stale and **advertise
    a meshing work item at that priority**, so consecutive changes coalesce
    into one re-mesh; not render-resident → mark stale only, and the work is
    advertised if the chunk later becomes render-resident.
  - **Rate caps by class:** a chunk changing every step (flowing water at
    20 Hz) needn't re-mesh every step; caps such as every change when
    occupied, about 10 Hz when visible, about 1 Hz when near; a change
    inside a cap's window becomes deferred meshing work. Values open.
  - **The frame thread's input is a timestamp, not a flag** (a hint, not a message): it writes `visibleUntil = frame + N`
    (a relaxed atomic store) for render items it drew (chunks, and boxes
    of distant terrain), refreshing every few frames
    (say every 8 for N = 16); the chunk counts as visible while the current
    frame is before it, so nothing is ever cleared (`core/soft-state`).
    **One writer per field:** the Surveyor's class and the frame thread's
    `visibleUntil` are separate fields, and the chunk takes the higher.
    Hint fields sit on a separate cache line from what the chunk's own turn
    writes. The rule "a chunk changes only in its own turn" concerns chunk
    content (cells, entities, metadata), not scheduling hints such as these
    or the nudge mask.
  - **Render residency is the Surveyor's decision**, as all
    residency is: a non-zero render priority means render-resident; LoD is
    the generation stage it requests. When a chunk leaves render range the
    Surveyor pushes a **retire descriptor** (render item id) into the same
    descriptor vector; the frame thread frees the pool range and record
    entry after the frame fence. Distant terrain is drawn per box from the
    World's chunk data (`world/octave-pyramid`), so its render items are
    keyed by box, not chunk, have no chunk record, and retire the same way.
  - **The mesher:** a process-wide mesher (a no-op `NoOpMesher` when
    headless), with per-chunk decisions made from the render
    priority, since render residency changes per chunk as players move.
  - **Staleness also gives backpressure:** deferred meshing is advertised
    work, driven by the mesh version lagging the content version, so a
    shortage of staging space or queue capacity simply stops it being
    advertised (no waiting). An in-turn mesh that finds no staging space
    falls back to marking the mesh stale and advertising it.
- **Working state is fixed-size and `thread_local`**: binary greedy
  meshing works on 64-bit column bitmasks (a 64×64 slice is 64 `uint64_t`,
  about 32 KB per face direction per chunk), read and written while culling
  faces and merging, binned per material. Its size is bounded by the
  chunk's dimensions, not the mesh, so it never grows. Safe because **a turn
  neither migrates between threads nor re-enters** (it runs to completion on
  one thread, never suspends, never nests).
- **Output is append-only, written straight into staging.** Each merged quad
  is written once, in order, never read back (per-vertex AO is computed from
  voxel data at emission), so write-combined memory is fine: no cached
  host-visible memory type is required, and there is no intermediate copy.
  Six streams by face direction, or one, as meshing runs direction-major.
- **Staging: one shared pool of small fixed-size blocks, one mesh per
  chain of blocks** (not sharing blocks between
  meshes makes it simple enough to get right; one pool shared by all
  workers has a lower footprint than per-worker sets). Blocks are host-visible, say 64 KB; a
  mesh fills as many as it needs (`vkCmdCopyBuffer` takes a list of
  regions), and **no block ever holds parts of two meshes**, so waste is at
  most one partly filled block per mesh in flight (a few MB at most, since
  staging is transient). **Staging is transient** (a mesh stays there a
  frame or two, until its GPU copy completes); long-lived meshes live in the
  device mesh pool, sub-allocated by range, so their age and arrival order
  don't matter (a mesh built 10 minutes ago may still be valid).
  - **A single-owner lifecycle per block:**
    1. **free,** in the shared pool;
    2. **filling,** taken by one worker and written sequentially by it (an
       abandoned mesh's blocks go back to the pool);
    3. **handed off,** referenced by a descriptor in the queue to the main
       thread (ownership travels with the descriptor);
    4. **in flight,** the frame thread has recorded the GPU copy and tagged
       the blocks with the frame's timeline value;
    5. **returned** to the pool once that value completes.
  - **Exactly one thread owns a block at each stage,** so the blocks
    themselves need no atomics. Debug builds keep a per-block state field
    and assert each transition.
  - **The pool is a mutex-guarded stack of free block indices**
    (`std::vector` plus `std::mutex`: standard C++ and standard
    sync primitives, not a custom lock-free structure; the critical section
    only moves indices, so contention is very rare). Its capacity is
    reserved for the cap, so it doesn't reallocate in steady state.
    **Workers** lock it and pop a few blocks at a time (a worker briefly
    waiting is fine). **The frame thread** returns all of a frame's released
    blocks in one batch with `try_lock`; if the lock is held, it keeps them
    and retries next frame, so it never waits, not even behind a preempted
    worker. Considered: a bounded lock-free queue of indices sized to the
    cap (exact, but custom).
  - **The frame thread alone creates blocks** (through VMA, mapped
    sequential-write memory: a batch at startup, more between frames when
    the pool runs low, up to a memory cap). **Workers never allocate GPU
    memory or call Vulkan.**
  - **Shortage:** when the pool is empty, every worker defers meshing alike
    (the chunk's mesh stays stale and stops being advertised); a full
    descriptor queue is handled the same way.
- **Delivery: a mutex-guarded descriptor vector to the frame thread**
  (no custom lock-free structures). A worker commits a
  mesh by locking it and `push_back`ing a descriptor (chunk id, content
  version, its block chain and lengths, bounds, face connectivity). Once per
  frame the frame thread `try_lock`s it and swaps the whole vector for an
  empty one it keeps (two vectors ping-pong, capacity reserved, so no
  reallocation in steady state); if the lock is held, it picks the
  descriptors up next frame. A cap on pending descriptors gives the same
  backpressure as before. **No render slot on or for the chunk**: chunks only know "send my mesh", and render
  residency stays separate from chunk residency. The frame thread's channel
  is not actor mail (the frame thread is not an actor the Dispenser
  schedules).
- **The frame thread installs meshes each frame, never waiting:**
  - takes the swapped-out descriptors and keeps only the newest version per chunk
    (latest wins); superseded or outdated descriptors' blocks join the
    frame's batch of blocks to return;
  - within a per-frame upload budget, allocates a range in the device mesh
    pool and records the copy; a descriptor that doesn't fit waits for the
    next frame;
  - keeps its own **render table**, keyed by render item (a chunk, or a
    box of distant terrain): pool range or texture, bounds, face
    connectivity, level. Visibility culling (a flood fill through open
    faces from the camera's chunk, as Minecraft's "advanced cave culling")
    runs on this table, not on chunk storage, so it works for boxes of
    distant terrain, which have no chunk records, and on a multiplayer
    client alike;
    GPU depth-pyramid occlusion is a later step.
  - returns blocks to the pool (one `try_lock` batch) after their copy's
    timeline value completes; frees an old pool range after the frames that
    might draw it.
- **Unloading:** when the Surveyor drops a chunk from render range, it tells
  the frame thread, which retires the chunk's pool range (by fence).
- **Every mesh crosses PCIe exactly once on a discrete GPU**:
  the default is staging in host-visible system RAM, pulled into VRAM by a
  GPU copy (DMA, no CPU cost). Considered for later
  measurement: staging blocks in the BAR window (the 1050M exposes 256 MB of
  host-visible VRAM; the output is append-only, so meshing straight into it
  works), at the cost of workers' stores crossing PCIe as they happen and
  spending a scarce window. Integrated GPUs (HD 530) have no PCIe hop.
  Bandwidth is no concern (several GB/s against tens of MB/s of new
  meshes); the per-frame upload budget is the real limit.
- **Upload synchronization: one graphics queue to start** (transfer-queue support is added only if needed). Copies are
  recorded at the start of the frame's command buffer, with a barrier from
  transfer writes to vertex, index and indirect reads, so new ranges are
  usable that same frame; the per-frame upload budget keeps this to a
  fraction of a millisecond. A new pool range is freshly allocated, so no
  frame in flight reads it (no write-after-read hazard); old ranges and
  staging blocks are released by timeline value (deferred release, below).
  - **If uploads are ever shown to compete with rendering:** a dedicated
    transfer queue (NVIDIA's copy engine) in its own submission signalling
    its own timeline semaphore; the frame thread polls it and lets a record
    into the record list only once its upload completed, so graphics
    submissions never wait on transfers (one late upload can't stall a
    frame); concurrent sharing between the two queue families to start.
- **The record list: the GPU's input for drawing**. One
  fixed-size entry (~32–48 bytes) per drawable chunk mesh: offset in the
  device mesh pool, quad counts per face direction, chunk coordinate
  relative to a camera-near origin, LoD, flags (such as the result of the
  connectivity flood fill). Each frame a culling compute shader reads it
  (one thread per entry: frustum, distance and LoD, visibility flag, face
  directions that can face the camera), appends indirect draw commands and
  a count, and one indirect-count draw per pass draws them. "Only completed
  uploads enter the list" is the whole synchronization rule for drawing.
  - **First implementation: rewrite it every frame**: the main
    thread writes the whole list into a host-visible buffer, one copy per
    frame in flight, so the GPU never reads a buffer being written. A few
    thousand chunks is about 100 KB; with distant LoD perhaps a megabyte.
    Its cost is O(drawable chunks) CPU work per frame, an accepted
    exception to `render/gpu-driven`'s O(changes) goal, to optimize if
    needed.
  - **Later, if needed: persistent records with deltas.** Device-local
    list indexed by a stable slot; entries are fixed-size, so the main
    thread alone keeps a free list of slots (a bitset with find-first-set
    hands out the lowest free slot, keeping the high-water mark low;
    no compaction, since fragmentation only costs empty entries culling
    skips); only changed slots are uploaded, scattered on the graphics
    queue at the start of the frame after a barrier ordering them behind
    the previous frame's culling reads, so a freed slot may be reused at
    once (only its old pool range waits for fences). Culling covers
    `[0, highWater)`. The render table's interface is the same either way.
- **What the frame thread still reads per frame:** published entity
  trajectories (as deltas, `engine/frame-thread`) and the body's published
  target and tool preview. It reads no chunk storage at
  all: targeting moved into the body's step.
- **Invariants:**
  - The frame thread's own invariants cover its side of the path: it alone
    calls Vulkan, allocates the mesh pool and creates staging blocks
    (`invariant/frame-thread/sole-vulkan-thread`), draws only completed
    uploads (`invariant/frame-thread/only-completed-uploads-drawn`), reads
    no chunk storage (`invariant/frame-thread/source-blind-view`) and never
    waits behind work (`invariant/frame-thread/never-waits-behind-work`).
  - **`invariant/concurrency/staging-block-one-owner`** — A staging block
    has exactly one owner at a time, and is reused only after the timeline
    value of its copy has completed, returning to the pool exactly once.
    - **Enforced:** debug audit: per-block state asserted on each
      transition (in flight → returned only once its timeline value has
      completed).
  - **`invariant/concurrency/mesh-never-older`** — An installed mesh is
    never older than the one it replaces.
    - **Enforced:** review: the descriptor's content version (latest
      wins).
  - **`invariant/concurrency/mesh-path-no-allocation`** — The mesh path
    makes no heap allocations in steady state.
    - **Enforced:** telemetry or watchdog: a release counter.
  - **`invariant/concurrency/mesh-path-never-waits`** — Workers never wait
    for staging or queue space, and never call Vulkan; a shortage only
    defers a mesh and reduces advertising.
    - **Enforced:** review: a full pool or queue marks the mesh stale
      instead of blocking.
  - **`invariant/concurrency/staging-bounded`** — Staging memory is
    bounded.
    - **Enforced:** review: the cap on blocks.
  - **`invariant/concurrency/record-buffer-not-shared`** — The GPU never
    reads a record buffer while the CPU writes it.
    - **Enforced:** review: one record buffer per frame in flight.
- **Checks to build in:**
  - `drawIndirectCount` is optional (core in Vulkan 1.2, not guaranteed):
    check at startup; fall back to fixed-count indirect draws where culled
    entries get `instanceCount = 0`.
  - The Khronos synchronization validation (in `VK_LAYER_KHRONOS_validation`)
    in debug runs, for missing barriers and write-after-read hazards
    (`deps/tooling`).
  - `VK_EXT_memory_budget` for sizing the pool from real video memory (2 GB
    on a 1050M).
  - **Vertex pulling:** quads are fetched from the pool by the vertex
    shader (one static index buffer for quads; the draw's base offset
    selects the mesh), standard for voxel renderers; detailed with the quad
    format.
- **Range management in the device mesh pool**. The GPU
  and driver don't manage placement inside a buffer: offsets are ours.
  - **Layers:** the driver allocates `VkDeviceMemory` (slow, count-limited:
    a few large blocks, never per mesh); the pool is a few large buffers
    (say 64–256 MB) created through VMA; ranges inside them are pure CPU
    bookkeeping by the frame thread, which owns each mesh's lifecycle.
  - **VMA's virtual allocator does the address bookkeeping:** one
    `VmaVirtualBlock` per pool buffer, the same size, tracking which offsets
    are used and free (TLSF, its default algorithm: O(1) allocate and free,
    good fragmentation behaviour). It never allocates or touches GPU
    memory; `vmaVirtualAllocate` returns an offset, the GPU copy writes the
    mesh there, the record list points drawing at it, and
    `vmaVirtualFree` marks it free after the fence. No internal locking,
    which suits a single owner (the frame thread). Staging blocks and pool
    buffers themselves are ordinary VMA allocations (sequential-write
    mapped memory for staging, device-preferred for the pool), so VMA also
    picks the memory types.
  - **Fragmentation is real** (mesh sizes from a few KB to a megabyte or
    more, constant churn from streaming and edits), so: keep slack (the
    pool, say, 25% larger than live meshes) and watch a health metric (the
    largest free range over total free, from
    `vmaCalculateVirtualBlockStatistics`).
  - **If the metric degrades: incremental compaction,** our code (VMA's
    defragmentation covers real allocations only): copy a few live ranges
    to new offsets on the GPU within the per-frame budget; since the record
    list is rewritten every frame, a moved mesh only needs its new offset
    next frame; the old range is freed by fence, as for a re-mesh.
  - **Fallback if compaction isn't enough: paging.** Meshes as lists of
    fixed-size pages (say 32–64 KB): no external fragmentation, at most one
    partial page per stream wasted, one indirect draw per page; pages pair
    with staging blocks. Changes only the renderer's internals and the
    record format.
  - Ours, not VMA's: lifetime and scheduling (deferred freeing by timeline
    value, staging block ownership, the descriptor queue, the upload
    budget).
- **Deferred release: the GPU reports progress counters, the CPU owns what
  they mean**. The GPU signals completion of whole
  submissions, never of individual resources. One **timeline semaphore per
  queue**: the CPU chooses each submission's signal value (strictly
  increasing, 64-bit, so no wraparound), the GPU writes it when the
  submission finishes; graphics values are frame numbers (transfer values
  upload batch numbers, if a transfer queue is ever added). Every release
  (an old pool range, staging blocks, a
  per-frame record buffer) is tagged with the (queue, value) of its last
  use and queued; each frame the frame thread reads each queue's completed
  value (`vkGetSemaphoreCounterValue`, non-blocking) and processes every
  release at or below it. Granularity is one submission, about a frame.
  This is the standard frame-indexed deferred-deletion pattern (older code
  uses a ring of per-frame fences, the same idea). **The only CPU wait** is
  frame pacing: with 2–3 frames in flight, before reusing a frame slot's
  resources (command buffers, its copy of the record list) the frame thread
  waits for the submission that last used that slot. Everything else
  polls.
- **Open:** block size; pool cap and growth; queue capacity; the per-frame
  upload budget; quad format and mesh size.

### The player's body is just another physics object
- **The body is an ordinary physical entity in the (parallel) physics
  simulation** (already the model in
  `sim/player-embodiment`: the player is a soul bound to a body, the third
  soul implementation beside canned souls and logic machines). The player
  soul only supplies intentions. So the question is not how the frame
  thread gets exclusive access to the current chunk, but **how the
  player's own actions show up without perceptible delay before the
  authoritative physics catches up.**
- **Latency is managed by prediction, not ownership** (from
  `net/client-server-seam`, open in detail):
  - **the client predicts its own body:** a small provisional simulation of
    the player's body against resident chunk versions, so a move or jump is
    visible on the next frame. The authoritative simulation steps the body
    like any other entity at its own rate; the client reconciles by taking
    the authoritative state and replaying unconfirmed inputs. Disagreement is
    rare for one's own body in static terrain;
  - **edits are predicted the same way:** shown at once with a sequence
    number, confirmed or rolled back (the "no ghost blocks" rule). This fits
    edit messages: the prediction shows immediately, the chunk's turn
    validates, its reply confirms;
  - **other entities are interpolated,** between authoritative states,
    slightly in the past;
  - **the same seam in single-player,** in memory rather than over a socket.
- **So the frame thread's job is input and presentation;** it never owns or
  blocks on world state, and it doesn't predict either:
  in single-player it evaluates the authoritative body's trajectory, and in
  multiplayer the client runs local physics in its own Vicinity
  implementation, merged continuously with the authority's
  (`engine/frame-thread`). Prediction runs against complete Vicinities,
  and the body waits at their edge.
- To settle: the prediction scope (own movement, own edits, tool previews;
  anything touching conserved items or other entities shown provisionally
  or waiting for the authority); the authoritative simulation's fixed rate
  versus the render frame rate (interpolation or extrapolation between
  steps); and where the body lives in the authoritative simulation (the
  entity scope above chunks: collision reads chunk versions through a
  Vicinity, chunk effects on it arrive as deltas).

### The lowest-latency path: standing on the ground, pressing jump
Simulation only, independent of rendering.
- **One pure stepping function, run on both sides** (the v0.1.7 `planEdit`
  pattern): `stepBody(BodyState, BodyInput, Vicinity, BaseTick) →
  BodyState`, with no hidden inputs and the fixed base tick as its step,
  never the frame delta.
- **The path:**
  1. **Input:** the frame thread polls the window once per frame; the
     intention layer turns the press into a `Jump` intention **tagged with the
     base tick it applies to** (the next one after the poll).
  2. **Local physics, at once** (the authority's body in single-player,
     or the client's Vicinity in multiplayer): on that base tick its turn calls
     `stepBody` against its complete Vicinity: the ground check finds
     support, the jump impulse is applied, the body leaves the ground.
     Microseconds. It publishes the predicted trajectory, which is what the
     rest of the client sees, and keeps a short ring buffer (about a
     second) of tick, input and predicted state.
  3. **To the authority:** "input for player P, tick n, sequence s", as a
     message (in memory in single-player, the same seam over a socket in
     multiplayer).
  4. **The authority steps the body** at tick n as a fast-tier turn with
     that tick's buffered inputs, and publishes the authoritative state for
     tick n. An input that arrives late is applied at the next tick and the
     client reconciles; it is never silently different.
  5. **Reconciliation:** when the authoritative state for tick n arrives, the
     client compares it with its predicted state for tick n. **Within a
     tolerance:** nothing to do; discard history up to n. **Beyond it:** take
     the authoritative state and **replay** the stored inputs n+1…now through
     `stepBody` (a few dozen microsecond calls), then **smooth** any visible
     correction over a frame or two.
- **Latency:** the jump exists in the prediction within one base tick of the
  poll that saw it (at most one frame waiting for the poll, overlapping the
  rounding to the base tick). The authority's latency affects feel only when
  the two disagree.
- **Replay versus overwrite**: overwriting the current
  state with tick n's rewinds the player by the latency's worth of movement
  (hardly visible over single-player's in-memory seam, a rubber-band at 100 ms
  network round trip); replay corrects only the error, and is cheap given a
  pure `stepBody`, tick-tagged inputs and states, and the ring buffer the
  tolerance check needs anyway. Delta correction (add the error at tick n,
  smoothed) is a middle option, fine for small continuous errors but wrong for
  discrete ones. Plan on replay; nothing outside the client's prediction code
  depends on the choice.
- **Client-side precision: f32 in a local frame; no f64 on the client**:
  - precision near the player is no issue: relative to a local origin a chunk
    or two away, f32 spacing is about 4×10⁻⁶ cells; physics never runs far
    from the viewer, so only a local solve is needed
    (`core/f32-origin-relative` forbids only absolute f32);
  - **bit-identical results between client and server are not required**: drift is many orders of magnitude below perception, the
    project already doesn't depend on cross-host floating-point identity, and
    reconciliation at every authoritative step (20–60 Hz) bounds even a rare
    discrete divergence (a body within a millionth of a cell of a ledge edge)
    to one step's worth of motion, which smoothing hides;
  - `stepBody` works in a local frame (origin a lattice point, such as the
    corner of the body's chunk at the start of the step); the server may run
    the same code in its own precision; no build discipline such as
    `-ffp-contract=off` is required for body physics;
  - **the wire carries the integer chunk coordinate plus a quantized local
    offset,** never f64 and never absolute f32;
  - authoritative storage stays f64 (`coords/f64-authoritative`); the local
    frame is how the step is computed and sent, not how positions are stored.
- Optional on top, as rules inside `stepBody`'s input handling: a short jump
  buffer and coyote time.
- **Which chunk state a step reads:** whatever the chunks hold when it
  runs, through validated reads of live storage ("Reading chunks", above);
  the client's local physics reads its replica, and reconciliation absorbs
  the difference.

## Open
- **Still to write up in this model:**
  - **reading chunks** (seqlocks, validated multi-chunk reads,
    representation swap with reclamation) moves into the chunk doc, which
    comes last and gathers reading chunks, representations, meshing and
    the record's content side (`world/chunk`);
  - **edits** are largely settled across docs (a thing the body does, sent
    by its Vicinity with expected old state and an applied or rejected
    reply; breaking as a process with a point of no return; the
    speculative remesh) but have no single home;
  - **the body:** much is settled in `sim/motion-segments` and
    `engine/frame-thread`; still open are the entity scope, control
    accelerations per situation and the input window's length;
  - **the LoD flood fill:** a World-owned sub-actor that lags; not started.
- **Open in the entity docs:**
  - `world/surveyor`: its and the World's invariants, how membership is
    computed, the budget into priorities and fairness, the perception
    index's granularity;
  - `sim/vicinity`: the assembly transaction API, mobs moving between
    chunk and Vicinity ownership, which kinds publish physics proxies, heavy
    scenes, the turn budget;
  - `engine/dispenser`: the priority scale and ageing, batch naming, the
    latency-critical class, cross-world fairness, the full set of System's
    intentions;
  - `engine/frame-thread`: the UI and inventory design pass, the
    extrapolation clamp, frame pacing, the scope of a client's local
    physics;
  - `engine/network-thread`: its library, and whether local and remote
    clients share one abstraction.
- **Measurements:** per-chunk meshing time, retry rates, halt duration,
  and the numeric budgets (block size, pool cap, upload budget, quad
  format, rate caps).
- **v0.2's point releases are ordered against this model:** octree terrain
  first, then goals and concurrency, then chunk actors (`versions.md`).
  The model is refined as releases implement it and their results come
  in.
