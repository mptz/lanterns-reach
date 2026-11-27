# Glossary

Glossary of engine and architecture terms; definitions live in the home entries.

Each entry gives the meaning in a sentence or two and the slug of the entry
that defines it in full.

## Terms

**actor** — An owner of state that runs one turn at a time, so its state
needs no locking; everything that runs on a worker is an actor's turn.
`sim/concurrency-model`

**appearance id** — A process-wide number for one distinct face appearance,
separate from block states, so states that look alike can share faces and
merge in meshing. `render/block-textures`

**batch** — What the Dispenser hands a worker: one or more work items from
one pull source, run in order. `engine/dispenser`

**bin (of octaves)** — The octaves of a field that one level of the terrain
tree adds on top of its parent's. `world/octave-pyramid`

**borrow and owner** — Every shared object has one owner, which alone can
change it, unlink it or hand it on; anyone else gets only a borrow, valid
within the current turn. `sim/concurrency-model`

**Brick** — The engine's one container of cells: a 64³ block of cells at a
power-of-two cell size, holding either one uniform value or indices into a
palette. `world/brick`

**Cartographer** — The world-generation service: given a region and a level
of detail, it produces that region's content from the seed (a terrain
node's: cells at full detail), without reading the world
or keeping state between requests. `worldgen/cartographer`

**cascade** — In GoalSource, the abandonment that spreads from a goal actor
to every dependent not yet started when it is abandoned or withdrawn.
`engine/goal-source`

**chunk** — A 64³ cube of one dimension, the unit of simulation, storage and
replication. `world/chunk`

**chunk actor** (`Chunk`) — A chunk's form while it is a member of a
Vicinity, which can simulate. It owns the chunk's Brick and
last-materialized time; outside every Vicinity a full-detail terrain node
owns them instead. `world/chunk`

**chunk blob** — The immutable, write-once saved form of one chunk's
plentiful content at one version, which becomes current only when a commit
names it in the manifest. `sim/concurrency-model`

**cleanup actor** — The actor a `retire` call hands objects to; it frees them
once no thread can still be reading them. `engine/dispenser`

**cleave** — Dividing one Vicinity, at a halt, into one or more running
Vicinities and at least one frozen one, which saves and shuts down; used
both to shed the chunks behind a receding viewpoint and to separate
players who walk apart. The inverse of a merge. `sim/vicinity`

**composed field** — A terrain-tree node's stored value of a field: its
parent's composed field carried down to the node's grid, plus the node's own
bin of octaves. `world/octave-pyramid`

**conserved transfer** — A move of a conserved thing between owners that can
neither lose nor duplicate it; the receiver always takes custody and
resolves it. `sim/concurrency-model`

**continuation** — The later turn of a parent that runs once the children it
halted are quiet; a parent observes its children only this way.
`engine/dispenser`

**dependency** — A goal actor whose completed data another goal actor's turn
reads; that turn runs only once all its dependencies have completed.
`engine/goal-source`

**dependency pointer** — The read-only link a dependent holds to one of its
dependencies, usable only inside the dependent's own turn.
`engine/goal-source`

**dependent** — A goal actor, seen from one of its dependencies.
`engine/goal-source`

**dissolve** — What a chunk actor does when it leaves its Vicinity: its
full-detail data becomes a terrain node again and the actor itself is
retired. A Vicinity dissolving (its last viewpoint gone) dissolves its
chunk actors. `world/chunk`

**Dispenser** — The process's single worker pool and actor scheduler, which
takes all its work from pull sources and holds no world state.
`engine/dispenser`

**equity** — The ledger's location for consumed instances: consuming moves
an instance to equity rather than deleting it. `data/entity-ledger`

**frame thread** — The thread, outside the actor model, that turns input into
intentions and draws frames from published state, never waiting on
simulation. `engine/frame-thread`

**frozen and running** — A running chunk or Vicinity simulates; a frozen
one is readable but never written, though a frozen Vicinity still runs its
own turns to save and shut down. Both differ from halted, when nothing
is dispatched at all. `sim/vicinity`

**function actor** — An actor that exists for one turn, for work that
neither has dependencies nor is one: its inputs are set when it is made, and
its results are read afterwards. Work with dependencies is a goal actor.
`engine/dispenser`

