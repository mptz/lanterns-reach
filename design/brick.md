# world/brick — The Brick: 64³ cells at a power-of-two cell size

A **Brick** is the engine's one cell storage: 64³ cells, each 2ᵏ units on
a side, in a palettized form whose palette entries are block states, at
every level of detail. A chunk's cells are a full-detail (1-unit) Brick; distant
terrain may hold its cells as coarser Bricks.

**The Brick is storage; its owner is the abstraction boundary** (owner,
2026-11-27). A Brick's owner is a chunk, for simulated terrain up close, or
a terrain node, for distant terrain (`world/surveyor`), which may also
store its cells some other way. The owner's code works with the Brick
directly, indices, widths and palette included, and nothing outside the
owner sees any of them: callers ask the chunk or the terrain node semantic
questions (`core/opaque-chunk-queries`). The Brick itself keeps only its
own class invariants (below).

`design/world.md` holds the entries that use Bricks
(`world/chunk-representation`, `world/chunk-meshing`, `world/cubic-chunks`)
and the octree terrain around them (`world/octave-pyramid`);
`design/chunk.md` holds the chunk and its actor. This file holds the
Brick's own role, structure, invariants and open questions.

**Brick** is the C++ class name; the in-game brick block shares the word,
as other engine terms share theirs.

## Role
- **The one cell storage,** at any power-of-two cell size: 1 unit at full
  detail, coarser for distant terrain.
- **A chunk's content is a full-detail Brick** plus its last-materialized
  time, owned by the chunk actor (`Chunk`) in a Vicinity or by a full-detail
  terrain node outside one, with no chunk-data type between owner and Brick
  (`invariant/chunk/content-is-cells-and-time`; there is no per-block
  metadata, and scarce instances are referenced from cells,
  `sim/block-metadata`). Bookkeeping both owners share is factored out
  beside the Brick only when it grows enough to ask for it.
- **The same shape as a TerrainNode's grid** (N = 64,
  `world/octave-pyramid`), so a node at spacing s can hold its cells as a
  Brick at cell size s; where coarse Bricks are used is part of the
  coarse-to-fine design, not yet settled.

## Representations
- **Uniform and palette.** Uniform: one entry fills the Brick (all air, all
  stone). Palette: bit-packed indices into a per-Brick palette of state ids
  (`Palette entries`). A Brick holds at most 2¹⁸ = 64³ distinct live
  states, so at 32 bits every cell can have its own entry, and the palette
  also covers what a separate dense form would, in one structure and one
  set of code paths. 2¹⁸ is not a limit on the palette: at 32 bits it may
  grow past 2¹⁸ entries, dead ones included, until the next compaction. A
  wider index costs nothing until a Brick needs it; the full-width case
  (every cell distinct, only a pathological player build) costs about twice
  a plain array of state ids (indices 1 MiB and palette 1 MiB, against
  1 MiB), plus the owner's reverse table.
- **Aligned, power-of-two storage widths:** indices are stored at 1, 2, 4,
  8, 16 or 32 bits, so an index never straddles a 64-bit word and an access
  is a shift and a mask, with one instantiation per width. The rare wide
  case spends a little memory to keep every accessor simple.
- **Layout:** cell index = x + 64·y + 64²·z (z up), so a row of 64 cells
  along x is 64 consecutive indices, and neighbours along y and z are 64 and
  64² indices away. This is the Brick's contract; how meshing uses it is
  `world/chunk-meshing`'s. It is chosen for bitmask meshing, where a row
  along x becomes one 64-bit mask; other layouts (Morton order, or small
  tiles for 3D locality in neighbour queries and raycasts) would break
  that, so a change comes only if profiling of other readers asks for it.
  Nothing persisted depends on it (`data/content-digest` is defined on
  coordinates).
- **A Brick is never resized in place:** its index width and its palette's
  physical capacity are fixed when it is built (the capacity is the
  palette array's allocation, independent of the width, which only limits
  how many entries the indices can name), and nothing a reader can reach is
  ever reallocated: the palette is one array allocated at its capacity,
  never a growing vector. A Brick is an internal representation, used only by
  its owner (a chunk actor, or a terrain node), which is the only writer
  and, behind its API, decides how readers reach it.
