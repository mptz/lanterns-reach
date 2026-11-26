# engine/world (draft) — The World actor: one world's coordinator, and its Registrar

The World is the actor for one loaded world. It holds the world's global
records, plans what of the world is loaded, simulated and rendered (through
its Surveyor component), is the parent of the world's Vicinities and its
Registrar, and is the Dispenser source for the world's work outside every
Vicinity. It is a child of System, which loads, pauses, resumes and
shuts it down.

`design/concurrency.md` keeps the assumptions, invariants and connective
tissue the World shares with the rest of the model (the actor hierarchy,
persistence); this file holds the World's own role, its Registrar, and open
questions. Its residency and membership planning is specified in
`world/surveyor`. (World *structure*, chunks, scale and the like, is
`design/world.md`.)

**World** and **Registrar** are the C++ class names.

## Architectural role
- **One per loaded world,** a child of System in the actor hierarchy
  (System → World → Vicinity and Registrar, `sim/concurrency-model`).
  Single-player has two: the in-process server's world and the client's
  world; a server may host several (a head-to-head race,
  `loop/speedrunning`).
- **Created by System** in the turn that handles a load intention; it then
  asks its Registrar for the world's records and manifest, and its
  Surveyor begins planning from the viewpoints that arrive.
- **A Dispenser source** for its own turns and for work outside every
  Vicinity: generation and refinement, loading, and meshing of chunks with
  no actor, which are World state, not objects (`world/chunk`).
  Generation, refinement and meshing are goal actors it owns and registers
  with its `GoalSource` (`engine/goal-source`), so any number run at once;
  TerrainNodes' sibling sets are among them. Chunks in a Vicinity have their turns offered by it
  (`sim/vicinity`).
- **The parent of its Vicinities and its Registrar:** it halts them, with
  continuations, and they never call or message it about halting.

## What its turn does
- **Planning, through its Surveyor component** (`world/surveyor`):
  viewpoints into residency, generation and loading work, the simulation
  budget's allocation, Vicinity membership, the goal actors it registers with
  and withdraws from its
  `GoalSource` (`engine/goal-source`), and the published perception index.
- **Global records:** the portal and interdimensional reference table
  (`data/dimension-gc`), dimension records, per-region state (a defeated
  boss), world metadata and player records. They change rarely and never on
  the hot path; the Registrar persists them with each save.