**ghost** — A translucent preview of the block or edit a tool would make,
shown before the action and, held with the orientation modifier, turned
before placing. Presentation only. `input/preview-mode`

**goal actor** — An actor with one scheduled turn that computes its own data
from its inputs and its dependencies' data; its client owns it and
GoalSource sequences it. `engine/goal-source`

**GoalSource** — The Dispenser source for work with dependencies, one per
World: it decides when each goal actor may run and never owns one.
`engine/goal-source`

**grace period** — The wait between a `retire` call and the cleanup it
registers: the cleanup runs only once every online thread has passed a
quiescent point since the `retire`. `engine/dispenser`

**halt** — A parent's request, in its own turn, that stops the Dispenser from
dispatching work to some of its children, together with the continuation to
run once they are quiet. `engine/dispenser`

**halt token** — What a halt returns to the parent: it alone can resume the
child, and while held it lets the parent act on the halted child's state.
`engine/dispenser`

**hard message** — An exactly-once command between owners, such as a
conserved transfer or an edit; hard messages are queued in a Vicinity, never
in a chunk. `sim/concurrency-model`

**hard simulation** — The small, local, conserved part of the simulation,
modelled formally before it is built; everything else is soft simulation.
`sim/exact-core`

**hidden simulation state** — Per-cell data a chunk keeps only to simulate
efficiently (a schedule of next events, scent); private to its turns,
never persisted, digested or replicated, and never authoritative.
`sim/block-metadata`

**intention** — What a soul wants its body to do (move, use, interact),
rather than a raw input; the body acts on it under its own physics.
`sim/player-embodiment`

**invariant** — A property that must always hold, named by an
`invariant/<entity>/<name>` id and stated with how it is enforced, its
release cost and why it matters. `DESIGN.md` ("How this is organized")

**key region** — The area of the world one seed key covers, in one release
tier. `worldgen/seed-keys`

**ledger** — The conservation accounting for scarce instances: each is
minted and sunk explicitly and is in exactly one location at a time, limbo
holding any that loading can't reconcile. `data/entity-ledger`

**lockstep mode** — A configuration in which one worker alternates strictly
with the frame thread, for reproducible runs. `engine/dispenser`

**merge** — Uniting two Vicinities that would touch into one, at a halt
of both: one absorbs the other (`absorb`), and only the absorbed members'
actors move. The inverse of a cleave. `sim/vicinity`

**mosaic** — The set of terrain nodes currently drawn, covering the view
exactly once, fine near the viewer and coarse far away; refinement replaces
a node in the mosaic with its eight children. Desired residency is a mosaic
plus its nodes' ancestors. `world/surveyor`

**mover** — An object in motion, owned and simulated by its Vicinity until it
comes to rest in a chunk and becomes settled, part of the chunk's own state.
`sim/concurrency-model`

**nudge** — A content-free wake-up telling an owner to look again, perhaps
with a hint of where; losing or repeating one is harmless.
`core/level-triggered`

**Observer** — An in-world object that keeps part of the world simulated
without a player present, with a diegetic cost; it is a viewpoint that draws
no frames. `sim/residency-levels`

**octave pyramid** — Terrain as a stack of levels, each adding a band of
noise octaves to the one above, so the same structure serves generation,
storage, rendering and replication at every level of detail.
`world/octave-pyramid`

**palette** — A Brick's table of the distinct values its cells use, which the
cells index. `world/brick`

**passkey** — A token type that only the Dispenser can create, given to a
turn, so that only code running in a turn can reach turn-only state and
APIs. `engine/dispenser`

**perception index** — A published index from regions of the world to the
viewpoints that perceive them, used to route events such as sounds.
`world/surveyor`

**physics proxy** — A soft, read-only projection of a mover's bounds and
collision-relevant state into the chunks it overlaps, published by its
owner with a TTL, so chunks can react to movers they don't own (a pressure
plate, a pickup). Opt-in per object kind. `sim/vicinity`

**plentiful** — Everything the simulation holds that isn't scarce: terrain
shape, ordinary blocks, soft simulation state. It is saved in each chunk's
chunk blob, not tracked by the ledger, and a crash or bug may duplicate or
lose some of it, which is accepted. `sim/concurrency-model`