- **Growth copies and swaps:** palettes keep no per-entry counts, so an
  edit can't tell which entries are dead. An edit that needs a new entry
  appends it within the palette's capacity, storing the entry before any
  index that refers to it. A full palette is replaced by a new Brick with
  twice the capacity, at the same width while the capacity is below what
  the width can name (2ʷ entries), else at the next width; it copies into
  it, swaps the owner's pointer, and retires the old Brick (`retire`,
  `engine/dispenser`), which stays readable and is never written again,
  in the owner's own turn and outside any write
  window, which holds only the pointer store (`sim/concurrency-model`'s
  representation swapping). A Brick widens at most five times
  (1 → 32 bits) and doubles its capacity at most 31 times, each one copy,
  so growth can't thrash. A Brick is built with room for its entries: the
  next power of two at or above them, at least 16 (a placeholder), at most
  what its width can name, so the 1-, 2- and 4-bit widths hold 2, 4 and 16
  entries and never double below their limit. **No edit ever
  compacts.** Edits only ever upgrade a Brick (uniform to palette,
  narrower to wider).
  - **Widening is proactive, at a high-water mark:** once the palette
    passes 15/16 of its capacity (a placeholder), the owner schedules the
    widening for a later turn of its own, and edits keep appending into
    the headroom meanwhile, so the copy is off the edit's path.
  - **Fallback:** an edit that finds the palette full anyway (a burst
    within one turn: a paste, a fill, created blocks) widens in its own
    turn, as before; correct, only slower for that turn. Telemetry counts
    fallback widenings, which tune the high-water mark.
- **Compaction is the same copy, renumbering:** it finds the live entries,
  renumbers them, and picks uniform if one state is live, else the
  narrowest width at which the live entries sit at or below a **low-water
  mark** of 7/8 of capacity (a placeholder), building a new Brick and
  swapping it in like growth. The gap between the low-water and high-water
  marks is the hysteresis: a just-compacted Brick fills at least 1/16 of
  its capacity before it widens again. A Brick becomes uniform or narrower
  only this way, never through a single edit, since uniform non-air
  Bricks are rare outside generation. **Compaction is never automatic**
  (owner, 2026-11-27): neither the Brick nor its owner has the context to
  schedule it, so it runs only when a caller that does asks:
  - **at a save,** when the save asks: encoding a chunk blob visits every
    cell anyway, and the owner swaps the compacted Brick in as its live
    one, so a chunk that stays loaded sheds its dead entries at a save;
  - **at a bulk fill,** which ends with one;
  - **on request:** the owner offers compaction as an explicit operation
    (to the plain low-water mark, or leaving room for more entries), for
    whatever layer comes to schedule it; nothing calls it yet. For
    churning Bricks a caller could use this policy: compact once the palette has gained max(F, α·L₀) entries since the
    last compaction, where L₀ is the live count that compaction found (or
    the palette's size when the Brick was built), F = 4096 and α = 1/4
    (placeholders). F must exceed the set of states a Brick keeps cycling
    through, or compaction thrashes; α spreads a compaction's
    O(cells + entries) cost over at least α·L₀ appends. Between
    compactions the palette holds at most L₀ + max(F, α·L₀) entries. A
    discretionary compaction picks the narrowest width at which the live
    entries plus F fit under the low-water mark, so a churning Brick
    doesn't widen again before its next compaction is due; a compaction
    at a save uses the plain mark, so a quiet Brick isn't forced wide.
    The signal costs one counter, incremented only when an edit appends;
    the owner keeps it, and the caller reads it to decide.
  - **Telemetry:** compactions per Brick (thrash shows as a high rate),
    and palette size against live entries at each save. F may become
    adaptive if the rate ever says so.
- **The reverse lookup belongs to the owner, not the Brick** (state id to
  palette index, which an edit needs to find whether its state is already
  an entry): only the owner's turn edits and no reader uses it, so it
  needs no synchronisation and never sits on a reader's path. A linear
  scan of the palette up to a threshold (8 entries with a scalar scan, 32
  with a SIMD scan over 4-byte entries, measured on the development
  machine; a placeholder); above it, a
  flat linear-probing table of palette indices at load at most ½,
  comparing through the palette.
  - **Sized to the Brick's palette capacity** when built, which is at most
    about twice its entries (the capacity doubles to fit them), so it
    never regrows within one Brick's life (512 B at a capacity of 64
    entries, 1 MiB at 2¹⁷); a swap to another Brick (growth) or a
    compaction drops it, and the next lookup that needs it rebuilds it from the
    palette, in O(entries).
  - **Built on the first edit that needs it, and dropped when the owner
    stops editing** (a chunk freezing or saving), so only Bricks being
    edited carry one.
  - An edit that finds a dead entry for its state reuses it. Writers that
    already know an index (copying a cell within the Brick, one state
    applied across many cells) resolve it once and write indices directly.