- **Published global state and logs:** the player roster (who is bound to
  this World), world time, weather and the paused flag are versioned
  published state that every client's interest includes; chat and join or
  leave announcements are an append-only, sequence-numbered log the World
  appends to in its turn (server-wide chat, if it ever exists, would be System's). They change only
  by server-mediated actions, like physics, and the server wins
  (`engine/network-thread`).
- **Saves:** a Vicinity's save (`sim/concurrency-model`) sends its
  capture of scarce rows, with the versions of the chunk blobs it wrote, to the
  Registrar to be committed.

## The Surveyor is a component, not an actor
- The Surveyor is a class inside the World: its planning and membership
  state is turn-only, reached through the World's turn passkey
  (`ActorOwned<…, WorldTurn>`), and its any-thread services (the map,
  `find`, `nudge`, `publishViewpoints`) are the World's. A library with
  state, not an actor (`world/surveyor`).
- **Why not a separate actor:** the World's other work is thin, a separate
  layer cost a step in every halt, Vicinities can span dimensions through
  portals (so membership is decided per world anyway), and the one thing
  that must not stall planning, storage I/O, has its own actor, the
  Registrar. More actors can be split out later if a turn grows too long
  (the LoD flood fill is already a World-owned sub-actor).

## The Registrar: the world's persistence actor
- **One per World, a child of it.** It owns the world's store: the SQLite
  database (the chunk manifest, the scarce table, the world records) and
  the immutable chunk-blob store (`sim/concurrency-model`'s persistence,
  `deps/save-store`). Named for a land registry: the manifest records each
  parcel's current version, and each committed save is a registration.
- **It commits; it doesn't write chunk blobs.** Chunk blobs are immutable
  and write-once, and an unreferenced chunk blob is harmless (the offline
  collector removes it), so writing them needs no single writer: each save's
  changed chunks are written as new chunk blobs in parallel, by chunk-blob
  write goal
  actors (I/O items by duty cycle, `engine/dispenser`) that the halted
  Vicinity builds, each owning its chunk's captured data, and sends to the
  Registrar, which registers them with the World's `GoalSource` together
  with a commit goal actor depending on all of them, then withdraws and
  frees them (`engine/goal-source`). The Registrar does
  only what makes a write count: one SQLite transaction switching the
  manifest to the new chunk-blob versions and applying the scarce rows and world
  records, each only if newer (several Vicinities' saves may share one
  transaction and one `fsync`, in any order), plus loads (manifest queries
  and the scarce instances anchored at a chunk, in its turns;
  chunk-blob reads are goal actors, whose chunk data the World keeps or a
  joining
  chunk's actor receives)
  and the offline collection of unreferenced chunk blobs at startup. A clean
  full-detail terrain node whose version matches the manifest needs no load
  at all (`world/chunk`).
- **Its turns are I/O items** (`engine/dispenser`'s duty cycle). Being its
  own actor keeps commits from stalling the World's planning or any
  Vicinity's simulation, and makes it the one writer of the database, as
  SQLite wants.
- **Why chunks and Vicinities don't commit themselves:** a chunk is the
  wrong scope (conserved objects cross chunk borders, so per-chunk commits
  reopen duplication routes 3 and 4 in `sim/concurrency-model`); a
  Vicinity is the right scope for capturing a save, but committing in its
  own turn would stall its simulation for the I/O, two Vicinities would
  queue on SQLite's write lock (a wait on our own design), and world
  records such as a placed container's portal row must commit with the
  save that placed it. So Vicinities capture saves; one writer commits
  them.
- **Requests arrive as hard messages** (a save's chunk-blob write goal actors
  and its captured scarce rows, load these records), and results come back
  as mail to the World or the requesting Vicinity.
- **Its halt quiets pending commits:** as a child of the World, it is
  halted with the World's other children, so nothing is half-committed when
  the World halts.

## Halts, pause and shutdown
- **The World orders its children's halts itself.** When System halts the
  World, the World gets a halting turn first and halts its children in the
  order it needs, each with a continuation:
  1. it halts its Vicinities; in the continuation, each saves while frozen and
     its capture goes to the Registrar, with the World's own records;
  2. it halts the Registrar; once the Registrar is quiet, every commit has
     landed;
  3. its last continuation completes its halt, and System's continuation
     can run.
  So the Registrar keeps running while the final saves are committed, which
  a fixed bottom-up order (all children at once) would not allow
  (`engine/dispenser`).
- **Pause** is the same halt without deregistration; **resume** releases
  halts top-down, and the Surveyor replans from the current viewpoints.
- **Shutdown** adds deregistration of each child once it is quiet.

## Invariants (draft)
- **`invariant/registrar/newer-rows-only`** — The Registrar applies a
  scarce row only if its version is newer than the stored one, and a
  manifest entry only if its content version is newer; nothing else
  commits to the world store.
  - **Enforced:** review: the commit's SQL compares versions in the
    statement itself (an upsert conditioned on the version); test: delayed
    and reordered commits from two Vicinities.
  - **Why:** commits from different Vicinities, and a delayed save from an
    earlier owner, then need no order (`invariant/concurrency/versions-rise`).

## The client's World
- It plays the same role without authority, merging or persistence: its
  Surveyor decides what the client requests and renders, its Vicinity runs
  the player's local physics in multiplayer (`engine/frame-thread`), and
  the client's Listener (`engine/audio`) sits under it. It has no
  Registrar.

## Open
- **Persistence, revisited:** per-Vicinity saves into a UUID-keyed scarce
  table with versioned rows (`sim/concurrency-model`) removed the need for
  halted snapshots; still to settle there: scarce stacks, limbo, the deadline.
  The halt ordering above remains for pause and shutdown.
- **Per-dimension work:** whether a Dimension actor is needed (pocket decay),
  and as a child of the World if so.
- **Logged-out players:** where a logged-out player's body and inventory
  live (`data/entity-ledger`), likely the World's player records.
- **How the World's records and the Registrar's transactions are batched**
  when several Vicinities save at once.
- **Its invariants,** to draft with the Surveyor's (`world/surveyor`).