**pull source** — An object the Dispenser pulls work from (implementing
`DispenserSource`): a Vicinity for its own and its chunks' turns, a World's
GoalSource, the World for work outside every Vicinity. Every actor's turns
come through exactly one pull source at a time. Not a fluid source block or
a light source. `engine/dispenser`

**quiescent point** — A moment when a thread holds no borrowed pointers, such
as the end of a turn or frame; grace periods are counted in them.
`engine/dispenser`

**quiet** — A halted subtree with no work in flight, which is when its
parent's continuation may run. `engine/dispenser`

**refinement** — Replacing a terrain-tree node with its eight children at the
next level of detail, generated from the parent. `world/surveyor`

**Registrar** — The World's persistence actor and its single committer to
the save. `engine/world`

**release tier** — A band of noise octaves (near, mid, far) whose seed keys
are released together, at a distance suited to that band.
`worldgen/seed-keys`

**residency** — What of the world is held where and at what level of detail:
unloaded, a terrain node the World holds, or a frozen or running chunk
actor.
`sim/residency-levels`

**retire** — To end an object's use now and its life later: the caller has
unlinked it so nothing new can reach it, readers that reached it before
stay safe until they finish, and then it is freed. How long that takes,
and by what mechanism, is not the caller's concern. Render items are
retired the same way, with the GPU as the reader. `engine/dispenser`

**root block** — The block a tree grows from, flush with the ground; its
few states carry worldgen's choices for that tree, and its coordinates the
rest of the tree's variety. `worldgen/tree-generation`

**scarce** — Conserved and tracked by the ledger: items, mobs, pets,
vehicles and valuable blocks once minted. Each scarce instance has a ULID
and its own row in the scarce table, so it can be rolled back but never
duplicated or lost; everything else is plentiful. `sim/concurrency-model`

**scarce table** — The saved table holding one versioned row per scarce
instance, keyed by its ULID. `data/entity-ledger`

**seed key** — A key derived one way from the world's seed that unlocks
generation of one key region in one release tier. `worldgen/seed-keys`

**sibling set** (`SiblingSet`) — The eight children of a terrain node, made
by shape refinement and released together as one goal actor; later stages
are per node. `world/surveyor`

**soft simulation** — Everything outside the hard simulation: simulation
that publishes state and reacts at its own checks, sending no messages. It
converges if a signal is lost or repeated, but it is not exact, and we
don't try to make it so: it needn't be deterministic, and duplication or
inconsistency there is accepted when it is cheap and self-repairing, since
nothing it holds is scarce. `sim/exact-core`

**soul** — The decision-maker bound to a body (a player, a creature's AI, a
logic machine), which senses through the body and sends it intentions.
`sim/player-embodiment`


**state hash** — A block state's durable identity: a 128-bit content hash
(BLAKE3) of its canonical text, its definition's ULID and its named
property values with defaults left out, written like a ULID.
This, not the state id, is what is saved. It is not a ULID: it carries no
timestamp and is derived, never minted. `world/brick`, `data/stable-identity`

**state id** (`StateId`) — A block state's compact 32-bit number within one
process, used in palettes and resolved ahead of time, never saved.
`world/brick`

**state table** — The process-wide table of block states and what systems
need to know about each, indexed by state id. `world/brick`

**superclass** — A placeholder block that coarse terrain holds where the
stage ladder hasn't yet chosen a specific block (generic stone, a soil, a
canopy): a block definition like any other, which refinement replaces by a
member before the terrain is playable, and which is never obtainable.
`world/progressive-lod`

**Surveyor** — The World's planning component: it decides what of the world
should be resident where, at what detail, and issues the work to get
there. `world/surveyor`

**System** — The root actor of the Dispenser's hierarchy, one per process;
from outside, worlds are loaded, paused and shut down through it.
`engine/dispenser`

**Terrain** — The World's sparse octree of everything outside every
Vicinity, from 2¹⁷-unit roots down to chunk-sized leaves. `world/surveyor`

**terrain node** (`TerrainNode`) — One node of the terrain: a region held
at its level of detail, with no actor of its own; at full detail it owns
one chunk's Brick and last-materialized time at rest. `world/surveyor`