- **Generation emits indices into fixed palettes:** each generator (a
  biome's material set, say) declares its palette up front, and generation
  writes indices into it at a width fixed in advance, with no
  deduplication or lookup per cell; bulk fills do the same. The dynamic
  path (append, grow, the reverse lookup) is for world edits and
  transformations afterwards. Fixed-palette entries a Brick never uses are
  dead entries like any other, dropped at the next compaction.
- **Concurrent reads:** a palette-form Brick carries a seqlock-style
  version counter, so other threads read it zero-copy and validate
  (`world/chunk-representation`, `sim/concurrency-model`'s reading chunks).
  Because nothing is reallocated in place, a reader can't reach freed
  memory: a retired Brick is never written after its swap, so a reader
  still holding it sees a consistent but stale snapshot, and only the
  live Brick's in-place cell writes and appends can be read mid-change.
  How much of that each reader tolerates, or validates away, is the
  owner's policy, behind its API.

## Palette entries: block states
- **A block's definition** has one ULID and holds what is true of every
  instance: material properties, symmetries, collision, behaviour, and the
  variant properties it declares. Appearance is a separate binding, so
  texture packs remain possible. How definitions are loaded is a later,
  grammar-based arc (`data/grammar`); until then they stay JSON files.
- **Internal states are invisible to players:** a definition maps each of
  its states to what players meet (what breaking it drops, what picking it
  gives, the name shown), so states that exist for simulation (a reed's
  height classes, a tip at a chunk's edge) all present as the one block.
- **Variant properties** each have a small closed domain: orientation
  (canonicalised under the block's symmetries, `world/block-orientation`),
  lit or unlit, a growth stage.
- **A state hash** identifies each state a block can take, hashed from the
  definition ULID and the canonical values of its own properties: a block
  with 6 canonical orientations and lit or unlit has 12. This generalises
  Minecraft's block states, where each block enumerates its own states, and
  holds up for player-defined blocks, since a new block or property needs
  no global layout, and blocks without a property pay nothing for it.
  - **The hash is cryptographic** (owner, 2026-11-27): BLAKE3 in its
    `derive_key` mode, with a versioned context string, truncated to 128
    bits (`deps/hashing`). States are interned often but not on a hot
    path, and a cryptographic hash removes accidental collisions and
    makes deliberate ones (about 2⁶⁴ work) impractical, even for
    player-defined blocks. A new encoding gets a new context string, so
    old and new hashes never collide.
  - **What is hashed is a canonical text form** (owner, 2026-11-27), also
    the state's reconstruction and its form in logs:
    `<definition ULID>[name=value,...]`, the ULID upper case, properties
    sorted by name, the brackets left out when no property is written.
    Names are `[a-z0-9_]+`. **A property at its default value is left
    out**, so adding a property with a default to an existing definition
    leaves every existing state's hash unchanged. Enumerated values are
    written by name, never by index, so reordering a domain changes no
    hash; booleans are `true` and `false`. Orientation is written so it
    doesn't depend on how the 24 rotations are numbered
    (`world/block-orientation`). The grammar (`data/grammar`) reads
    properties into the same values; this text is the identity, whatever
    authors write.
- **Never persisted without its reconstruction:** a saved Brick or a network message may
  carry state hashes, or a palettized form of them, for speed, but always
  with what is needed to reconstruct each state from definitions (each
  palette entry's definition ULID and named property values, or the hashing
  scheme's key and version), so no information is lost when a block's
  properties change (`data/stable-identity`). The rule limits information
  loss, not the representation.
- **The state table,** one per process and shared by every Brick, holds
  what systems need per state: the state hash, the definition, decoded
  property values (orientation as a column every block has, for generic
  rotation code), visibility class, collision, and six **appearance ids**,
  one per world face (`render/block-textures`), derived from the
  definition, the state and the resource pack rather than part of the
  state's identity.
  - **State ids:** a palette entry is a 32-bit **state id** (`StateId`, a
    distinct type, so it can't be mixed up with a palette index), a dense
    index into the table, assigned when the process first sees the state (a chunk blob
    load, definition loading, a generator registering its palette, a
    player creating a block). Ids are scoped to the process and never
    persisted; state hashes are what is persisted. Reading a state's data
    by id is one array access, read once per palette entry when a Brick is
    meshed or queried, not per cell.
  - **Interning** (state hash to id) is the one hash, used only when a
    state is first seen, never per cell or per edit.
  - **Retrieved ahead of time** (owner, 2026-11-27): now that a state has a
    lookup cost, code resolves the ids it will need before the loop that
    uses them (a tool its states when an action is planned, a generator
    its palette when it starts, a mesher one table row per palette
    entry), and anything per cell, per face or per frame reads the table
    by id only. Moving between states of one definition (another
    orientation, `lit` on) is an array lookup in the definition's dense
    index of its states, not an intern.
  - **Shared across threads:** the table only grows by appending, in
    chunked storage that never moves, so reads by id take no lock;
    interning takes a short lock (sharded if measurement asks), and is
    rare.
  - **Overflow is fatal:** ids are never reused within a process, so
    interning more than 2³² states is a fatal error, in every build, not a
    rollover. Player-created blocks make this reachable in principle on a
    large, long-running server; at worst that is a crash, and a restart
    starts the count again. A telemetry gauge counts interned states.
- **Kept under control:** each definition caps the number of states its
  domains multiply out to at **2¹⁰** (owner, 2026-11-27; checked when
  definitions load, and a definition over it is rejected). That bounds the
  product of orientations and every other property; unique objects (a
  written book) take another path, below. A value that is
  open-ended or timestamped is not part of a state: timing comes from
  closed forms keyed by the state, the coordinates and world time;
  rare rich content (a book's text) is a world-level record that a
  reference property names, the one exception to small closed domains;
  and simulation's own per-cell data is hidden, never persisted
  (`sim/block-metadata`). This, not the id width, is
  what keeps palettes and the state table small.

## What a cell holds by level
- **A block state, at every level** (owner, 2026-11-27): a coarse Brick's
  palette holds block states interned like a full-detail Brick's. What a
  coarse cell shows is a **superclass** block where the stage ladder
  hasn't specialised it yet (generic stone, a soil, a canopy,
  `world/progressive-lod`): superclasses are block definitions like any
  other, with ULIDs, and one that needs no orientation declares none. So
  coarse Bricks persist, replicate and mesh as full-detail ones do, and no
  second vocabulary or palette bound exists for them.

## Invariants (draft)
- **`invariant/brick/owner-only`** — Only a Brick's owner (a chunk or a
  terrain node) reads or writes it, and nothing outside the owner sees its
  cells, indices, widths or palette.
  - **Enforced:** type: the owner holds the Brick privately and its API
    answers semantic queries (`core/opaque-chunk-queries`).
- **`invariant/brick/representation-invisible`** — A Brick's
  representation and width are invisible in its owner's answers: the same content, uniform or as
  a palette at any width, answers every query, digest and mesh
  identically.
  - **Enforced:** test: the rep-swap tests (`world/chunk-meshing`).
- **`invariant/brick/canonical-palette`** — Palette entries are distinct
  and canonical: no two entries of one palette name the same state, and
  every state is canonical under its block's symmetries.
  - **Enforced:** fatal check on insertion.
- **`invariant/brick/index-in-palette`** — No index refers past the
  palette.
  - **Enforced:** fatal check on write; debug audit.
- **`invariant/brick/unique-state-hash`** — No two distinct states share a
  state hash.
  - **Enforced:** fatal check on interning, in every build.
- **`invariant/brick/no-state-id-leaves-process`** — Every state hash
  written to a save or a message travels with what reconstructs its state
  from definitions (each palette entry's definition ULID and named
  property values, or the hashing scheme's key and version), and no state
  id is written to either.
  - **Enforced:** type: the save and message encoders take the state
    table; review.
- **`invariant/brick/edits-never-compact`** — No edit compacts, and no
  edit narrows a Brick.
  - **Enforced:** fatal check in the edit path; test: the representation
    tests.
- **`invariant/brick/state-ids-never-overflow`** — State ids never
  overflow: interning past 2³² is fatal.
  - **Enforced:** fatal check on interning, in every build.
- **`invariant/brick/never-reallocated-in-place`** — A Brick is never
  reallocated in place: its width and palette capacity are fixed for its
  life (the palette is one array allocated at its capacity), every change
  of either is a new Brick swapped in by its owner, and
  an old Brick is freed only after a grace period.
  - **Enforced:** type: no resizing operation exists on the class, and the
    swap path is the only constructor from another Brick.

## Open
- Where coarse Bricks are used (the coarse-to-fine pipeline).
- The live Brick's dirty-read policy per reader.