**time block** — A span of world time in `sim/random-ticks`'s hierarchy,
with its own key derived from its parent's; clients hold only current and
next blocks' keys, so they can't predict events beyond them.
`sim/random-ticks`

**turn** — One uninterrupted, single-threaded run of one actor.
`engine/dispenser`

**ULID** — A 128-bit identifier minted once, for an authored definition or a
scarce instance: a 48-bit millisecond timestamp then 80 random bits, written
as 26 characters of Crockford base32 (the first is 0–7). Every durable id in
the game, ULID or state hash, uses that 128-bit form and text.
`data/stable-identity`

**unit** — The edge length of one full-detail cell, and the engine's unit of
length: world coordinates, sizes and speeds are in units, and a cell at a
coarse level spans 2ᵏ units. How large a unit is in the game world is
`world/voxel-scale`'s choice (about a foot); engine docs speak in units.
`world/voxel-scale`

**Vicinity** — A set of chunks simulated together as one single-threaded
actor; Vicinities don't overlap, and they merge and cleave as viewpoints move.
`sim/vicinity`

**viewpoint** — A body's senses projected for scheduling: where it is, where
it faces and how far each sense reaches. Viewpoints alone decide what is
resident. `world/surveyor`

**withdraw** — A client's statement to GoalSource that a goal actor is no
longer wanted, after which nothing that depends on it will start; it must
come before the goal actor is freed. `engine/goal-source`

**World** — The actor for one loaded world, holding its records and planning
through its Surveyor. `engine/world`

**work item** — One unit of work a pull source hands out, run as one actor's
turn. `engine/dispenser`

## Collisions

- **cascade:** GoalSource's abandonment cascade, versus the Surveyor's joining
  cascade (the fixed order of generation a player waits for on joining).
  Prefer "abandonment cascade" and "joining cascade".
- **client:** a GoalSource client (the actor that owns and registers goal
  actors), versus a game client (a player's process or a remote connection).
  Prefer "GoalSource client" and "game client" or "remote client".
- **epoch:** the Dispenser's wake-up counter, versus a random-tick epoch (a
  fixed span of ticks), versus the superseded snapshot epoch of saves, versus
  the reclamation epoch that grace periods replaced. Prefer "wake counter",
  "random-tick epoch" and "grace period".
- **join:** a player joining a world (after the joining cascade), versus a
  chunk joining a Vicinity as a member. Prefer "player join" and "chunk joins".
- **key:** a seed key (worldgen), versus a chunk key (a chunk's name in the
  manifest), versus a passkey (a turn's capability). Always qualify.
- **manifest:** the chunk manifest (the save's table of current chunk blobs),
  versus a content-bundle manifest (a hashed list of a bundle's contents).
  Prefer "chunk manifest" and "bundle manifest".
- **node:** an terrain node (a terrain node), versus a node of the Dispenser's
  halt hierarchy (System, World, Vicinity, Registrar), versus a waiter node in
  a condition's list. Prefer "TerrainNode", "hierarchy node" and "waiter".

## Former names

- job, task: now turn, or work item for a unit a pull source hands out.
- the exact core: now hard simulation.
- `elevation` (worldgen field): now `surface`.
- the periphery: now soft simulation.
- chunk mailbox, inbox: now the Vicinity's hard-message queue for that chunk.
- per-pair outboxes (two-phase step/reconcile): now Vicinity-owned movers and
  hard messages.
- reclamation epoch: now grace period.
- `afterGracePeriod`: now `retire`.
- terrain box: now terrain node.
- `OctreeNode`: now `TerrainNode`.
- a chunk actor retiring (leaving its Vicinity): now dissolving.
- built-in ready queue: now pull sources.
- blob (unqualified): now chunk blob; blob store: now chunk-blob store.
- partition, split (of a Vicinity): now cleave; `partition(spec, token)`:
  now `cleave(spec, token)`.
- the cut (terrain nodes drawn): now the mosaic; consistent (save) cut: now
  consistent snapshot.
- ghost (a mover's projection into chunks): now physics proxy. "Ghost" is
  the tool preview only.
